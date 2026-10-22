#!/usr/bin/env python3
"""Package an explicitly labelled developer probe, never a release overhaul."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
from build_linux_compat import glibc_versions_within_limit

ROOT = Path(__file__).resolve().parents[1]


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
    # Mod-owned YAML is loaded through ONI's virtual streaming-assets layer.
    # Never copy data into the licensed game installation.
    shutil.copytree(ROOT / "elements", output / "elements")
    rid = "win-x64" if args.native.suffix == ".dll" else "linux-x64"
    native_directory = output / "native" / rid
    native_directory.mkdir(parents=True)
    shutil.copy2(files[2], native_directory / files[2].name)
    if coupled is not None:
        shutil.copy2(coupled.resolve(), native_directory / coupled.name)
    if observer is not None:
        shutil.copy2(observer.resolve(), native_directory / observer.name)
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
    (output / "mod.yaml").write_text(
        'title: "Mixed Atmospheres"\n'
        'description: "Private base-game playtest build (Linux). The mod\'s own solver runs every gas and liquid of a new colony; existing saves keep ONI\'s simulation."\n'
        'staticID: "MixedAtmospheres.IntegrationProbe"\n', encoding="utf-8")
    (output / "mod_info.yaml").write_text(
        f'minimumSupportedBuild: {args.oni_build}\nversion: "0.1.0-probe"\nAPIVersion: 2\n', encoding="utf-8")
    (output / "README.txt").write_text(
        "Mixed Atmospheres - private base-game playtest build (Linux)\n\n"
        "Place this complete folder under ONI's mods/Dev folder as MixedAtmospheres, enable\n"
        "Mixed Atmospheres in Mods and restart. A new base-game colony (Spaced Out! off) is\n"
        "then simulated by the mod: every gas and liquid of the asteroid belongs to its solver.\n"
        "Existing colonies and saves made without the mod keep ONI's own simulation; saves\n"
        "made with it need the mod to load. Keep only one folder with staticID\n"
        "MixedAtmospheres.IntegrationProbe in mods/Dev. Native libraries must remain under\n"
        "native/<platform>; the root DLLs are managed. At the main menu the mod exports\n"
        "game-contract-local for local interface tests: never redistribute those files.\n"
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
