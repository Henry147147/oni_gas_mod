using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Is this game build one the mod can run on (docs/game-update-resilience.md)?
    // Checked in Mod.OnLoad before any patch: the managed game contract
    // shipped beside the DLLs, then the native library's own SimDLL check.
    // A package without a contract (older development flows) keeps the
    // original exact-build check in IntegrationProbe.VerifyContract.
    internal static class GameBuildCheck
    {
        // Embedded from the references used to compile this adapter, also checked by the packager.
        internal const int CompiledBuild = (int)KleiVersion.ChangeList;
        internal static string Platform => RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "windows-x64" : "linux-x64";
        internal static GameContractPlatform? Contract { get; private set; }
        internal static int CheckedEntries { get; private set; }
        internal static double Milliseconds { get; private set; }

        // The running game's build. KleiVersion.ChangeList is a constant, so
        // it is read through reflection: a compiled reference would keep the
        // build this mod was compiled against.
        internal static int RunningBuild
        {
            get
            {
                try { return Convert.ToInt32(typeof(KleiVersion).GetField("ChangeList", BindingFlags.Public | BindingFlags.Static)?.GetRawConstantValue()); }
                catch (Exception) { return 0; }
            }
        }

        // This copy's version, as the packages write it into mod_info.yaml.
        internal static string ModVersion
        {
            get
            {
                var version = typeof(GameBuildCheck).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "";
                var metadata = version.IndexOf('+');
                return metadata < 0 ? version : version.Substring(0, metadata);
            }
        }

        // Null when the managed side is verified (or no contract ships);
        // otherwise why the mod stays off, by cause (ModAdmission.GameRefusal).
        internal static ModAdmission.GameRefusal? VerifyManaged(string modDirectory)
        {
            Contract = null; OtherModPatches = Array.Empty<string>();
            var file = Path.Combine(modDirectory, GameContractFile.FileName);
            if (!File.Exists(file))
            {
                if (File.Exists(Path.Combine(modDirectory, "development.json")) && RunningBuild == 744825 && CompiledBuild == 744825)
                    return null;
                return ModAdmission.Installation("the game contract is missing");
            }
            var started = System.Diagnostics.Stopwatch.StartNew();
            GameContractFile contract;
            try { contract = GameContractFile.Parse(File.ReadAllText(file)); }
            catch (Exception error) { return ModAdmission.Installation("the mod's game contract cannot be read: " + error.Message); }
            var module = typeof(GameBuildCheck).Assembly.ManifestModule.ModuleVersionId.ToString("D");
            if (!string.Equals(contract.ModuleVersionId, module, StringComparison.OrdinalIgnoreCase))
                return ModAdmission.Installation("the mod's game contract belongs to a different build of the mod");
            if (!contract.Platforms.TryGetValue(Platform, out var platform))
                return ModAdmission.Installation("this package has no game contract for " + Platform);
            if (platform.GameBuild != CompiledBuild)
                return ModAdmission.Installation($"the packaged contract is for build {platform.GameBuild}, but the adapter targets {CompiledBuild}");
            if (platform.GameBuild != RunningBuild)
                return ModAdmission.UnsupportedBuild(ModVersion, RunningBuild, platform.GameBuild);
            var modPatched = new List<GameContractCheck.ModPatched>();
            var differences = GameContractCheck.Verify(platform, typeof(GameBuildCheck).Assembly, out var checkedEntries, modPatched);
            CheckedEntries = checkedEntries; Milliseconds = started.Elapsed.TotalMilliseconds;
            var refusal = Admit(RunningBuild, differences, modPatched, out var otherModPatches);
            if (refusal != null) return refusal;
            Contract = platform; OtherModPatches = otherModPatches;
            UnityEngine.Debug.Log($"[Mixed Atmospheres] Game build {RunningBuild} verified: {checkedEntries} managed dependencies match the game contract " +
                $"(written against build {platform.GameBuild}) in {Milliseconds:F0} ms." + ModAdmission.OtherModPatchesLog(otherModPatches));
            return null;
        }

        // Game methods the mod uses that carry other mods' Harmony patches on
        // verified code, one line each ("Type.Method <- Mod title (Harmony:
        // id)"); empty when there are none or the build is not verified.
        internal static IReadOnlyList<string> OtherModPatches { get; private set; } = Array.Empty<string>();

        // Contract differences on a supported build number. When every one
        // is another mod's patch mark on verified code, the build is
        // verified and the mod runs beside those patches (user request
        // 2026-11-26, docs/game-update-resilience.md#other-mods): null, with
        // the patched methods in `otherModPatches`. Anything else, alone or
        // beside patch marks, means the game's code is not this build's.
        internal static ModAdmission.GameRefusal? Admit(int build, List<string> differences, List<GameContractCheck.ModPatched> modPatched, out string[] otherModPatches)
        {
            otherModPatches = Array.Empty<string>();
            if (modPatched.Count != differences.Count) return ModAdmission.GameFiles(build, differences);
            // One line per patched method: the contract can hold a method as
            // a call, as a name looked up and as a patch target at once.
            var methods = new SortedDictionary<string, MethodBase>(StringComparer.Ordinal);
            foreach (var method in modPatched.SelectMany(part => part.Methods))
                methods[GameContractCanon.Type(method.DeclaringType).Replace('/', '.') + "." + method.Name] = method;
            otherModPatches = methods.Select(method => method.Key + " <- " + string.Join(", ", PatchingMods(method.Value)) +
                " (Harmony: " + string.Join(", ", Harmony.GetPatchInfo(method.Value)?.Owners ?? Enumerable.Empty<string>()) + ")").ToArray();
            return null;
        }

        // The mods whose patches Harmony reports on this method, by the name
        // the game's mod list shows.
        private static string[] PatchingMods(MethodBase method)
        {
            var names = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
            var info = Harmony.GetPatchInfo(method);
            if (info != null)
            {
                foreach (var patch in info.Prefixes.Concat(info.Postfixes).Concat(info.Transpilers).Concat(info.Finalizers))
                    names.Add(ModName(patch.PatchMethod?.DeclaringType?.Assembly, patch.owner));
                if (names.Count == 0) foreach (var owner in info.Owners) names.Add(ModName(null, owner));
            }
            return names.Count != 0 ? names.ToArray() : new[] { "an unnamed mod" };
        }

        // A mod's title for the assembly that holds its patch (a mod's own
        // Harmony ID is its choice and need not say which mod it is); without
        // one, the assembly's name or the Harmony ID.
        private static string ModName(Assembly? assembly, string owner)
        {
            var fallback = assembly?.GetName().Name ?? owner;
            try { return InstalledModTitle(assembly, owner) ?? fallback; }
            catch (Exception) { return fallback; }
        }

        // Apart, so that a game without these members fails here and not in the caller.
        [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
        private static string? InstalledModTitle(Assembly? assembly, string owner)
        {
            var mods = Global.Instance?.modManager?.mods;
            if (mods == null) return null;
            string location;
            try { location = assembly == null ? "" : Path.GetFullPath(assembly.Location); }
            catch (Exception) { location = ""; }
            foreach (var mod in mods)
            {
                if (mod == null || string.IsNullOrEmpty(mod.title)) continue;
                if (location.Length != 0 && !string.IsNullOrEmpty(mod.ContentPath))
                {
                    var root = Path.GetFullPath(mod.ContentPath).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
                    if (location.StartsWith(root, StringComparison.OrdinalIgnoreCase)) return mod.title;
                }
                else if (location.Length == 0 && mod.staticID == owner) return mod.title;
            }
            return null;
        }

        // After patching: every original this mod's Harmony owner patched must
        // be a verified target. Validation-only scenes that patch extra
        // originals are development runs and are exempt.
        internal static void RequireVerifiedPatches(Harmony harmony)
        {
            if (Contract == null || ColonyRuntimeValidation.Settings?.Mode == "load-rejection") return;
            var extra = GameContractCheck.UnverifiedPatches(Contract, harmony.Id);
            if (extra.Count != 0)
                throw new NotSupportedException("Patched game methods outside the verified game contract: " + string.Join("; ", extra.Take(5)) +
                    (extra.Count > 5 ? $" and {extra.Count - 5} more" : ""));
        }
    }

    // When the mod is refused, mixture saves still get a clear message
    // instead of reaching ONI's native loader, but only through load entry
    // points whose own code the contract verifies. Nothing else is patched.
    internal static class RefusedBuildLoadGuard
    {
        private static string refusal = "";

        internal static void TryInstall(Harmony harmony, GameContractPlatform? contract, ModAdmission.GameRefusal reason)
        {
            if (contract == null) return;
            refusal = reason.LoadRefusal;
            var targets = new[] { AccessTools.Method(typeof(LoadScreen), nameof(LoadScreen.DoLoad), new[] { typeof(string) }), AccessTools.Method(typeof(MainMenu), "ResumeGame") };
            var verified = contract.Entries.Where(e => e.Kind == GameContractScope.Kind_Patched).ToDictionary(e => e.Key, e => e.Value, StringComparer.Ordinal);
            try
            {
                if (targets.Any(t => t == null || !verified.TryGetValue(GameContractCanon.Method(t), out var body) ||
                    body != GameContractCanon.Compact(GameContractCanon.Body(t)) && body != GameContractCanon.Body(t))) return;
                harmony.Patch(targets[0], prefix: new HarmonyMethod(typeof(RefusedBuildLoadGuard), nameof(LoadPrefix)));
                harmony.Patch(targets[1], prefix: new HarmonyMethod(typeof(RefusedBuildLoadGuard), nameof(ResumePrefix)));
                UnityEngine.Debug.Log("[Mixed Atmospheres] Only the verified save-load guard is installed.");
            }
            catch (Exception error)
            {
                try { harmony.UnpatchAll(harmony.Id); } catch (Exception) { }
                UnityEngine.Debug.LogWarning("[Mixed Atmospheres] The save-load guard was not installed: " + error.Message);
            }
        }

        private static bool LoadPrefix(string __0) => !Refuse(__0);

        private static bool ResumePrefix()
        {
            string? path = null;
            try { path = KPlayerPrefs.HasKey("AutoResumeSaveFile") ? KPlayerPrefs.GetString("AutoResumeSaveFile") : SaveLoader.GetLatestSaveForCurrentDLC(); }
            catch (Exception) { return true; }
            if (string.IsNullOrEmpty(path) || !Refuse(path!)) return true;
            if (KPlayerPrefs.HasKey("AutoResumeSaveFile")) KPlayerPrefs.DeleteKey("AutoResumeSaveFile");
            return false;
        }

        private static bool Refuse(string path)
        {
            if (!ColonyOwnerPersistence.IsMixtureSave(path)) return false;
            ColonyLoadPrecheckPatch.Refuse(refusal);
            return true;
        }
    }
}
