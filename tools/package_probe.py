#!/usr/bin/env python3
"""Package an explicitly labelled developer probe, never a release overhaul."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
from build_linux_compat import glibc_versions_within_limit
import game_contract
import mod_identity

ROOT = Path(__file__).resolve().parents[1]


def validate_native_image(data: bytes, suffix: str) -> None:
    """Reject empty/truncated link outputs before they can acquire a manifest."""
    def require(condition, reason):
        if not condition:
            raise ValueError("Incomplete or invalid native image: " + reason)

    if suffix == ".so":
        require(len(data) >= 64 and data[:7] == b"\x7fELF\x02\x01\x01", "expected ELF64 little-endian header")
        kind, machine, version, _, phoff, shoff, _, ehsize, phsize, phnum, shsize, shnum, _ = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
        require((kind, machine, version, ehsize) == (3, 62, 1, 64), "expected x86-64 shared object")
        require(phsize == 56 and phnum > 0 and phoff >= 64 and phoff + phnum * phsize <= len(data), "truncated program headers")
        load = dynamic = False
        for i in range(phnum):
            tag, _, offset, _, _, size, memory, _ = struct.unpack_from("<IIQQQQQQ", data, phoff + i * phsize)
            require(offset + size <= len(data), "truncated program segment")
            if tag == 1:
                require(memory >= size, "load segment is smaller than its file image")
                load = True
            if tag == 2:
                require(size >= 16 and size % 16 == 0, "incomplete dynamic table")
                require(any(struct.unpack_from("<q", data, at)[0] == 0 for at in range(offset, offset + size, 16)), "unterminated dynamic table")
                dynamic = True
        require(load and dynamic, "shared object lacks load/dynamic segments")
        if shnum:
            require(shsize == 64 and shoff >= 64 and shoff + shnum * shsize <= len(data), "truncated section headers")
            for i in range(shnum):
                _, tag, _, _, offset, size, _, _, _, _ = struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shsize)
                require(tag == 8 or offset + size <= len(data), "truncated file-backed section")
    elif suffix == ".dll":
        require(len(data) >= 64 and data[:2] == b"MZ", "expected DOS header")
        pe, = struct.unpack_from("<I", data, 60)
        require(pe >= 64 and pe + 24 <= len(data) and data[pe:pe + 4] == b"PE\0\0", "truncated PE header")
        machine, sections, _, _, _, optional_size, flags = struct.unpack_from("<HHIIIHH", data, pe + 4)
        optional = pe + 24
        require(machine == 0x8664 and 0 < sections <= 96 and flags & 0x2000, "expected x86-64 DLL")
        require(optional_size >= 120 and optional + optional_size + 40 * sections <= len(data), "truncated optional/section headers")
        require(struct.unpack_from("<H", data, optional)[0] == 0x20b, "expected PE32+ optional header")
        image_size, headers_size = struct.unpack_from("<II", data, optional + 56)
        require(optional + optional_size + 40 * sections <= headers_size <= len(data), "incomplete image headers")
        require(struct.unpack_from("<I", data, optional + 108)[0] > 0, "missing export directory")
        export_rva, export_size = struct.unpack_from("<II", data, optional + 112)
        require(export_rva > 0 and export_size >= 40, "incomplete export directory")
        export_offset = None
        for i in range(sections):
            at = optional + optional_size + 40 * i
            virtual_size, address, size, offset = struct.unpack_from("<IIII", data, at + 8)
            require(offset + size <= len(data) and address + max(virtual_size, size) <= image_size, "truncated section image")
            if address <= export_rva and export_rva + 40 <= address + size:
                export_offset = offset + export_rva - address
        require(export_offset is not None and export_offset + 40 <= len(data), "export directory has no file-backed section")
        require(struct.unpack_from("<I", data, export_offset + 20)[0] > 0, "DLL has no exported functions")
    else:
        raise ValueError("Unknown native image suffix: " + suffix)


def copy_native_binary(source: Path, target: Path) -> None:
    """Copy one validated snapshot and refuse a concurrent link or changed copy."""
    signature = lambda value: (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns)
    before = signature(source.stat())
    data = source.read_bytes()
    if signature(source.stat()) != before or len(data) != before[2]:
        raise ValueError("Native input changed while reading: " + str(source))
    validate_native_image(data, source.suffix)
    digest = hashlib.sha256(data).digest()
    created = False
    try:
        with target.open("xb") as output:
            created = True
            output.write(data)
        shutil.copystat(source, target)
        if signature(source.stat()) != before or hashlib.sha256(source.read_bytes()).digest() != digest or signature(source.stat()) != before:
            raise ValueError("Native input changed during packaging: " + str(source))
        if hashlib.sha256(target.read_bytes()).digest() != digest:
            raise ValueError("Native package copy differs from its validated input: " + str(target))
    except Exception:
        if created:
            target.unlink(missing_ok=True)
        raise


def observer_build_root(observer: Path) -> Path:
    """Find dependency notices for single- and multi-config CMake outputs."""
    parent = observer.resolve().parent
    required = ("CMakeCache.txt", "oni-generated/licenses/funchook/LICENSE",
                "oni-generated/licenses/distorm/COPYING", "_deps/ma_funchook-src/src/funchook_x86.c")
    candidates = [root for root in (parent, parent.parent)
                  if all((root / name).is_file() for name in required)]
    if len(candidates) != 1:
        raise ValueError("Observer must identify exactly one CMake build with dependency sources/notices")
    return candidates[0]


def source_state() -> tuple[str, bool, list[str]]:
    """Return the source tree revision, dirty flag and short status at packaging time.

    This records the tree the package was assembled from; it does not prove the
    copied binaries were rebuilt from that tree.
    """
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    status = subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=normal"],
                                     cwd=ROOT, text=True).splitlines()
    return revision, bool(status), status

def native_build_info(path: Path | None, libraries: list[Path]) -> dict | None:
    if path is None:
        return None
    report = json.loads(path.read_text())
    if not isinstance(report, dict) or report.get("kind") != "linux-native-compatibility-build" or report.get("passed") is not True:
        raise ValueError("Native build provenance is not a completed compatibility build")
    recorded = report.get("libraries")
    if not isinstance(recorded, dict):
        raise ValueError("Native build provenance has no library inventory")
    for library in libraries:
        entry = recorded.get(library.name)
        if not isinstance(entry, dict) or entry.get("sha256") != hashlib.sha256(library.read_bytes()).hexdigest():
            raise ValueError("Native build provenance differs from the packaged library: " + library.name)
        if not glibc_versions_within_limit(entry.get("required_glibc")):
            raise ValueError("Native build provenance exceeds or omits the glibc 2.34 ceiling: " + library.name)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True, help="libmixed_atmospheres.so or mixed_atmospheres.dll")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--managed-dir", type=Path, default=ROOT / "managed/MixedAtmospheres.Oni/bin/Release/netstandard2.1",
                        help="Built managed adapter directory, for separate Windows/Linux outputs")
    parser.add_argument("--coupled-reference", type=Path, help="Optional explicit closed-reference library for private presentation validation")
    parser.add_argument("--native-observer", type=Path, help="Opt-in SimDLL backend from its CMake build directory; forwards by default, experimental sealed domains require separate validation")
    parser.add_argument("--native-build-info", type=Path, help="Completed Linux compatibility build report, checked against all supplied native binaries")
    parser.add_argument("--oni-build", type=int, default=744825, help="Verified ONI build number, not Steam BuildID")
    parser.add_argument("--game-dir", type=Path, help="Licensed installation for the managed game contract "
                        "(default: ONI_GAME_DIR_WINDOWS/ONI_GAME_DIR_LINUX/ONI_GAME_DIR or the team's share copy)")
    parser.add_argument("--no-game-contract", action="store_true",
                        help="Omit game-contract.json: the mod then accepts only the exact inspected 744825 assemblies")
    parser.add_argument("--private-file-protocol-validator", type=Path,
                        help="Optional normal-source Windows Mono fixture DLL; never a save/load permission")
    args = parser.parse_args()
    if args.oni_build != 744825:
        parser.error("This probe was inspected against ONI 744825. Reinspect and update the contract before packaging another build.")
    if args.native.name not in ("libmixed_atmospheres.so", "mixed_atmospheres.dll") or not args.native.is_file():
        parser.error("Supply a built Linux or Windows native library")
    coupled = args.coupled_reference
    expected_coupled = "mixed_atmospheres_coupled.dll" if args.native.suffix == ".dll" else "libmixed_atmospheres_coupled.so"
    if coupled is not None and (coupled.name != expected_coupled or not coupled.is_file()):
        parser.error("Supply the matching platform's explicit coupled reference library")
    validator = args.private_file_protocol_validator
    observer = args.native_observer
    observer_name = "mixed_atmospheres_oni.dll" if args.native.suffix == ".dll" else "libmixed_atmospheres_oni.so"
    observer_root = None
    if observer is not None:
        if observer.name != observer_name or not observer.is_file():
            parser.error("Supply the platform observer from its CMake build directory")
        try:
            observer_root = observer_build_root(observer)
        except ValueError as error:
            parser.error(str(error))
    observer_support = observer_root / "oni-generated" if observer_root else None
    observer_source = observer_root / "_deps/ma_funchook-src" if observer_root else None
    try:
        build_info = native_build_info(args.native_build_info, [args.native, *([observer] if observer else []), *([coupled] if coupled else [])])
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if validator is not None and (args.native.suffix != ".dll" or not validator.is_file() or
                                  validator.name not in ("MixedAtmospheres.GameMonoFileProtocolValidation.dll",
                                                         "GameMonoFileProtocolValidation.dll")):
        parser.error("Supply the built Windows private GameMono file protocol validation DLL")
    managed = args.managed_dir.resolve()
    files = [managed / "MixedAtmospheres.Oni.dll", managed / "MixedAtmospheres.Core.dll", args.native.resolve()]
    for file in files:
        if not file.is_file():
            parser.error(f"Build first; missing {file}")
    try:
        revision, dirty, status = source_state()
    except (OSError, subprocess.CalledProcessError) as error:
        parser.error(f"Cannot record the source git revision: {error}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        parser.error("Output must be empty so a previous package cannot leave stale or game-owned DLLs")
    for file in files[:2]:
        shutil.copy2(file, output / file.name)
    rid = "win-x64" if args.native.suffix == ".dll" else "linux-x64"
    if not args.no_game_contract:
        # The managed game contract for exactly these DLLs (docs/game-update-resilience.md).
        platform = "windows-x64" if rid == "win-x64" else "linux-x64"
        try:
            game_contract.generate(managed, {platform: game_contract.game_root(platform, args.game_dir)}, output / game_contract.FILE_NAME)
        except ValueError as error:
            shutil.rmtree(output, ignore_errors=True)
            parser.error(str(error))
    # Mod-owned YAML is loaded through ONI's virtual streaming-assets layer.
    # Never copy data into the licensed game installation.
    shutil.copytree(ROOT / "elements", output / "elements")
    native_directory = output / "native" / rid
    native_directory.mkdir(parents=True)
    copy_native_binary(files[2], native_directory / files[2].name)
    if coupled is not None:
        copy_native_binary(coupled.resolve(), native_directory / coupled.name)
    if observer is not None:
        copy_native_binary(observer.resolve(), native_directory / observer.name)
        third_party = output / "third-party"
        shutil.copytree(observer_support / "licenses", third_party / "licenses")
        shutil.copy2(observer_support / "third-party-sources.txt", third_party)
        shutil.copy2(ROOT / "tools/patch_funchook.py", third_party)
        shutil.copy2(ROOT / "tools/patch_funchook_unix.py", third_party)
        shutil.copy2(ROOT / "tools/patch_distorm.py", third_party)
        shutil.copy2(ROOT / "cmake/OniNativeBackend.cmake", third_party)
        with tarfile.open(third_party / "funchook-1.1.3-with-distorm.tar.gz", "w:gz") as archive:
            archive.add(observer_source, arcname="funchook-1.1.3", recursive=True)
        (output / "native-backend-observation.json").write_text(
            json.dumps({"SchemaVersion": 1, "Enabled": True}) + "\n", encoding="utf-8")
    if validator is not None:
        private_directory = output / "private-validation"
        private_directory.mkdir()
        shutil.copy2(validator.resolve(), private_directory / "GameMonoFileProtocolValidation.dll")
    platform_name = "Windows x64" if rid == "win-x64" else "Linux"
    (output / "mod.yaml").write_text(mod_identity.mod_yaml(
        f"Private base-game and Spaced Out! playtest build ({platform_name}). The mod's own solver runs every gas and "
        "liquid of a new colony; existing saves keep ONI's simulation."), encoding="utf-8")
    (output / "mod_info.yaml").write_text(mod_identity.mod_info(mod_identity.version() + "-probe"), encoding="utf-8")
    # Development packages keep the reports and licensed-data exports the
    # harnesses read beside the mod; release packages never carry this file.
    (output / "development.json").write_text(json.dumps(
        {"SchemaVersion": 1, "Purpose": "Private development package: reports and licensed-data exports are written beside the mod."}) + "\n",
        encoding="utf-8")
    (output / "README.txt").write_text(
        f"Mixed Atmospheres - private base-game and Spaced Out! playtest build ({platform_name})\n\n"
        f"Place this complete folder under ONI's mods/Dev folder as {mod_identity.DEV_FOLDER}, enable\n"
        "Mixed Atmospheres in Mods and restart. Start a NEW base-game or Spaced Out! colony;\n"
        "Spaced Out! can remain enabled. Every gas and liquid in its worlds belongs to the mod's solver.\n"
        "Existing colonies and saves made without the mod keep ONI's own simulation; saves\n"
        "made with it need the mod to load. Keep only one Mixed Atmospheres copy under mods:\n"
        "a second copy refuses to run and names the folder to remove. Native libraries must remain under\n"
        "native/<platform>; the root DLLs are managed. This development package writes\n"
        "integration-probe.json and, at the main menu, game-contract-local beside the mod for\n"
        "local interface tests: never redistribute those files.\n"
        "Not a release; see the repository README for status and limits.\n",
        encoding="utf-8")
    if build_info is not None:
        (output / "native-build-provenance.json").write_text(json.dumps(build_info, indent=2) + "\n", encoding="utf-8")
    manifest = {"kind": "integration-probe", "release_ready": False, "inspected_oni_build": args.oni_build,
                "source_revision": revision, "source_dirty": dirty, "source_status": status,
                "native_observer_enabled": observer is not None,
                "native_build_provenance": "native-build-provenance.json" if build_info is not None else None,
                "files": {f.relative_to(output).as_posix(): hashlib.sha256(f.read_bytes()).hexdigest()
                          for f in sorted(output.rglob("*")) if f.is_file()}}
    (output / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
