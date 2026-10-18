using System;
using System.Linq;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool restoredReadersVerified, restoredBreathingOutsideVerified;
        private static long restoredBreathingReads;
        private static BreathingReadSample[] restoredBreathingSamples = Array.Empty<BreathingReadSample>();
        private static object? restoredBreathingSelection;
        private static void VerifyRestoredReaderBindings()
        {
            var method = AccessTools.Method(typeof(GasBreatherFromWorldProvider), "GetBreathableCellMass");
            var patch = AccessTools.Method(typeof(NativeBreathingCellReadPatch), "Prefix");
            Require(Harmony.GetPatchInfo(method)?.Prefixes.Any(item => item.PatchMethod == patch) == true,
                "The restored world breathing reader patch is missing.");
            var before = breathingCellReads;
            restoredBreathingSamples = SampleNativeBreathingCells(false);
            restoredBreathingSelection = SampleNativeBreathingSelection(restoredBreathingSamples);
            restoredBreathingReads = breathingCellReads - before;
            var outsideArgs = new object[] { hotSolid, SimHashes.Vacuum }; before = breathingCellReads;
            var outside = (float)method.Invoke(null, outsideArgs);
            restoredBreathingOutsideVerified = outside == 0 && (SimHashes)outsideArgs[1] == SimHashes.Vacuum && before == breathingCellReads;
            VerifyPressureReadFixture(false);
            restoredReadersVerified = restoredBreathingReads >= 12 && restoredBreathingOutsideVerified && pressureReadVerified &&
                restoredBreathingSamples.Length == OwnedCellCount && restoredBreathingSamples.All(item => item.CompletedPhysicalFrame == (long)latest.CompletedFrames);
            Require(restoredReadersVerified, "Restored physical pressure, oxygen policy or native cell selection was not rebound to the current session.");
        }
        private static object RestoredReadersReport() => new { Required = ColonyRestoreMode, Verified = restoredReadersVerified,
            Source = "actual-native-pressure-and-world-breathing-readers", BreathingReads = restoredBreathingReads,
            NativeBreathingOutsideVerified = restoredBreathingOutsideVerified,
            OxygenElementId = (int)SimHashes.Oxygen, WaterElementId = (int)SimHashes.Water, VacuumElementId = (int)SimHashes.Vacuum,
            Policy = MixturePresentationRuntime.CurrentModel?.BreathingSettings,
            BreathingSamples = restoredBreathingSamples, BreathingSelection = restoredBreathingSelection };
    }
}
