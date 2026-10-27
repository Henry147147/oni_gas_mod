using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    // What the mod may run on and which colonies it may take over
    // (docs/game-update-resilience.md#refusals). Pure decisions with no game
    // types, so the refusals have unit controls even for content and
    // platforms that are not installed here. Every refusal text is shown to the
    // player and names its reason in plain words.
    internal static class ModAdmission
    {
        internal const string Title = "Mixed Atmospheres";
        internal const string SpacedOutId = "EXPANSION1_ID";

        // Gameplay packs this release supports: the base game ("") and Spaced
        // Out! (user decision 2026-10-27). Cosmetic-only packs never change
        // gases or liquids, so they are allowed.
        private static readonly Dictionary<string, string> PackNames = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["DLC2_ID"] = "The Frosty Planet Pack",
            ["DLC3_ID"] = "The Bionic Booster Pack",
            ["DLC4_ID"] = "The Prehistoric Planet Pack",
            ["DLC5_ID"] = "The Aquatic Planet Pack",
        };

        // Null when this process can run the mod; otherwise the reason.
        internal static string? PlatformRefusal(bool windows, bool linux, Architecture process, int pointerBytes, string description)
        {
            if ((windows || linux) && process == Architecture.X64 && pointerBytes == 8) return null;
            var system = windows ? "Windows" : linux ? "Linux" : string.IsNullOrWhiteSpace(description) ? "this operating system" : description.Trim();
            return "Mixed Atmospheres runs only on 64-bit Windows and 64-bit Linux. This game is running on " + system + " (" +
                process.ToString().ToLowerInvariant() + "), so the mod has not changed anything and the game runs as normal.";
        }

        internal static string? CurrentPlatformRefusal() => PlatformRefusal(
            RuntimeInformation.IsOSPlatform(OSPlatform.Windows), RuntimeInformation.IsOSPlatform(OSPlatform.Linux),
            RuntimeInformation.ProcessArchitecture, IntPtr.Size, RuntimeInformation.OSDescription);

        // Active gameplay packs other than the base game and Spaced Out!, in a
        // stable order. An unknown future pack is unsupported unless the game
        // marks it cosmetic.
        internal static string[] UnsupportedPacks(IEnumerable<string>? activeIds, Func<string, bool> isCosmetic)
        {
            if (activeIds == null) return Array.Empty<string>();
            return activeIds.Where(id => id != null && id.Length != 0 && id != SpacedOutId && !isCosmetic(id))
                .Distinct(StringComparer.Ordinal).OrderBy(id => id, StringComparer.Ordinal).ToArray();
        }

        internal static string PackName(string id) => PackNames.TryGetValue(id, out var name) ? name : id;

        internal static string PackList(IReadOnlyList<string> ids)
        {
            var names = ids.Select(PackName).ToArray();
            return names.Length <= 1 ? string.Concat(names) :
                string.Join(", ", names, 0, names.Length - 1) + " and " + names[names.Length - 1];
        }

        // Shown at the main menu while unsupported packs are active.
        internal static string PacksMenuNotice(IReadOnlyList<string> ids) =>
            "Mixed Atmospheres supports the base game and Spaced Out! only. " + PackList(ids) + (ids.Count == 1 ? " is" : " are") +
            " active, so new colonies keep ONI's own gases and liquids and Mixed Atmospheres colonies cannot be loaded.\n\n" +
            "To use Mixed Atmospheres, turn " + (ids.Count == 1 ? "that pack" : "those packs") + " off in the main menu's content settings and start a new colony.";

        // Shown when a new colony starts with unsupported packs active.
        internal static string PacksColonyNotice(IReadOnlyList<string> ids) =>
            "Mixed Atmospheres supports the base game and Spaced Out! only, and " + PackList(ids) + (ids.Count == 1 ? " is" : " are") +
            " active. This colony keeps ONI's own gases and liquids; Mixed Atmospheres will not take it over.";

        // Why a Mixed Atmospheres colony save cannot be opened with these packs.
        internal static string PacksLoadRefusal(IReadOnlyList<string> ids) =>
            "This colony uses Mixed Atmospheres, which supports the base game and Spaced Out! only, but " + PackList(ids) +
            (ids.Count == 1 ? " is" : " are") + " active. Turn " + (ids.Count == 1 ? "that pack" : "those packs") +
            " off in the main menu's content settings, then load the colony again. The save has not been changed.";

        // A second copy in the same process. Keep the Steam Workshop copy when
        // there is one; otherwise keep the copy that loaded first.
        internal static string DuplicateNotice(string runningPath, string runningPlatform, string otherPath, string otherPlatform)
        {
            var removeOther = !(otherPlatform == "Steam" && runningPlatform != "Steam");
            var remove = removeOther ? otherPath : runningPath;
            var keep = removeOther ? runningPath : otherPath;
            return "Mixed Atmospheres is installed and enabled twice:\n  " + runningPath + " (" + runningPlatform + ", running)\n  " +
                otherPath + " (" + otherPlatform + ", not loaded)\n\nOnly one copy can run. Close the game, delete or disable the copy in\n  " + remove +
                "\nand keep\n  " + keep + "\nThen start the game again. Your settings, presets and saves are kept outside these folders.";
        }
    }
}
