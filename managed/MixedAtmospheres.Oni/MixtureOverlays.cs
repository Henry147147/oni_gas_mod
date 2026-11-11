using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using HarmonyLib;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal enum MixtureOverlayKind { Composition, Pressure, OxygenMargin, Breathability }

    internal sealed class MixtureOverlayMode : OverlayModes.Mode
    {
        private readonly MixtureOverlayKind kind;
        internal MixtureOverlayMode(MixtureOverlayKind kind) { this.kind = kind; legendFilters = CreateDefaultFilters(); }
        public override HashedString ViewMode() => MixtureOverlays.Id(kind);
        public override string GetSoundName() => "Oxygen";

        public override List<LegendEntry> GetCustomLegendData() => MixtureOverlays.Legend(kind);
        public override ToolParameterMenu.ToggleData[] CreateDefaultFilters()
        {
            // Native OverlayLegend checks for null, not Length. An empty array
            // still creates an active ToolParameterMenu and its raycast/layout
            // surface even though this mode has no controls.
            if (kind != MixtureOverlayKind.Composition) return null!;
            return MixtureOverlays.Contrasts.Select((value, index) => new ToolParameterMenu.ToggleData(
                "MA_TEXTURE_" + index, Math.Abs(MixtureAppearanceSettings.Contrast - value) < 0.0001
                    ? ToolParameterMenu.ToggleState.On : ToolParameterMenu.ToggleState.Off)).ToArray();
        }
        public override void OnFiltersChanged()
        {
            if (kind != MixtureOverlayKind.Composition || legendFilters == null) return;
            for (var index = 0; index < legendFilters.Length; ++index)
                if (legendFilters[index].IsOn) { MixtureAppearanceSettings.SetContrast(MixtureOverlays.Contrasts[index]); return; }
        }
    }

    internal static class MixtureOverlays
    {
        internal static readonly double[] Contrasts = { 0, 0.02, 0.04, 0.08, 0.12 };
        // "Oxygen margin", not "Oxygen": ONI's own Oxygen overlay keeps that name.
        private static readonly string[] Names = { "Gas composition", "Gas pressure", "Oxygen margin", "Breathability" };
        private static readonly string[] Descriptions =
        {
            "Gas colours show constituent mole fractions. Inspect a cell for every constituent, including traces.",
            "Total gas pressure from vacuum to 250 kPa. Normal base air is about 125 kPa.",
            "How far each tile's oxygen is above the breathing minimums (share and partial pressure): red cannot be breathed, yellow barely, teal comfortably.",
            "Which breathing check each tile fails: too little oxygen, toxic gas (and carbon dioxide when its limits are on), or vacuum."
        };
        internal static HashedString Id(MixtureOverlayKind kind) => "MixedAtmospheres." + kind;
        internal static string Name(MixtureOverlayKind kind) => Names[(int)kind];
        private static readonly Color Safe = new Color(0.23f, 0.75f, 0.65f);
        private static readonly Color Unsafe = new Color(0.83f, 0.24f, 0.20f);
        private static readonly Color Toxic = new Color(0.85f, 0.36f, 0.79f);
        private static readonly Color Low = new Color(0.16f, 0.22f, 0.40f);
        private static readonly Color High = new Color(0.96f, 0.73f, 0.26f);
        // Gas pressure, linear from vacuum to 250 kPa: normal base air (1.6–1.8
        // kg of oxygen per tile, about 122–138 kPa with molecular oxygen) sits
        // mid-scale, so both thin air and overpressure stand out from it.
        internal const double NormalPressurePa = 125000, MaximumPressurePa = 250000;
        private static readonly Color Normal = new Color(0.40f, 0.68f, 0.58f);
        internal static Color PressureColor(double pascals)
        {
            var t = Mathf.Clamp01((float)(pascals / MaximumPressurePa)) * 2;
            return t <= 1 ? Color.Lerp(Low, Normal, t) : Color.Lerp(Normal, High, t - 1);
        }
        // The oxygen margin: how many times the colony's breathing minimums a
        // cell's oxygen holds, the smaller of its share by moles and its
        // partial pressure over their minimums (polluted oxygen counts, as for
        // breathing). Below 1 a duplicant cannot breathe the cell; from 4 the
        // air has plenty to spare.
        internal const double ComfortableMargin = 4;
        internal static double OxygenMargin(MixturePresentation reading, MixturePresentationModel model) =>
            !(reading.GasMoles > 0) ? 0 : Math.Min(reading.OxygenMoleFraction / model.MinimumOxygenFraction,
                reading.OxygenPartialPressurePa / model.MinimumOxygenPartialPressurePa);
        // Comfort on the 1..4 margin scale, 0 at the minimums.
        internal static float Comfort(double margin) => Mathf.Clamp01((float)((margin - 1) / (ComfortableMargin - 1)));
        private static readonly Color Suffocating = new Color(0.42f, 0.07f, 0.09f), Failing = new Color(0.88f, 0.32f, 0.16f),
            Barely = new Color(0.94f, 0.80f, 0.26f);
        internal static Color MarginColor(double margin) =>
            !(margin >= 1) ? Color.Lerp(Suffocating, Failing, Mathf.Clamp01((float)margin)) : Color.Lerp(Barely, Safe, Comfort(margin));
        private static string Percent(double fraction) => (fraction * 100).ToString("0.##", CultureInfo.InvariantCulture) + "%";
        private static string Kilopascals(double pascals) => (pascals / 1000).ToString("0.##", CultureInfo.InvariantCulture) + " kPa";
        // Another gas makes the air unbreathable: toxic gas over its limit, or
        // carbon dioxide over its limits when they are on.
        internal const BreathingFailure Harmful = BreathingFailure.Contaminant | BreathingFailure.CarbonDioxideFractionHigh | BreathingFailure.CarbonDioxidePartialPressureHigh;
        private static string List(IEnumerable<string> names)
        {
            var all = names.ToArray();
            return all.Length <= 1 ? string.Concat(all) : string.Join(", ", all.Take(all.Length - 1)) + " and " + all[all.Length - 1];
        }
        // The legend row for harmful gas, from the loaded colony's limits; none
        // when no limit can fail (toxic gas limit at 100%, CO2 limits off).
        private static (string label, string tooltip)? HarmfulGasRow(MixturePresentationModel? model)
        {
            if (model == null) return ("Toxic gas", "A toxic gas is above its breathing limit.");
            var toxic = model.MaximumToxicGasFraction < 1 && model.ToxicGasMaterialIds.Count > 0;
            var co2 = model.EnforceCarbonDioxideLimits;
            var other = model.ContaminantMoleFractionLimits.Count > 0;
            if (!toxic && !co2 && !other) return null;
            var where = new List<string>();
            if (toxic) where.Add(List(model.ToxicGasMaterialIds.Select(id => model.Styles[id].DisplayName)) + " together make up " +
                Percent(model.MaximumToxicGasFraction) + " of the gas or more");
            if (co2) where.Add("carbon dioxide is above " + Percent(model.MaximumCo2Fraction) + " or " + Kilopascals(model.MaximumCo2PartialPressurePa));
            if (other) where.Add("a gas is above its own limit");
            return (toxic || other ? co2 ? "Toxic gas or CO₂" : "Toxic gas" : "Too much CO₂",
                "Duplicants cannot breathe where " + string.Join(", or where ", where) + ".");
        }
        private static (string label, string tooltip) LowOxygenRow(MixturePresentationModel? model)
        {
            var share = model?.MinimumOxygenFraction ?? .15; var pascals = model?.MinimumOxygenPartialPressurePa ?? 15000;
            var ceiling = model != null && model.HardMode;
            return (ceiling ? "Too little or too much oxygen" : "Too little oxygen", "Less than " + Percent(share) + " oxygen by moles or less than " +
                Kilopascals(pascals) + " of oxygen (polluted oxygen counts)" + (ceiling ? ", or more than " + Percent(model!.MaximumOxygenFraction) + " oxygen" : "") + ".");
        }
        private static ConditionalWeakTable<OverlayLegend, NativeOxygenLegend> OxygenLegends = new ConditionalWeakTable<OverlayLegend, NativeOxygenLegend>();

        static MixtureOverlays() { MixturePresentationRuntime.SessionEnding += ResetSessionLegend; }

        private static T ReadField<T>(Type ownerType, string name, object? owner) where T : class
            => AccessTools.Field(ownerType, name)?.GetValue(owner) as T
                ?? throw new NotSupportedException("Mixture overlay field changed: " + ownerType.Name + "." + name);

        internal static void RegisterStatusItemOverlays()
        {
            var map = ReadField<Dictionary<HashedString, StatusItem.StatusItemOverlays>>(typeof(StatusItem), "overlayBitfieldMap", null);
            // Native Oxygen and material views use None. Reuse that visibility
            // policy without inventing flags or changing a gameplay status.
            statusOverlaysRegistered = true;
            foreach (MixtureOverlayKind kind in Enum.GetValues(typeof(MixtureOverlayKind)))
                map[Id(kind)] = StatusItem.StatusItemOverlays.None;
        }

        // Failed OnLoad teardown: this static game registry outlives Harmony.
        private static bool statusOverlaysRegistered;
        internal static void UnregisterStatusItemOverlays()
        {
            if (!statusOverlaysRegistered) return;
            var map = ReadField<Dictionary<HashedString, StatusItem.StatusItemOverlays>>(typeof(StatusItem), "overlayBitfieldMap", null);
            foreach (MixtureOverlayKind kind in Enum.GetValues(typeof(MixtureOverlayKind)))
                map.Remove(Id(kind));
            statusOverlaysRegistered = false;
        }

        private static void ResetSessionLegend()
        {
            try
            {
                var legend = OverlayLegend.Instance;
                if (legend != null && OxygenLegends.TryGetValue(legend, out var original)) original.Restore();
            }
            finally { OxygenLegends = new ConditionalWeakTable<OverlayLegend, NativeOxygenLegend>(); }
        }

        private sealed class NativeOxygenLegend
        {
            internal readonly OverlayLegend.OverlayInfo Info;
            private readonly string name;
            private readonly List<OverlayLegend.OverlayInfoUnit> units;
            private readonly List<GameObject> diagrams;
            private readonly bool generated;
            internal NativeOxygenLegend(OverlayLegend.OverlayInfo info)
            { Info = info; name = info.name; units = info.infoUnits; diagrams = info.diagrams; generated = info.isProgrammaticallyPopulated; }
            internal void Restore()
            { Info.name = name; Info.infoUnits = units; Info.diagrams = diagrams; Info.isProgrammaticallyPopulated = generated; }
            internal void Apply(SimDebugView view)
            {
                var icon = units.FirstOrDefault()?.icon;
                Info.name = "Oxygen · breathability";
                Info.isProgrammaticallyPopulated = false;
                Info.diagrams = new List<GameObject>();
                OverlayLegend.OverlayInfoUnit Unit(string text, string description, Color color) =>
                    new OverlayLegend.OverlayInfoUnit(icon, text, color, Color.white) { tooltip = description };
                var model = MixturePresentationRuntime.CurrentModel;
                Info.infoUnits = new List<OverlayLegend.OverlayInfoUnit>
                {
                    Unit("Comfortable", "At least four times the breathing minimums for oxygen; every other breathing check passes. Unowned cells keep ONI's own oxygen colours.", view.breathableGradient.Evaluate(1)),
                    Unit("Barely breathable", "Just above the breathing minimums for oxygen: a little more of another gas, or thinner air, and duplicants cannot breathe.", view.breathableGradient.Evaluate(0))
                };
                if (HarmfulGasRow(model) is (string label, string tooltip))
                    Info.infoUnits.Add(Unit(label, tooltip, view.toxicColour != null && view.toxicColour.Length != 0 ? view.toxicColour[view.toxicColour.Length - 1] : view.unbreathableColour));
                var low = LowOxygenRow(model);
                Info.infoUnits.Add(Unit(low.label + " or vacuum", low.tooltip, view.unbreathableColour));
            }
        }

        internal static void InitializeStrings()
        {
            RegisterStatusItemOverlays();
            var levels = new[] { "Colour only", "Texture contrast: very subtle", "Texture contrast: subtle", "Texture contrast: clear", "Texture contrast: strong" };
            for (var i = 0; i < levels.Length; ++i)
            {
                Strings.Add("STRINGS.UI.TOOLS.FILTERLAYERS.MA_TEXTURE_" + i + ".NAME", levels[i]);
                Strings.Add("STRINGS.UI.TOOLS.FILTERLAYERS.MA_TEXTURE_" + i + ".TOOLTIP",
                    "Composition texture contrast in the normal world view. Colours and exact readings remain available in every setting.");
            }
        }

        internal static Color ColorFor(MixtureOverlayKind kind, int cell)
        {
            var frame = MixturePresentationRuntime.AcquireFrame();
            if (frame == null || !frame.TryGetCell(cell, out var sample) || sample == null) return Color.clear;
            switch (kind)
            {
                case MixtureOverlayKind.Composition:
                    var rgb = sample.ColorAt(cell % frame.Width, cell / frame.Width);
                    return new Color((float)rgb.R, (float)rgb.G, (float)rgb.B, sample.GasMassKg > 0 ? 1 : 0);
                case MixtureOverlayKind.Pressure:
                    return PressureColor(sample.TotalPressurePa);
                case MixtureOverlayKind.OxygenMargin:
                    return MarginColor(OxygenMargin(sample, frame.Model));
                default:
                    // As ONI's Oxygen overlay: harmful gas before too little oxygen.
                    if ((sample.Failures & BreathingFailure.Vacuum) != 0) return Low;
                    if ((sample.Failures & Harmful) != 0) return Toxic;
                    return sample.IsBreathable ? Safe : Unsafe;
            }
        }

        internal static List<LegendEntry> Legend(MixtureOverlayKind kind)
        {
            var list = new List<LegendEntry>();
            if (!MixturePresentationRuntime.HasWorldProvider)
            {
                list.Add(new LegendEntry("No mixture readings available",
                    "Mixture colours and measurements are unavailable in this world. Blank areas do not indicate vacuum.",
                    Color.gray, displaySprite: false));
                return list;
            }
            if (kind == MixtureOverlayKind.Composition)
            {
                var model = MixturePresentationRuntime.AcquireFrame()?.Model;
                if (model != null)
                    foreach (var style in model.GasMaterialIds.Select(id => model.Styles[id]).OrderBy(value => value.DisplayName, StringComparer.Ordinal))
                        list.Add(new LegendEntry(style.DisplayName, "Each gas colours a tile by its share of the gas by moles (not by mass).",
                            new Color((float)style.Color.R, (float)style.Color.G, (float)style.Color.B)));
            }
            else if (kind == MixtureOverlayKind.Pressure)
            {
                list.Add(new LegendEntry("Vacuum", "No gas, or almost none.", PressureColor(0)));
                list.Add(new LegendEntry("50 kPa", "Thin: less than half the pressure of normal base air.", PressureColor(50000)));
                list.Add(new LegendEntry("125 kPa · normal base air", "1.6–1.8 kg of oxygen per tile reads about 122–138 kPa.", PressureColor(NormalPressurePa)));
                list.Add(new LegendEntry("250 kPa or more", "The scale ends here; the hover card shows the exact pressure.", PressureColor(MaximumPressurePa)));
            }
            else if (kind == MixtureOverlayKind.OxygenMargin)
            {
                // The loaded colony's minimums (docs/settings.md).
                var model = MixturePresentationRuntime.AcquireFrame()?.Model;
                var share = model?.MinimumOxygenFraction ?? .15; var pascals = model?.MinimumOxygenPartialPressurePa ?? 15000;
                list.Add(new LegendEntry("Below the breathing minimum", "Duplicants cannot breathe here: less than " + Percent(share) +
                    " oxygen by moles or less than " + Kilopascals(pascals) + " of oxygen (polluted oxygen counts).", MarginColor(.5)));
                list.Add(new LegendEntry("Barely breathable", "Just above " + Percent(share) + " and " + Kilopascals(pascals) +
                    ": a little more of another gas, or thinner air, and duplicants cannot breathe.", MarginColor(1)));
                list.Add(new LegendEntry("Comfortable", "At least four times the minimums: " + Percent(ComfortableMargin * share) + " oxygen and " +
                    Kilopascals(ComfortableMargin * pascals) + ". Carbon dioxide and other harmful gases show on the Breathability overlay.", MarginColor(ComfortableMargin)));
            }
            else
            {
                // The loaded colony's limits (docs/settings.md); a row only for a check that can fail.
                var model = MixturePresentationRuntime.AcquireFrame()?.Model;
                list.Add(new LegendEntry("Breathable", "Every breathing check passes.", Safe));
                var low = LowOxygenRow(model);
                list.Add(new LegendEntry(low.label, low.tooltip, Unsafe));
                if (HarmfulGasRow(model) is (string label, string tooltip)) list.Add(new LegendEntry(label, tooltip + " The hover card names the gas, even a trace too faint to see.", Toxic));
                list.Add(new LegendEntry("Vacuum", "No gas to breathe.", Low));
            }
            return list;
        }

        internal static void RefreshLegend()
        {
            if (OverlayLegend.Instance == null || OverlayScreen.Instance == null) return;
            var id = OverlayScreen.Instance.GetMode();
            foreach (MixtureOverlayKind kind in Enum.GetValues(typeof(MixtureOverlayKind)))
                if (id == Id(kind)) { OverlayLegend.Instance.SetLegend(new MixtureOverlayMode(kind), true); return; }
        }

        [HarmonyPatch(typeof(OverlayScreen), "RegisterModes")]
        private static class RegisterModesPatch
        {
            private static void Postfix(OverlayScreen __instance)
            {
                RegisterStatusItemOverlays();
                var register = AccessTools.Method(typeof(OverlayScreen), "RegisterMode");
                foreach (MixtureOverlayKind kind in Enum.GetValues(typeof(MixtureOverlayKind)))
                    register.Invoke(__instance, new object[] { new MixtureOverlayMode(kind) });
            }
        }

        [HarmonyPatch(typeof(OverlayMenu), "InitializeToggles")]
        private static class OverlayMenuPatch
        {
            private static void Postfix(OverlayMenu __instance)
            {
                var list = ReadField<IList>(typeof(OverlayMenu), "overlayToggleInfos", __instance);
                var type = AccessTools.Inner(typeof(OverlayMenu), "OverlayToggleInfo");
                foreach (MixtureOverlayKind kind in Enum.GetValues(typeof(MixtureOverlayKind)))
                    list.Add(Activator.CreateInstance(type, Name(kind), kind == MixtureOverlayKind.Composition ? "overlay_materials" : "overlay_oxygen",
                        Id(kind), "", global::Action.NumActions, Descriptions[(int)kind], Name(kind)));
            }
        }

        [HarmonyPatch(typeof(OverlayLegend), nameof(OverlayLegend.SetLegend), new[] { typeof(OverlayModes.Mode), typeof(bool) })]
        private static class LegendPatch
        {
            private static void Prefix(OverlayLegend __instance, OverlayModes.Mode __0)
            {
                var list = ReadField<List<OverlayLegend.OverlayInfo>>(typeof(OverlayLegend), "overlayInfoList", __instance);
                if (!MixturePresentationRuntime.HasWorldProvider)
                {
                    if (OxygenLegends.TryGetValue(__instance, out var previous)) previous.Restore();
                }
                else if (__0.ViewMode() == OverlayModes.Oxygen.ID && SimDebugView.Instance != null)
                {
                    var original = list.First(info => info.mode == OverlayModes.Oxygen.ID);
                    OxygenLegends.GetValue(__instance, _ => new NativeOxygenLegend(original)).Apply(SimDebugView.Instance);
                }
                if (!(__0 is MixtureOverlayMode)) return;
                if (list.Any(info => info.mode == __0.ViewMode())) return;
                var kind = Enum.GetValues(typeof(MixtureOverlayKind)).Cast<MixtureOverlayKind>().Single(value => Id(value) == __0.ViewMode());
                list.Add(new OverlayLegend.OverlayInfo
                { mode = Id(kind), name = Name(kind), isProgrammaticallyPopulated = true, infoUnits = new List<OverlayLegend.OverlayInfoUnit>(), diagrams = new List<GameObject>() });
            }
        }

        [HarmonyPatch(typeof(SimDebugView), "OnPrefabInit")]
        private static class ColorRegistrationPatch
        {
            private static void Postfix(SimDebugView __instance)
            {
                var colors = ReadField<Dictionary<HashedString, Func<SimDebugView, int, Color>>>(typeof(SimDebugView), "getColourFuncs", __instance);
                var updates = ReadField<Dictionary<HashedString, System.Action<SimDebugView, Texture>>>(typeof(SimDebugView), "dataUpdateFuncs", __instance);
                var point = (System.Action<SimDebugView, Texture>)Delegate.CreateDelegate(typeof(System.Action<SimDebugView, Texture>), AccessTools.Method(typeof(SimDebugView), "SetDefaultPoint"));
                foreach (MixtureOverlayKind kind in Enum.GetValues(typeof(MixtureOverlayKind)))
                {
                    var captured = kind;
                    colors[Id(kind)] = (_, cell) => ColorFor(captured, cell);
                    updates[Id(kind)] = point;
                }
            }
        }
    }
}
