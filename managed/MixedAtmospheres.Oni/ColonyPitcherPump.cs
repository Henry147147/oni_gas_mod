using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // The pitcher pump offers every liquid of the pool beneath it
    // (docs/colony-owner.md#machines). ONI counts each cell's one element; an
    // owned cell offers each liquid constituent it holds, so a mixed pool can
    // be bottled liquid by liquid (each bottle then takes its own element from
    // the owner).
    internal static class ColonyPitcherPump
    {
        private static readonly FieldInfo Infos = AccessTools.Field(typeof(LiquidPumpingStation), "infos"),
            Count = AccessTools.Field(typeof(LiquidPumpingStation), "infoCount");
        private static readonly Type InfoType = AccessTools.Inner(typeof(LiquidPumpingStation), "LiquidInfo");
        private static readonly FieldInfo Amount = AccessTools.Field(InfoType, "amount"), Kind = AccessTools.Field(InfoType, "element");
        internal static bool Ready => Infos != null && Count != null && Amount != null && Kind != null;

        // One cell's liquid, added to the station's per-element totals as ONI does.
        internal static void Sample(LiquidPumpingStation station, int cell)
        {
            if (!ColonyOwner.Active || !ColonyOwner.OwnsFluidCell(cell))
            {
                var element = Grid.Element[cell];
                if (element.IsLiquid) Add(station, element, Grid.Mass[cell]);
                return;
            }
            var count = ColonyOwner.Composition(cell, out var indices, out var masses);
            for (var k = 0; k < count; ++k)
            {
                var element = ElementLoader.elements[(int)indices[k]];
                if (element.IsLiquid && masses[k] > 0) Add(station, element, (float)masses[k]);
            }
        }
        private static void Add(LiquidPumpingStation station, Element element, float mass)
        {
            var infos = (Array)Infos.GetValue(station); var count = (int)Count.GetValue(station);
            for (var i = 0; i < count; ++i)
            {
                var info = infos.GetValue(i);
                if (!ReferenceEquals(Kind.GetValue(info), element)) continue;
                Amount.SetValue(info, (float)Amount.GetValue(info) + mass); infos.SetValue(info, i);
                return;
            }
            if (count >= infos.Length) return;
            var added = infos.GetValue(count);
            Amount.SetValue(added, mass); Kind.SetValue(added, element); infos.SetValue(added, count);
            Count.SetValue(station, count + 1);
        }
    }

    // Replaces the sampler's per-cell block "element = Grid.Element[cell]; if
    // liquid, add Grid.Mass[cell] to its info" with ColonyPitcherPump.Sample.
    [HarmonyPatch(typeof(LiquidPumpingStation), "Sim200ms")]
    internal static class ColonyPitcherPumpPatch
    {
        private static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
        {
            var list = new List<CodeInstruction>(instructions);
            var offsetCell = AccessTools.Method(typeof(Grid), nameof(Grid.OffsetCell), new[] { typeof(int), typeof(CellOffset) });
            var elements = AccessTools.Field(typeof(Grid), nameof(Grid.Element));
            var sample = AccessTools.Method(typeof(ColonyPitcherPump), nameof(ColonyPitcherPump.Sample));
            for (var i = 0; i + 3 < list.Count && ColonyPitcherPump.Ready; ++i)
            {
                // call Grid.OffsetCell; stloc cell; ... ldsfld Grid.Element (the block) ... up to the loop step "ldloc i; ldc.i4.1; add; stloc i"
                if (!list[i].Calls(offsetCell) || !(list[i + 1].opcode == OpCodes.Stloc_S || list[i + 1].opcode == OpCodes.Stloc)) continue;
                var store = list[i + 1];
                var start = i + 2;
                if (!list.GetRange(start, Math.Min(6, list.Count - start)).Exists(c => c.opcode == OpCodes.Ldsfld && Equals(c.operand, elements))) continue;
                var end = -1;
                for (var k = start; k + 3 < list.Count; ++k)
                    if (MixtureInstructionLabels.Any(list[k]) && list[k].opcode == OpCodes.Ldloc_3 && list[k + 1].opcode == OpCodes.Ldc_I4_1 && list[k + 2].opcode == OpCodes.Add &&
                        list[k + 3].opcode == OpCodes.Stloc_3) { end = k; break; }
                if (end < 0) break;
                var load = store.opcode == OpCodes.Stloc_S ? OpCodes.Ldloc_S : OpCodes.Ldloc;
                list.RemoveRange(start, end - start);
                list.InsertRange(start, new[] { new CodeInstruction(OpCodes.Ldarg_0), new CodeInstruction(load, store.operand), new CodeInstruction(OpCodes.Call, sample) });
                return list;
            }
            UnityEngine.Debug.LogWarning("[Mixed Atmospheres] LiquidPumpingStation.Sim200ms changed; the pitcher pump offers each cell's shown liquid only.");
            return list;
        }
    }
}
