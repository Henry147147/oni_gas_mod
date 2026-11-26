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

        // Where a player reports a problem. Named in every notice the player
        // cannot resolve alone.
        internal const string ReportPlaces = "the Mixed Atmospheres page on the Steam Workshop or github.com/Henry147147/oni_gas_mod/issues";

        private static string Report(string lead = "please report it") => lead + " with your Player.log on " + ReportPlaces + ".";

        private static string Untouched(string until) =>
            "\n\nThe mod has not changed the game. Colonies started with Mixed Atmospheres are untouched and can be loaded again once " + until + ".";

        private static string Parts(int count) => count == 1 ? "one part" : count + " parts";
        private static string Methods(int count) => count == 1 ? "one method" : count + " methods";

        // "changed static SaveLoader::Save(System.String,...)System.String" as
        // "SaveLoader.Save": what fits a dialog. Player.log keeps the full form.
        internal static string ShortPart(string difference)
        {
            var at = difference.IndexOf("::", StringComparison.Ordinal);
            if (at < 0) return difference;
            var owner = difference.LastIndexOf(' ', at) + 1;
            var end = at + 2;
            while (end < difference.Length && (char.IsLetterOrDigit(difference[end]) || difference[end] == '_' || difference[end] == '.' || difference[end] == '*')) ++end;
            var verb = difference.Substring(0, Math.Max(0, difference.IndexOf(' ')));
            return difference.Substring(owner, at - owner).Replace('/', '.') + "." + difference.Substring(at + 2, end - at - 2) +
                (verb == "missing" || verb == "new" || verb == "unreadable" ? " (" + verb + ")" : "");
        }

        private static string Shown(IReadOnlyList<string> lines, int limit, string rest)
        {
            var shown = lines.Take(limit).Select(line => "  - " + line).ToArray();
            return string.Join("\n", shown) + (lines.Count > shown.Length ? "\n  - and " + (lines.Count - shown.Length) + " more" + (rest.Length != 0 ? " " + rest : "") + " (listed in Player.log)" : "");
        }

        // Why the mod stays off before patching anything
        // (docs/game-update-resilience.md#refusals): the main-menu notice,
        // what a refused mixture save says, and the lines for Player.log.
        internal sealed class GameRefusal
        {
            internal string Kind = "", Notice = "", LoadReason = "", LoadRemedy = "";
            internal IReadOnlyList<string> Details = Array.Empty<string>();

            // A mixture save while the mod is refused.
            internal string LoadRefusal =>
                "This colony uses Mixed Atmospheres, which is not active " + LoadReason + ", so it was not loaded. The save has not been changed. " +
                LoadRemedy + ", then load the colony again.";
        }

        // The game is a build this copy of the mod was not made for: the game
        // was updated first, or the installed copy of the mod is an old one.
        internal static GameRefusal UnsupportedBuild(string version, int running, int supported)
        {
            var game = running > 0 ? "build " + running : "an unknown build";
            var older = running > 0 && running < supported;
            return new GameRefusal
            {
                Kind = "unsupported game build",
                Notice = "Mixed Atmospheres " + version + " does not support this version of Oxygen Not Included (the game is " + game + "; this copy of the mod is for build " + supported + ").\n\n" +
                    (older ? "The game is older than the mod expects. Update Oxygen Not Included, or leave the previous-version branch in its Steam properties."
                        : "The game was probably updated before the mod. Steam installs the next Mixed Atmospheres update by itself; if this notice stays after an update " +
                          "was released, unsubscribe from the mod and subscribe again.") +
                    Untouched("a Mixed Atmospheres version for this game build is installed"),
                LoadReason = "on this version of Oxygen Not Included (" + game + ")",
                LoadRemedy = older ? "Update Oxygen Not Included" : "Install a Mixed Atmospheres update for this game version",
                Details = new[] { "the game is " + game + "; this adapter was verified for build " + supported },
            };
        }

        // `text` with every whole-word occurrence of the game build number
        // (no digit right before or after it) replaced by `mark`; the pieces
        // between occurrences pass through `piece` (string escaping). Exact
        // and narrow: 0 or a negative build masks nothing
        // (docs/game-update-resilience.md#tolerated-newer-builds).
        internal static string MaskBuild(string text, int build, string mark, Func<string, string>? piece = null)
        {
            piece ??= part => part;
            if (build <= 0) return piece(text);
            var number = build.ToString(System.Globalization.CultureInfo.InvariantCulture);
            var result = new System.Text.StringBuilder();
            var from = 0;
            for (var at = text.IndexOf(number, StringComparison.Ordinal); at >= 0; at = text.IndexOf(number, at + 1, StringComparison.Ordinal))
            {
                var end = at + number.Length;
                if (at > 0 && char.IsDigit(text[at - 1]) || end < text.Length && char.IsDigit(text[end])) continue;
                result.Append(piece(text.Substring(from, at - from))).Append(mark);
                from = end; at = end - 1;
            }
            return result.Append(piece(text.Substring(from))).ToString();
        }

        // Is a running build newer than the contract's checked with the
        // build-number reading? Only by the package root, which ONI loads for
        // every build from the newest supported one on; an adapter under
        // archived_versions/ is loaded for a build between two supported
        // ones, and an older or in-between build stays refused by its number.
        internal static bool ChecksNewerBuild(int running, int contract, bool atPackageRoot) => atPackageRoot && contract > 0 && running > contract;

        internal static bool AtPackageRoot(string modDirectory)
        {
            var trimmed = System.IO.Path.GetFullPath(modDirectory).TrimEnd(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar);
            var folder = System.IO.Path.GetFileName(System.IO.Path.GetDirectoryName(trimmed) ?? "");
            return !string.Equals(folder, "archived_versions", StringComparison.OrdinalIgnoreCase);
        }

        // The game is newer than the build this copy was checked against, and
        // parts of the game the mod uses changed beyond the build number
        // (docs/game-update-resilience.md#tolerated-newer-builds): an update
        // of the mod is needed. `differences` are the contract's wording.
        internal static GameRefusal NewerBuildChanged(string version, int running, int supported, IReadOnlyList<string> differences)
        {
            var parts = differences.Select(ShortPart).Distinct(StringComparer.Ordinal).ToArray();
            var refusal = UnsupportedBuild(version, running, supported);
            refusal.Notice = "Mixed Atmospheres " + version + " does not support this version of Oxygen Not Included (the game is build " + running +
                "; this copy of the mod is for build " + supported + "). The game update changed " + Parts(parts.Length) + " of the game the mod uses:\n" +
                Shown(parts, 3, "") + "\n\n" +
                "An update of Mixed Atmospheres is needed. Steam installs it by itself once it is released; if this notice stays after an update " +
                "was released, unsubscribe from the mod and subscribe again." +
                Untouched("a Mixed Atmospheres version for this game build is installed");
            refusal.Details = new[] { "the game is build " + running + ", newer than build " + supported + " this version was checked against; changed: " + string.Join("; ", parts) }
                .Concat(differences).ToArray();
            return refusal;
        }

        // Player.log, once, when a newer game is accepted because every part
        // of the game the mod uses is unchanged apart from the build number.
        // No notice: nothing needs the player's attention.
        internal static string ToleratedBuildLog(int running, int supported, int checkedEntries, IReadOnlyList<string> forgiven, double milliseconds) =>
            "Game build " + running + " is newer than the build this version was checked against (" + supported + "); every part of the game the mod uses is unchanged (" +
            checkedEntries + " managed dependencies checked in " + milliseconds.ToString("F0", System.Globalization.CultureInfo.InvariantCulture) + " ms); " +
            (forgiven.Count == 1 ? "1 entry differs" : forgiven.Count + " entries differ") + " only by the build number:" +
            string.Concat(forgiven.Select(entry => "\n  " + entry));

        // The build is supported and its code is the verified code, but other
        // mods patched methods the mod depends on before it loaded. `parts`
        // pairs each such game method with the mods that patched it.
        internal static GameRefusal OtherMods(int build, IReadOnlyList<KeyValuePair<string, string[]>> parts)
        {
            var mods = parts.SelectMany(part => part.Value.Distinct(StringComparer.Ordinal)).GroupBy(name => name, StringComparer.Ordinal)
                .OrderByDescending(group => group.Count()).ThenBy(group => group.Key, StringComparer.OrdinalIgnoreCase)
                .Select(group => group.Key + " (" + Methods(group.Count()) + ")").ToArray();
            return new GameRefusal
            {
                Kind = "other mods changed game code the mod depends on",
                Notice = "Mixed Atmospheres is not active because " + (mods.Length == 1 ? "another mod has" : "other mods have") + " changed game code it depends on. " +
                    "This version of Mixed Atmospheres supports your game (build " + build + "): neither is out of date.\n\n" +
                    (parts.Count == 1 ? "One game method it uses was" : parts.Count + " game methods it uses were") + " patched by:\n" +
                    Shown(mods, 6, "mods") + "\n\n" +
                    "Try disabling " + (mods.Length == 1 ? "that mod" : "those mods") + " and restart the game. To use " + (mods.Length == 1 ? "it" : "them") +
                    " together with Mixed Atmospheres, " + Report("please report the conflict") +
                    Untouched("the conflict is gone"),
                LoadReason = "because other mods have changed game code it depends on (the notice at the main menu names them)",
                LoadRemedy = "Disable those mods and restart the game",
                Details = parts.Select(part => part.Key + " <- " + string.Join(", ", part.Value)).ToArray(),
            };
        }

        // The build number is a supported one, yet the game's code is not
        // that build's and no mod's patch accounts for it: files replaced by a
        // mod or tool, or damaged.
        internal static GameRefusal GameFiles(int build, IReadOnlyList<string> differences)
        {
            var parts = differences.Select(ShortPart).Distinct(StringComparer.Ordinal).ToArray();
            return new GameRefusal
            {
                Kind = "the game's code differs from the supported build",
                Notice = "Mixed Atmospheres is not active. Your game reports a version the mod supports (build " + build + "), but " + Parts(differences.Count) +
                    " of its code " + (differences.Count == 1 ? "differs" : "differ") + " from that version:\n" + Shown(parts, 3, "") + "\n\n" +
                    "The game is not out of date. Usually another mod or tool has modified the game's files, or files are damaged. Try disabling other mods, and " +
                    "\"Verify integrity of game files\" in the game's Steam properties. If this notice stays, " + Report() +
                    Untouched("the game's code is that of build " + build + " again"),
                LoadReason = "because the game's code differs from the supported build " + build,
                LoadRemedy = "Disable other mods or verify the game's files in Steam",
                Details = differences.ToArray(),
            };
        }

        // The mod's own files do not belong together.
        internal static GameRefusal Installation(string reason) => new GameRefusal
        {
            Kind = "the mod's installation is incomplete",
            Notice = "Mixed Atmospheres is not active because its own files are incomplete or come from two versions (" + reason + "). The game is not out of date.\n\n" +
                "Unsubscribe from the mod and subscribe again, or replace a copy you installed by hand, then restart the game. If this notice stays, " + Report() +
                Untouched("the mod is installed completely"),
            LoadReason = "because its installation is incomplete",
            LoadRemedy = "Reinstall Mixed Atmospheres",
            Details = new[] { reason },
        };

        // The check itself failed.
        internal static GameRefusal CheckFailed(string reason) => new GameRefusal
        {
            Kind = "the check of the game failed",
            Notice = "Mixed Atmospheres is not active because its check of the game failed: " + reason + "\n\n" +
                "Another mod may be the cause: try disabling other mods and restart the game. If this notice stays, " + Report() +
                Untouched("the check passes"),
            LoadReason = "because its check of the game failed (see the notice at the main menu)",
            LoadRemedy = "Disable other mods and restart the game",
            Details = new[] { reason },
        };

        // Any other failure while starting: the mod removed its changes.
        internal static string StartFailure(string reason) =>
            "Mixed Atmospheres could not start and has removed all of its changes, so the game runs as normal: " + reason +
            "\n\nAnother mod may be the cause: try disabling other mods. If this notice stays, " + Report();

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
