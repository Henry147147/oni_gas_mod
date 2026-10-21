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
        private static IntegrationReport? probeReport;
        private static string ReportPath => Path.Combine(DirectoryPath, "integration-probe.json");

        // KMod.DLLLoader creates one Harmony instance whose Id is this mod's
        // static ID, passes it to every UserMod2 in the folder (UserMod2.OnLoad
        // is PatchAll(assembly)) and, if OnLoad throws, only logs the exception
        // and marks the DLL content as failed; nothing it already patched is
        // removed. Every exception therefore removes this owner's patches and
        // detaches partially installed runtime hooks before it is reported.
        public override void OnLoad(Harmony harmony)
        {
            try
            {
                DirectoryPath = path;
                var verifiedAssemblyHash = IntegrationProbe.VerifyContract();
                NativeLibraryBootstrap.Load(path);
                NativeBackendObservation.Initialize(path);
                MixtureAppearanceSettings.Initialize();
                GasDiffusionOptions.Initialize();
                MixtureOverlays.InitializeStrings();
                OniNitrogenElements.InitializeStrings();
                ColonyRuntimeValidation.Initialize();
                NitrogenElementTagPatch.WarmNativeCalls();
                UnityEngine.Debug.Log("[Mixed Atmospheres] Original enum tag helpers passed before patching.");
                if (ColonyRuntimeValidation.Settings?.Mode == "load-rejection")
                    OwnedReservoirLoadVisualValidation.Install(harmony);
                base.OnLoad(harmony);
                NitrogenElementTagPatch.VerifyCalls();
                UnityEngine.Debug.Log("[Mixed Atmospheres] Native enum tag helpers and nitrogen aliases passed.");
                MixtureWorldRendering.Install(harmony);
                OwnedReservoirOwnerMarker.Install(harmony);
                var report = IntegrationProbe.Run(verifiedAssemblyHash);
                var destination = ReportPath;
                File.WriteAllText(destination, report.ToJson());
                probeReport = report;
                MixtureStatusDisplay.Install();
                UnityEngine.Debug.Log("[Mixed Atmospheres] Integration probe completed. " +
                    (NativeSealedDomainValidation.IsMode ? "A sealed native development domain is enabled; whole-colony ownership is not certified. Report: " :
                    ColonyOwner.Available ? "The colony owner takes the gases and liquids of a new base-game colony. Report: " :
                    "Full simulation ownership is NOT certified; this package does not change colony physics. Report: ") + destination);
            }
            catch (Exception error)
            {
                var teardown = TearDownFailedLoad(harmony, error);
                UnityEngine.Debug.LogError("[Mixed Atmospheres] Integration probe failed: " + error + "\n" + teardown);
                throw;
            }
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
            Step("nitrogen tag verification", NitrogenElementTagPatch.ResetAfterFailedLoad);
            Step("mixture renderer", MixtureWorldRendering.ResetAfterFailedLoad);
            Step("mixture status display", MixtureStatusDisplay.Remove);
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
            if (report == null) return;
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
