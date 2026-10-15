using System;
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
        public override void OnLoad(Harmony harmony)
        {
            try
            {
                DirectoryPath = path;
                var verifiedAssemblyHash = IntegrationProbe.VerifyContract();
                NativeLibraryBootstrap.Load(path);
                MixtureAppearanceSettings.Initialize();
                MixtureOverlays.InitializeStrings();
                ColonyRuntimeValidation.Initialize();
                base.OnLoad(harmony);
                MixtureWorldRendering.Install(harmony);
                var report = IntegrationProbe.Run(verifiedAssemblyHash);
                var destination = Path.Combine(path, "integration-probe.json");
                File.WriteAllText(destination, report.ToJson());
                UnityEngine.Debug.Log("[Mixed Atmospheres] Integration probe completed. " +
                    "Full simulation ownership is NOT certified; this package does not change colony physics. Report: " + destination);
            }
            catch (Exception error)
            {
                UnityEngine.Debug.LogError("[Mixed Atmospheres] Integration probe failed: " + error);
                throw;
            }
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
