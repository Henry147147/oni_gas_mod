import importlib.util
from pathlib import Path
import tempfile
import unittest
import sys
import struct
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

SPEC = importlib.util.spec_from_file_location('observer_package', Path(__file__).resolve().parents[1] / 'package_probe.py')
PACKAGE = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(PACKAGE)


def native_elf():
    data = bytearray(256)
    data[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHIQQQIHHHHHH", data, 16, 3, 62, 1, 0, 64, 0, 0, 64, 56, 2, 64, 0, 0)
    struct.pack_into("<IIQQQQQQ", data, 64, 1, 5, 0, 0, 0, len(data), len(data), 4096)
    struct.pack_into("<IIQQQQQQ", data, 120, 2, 6, 176, 176, 176, 16, 16, 8)
    return bytes(data)


def native_pe():
    data = bytearray(1024); data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 128); data[128:132] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 132, 0x8664, 1, 0, 0, 0, 240, 0x2000)
    struct.pack_into("<H", data, 152, 0x20b)
    struct.pack_into("<II", data, 152 + 56, 8192, 512)
    struct.pack_into("<III", data, 152 + 108, 16, 4096, 40)
    struct.pack_into("<IIII", data, 152 + 240 + 8, 512, 4096, 512, 512)
    struct.pack_into("<I", data, 512 + 20, 1)
    return bytes(data)


class NativeSnapshotTests(unittest.TestCase):
    def test_headers_and_complete_file_ranges_are_required_for_both_platforms(self):
        for suffix, payload in ((".so", native_elf()), (".dll", native_pe())):
            PACKAGE.validate_native_image(payload, suffix)
            for partial in (b"", payload[:64], payload[:-1]):
                with self.subTest(suffix=suffix, size=len(partial)), self.assertRaisesRegex(ValueError, "native image"):
                    PACKAGE.validate_native_image(partial, suffix)

    def test_native_copy_matches_a_stable_validated_source(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for suffix, payload in ((".so", native_elf()), (".dll", native_pe())):
                source, target = root / ("source" + suffix), root / ("copy" + suffix)
                source.write_bytes(payload); PACKAGE.copy_native_binary(source, target)
                self.assertEqual(target.read_bytes(), payload)
                self.assertEqual(source.read_bytes(), payload)

    def test_invalid_source_never_leaves_a_native_copy(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); source, target = root / "source.so", root / "copy.so"
            for payload in (b"", native_elf()[:64], native_elf()[:-1]):
                source.write_bytes(payload)
                with self.assertRaises(ValueError): PACKAGE.copy_native_binary(source, target)
                self.assertFalse(target.exists())

    def test_concurrent_link_rejects_and_removes_the_created_copy(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); source, target = root / "source.so", root / "copy.so"
            payload = native_elf(); source.write_bytes(payload)
            with mock.patch.object(PACKAGE.shutil, "copystat", side_effect=lambda *_: source.write_bytes(payload + b"changed")):
                with self.assertRaisesRegex(ValueError, "changed during packaging"):
                    PACKAGE.copy_native_binary(source, target)
            self.assertFalse(target.exists())

    def test_changed_destination_rejects_even_when_source_is_stable(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); source, target = root / "source.dll", root / "copy.dll"
            payload = native_pe(); source.write_bytes(payload)
            with mock.patch.object(PACKAGE.shutil, "copystat", side_effect=lambda *_: target.write_bytes(payload[:-1])):
                with self.assertRaisesRegex(ValueError, "copy differs"):
                    PACKAGE.copy_native_binary(source, target)
            self.assertFalse(target.exists()); self.assertEqual(source.read_bytes(), payload)


class ObserverBuildLayoutTests(unittest.TestCase):
    def complete(self, root):
        for name in ('CMakeCache.txt', 'oni-generated/licenses/funchook/LICENSE',
                     'oni-generated/licenses/distorm/COPYING', '_deps/ma_funchook-src/src/funchook_x86.c'):
            path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text('fixture')

    def test_single_config(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); self.complete(root)
            self.assertEqual(PACKAGE.observer_build_root(root / 'libmixed_atmospheres_oni.so'), root.resolve())

    def test_visual_studio_release(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); self.complete(root); (root / 'Release').mkdir()
            self.assertEqual(PACKAGE.observer_build_root(root / 'Release/mixed_atmospheres_oni.dll'), root.resolve())

    def test_incomplete_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); self.complete(root); (root / 'oni-generated/licenses/distorm/COPYING').unlink()
            with self.assertRaises(ValueError): PACKAGE.observer_build_root(root / 'mixed_atmospheres_oni.dll')

    def test_ambiguous_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); self.complete(root); self.complete(root / 'Release')
            with self.assertRaises(ValueError): PACKAGE.observer_build_root(root / 'Release/mixed_atmospheres_oni.dll')


if __name__ == '__main__':
    unittest.main()
