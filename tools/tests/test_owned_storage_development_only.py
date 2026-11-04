"""The exact owned-storage writer (schemas 5 to 7) is development-only.

User decision 2026-11-04 (docs/completion-plan.md amendments): storage beyond
the reservoirs stays ONI's own, with every crossing booked in the owner's
ledgers, and the exact owned-storage writer is no longer a release requirement.
It stays off by default and nothing a player receives may switch it on. These
checks read the sources; they need neither the game nor a build.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
MANAGED = ROOT / "managed/MixedAtmospheres.Oni"
VARIABLE = "MA_COLONY_NATIVE_SCHEDULE_CONTROL"
# What builds a player's package, installs it or configures a player's game.
RELEASE_FILES = ("tools/package_release.py", "tools/package_windows.py", "tools/install_windows.ps1",
                 "managed/MixedAtmospheres.Oni/Mod.cs", "managed/MixedAtmospheres.Oni/ModSettings.cs",
                 "managed/MixedAtmospheres.Oni/ModSettingsStore.cs", "managed/MixedAtmospheres.Oni/ModSettingsRuntime.cs")


def text(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class OwnedStorageDevelopmentOnlyTests(unittest.TestCase):
    def test_no_release_file_names_the_switch(self):
        for relative in RELEASE_FILES:
            self.assertNotIn(VARIABLE, text(relative), relative)

    def test_only_the_environment_switch_or_a_saved_schema_5_image_starts_the_writer(self):
        reads = [path.name for path in sorted(MANAGED.glob("*.cs")) if VARIABLE in path.read_text(encoding="utf-8")]
        self.assertEqual(reads, ["ColonyConduitsRuntime.cs"])
        runtime = text("managed/MixedAtmospheres.Oni/ColonyConduitsRuntime.cs")
        self.assertEqual(runtime.count(f'GetEnvironmentVariable("{VARIABLE}")=="1"'), 1)
        self.assertEqual(len(re.findall(r"StartNativeScheduleControl\(\)", runtime)), 2)  # definition and the one call
        self.assertIn("NativeScheduleControl=image.SchemaVersion>=5;", runtime)
        starters = {path.name for path in MANAGED.glob("*.cs")
                    if not path.name.startswith("ColonyScenarios") and "ColonyOwnedStorage.StartControl()" in path.read_text(encoding="utf-8")}
        self.assertEqual(starters, {"ColonyConduitsRuntime.cs"})

    def test_the_writer_is_off_until_started_and_schema_5_to_7_need_the_storage_image(self):
        storage = text("managed/MixedAtmospheres.Oni/ColonyOwnedStorage.cs")
        self.assertIn("internal static bool Enabled {get;private set;}", storage)
        self.assertEqual(len(re.findall(r"\bEnabled=true", storage)), 1)
        runtime = text("managed/MixedAtmospheres.Oni/ColonyConduitsRuntime.cs")
        self.assertRegex(runtime, r"if\(NativeScheduleControl\)\s*\{runtime\.OwnedStorage=ColonyOwnedStorage\.Capture\(\)")
        checkpoint = text("managed/MixedAtmospheres.Oni/ColonyConduitCheckpoint.cs")
        self.assertIn("Runtime.OwnedStorage!=null?(Runtime.OwnedStorage.Retired.Length!=0?7:", checkpoint)


if __name__ == "__main__":
    unittest.main()
