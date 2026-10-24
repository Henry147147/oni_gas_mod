using System;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // A colony save whose owner (or pipe runtime) cannot be restored must not
    // run on ONI's own fluid simulation (docs/colony-owner.md#saves). The
    // refusal holds simulation time, tells the player why and returns to the
    // main menu; the save file is left unchanged.
    internal static class ColonyLoadRefusal
    {
        private static bool shown;
        internal static string Reason { get; private set; } = "";
        internal static bool Refused => Reason.Length != 0;
        internal static void Reset() { Reason = ""; shown = false; }
        internal static void Refuse(Exception error)
        {
            if (Refused) return;
            Reason = error.Message.Length != 0 ? error.Message : error.GetType().Name;
            Debug.LogError("[Mixed Atmospheres] This colony cannot be loaded: " + Reason + "\n" + error);
        }
        // Unity update, outside the simulation step: one dialog, then the menu.
        internal static void Update()
        {
            if (!Refused || shown || Game.Instance == null) return;
            shown = true;
            try { SpeedControlScreen.Instance?.Pause(false); } catch (Exception) { }
            void Quit() => LoadingOverlay.Load(PauseScreen.TriggerQuitGame);
            // Validation records the refusal and ends the run here (its driver would reload from the menu).
            if (ColonyRuntimeValidation.Settings != null) { ColonyRuntimeValidation.RecordLoadRefusal(Reason); return; }
            try
            {
                ((ConfirmDialogScreen)GameScreenManager.Instance.StartScreen(ScreenPrefabs.Instance.ConfirmDialogScreen.gameObject,
                    GameScreenManager.Instance.ssOverlayCanvas.gameObject)).PopupConfirmDialog(
                    "Mixed Atmospheres cannot continue this colony, so it was not loaded and the save file is unchanged.\n\n" + Reason,
                    Quit, null, title_text: "COLONY NOT LOADED");
            }
            catch (Exception error) { Debug.LogWarning("[Mixed Atmospheres] The load refusal could not be shown: " + error.Message); Quit(); }
        }
    }

    [HarmonyPatch(typeof(Game), "SimEveryTick")]
    internal static class ColonyLoadRefusalHoldPatch
    {
        [HarmonyPriority(Priority.First)]
        private static bool Prefix() { ColonyLoadRefusal.Update(); return !ColonyLoadRefusal.Refused; }
    }
}
