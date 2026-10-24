using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Named balance ledgers (docs/colony-owner.md#machines, playtest plan
    // "Balance"). Machines keep ONI's rates; where a conversion's products
    // carry more or less energy than its inputs plus the electricity it drew
    // (an electrolyzer splitting water on 120 W), the difference is booked
    // here per building kind, so it is visible rather than hidden. Energies use
    // the catalogue's caloric model, c·T plus each species' formation offset
    // (c·T alone for solids and other elements); the owner's own ledgers book
    // the fluids it receives exactly.
    internal static class ColonyBalance
    {
        internal sealed class Ledger
        {
            public long Conversions;
            public double InputJoules, OutputJoules, ElectricalJoules;
            public double BalanceJoules => OutputJoules - InputJoules - ElectricalJoules;
        }
        internal static readonly SortedDictionary<string, Ledger> Ledgers = new SortedDictionary<string, Ledger>();
        private static Ledger? current;
        private static readonly Dictionary<int, (double cv, double offset)> species = new Dictionary<int, (double, double)>();
        private static readonly ConditionalWeakTable<ElementConverter, StrongBox<double>> lastConversion = new ConditionalWeakTable<ElementConverter, StrongBox<double>>();
        internal static void Reset() { Ledgers.Clear(); current = null; species.Clear(); }

        // Joules held by mass of an element at a temperature.
        internal static double Energy(Element element, double mass, double temperature)
        {
            if (species.Count == 0 && ColonyOwner.Active)
            {
                try
                {
                    OniNativeColony.Check(OniNativeColony.SpeciesList(null, 0, out var count));
                    var list = new OniNativeColony.Species[count];
                    OniNativeColony.Check(OniNativeColony.SpeciesList(list, count, out _));
                    foreach (var s in list) species[(int)s.NativeIndex] = (s.CvJoulesPerKgKelvin, s.EnergyOffsetJoulesPerKg);
                }
                catch (Exception) { species.Clear(); }
            }
            if (species.TryGetValue(element.idx, out var model)) return mass * (model.cv * temperature + model.offset);
            return mass * element.specificHeatCapacity * 1000.0 * temperature;
        }
        private static Ledger For(UnityEngine.GameObject building)
        {
            var id = building.PrefabID().Name;
            if (!Ledgers.TryGetValue(id, out var ledger)) Ledgers[id] = ledger = new Ledger();
            return ledger;
        }

        // A conversion: its storage before and after, and the world outputs
        // it sends to the owner meanwhile.
        internal sealed class Scope
        {
            internal Ledger Ledger = null!;
            internal Storage? Storage;
            internal readonly List<(Element element, float mass, float temperature)> Before = new List<(Element, float, float)>();
        }
        private static List<(Element, float, float)> Contents(Storage? storage)
        {
            var list = new List<(Element, float, float)>();
            if (storage == null) return list;
            foreach (var item in storage.items)
            {
                var primary = item != null ? item.GetComponent<PrimaryElement>() : null;
                if (primary != null && primary.Element != null) list.Add((primary.Element, primary.Mass, primary.Temperature));
            }
            return list;
        }
        private static readonly System.Reflection.FieldInfo ConverterStorage = AccessTools.Field(typeof(ElementConverter), "storage");
        internal static Scope? Begin(UnityEngine.GameObject? building, Storage? storage)
        {
            if (!ColonyOwner.Active || building == null) return null;
            var scope = new Scope { Ledger = For(building), Storage = storage };
            scope.Before.AddRange(Contents(storage));
            current = scope.Ledger;
            return scope;
        }
        internal static Scope? Begin(ElementConverter converter) =>
            converter == null ? null : Begin(converter.gameObject, ConverterStorage.GetValue(converter) as Storage);
        // Electricity: a consumer's draw since its previous conversion is
        // input; a generator's output this tick leaves as electricity.
        internal static bool End(Scope? scope)
        {
            current = null;
            if (scope == null) return false;
            // Per element: what left the storage is input, what arrived is output.
            var net = new Dictionary<Element, (double mass, double energy)>();
            void Add(Element e, double mass, double energy) { net.TryGetValue(e, out var v); net[e] = (v.mass + mass, v.energy + energy); }
            foreach (var (e, m, t) in scope.Before) Add(e, -m, -Energy(e, m, t));
            foreach (var (e, m, t) in Contents(scope.Storage)) Add(e, m, Energy(e, m, t));
            var changed = false;
            foreach (var v in net.Values)
            {
                if (v.mass < 0) { scope.Ledger.InputJoules -= v.energy; changed = true; }
                else if (v.mass > 0) { scope.Ledger.OutputJoules += v.energy; changed = true; }
            }
            if (changed) ++scope.Ledger.Conversions;
            return changed;
        }
        internal static void End(ElementConverter converter, Scope? scope)
        {
            if (!End(scope) || converter == null) return;
            var now = GameClock.Instance != null ? GameClock.Instance.GetTime() : 0;
            var last = lastConversion.GetOrCreateValue(converter);
            var consumer = converter.GetComponent<EnergyConsumer>();
            if (consumer != null && last.Value > 0 && now > last.Value) scope!.Ledger.ElectricalJoules += consumer.WattsUsed * (now - last.Value);
            last.Value = now;
        }
        // A world output of the conversion in progress (ColonyOwner routes it).
        internal static void WorldOutput(int elementIndex, float mass, float temperature)
        {
            if (current == null || !(mass > 0)) return;
            current.OutputJoules += Energy(ElementLoader.elements[elementIndex], mass, temperature);
        }
        internal static object Report()
        {
            var report = new Dictionary<string, object>();
            foreach (var (id, l) in Ledgers)
                report[id] = new { l.Conversions, l.InputJoules, l.OutputJoules, l.ElectricalJoules, l.BalanceJoules };
            return report;
        }
    }

    // A generator burning fuel: its exhaust goes to the owner or its storage,
    // and its rated output leaves as electricity.
    [HarmonyPatch(typeof(EnergyGenerator), nameof(EnergyGenerator.EnergySim200ms))]
    internal static class ColonyBalanceGeneratorPatch
    {
        [HarmonyPriority(Priority.Last)]
        private static void Prefix(EnergyGenerator __instance, ref ColonyBalance.Scope? __state) =>
            __state = ColonyBalance.Begin(__instance.gameObject, __instance.GetComponent<Storage>());
        private static void Postfix(EnergyGenerator __instance, float __0, ColonyBalance.Scope? __state)
        {
            if (ColonyBalance.End(__state)) __state!.Ledger.ElectricalJoules -= __instance.WattageRating * __0;
        }
        private static Exception? Finalizer(Exception? __exception, ColonyBalance.Scope? __state) { if (__exception != null) ColonyBalance.End((ColonyBalance.Scope?)null); return __exception; }
    }

    [HarmonyPatch(typeof(ElementConverter), "ConvertMass")]
    internal static class ColonyBalanceConvertPatch
    {
        [HarmonyPriority(Priority.Last)]
        private static void Prefix(ElementConverter __instance, ref ColonyBalance.Scope? __state) => __state = ColonyBalance.Begin(__instance);
        private static void Postfix(ElementConverter __instance, ColonyBalance.Scope? __state) => ColonyBalance.End(__instance, __state);
        private static Exception? Finalizer(Exception? __exception, ColonyBalance.Scope? __state) { if (__exception != null) ColonyBalance.End((ColonyBalance.Scope?)null); return __exception; }
    }
}
