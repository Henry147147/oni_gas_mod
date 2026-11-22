using System;
using System.Collections.Generic;
using System.Reflection.Emit;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    [HarmonyPatch(typeof(GasAndLiquidConsumerMonitor.Instance), "OnMassConsumed")]
    internal static class ColonyCritterConsumption
    {
        // A pending draw survives a save. The critter's most recent forage
        // target is transient and may also change before a live draw settles.
        private static Element ConsumedElement(GasAndLiquidConsumerMonitor.Instance monitor, Sim.MassConsumedCallback receipt)
            => ColonyOwner.Active ? ElementLoader.elements[receipt.elemIdx] : monitor.GetTargetElement();

        private static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
        {
            var target = AccessTools.Field(typeof(GasAndLiquidConsumerMonitor.Instance), "targetElement");
            var consumed = AccessTools.Method(typeof(ColonyCritterConsumption), nameof(ConsumedElement));
            var reads = 0;
            foreach (var instruction in instructions)
            {
                if (!instruction.LoadsField(target))
                {
                    yield return instruction;
                    continue;
                }
                // Keep the monitor on the stack and any branch labels on the
                // first instruction. The callback already carries the element.
                instruction.opcode = OpCodes.Ldarg_1;
                instruction.operand = null;
                yield return instruction;
                yield return new CodeInstruction(OpCodes.Call, consumed);
                ++reads;
            }
            if (reads != 3)
                throw new InvalidOperationException($"Expected three critter diet and storage element reads, found {reads}.");
        }
    }
}
