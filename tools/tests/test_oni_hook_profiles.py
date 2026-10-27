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
            # The backend selects a profile by normalised identity; whole-file
            # and managed-assembly hashes are no longer compiled in.
            self.assertIn(value["native_identity"], text)
            self.assertNotIn(value["native_sha256"], text)
            self.assertNotIn(value["assembly_sha256"], text)
        self.assertNotIn("0x48,", text)

    def test_publication_bindings_preserve_observer_abi_and_reject_drift(self):
        for platform in range(2):
            bindings=self.value['profiles'][platform]['publication_bindings']
            self.assertEqual([h['role'] for h in bindings],['copy-to-game','prepare-game-data-update'])
            for update in ({'rva':0},{'signature':'void()'},{'symbol':'other'},{'fingerprint_sha256':'bad'},
                           {'rva':self.value['profiles'][platform]['hooks'][0]['rva']}):
                value=copy.deepcopy(self.value);value['profiles'][platform]['publication_bindings'][0].update(update)
                with self.assertRaises(ValueError):profiles.validate(value)
            value=copy.deepcopy(self.value);value['profiles'][platform]['publication_bindings'].pop()
            with self.assertRaises(ValueError):profiles.validate(value)
        legacy=copy.deepcopy(self.value)
        for platform in legacy['profiles']:del platform['publication_bindings']
        profiles.validate(legacy)
        self.assertIn('std::array<HookProfile, 0> profile_0_bindings',profiles.header(legacy))
        self.assertIn('std::array<HookProfile, 2> profile_0_bindings',profiles.header(self.value))

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


def identity_pe(stamp=0x11111111, checksum=0x22222222, guid=b"\x33" * 16, code=None):
    """Synthetic PE32+ shared by native/tests/oni_image_identity_tests.cpp."""
    image = bytearray((i * 7 + 3) & 0xFF for i in range(0x400))
    image[0:2] = b"MZ"; struct.pack_into("<I", image, 60, 0x80); image[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", image, 0x84, 0x8664, 1, stamp, 0, 0, 240, 0x2022)
    struct.pack_into("<H", image, 0x98, 0x20B); struct.pack_into("<I", image, 0x98 + 64, checksum)
    struct.pack_into("<I", image, 0x98 + 108, 16)
    directories = [(0, 0)] * 16; directories[0] = (0x1000, 40); directories[6] = (0x1100, 28)
    for index, (rva, size) in enumerate(directories):
        struct.pack_into("<II", image, 0x98 + 112 + 8 * index, rva, size)
    image[0x188:0x190] = b".text\0\0\0"; struct.pack_into("<IIII", image, 0x188 + 8, 0x200, 0x1000, 0x200, 0x200)
    struct.pack_into("<I", image, 0x204, stamp)
    struct.pack_into("<IIHHIIII", image, 0x300, 0, stamp, 0, 0, 2, 24, 0x1180, 0x380)
    image[0x380:0x398] = b"RSDS" + guid + struct.pack("<I", 7)
    if code is not None:
        image[0x3F0] = code
    return bytes(image)


def identity_elf(build_id=b"\x44" * 20, code=None):
    """Synthetic ELF64 shared by native/tests/oni_image_identity_tests.cpp."""
    image = bytearray((i * 7 + 3) & 0xFF for i in range(0x200))
    image[0:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHI", image, 16, 3, 62, 1); struct.pack_into("<Q", image, 32, 64)
    struct.pack_into("<HHH", image, 52, 64, 56, 1)
    struct.pack_into("<IIQQQQQQ", image, 64, 4, 4, 0x100, 0x100, 0x100, 36, 36, 4)
    struct.pack_into("<III", image, 0x100, 4, 20, 3); image[0x10C:0x110] = b"GNU\0"; image[0x110:0x124] = build_id
    if code is not None:
        image[0x1F0] = code
    return bytes(image)


# The C++ test asserts these same values.
PE_IDENTITY = "46826a46eff2245d599daf8a4d07190d561347d63c188e18e265bea5c6838a25"
ELF_IDENTITY = "78245165da47cdd954305d070ccdf123e9fa4721c9433930335c0147f399f623"


class NormalisedIdentityTests(unittest.TestCase):
    def test_stamps_are_ignored_and_code_is_not(self):
        pe = profiles.normalized_identity(identity_pe())
        self.assertEqual(profiles.normalized_identity(identity_pe(stamp=0x5EED, checksum=9, guid=b"\x99" * 16)), pe)
        self.assertNotEqual(profiles.normalized_identity(identity_pe(code=0)), pe)
        self.assertNotEqual(pe, profiles.sha(identity_pe()))
        elf = profiles.normalized_identity(identity_elf())
        self.assertEqual(profiles.normalized_identity(identity_elf(build_id=b"\x01" * 20)), elf)
        self.assertNotEqual(profiles.normalized_identity(identity_elf(code=0)), elf)
        self.assertEqual((pe, elf), (PE_IDENTITY, ELF_IDENTITY))

    def test_malformed_images_are_refused(self):
        for data in (b"", b"MZ" + bytes(10), identity_pe()[:0x300], b"\x7fELF\x02\x01" + bytes(20), b"plain"):
            with self.assertRaises(ValueError):
                profiles.normalized_identity(data)

    def test_committed_profiles_carry_identities(self):
        value = json.loads(profiles.PROFILE.read_text())
        profiles.validate(value)
        changed = json.loads(json.dumps(value)); changed["profiles"][0]["native_identity"] = changed["profiles"][1]["native_identity"]
        with self.assertRaises(ValueError):
            profiles.validate(changed)
        accumulated = json.loads(json.dumps(value)); extra = json.loads(json.dumps(value["profiles"][0]))
        extra["native_identity"] = "f" * 64; extra["oni_build"] = 744826
        accumulated["profiles"].insert(1, extra)
        profiles.validate(accumulated)
        header = profiles.header(accumulated)
        self.assertEqual(header.count("744826U"), 1)
        self.assertIn("profile_1_hooks", header)


GAMES = {"windows-x64": Path("/mnt/data/share/oni/OxygenNotIncluded/OxygenNotIncluded_Data/Plugins/x86_64/SimDLL.dll"),
         "linux-x64": Path("/mnt/data/share/oni/OxygenNotIncluded-linux/OxygenNotIncluded_Data/Plugins/libSimDLL.so")}


class InstalledImageControls(unittest.TestCase):
    """Synthetic changes to the private 744825 images (skipped without them)."""

    def image(self, platform):
        path = GAMES[platform]
        if not path.is_file():
            self.skipTest("licensed " + platform + " SimDLL not available")
        return bytearray(path.read_bytes())

    def test_identity_ignores_stamps_and_catches_code(self):
        value = json.loads(profiles.PROFILE.read_text())
        for profile in value["profiles"]:
            data = self.image(profile["platform"])
            self.assertEqual(profiles.normalized_identity(bytes(data)), profile["native_identity"])
            hook = profile["hooks"][3]
            if profile["platform"] == "windows-x64":
                pe = struct.unpack_from("<I", data, 60)[0]
                restamped = bytearray(data); struct.pack_into("<I", restamped, pe + 8, 0x12345678)
                self.assertEqual(profiles.normalized_identity(bytes(restamped)), profile["native_identity"])
                offset = profiles.file_offset(profiles.pe_sections(bytes(data)), hook["rva"], 32)
            else:
                segments = profiles.linux_load_segments(bytes(data))
                offset = profiles.file_offset([(s["rva"], s["file_offset"], s["file_bytes"], 0) for s in segments], hook["rva"], 32)
            changed = bytearray(data); changed[offset + 5] ^= 0xFF
            self.assertNotEqual(profiles.normalized_identity(bytes(changed)), profile["native_identity"])
            elsewhere = bytearray(data); elsewhere[len(data) // 2] ^= 0x01
            self.assertNotEqual(profiles.normalized_identity(bytes(elsewhere)), profile["native_identity"])

    def test_backend_probe_accepts_only_verified_images(self):
        import ctypes, os, shutil, tempfile
        library = os.environ.get("MA_TEST_ONI_BACKEND_LIBRARY")
        if not library or not Path(library).is_file():
            self.skipTest("set MA_TEST_ONI_BACKEND_LIBRARY to a built libmixed_atmospheres_oni.so")
        data = self.image("linux-x64")
        backend = ctypes.CDLL(library)
        backend.ma_oni_last_error.restype = ctypes.c_char_p

        class Config(ctypes.Structure):
            _fields_ = [("size", ctypes.c_uint32), ("version", ctypes.c_uint32), ("flags", ctypes.c_uint32),
                        ("reserved", ctypes.c_uint32), ("game", ctypes.c_char_p)]
        status = (ctypes.c_uint8 * 528)()
        ctypes.cast(status, ctypes.POINTER(ctypes.c_uint32))[0] = 528
        ctypes.cast(status, ctypes.POINTER(ctypes.c_uint32))[1] = 4

        def probe(image):
            with tempfile.TemporaryDirectory() as folder:
                plugins = Path(folder) / "OxygenNotIncluded_Data/Plugins"; plugins.mkdir(parents=True)
                (plugins / "libSimDLL.so").write_bytes(bytes(image))
                config = Config(24, 4, 1, 0, folder.encode())
                return backend.ma_oni_probe(ctypes.byref(config), status), backend.ma_oni_last_error().decode()
        self.assertEqual(probe(data)[0], 0)
        stamped = bytearray(data)
        note = bytes(stamped).find(b"GNU\0", 0, 4096)
        self.assertGreater(note, 0)
        stamped[note + 4:note + 24] = bytes(range(20))  # a different build-id only
        self.assertEqual(probe(stamped)[0], 0)
        changed = bytearray(data); changed[len(data) // 2] ^= 0x01
        result, message = probe(changed)
        self.assertEqual(result, 2)
        self.assertIn("not a verified image", message)
