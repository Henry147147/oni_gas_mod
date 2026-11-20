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

    def report(self, old_body, new_body, accept_indirect=False):
        with tempfile.TemporaryDirectory() as folder:
            new_root, old_root = Path(folder) / "new", Path(folder) / "old"
            for root, data in ((old_root, b"a" * 128), (new_root, b"a" * 127 + b"b")):
                image = profiles.sim_image(root, "linux-x64")
                image.parent.mkdir(parents=True)
                image.write_bytes(data)
            value = {"profiles": [{"platform": "linux-x64", "native_identity": retarget_oni.sha(b"a" * 128),
                      "hooks": [{"symbol": "fixture", "role": "fixture", "rva": 0x100}], "globals": []}]}
            with patch.object(profiles, "normalized_identity", side_effect=retarget_oni.sha), \
                 patch.object(profiles, "linux_symbols", return_value=({"fixture": 0x100}, [(0x100, 0, 128, 0x20000000)], {})), \
                 patch.object(profiles, "linux_trampoline_storage", return_value={}), \
                 patch.object(retarget_oni, "function_extents", return_value={0x100: 0x110}), \
                 patch.object(retarget_oni.subprocess, "check_output", side_effect=[old_body, new_body] * 2):
                return retarget_oni.native_report("linux-x64", new_root, old_root, value, "unused", accept_indirect)

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

    def test_indirect_branches_are_equated_only_on_explicit_acceptance(self):
        body = "100: mov rax,QWORD PTR [rcx]\n104: call QWORD PTR [rax+0x10]\n107: call 2000 <helper>\n"
        report = self.report(body, body)
        self.assertEqual(report["status"], "needs review")
        self.assertIn("Unresolved indirect branch", report["needs_review"][0])
        self.assertIn("otherwise equivalent, with 1 identical indirect", report["needs_review"][0])
        self.assertEqual(report["accepted_indirect"], [])
        accepted = self.report(body, body, accept_indirect=True)
        self.assertEqual(accepted["status"], "carried: every hooked function is equivalent")
        self.assertEqual((accepted["accepted_indirect"], accepted["hooks"][0]["indirect_branches"]), (["fixture"], 1))
        # A different operand, or any other difference, is never accepted.
        for new in (body.replace("[rax+0x10]", "[rax+0x18]"), body.replace("rcx]", "rdx]"),
                    body.replace("2000 <helper>", "2000 <other>")):
            with self.subTest(new=new):
                refused = self.report(body, new, accept_indirect=True)
                self.assertEqual(refused["status"], "needs review")
                self.assertFalse(refused["hooks"][0]["equivalent_body"])
                self.assertEqual(refused["accepted_indirect"], [])

    def test_unregistered_private_location_needs_review_even_when_equivalent(self):
        with patch.dict(profiles.CHUNK_HEAT_LOCATIONS, {"linux-x64": {}}):
            with tempfile.TemporaryDirectory() as folder:
                new_root, old_root = Path(folder) / "new", Path(folder) / "old"
                for root, data in ((old_root, b"a" * 128), (new_root, b"a" * 127 + b"b")):
                    image = profiles.sim_image(root, "linux-x64"); image.parent.mkdir(parents=True); image.write_bytes(data)
                value = {"profiles": [{"platform": "linux-x64", "native_identity": retarget_oni.sha(b"a" * 128), "globals": [],
                          "hooks": [{"symbol": "<private>", "role": "chunk-heat", "rva": 0x100}]}]}
                with patch.object(profiles, "normalized_identity", side_effect=retarget_oni.sha), \
                     patch.object(profiles, "linux_symbols", return_value=({}, [(0x100, 0, 128, 0x20000000)], {})), \
                     patch.object(profiles, "linux_trampoline_storage", return_value={}), \
                     patch.object(retarget_oni, "function_extents", return_value={0x100: 0x110}), \
                     patch.object(retarget_oni.subprocess, "check_output", side_effect=["100: ret\n"] * 2):
                    report = retarget_oni.native_report("linux-x64", new_root, old_root, value, "unused")
        self.assertTrue(report["hooks"][0]["equivalent_body"])
        self.assertEqual(report["status"], "needs review")
        self.assertIn("not in CHUNK_HEAT_LOCATIONS", report["needs_review"][0])


class WindowsPdbSymbolControls(unittest.TestCase):
    """Windows PE images have no COFF symbols, and objdump (binutils 2.46)
    prints bare addresses for their targets; the matching PDB names them."""

    BASE = 0x180000000
    SECTIONS = [(0x1000, 0x5000), (0x6000, 0x8000), (0x9000, 0xA000)]

    def symbolizer(self, **renamed):
        publics = {"?Helper@@YAXXZ": 0x2000, "?Folded@@YAXXZ": 0x2100, "?AFolded@@YAXXZ": 0x2100,
                   "__imp_QueryPerformanceCounter": 0x6000, "?gTable@@3PAHA": 0x6100}
        publics = {renamed.get(name, name): rva for name, rva in publics.items()}
        records = [("S_GPROC32", "Helper", 0x2000, 0x40), ("S_LPROC32", "PrivateWork", 0x3000, 0x80),
                   ("S_LDATA32", "kStaticConstants", 0x6200, 0)]
        return retarget_oni.PdbSymbolizer(publics, records, self.SECTIONS)

    def test_symbolizer_names_exact_inner_and_preceding_locations(self):
        names = self.symbolizer()
        self.assertEqual(names(0x2000), "?Helper@@YAXXZ")          # the public before the procedure
        self.assertEqual(names(0x2100), "?AFolded@@YAXXZ")         # folded COMDATs: the smallest name
        self.assertEqual(names(0x3000), "PrivateWork")            # a static procedure: module record only
        self.assertEqual(names(0x3010), "PrivateWork+0x10")
        self.assertEqual(names(0x6000), "__imp_QueryPerformanceCounter")
        self.assertEqual(names(0x6208), "kStaticConstants+0x8")
        self.assertIsNone(names(0x1500))                          # nothing named before it in .text
        self.assertIsNone(names(0x9010))                          # nothing named in that section
        self.assertIsNone(names(0x5800))                          # outside every section

    def body(self, output, symbolizer):
        data = bytearray(128)
        struct.pack_into("<I", data, 60, 64)
        struct.pack_into("<Q", data, 112, self.BASE)
        with patch.object(retarget_oni.subprocess, "check_output", return_value=output):
            return retarget_oni.normalized_body(Path("SimDLL.dll"), bytes(data), "windows-x64", 0x1000,
                                                {0x1000: 0x1040}, symbolizer)

    def test_bare_objdump_targets_resolve_through_the_pdb(self):
        output = ("180001000: call 0x180002000\n"
                  "180001005: call QWORD PTR [rip+0x4ff5] # 0x180006000\n"
                  "18000100b: lea rcx,[rip+0x51ee] # 0x180006200\n"
                  "180001012: jmp 0x180003010\n"
                  "180001017: lea rdx,[rip+0x12] # 0x180001030\n"
                  "18000101e: jne 0x180001030\n")
        body = self.body(output, self.symbolizer())
        self.assertEqual(body, ["call RVA[0x2000 <?Helper@@YAXXZ>]",
                                "call QWORD PTR [RVA[0x6000 <__imp_QueryPerformanceCounter>]]",
                                "lea rcx,[RVA[0x6200 <kStaticConstants>]]",
                                "jmp RVA[0x3010 <PrivateWork+0x10>]",
                                "lea rdx,[L+0x30]", "jne L+0x30"])
        # Without the PDB the same output refuses, as before.
        with self.assertRaisesRegex(ValueError, "Unresolved external branch"):
            self.body(output, None)
        # A renamed destination at the same address is a different body.
        self.assertNotEqual(body, self.body(output, self.symbolizer(**{"?Helper@@YAXXZ": "?Other@@YAXXZ"})))

    def test_unnamed_pdb_target_still_refuses(self):
        with self.assertRaisesRegex(ValueError, "Unresolved external branch"):
            self.body("180001000: call 0x180001500\n", self.symbolizer())
        with self.assertRaisesRegex(ValueError, "Unresolved RIP-relative"):
            self.body("180001000: mov eax,DWORD PTR [rip+0x8000] # 0x180009010\n", self.symbolizer())

    def test_pdb_module_records_parse_names_with_backticks(self):
        dump = ("  Mod 0001 | `a.obj`:\n"
                "    2540 | S_LPROC32 [size = 128] ``std::vector<int>::_Emplace'::`1'::catch$0`\n"
                "           parent = 0, end = 2992, addr = 0001:776288, code size = 46\n"
                "    3288 | S_LPROC32 [size = 68] `ExchangeHeatEnergyWithWorld`\n"
                "           parent = 0, end = 5192, addr = 0001:254352, code size = 756\n"
                "     564 | S_LDATA32 [size = 56] `REPORT_ERROR`\n"
                "           type = 0x0030 (bool), addr = 0003:12200\n"
                "     600 | S_LOCAL [size = 16] `this`\n")
        sections = [(0x1000, 0x400, 0xca000, 0x60000020), (0xcb000, 0, 0, 0), (0x112000, 0, 0, 0)]
        with patch.object(profiles.subprocess, "check_output", return_value=dump):
            records = profiles.windows_module_symbols(Path("SimDLL.pdb"), sections, "llvm-pdbutil")
            self.assertEqual(profiles.windows_private_procedure(Path("SimDLL.pdb"), sections, "llvm-pdbutil",
                                                                "ExchangeHeatEnergyWithWorld"), 258448)
        self.assertEqual(records, [("S_LPROC32", "`std::vector<int>::_Emplace'::`1'::catch$0", 0x1000 + 776288, 46),
                                   ("S_LPROC32", "ExchangeHeatEnergyWithWorld", 258448, 756),
                                   ("S_LDATA32", "REPORT_ERROR", 0x112000 + 12200, 0)])
        twice = dump + dump.replace("0001:254352", "0001:254400")
        with patch.object(profiles.subprocess, "check_output", return_value=twice):
            with self.assertRaisesRegex(ValueError, "ambiguous"):
                profiles.windows_private_procedure(Path("SimDLL.pdb"), sections, "llvm-pdbutil", "ExchangeHeatEnergyWithWorld")

    def test_changed_contributions_name_every_difference(self):
        old = {0x1000: (16, 1, "a.obj"), 0x2000: (6, 7, "AppVersion.obj"), 0x3000: (8, 9, "gone.obj")}
        new = {0x1000: (16, 1, "a.obj"), 0x2000: (6, 8, "AppVersion.obj"), 0x4000: (4, 2, "new.obj")}
        changes = retarget_oni.changed_contributions(old, new, lambda rva: f"at{rva:x}")
        self.assertEqual([(c["rva"], c["change"], c["object"], c["symbol"]) for c in changes],
                         [(0x2000, "changed", "AppVersion.obj", "at2000"), (0x3000, "removed", "gone.obj", "at3000"),
                          (0x4000, "added", "new.obj", "at4000")])


WINDOWS_REFERENCE = Path(os.environ.get("ONI_GAME_DIR_WINDOWS_REFERENCE") or "/mnt/data/share/oni/OxygenNotIncluded-744825")


@unittest.skipUnless((WINDOWS_REFERENCE / "OxygenNotIncluded_Data/Plugins/x86_64/SimDLL.pdb").is_file()
                     and shutil.which("objdump") and shutil.which("llvm-pdbutil"),
                     "licensed 744825 Windows installation with its PDB, binutils and llvm-pdbutil are needed")
class WindowsRetargetControls(unittest.TestCase):
    """Mutated copies of the private 744825 Windows image, compared through its PDB."""

    @classmethod
    def setUpClass(cls):
        cls.value = json.loads(profiles.PROFILE.read_text())
        cls.image = profiles.sim_image(WINDOWS_REFERENCE, "windows-x64")
        cls.data = cls.image.read_bytes()
        cls.profile = next(p for p in cls.value["profiles"] if p["platform"] == "windows-x64"
                           and p["native_identity"] == profiles.normalized_identity(cls.data))

    def report(self, folder: Path, rva: int, accept_indirect: bool) -> dict:
        root = folder / "game"
        plugins = root / "OxygenNotIncluded_Data/Plugins/x86_64"; plugins.mkdir(parents=True)
        data = bytearray(self.data)
        data[profiles.file_offset(profiles.pe_sections(self.data), rva, 1)] ^= 0x01
        (plugins / "SimDLL.dll").write_bytes(bytes(data))
        (plugins / "SimDLL.pdb").symlink_to(self.image.with_suffix(".pdb"))
        return retarget_oni.native_report("windows-x64", root, WINDOWS_REFERENCE, self.value, "llvm-pdbutil", accept_indirect)

    def test_change_outside_hooked_functions_carries_after_indirect_review(self):
        # The one code change between 744825 and 763796: GetRevision's immediate.
        with tempfile.TemporaryDirectory() as folder:
            refused = self.report(Path(folder), 0x4F931, False)
        self.assertEqual(refused["status"], "needs review")
        self.assertEqual(sum(h["equivalent_body"] for h in refused["hooks"]), 21)
        self.assertEqual(sum("otherwise equivalent" in item for item in refused["needs_review"]), 6, refused["needs_review"])
        self.assertEqual(len(refused["needs_review"]), 7, refused["needs_review"])
        with tempfile.TemporaryDirectory() as folder:
            report = self.report(Path(folder), 0x4F931, True)
        # Only the chunk-heat location of an unregistered image stands in the way.
        self.assertEqual(len(report["needs_review"]), 1, report["needs_review"])
        self.assertIn("not in CHUNK_HEAT_LOCATIONS", report["needs_review"][0])
        self.assertTrue(all(h["equivalent_body"] for h in report["hooks"]))
        self.assertEqual(len(report["accepted_indirect"]), 6)
        candidate = report["candidate"]
        self.assertEqual([(h["rva"], h["fingerprint_sha256"]) for h in candidate["hooks"]],
                         [(h["rva"], h["fingerprint_sha256"]) for h in self.profile["hooks"]])

    def test_change_inside_a_hooked_function_needs_review(self):
        hook = next(h for h in self.profile["hooks"] if h["role"] == "temperature")
        with tempfile.TemporaryDirectory() as folder:
            report = self.report(Path(folder), hook["rva"] + 64, True)
        self.assertEqual(report["status"], "needs review")
        self.assertTrue(any(item.startswith("temperature ") for item in report["needs_review"]), report["needs_review"])
        self.assertFalse(next(h for h in report["hooks"] if h["role"] == "temperature")["equivalent_body"])


@unittest.skipUnless(profiles.sim_image(GAME, "linux-x64").is_file() and shutil.which("objdump") and shutil.which("nm"),
                     "licensed Linux installation and binutils are needed")
class RetargetControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.value = json.loads(profiles.PROFILE.read_text())
        cls.data = profiles.sim_image(GAME, "linux-x64").read_bytes()
        cls.extents = retarget_oni.function_extents(cls.data, "linux-x64", profiles.sim_image(GAME, "linux-x64"))
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
