#!/usr/bin/env python3
"""Assemble the Steam Workshop release folder and its deterministic ZIP.

One folder serves Windows x64 and Linux x64 players: managed DLLs, element
data, both platforms' native libraries, third-party notices and sources, the
final mod.yaml/mod_info.yaml, a player README and a manifest of hashes. It has
no installer, no development marker, no validation fixtures and no reports.
Licensed game files never enter it. See docs/distribution.md.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import shutil
import struct
import sys
import tarfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_contract  # noqa: E402
import mod_identity  # noqa: E402
import package_probe  # noqa: E402

ROOT = mod_identity.ROOT
# Fixed archive metadata: identical inputs give identical bytes.
ZIP_DATE = (2026, 1, 1, 0, 0, 0)
TAR_MTIME = 1767225600  # 2026-01-01T00:00:00Z
MANAGED = ("MixedAtmospheres.Oni.dll", "MixedAtmospheres.Core.dll")
NATIVE = {"linux-x64": ("libmixed_atmospheres.so", "libmixed_atmospheres_oni.so"),
          "win-x64": ("mixed_atmospheres.dll", "mixed_atmospheres_oni.dll")}
# Files that must never reach a release (development, validation or reports).
FORBIDDEN = {"development.json", "colony-validation.json", "integration-probe.json", "native-backend-observation.json",
             "native-backend-observation-report.json", "install_windows.ps1", "INSTALL.txt"}
FORBIDDEN_PARTS = {"game-contract-local", "private-validation"}
DESCRIPTION = ("Real gas and liquid mixtures: a new base-game or Spaced Out! colony's gases and liquids are simulated "
               "by the mod's own solver. Windows x64 and Linux x64.")


def _rva_offset(sections: list[tuple[int, int, int, int]], rva: int) -> int:
    for address, virtual_size, raw_offset, raw_size in sections:
        if address <= rva < address + max(virtual_size, raw_size):
            return raw_offset + rva - address
    raise ValueError("RVA outside the image's sections")


def normalized_managed_image(data: bytes) -> bytes:
    """A managed DLL with only its deterministic build identity cleared.

    Deterministic builds derive the COFF timestamp, the module version ID
    (#GUID heap) and the debug directory (CodeView ID, PDB checksum) from
    every input, including the game assemblies referenced. Everything else
    (IL, metadata, resources) is compared as is.
    """
    image = bytearray(data)
    if len(image) < 64 or image[:2] != b"MZ":
        raise ValueError("Not a managed PE image")
    pe = struct.unpack_from("<I", image, 60)[0]
    if image[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Not a managed PE image")
    count, = struct.unpack_from("<H", image, pe + 6)
    optional_size, = struct.unpack_from("<H", image, pe + 20)
    optional = pe + 24
    magic, = struct.unpack_from("<H", image, optional)
    directories = optional + (96 if magic == 0x10B else 112 if magic == 0x20B else -1)
    if directories < optional:
        raise ValueError("Unknown PE optional header")
    struct.pack_into("<I", image, pe + 8, 0)          # COFF TimeDateStamp
    struct.pack_into("<I", image, optional + 64, 0)   # CheckSum
    sections = []
    for index in range(count):
        at = optional + optional_size + 40 * index
        virtual_size, address, raw_size, raw_offset = struct.unpack_from("<IIII", image, at + 8)
        sections.append((address, virtual_size, raw_offset, raw_size))
    debug_rva, debug_size = struct.unpack_from("<II", image, directories + 6 * 8)
    if debug_rva:
        start = _rva_offset(sections, debug_rva)
        for entry in range(start, start + debug_size, 28):
            struct.pack_into("<I", image, entry + 4, 0)  # TimeDateStamp
            size, _, raw = struct.unpack_from("<III", image, entry + 16)
            image[raw:raw + size] = bytes(size)
    clr_rva, _ = struct.unpack_from("<II", image, directories + 14 * 8)
    if not clr_rva:
        raise ValueError("Not a managed image (no CLI header)")
    clr = _rva_offset(sections, clr_rva)
    metadata = _rva_offset(sections, struct.unpack_from("<I", image, clr + 8)[0])
    if struct.unpack_from("<I", image, metadata)[0] != 0x424A5342:
        raise ValueError("Missing CLI metadata root")
    length, = struct.unpack_from("<I", image, metadata + 12)
    at = metadata + 16 + length + 2
    streams, = struct.unpack_from("<H", image, at)
    at += 2
    for _ in range(streams):
        offset, size = struct.unpack_from("<II", image, at)
        end = image.index(0, at + 8)
        name = bytes(image[at + 8:end]).decode("ascii")
        at = (end + 4) & ~3
        if name == "#GUID":
            image[metadata + offset:metadata + offset + size] = bytes(size)
    return bytes(image)


def managed_equivalence(windows: Path, linux: Path) -> dict:
    """Builds against the Windows and Linux game assemblies must agree byte for
    byte apart from their build identity, so one managed build serves both."""
    result = {}
    for name in MANAGED:
        left, right = (windows / name).read_bytes(), (linux / name).read_bytes()
        if normalized_managed_image(left) != normalized_managed_image(right):
            raise ValueError(f"{name} differs between the Windows- and Linux-referenced builds beyond its build identity")
        result[name] = {"windows_sha256": hashlib.sha256(left).hexdigest(), "linux_sha256": hashlib.sha256(right).hexdigest()}
    return result


def deterministic_tar_gz(source: Path, arcname: str) -> bytes:
    """A source archive whose bytes depend only on the files' paths and contents."""
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.PAX_FORMAT) as archive:
        for path in [source, *sorted(source.rglob("*"), key=lambda p: p.relative_to(source).as_posix())]:
            if path.is_symlink():
                raise ValueError("Source archives must not contain symlinks: " + str(path))
            name = arcname if path == source else arcname + "/" + path.relative_to(source).as_posix()
            info = tarfile.TarInfo(name)
            info.mtime, info.uid, info.gid, info.uname, info.gname = TAR_MTIME, 0, 0, "", ""
            if path.is_dir():
                info.type, info.mode = tarfile.DIRTYPE, 0o755
                archive.addfile(info)
            else:
                data = path.read_bytes()
                info.size, info.mode = len(data), 0o644
                archive.addfile(info, io.BytesIO(data))
    output = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0, compresslevel=9) as compressed:
        compressed.write(buffer.getvalue())
    return output.getvalue()


def third_party(roots: dict[str, Path], destination: Path) -> None:
    """Every native dependency's licence and source pointer, from both builds."""
    licences: dict[str, bytes] = {}
    lines: list[str] = []
    for platform, root in roots.items():
        generated = root / "oni-generated"
        for file in sorted((generated / "licenses").rglob("*")):
            if file.is_file():
                key = file.relative_to(generated / "licenses").as_posix()
                data = file.read_bytes()
                if licences.setdefault(key, data) != data:
                    raise ValueError(f"Licence {key} differs between the native builds")
        for line in (generated / "third-party-sources.txt").read_text(encoding="utf-8").splitlines():
            if line not in lines:
                lines.append(line)
    for key, data in licences.items():
        (destination / "licenses" / key).parent.mkdir(parents=True, exist_ok=True)
        (destination / "licenses" / key).write_bytes(data)
    (destination / "third-party-sources.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    for script in ("tools/patch_funchook.py", "tools/patch_funchook_unix.py", "tools/patch_distorm.py", "cmake/OniNativeBackend.cmake"):
        shutil.copyfile(ROOT / script, destination / Path(script).name)
    archives = {deterministic_tar_gz(root / "_deps/ma_funchook-src", "funchook-1.1.3") for root in roots.values()}
    if len(archives) != 1:
        raise ValueError("The patched funchook sources differ between the native builds")
    (destination / "funchook-1.1.3-with-distorm.tar.gz").write_bytes(archives.pop())


# The Windows DLLs link the MinGW-w64 C runtime, winpthreads and GCC's
# libgcc/libstdc++ statically; their notices come from the toolchain's packages.
MINGW_NOTICES = {"mingw-w64-runtime/copyright": "share/doc/mingw-w64-x86-64-dev/copyright",
                 "gcc-runtime/copyright": "share/doc/gcc-mingw-w64-base/copyright"}
MINGW_SOURCES = ("MinGW-w64 runtime and winpthreads (Windows only, linked statically): https://www.mingw-w64.org/; "
                 "notices in licenses/mingw-w64-runtime.",
                 "GCC 13 libgcc and libstdc++ (Windows only, linked statically under the GCC Runtime Library Exception): "
                 "https://gcc.gnu.org/; notices in licenses/gcc-runtime.")


def default_mingw_root() -> Path:
    """The team's MinGW toolchain, beside the main checkout (docs/windows-validation.md)."""
    import os
    import subprocess
    if os.environ.get("MA_MINGW_ROOT"):
        return Path(os.environ["MA_MINGW_ROOT"])
    common = Path(subprocess.check_output(["git", "rev-parse", "--git-common-dir"], cwd=ROOT, text=True).strip())
    return (common if common.is_absolute() else ROOT / common).resolve().parent / "build-oni-cross-tools/root/usr"


def mingw_notices(root: Path, destination: Path) -> None:
    for target, source in MINGW_NOTICES.items():
        if not (root / source).is_file():
            raise ValueError(f"MinGW runtime notice missing: {root / source}")
        (destination / "licenses" / target).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / source, destination / "licenses" / target)
    with (destination / "third-party-sources.txt").open("a", encoding="utf-8") as sources:
        sources.write("\n".join(MINGW_SOURCES) + "\n")


def player_readme(version: str) -> str:
    return f"""Mixed Atmospheres {version}
{"=" * (len("Mixed Atmospheres ") + len(version))}

Mixed Atmospheres replaces Oxygen Not Included's gas and liquid simulation with
its own solver. Gases mix instead of stacking one element per tile, liquids and
gases share cells, and pressure, heat and phase changes follow from the mixture.

Requirements
- 64-bit Windows or 64-bit Linux. macOS is not supported: the mod stays
  inactive there and the game runs as normal.
- The base game or Spaced Out!. The Frosty, Bionic, Prehistoric and Aquatic
  packs are not supported yet: while any of them is active, colonies keep ONI's
  own gases and liquids and you are told so at the main menu.

Using it
1. Enable Mixed Atmospheres in Mods and restart when ONI asks.
2. Start a NEW base-game or Spaced Out! colony. The mod takes over every gas
   and liquid in it from the first moment.
3. Colonies started without the mod keep ONI's own simulation.
4. Optional: Mods > Mixed Atmospheres > Settings. Default is recommended.

Saves
- A colony started with Mixed Atmospheres needs the mod to load. It cannot be
  returned to ONI's own simulation.
- If the mod cannot open a colony (for example after an update that changes
  something the colony depends on), it says why and leaves the save unchanged.

Game updates
Mixed Atmospheres checks every part of the game it depends on each time ONI
starts. After a Klei update that changes any of them, the mod stays inactive
and says so at the main menu; your colonies are not touched until a matching
Mixed Atmospheres update is available. Updates that do not touch those parts
need no mod update.

Only one copy
Keep one copy of Mixed Atmospheres under mods. If two are enabled, the second
does nothing and the main menu names the folder to remove.

Your data
Settings and presets are kept in mods/config/MixedAtmospheres in your ONI
user folder, so updating or reinstalling the mod keeps them. The mod writes
nothing into its own folder.

Reporting a problem
Send Player.log, found at:
- Windows: %USERPROFILE%\\AppData\\LocalLow\\Klei\\Oxygen Not Included\\Player.log
- Linux: ~/.config/unity3d/Klei/Oxygen Not Included/Player.log

Third-party components and their licences are in the third-party folder.
"""


def assemble(args: argparse.Namespace) -> dict:
    version = mod_identity.version()
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError("Output must be a new or empty folder")
    roots: dict[str, Path] = {}
    natives: dict[str, list[Path]] = {}
    for rid, directory in (("linux-x64", args.linux_native), ("win-x64", args.windows_native)):
        files = [directory.resolve() / name for name in NATIVE[rid]]
        for file in files:
            if not file.is_file():
                raise ValueError(f"Missing native library {file}")
        roots[rid] = package_probe.observer_build_root(files[1])
        natives[rid] = files
    build_info = package_probe.native_build_info(args.linux_build_info, natives["linux-x64"])
    equivalence = managed_equivalence(args.managed_windows.resolve(), args.managed_linux.resolve())
    revision, dirty, status = package_probe.source_state()
    if dirty and not args.allow_dirty:
        raise ValueError("The source tree has uncommitted changes; commit them or pass --allow-dirty (recorded)")
    output.mkdir(parents=True, exist_ok=True)
    try:
        for name in MANAGED:
            shutil.copyfile(args.managed_linux.resolve() / name, output / name)
        # The shipped DLLs' managed game contract for both platforms' games.
        game_contract.generate(args.managed_linux.resolve(), {
            "windows-x64": game_contract.game_root("windows-x64", args.windows_game),
            "linux-x64": game_contract.game_root("linux-x64", args.linux_game)}, output / game_contract.FILE_NAME)
        shutil.copytree(ROOT / "elements", output / "elements")
        for rid, files in natives.items():
            (output / "native" / rid).mkdir(parents=True)
            for file in files:
                package_probe.copy_native_binary(file, output / "native" / rid / file.name)
        third_party(roots, output / "third-party")
        mingw_notices((args.mingw_root or default_mingw_root()).resolve(), output / "third-party")
        if build_info is not None:
            (output / "native-build-provenance.json").write_text(json.dumps(build_info, indent=2) + "\n", encoding="utf-8")
        if args.preview is not None:
            data = args.preview.read_bytes()
            if not data.startswith(b"\x89PNG\r\n\x1a\n"):
                raise ValueError("The preview must be a PNG image")
            (output / "preview.png").write_bytes(data)
        (output / "mod.yaml").write_text(mod_identity.mod_yaml(DESCRIPTION), encoding="utf-8")
        (output / "mod_info.yaml").write_text(mod_identity.mod_info(version), encoding="utf-8")
        (output / "README.txt").write_text(player_readme(version), encoding="utf-8")
        files = sorted(p for p in output.rglob("*") if p.is_file())
        check_release_contents(output, files)
        manifest = {
            "kind": "release", "release_ready": True, "static_id": mod_identity.STATIC_ID, "version": version,
            "minimum_supported_build": mod_identity.MINIMUM_SUPPORTED_BUILD, "platforms": sorted(NATIVE),
            "native_observer_enabled": True, "source_revision": revision, "source_dirty": dirty, "source_status": status,
            "managed_builds": equivalence,
            "native_build_provenance": "native-build-provenance.json" if build_info is not None else None,
            "files": {p.relative_to(output).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
        (output / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    except Exception:
        shutil.rmtree(output, ignore_errors=True)
        raise
    return manifest


def check_release_contents(output: Path, files: list[Path]) -> None:
    for file in files:
        relative = PurePosixPath(file.relative_to(output).as_posix())
        if file.is_symlink() or relative.name in FORBIDDEN or FORBIDDEN_PARTS.intersection(relative.parts):
            raise ValueError(f"A release must not contain {relative}")
        if relative.suffix == ".dll" and len(relative.parts) == 1 and relative.name not in MANAGED:
            raise ValueError(f"Unexpected root DLL {relative}: ONI loads every root DLL as managed code")
        if "coupled" in relative.name:
            raise ValueError(f"Validation-only library {relative} must not be released")


def write_zip(folder: Path, destination: Path) -> str:
    """Deterministic ZIP whose single top folder is the Workshop folder."""
    manifest = json.loads((folder / "package-manifest.json").read_text(encoding="utf-8"))
    names = sorted([*manifest["files"], "package-manifest.json"])
    if {p.relative_to(folder).as_posix() for p in folder.rglob("*") if p.is_file()} != set(names):
        raise ValueError("The release folder changed after its manifest was written")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, "x", zipfile.ZIP_DEFLATED) as archive:
        for name in names:
            data = (folder / name).read_bytes()
            if name in manifest["files"] and hashlib.sha256(data).hexdigest() != manifest["files"][name]:
                raise ValueError("Release file changed after its manifest was written: " + name)
            info = zipfile.ZipInfo(mod_identity.STATIC_ID + "/" + name, ZIP_DATE)
            info.compress_type, info.external_attr, info.create_system = zipfile.ZIP_DEFLATED, 0o100644 << 16, 3
            archive.writestr(info, data, compresslevel=9)
    with zipfile.ZipFile(destination) as archive:
        if archive.testzip() is not None:
            raise ValueError("ZIP verification failed")
    return hashlib.sha256(destination.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--managed-linux", type=Path, required=True, help="Managed build directory referencing the Linux game assemblies")
    parser.add_argument("--managed-windows", type=Path, required=True, help="Managed build directory referencing the Windows game assemblies")
    parser.add_argument("--linux-native", type=Path, required=True, help="Linux compatibility build's native directory (both .so files)")
    parser.add_argument("--linux-build-info", type=Path, required=True, help="Its compatibility-build.json (glibc ceiling)")
    parser.add_argument("--windows-native", type=Path, required=True, help="Windows cross-build directory (both DLLs)")
    parser.add_argument("--windows-game", type=Path, help="Windows ONI installation for the game contract (default: the team's copy)")
    parser.add_argument("--linux-game", type=Path, help="Linux ONI installation for the game contract (default: the team's copy)")
    parser.add_argument("--mingw-root", type=Path, help="MinGW toolchain root (usr) for the static runtime notices (default: MA_MINGW_ROOT or the team's)")
    parser.add_argument("--preview", type=Path, help="Optional preview.png for the Mods list and Workshop page")
    parser.add_argument("--output", type=Path, required=True, help="New Workshop-shaped folder")
    parser.add_argument("--zip", type=Path, help="Fresh deterministic ZIP of the folder")
    parser.add_argument("--allow-dirty", action="store_true", help="Package uncommitted sources (recorded in the manifest)")
    args = parser.parse_args()
    try:
        assemble(args)
        digest = write_zip(args.output.resolve(), args.zip) if args.zip else None
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(args.output.resolve())
    if digest:
        print(digest, args.zip)


if __name__ == "__main__":
    main()
