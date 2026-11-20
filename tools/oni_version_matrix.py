#!/usr/bin/env python3
"""Load the same base-game colony in every registered Linux and Windows player."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import game_contract
import mod_identity
from oni_dlc_matrix import verify_case
from oni_native_matrix import digest, invoke


def verify_reload(result: dict, target: str, build: int, save_hash: str) -> None:
    verify_case(result, target, mask=0, loading=True, menu=True)
    if result["integration_probe"].get("RunningOniBuild") != build:
        raise ValueError(f"The player reported a different game build from {build}")
    if (result.get("load_save_fixture") or {}).get("sha256") != save_hash:
        raise ValueError("The player loaded a different source save")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True, type=Path, help="Complete two-platform distribution folder")
    parser.add_argument("--save", required=True, type=Path, help="Base-game mod save from the oldest supported game")
    parser.add_argument("--output", required=True, type=Path, help="Fresh directory for private game evidence")
    parser.add_argument("--linux-game", type=Path, help="Newest Linux installation")
    parser.add_argument("--windows-game", type=Path, help="Newest Windows installation")
    parser.add_argument("--proton", required=True, type=Path, help="Installed Proton tool root")
    parser.add_argument("--gpu-display", default=":99")
    args = parser.parse_args()

    tools = Path(__file__).resolve().parent
    package, save = args.package.resolve(), args.save.resolve()
    save_hash = digest(save)
    roots = {"linux": game_contract.game_root("linux-x64", args.linux_game),
             "proton": game_contract.game_root("windows-x64", args.windows_game)}
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    failures = []
    for build in sorted(mod_identity.GAME_BUILDS):
        for target, current in roots.items():
            label = f"{build}-{target}"
            run = output / label
            game = mod_identity.reference_installation(current, build)
            if target == "linux":
                runner = [str(tools / "oni_headless.py"), "run"]
            else:
                runner = [str(tools / "oni_proton_headless.py"), "--proton", str(args.proton.resolve())]
            command = [str(tools / "game_slot.sh"), "low", sys.executable, *runner,
                       "--game-dir", str(game), "--package", str(package), "--mode", "colony-owner-load",
                       "--load-save", str(save), "--dlc-mask", "0", "--frames", "40", "--save-colony",
                       "--reload-in-process", "--gpu-display", args.gpu_display, "--timeout", "1200", "--output", str(run)]
            try:
                if invoke(command, output / (label + ".log"), timeout=None):
                    raise ValueError("The game harness failed")
                verify_reload(json.loads((run / "result.json").read_text()), target, build, save_hash)
            except (OSError, ValueError) as error:
                failures.append(label)
                print(f"{label} failed: {error}", file=sys.stderr, flush=True)
            else:
                print(f"{label} passed", flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
