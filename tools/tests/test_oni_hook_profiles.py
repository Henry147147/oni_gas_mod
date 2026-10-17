"""Native hook profiles reject drift instead of selecting approximate addresses."""
import copy
import json
from pathlib import Path
import sys
import struct
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import oni_hook_profiles as profiles


class NativeProfileTests(unittest.TestCase):
    def setUp(self):
        self.value = json.loads(profiles.PROFILE.read_text())

    def test_pinned_profiles_and_platform_specific_step(self):
        profiles.validate(self.value)
        win, linux = self.value["profiles"]
        self.assertEqual(len(win["hooks"]), 25)
        self.assertEqual(len(linux["hooks"]), 29)
        a,b=(next(h for h in p['hooks'] if h['role']=='chunk-heat') for p in (win,linux))
        self.assertNotEqual(a['signature'],b['signature'])
        self.assertEqual(a['native_layout'],b['native_layout'])
        self.assertNotIn("emitter-dispatch", [h["role"] for h in win["hooks"]])
        self.assertEqual(next(h["signature"] for h in linux["hooks"] if h["role"] == "gas-displacement"), "float(uint16,int,int,int,data*)")
        self.assertNotIn("gas-interaction", [h["role"] for h in win["hooks"]])
        self.assertEqual(next(h["signature"] for h in linux["hooks"] if h["role"] == "gas-interaction"), "bool(data*,events*,int)")
        self.assertNotIn("step", [h["role"] for h in win["hooks"]])
        self.assertNotEqual(win["assembly_sha256"], linux["assembly_sha256"])
        self.assertNotEqual(win["native_sha256"], linux["native_sha256"])

    def test_missing_duplicate_or_misidentified_hooks_rejected(self):
        for mutate in (lambda p: p["hooks"].pop(),
                       lambda p: p["hooks"].append(p["hooks"][0]),
                       lambda p: p["hooks"][0].update(role="other"),
                       lambda p: p["hooks"][0].update(signature="int()"),
                       lambda p: p["hooks"][0].update(symbol="unknown")):
            value = copy.deepcopy(self.value)
            mutate(value["profiles"][1])
            with self.assertRaises(ValueError): profiles.validate(value)

    def test_unknown_build_abi_or_fingerprint_rejected(self):
        for key in ("oni_build", "schema_version"):
            value = copy.deepcopy(self.value); value[key] += 1
            with self.assertRaises(ValueError): profiles.validate(value)
        for update in ({"rva": 0}, {"fingerprint_bytes": 8}, {"fingerprint_sha256": "0" * 63}):
            value = copy.deepcopy(self.value); value["profiles"][1]["hooks"][0].update(update)
            with self.assertRaises(ValueError): profiles.validate(value)

    def test_codegen_contains_hashes_without_game_code(self):
        text = profiles.header(self.value)
        for value in self.value["profiles"]:
            self.assertIn(value["native_sha256"], text)
            self.assertIn(value["assembly_sha256"], text)
        self.assertNotIn("0x48,", text)

    def test_native_body_layout_and_private_location_drift_rejected(self):
        for platform in range(2):
            for key in profiles.CHUNK_HEAT_LAYOUT:
                value=copy.deepcopy(self.value)
                next(h for h in value['profiles'][platform]['hooks'] if h['role']=='chunk-heat')['native_layout'][key]+=4
                with self.assertRaises(ValueError):profiles.validate(value)
            value=copy.deepcopy(self.value)
            next(h for h in value['profiles'][platform]['hooks'] if h['role']=='chunk-heat')['rva']+=16
            with self.assertRaises(ValueError):profiles.validate(value)

    def test_invalid_trampoline_storage_rejected(self):
        original=self.value["profiles"][1]["trampoline_storage"]
        for update in ({"rva": original["rva"]-16}, {"bytes": original["bytes"]+16},
                       {"bytes": -1}, {"bytes": True}, {"bytes": 1 << 64},
                       {"page_bytes": 8192}, {"fill_byte": 1}, {"sha256": "0"*64},
                       {"load_segment_memory_bytes": 1 << 64}, {"load_segment_file_offset": 1}):
            with self.subTest(update=update):
                value=copy.deepcopy(self.value)
                value["profiles"][1]["trampoline_storage"].update(update)
                with self.assertRaises(ValueError): profiles.validate(value)
        value=copy.deepcopy(self.value);del value["profiles"][1]["trampoline_storage"]
        with self.assertRaises(ValueError): profiles.validate(value)

    @staticmethod
    def elf(flags=5, memory=0x1a1, overlap=False):
        data=bytearray(0x3000)
        data[:6]=b"\x7fELF\x02\x01"
        struct.pack_into("<H",data,18,62)
        struct.pack_into("<Q",data,32,64)
        struct.pack_into("<HH",data,54,56,2)
        struct.pack_into("<IIQQQQQQ",data,64,1,flags,0x1000,0x1000,0,0x1a1,memory,4096)
        struct.pack_into("<IIQQQQQQ",data,120,1,4,0x2000,0x2000 if not overlap else 0x11b0,0,0x1000,0x1000,4096)
        return data

    def test_elf_padding_excludes_allocated_bytes_and_other_segments(self):
        data=self.elf()
        data[0x11a0]=0xcc  # Last allocated byte belongs to the program.
        result=profiles.linux_trampoline_storage(data)
        self.assertEqual((result["rva"],result["bytes"]),(0x11b0,0xe50))
        self.assertEqual(result["sha256"],profiles.sha(bytes(0xe50)))
        data[0x11b0]=1
        with self.assertRaises(ValueError): profiles.linux_trampoline_storage(data)
        for invalid in (self.elf(flags=7),self.elf(memory=0x1a2),self.elf(overlap=True)):
            with self.assertRaises(ValueError): profiles.linux_trampoline_storage(invalid)

    def test_elf_program_header_bounds_rejected(self):
        data=self.elf()
        struct.pack_into("<Q",data,32,len(data))
        with self.assertRaises(ValueError): profiles.linux_trampoline_storage(data)
        data=self.elf();struct.pack_into("<H",data,54,64)
        with self.assertRaises(ValueError): profiles.linux_trampoline_storage(data)


if __name__ == "__main__": unittest.main()
