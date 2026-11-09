using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    // Natural sublimation (docs/colony-owner.md#natural-sublimation). ONI's
    // native simulation lets natural solid tiles (oxylite, polluted dirt)
    // sublimate into the gas or vacuum beside them, and polluted water
    // off-gas into the cell above, each 0.2 s frame. That step belongs to the
    // native post-processing of the receiving cell, which the owner suppresses
    // for its cells; the owner's native adapter runs it there instead and books
    // the products. This hands the adapter the game's per-element rules, as
    // the game's native simulation receives them.
    internal static class ColonySublimation
    {
        private const string Library = "mixed_atmospheres_oni";
        private const uint Solid = 1, Liquid = 2, RequiredNativeVersion = 1;

        [StructLayout(LayoutKind.Sequential)]
        private struct Rule
        {
            public uint Size, Version, Element, Kind, Target, Reserved;
            public float Rate, Efficiency, Probability, Offgas;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Totals
        {
            public uint Size, Version;
            public ulong Session, Events, TilesUsedUp;
            public double TileKg, LiquidKg, ProducedKg;
            internal static Totals Empty => new Totals { Size = 56, Version = 1 };
        }

        [DllImport(Library, EntryPoint = "ma_oni_colony_sublimation_version", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint Version();
        [DllImport(Library, EntryPoint = "ma_oni_colony_set_sublimation", CallingConvention = CallingConvention.Cdecl)]
        private static extern int SetRules([In] Rule[] rules, uint count);
        [DllImport(Library, EntryPoint = "ma_oni_colony_sublimation_totals", CallingConvention = CallingConvention.Cdecl)]
        private static extern int ReadTotals(ref Totals totals);

        internal static bool Supported { get; private set; }
        internal static int Rules { get; private set; }

        // Before the owner starts or restores: every element that sublimates
        // (a solid) or off-gasses (a liquid), with its product.
        internal static void Configure()
        {
            Supported = false; Rules = 0;
            try { if (Version() != RequiredNativeVersion) return; }
            catch (EntryPointNotFoundException) { return; }
            if (Marshal.SizeOf<Rule>() != 40 || Marshal.SizeOf<Totals>() != 56)
                throw new InvalidOperationException("Sublimation ABI layouts differ from the native library.");
            var rules = new List<Rule>();
            static bool Fraction(float v) => !float.IsNaN(v) && v >= 0 && v <= 1;
            foreach (var element in ElementLoader.elements)
            {
                if (element == null || element.sublimateId == 0 || !(element.IsSolid || element.IsLiquid)) continue;
                var target = ElementLoader.FindElementByHash(element.sublimateId);
                // A rule the native records refuse (another mod's odd data) keeps ONI's
                // behaviour under the owner: that element does not sublimate.
                if (target == null || target.idx == element.idx || !Fraction(element.sublimateEfficiency) || !Fraction(element.sublimateProbability) ||
                    !Fraction(element.offGasPercentage) || float.IsNaN(element.sublimateRate) || float.IsInfinity(element.sublimateRate) || element.sublimateRate < 0) continue;
                rules.Add(new Rule
                {
                    Size = 40, Version = 1, Element = element.idx, Kind = element.IsSolid ? Solid : Liquid, Target = target.idx,
                    Rate = element.sublimateRate, Efficiency = element.sublimateEfficiency,
                    Probability = element.sublimateProbability, Offgas = element.offGasPercentage
                });
            }
            try { OniNativeColony.Check(SetRules(rules.ToArray(), (uint)rules.Count)); }
            catch (Exception error)
            {
                UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Natural sublimation rules refused; natural tiles will not sublimate into owned cells: " + error.Message);
                try { OniNativeColony.Check(SetRules(Array.Empty<Rule>(), 0)); } catch (Exception) { }
                return;
            }
            Supported = true; Rules = rules.Count;
            UnityEngine.Debug.Log($"[Mixed Atmospheres] Natural sublimation: {Rules} element rules.");
        }

        // This session's events (scenarios and the stage log).
        internal static Totals Read()
        {
            var totals = Totals.Empty;
            if (!Supported) return totals;
            OniNativeColony.Check(ReadTotals(ref totals));
            return totals;
        }
    }
}
