using System;
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

    private static int Main()
    {
        Platforms(); Packs(); Duplicates();
        Console.WriteLine($"Distribution refusal controls passed ({checks} checks).");
        return 0;
    }
}
