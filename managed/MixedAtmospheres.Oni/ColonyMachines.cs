using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Machines on the owned asteroid (docs/colony-owner.md#machines).
    // A converter's output into the world is a reserved transaction: when no
    // owned cell near its output can take it, the converter does not run, so
    // it takes neither its inputs nor (through its building's idle state) its
    // power. A gas always fits an open cell (ONI's own pressure limits then
    // apply); a liquid needs a cell within two that is not already full of
    // liquid, the cells the owner places an injection into.
    internal static class ColonyMachines
    {
        internal const float FullLiquidFraction = .98f;
        internal const int OutputRadius = 2;

        internal static bool OutputRoom(ElementConverter converter)
        {
            if (!ColonyOwner.Active || converter.outputElements == null) return true;
            var position = converter.transform.GetPosition();
            foreach (var output in converter.outputElements)
            {
                if (output.storeOutput || !output.IsActive) continue;
                var element = ElementLoader.FindElementByHash(output.elementHash);
                if (element == null || !(element.IsGas || element.IsLiquid)) continue;
                var cell = Grid.PosToCell(new Vector3(position.x + output.outputElementOffset.x, position.y + output.outputElementOffset.y, 0));
                if (!ColonyOwner.OwnsFluidCell(cell) && Grid.IsValidCell(cell) && !Grid.Solid[cell]) continue; // not the owner's
                if (!Room(cell, element.IsLiquid)) return false;
            }
            return true;
        }

        // A generator's gas exhaust (coal, natural gas, petroleum, wood) is the
        // same reservation: with no owned cell near its exhaust it does not
        // burn, so it takes no fuel and makes no power. Liquid outputs fall as
        // particles and find their own cell.
        internal static bool OutputRoom(EnergyGenerator generator)
        {
            if (!ColonyOwner.Active || generator.formula.outputs == null) return true;
            var origin = Grid.PosToCell(generator.transform.GetPosition());
            foreach (var output in generator.formula.outputs)
            {
                if (output.store) continue;
                var element = ElementLoader.FindElementByHash(output.element);
                if (element == null || !element.IsGas) continue;
                var cell = Grid.OffsetCell(origin, output.emitOffset);
                if (!ColonyOwner.OwnsFluidCell(cell) && Grid.IsValidCell(cell) && !Grid.Solid[cell]) continue; // not the owner's
                if (!Room(cell, false)) return false;
            }
            return true;
        }

        private static bool Room(int origin, bool liquid)
        {
            Grid.CellToXY(origin, out var x0, out var y0);
            for (var dy = -OutputRadius; dy <= OutputRadius; ++dy)
                for (var dx = -OutputRadius; dx <= OutputRadius; ++dx)
                {
                    var cell = Grid.XYToCell(x0 + dx, y0 + dy);
                    if (!Grid.IsValidCell(cell) || !ColonyOwner.TryReadView(cell, out var view)) continue;
                    if (!liquid || view.LiquidFraction < FullLiquidFraction) return true;
                }
            return false;
        }
    }

    [HarmonyPatch(typeof(ElementConverter), nameof(ElementConverter.CanConvertAtAll))]
    internal static class ColonyConverterCanConvertPatch
    {
        private static void Postfix(ElementConverter __instance, ref bool __result) { if (__result) __result = ColonyMachines.OutputRoom(__instance); }
    }

    [HarmonyPatch(typeof(ElementConverter), nameof(ElementConverter.HasEnoughMassToStartConverting))]
    internal static class ColonyConverterStartPatch
    {
        private static void Postfix(ElementConverter __instance, ref bool __result) { if (__result) __result = ColonyMachines.OutputRoom(__instance); }
    }

    [HarmonyPatch(typeof(ElementConverter), "ConvertMass")]
    internal static class ColonyConverterConvertPatch
    {
        private static bool Prefix(ElementConverter __instance) => ColonyMachines.OutputRoom(__instance);
    }

    [HarmonyPatch(typeof(EnergyGenerator), "IsConvertible")]
    internal static class ColonyGeneratorConvertiblePatch
    {
        private static void Postfix(EnergyGenerator __instance, ref bool __result) { if (__result) __result = ColonyMachines.OutputRoom(__instance); }
    }

    // A wrong element arriving at a machine's input is dumped where ONI would
    // destroy it: the owner takes it into the building's cell.
    [HarmonyPatch(typeof(ConduitConsumer), "Consume")]
    internal static class ColonyWrongElementPatch
    {
        private static void Prefix(ConduitConsumer __instance)
        {
            if (ColonyOwner.Active && __instance.wrongElementResult == ConduitConsumer.WrongElementResult.Destroy)
                __instance.wrongElementResult = ConduitConsumer.WrongElementResult.Dump;
        }
    }
}
