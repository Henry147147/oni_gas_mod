#!/usr/bin/env python3
"""Package an explicitly labelled developer probe, never a release overhaul."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def source_state() -> tuple[str, bool, list[str]]:
    """Return the source tree revision, dirty flag and short status at packaging time.

    This records the tree the package was assembled from; it does not prove the
    copied binaries were rebuilt from that tree.
    """
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    status = subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=normal"],
                                     cwd=ROOT, text=True).splitlines()
    return revision, bool(status), status


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True, help="libmixed_atmospheres.so or mixed_atmospheres.dll")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--coupled-reference", type=Path, help="Optional explicit closed-reference library for private presentation validation")
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
    if validator is not None and (args.native.suffix != ".dll" or not validator.is_file() or
                                  validator.name not in ("MixedAtmospheres.GameMonoFileProtocolValidation.dll",
                                                         "GameMonoFileProtocolValidation.dll")):
        parser.error("Supply the built Windows private GameMono file protocol validation DLL")
    managed = ROOT / "managed/MixedAtmospheres.Oni/bin/Release/netstandard2.1"
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
    if validator is not None:
        private_directory = output / "private-validation"
        private_directory.mkdir()
        shutil.copy2(validator.resolve(), private_directory / "GameMonoFileProtocolValidation.dll")
    (output / "mod.yaml").write_text(
        'title: "Mixed Atmospheres - Integration Probe"\n'
        'description: "Developer probe only. Validates native loading and scoped simulation hooks; does not change colony physics."\n'
        'staticID: "MixedAtmospheres.IntegrationProbe"\n', encoding="utf-8")
    (output / "mod_info.yaml").write_text(
        f'minimumSupportedBuild: {args.oni_build}\nversion: "0.1.0-probe"\nAPIVersion: 2\n', encoding="utf-8")
    (output / "README.txt").write_text(
        "Mixed Atmospheres - developer integration probe\n\n"
        "This is NOT the playable gas/liquid overhaul. It leaves colony physics unchanged.\n"
        "Place this complete folder under ONI's mods/Dev folder. Enable the probe,\n"
        "restart to the main menu, and inspect integration-probe.json and Player.log.\n"
        "Keep only one folder with staticID MixedAtmospheres.IntegrationProbe in mods/Dev;\n"
        "duplicate copies can cause ONI to load an older probe instead.\n"
        "Native libraries must remain under native/<platform>; the root DLLs are managed.\n"
        "At the main menu this private probe also exports game-contract-local.\n"
        "Those game-derived files are for local interface tests only: never redistribute them.\n"
        "For colony checks, use tools/run_colony_validation.ps1 from the source\n"
        "workspace. It creates a separate disposable colony and restores settings.\n"
        "Never use an existing personal colony for development validation.\n"
        "A successful report proves only its listed scoped checks, not\n"
        "authoritative colony simulation replacement.\n",
        encoding="utf-8")
    manifest = {"kind": "integration-probe", "release_ready": False, "inspected_oni_build": args.oni_build,
                "source_revision": revision, "source_dirty": dirty, "source_status": status,
                "files": {f.relative_to(output).as_posix(): hashlib.sha256(f.read_bytes()).hexdigest()
                          for f in sorted(output.rglob("*")) if f.is_file()}}
    (output / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
