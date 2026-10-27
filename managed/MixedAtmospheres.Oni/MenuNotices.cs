using System;
using System.Collections.Generic;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Main-menu notices for refusals (docs/game-update-resilience.md#refusals).
    // This needs no Harmony patch, so a mod that refuses to run before
    // patching anything (unsupported platform, second copy, unverified game
    // build) can still tell the player why. Each distinct notice shows once
    // per process, one dialog at a time, as soon as the main menu exists.
    internal sealed class MenuNotices : MonoBehaviour
    {
        private static MenuNotices? instance;
        private static readonly Queue<KeyValuePair<string, string>> pending = new Queue<KeyValuePair<string, string>>();
        private static readonly HashSet<string> posted = new HashSet<string>();
        private ConfirmDialogScreen? open;

        internal static void Post(string title, string message)
        {
            Debug.LogWarning("[Mixed Atmospheres] " + title + ": " + message);
            if (!posted.Add(title + "\n" + message)) return;
            pending.Enqueue(new KeyValuePair<string, string>(title, message));
            try
            {
                if (instance != null) return;
                var owner = new GameObject("MixedAtmospheresMenuNotices");
                DontDestroyOnLoad(owner);
                instance = owner.AddComponent<MenuNotices>();
            }
            catch (Exception error) { Debug.LogWarning("[Mixed Atmospheres] Notices cannot be shown: " + error.Message); }
        }

        private void Update()
        {
            if (pending.Count == 0 || (open != null && open.gameObject.activeInHierarchy)) return;
            if (MainMenu.Instance == null || FrontEndManager.Instance == null || ScreenPrefabs.Instance == null) return;
            var notice = pending.Dequeue();
            try
            {
                open = Util.KInstantiateUI(ScreenPrefabs.Instance.ConfirmDialogScreen.gameObject, FrontEndManager.Instance.gameObject, true)
                    .GetComponent<ConfirmDialogScreen>();
                open.PopupConfirmDialog(notice.Value, null, null, title_text: notice.Key.ToUpperInvariant());
            }
            catch (Exception error) { open = null; Debug.LogWarning("[Mixed Atmospheres] The notice could not be shown: " + error.Message); }
        }
    }
}
