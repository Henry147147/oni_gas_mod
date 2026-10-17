#!/usr/bin/env python3
"""Prepare a packaged mod for a visible, manually started development colony."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


SCENES = {
    "air": "native-domain",
    "water": "native-liquid-domain",
    "mixed": "native-mixed-domain",
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--scene", choices=SCENES, default="air")
    args = parser.parse_args()
    package = args.package.resolve()
    if not (package / "MixedAtmospheres.Oni.dll").is_file():
        parser.error("Build and package the mod first; the managed adapter is missing.")
    observer = package / "native-backend-observation.json"
    if not observer.is_file() or json.loads(observer.read_text(encoding="utf-8")).get("Enabled") is not True:
        parser.error("Package with --native-observer first; the visual scene requires its native backend.")
    destination = package / "colony-validation.json"
    if destination.exists():
        parser.error("colony-validation.json already exists; move it aside before selecting another scene.")
    configuration = {
        "Mode": SCENES[args.scene],
        "AutoStartNewColony": False,
        "QuitWhenComplete": False,
        "SaveOnComplete": False,
        "CompletedFrames": 120,
        "TimeoutSeconds": 7200,
        "Seed": "147147",
        "ColonyName": "Mixed Atmospheres visual demo",
    }
    with destination.open("x", encoding="utf-8") as output:
        json.dump(configuration, output, indent=2)
        output.write("\n")
    print(f"Prepared {args.scene} scene: {destination}")
    print("Enable the mod, restart ONI and manually start a disposable new colony.")
    print("The scene pauses after 120 completed frames. Ctrl+F9 centres the fluid region.")


if __name__ == "__main__":
    main()
