#!/usr/bin/env python3
"""Full-colony benchmark saves (docs/performance-acceptance.md#full-colony-benchmarks).

`build` runs the `full-colony-base` harness scenario on the pinned Terra world
and keeps the built colony's save. `settle` loads that save and plays it at game
speed 3 for several cycles with ordinary natural autosaves, then saves the end
state. `publish` copies a settle run's final save to local/saves/<name>.sav and
writes <name>.json beside it: SHA-256, cycle, counts, revision and the windows
the run measured (diagnostics, not acceptance numbers).

Run `build` and `settle` through the game slot, e.g.
  tools/game_slot.sh low tools/full_colony_benchmark.py build --package PKG --output DIR
The package comes from tools/package_probe.py, as for the other harness runs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADLESS = ROOT / "tools" / "oni_headless.py"
VALIDATION = Path("home/.config/unity3d/Klei/Oxygen Not Included/mods/Dev/MixedAtmospheres.IntegrationProbe/game-contract-local/colony-validation")
WINDOW = re.compile(r"perf frames (\d+)-(\d+) session \d+ partial (\d): owner_ms mean ([\d.]+) p95 ([\d.]+) max ([\d.]+) "
                    r"stages_ms .*? interval_ms mean ([\d.]+) p95 ([\d.]+) max ([\d.]+) .*? owner_seconds ([\d.]+) game_seconds ([\d.]+) "
                    r"wall_seconds ([\d.]+) owner_per_real ([\d.]+) game_per_real ([\d.]+)")
RUN = re.compile(r"perf run session \d+ frames (\d+)-(\d+) reason (\w+) owner_seconds ([\d.]+) game_seconds ([\d.]+) "
                 r"wall_seconds ([\d.]+) owner_per_real ([\d.]+) game_per_real ([\d.]+)")
CYCLE_FRAMES = 3000


def common(args: argparse.Namespace) -> list[str]:
    command = [sys.executable, str(HEADLESS), "run", "--package", str(args.package), "--dlc-mask", str(args.dlc_mask),
               "--gpu-display", args.gpu_display, "--output", str(args.output.resolve())]
    return command + (["--game-dir", str(args.game_dir)] if args.game_dir else [])


def build(args: argparse.Namespace) -> int:
    """The scenario builds the colony and runs it for 420 frames; --save-colony keeps it."""
    command = common(args) + ["--mode", "colony-owner", "--scenario", args.scenario, "--save-colony", "--story-traits", "V9",
                              "--mixing", "0", "--speed", "3", "--timeout", "2400"]
    return subprocess.call(command, cwd=ROOT, env={**os.environ, "MA_COLONY_DUPE_LOG": "1", "MA_COLONY_CENSUS": "1"})


def settle(args: argparse.Namespace) -> int:
    """Natural autosaves happen at each day rollover; 3,000 frames are one cycle. A run of at most 10,000 frames
    (three cycles and a margin) is not a --long-run, which would exclude the final save."""
    frames = args.cycles * CYCLE_FRAMES + 600
    if frames > 10000:
        raise SystemExit("settle: at most three cycles per run (10,000 frames); settle again from the saved result for more")
    command = common(args) + ["--mode", "colony-owner-load", "--load-save", str(args.save), "--frames", str(frames),
                              "--timeout", str(args.timeout), "--speed", "3", "--save-colony"]
    return subprocess.call(command, cwd=ROOT, env={**os.environ, "MA_COLONY_DUPE_LOG": "1", "MA_COLONY_LEDGER_LOG": "1", "MA_COLONY_CENSUS": "1"})


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def windows(log: str) -> list[dict]:
    rows = []
    for m in WINDOW.finditer(log):
        first, last, partial, om, op, ox, im, ip, ixx, owner_s, game_s, wall_s, owner_rate, game_rate = m.groups()
        rows.append({"frames": [int(first), int(last)], "partial": partial == "1", "owner_ms": {"mean": float(om), "p95": float(op), "max": float(ox)},
                     "interval_ms": {"mean": float(im), "p95": float(ip), "max": float(ixx)}, "game_seconds": float(game_s),
                     "wall_seconds": float(wall_s), "game_per_wall_second": float(game_rate), "owner_per_wall_second": float(owner_rate)})
    return rows


def summary(run: Path) -> dict:
    """Everything `publish` records about one settle run."""
    log = (run / "Player.log").read_text(errors="replace")
    validation = run / VALIDATION
    owner = json.loads((validation / "colony-owner.json").read_text())
    census = json.loads((validation / "colony-census.json").read_text())
    result = json.loads((run / "result.json").read_text())
    rows = windows(log)
    runs = RUN.findall(log)
    # The harness keeps the game's cycle autosaves in the run folder's auto_save/.
    autosaves = sorted(p.name for p in run.rglob("*.sav") if "auto_save" in p.parts)
    total = None
    if runs:
        first, last, reason, owner_s, game_s, wall_s, owner_rate, game_rate = runs[-1]
        total = {"frames": [int(first), int(last)], "game_seconds": float(game_s), "wall_seconds": float(wall_s), "game_per_wall_second": float(game_rate)}
    return {
        "revision": result.get("source_revision"), "revision_dirty": result.get("source_dirty"),
        "owner": {"state": owner.get("State"), "faulted": owner.get("Faulted"), "error": owner.get("Error"), "completed_frames": owner.get("CompletedFrames"),
                  "rejected_attempts": owner.get("RejectedAttempts"), "maximum_relative_mass_error": owner.get("MaximumRelativeMassError"),
                  "maximum_relative_energy_error": owner.get("MaximumRelativeEnergyError"), "owned_cells": owner.get("OwnedCells")},
        "census": census, "natural_autosaves": autosaves, "windows": rows, "run_total": total,
    }


def publish(args: argparse.Namespace) -> int:
    run = args.run.resolve()
    validation = run / VALIDATION
    report = json.loads((validation / "colony-validation.json").read_text())
    saved = Path(report["ColonySavePath"])
    destination = ROOT / "local" / "saves" / (args.name + ".sav")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(saved.read_bytes())
    data = summary(run)
    census = data["census"]
    record = {
        "name": args.name, "file": destination.name, "sha256": sha256(destination), "bytes": destination.stat().st_size,
        "world": {"cluster": args.cluster, "seed": "147147", "story_traits": "V9", "mixing": "0", "dlc_mask": args.dlc_mask},
        "cycle": census["GameCycle"] + 1, "cycle_note": "as the game shows it (GameClock.GetCycle() + 1)",
        "game_seconds": census["GameSeconds"], "owner_frame": census["Frame"],
        "duplicants": census["Duplicants"], "gas_cells": census["GasCells"], "liquid_cells": census["LiquidCells"],
        "active_cells": None, "active_cells_note": "the owner does not report an active-cell count; owned_cells is the grid the owner steps",
        "owned_cells": census["OwnedCells"], "buildings": census["Buildings"], "gas_pipe_cells": census["GasPipeCells"],
        "liquid_pipe_cells": census["LiquidPipeCells"], "wire_cells": census["WireCells"],
        "owned_pipe_cells_with_contents": census["OwnedPipeCellsWithContents"], "gas_kg": census["GasKg"], "liquid_kg": census["LiquidKg"],
        "revision": data["revision"], "revision_dirty": data["revision_dirty"], "owner": data["owner"],
        "natural_autosaves": data["natural_autosaves"], "run_total": data["run_total"], "windows": data["windows"],
        "note": "Window speeds are diagnostics of one run, not acceptance numbers.",
    }
    (destination.with_suffix(".json")).write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps({k: v for k, v in record.items() if k != "windows"}, indent=2))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("build", "settle"):
        p = sub.add_parser(name)
        p.add_argument("--package", type=Path, required=True)
        p.add_argument("--output", type=Path, required=True)
        p.add_argument("--dlc-mask", type=int, default=0)
        p.add_argument("--game-dir", type=Path, help="a game copy with the DLC the mask selects (default: oni_headless.py's)")
        p.add_argument("--gpu-display", default=os.environ.get("ONI_GPU_DISPLAY", ":99"))
        if name == "build":
            p.add_argument("--scenario", default="full-colony-base")
        else:
            p.add_argument("--save", type=Path, required=True)
            p.add_argument("--cycles", type=int, default=3)
            p.add_argument("--timeout", type=int, default=7200)
    p = sub.add_parser("publish")
    p.add_argument("--run", type=Path, required=True, help="the settle run's output folder")
    p.add_argument("--name", required=True, help="e.g. full-colony-base-v1")
    p.add_argument("--dlc-mask", type=int, default=0)
    p.add_argument("--cluster", default="SandstoneDefault")
    args = parser.parse_args()
    return {"build": build, "settle": settle, "publish": publish}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
