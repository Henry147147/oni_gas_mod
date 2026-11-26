using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using MixedAtmospheres.Oni;

// Refusal decisions for platforms and gameplay packs that are not installed
// here (docs/game-update-resilience.md#refusals). No game files are needed.
static class Program
{
    private static int checks;
    private static void Check(bool ok, string why) { ++checks; if (!ok) throw new Exception(why); }

    private static void Platforms()
    {
        Check(ModAdmission.PlatformRefusal(true, false, Architecture.X64, 8, "Windows") == null, "Windows x64 runs");
        Check(ModAdmission.PlatformRefusal(false, true, Architecture.X64, 8, "Linux") == null, "Linux x64 runs");
        var mac = ModAdmission.PlatformRefusal(false, false, Architecture.Arm64, 8, "Darwin 24.1.0");
        Check(mac != null && mac.Contains("64-bit Windows and 64-bit Linux") && mac.Contains("Darwin") && mac.Contains("arm64"), "macOS refused by name: " + mac);
        Check(ModAdmission.PlatformRefusal(false, false, Architecture.X64, 8, "Darwin 23.0") != null, "Intel macOS refused");
        Check(ModAdmission.PlatformRefusal(true, false, Architecture.X86, 4, "Windows")?.Contains("Windows (x86)") == true, "32-bit Windows refused");
        Check(ModAdmission.PlatformRefusal(false, true, Architecture.Arm64, 8, "Linux") != null, "ARM Linux refused");
        Check(ModAdmission.PlatformRefusal(true, false, Architecture.X64, 4, "Windows") != null, "32-bit pointers refused");
        Check(ModAdmission.PlatformRefusal(false, false, Architecture.X64, 8, " ")!.Contains("this operating system"), "unnamed OS described");
    }

    private static void Packs()
    {
        static bool Cosmetic(string id) => id == "COSMETIC1_ID";
        Check(ModAdmission.UnsupportedPacks(null, Cosmetic).Length == 0, "no packs");
        Check(ModAdmission.UnsupportedPacks(new[] { "" }, Cosmetic).Length == 0, "base game");
        Check(ModAdmission.UnsupportedPacks(new[] { "EXPANSION1_ID" }, Cosmetic).Length == 0, "Spaced Out!");
        Check(ModAdmission.UnsupportedPacks(new[] { "EXPANSION1_ID", "COSMETIC1_ID" }, Cosmetic).Length == 0, "cosmetic packs allowed");
        foreach (var pack in new[] { "DLC2_ID", "DLC3_ID", "DLC4_ID", "DLC5_ID" })
        {
            var refused = ModAdmission.UnsupportedPacks(new[] { "EXPANSION1_ID", pack }, Cosmetic);
            Check(refused.SequenceEqual(new[] { pack }), pack + " refused with Spaced Out!");
            Check(ModAdmission.UnsupportedPacks(new[] { pack }, Cosmetic).SequenceEqual(new[] { pack }), pack + " refused alone");
            Check(ModAdmission.PackName(pack).StartsWith("The ", StringComparison.Ordinal) && ModAdmission.PackName(pack).EndsWith(" Pack", StringComparison.Ordinal), "named: " + pack);
        }
        Check(ModAdmission.UnsupportedPacks(new[] { "DLC6_ID" }, Cosmetic).SequenceEqual(new[] { "DLC6_ID" }), "unknown future pack refused");
        var all = ModAdmission.UnsupportedPacks(new[] { "DLC5_ID", "EXPANSION1_ID", "DLC2_ID", "DLC3_ID", "DLC2_ID", "DLC4_ID", "COSMETIC1_ID" }, Cosmetic);
        Check(all.SequenceEqual(new[] { "DLC2_ID", "DLC3_ID", "DLC4_ID", "DLC5_ID" }), "stable, distinct order");
        var one = ModAdmission.PacksMenuNotice(new[] { "DLC2_ID" });
        Check(one.Contains("The Frosty Planet Pack is active") && one.Contains("turn that pack off") && one.Contains("base game and Spaced Out! only"), one);
        var many = ModAdmission.PacksMenuNotice(all);
        Check(many.Contains("The Frosty Planet Pack, The Bionic Booster Pack, The Prehistoric Planet Pack and The Aquatic Planet Pack are active") &&
              many.Contains("turn those packs off"), many);
        var colony = ModAdmission.PacksColonyNotice(new[] { "DLC3_ID" });
        Check(colony.Contains("keeps ONI's own gases and liquids") && colony.Contains("The Bionic Booster Pack is active"), colony);
        var load = ModAdmission.PacksLoadRefusal(new[] { "DLC4_ID", "DLC5_ID" });
        Check(load.Contains("The Prehistoric Planet Pack and The Aquatic Planet Pack are active") && load.Contains("has not been changed"), load);
        Check(ModAdmission.PackName("DLC9_ID") == "DLC9_ID", "unknown pack keeps its ID");
    }

    private static void Duplicates()
    {
        var keepWorkshop = ModAdmission.DuplicateNotice("/m/Dev/MixedAtmospheres.IntegrationProbe", "Dev", "/m/Steam/123", "Steam");
        Check(keepWorkshop.Contains("delete or disable the copy in\n  /m/Dev/MixedAtmospheres.IntegrationProbe\nand keep\n  /m/Steam/123"), keepWorkshop);
        var keepRunning = ModAdmission.DuplicateNotice("/m/Steam/123", "Steam", "/m/Dev/MixedAtmospheres.IntegrationProbe", "Dev");
        Check(keepRunning.Contains("delete or disable the copy in\n  /m/Dev/MixedAtmospheres.IntegrationProbe\nand keep\n  /m/Steam/123"), keepRunning);
        var two = ModAdmission.DuplicateNotice("/m/Dev/A", "Dev", "/m/Local/B", "Local");
        Check(two.Contains("copy in\n  /m/Local/B\nand keep\n  /m/Dev/A") && two.Contains("(Dev, running)") && two.Contains("(Local, not loaded)"), two);
    }

    // Why the mod stays off before patching anything: one text per cause
    // (docs/game-update-resilience.md#refusals). Only a build the mod has no
    // adapter for may say that an update is needed.
    private static void GameRefusals()
    {
        const string Updates = "Mixed Atmospheres update";
        var newer = ModAdmission.UnsupportedBuild("1.13.9", 770001, 764025);
        Check(newer.Notice.Contains("Mixed Atmospheres 1.13.9 does not support this version of Oxygen Not Included") && newer.Notice.Contains("the game is build 770001") &&
            newer.Notice.Contains("for build 764025") && newer.Notice.Contains(Updates) && newer.Notice.Contains("has not changed the game"), newer.Notice);
        Check(newer.LoadRefusal.Contains("(build 770001)") && newer.LoadRefusal.Contains("Install a Mixed Atmospheres update") &&
            newer.LoadRefusal.Contains("The save has not been changed."), newer.LoadRefusal);
        var older = ModAdmission.UnsupportedBuild("1.13.9", 700000, 744825);
        Check(older.Notice.Contains("Update Oxygen Not Included") && !older.Notice.Contains(Updates) && older.LoadRefusal.Contains("Update Oxygen Not Included, then load"), older.Notice);
        Check(ModAdmission.UnsupportedBuild("1.13.9", 0, 764025).Notice.Contains("an unknown build"), "unreadable build number");

        // Another mod's patch alone is no refusal (docs/game-update-resilience.md#other-mods): Player.log
        // counts the patched methods after the verified line and lists one per line.
        Check(ModAdmission.OtherModPatchesLog(Array.Empty<string>()) == "", "no other mods, nothing added");
        var one = ModAdmission.OtherModPatchesLog(new[] { "SaveLoader.Save <- Fast Track (Harmony: FastTrack)" });
        Check(one == " One game method the mod uses carries another mod's Harmony patch; their code is verified and the mod runs beside the patches:\n" +
            "  SaveLoader.Save <- Fast Track (Harmony: FastTrack)", one);
        var many = ModAdmission.OtherModPatchesLog(new[] { "Db.Initialize <- Rephysicalized (Harmony: Rephysicalized)", "MainMenu.OnSpawn <- A, B (Harmony: a, b)" });
        Check(many.StartsWith(" 2 game methods the mod uses carry other mods' Harmony patches;", StringComparison.Ordinal) &&
            many.EndsWith(":\n  Db.Initialize <- Rephysicalized (Harmony: Rephysicalized)\n  MainMenu.OnSpawn <- A, B (Harmony: a, b)", StringComparison.Ordinal), many);

        var files = ModAdmission.GameFiles(764025, new[]
        {
            "changed static SaveLoader::LoadHeader(System.String,SaveGame/Header&)SaveGame/GameInfo", "missing property SaveGame/Header::buildVersion:System.UInt32[]",
            "changed patch target Game::Save(System.IO.BinaryWriter)System.Void", "changed named KCompactedVector<!0>::GetData", "patched by another mod: named SaveLoader::Save",
        });
        Check(files.Notice.Contains("reports a version the mod supports (build 764025), but 5 parts of its code differ") &&
            files.Notice.Contains("  - SaveLoader.LoadHeader\n  - SaveGame.Header.buildVersion (missing)\n  - Game.Save\n  - and 2 more (listed in Player.log)") &&
            files.Notice.Contains("The game is not out of date") && files.Notice.Contains("Verify integrity of game files") && files.Notice.Contains("disabling other mods") &&
            files.Notice.Contains("github.com/Henry147147/oni_gas_mod/issues") && !files.Notice.Contains(Updates), files.Notice);
        Check(files.Details.Count == 5 && files.Details[0].Contains("SaveGame/Header&"), "the log keeps the full form");
        var native = ModAdmission.GameFiles(764025, new[] { "the game's native simulator (SimDLL): unsupported image" });
        Check(native.Notice.Contains("one part of its code differs") && native.Notice.Contains("  - the game's native simulator (SimDLL): unsupported image"), native.Notice);
        Check(ModAdmission.ShortPart("changed named KCompactedVector<!0>::GetData") == "KCompactedVector<!0>.GetData" &&
            ModAdmission.ShortPart("new patch target static Sim::Step()System.Void") == "Sim.Step (new)" && ModAdmission.ShortPart("changed named Game::*") == "Game.*" &&
            ModAdmission.ShortPart("this is no member") == "this is no member", "short names");

        var installation = ModAdmission.Installation("the game contract is missing");
        Check(installation.Notice.Contains("its own files are incomplete or come from two versions (the game contract is missing)") && installation.Notice.Contains("The game is not out of date") &&
            installation.Notice.Contains("Unsubscribe from the mod and subscribe again") && installation.LoadRefusal.Contains("Reinstall Mixed Atmospheres, then load"), installation.Notice);
        var failed = ModAdmission.CheckFailed("boom");
        Check(failed.Notice.Contains("its check of the game failed: boom") && failed.Notice.Contains("try disabling other mods") && failed.Notice.Contains("Player.log") &&
            !failed.Notice.Contains(Updates), failed.Notice);
        var start = ModAdmission.StartFailure("boom");
        Check(start.Contains("could not start and has removed all of its changes, so the game runs as normal: boom") && start.Contains("try disabling other mods") &&
            start.Contains("github.com/Henry147147/oni_gas_mod/issues"), start);
        foreach (var refusal in new[] { newer, older, files, native, installation, failed })
            Check(refusal.Kind.Length != 0 && refusal.Details.Count != 0 && refusal.Notice.Contains("can be loaded again once") &&
                refusal.LoadRefusal.StartsWith("This colony uses Mixed Atmospheres, which is not active ", StringComparison.Ordinal), "complete refusal: " + refusal.Kind);
    }

    private static bool Refused(Action parse, string why)
    {
        try { parse(); } catch (NotSupportedException error) { Check(error.Message.Contains(why), "refusal names its reason: " + error.Message); return true; }
        return false;
    }

    // A minimal SIMSAVE v15 native section (3x3 cells) for synthetic envelopes.
    private static byte[] NativeSection()
    {
        var native = new byte[29 + 36 * 9];
        System.Text.Encoding.ASCII.GetBytes("SIMSAVE\0").CopyTo(native, 0);
        BitConverter.GetBytes(15).CopyTo(native, 8); BitConverter.GetBytes(3).CopyTo(native, 12); BitConverter.GetBytes(3).CopyTo(native, 16);
        return native;
    }

    private static void SyntheticEnvelopes()
    {
        var catalog = Enumerable.Repeat((byte)7, 32).ToArray();
        byte[] Saved(int build) => ColonySaveEnvelope.Create(build, 3, 42, catalog, NativeSection(), new byte[] { 1, 2, 3 }).CopyEncodedData();
        var first = ColonySaveEnvelope.FirstSupportedBuild;
        // Saved at 744825, loaded by 744825 and by a later verified build.
        Check(ColonySaveEnvelope.Parse(Saved(first), first, first, 3, catalog).OniBuild == first, "same build loads");
        Check(ColonySaveEnvelope.Parse(Saved(first), first, first + 1, 3, catalog).OniBuild == first, "a later verified build loads a 744825 save");
        Check(ColonySaveEnvelope.Parse(Saved(first + 1), first, first + 1, 3, catalog).OniBuild == first + 1, "a later build loads its own save");
        Check(Refused(() => ColonySaveEnvelope.Parse(Saved(first + 2), first, first + 1, 3, catalog), "newer game build (744827)"), "a newer save is refused");
        Check(Refused(() => ColonySaveEnvelope.Parse(Saved(first - 1), first, first + 1, 3, catalog), "744824"), "an earlier unsupported save is refused");
        Check(Refused(() => ColonySaveEnvelope.Parse(Saved(first), first, first + 1, 3, Enumerable.Repeat((byte)8, 32).ToArray()), "save migration"), "a changed catalogue is refused");
        // Model 4 (0.1.4, molecular oxygen, hydrogen and chlorine) refuses earlier colonies and says what to do.
        Check(Refused(() => ColonySaveEnvelope.Parse(Saved(first), first, first + 1, 4, catalog), "earlier Mixed Atmospheres material model"), "an earlier material model is refused");
        Check(Refused(() => ColonySaveEnvelope.Parse(Saved(first), first, first + 1, 4, catalog), "Start a new colony"), "the refusal says to start a new colony");
        Check(Refused(() => ColonySaveEnvelope.Parse(Saved(first), first, first + 1, 2, catalog), "newer Mixed Atmospheres material model"), "a newer material model is refused");
        // The single-build form keeps its exact meaning for development paths.
        Check(Refused(() => ColonySaveEnvelope.Parse(Saved(first), first + 1, 3, catalog), "game build 744825"), "exact-build parsing is unchanged");
    }

    // Real envelopes from private colony saves (skipped where absent): every prepared
    // base-game save in local/saves and a bfb1168 Spaced Out! manual save. The envelope
    // (framing, build, catalogue) is independent of whether the content is still accepted.
    private static int RealEnvelopes()
    {
        var root = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../.."));
        var saves = (Environment.GetEnvironmentVariable("MA_TEST_MIXTURE_SAVES") ?? "").Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries).ToList();
        if (saves.Count == 0)
        {
            if (Directory.Exists(Path.Combine(root, "local/saves"))) saves.AddRange(Directory.GetFiles(Path.Combine(root, "local/saves"), "*.sav").OrderBy(p => p));
            saves.Add(Path.Combine(root, "../../archive/oni-agents/claude-team/spacedout-ordinary-bfb1168/artifacts/spacedout-ordinary-classic-manual-save-cold/input-colony.sav"));
        }
        var checkedSaves = 0;
        foreach (var path in saves.Where(File.Exists))
        {
            var sim = ColonySaveReadback.SimSection(File.ReadAllBytes(path));
            Check(sim != null && ColonySaveEnvelope.HasMagic(sim), "real save holds a Mixed Atmospheres envelope: " + path);
            var build = BitConverter.ToInt32(sim!, 12); var model = BitConverter.ToInt32(sim!, 16);
            var catalog = sim!.AsSpan(28, 32).ToArray();
            // The envelope records the build that wrote it: 744825 in the prepared saves, a later supported build in a player's.
            Check(build >= ColonySaveEnvelope.FirstSupportedBuild, "a real save records a supported build: " + build);
            Check(ColonySaveEnvelope.Parse(sim, build, model, catalog).EncodedLength == sim!.Length, "the real envelope parses unchanged");
            var later = ColonySaveEnvelope.Parse(sim, ColonySaveEnvelope.FirstSupportedBuild, build + 1, model, catalog);
            Check(later.OniBuild == build && later.CopyEncodedData().AsSpan().SequenceEqual(sim!), "a later verified build accepts the real envelope byte for byte");
            var other = (byte[])catalog.Clone(); other[0] ^= 1;
            Check(Refused(() => ColonySaveEnvelope.Parse(sim, ColonySaveEnvelope.FirstSupportedBuild, build + 1, model, other), "save migration"), "a changed catalogue refuses the real envelope");
            Check(Refused(() => ColonySaveEnvelope.Parse(sim, ColonySaveEnvelope.FirstSupportedBuild, build - 1, model, catalog), "newer game build"), "an older running build refuses it");
            ++checkedSaves;
        }
        return checkedSaves;
    }

    // The content decision the game makes at load (ColonyOwnerPersistence.ValidateCurrentContent), engine-free.
    private static string? Refusal(byte[] sim)
    {
        try
        {
            var envelope = ColonySaveEnvelope.Parse(sim, ColonySaveEnvelope.FirstSupportedBuild, BitConverter.ToInt32(sim, 12), BitConverter.ToInt32(sim, 16), sim.AsSpan(28, 32).ToArray());
            var owner = ColonySavePayload.Decode(envelope.CustomData).Owner;
            ColonyCheckpointMetadata.RefuseRemovedNitrogen(owner); ColonyCheckpointMetadata.RefuseUnboundedPressure(owner); ColonyCheckpointMetadata.RefuseExperimentalActivity(owner);
            return null;
        }
        catch (Exception error) { return error.Message; }
    }

    // Prepared base-game saves in local/saves (skipped where absent). Those made before the nitrogen
    // removal (2026-10-27, docs/completion-plan.md amendments) have a valid envelope but their content is
    // refused with an explanation. The profile-10 and profile-11 frozen saves (base-defaults-p10-v1 and -p11-v1) hold accepted
    // content under material model 3, which this build (model 4, molecular masses, 0.1.4) refuses at the envelope with what to
    // do; the model-4 frozen save base-defaults-p12-v1 must be accepted by both.
    private static int PreparedContent()
    {
        var root = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../.."));
        var directory = Path.Combine(root, "local/saves");
        if (!Directory.Exists(directory)) return 0;
        var count = 0;
        foreach (var path in Directory.GetFiles(directory, "*.sav").OrderBy(p => p))
        {
            var name = Path.GetFileNameWithoutExtension(path);
            var sim = ColonySaveReadback.SimSection(File.ReadAllBytes(path));
            if (sim == null || !ColonySaveEnvelope.HasMagic(sim)) continue;
            var refusal = Refusal(sim);
            var model = BitConverter.ToInt32(sim, 16);
            string? Running() { try { ColonySaveEnvelope.Parse(sim, ColonySaveEnvelope.FirstSupportedBuild, BitConverter.ToInt32(sim, 12), ColonySaveEnvelope.CurrentMaterialModel, sim.AsSpan(28, 32).ToArray()); return null; } catch (Exception error) { return error.Message; } }
            if (name == "base-defaults-p10-v1" || name == "base-defaults-p11-v1")
            {
                Check(refusal == null && model == 3, "an earlier frozen save holds accepted content under model 3: " + refusal);
                var running = Running();
                Check(running != null && running.Contains("earlier Mixed Atmospheres material model") && running.Contains("Start a new colony"),
                    "this build refuses a model-3 frozen save with what to do: " + running);
            }
            else if (name == "base-defaults-p12-v1")
                Check(refusal == null && model == ColonySaveEnvelope.CurrentMaterialModel && Running() == null, "the current frozen save is accepted: " + refusal);
            else if (name.StartsWith("base-core21-") || name.StartsWith("base-defaults-p5-") || name.StartsWith("base-floor-") ||
                     name.StartsWith("base-package-") || name.StartsWith("base-trace-"))
                Check(refusal != null && refusal.Contains("removed nitrogen content") && refusal.Contains("Start a new colony"),
                    "a pre-nitrogen-removal save is refused with an explanation: " + name);
            else continue;
            ++count;
        }
        return count;
    }

    private static int Main()
    {
        Platforms(); Packs(); Duplicates(); GameRefusals(); SyntheticEnvelopes();
        var real = RealEnvelopes();
        var content = PreparedContent();
        Console.WriteLine(content == 0 ? "Prepared base saves: none available, skipped." : $"Prepared base saves: {content} content decisions pinned.");
        Console.WriteLine(real == 0 ? "Real mixture saves: none available, skipped." : $"Real mixture saves: {real} envelopes accepted across builds.");
        Console.WriteLine($"Distribution refusal controls passed ({checks} checks).");
        return 0;
    }
}
