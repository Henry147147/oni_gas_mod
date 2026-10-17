using System;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static long breathingCellReads;
        internal static bool TryReadBreathingCell(int cell, out float mass, out SimHashes element)
        {
            mass = 0; element = SimHashes.Vacuum;
            if (!active || !OwnsPublicCell(cell)) return false;
            ++breathingCellReads;
            // A failed owner cannot fall back to its oxygen compatibility cell.
            if (failed) return true;
            try
            {
                if (publishedReadings == null || !publishedReadings.TryGetValue(cell, out var source))
                    throw new InvalidOperationException("The owned breathing cell has no completed physical reading.");
                var model = MixturePresentationRuntime.CurrentModel
                    ?? throw new InvalidOperationException("The owned breathing model is unavailable.");
                mass = MixtureBreathingCellRead.OxygenMass(model.Describe(source), model.OxygenMaterialId);
                if (mass > 0) element = SimHashes.Oxygen;
                return true;
            }
            catch (Exception caught) { Fail(caught); throw; }
        }
    }

    [HarmonyPatch(typeof(GasBreatherFromWorldProvider), "GetBreathableCellMass")]
    internal static class NativeBreathingCellReadPatch
    {
        private static bool Prefix(int cell, ref SimHashes elementID, ref float __result)
        {
            if (!NativeSealedDomainValidation.TryReadBreathingCell(cell, out var mass, out var element)) return true;
            __result = mass; elementID = element;
            return false;
        }
    }
}
