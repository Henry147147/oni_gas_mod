"""Content identity controls for the 32 real-game compatibility selections."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parents[1]))
import oni_dlc as dlc


class DlcSelectionTests(unittest.TestCase):
    def test_all_32_selections_round_trip_and_explicitly_write_every_toggle(self):
        seen = set()
        for mask in range(32):
            enabled = dlc.ids_for_mask(mask)
            seen.add(tuple(enabled))
            self.assertEqual(dlc.mask_for_ids(enabled), mask)
            self.assertEqual(dlc.mask_for_ids(["", *enabled]), mask)
            text = dlc.preferences(mask)
            for identity in dlc.DLC_IDS:
                self.assertEqual(text.count(identity + ".ENABLED:"), 1)
                self.assertIn(f"{identity}.ENABLED: {int(identity in enabled)}\n", text)
        self.assertEqual(len(seen), 32)
        self.assertEqual(dlc.ids_for_mask(1), ["EXPANSION1_ID"])
        self.assertEqual(dlc.ids_for_mask(16), ["DLC5_ID"])
        self.assertEqual(dlc.selection(20)["packs"], ["Bionic", "Aquatic"])

    def test_unknown_duplicate_and_mistyped_selections_cannot_be_normalized_away(self):
        for value in (-1, 32, 1.0, True, False, None, "0"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                dlc.ids_for_mask(value)
        for identities in (None, {}, "DLC2_ID", [None], ["VANILLA_ID"], ["DLC6_ID"],
                           ["DLC2_ID", "DLC2_ID"], ["", ""]):
            with self.subTest(ids=identities), self.assertRaises(ValueError):
                dlc.mask_for_ids(identities)

    def test_existing_preferences_preserve_unrelated_settings_and_replace_disabled_flags(self):
        original = ("strings:\n  ColonyName: private\nints:\n  music: 80\n"
                    "  EXPANSION1_ID.ENABLED: 1\n  DLC2_ID.ENABLED: 1\nfloats:\n  zoom: 1.5\n")
        result = dlc.preferences(16, original)
        for retained in ("  ColonyName: private\n", "  music: 80\n", "floats:\n  zoom: 1.5\n"):
            self.assertIn(retained, result)
        self.assertIn("  EXPANSION1_ID.ENABLED: 0\n", result)
        self.assertIn("  DLC5_ID.ENABLED: 1\n", result)
        self.assertEqual(dlc.preferences(16, result), result)
        quoted = dlc.preferences(0, 'ints:\n  "DLC2_ID.ENABLED": 1\n  \'DLC3_ID.ENABLED\': 1\n')
        self.assertNotIn('"DLC2_ID.ENABLED"', quoted)
        self.assertNotIn("'DLC3_ID.ENABLED'", quoted)
        self.assertIn("  DLC2_ID.ENABLED: 0\n", quoted)
        self.assertIn("  music: 80\n  EXPANSION1_ID", dlc.preferences(0, "ints:\n  music: 80"))
        self.assertIn("ints:\n  EXPANSION1_ID", dlc.preferences(0, "strings: {}\nints: {}\nfloats: {}\n"))
        for original in ("", "ints: {music: 80}\n", "ints:\nints:\n"):
            with self.assertRaises(ValueError):
                dlc.preferences(0, original)

    def test_disposable_writer_cannot_overwrite_an_existing_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory) / "profile"
            dlc.write_preferences(profile, 31)
            path = profile / "kplayerprefs.yaml"
            before = path.read_bytes()
            with self.assertRaises(FileExistsError):
                dlc.write_preferences(profile, 0)
            self.assertEqual(path.read_bytes(), before)

    def test_preferences_and_success_booleans_cannot_substitute_for_loaded_content(self):
        report = dict(RequestedDlcMask=20, DlcSelectionChecked=True, DlcSelectionMatched=True,
                      RuntimeDlcIds=["DLC3_ID", "DLC5_ID"], ActiveDlcIds=["", "DLC5_ID", "DLC3_ID"])
        dlc.verify_report(20, report, saved=True)
        for key, value in (("RequestedDlcMask", 0), ("DlcSelectionChecked", False), ("DlcSelectionMatched", False),
                           ("RuntimeDlcIds", ["DLC3_ID"]), ("RuntimeDlcIds", ["DLC3_ID", "DLC5_ID", "DLC6_ID"]),
                           ("ActiveDlcIds", ["DLC3_ID"]), ("ActiveDlcIds", None)):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                dlc.verify_report(20, dict(report, **{key: value}), saved=True)
        for report in (None, {}, dict(RequestedDlcMask=False, DlcSelectionChecked=True,
                                     DlcSelectionMatched=True, RuntimeDlcIds=[])):
            with self.assertRaises(ValueError):
                dlc.verify_report(0, report)

    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux runner imports require fcntl")
    def test_linux_and_proton_reject_bad_masks_before_accessing_game_or_steam(self):
        from types import SimpleNamespace
        for module in ("oni_headless", "oni_proton_headless"):
            spec = importlib.util.spec_from_file_location(module, Path(__file__).parents[1] / (module + ".py"))
            runner = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(runner)
            for mask, mode in ((-1, "colony-owner"), (32, "colony-owner"), (True, "colony-owner"), (0, "menu")):
                with self.subTest(module=module, mask=mask, mode=mode), self.assertRaises(ValueError):
                    runner.playtest(SimpleNamespace(dlc_mask=mask, mode=mode), Path("/unused"), {})


if __name__ == "__main__":
    unittest.main()
