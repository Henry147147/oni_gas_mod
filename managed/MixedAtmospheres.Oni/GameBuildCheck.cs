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

        // Null when the managed side is verified (or no contract ships);
        // otherwise every difference, in plain words.
        internal static List<string>? VerifyManaged(string modDirectory)
        {
            Contract = null;
            var file = Path.Combine(modDirectory, GameContractFile.FileName);
            if (!File.Exists(file)) return null;
            var started = System.Diagnostics.Stopwatch.StartNew();
            GameContractFile contract;
            try { contract = GameContractFile.Parse(File.ReadAllText(file)); }
            catch (Exception error) { return new List<string> { "the mod's game contract cannot be read (" + error.Message + "); reinstall the mod" }; }
            var module = typeof(GameBuildCheck).Assembly.ManifestModule.ModuleVersionId.ToString("D");
            if (!string.Equals(contract.ModuleVersionId, module, StringComparison.OrdinalIgnoreCase))
                return new List<string> { "the mod's game contract belongs to a different build of the mod; reinstall the mod" };
            if (!contract.Platforms.TryGetValue(Platform, out var platform))
                return new List<string> { "this package has no game contract for " + Platform };
            var differences = GameContractCheck.Verify(platform, typeof(GameBuildCheck).Assembly, out var checkedEntries);
            CheckedEntries = checkedEntries; Milliseconds = started.Elapsed.TotalMilliseconds;
            if (differences.Count != 0) return differences;
            Contract = platform;
            UnityEngine.Debug.Log($"[Mixed Atmospheres] Game build {RunningBuild} verified: {checkedEntries} managed dependencies match the game contract " +
                $"(written against build {platform.GameBuild}) in {Milliseconds:F0} ms.");
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

    // When the build is refused, mixture saves still get a clear message
    // instead of reaching ONI's native loader, but only through load entry
    // points whose own code the contract verifies. Nothing else is patched.
    internal static class RefusedBuildLoadGuard
    {
        private static string build = "";

        internal static void TryInstall(Harmony harmony, GameContractPlatform? contract, int runningBuild)
        {
            if (contract == null) return;
            build = runningBuild.ToString();
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
            ColonyLoadPrecheckPatch.Refuse(ModAdmission.RefusedBuildLoad(build));
            return true;
        }
    }
}
