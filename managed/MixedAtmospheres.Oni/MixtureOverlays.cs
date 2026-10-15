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
    internal enum MixtureOverlayKind { Composition, Pressure, OxygenPartialPressure, Breathability }

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
        private static readonly string[] Names = { "Gas composition", "Gas pressure", "Oxygen partial pressure", "Breathability" };
        private static readonly string[] Descriptions =
        {
            "Gas colours show constituent mole fractions. Inspect a cell for every constituent, including traces.",
            "Total gas pressure in kPa. Gas pressure is calculated from the complete mixture and its available volume.",
            "Oxygen partial pressure in kPa. The same atmosphere assessment drives breathing warnings.",
            "Breathing safety includes oxygen fraction, oxygen partial pressure, carbon dioxide and trace contaminants."
        };
        internal static HashedString Id(MixtureOverlayKind kind) => "MixedAtmospheres." + kind;
        internal static string Name(MixtureOverlayKind kind) => Names[(int)kind];
        private static readonly Color Safe = new Color(0.23f, 0.75f, 0.65f);
        private static readonly Color Unsafe = new Color(0.83f, 0.24f, 0.20f);
        private static readonly Color Toxic = new Color(0.85f, 0.36f, 0.79f);
        private static readonly Color Low = new Color(0.16f, 0.22f, 0.40f);
        private static readonly Color High = new Color(0.96f, 0.73f, 0.26f);
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
                Info.name = "Mixture breathability · owned cells";
                Info.isProgrammaticallyPopulated = false;
                Info.diagrams = new List<GameObject>();
                OverlayLegend.OverlayInfoUnit Unit(string text, string description, Color color) =>
                    new OverlayLegend.OverlayInfoUnit(icon, text, color, Color.white) { tooltip = description };
                Info.infoUnits = new List<OverlayLegend.OverlayInfoUnit>
                {
                    Unit("Breathable · all checks pass", "For owned mixture cells: oxygen fraction, oxygen partial pressure, carbon dioxide and contaminant limits all pass. Unowned cells retain ONI's native oxygen colours.", view.breathableGradient.Evaluate(1)),
                    Unit("Carbon dioxide / contaminants · unsafe", "Inspect for the exact constituent and limit; a hazardous trace can be visually imperceptible.",
                        view.toxicColour != null && view.toxicColour.Length != 0 ? view.toxicColour[view.toxicColour.Length - 1] : view.unbreathableColour),
                    Unit("Oxygen / pressure / vacuum · unsafe", "Low oxygen, thin atmosphere, excessive oxygen or vacuum in an owned mixture cell. This presentation does not replace gameplay breathing.", view.unbreathableColour)
                };
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
                    return Color.Lerp(Low, High, Mathf.Clamp01((float)(sample.TotalPressurePa / 200000)));
                case MixtureOverlayKind.OxygenPartialPressure:
                    var threshold = frame.Model.MinimumOxygenPartialPressurePa;
                    return Color.Lerp(Unsafe, Safe, Mathf.Clamp01((float)(sample.OxygenPartialPressurePa / threshold)));
                default:
                    if ((sample.Failures & BreathingFailure.Contaminant) != 0) return Toxic;
                    if ((sample.Failures & BreathingFailure.Vacuum) != 0) return Low;
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
                        list.Add(new LegendEntry(style.DisplayName, "Colour contribution is weighted by gas mole fraction.",
                            new Color((float)style.Color.R, (float)style.Color.G, (float)style.Color.B)));
                list.Add(new LegendEntry("Mole fractions, not mass fractions", Descriptions[0], Color.white, displaySprite: false));
            }
            else if (kind == MixtureOverlayKind.Pressure)
            {
                foreach (var kpa in new[] { 0, 15, 50, 101.325, 200 })
                    list.Add(new LegendEntry(kpa.ToString("0.###", CultureInfo.InvariantCulture) + " kPa", "Total gas pressure.", Color.Lerp(Low, High, (float)(kpa / 200))));
                list.Add(new LegendEntry("Above 200 kPa", "The scale saturates; exact pressure remains in the inspection card.", High));
            }
            else if (kind == MixtureOverlayKind.OxygenPartialPressure)
            {
                list.Add(new LegendEntry("0 kPa O2", "No oxygen partial pressure.", Unsafe));
                var threshold = MixturePresentationRuntime.AcquireFrame()?.Model.MinimumOxygenPartialPressurePa ?? 15000;
                list.Add(new LegendEntry((threshold / 1000).ToString("0.###", CultureInfo.InvariantCulture) + " kPa O2", "Active minimum oxygen partial pressure. Other breathing checks also apply.", Safe));
                list.Add(new LegendEntry("Partial pressure is only one breathing check", Descriptions[2], Color.white, displaySprite: false));
            }
            else
            {
                list.Add(new LegendEntry("Breathable", "All configured breathing checks pass.", Safe));
                list.Add(new LegendEntry("Unsafe atmosphere", "Inspect for oxygen fraction, thin atmosphere or carbon dioxide warnings.", Unsafe));
                list.Add(new LegendEntry("Contaminated", "A trace constituent exceeds its configured limit, even when its tint is imperceptible.", Toxic));
                list.Add(new LegendEntry("Vacuum / no gas", "There is no gas available for breathing.", Low));
            }
            list.Add(new LegendEntry("Cells without readings are left blank",
                "Inspect a cell to see its mixture measurements. A blank area does not indicate vacuum.", Color.white, displaySprite: false));
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
