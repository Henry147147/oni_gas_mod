using System;
using System.Collections.Generic;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // What the player is told (docs/colony-owner.md#activation-and-data-flow):
    // one dialog per distinct problem and session, beside the log line.
    // Validation scenes only log; their reports carry the detail.
    internal static class ColonyNotices
    {
        private static readonly HashSet<string> shown = new HashSet<string>();
        internal static int Shown { get; private set; }
        internal static void Reset() { shown.Clear(); }
        internal static void Show(string title, string message)
        {
            Debug.LogWarning("[Mixed Atmospheres] " + title + ": " + message);
            if (ColonyRuntimeValidation.Settings != null || !shown.Add(title + "\n" + message)) return;
            try
            {
                if (GameScreenManager.Instance == null || ScreenPrefabs.Instance == null) return;
                ((ConfirmDialogScreen)GameScreenManager.Instance.StartScreen(ScreenPrefabs.Instance.ConfirmDialogScreen.gameObject,
                    GameScreenManager.Instance.ssOverlayCanvas.gameObject)).PopupConfirmDialog(message, null, null, title_text: title.ToUpperInvariant());
                ++Shown;
            }
            catch (Exception error) { Debug.LogWarning("[Mixed Atmospheres] The notice could not be shown: " + error.Message); }
        }
    }
}
