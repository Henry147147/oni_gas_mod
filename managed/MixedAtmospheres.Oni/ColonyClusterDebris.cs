using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Native self-destruct rounds pickupable unit counts before packing them.
    // Fluids use continuous quantities; preserve their original binary32 mass
    // while retaining the game's solid-unit and storage/capacity operations.
    [HarmonyPatch(typeof(CraftModuleInterface), "CompleteSelfDestruct")]
    internal static class ColonyRocketDebrisUnitsPatch
    {
        private static void SetUnits(PrimaryElement item, float rounded)
        {
            var preserve = ColonyClusterWorlds.Enabled && ColonyOwner.Active &&
                           (item.Element.IsGas || item.Element.IsLiquid);
            item.Units = preserve ? item.Units : rounded;
        }
        private static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
        {
            var code = instructions.ToList();
            var setter = AccessTools.PropertySetter(typeof(PrimaryElement), "Units");
            var matches = code.Select((instruction, index) => (instruction, index))
                .Where(pair => pair.instruction.Calls(setter)).Select(pair => pair.index).ToArray();
            if (matches.Length != 1)
                throw new NotSupportedException("Unknown rocket debris unit setter; refusing fluid rounding.");
            var at = matches[0];
            var preceding = code.Take(at).Where(instruction => instruction.operand is MethodInfo)
                .Select(instruction => (MethodInfo)instruction.operand).Reverse().Take(3).Reverse().ToArray();
            var expected = new[] {
                AccessTools.PropertyGetter(typeof(PrimaryElement), "Units"),
                AccessTools.Method(typeof(Mathf), "RoundToInt", new[] { typeof(float) }),
                AccessTools.Method(typeof(Mathf), "Max", new[] { typeof(int), typeof(int) })
            };
            if (!preceding.SequenceEqual(expected))
                throw new NotSupportedException("Unknown rocket debris rounding sequence.");
            code[at] = new CodeInstruction(code[at]) {
                opcode = OpCodes.Call, operand = AccessTools.Method(typeof(ColonyRocketDebrisUnitsPatch), nameof(SetUnits))
            };
            return code;
        }
    }
}
