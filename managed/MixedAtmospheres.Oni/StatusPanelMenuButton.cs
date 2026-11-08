using System;
using System.Collections.Generic;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // An "Atmospheres" button beside Vitals in ONI's top management bar that
    // shows or hides the status panel (user decision 2026-11-08). It is a
    // plain push button: it opens no management screen, so its click is
    // detached from the menu's screen toggling and toggle group.
    internal static class StatusPanelMenuButton
    {
        internal const string Label = "Atmospheres";
        private static ManagementMenu.ManagementMenuToggleInfo? info;
        private static bool warned;

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
                    button.getTooltipText = () => new List<Tuple<string, TextStyleSetting>>
                    {
                        new Tuple<string, TextStyleSetting>(button.tooltipHeader, ToolTipScreen.Instance.defaultTooltipHeaderStyle),
                        new Tuple<string, TextStyleSetting>(button.tooltip, ToolTipScreen.Instance.defaultTooltipBodyStyle)
                    };
                    var vitals = Traverse.Create(menu).Field("vitalsInfo").GetValue<KIconToggleMenu.ToggleInfo>();
                    var index = vitals == null ? -1 : __0.IndexOf(vitals);
                    __0.Insert(index < 0 ? 0 : index, button);
                    info = button;
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
