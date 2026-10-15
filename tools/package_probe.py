#!/usr/bin/env python3
"""Package an explicitly labelled developer probe, never a release overhaul."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True, help="libmixed_atmospheres.so or mixed_atmospheres.dll")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--oni-build", type=int, default=744825, help="Verified ONI build number, not Steam BuildID")
    args = parser.parse_args()
    if args.oni_build != 744825:
        parser.error("This probe was inspected against ONI 744825. Reinspect and update the contract before packaging another build.")
    if args.native.name not in ("libmixed_atmospheres.so", "mixed_atmospheres.dll") or not args.native.is_file():
        parser.error("Supply a built Linux or Windows native library")
    managed = ROOT / "managed/MixedAtmospheres.Oni/bin/Release/netstandard2.1"
    files = [managed / "MixedAtmospheres.Oni.dll", managed / "MixedAtmospheres.Core.dll", args.native.resolve()]
    for file in files:
        if not file.is_file():
            parser.error(f"Build first; missing {file}")
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
                "files": {f.relative_to(output).as_posix(): hashlib.sha256(f.read_bytes()).hexdigest()
                          for f in sorted(output.rglob("*")) if f.is_file()}}
    (output / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
