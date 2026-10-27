"""Retargeting controls on synthetic changes to the private 744825 Linux image.

Skipped where the licensed installation is absent. A copy of the game root
links everything except a modified SimDLL and element folder.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import oni_hook_profiles as profiles  # noqa: E402
import retarget_oni  # noqa: E402

GAME = Path("/mnt/data/share/oni/OxygenNotIncluded-linux")


@unittest.skipUnless((GAME / retarget_oni.RELATIVE["linux-x64"]).is_file() and shutil.which("objdump") and shutil.which("nm"),
                     "licensed Linux installation and binutils are needed")
class RetargetControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.value = json.loads(profiles.PROFILE.read_text())
        cls.data = (GAME / retarget_oni.RELATIVE["linux-x64"]).read_bytes()
        cls.extents = retarget_oni.function_extents(cls.data, "linux-x64", GAME / retarget_oni.RELATIVE["linux-x64"])
        profile = next(p for p in cls.value["profiles"] if p["platform"] == "linux-x64")
        cls.hooks = [h for h in [*profile["hooks"], *profile["publication_bindings"]]]

    def root(self, folder: Path, change=None, element=None) -> Path:
        root = folder / "game"
        plugins = root / "OxygenNotIncluded_Data/Plugins"; plugins.mkdir(parents=True)
        data = bytearray(self.data)
        if change is not None:
            data[change] ^= 0x01
        (plugins / "libSimDLL.so").write_bytes(bytes(data))
        (root / "OxygenNotIncluded_Data/Managed").symlink_to(GAME / "OxygenNotIncluded_Data/Managed")
        elements = root / "OxygenNotIncluded_Data/StreamingAssets/elements"
        shutil.copytree(GAME / "OxygenNotIncluded_Data/StreamingAssets/elements", elements)
        if element:
            (elements / element).write_text((elements / element).read_text() + "\n# changed\n")
        return root

    def hooked(self, offset: int) -> bool:
        segments = profiles.linux_load_segments(self.data)
        for hook in self.hooks:
            end = self.extents.get(hook["rva"], hook["rva"] + retarget_oni.WINDOW)
            for segment in segments:
                if segment["rva"] <= hook["rva"] < segment["rva"] + segment["file_bytes"]:
                    start = segment["file_offset"] + hook["rva"] - segment["rva"]
                    if start <= offset < start + (end - hook["rva"]):
                        return True
        return False

    def report(self, root: Path) -> dict:
        return retarget_oni.native_report("linux-x64", root, GAME, self.value, "llvm-pdbutil")

    def test_unchanged_image_is_already_verified(self):
        with tempfile.TemporaryDirectory() as folder:
            report = self.report(self.root(Path(folder)))
            self.assertTrue(report["verified_profile"]); self.assertEqual(report["needs_review"], [])

    def test_change_outside_hooked_functions_is_carried(self):
        offset = next(o for o in range(len(self.data) // 2, len(self.data), 4093) if not self.hooked(o))
        with tempfile.TemporaryDirectory() as folder:
            report = self.report(self.root(Path(folder), change=offset))
            self.assertFalse(report["verified_profile"])
            self.assertEqual(report["needs_review"], [], report["needs_review"])
            self.assertTrue(report["status"].startswith("carried"))
            candidate = report["candidate"]
            self.assertNotEqual(candidate["native_identity"], self.value["profiles"][-1]["native_identity"])
            self.assertTrue(all(h["result"] == "carried" for h in report["hooks"]))

    def test_change_inside_a_hooked_function_needs_review(self):
        profile = next(p for p in self.value["profiles"] if p["platform"] == "linux-x64")
        hook = next(h for h in profile["hooks"] if h["role"] == "temperature")
        segment = next(s for s in profiles.linux_load_segments(self.data) if s["rva"] <= hook["rva"] < s["rva"] + s["file_bytes"])
        # Inside the body, past the 32 fingerprinted entry bytes.
        offset = segment["file_offset"] + hook["rva"] - segment["rva"] + 64
        with tempfile.TemporaryDirectory() as folder:
            report = self.report(self.root(Path(folder), change=offset))
            self.assertTrue(any("temperature" in item for item in report["needs_review"]), report["needs_review"])
            self.assertEqual(report["status"], "needs review")

    def test_changed_element_definitions_are_reported(self):
        with tempfile.TemporaryDirectory() as folder:
            elements = retarget_oni.element_report(self.root(Path(folder), element="gas.yaml"), GAME)
            self.assertEqual(elements["changed"], ["elements/gas.yaml"])
            self.assertIn("save migration", elements["note"])


if __name__ == "__main__":
    unittest.main()
