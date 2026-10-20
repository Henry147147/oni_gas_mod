using System;
using System.Linq;
using HarmonyLib;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool BreathingReadMode => ColonyRuntimeValidation.Settings?.Mode == "native-breathing-read-domain";
        private static bool breathingReadStarted, breathingReadVerified, breathingNativeOutsideVerified;
        private static BreathingReadSample[] breathingBefore = Array.Empty<BreathingReadSample>(), breathingAfter = Array.Empty<BreathingReadSample>();
        private static object? breathingSelectionBefore, breathingSelectionAfter;
        private static int breathingBreatherInstance;
        private sealed class BreathingReadSample
        {
            public int PublicCell;
            public long CompletedPhysicalFrame;
            public double OxygenMassKg, ScalarGridMassKg, OxygenMoleFraction, OxygenPartialPressurePa, CarbonDioxideMoleFraction, CarbonDioxidePartialPressurePa;
            public float AdvertisedOxygenMassKg;
            public int ScalarElement, AdvertisedElement;
            public BreathingFailure Failures;
            public bool Breathable;
        }
        private static BreathingReadSample[] SampleNativeBreathingCells(bool requireMixtureDistinct = true)
        {
            var method = AccessTools.Method(typeof(GasBreatherFromWorldProvider), "GetBreathableCellMass");
            var model = MixturePresentationRuntime.CurrentModel ?? throw new InvalidOperationException("Missing native breathing policy.");
            return publishedReadings!.Select(item => {
                var reading = model.Describe(item.Value);
                var args = new object[] { item.Key, SimHashes.Vacuum };
                var actual = (float)method.Invoke(null, args);
                var oxygen = reading.Constituents.SingleOrDefault(item => item.MaterialId == model.OxygenMaterialId)?.MassKg ?? 0;
                var sample = new BreathingReadSample { PublicCell = item.Key, CompletedPhysicalFrame = reading.Revision,
                    OxygenMassKg = oxygen, AdvertisedOxygenMassKg = actual, ScalarGridMassKg = Grid.Mass[item.Key],
                    ScalarElement = (int)Grid.Element[item.Key].id, AdvertisedElement = (int)(SimHashes)args[1],
                    OxygenMoleFraction = reading.OxygenMoleFraction, OxygenPartialPressurePa = reading.OxygenPartialPressurePa,
                    CarbonDioxideMoleFraction = reading.Co2MoleFraction, CarbonDioxidePartialPressurePa = reading.Co2PartialPressurePa,
                    Failures = reading.Failures, Breathable = reading.IsBreathable };
                var next = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(actual) + 1);
                Require(actual == 0 && (SimHashes)args[1] == SimHashes.Vacuum && !reading.IsBreathable ||
                    actual > 0 && actual <= oxygen && next > oxygen && reading.IsBreathable && (SimHashes)args[1] == SimHashes.Oxygen,
                    "The actual native breathing helper disagrees with the completed mixture oxygen or policy.");
                var index = item.Key % Grid.WidthInCells - x + (item.Key / Grid.WidthInCells - y) * Width;
                var expectedElement = ConfiguredVacuumMode ? PublishedElementAt(index) : Liquid ? SimHashes.Water : SimHashes.Oxygen;
                Require(Grid.Element[item.Key].id == expectedElement && (!requireMixtureDistinct || Grid.Mass[item.Key] > oxygen),
                    "The breathing reader's scalar identity or mixture control differs from its publication.");
                return sample;
            }).ToArray();
        }
        private static object SampleNativeBreathingSelection(BreathingReadSample[] samples)
        {
            var breather = UnityEngine.Object.FindFirstObjectByType<OxygenBreather>()
                ?? throw new InvalidOperationException("No actual duplicant oxygen breather is present.");
            breathingBreatherInstance = breather.GetInstanceID();
            var centre = Grid.XYToCell(x + 1, y);
            var result = GasBreatherFromWorldProvider.GetBestBreathableCellAroundSpecificCell(centre, null, breather, out var total);
            // The native search reads only its default offsets around the centre:
            // every owned cell of the six-cell room, a corner of a larger one.
            var scope = GasBreatherFromWorldProvider.DEFAULT_BREATHABLE_OFFSETS.Select(offset => Grid.OffsetCell(centre, offset)).ToArray();
            var searched = samples.Where(item => scope.Contains(item.PublicCell)).ToArray();
            Require(searched.Length == scope.Length, "The breathing search region left the owned fluid cells.");
            var expected = searched.Where(item => item.AdvertisedOxygenMassKg > breather.noOxygenThreshold)
                .OrderByDescending(item => item.AdvertisedOxygenMassKg).FirstOrDefault();
            Require(result.IsBreathable == (expected != null) && result.Mass == (expected?.AdvertisedOxygenMassKg ?? 0) &&
                (expected == null ? result.ElementID == SimHashes.Vacuum : result.ElementID == SimHashes.Oxygen &&
                    searched.Any(item => item.PublicCell == result.Cell && item.AdvertisedOxygenMassKg == expected.AdvertisedOxygenMassKg)) &&
                Math.Abs(total - searched.Sum(item => (double)item.AdvertisedOxygenMassKg)) <= 1e-6,
                "Native breathing selection did not use physical available oxygen.");
            return new { result.Cell, result.ElementID, result.Mass, result.IsBreathable, TotalBreathableMass = total,
                BreatherInstanceId = breathingBreatherInstance, NoOxygenThreshold = breather.noOxygenThreshold, SearchedCells = scope };
        }
        private static void StartBreathingReadFixture()
        {
            if (breathingReadStarted) return;
            breathingBefore = SampleNativeBreathingCells();
            Require(breathingBefore.Length == cells.Length && breathingBefore.All(item => item.Breathable),
                "The initial native breathing control is not ordinary breathable mixed air.");
            breathingSelectionBefore = SampleNativeBreathingSelection(breathingBefore);
            var method = AccessTools.Method(typeof(GasBreatherFromWorldProvider), "GetBreathableCellMass");
            var args = new object[] { hotSolid, SimHashes.Vacuum }; var before = breathingCellReads;
            var nativeMass = (float)method.Invoke(null, args);
            breathingNativeOutsideVerified = nativeMass == 0 && (SimHashes)args[1] == SimHashes.Vacuum && before == breathingCellReads;
            Require(breathingNativeOutsideVerified, "An outside native solid entered the owned breathing reader.");
            breathingReadStarted = true;
            SimMessages.ModifyMass(target, .125f, 255, 0, CellEventLogger.Instance.CO2ManagerFixedUpdate, 310, SimHashes.CarbonDioxide);
        }
        private static void VerifyBreathingReadFixture()
        {
            breathingAfter = SampleNativeBreathingCells();
            breathingSelectionAfter = SampleNativeBreathingSelection(breathingAfter);
            var co2 = BreathingFailure.CarbonDioxideFractionHigh | BreathingFailure.CarbonDioxidePartialPressureHigh;
            breathingReadVerified = breathingReadStarted && breathingNativeOutsideVerified && breathingCellReads >= 24 &&
                breathingAfter.Any(item => !item.Breathable && (item.Failures & co2) != 0 && item.AdvertisedOxygenMassKg == 0) &&
                breathingAfter.All(item => item.CompletedPhysicalFrame == (long)renderedRevision) &&
                cellMassEditsRouted == 1 && cellMassEditsAvoidedNativeDispatch && receiptsPublished == 1 &&
                transferStatus.AcceptedBatches == 1 && transferStatus.CommittedBatches == 1 &&
                transferStatus.PendingBatches == 0 && transferStatus.UnacknowledgedReceipts == 0 && transferStatus.MassChangesKg[4] == .125;
            Require(breathingReadVerified, "Actual native breathing selection did not reject the owned CO2 atmosphere.");
        }
        private static object BreathingReadReport() => new { Required = BreathingReadMode, Started = breathingReadStarted,
            Verified = breathingReadVerified, Source = "GasBreatherFromWorldProvider.GetBestBreathableCellAroundSpecificCell",
            OxygenElementId = (int)SimHashes.Oxygen, VacuumElementId = (int)SimHashes.Vacuum,
            Policy = MixturePresentationRuntime.CurrentModel?.BreathingSettings,
            NativeOutsideVerified = breathingNativeOutsideVerified, Reads = breathingCellReads,
            Before = breathingBefore, After = breathingAfter, SelectionBefore = breathingSelectionBefore, SelectionAfter = breathingSelectionAfter,
            NativeDispatchAvoided = cellMassEditsAvoidedNativeDispatch };
    }
}
