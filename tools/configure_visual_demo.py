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
    # Configured 7x5 rooms with an interior Neutronium pillar and ledge. Their
    # natural autosaves use the paired configured save, so long runs admit them.
    "pillared": "native-configured-domain",
    "pillared-water": "native-configured-water-domain",
}
LONG_RUN_SCENES = ("air", "water", "mixed", "pillared", "pillared-water")
# Mirrors ColonyValidationLimits; ten ONI cycles are about 30,000 native frames.
FRAMES, LONG_RUN_FRAMES = (120, 10000), (30000, 100000)
TIMEOUT_SECONDS, LONG_RUN_TIMEOUT_SECONDS = 7200, 48 * 3600
TELEMETRY_INTERVAL_FRAMES = 300


def configuration(scene: str, long_run: bool = False, frames: int | None = None) -> dict:
    """The colony-validation.json request; the short scene keeps its original keys."""
    if long_run and scene not in LONG_RUN_SCENES:
        raise ValueError("--long-run admits only the " + ", ".join(LONG_RUN_SCENES) + " scenes")
    default, limit = LONG_RUN_FRAMES if long_run else FRAMES
    frames = default if frames is None else frames
    if type(frames) is not int or not 1 <= frames <= limit:
        raise ValueError(f"--frames requires 1..{limit}" + ("" if long_run else f"; up to {LONG_RUN_FRAMES[1]} requires --long-run"))
    value = {
        "Mode": SCENES[scene],
        "AutoStartNewColony": False,
        "QuitWhenComplete": False,
        "SaveOnComplete": False,
        "CompletedFrames": frames,
        "TimeoutSeconds": LONG_RUN_TIMEOUT_SECONDS if long_run else TIMEOUT_SECONDS,
        "Seed": "147147",
        "ColonyName": "Mixed Atmospheres visual demo",
    }
    if long_run:
        value.update(LongRun=True, TelemetryIntervalFrames=TELEMETRY_INTERVAL_FRAMES)
    return value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--scene", choices=SCENES, default="air")
    parser.add_argument("--long-run", action="store_true",
                        help="Keep the scene running for ten ONI cycles (30,000 frames) under a 48-hour limit and write development telemetry")
    parser.add_argument("--frames", type=int, help="Completed native frames before the scene pauses (default 120, or 30,000 with --long-run)")
    args = parser.parse_args()
    try:
        value = configuration(args.scene, args.long_run, args.frames)
    except ValueError as error:
        parser.error(str(error))
    package = args.package.resolve()
    if not (package / "MixedAtmospheres.Oni.dll").is_file():
        parser.error("Build and package the mod first; the managed adapter is missing.")
    observer = package / "native-backend-observation.json"
    if not observer.is_file() or json.loads(observer.read_text(encoding="utf-8")).get("Enabled") is not True:
        parser.error("Package with --native-observer first; the visual scene requires its native backend.")
    destination = package / "colony-validation.json"
    if destination.exists():
        parser.error("colony-validation.json already exists; move it aside before selecting another scene.")
    with destination.open("x", encoding="utf-8") as output:
        json.dump(value, output, indent=2)
        output.write("\n")
    print(f"Prepared {args.scene} scene: {destination}")
    print("Enable the mod, restart ONI and manually start a disposable new colony.")
    print(f"The scene pauses after {value['CompletedFrames']} completed frames. Shift+F9 centres the fluid region.")
    if args.long_run:
        print("Development telemetry: game-contract-local/colony-validation/development-telemetry.jsonl in the mod folder.")


if __name__ == "__main__":
    main()
