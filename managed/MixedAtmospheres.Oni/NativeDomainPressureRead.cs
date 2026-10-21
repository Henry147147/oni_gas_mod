using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Threading;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static long pressureGetterReads, pressureCallSiteReads;
        internal static bool TryReadNativePressure(int cell, bool callSite, out float pressure)
        {
            pressure = 0;
            if (!active || !OwnsPublicCell(cell)) return false;
            // AcquireFrame pins the same immutable reading for Unity texture
            // workers as for the rest of the completed presentation.
            if (!MixturePresentationRuntime.TryGetCell(cell, out var reading) || reading!.ThermodynamicReading == null)
                throw new InvalidOperationException("Owned pressure has no completed physical publication.");
            pressure = NativePressureReadPlan.Kilopascals(reading.ThermodynamicReading.MechanicalPressurePa);
            if (callSite) Interlocked.Increment(ref pressureCallSiteReads);
            else Interlocked.Increment(ref pressureGetterReads);
            return true;
        }
    }

    internal static class NativePressureReadBoundary
    {
        internal static float Read(ref Grid.PressureIndexer indexer, int cell)
            => NativeSealedDomainValidation.TryReadNativePressure(cell, true, out var pressure) ? pressure :
               ColonyOwner.TryReadPressure(cell, out var owned) ? owned : indexer[cell];
    }

    [HarmonyPatch(typeof(Grid.PressureIndexer), "get_Item")]
    internal static class NativePressureGetterPatch
    {
        private static bool Prefix(int i, ref float __result)
        {
            if (!NativeSealedDomainValidation.TryReadNativePressure(i, false, out var pressure) && !ColonyOwner.TryReadPressure(i, out pressure)) return true;
            __result = pressure;
            return false;
        }
    }

    [HarmonyPatch]
    internal static class NativePressureCallSitePatch
    {
        private static readonly MethodInfo Getter = AccessTools.PropertyGetter(typeof(Grid.PressureIndexer), "Item");
        private static readonly MethodInfo Bridge = AccessTools.Method(typeof(NativePressureReadBoundary), nameof(NativePressureReadBoundary.Read));
        internal static int TargetCount { get; private set; }
        internal static void RequireInstalled()
        {
            var getterPrefix = AccessTools.Method(typeof(NativePressureGetterPatch), "Prefix");
            var transpiler = AccessTools.Method(typeof(NativePressureCallSitePatch), nameof(Transpiler));
            if (Harmony.GetPatchInfo(Getter)?.Prefixes.Any(item => item.PatchMethod == getterPrefix) != true ||
                NativePressureReadPlan.CallSites(typeof(Grid).Assembly).Any(method =>
                    Harmony.GetPatchInfo(method)?.Transpilers.Any(item => item.PatchMethod == transpiler) != true))
                throw new InvalidOperationException("A required native pressure reader patch is missing.");
        }
        private static IEnumerable<MethodBase> TargetMethods()
        {
            TargetCount = 0;
            foreach (var method in NativePressureReadPlan.CallSites(typeof(Grid).Assembly))
            {
                if (!NativeMessagePatch.ReadInstructions(method).Any(instruction => instruction.Calls(Getter)))
                    throw new InvalidOperationException("Native pressure call site differs from the inspected build: " + method);
                ++TargetCount;
                yield return method;
            }
        }
        private static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
        {
            foreach (var instruction in instructions)
            {
                if (instruction.Calls(Getter)) { instruction.opcode = OpCodes.Call; instruction.operand = Bridge; }
                yield return instruction;
            }
        }
    }
}
