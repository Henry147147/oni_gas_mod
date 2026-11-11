using System;
using System.Collections.Generic;
using System.Globalization;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // An "Atmospheres" button beside Vitals in ONI's top management bar that
    // shows or hides the status panel (user decision 2026-11-08). It is a
    // plain push button: it opens no management screen, so its click is
    // detached from the menu's screen toggling and toggle group.
    //
    // While the game clock runs well below the chosen speed (ColonyPacing.
    // SlowedDown: the solver cannot keep up), its label shows the actual
    // speed in a warning colour, e.g. "Speed 0.62×", and its tooltip says
    // why; otherwise it reads "Atmospheres". No sound, popup or other text.
    internal static class StatusPanelMenuButton
    {
        internal const string Label = "Atmospheres";
        private static ManagementMenu.ManagementMenuToggleInfo? info;
        private static bool warned;
        private static readonly Color Slow = new Color(1, .62f, .3f);
        private static LocText? label;
        private static string normalText = Label, shownText = "";
        private static Color normalColour = Color.white;
        private static bool showingSlow;

        // "Speed 0.62×" while slowed down, else null.
        internal static string? SlowText(bool slowed, double rate) =>
            slowed ? "Speed " + rate.ToString(rate >= 1 ? "0.#" : "0.##", CultureInfo.InvariantCulture) + "×" : null;
        private static bool Slowed => ColonyOwner.Active && ColonyPacing.SlowedDown && SpeedControlScreen.Instance != null && !SpeedControlScreen.Instance.IsPaused;

        // Five times a second (MixtureStatusDisplay): the label follows the slowdown.
        internal static void Refresh()
        {
            if (info?.toggle == null) return;
            var text = SlowText(Slowed, ColonyPacing.GameRate);
            if (text == null && !showingSlow) return;
            try
            {
                if (label == null)
                {
                    label = info.toggle.GetComponentInChildren<LocText>(true);
                    if (label == null) return; // no text label: the tooltip still explains
                    normalText = label.text; normalColour = label.color;
                }
                if (text != null)
                {
                    if (text != shownText) { label.text = text; shownText = text; }
                    label.color = Slow; showingSlow = true;
                }
                else { label.text = normalText; label.color = normalColour; showingSlow = false; shownText = ""; }
            }
            catch (Exception error) when (!(error is OutOfMemoryException)) { label = null; showingSlow = false; Warn(error); }
        }
        // The tooltip's reason while slowed down.
        private static string? SlowReason()
        {
            if (!Slowed) return null;
            var chosen = ColonyPacing.ChosenSpeed.ToString("0.#", CultureInfo.InvariantCulture) + "×";
            var actual = ColonyPacing.GameRate.ToString(ColonyPacing.GameRate >= 1 ? "0.#" : "0.##", CultureInfo.InvariantCulture) + "×";
            return "The game runs at " + actual + " instead of your chosen " + chosen + ": " +
                (ColonyPacing.TimeScale < ColonyPacing.SlowFraction * ColonyPacing.ChosenSpeed ? "the fluid simulation cannot keep up, so the game clock waits for it." :
                    "the game's frames cannot keep up.");
        }

        private static void Warn(Exception error)
        {
            if (warned) return;
            warned = true;
            Debug.LogWarning("[Mixed Atmospheres] The top-bar status panel button is unavailable: " + error.Message);
        }

        [HarmonyPatch(typeof(KIconToggleMenu), nameof(KIconToggleMenu.Setup), new[] { typeof(IList<KIconToggleMenu.ToggleInfo>) })]
        private static class SetupPatch
        {
            private static void Prefix(KIconToggleMenu __instance, IList<KIconToggleMenu.ToggleInfo> __0)
            {
                if (!(__instance is ManagementMenu menu) || __0 == null || __0.IsReadOnly) return;
                try
                {
                    var button = new ManagementMenu.ManagementMenuToggleInfo(Label, "overlay_oxygen", null, global::Action.NumActions,
                        "Show or hide the Mixed Atmospheres panel.", "MIXED ATMOSPHERES");
                    button.getTooltipText = () =>
                    {
                        var lines = new List<Tuple<string, TextStyleSetting>>
                        {
                            new Tuple<string, TextStyleSetting>(button.tooltipHeader, ToolTipScreen.Instance.defaultTooltipHeaderStyle),
                            new Tuple<string, TextStyleSetting>(button.tooltip, ToolTipScreen.Instance.defaultTooltipBodyStyle)
                        };
                        if (SlowReason() is string reason) lines.Insert(1, new Tuple<string, TextStyleSetting>(reason, ToolTipScreen.Instance.defaultTooltipBodyStyle));
                        return lines;
                    };
                    var vitals = Traverse.Create(menu).Field("vitalsInfo").GetValue<KIconToggleMenu.ToggleInfo>();
                    var index = vitals == null ? -1 : __0.IndexOf(vitals);
                    __0.Insert(index < 0 ? 0 : index, button);
                    info = button; label = null; showingSlow = false; shownText = "";
                }
                catch (Exception error) when (!(error is OutOfMemoryException)) { info = null; Warn(error); }
            }
        }

        [HarmonyPatch(typeof(KIconToggleMenu), "RefreshButtons")]
        private static class RefreshPatch
        {
            private static void Postfix(KIconToggleMenu __instance)
            {
                if (!(__instance is ManagementMenu) || info?.toggle == null) return;
                try
                {
                    var toggle = info.toggle;
                    toggle.ClearOnClick();
                    toggle.group = null;
                    toggle.onClick += Clicked;
                    toggle.isOn = false;
                }
                catch (Exception error) when (!(error is OutOfMemoryException)) { Warn(error); }
            }
        }

        private static void Clicked()
        {
            var shown = MixtureStatusDisplay.ToggleVisible();
            KMonoBehaviour.PlaySound(GlobalAssets.GetSound(shown ? "HUD_Click_Open" : "HUD_Click_Close"));
            // KToggle.Click flipped its state; keep the button unhighlighted.
            if (info?.toggle != null) info.toggle.isOn = false;
        }
    }
}
