using System.Linq;

namespace MixedAtmospheres.Oni
{
    // The release supports the base game and Spaced Out! only (user decision
    // 2026-10-27). With any other gameplay pack active the colony owner never
    // takes a colony over and Mixed Atmospheres saves are not loaded; the
    // decisions themselves are ModAdmission's pure functions.
    internal static class GameplayPacks
    {
        internal static bool IsCosmetic(string id) =>
            DlcManager.CONTENT_ONLY_DLC_IDS.Contains(id) || (DlcManager.DLC_PACKS.TryGetValue(id, out var info) && info.isCosmetic);

        internal static string[] ActiveUnsupported() => ModAdmission.UnsupportedPacks(DlcManager.GetActiveDLCIds(), IsCosmetic);

        internal static void NoticeAtMainMenu()
        {
            var packs = ActiveUnsupported();
            if (packs.Length == 0) return;
            if (ColonyRuntimeValidation.Settings != null) UnityEngine.Debug.LogWarning("[Mixed Atmospheres] " + ModAdmission.PacksMenuNotice(packs));
            else MenuNotices.Post("Mixed Atmospheres inactive", ModAdmission.PacksMenuNotice(packs));
        }
    }
}
