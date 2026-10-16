#!/usr/bin/env python3
"""Compare unhooked and observed licensed SimDLL runs in bounded processes.

Uses the existing native interface harness; no Unity/Steam process is attached.
Reports deliberately keep fluid ownership false. Game files remain external.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
MODES = ("baseline", "multi-region", "transition", "filtered-transition", "load-shadow", "fluid-actors")
VOLATILE = {"processId", "startedUtc", "completedUtc", "nativeObserver", "nativeObserverTrace",
            "nativeObserverDetached", "nativeObserverLiveDetachRejected"}


def physics_report(report: dict) -> dict:
    return {key: value for key, value in report.items() if key not in VOLATILE}


def check_observation(report: dict, mode: str) -> dict:
    status = report["nativeObserver"]
    if not report["passed"] or not status["ForwardingOnly"] or status["AuthoritativeFluidOwner"] or status.get("RecoveryRequired") is not False:
        raise ValueError("Missing forwarding-only success")
    if status["ActiveCalls"] or not report["nativeObserverDetached"] or not report["nativeObserverLiveDetachRejected"]:
        raise ValueError("Unbalanced observer lifecycle")
    hooks = {item["Name"]: item for item in status["Hooks"]}
    if len(hooks) != 28 or any(item["Entries"] != item["Exits"] for item in hooks.values()):
        raise ValueError("Hook entry/exit imbalance")
    if hooks["update-data"]["Entries"] != (13 if mode == "fluid-actors" else 12):
        raise ValueError("Native physical update count differs from requested timesteps")
    if not hooks["initialize"]["Entries"] or not hooks["pressure"]["Entries"]:
        raise ValueError("Native control/physics hooks were not exercised")
    if hooks["messages"]["Entries"] < 2:
        raise ValueError("Empty and populated native batch branches were not exercised")
    emitter_entries = hooks["emitter"]["Entries"] + hooks["emitter-dispatch"]["Entries"]
    if mode == "fluid-actors" and (not report["fluidActorRegistrationModificationRemovalPassed"] or
                                  hooks["consumer"]["Entries"] != 13 or emitter_entries != 13):
        raise ValueError("Native fluid actor registration, transfers or removal control is missing")
    trace = report["nativeObserverTrace"]
    if status["TraceEventsDropped"] or len(trace) != status["TraceEventsReserved"]:
        raise ValueError("Diagnostic trace truncated or incomplete")
    if [event["Sequence"] for event in trace] != list(range(1, len(trace) + 1)):
        raise ValueError("Trace ordering changed")
    stacks = {}
    for event in trace:
        stack = stacks.setdefault(event["Thread"], [])
        if event["Entering"]:
            stack.append(event["Hook"])
        elif not stack or stack.pop() != event["Hook"]:
            raise ValueError("Native control trace has an unmatched exit")
    if any(stacks.values()):
        raise ValueError("Native control trace has an unmatched entry after shutdown")
    # A native Step call also processes synchronization-only work. Neither Step
    # entries nor PrepareGameData calls are authoritative elapsed-time counts.
    return {"mode": mode, "installedMask": status["InstalledMask"],
            "positiveTimestepReturns": status["PositiveTimestepReturns"],
            "physicalUpdates": hooks["update-data"]["Entries"],
            "stepEntries": hooks["step"]["Entries"], "traceEvents": len(trace),
            "unexercisedHooks": [name for name, item in hooks.items() if not item["Entries"]]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--observer", type=Path, required=True)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New evidence directory")
    parser.add_argument("--probe", type=Path, default=ROOT / "tools/NativeShadowProbe/bin/Release/net10.0/NativeShadowProbe.dll")
    parser.add_argument("--dotnet", default="dotnet")
    parser.add_argument("--mode", choices=(*MODES, "all"), default="all")
    args = parser.parse_args()
    native = args.game / "OxygenNotIncluded_Data/Plugins" / ("x86_64/SimDLL.dll" if os.name == "nt" else "libSimDLL.so")
    for path in (native, args.observer, args.probe, *(args.contract / name for name in ("contract.json", "elements.bin", "diseases.bin"))):
        if not path.is_file(): parser.error(f"Required input missing: {path}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    summary = {"kind": "native-hook-forwarding-comparison", "authoritativeFluidOwner": False,
               "gameBuild": 744825, "platform": sys.platform,
               "observerSha256": hashlib.sha256(args.observer.read_bytes()).hexdigest(), "results": []}
    for mode in MODES if args.mode == "all" else (args.mode,):
        result = {"mode": mode, "passed": False}
        try:
            reports = []
            for observed in (False, True):
                name = mode + ("-observed" if observed else "-control")
                report_path = output / (name + ".json")
                command = [args.dotnet, str(args.probe.resolve()), str(native.resolve()), str(args.contract.resolve()), mode, str(report_path)]
                if observed: command += [str(args.observer.resolve()), str(args.game.resolve())]
                with (output / (name + ".log")).open("w", encoding="utf-8") as log:
                    child = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=45, check=False)
                if child.returncode != 0: raise ValueError(f"{name} exited {child.returncode}; inspect retained log")
                report = json.loads(report_path.read_text(encoding="utf-8"))
                if report.get("passed") is not True: raise ValueError(f"{name} did not pass")
                reports.append(report)
            if physics_report(reports[0]) != physics_report(reports[1]):
                raise ValueError("Observed physics/events/save output differs from unhooked control")
            result.update(check_observation(reports[1], mode))
            result.update(passed=True, identicalPhysicsReports=True)
        except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
            result["error"] = str(error)
        summary["results"].append(result)
        print(json.dumps(result), flush=True)
    summary["passed"] = all(item["passed"] for item in summary["results"])
    (output / "suite.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
