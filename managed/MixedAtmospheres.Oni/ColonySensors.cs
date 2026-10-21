using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Automation on the owned asteroid (docs/colony-owner.md#sensors). An
    // element sensor is true when its element is present above the trace
    // threshold: at least 0.1% of the mass of its phase in the cell. Elsewhere
    // the sensor keeps ONI's comparison with the cell's single element.
    internal static class ColonySensors
    {
        internal const double TraceFraction = 1e-3;

        internal static bool Present(int cell, int element)
        {
            if (!ColonyOwner.Active || !ColonyOwner.OwnsFluidCell(cell) || element < 0 || element >= ElementLoader.elements.Count)
                return Grid.ElementIdx[cell] == element;
            var wanted = ElementLoader.elements[element];
            if (!wanted.IsGas && !wanted.IsLiquid) return Grid.ElementIdx[cell] == element;
            var count = ColonyOwner.Composition(cell, out var indices, out var masses);
            double phase = 0, mass = 0;
            for (var k = 0; k < count; ++k)
            {
                var e = ElementLoader.elements[(int)indices[k]];
                if (e.IsGas != wanted.IsGas) continue;
                phase += masses[k];
                if (indices[k] == element) mass += masses[k];
            }
            return mass > 0 && mass >= TraceFraction * phase;
        }
    }

    // Replaces "Grid.ElementIdx[cell] == desiredElementIdx" in the sampler.
    [HarmonyPatch(typeof(LogicElementSensor), "Sim200ms")]
    internal static class ColonyElementSensorPatch
    {
        private static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
        {
            var list = new List<CodeInstruction>(instructions);
            var elementIdx = AccessTools.Field(typeof(Grid), nameof(Grid.ElementIdx));
            var desired = AccessTools.Field(typeof(LogicElementSensor), "desiredElementIdx");
            var present = AccessTools.Method(typeof(ColonySensors), nameof(ColonySensors.Present));
            for (var i = 0; i + 5 < list.Count; ++i)
            {
                // ldsflda Grid.ElementIdx; ldloc cell; call get_Item; ldarg.0; ldfld desiredElementIdx; ceq
                if (!(list[i].opcode == OpCodes.Ldsflda && Equals(list[i].operand, elementIdx)) || list[i + 2].opcode != OpCodes.Call ||
                    list[i + 3].opcode != OpCodes.Ldarg_0 || !(list[i + 4].opcode == OpCodes.Ldfld && Equals(list[i + 4].operand, desired)) ||
                    list[i + 5].opcode != OpCodes.Ceq) continue;
                var cell = list[i + 1]; var first = new CodeInstruction(cell.opcode, cell.operand);
                list[i].MoveLabelsTo(first);
                list.RemoveRange(i, 6);
                list.InsertRange(i, new[] { first, new CodeInstruction(OpCodes.Ldarg_0), new CodeInstruction(OpCodes.Ldfld, desired),
                    new CodeInstruction(OpCodes.Call, present) });
                return list;
            }
            UnityEngine.Debug.LogWarning("[Mixed Atmospheres] LogicElementSensor.Sim200ms changed; element sensors keep the single-element test.");
            return list;
        }
    }
}
