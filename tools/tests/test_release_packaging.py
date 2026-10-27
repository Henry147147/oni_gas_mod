"""Release folder and ZIP: contents, identity, determinism (docs/distribution.md)."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import mod_identity  # noqa: E402

SPEC = importlib.util.spec_from_file_location("package_release", TOOLS / "package_release.py")
RELEASE = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(RELEASE)
OBSERVER_SPEC = importlib.util.spec_from_file_location("observer_tests", TOOLS / "tests/test_observer_packaging.py")
OBSERVER = importlib.util.module_from_spec(OBSERVER_SPEC); OBSERVER_SPEC.loader.exec_module(OBSERVER)


def managed_image(stamp=0, guid=b"\x11" * 16, debug=b"RSDS" + b"\x22" * 28, il=b"\x2a" * 16):
    """A minimal PE32 managed image: CLI header, metadata root with #~ and
    #GUID streams, one debug directory entry and an IL-like payload."""
    data = bytearray(0x1200); data[:2] = b"MZ"; struct.pack_into("<I", data, 60, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, 0x14C, 1, stamp, 0, 0, 224, 0x2102)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x10B); struct.pack_into("<I", data, optional + 64, stamp ^ 0x55)
    struct.pack_into("<II", data, optional + 96 + 6 * 8, 0x2200, 28)
    struct.pack_into("<II", data, optional + 96 + 14 * 8, 0x2000, 72)
    section = optional + 224
    data[section:section + 8] = b".text\0\0\0"
    struct.pack_into("<IIII", data, section + 8, 0x1000, 0x2000, 0x1000, 0x200)
    struct.pack_into("<IHHII", data, 0x200, 72, 2, 5, 0x2100, 0x100)
    root = 0x300
    struct.pack_into("<IHHII", data, root, 0x424A5342, 1, 1, 0, 12); data[root + 16:root + 28] = b"v4.0.30319\0\0"
    struct.pack_into("<HH", data, root + 28, 0, 2)
    struct.pack_into("<II", data, root + 32, 0x40, 0x10); data[root + 40:root + 44] = b"#~\0\0"
    struct.pack_into("<II", data, root + 44, 0x50, 16); data[root + 52:root + 60] = b"#GUID\0\0\0"
    data[root + 0x50:root + 0x60] = guid
    struct.pack_into("<IIHHIIII", data, 0x400, 0, stamp, 0, 0, 2, len(debug), 0x2300, 0x500)
    data[0x500:0x500 + len(debug)] = debug
    data[0x600:0x600 + len(il)] = il
    return bytes(data)


def fake_contract(managed, games, output):
    """Stands in for tools/GameContract, which needs the real mod and game."""
    output.write_text(json.dumps({"SchemaVersion": 1, "ModuleVersionId": "fixture", "Platforms": {p: {} for p in games}}))


def setUpModule():
    global CONTRACT_PATCHES
    CONTRACT_PATCHES = [mock.patch.object(RELEASE.game_contract, "generate", side_effect=fake_contract),
                        mock.patch.object(RELEASE.game_contract, "game_root", side_effect=lambda platform, explicit=None: Path("/game/" + platform))]
    for patch in CONTRACT_PATCHES:
        patch.start()


def tearDownModule():
    for patch in CONTRACT_PATCHES:
        patch.stop()


class ManagedEquivalenceTests(unittest.TestCase):
    def test_only_build_identity_is_ignored(self):
        base = RELEASE.normalized_managed_image(managed_image())
        other = managed_image(stamp=0x12345678, guid=b"\x99" * 16, debug=b"RSDS" + b"\x77" * 28)
        self.assertEqual(RELEASE.normalized_managed_image(other), base)
        self.assertNotEqual(RELEASE.normalized_managed_image(managed_image(il=b"\x2b" * 16)), base)

    def test_builds_must_agree(self):
        with tempfile.TemporaryDirectory() as folder:
            windows, linux = Path(folder) / "w", Path(folder) / "l"
            for directory, stamp in ((windows, 1), (linux, 2)):
                directory.mkdir()
                for name in RELEASE.MANAGED:
                    (directory / name).write_bytes(managed_image(stamp=stamp, guid=bytes([stamp]) * 16))
            self.assertEqual(set(RELEASE.managed_equivalence(windows, linux)), set(RELEASE.MANAGED))
            (linux / "MixedAtmospheres.Oni.dll").write_bytes(managed_image(il=b"\x00" * 16))
            with self.assertRaisesRegex(ValueError, "differs between"):
                RELEASE.managed_equivalence(windows, linux)

    def test_non_managed_images_rejected(self):
        for data in (b"", b"MZ" + bytes(100), OBSERVER.native_pe()):
            with self.assertRaises(ValueError):
                RELEASE.normalized_managed_image(data)


class ReleaseAssemblyTests(unittest.TestCase):
    def fixture(self, root: Path, licence=b"funchook licence"):
        builds = {}
        for rid, (solver, observer) in RELEASE.NATIVE.items():
            build = root / ("build-" + rid)
            for name in ("CMakeCache.txt", "_deps/ma_funchook-src/src/funchook_x86.c"):
                (build / name).parent.mkdir(parents=True, exist_ok=True); (build / name).write_text("fixture")
            generated = build / "oni-generated"
            (generated / "licenses/funchook").mkdir(parents=True); (generated / "licenses/funchook/LICENSE").write_bytes(licence)
            (generated / "licenses/distorm").mkdir(parents=True); (generated / "licenses/distorm/COPYING").write_text("distorm")
            lines = ["Funchook 1.1.3: pinned"] + (["mimalloc 2.5.2 (Windows only)"] if rid == "win-x64" else [])
            (generated / "third-party-sources.txt").write_text("\n".join(lines) + "\n")
            image = OBSERVER.native_elf() if rid == "linux-x64" else OBSERVER.native_pe()
            for name in (solver, observer):
                (build / name).write_bytes(image)
            builds[rid] = build
        info = root / "compatibility-build.json"
        info.write_text(json.dumps({"kind": "linux-native-compatibility-build", "passed": True, "libraries": {
            name: {"sha256": hashlib.sha256((builds["linux-x64"] / name).read_bytes()).hexdigest(), "required_glibc": ["2.34"]}
            for name in RELEASE.NATIVE["linux-x64"]}}))
        managed = {}
        for platform, stamp in (("windows", 7), ("linux", 9)):
            directory = root / ("managed-" + platform); directory.mkdir()
            for name in RELEASE.MANAGED:
                (directory / name).write_bytes(managed_image(stamp=stamp, guid=bytes([stamp]) * 16))
            managed[platform] = directory
        mingw = root / "mingw"
        for source in RELEASE.MINGW_NOTICES.values():
            (mingw / source).parent.mkdir(parents=True, exist_ok=True); (mingw / source).write_text("notice " + source)
        self.mingw = mingw
        return builds, info, managed

    def arguments(self, root, output, builds, info, managed, preview=None):
        return mock.Mock(output=output, linux_native=builds["linux-x64"], windows_native=builds["win-x64"],
                         linux_build_info=info, managed_linux=managed["linux"], managed_windows=managed["windows"],
                         preview=preview, allow_dirty=False, windows_game=None, linux_game=None, mingw_root=self.mingw)

    def build(self, root, name="release", **changes):
        builds, info, managed = self.fixture(root / ("inputs-" + name), **changes)
        output = root / name
        with mock.patch.object(RELEASE.package_probe, "source_state", return_value=("a" * 40, False, [])):
            manifest = RELEASE.assemble(self.arguments(root, output, builds, info, managed))
        return output, manifest

    def test_release_folder_is_complete_and_clean(self):
        with tempfile.TemporaryDirectory() as folder:
            output, manifest = self.build(Path(folder))
            files = set(manifest["files"])
            for required in ("MixedAtmospheres.Oni.dll", "MixedAtmospheres.Core.dll", "mod.yaml", "mod_info.yaml", "README.txt", "game-contract.json",
                             "native/linux-x64/libmixed_atmospheres.so", "native/linux-x64/libmixed_atmospheres_oni.so",
                             "native/win-x64/mixed_atmospheres.dll", "native/win-x64/mixed_atmospheres_oni.dll",
                             "third-party/licenses/funchook/LICENSE", "third-party/licenses/distorm/COPYING",
                             "third-party/third-party-sources.txt", "third-party/funchook-1.1.3-with-distorm.tar.gz",
                             "native-build-provenance.json"):
                self.assertIn(required, files)
            self.assertFalse(any(name.startswith("elements/") for name in files), "removed nitrogen content must not be packaged")
            for forbidden in RELEASE.FORBIDDEN:
                self.assertNotIn(forbidden, files)
            self.assertFalse(any("coupled" in name or "private-validation" in name for name in files))
            self.assertEqual((manifest["kind"], manifest["release_ready"], manifest["static_id"]), ("release", True, "MixedAtmospheres"))
            self.assertEqual(manifest["version"], mod_identity.version())
            self.assertIn('staticID: "MixedAtmospheres"', (output / "mod.yaml").read_text())
            self.assertEqual((output / "mod_info.yaml").read_text(), mod_identity.mod_info(mod_identity.version()))
            self.assertIn("mimalloc", (output / "third-party/third-party-sources.txt").read_text())
            self.assertIn("winpthreads", (output / "third-party/third-party-sources.txt").read_text())
            for notice in RELEASE.MINGW_NOTICES:
                self.assertIn("third-party/licenses/" + notice, files)
            readme = (output / "README.txt").read_text()
            for text in ("64-bit Windows or 64-bit Linux", "Spaced Out!", "cannot be", "mods/config/MixedAtmospheres", "Player.log"):
                self.assertIn(text, readme)
            for name, digest in manifest["files"].items():
                self.assertEqual(hashlib.sha256((output / name).read_bytes()).hexdigest(), digest)

    def test_zip_is_deterministic(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            first, _ = self.build(root, "one"); second, _ = self.build(root, "two")
            a, b = RELEASE.write_zip(first, root / "one.zip"), RELEASE.write_zip(second, root / "two.zip")
            self.assertEqual(a, b)
            with zipfile.ZipFile(root / "one.zip") as archive:
                names = archive.namelist()
                self.assertEqual(names, sorted(names))
                self.assertTrue(all(name.startswith("MixedAtmospheres/") for name in names))
                self.assertIn("MixedAtmospheres/package-manifest.json", names)

    def test_changed_folder_is_not_zipped(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); output, _ = self.build(root)
            (output / "integration-probe.json").write_text("{}")
            with self.assertRaisesRegex(ValueError, "changed after"):
                RELEASE.write_zip(output, root / "release.zip")
            (output / "integration-probe.json").unlink(); (output / "README.txt").write_text("edited")
            with self.assertRaisesRegex(ValueError, "changed after"):
                RELEASE.write_zip(output, root / "again.zip")

    def test_dirty_sources_and_mismatched_inputs_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); builds, info, managed = self.fixture(root / "inputs")
            arguments = self.arguments(root, root / "dirty", builds, info, managed)
            with mock.patch.object(RELEASE.package_probe, "source_state", return_value=("a" * 40, True, [" M x"])):
                with self.assertRaisesRegex(ValueError, "uncommitted"):
                    RELEASE.assemble(arguments)
            self.assertFalse((root / "dirty").exists())
            (managed["linux"] / "MixedAtmospheres.Core.dll").write_bytes(managed_image(il=b"\x01" * 16))
            with mock.patch.object(RELEASE.package_probe, "source_state", return_value=("a" * 40, False, [])):
                with self.assertRaisesRegex(ValueError, "differs between"):
                    RELEASE.assemble(self.arguments(root, root / "mismatch", builds, info, managed))

    def test_conflicting_licences_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); builds, info, managed = self.fixture(root / "inputs")
            (builds["win-x64"] / "oni-generated/licenses/funchook/LICENSE").write_text("different")
            with mock.patch.object(RELEASE.package_probe, "source_state", return_value=("a" * 40, False, [])):
                with self.assertRaisesRegex(ValueError, "Licence"):
                    RELEASE.assemble(self.arguments(root, root / "out", builds, info, managed))
            self.assertFalse((root / "out").exists())

    def test_forbidden_contents(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ("development.json", "game-contract-local/elements.bin", "private-validation/x.dll",
                         "native/win-x64/mixed_atmospheres_coupled.dll", "Extra.dll", "install_windows.ps1"):
                path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text("x")
                with self.subTest(name=name), self.assertRaises(ValueError):
                    RELEASE.check_release_contents(root, [path])
                path.unlink()

    def test_source_archive_is_deterministic(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "src"; (source / "a").mkdir(parents=True)
            (source / "a/file.c").write_text("int x;"); (source / "b.txt").write_text("b")
            first = RELEASE.deterministic_tar_gz(source, "pkg")
            import os
            os.utime(source / "b.txt", (1, 1))
            self.assertEqual(RELEASE.deterministic_tar_gz(source, "pkg"), first)
            (source / "b.txt").write_text("c")
            self.assertNotEqual(RELEASE.deterministic_tar_gz(source, "pkg"), first)


class IdentityTests(unittest.TestCase):
    def test_single_version_source(self):
        self.assertRegex(mod_identity.version(), r"^\d+\.\d+\.\d+$")
        with tempfile.TemporaryDirectory() as folder:
            project = Path(folder) / "p.csproj"
            project.write_text("<Version>1.2.3</Version><InformationalVersion>1.2.3-x</InformationalVersion>")
            with self.assertRaises(ValueError):
                mod_identity.version(project)

    def test_probe_and_release_share_the_static_id(self):
        self.assertEqual(mod_identity.STATIC_ID, "MixedAtmospheres")
        self.assertIn('staticID: "MixedAtmospheres"', mod_identity.mod_yaml("x"))
        with self.assertRaises(ValueError):
            mod_identity.mod_yaml('quote " inside')


if __name__ == "__main__":
    unittest.main()
