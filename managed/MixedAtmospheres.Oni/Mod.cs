using System;
using System.Collections.Generic;
using System.IO;
using HarmonyLib;
using MixedAtmospheres.Core;

namespace MixedAtmospheres.Oni
{
    // This assembly is an explicitly labelled integration probe. Its scoped
    // patches intercept only the probe's own calls on the loading thread.
    // It never substitutes a partial solver for an active colony's simulation.
    public sealed class Mod : KMod.UserMod2
    {
        internal static string DirectoryPath { get; private set; } = "";
        // This mod's static ID in ONI's mod list (the Mods screen settings button).
        internal static string StaticId { get; private set; } = "";
        // Development packages (tools/package_probe.py writes development.json)
        // and validation requests (colony-validation.json) keep their reports
        // and contract exports beside the mod, where the harnesses read them.
        // Ordinary play writes nothing into the mod's folder; player data lives
        // in mods/config/MixedAtmospheres.
        internal static bool Development { get; private set; }
        private static IntegrationReport? probeReport;
        private static string ReportPath => Path.Combine(DirectoryPath, "integration-probe.json");
        private const string ProcessClaim = "MixedAtmospheres.RunningCopy";

        // KMod.DLLLoader creates one Harmony instance whose Id is this mod's
        // static ID, passes it to every UserMod2 in the folder (UserMod2.OnLoad
        // is PatchAll(assembly)) and, if OnLoad throws, only logs the exception
        // and marks the DLL content as failed; nothing it already patched is
        // removed. Every exception therefore removes this owner's patches and
        // detaches partially installed runtime hooks before it is reported.
        public override void OnLoad(Harmony harmony)
        {
            // Refusals that come before any patch or native library: the game
            // runs unchanged and the player is told why at the main menu.
            var platformRefusal = ModAdmission.CurrentPlatformRefusal();
            if (platformRefusal != null) { MenuNotices.Post("Mixed Atmospheres is not active", platformRefusal); return; }
            var platform = mod?.label.distribution_platform.ToString() ?? "unknown";
            if (!ClaimProcess(path, platform, out var running))
            {
                var separator = running.IndexOf('|');
                MenuNotices.Post("Mixed Atmospheres is installed twice", ModAdmission.DuplicateNotice(
                    running.Substring(separator + 1), running.Substring(0, separator), path, platform));
                return;
            }
            DirectoryPath = path; StaticId = mod?.staticID ?? "";
            ColonyFrameLog.Install();
            // The game build: every managed dependency, then the native
            // SimDLL. A refused build patches nothing except, when its own
            // targets verify, a guard that explains why mixture saves wait.
            var build = GameBuildCheck.RunningBuild;
            List<string>? differences;
            try { differences = GameBuildCheck.VerifyManaged(path) ?? NativeBuildRefusal(path); }
            catch (Exception error) { differences = new List<string> { "the game could not be checked (" + error.Message + ")" }; }
            if (differences != null)
            {
                UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Game build " + build + " is not supported; nothing is patched. Differences:\n  " + string.Join("\n  ", differences));
                MenuNotices.Post("Mixed Atmospheres is not active", ModAdmission.BuildRefusal(build, differences));
                var contract = GameBuildCheck.Contract;
                if (contract == null && File.Exists(Path.Combine(path, GameContractFile.FileName)))
                    try { GameContractFile.Parse(File.ReadAllText(Path.Combine(path, GameContractFile.FileName))).Platforms.TryGetValue(GameBuildCheck.Platform, out contract); }
                    catch (Exception) { contract = null; }
                RefusedBuildLoadGuard.TryInstall(harmony, contract, build);
                return;
            }
            try
            {
                Development = File.Exists(Path.Combine(path, "development.json")) || File.Exists(Path.Combine(path, "colony-validation.json"));
                var verifiedAssemblyHash = IntegrationProbe.VerifyContract(GameBuildCheck.Contract != null);
                NativeLibraryBootstrap.Load(path);
                NativeBackendObservation.Initialize(path);
                ColonyWindowsLifecycle.Install();
                GasDiffusionOptions.Initialize();
                MixtureOverlays.InitializeStrings();
                GasShareSensorConfig.InitializeStrings();
                ColonyRuntimeValidation.Initialize();
                ModSettingsRuntime.Initialize(); // docs/settings.md; after the validation request is known
                NativeElementTagChecks.WarmNativeCalls();
                UnityEngine.Debug.Log("[Mixed Atmospheres] Original enum tag helpers passed before patching.");
                if (ColonyRuntimeValidation.Settings?.Mode == "load-rejection")
                    OwnedReservoirLoadVisualValidation.Install(harmony);
                base.OnLoad(harmony);
                NativeElementTagChecks.VerifyCalls();
                UnityEngine.Debug.Log("[Mixed Atmospheres] Native enum tag helpers passed.");
                MixtureWorldRendering.Install(harmony);
                OwnedReservoirOwnerMarker.Install(harmony);
                GameBuildCheck.RequireVerifiedPatches(harmony);
                var report = IntegrationProbe.Run(verifiedAssemblyHash);
                var destination = Development ? ReportPath : "Player.log only (ordinary play writes nothing into the mod folder)";
                if (Development) File.WriteAllText(destination, report.ToJson());
                probeReport = report;
                MixtureStatusDisplay.Install();
                ModSettingsScreen.Install();
                UnityEngine.Debug.Log("[Mixed Atmospheres] Integration probe completed. " +
                    (NativeSealedDomainValidation.IsMode ? "A sealed native development domain is enabled; whole-colony ownership is not certified. Report: " :
                    ColonyOwner.Available ? "The colony owner takes the gases and liquids of a new base-game or Spaced Out colony. Report: " :
                    "Full simulation ownership is NOT certified; this package does not change colony physics. Report: ") + destination);
            }
            catch (Exception error)
            {
                var teardown = TearDownFailedLoad(harmony, error);
                UnityEngine.Debug.LogError("[Mixed Atmospheres] Integration probe failed: " + error + "\n" + teardown);
                MenuNotices.Post("Mixed Atmospheres is not active", ModAdmission.StartFailure(error.Message));
                throw;
            }
        }

        // The native library checks the game's SimDLL against its verified
        // profiles; an unsupported image refuses the build like a managed
        // difference. Probing loads only this mod's own library and changes nothing.
        private static List<string>? NativeBuildRefusal(string modDirectory)
        {
            if (!NativeLibraryBootstrap.ObserverAvailable(modDirectory)) return null;
            var managed = Path.GetDirectoryName(typeof(Sim).Assembly.Location) ?? throw new InvalidOperationException("Cannot locate the loaded game assembly.");
            var game = Directory.GetParent(managed)?.Parent?.FullName ?? throw new InvalidOperationException("Cannot locate the loaded game's root.");
            NativeLibraryBootstrap.LoadObserver(modDirectory);
            var refusal = OniNativeObserver.ProbeGame(game);
            return refusal == null ? null : new List<string> { "the game's native simulator (SimDLL): " + refusal };
        }

        // One copy per process. ONI loads every enabled copy: a Dev copy left
        // beside a Workshop subscription would patch the game twice. The claim
        // is process-wide, so it holds even when the copies are different
        // assemblies, and it stays held after a failed load.
        private static bool ClaimProcess(string path, string platform, out string running)
        {
            // KMod loads mods one at a time on the main thread.
            var domain = AppDomain.CurrentDomain;
            running = domain.GetData(ProcessClaim) as string ?? "";
            if (running.Length != 0) return false;
            domain.SetData(ProcessClaim, platform + "|" + path);
            return true;
        }

        private static string TearDownFailedLoad(Harmony harmony, Exception cause)
        {
            probeReport = null;
            var problems = new List<string>();
            void Step(string name, System.Action action)
            {
                try { action(); }
                catch (Exception error) { problems.Add(name + ": " + error.Message); }
            }
            // Never UnpatchAll(null): that removes every other mod's patches.
            var owner = harmony?.Id;
            if (string.IsNullOrEmpty(owner)) problems.Add("Harmony owner is unavailable; patches could not be removed.");
            else
            {
                Step("Harmony unpatch", () => harmony!.UnpatchAll(owner));
                Step("Harmony verification", () =>
                {
                    if (Harmony.HasAnyPatches(owner)) throw new InvalidOperationException("patches owned by " + owner + " remain installed");
                });
            }
            if (ColonyRuntimeValidation.Settings?.Mode == "load-rejection")
                Step("load-rejection observers", () => OwnedReservoirLoadVisualValidation.AbandonFailedLoad(cause));
            Step("native tag verification", NativeElementTagChecks.ResetAfterFailedLoad);
            Step("mixture renderer", MixtureWorldRendering.ResetAfterFailedLoad);
            Step("mixture status display", MixtureStatusDisplay.Remove);
            Step("settings window", ModSettingsScreen.Remove);
            Step("reservoir marker", OwnedReservoirOwnerMarker.ResetAfterFailedLoad);
            Step("overlay status registry", MixtureOverlays.UnregisterStatusItemOverlays);
            Step("native observer", NativeBackendObservation.ResetAfterFailedLoad);
            // The native library stays mapped (see NativeLibraryBootstrap); no
            // managed caller remains once the patches above are removed.
            return problems.Count == 0
                ? "Fail-closed teardown removed every Harmony patch owned by " + owner + "; the mod made no further changes."
                : "Fail-closed teardown INCOMPLETE: " + string.Join("; ", problems);
        }

        // Main-menu diagnostics are recorded beside, never inside, the six
        // integration checks. A write failure leaves the earlier report intact.
        internal static void RecordNativeContractCapture(string status, string error)
        {
            var report = probeReport;
            if (report == null || !Development) return;
            report.NativeContractCapture = status;
            report.NativeContractCaptureError = error;
            var destination = ReportPath;
            var temporary = destination + ".tmp";
            File.WriteAllText(temporary, report.ToJson());
            if (File.Exists(destination)) File.Replace(temporary, destination, null);
            else File.Move(temporary, destination);
        }
    }

    internal static class ProbeScope
    {
        [ThreadStatic] internal static FluidWorld? World;
        [ThreadStatic] internal static int InterceptedFrames;
        [ThreadStatic] internal static int InterceptedSnapshots;
        [ThreadStatic] internal static int InterceptedDirectMessages;
        [ThreadStatic] internal static int InterceptedSaves;
    }
}
