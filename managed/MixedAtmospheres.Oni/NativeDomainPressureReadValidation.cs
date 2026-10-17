using System;
using System.Linq;
using System.Threading;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool LiquidPressureReadMode => ColonyRuntimeValidation.Settings?.Mode == "native-liquid-pressure-read-domain";
        private static bool PressureReadMode => LiquidPressureReadMode || ColonyRuntimeValidation.Settings?.Mode == "native-pressure-read-domain";
        private static bool pressureReadVerified, pressureOutsideVerified, pressureCallSitesVerified;
        private static object[] pressureReadSamples = Array.Empty<object>();
        private static object? pressureOutsideSample;
        private static bool pressureDistinctScalarRequired;
        private static void VerifyPressureReadFixture(bool requireDistinctScalar = true)
        {
            pressureDistinctScalarRequired = requireDistinctScalar;
            NativePressureCallSitePatch.RequireInstalled(); pressureCallSitesVerified = true;
            var getter = AccessTools.PropertyGetter(typeof(Grid.PressureIndexer), "Item");
            var view = MixturePresentationRuntime.AcquireFrame()
                ?? throw new InvalidOperationException("The pressure control has no completed presentation.");
            pressureReadSamples = Enumerable.Range(0, cells.Length).Select(index => {
                var global = Grid.XYToCell(x + index % Width, y + index / Width);
                Require(view.TryGetCell(global, out var reading) && reading!.ThermodynamicReading != null,
                    "A native pressure control cell has no physical reading.");
                var source = reading!.ThermodynamicReading!;
                var actual = (float)getter.Invoke(Grid.Pressure, new object[] { global });
                var expected = (float)(source.MechanicalPressurePa / 1000);
                var legacy = Grid.Mass[global] * 101.3f;
                Require(actual == expected && actual > 0 && (!requireDistinctScalar || Math.Abs(actual - legacy) > 1),
                    "Native pressure still uses the scalar mass approximation instead of completed physical pressure.");
                return (object)new { PublicCell = global, CompletedPhysicalFrame = reading.Revision,
                    PhysicalPressurePa = source.MechanicalPressurePa, GasPressurePa = source.GasPressurePa,
                    NativeKilopascals = actual, LegacyScalarKilopascals = legacy, ScalarMassKg = Grid.Mass[global] };
            }).ToArray();
            var before = Interlocked.Read(ref pressureGetterReads);
            var outside = (float)getter.Invoke(Grid.Pressure, new object[] { hotSolid });
            var expectedOutside = Grid.Mass[hotSolid] * 101.3f;
            var actualBits = BitConverter.SingleToInt32Bits(outside);
            var expectedBits = BitConverter.SingleToInt32Bits(expectedOutside);
            var after = Interlocked.Read(ref pressureGetterReads);
            pressureOutsideVerified = actualBits == expectedBits && before == after;
            pressureOutsideSample = new { PublicCell = hotSolid, ScalarMassKg = Grid.Mass[hotSolid],
                NativeKilopascals = outside, ExpectedScalarKilopascals = expectedOutside,
                NativeBits = actualBits, ExpectedBits = expectedBits, GetterReadsBefore = before, GetterReadsAfter = after };
            pressureReadVerified = pressureOutsideVerified && NativePressureCallSitePatch.TargetCount == 5 &&
                Interlocked.Read(ref pressureGetterReads) >= cells.Length && (Liquid || Interlocked.Read(ref pressureCallSiteReads) > 0);
            Require(pressureReadVerified, "Actual native pressure readers, worker call sites or outside ownership failed.");
        }
        private static object PressureReadReport() => new { Required = PressureReadMode || ColonyRestoreMode, Verified = pressureReadVerified,
            DistinctFromLegacyScalarRequired = pressureDistinctScalarRequired,
            Source = "Grid.PressureIndexer.get_Item", Units = "kPa-from-completed-mechanical-Pa",
            GetterReads = Interlocked.Read(ref pressureGetterReads), GameCallSiteReads = Interlocked.Read(ref pressureCallSiteReads),
            PatchedGameCallSites = NativePressureCallSitePatch.TargetCount, NativeOutsideVerified = pressureOutsideVerified,
            CallSitePatchesVerified = pressureCallSitesVerified,
            Outside = pressureOutsideSample, Samples = pressureReadSamples };
    }
}
