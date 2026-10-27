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

        // A reader that throws faults the owner (ColonyFault) and the sensor
        // falls back to the grid's shown cell.
        internal static bool Present(int cell, int element)
        {
            try { return ReadPresent(cell, element); }
            catch (System.Exception error) { ColonyFault.Raise("element sensor", error); return Grid.ElementIdx[cell] == element; }
        }
        private static bool ReadPresent(int cell, int element)
        {
            if (!ColonyOwner.Active || !ColonyOwner.OwnsFluidCell(cell) || element < 0 || element >= ElementLoader.elements.Count)
                return Grid.ElementIdx[cell] == element;
            ColonyFault.Inject("sensor");
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

        // The mole share (0..1) of one gas among a cell's gases, for the gas
        // share sensor (docs/settings.md). Off the owned asteroid a cell holds
        // one element: all of it or none.
        internal static double GasShare(int cell, Element? wanted)
        {
            try { return ReadGasShare(cell, wanted); }
            catch (System.Exception error) { ColonyFault.Raise("gas share sensor", error); return wanted != null && Grid.Element[cell] == wanted && Grid.Mass[cell] > 0 ? 1 : 0; }
        }
        private static double ReadGasShare(int cell, Element? wanted)
        {
            if (wanted == null || !wanted.IsGas || !Grid.IsValidCell(cell)) return 0;
            if (!ColonyOwner.Active || !ColonyOwner.OwnsFluidCell(cell)) return Grid.Element[cell] == wanted && Grid.Mass[cell] > 0 ? 1 : 0;
            var count = ColonyOwner.Composition(cell, out var indices, out var masses);
            double all = 0, mine = 0;
            for (var k = 0; k < count; ++k)
            {
                var e = ElementLoader.elements[(int)indices[k]];
                if (!e.IsGas || !(e.molarMass > 0)) continue;
                var moles = masses[k] / e.molarMass;
                all += moles;
                if (e == wanted) mine += moles;
            }
            return all > 0 ? mine / all : 0;
        }

        // Gas or liquid mass of a cell for an atmo or hydro sensor.
        internal static float PhaseMass(int cell, Element.State state)
        {
            try { return ReadPhaseMass(cell, state); }
            catch (System.Exception error) { ColonyFault.Raise("pressure sensor", error); return Grid.Element[cell].IsState(state) ? Grid.Mass[cell] : 0f; }
        }
        private static float ReadPhaseMass(int cell, Element.State state)
        {
            var gas = state == Element.State.Gas; var liquid = state == Element.State.Liquid;
            if (!ColonyOwner.Active || !ColonyOwner.OwnsFluidCell(cell) || !(gas || liquid))
                return Grid.Element[cell].IsState(state) ? Grid.Mass[cell] : 0f;
            var count = ColonyOwner.Composition(cell, out var indices, out var masses);
            double total = 0;
            for (var k = 0; k < count; ++k) if (ElementLoader.elements[(int)indices[k]].IsGas == gas) total += masses[k];
            return (float)total;
        }

        // A germ sensor counts every disease the cell's fluid carries, not only
        // the most numerous one the game's grid shows.
        private static readonly uint[] one = new uint[1];
        internal static int Germs(int cell)
        {
            try { return ReadGerms(cell); }
            catch (System.Exception error) { ColonyFault.Raise("germ sensor", error); return Grid.DiseaseCount[cell]; }
        }
        private static int ReadGerms(int cell)
        {
            if (!ColonyOwner.Active || !ColonyOwner.OwnsFluidCell(cell)) return Grid.DiseaseCount[cell];
            one[0] = (uint)cell;
            for (var attempt = 0; attempt < 4; ++attempt)
            {
                var result = ColonyWorldPassives.Read(OniNativeColony.ReadPassives, 0, 0, one, out var snapshot);
                if (result == ColonyWorldPassives.Busy) continue;
                if (result != 0 || snapshot == null) break;
                double total = 0;
                foreach (var row in snapshot.Rows) if (row.Identity >= 1 && row.Identity <= 256) total += row.Quantity;
                return (int)System.Math.Min(System.Math.Floor(total), int.MaxValue);
            }
            return Grid.DiseaseCount[cell];
        }
    }

    // Replaces "Grid.Element[cell].IsState(desiredState) ? Grid.Mass[cell] : 0"
    // in the atmo and hydro sensors' sampler: an owned cell offers the mass of
    // its gas or liquid constituents, whichever phase the game shows.
    [HarmonyPatch(typeof(LogicPressureSensor), "Sim200ms")]
    internal static class ColonyPressureSensorPatch
    {
        private static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
        {
            var list = new List<CodeInstruction>(instructions);
            var elements = AccessTools.Field(typeof(Grid), nameof(Grid.Element));
            var masses = AccessTools.Field(typeof(Grid), nameof(Grid.Mass));
            var state = AccessTools.Field(typeof(LogicPressureSensor), "desiredState");
            var phaseMass = AccessTools.Method(typeof(ColonySensors), nameof(ColonySensors.PhaseMass));
            for (var i = 0; i + 10 < list.Count; ++i)
            {
                // ldsfld Grid.Element; ldloc cell; ldelem.ref; ldarg.0; ldfld desiredState; callvirt IsState;
                // brtrue; ldc.r4 0; br; ldsflda Grid.Mass; ldloc cell; call get_Item
                if (!(list[i].opcode == OpCodes.Ldsfld && Equals(list[i].operand, elements)) || list[i + 2].opcode != OpCodes.Ldelem_Ref ||
                    list[i + 3].opcode != OpCodes.Ldarg_0 || !(list[i + 4].opcode == OpCodes.Ldfld && Equals(list[i + 4].operand, state)) ||
                    !(list[i + 9].opcode == OpCodes.Ldsflda && Equals(list[i + 9].operand, masses)) || list[i + 11].opcode != OpCodes.Call) continue;
                var cell = list[i + 1]; var first = new CodeInstruction(cell.opcode, cell.operand);
                list[i].MoveLabelsTo(first);
                // The removed branches' labels land on what follows (the sample store).
                for (var k = i + 1; k <= i + 11; ++k) MixtureInstructionLabels.Move(list[k], list[i + 12]);
                list.RemoveRange(i, 12);
                list.InsertRange(i, new[] { first, new CodeInstruction(OpCodes.Ldarg_0), new CodeInstruction(OpCodes.Ldfld, state), new CodeInstruction(OpCodes.Call, phaseMass) });
                return list;
            }
            UnityEngine.Debug.LogWarning("[Mixed Atmospheres] LogicPressureSensor.Sim200ms changed; pressure sensors read the shown element's mass.");
            return list;
        }
    }

    // Replaces "Grid.DiseaseCount[cell]" in the germ sensor's sampler.
    [HarmonyPatch(typeof(LogicDiseaseSensor), "Sim200ms")]
    internal static class ColonyGermSensorPatch
    {
        private static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
        {
            var list = new List<CodeInstruction>(instructions);
            var counts = AccessTools.Field(typeof(Grid), nameof(Grid.DiseaseCount));
            var germs = AccessTools.Method(typeof(ColonySensors), nameof(ColonySensors.Germs));
            for (var i = 0; i + 2 < list.Count; ++i)
            {
                // ldsflda Grid.DiseaseCount; ldloc cell; call get_Item
                if (!(list[i].opcode == OpCodes.Ldsflda && Equals(list[i].operand, counts)) || list[i + 2].opcode != OpCodes.Call) continue;
                var cell = list[i + 1]; var first = new CodeInstruction(cell.opcode, cell.operand);
                list[i].MoveLabelsTo(first);
                list.RemoveRange(i, 3);
                list.InsertRange(i, new[] { first, new CodeInstruction(OpCodes.Call, germs) });
                return list;
            }
            UnityEngine.Debug.LogWarning("[Mixed Atmospheres] LogicDiseaseSensor.Sim200ms changed; germ sensors read the grid's shown germ.");
            return list;
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
