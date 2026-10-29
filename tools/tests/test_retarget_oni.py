"""Synthetic target controls and mutations of the private 744825 Linux image.

Only installed-image controls need the licensed installation. A copy of the
game root links everything except a modified SimDLL and element folder.
"""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import oni_hook_profiles as profiles  # noqa: E402
import retarget_oni  # noqa: E402

GAME = Path(os.environ.get("ONI_GAME_DIR_LINUX") or os.environ.get("ONI_GAME_DIR") or
            "/home/henry/.local/share/oni-headless/steam-home/.local/share/Steam/steamapps/common/OxygenNotIncluded")


class TargetIdentityControls(unittest.TestCase):
    """Small disassembly fixtures need neither licensed files nor binutils."""

    def body(self, instruction: str, *, start: int = 0x1000) -> list[str]:
        with patch.object(retarget_oni.subprocess, "check_output", return_value=instruction):
            return retarget_oni.normalized_body(Path("fixture.so"), b"", "linux-x64", start, {start: start + 0x20})

    def test_external_call_and_jump_destinations_are_not_erased(self):
        for opcode in ("call", "jmp", "jne"):
            with self.subTest(opcode=opcode):
                old = self.body(f"1000: {opcode} 2000 <first>\n")
                new = self.body(f"1000: {opcode} 3000 <second>\n")
                self.assertNotEqual(old, new)

    def test_external_same_symbol_at_different_address_needs_review(self):
        self.assertNotEqual(self.body("1000: call 2000 <helper>\n"),
                            self.body("1000: call 3000 <helper>\n"))

    def test_external_different_symbol_at_same_address_needs_review(self):
        self.assertNotEqual(self.body("1000: call 2000 <first>\n"),
                            self.body("1000: call 2000 <second>\n"))

    def test_rip_data_and_indirect_call_destinations_are_not_erased(self):
        for opcode in ("mov rax,QWORD PTR", "call QWORD PTR", "lea rax,"):
            with self.subTest(opcode=opcode):
                old = self.body(f"1000: {opcode} [rip+0xff9] # 2000 <first>\n")
                new = self.body(f"1000: {opcode} [rip+0x1ff9] # 3000 <second>\n")
                self.assertNotEqual(old, new)

    def test_unresolved_external_and_rip_targets_refuse_comparison(self):
        for instruction in ("call 2000", "jmp 2000", "call rax", "jmp QWORD PTR [rax]",
                            "mov rax,QWORD PTR [rip+0xff9]", "mov rax,QWORD PTR [rip+0xff9] # 2000"):
            with self.subTest(instruction=instruction):
                with self.assertRaisesRegex(ValueError, "[Uu]nresolved"):
                    self.body(f"1000: {instruction}\n")

    def test_internal_branches_keep_relative_identity(self):
        self.assertEqual(self.body("1000: jne 1010 <function+0x10>\n1002: ret\n"),
                         self.body("3000: jne 3010 <function+0x10>\n3002: ret\n", start=0x3000))
        self.assertNotEqual(self.body("1000: jne 1010 <function+0x10>\n"),
                            self.body("1000: jne 1012 <function+0x12>\n"))

    def test_unknown_extent_cannot_classify_external_branch_as_internal(self):
        with patch.object(retarget_oni.subprocess, "check_output", return_value="1000: call 1100 <other>\n"):
            with self.assertRaisesRegex(ValueError, "[Uu]nresolved function extent"):
                retarget_oni.normalized_body(Path("fixture.so"), b"", "linux-x64", 0x1000, {})

    def test_windows_image_base_is_not_destination_identity(self):
        def body(base):
            data = bytearray(128)
            struct.pack_into("<I", data, 60, 64)
            struct.pack_into("<Q", data, 112, base)
            output = f"{base + 0x1000:x}: call {base + 0x2000:x} <helper>\n" + \
                     f"{base + 0x1005:x}: mov rax,QWORD PTR [rip+0xff4] # {base + 0x2000:x} <global>\n"
            with patch.object(retarget_oni.subprocess, "check_output", return_value=output):
                return retarget_oni.normalized_body(Path("fixture.dll"), data, "windows-x64", 0x1000, {0x1000: 0x1020})
        self.assertEqual(body(0x180000000), body(0x190000000))

    def report(self, old_body, new_body):
        with tempfile.TemporaryDirectory() as folder:
            new_root, old_root = Path(folder) / "new", Path(folder) / "old"
            for root, data in ((old_root, b"a" * 128), (new_root, b"a" * 127 + b"b")):
                image = root / retarget_oni.RELATIVE["linux-x64"]
                image.parent.mkdir(parents=True)
                image.write_bytes(data)
            value = {"profiles": [{"platform": "linux-x64", "native_identity": retarget_oni.sha(b"a" * 128),
                      "hooks": [{"symbol": "fixture", "role": "fixture", "rva": 0x100}], "globals": []}]}
            with patch.object(profiles, "normalized_identity", side_effect=retarget_oni.sha), \
                 patch.object(profiles, "linux_symbols", return_value=({"fixture": 0x100}, [(0x100, 0, 128, 0x20000000)], {})), \
                 patch.object(profiles, "linux_trampoline_storage", return_value={}), \
                 patch.object(retarget_oni, "function_extents", return_value={0x100: 0x110}), \
                 patch.object(retarget_oni.subprocess, "check_output", side_effect=[old_body, new_body]):
                return retarget_oni.native_report("linux-x64", new_root, old_root, value, "unused")

    def test_comparison_refusal_becomes_reported_review(self):
        report = self.report("100: call 200\n", "100: call 200\n")
        self.assertEqual(report["status"], "needs review")
        self.assertFalse(report["hooks"][0]["equivalent_body"])
        self.assertTrue(any("nresolved" in reason for reason in report["needs_review"]), report)

    def test_changed_external_or_data_target_cannot_carry_profile(self):
        for old, new in (("call 2000 <first>", "call 3000 <second>"),
                         ("mov rax,[rip+0x1ef9] # 2000 <first>", "mov rax,[rip+0x2ef9] # 3000 <second>")):
            with self.subTest(old=old):
                report = self.report(f"100: {old}\n", f"100: {new}\n")
                self.assertEqual(report["status"], "needs review")
                self.assertFalse(report["hooks"][0]["equivalent_body"])
                self.assertIn("body changed", report["needs_review"][0])

    def test_resolved_unchanged_body_can_still_carry(self):
        report = self.report("100: call 2000 <helper>\n", "100: call 2000 <helper>\n")
        self.assertEqual(report["status"], "carried: every hooked function is equivalent")
        self.assertEqual(report["needs_review"], [])


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
            # Avoid mutating even a conservative prefix of a private procedure
            # lacking symbol extent data. Production comparison refuses it.
            end = self.extents.get(hook["rva"], hook["rva"] + 512)
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

    def test_change_outside_hooked_functions_cannot_hide_unresolved_calls(self):
        offset = next(o for o in range(len(self.data) // 2, len(self.data), 4093) if not self.hooked(o))
        with tempfile.TemporaryDirectory() as folder:
            report = self.report(self.root(Path(folder), change=offset))
            self.assertFalse(report["verified_profile"])
            self.assertTrue(any("Unresolved" in reason for reason in report["needs_review"]), report)
            self.assertEqual(report["status"], "needs review")
            candidate = report["candidate"]
            self.assertNotEqual(candidate["native_identity"], self.value["profiles"][-1]["native_identity"])
            self.assertTrue(any(not h["equivalent_body"] for h in report["hooks"]))

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
