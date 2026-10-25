#!/usr/bin/env python3
"""Run the inspected native interface in bounded, disposable child processes.

Linux counterpart to run.ps1. Game libraries and exported tables stay external.
No Unity/Steam process is launched, modified, attached to, or terminated.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import resource
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_probe_process import add_runtime_arguments, ProbeRuntime

MODES = ("baseline", "multi-region", "fluid-actors", "rebuild-shadow", "load-shadow", "transition", "filtered-transition", "sanitize", "reject-custom-envelope")
DOMAIN_MODES = ("sealed-gas", "sealed-water-small-heat", "sealed-water-heat", "sealed-water-transfer", "sealed-water", "sealed-heat", "sealed-conduction", "sealed-gravity", "sealed-water-gravity", "sealed-checkpoint", "sealed-pair", "sealed-restore", "sealed-restore-water", "sealed-restore-gravity", "sealed-restore-water-gravity")
EXPERIMENT_MODES = ("zero-time-frame","native-disease-retention","floor-f4","colony-gas-hooks","colony-water-hooks","colony-gas-boundary-hooks","colony-gas-density-hooks","colony-gas-lateral-hooks","colony-gas-transition-hooks")


def no_core_dump() -> None:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New or empty evidence directory")
    parser.add_argument("--probe", type=Path, default=Path(__file__).parent / "bin/Release/net10.0/NativeShadowProbe.dll")
    add_runtime_arguments(parser)
    parser.add_argument("--observer", type=Path)
    parser.add_argument("--game", type=Path)
    parser.add_argument("--floor-request", type=Path, help="floor-f4: exact private MAFLRQ01 input proved in Unity")
    parser.add_argument("--mode", choices=(*MODES, *DOMAIN_MODES, *EXPERIMENT_MODES, "all", "domains"), default="all")
    args = parser.parse_args()
    if not sys.platform.startswith("linux"):
        parser.error("Use run.ps1 on Windows")
    runtime = ProbeRuntime(args, parser)
    if args.mode == "floor-f4" and (args.observer or args.game or not args.floor_request or not args.floor_request.is_file()):
        parser.error("floor-f4 requires --floor-request and must run without an observer/game owner")
    if args.floor_request and args.mode != "floor-f4":
        parser.error("--floor-request is only valid for floor-f4")
    if bool(args.observer) != bool(args.game) or (args.mode in (*DOMAIN_MODES, "zero-time-frame", "colony-gas-hooks", "colony-water-hooks", "colony-gas-boundary-hooks","colony-gas-density-hooks","colony-gas-lateral-hooks","colony-gas-transition-hooks", "domains") and not args.observer):
        parser.error("Sealed domains, colony hooks and zero-time controls require both --observer and --game")
    if args.observer and not args.observer.is_file(): parser.error("Native observer library is missing")
    for path in (args.native, args.probe, *(args.contract / name for name in ("contract.json", "elements.bin", "diseases.bin"))):
        if not path.is_file():
            parser.error(f"Required input missing: {path}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        parser.error("Evidence directory must be empty; previous results are preserved")
    summary = {"kind": "isolated-native-contract-suite", "authoritativeColonySimulationReady": False, "results": []}
    summary.update(runtime.metadata())
    modes = MODES if args.mode == "all" else DOMAIN_MODES if args.mode == "domains" else (args.mode,)
    for mode in modes:
        report_path = output / f"{mode}.json"
        parameters = [args.native, args.contract, mode, report_path]
        if args.observer: parameters += [args.observer, args.game]
        command = runtime.command(args.probe, *parameters)
        item = {"mode": mode, "expectedNegativeControl": mode in ("sanitize", "reject-custom-envelope"), "passed": False}
        try:
            with (output / f"{mode}.log").open("w") as log:
                environment = runtime.environment()
                if args.floor_request:
                    environment = dict(os.environ if environment is None else environment)
                    environment["MA_NATIVE_FLOOR_REQUEST"] = str(args.floor_request.resolve())
                child = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=240 if mode == "floor-f4" else 45,
                                       preexec_fn=no_core_dump, check=False, env=environment)
            item["exitCode"] = child.returncode
            report = json.loads(report_path.read_text()) if report_path.is_file() else {}
            if mode == "sanitize":
                observed = report.get("afterSanitationWithoutFrame", {})
                item["passed"] = (child.returncode == 1 and report.get("passed") is False
                                  and report.get("sanitationCommittedBeforeFrame") is False
                                  and observed.get("FramesProcessed") == 0
                                  and observed.get("FluidCells") == 2
                                  and observed.get("FluidMass") == 102
                                  and "Sanitation did not commit before a native frame." in report.get("error", ""))
            else:
                item["passed"] = child.returncode == 0 and report.get("passed") is True
            if not item["passed"]:
                item["error"] = report.get("error", "Missing success report; inspect child log")
        except (subprocess.TimeoutExpired, OSError, ValueError) as error:
            item["error"] = str(error)
        summary["results"].append(item)
        print(json.dumps(item), flush=True)
    summary["passed"] = all(item["passed"] for item in summary["results"])
    (output / "suite.json").write_text(json.dumps(summary, indent=2) + "\n")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
