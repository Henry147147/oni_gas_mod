#!/usr/bin/env python3
"""Run explicit DLC startup/world-generation/save/load compatibility checks.

There are 32 content selections per operating system. Proton is an additional
WindowsPlayer route on Linux and never fills a Windows OS coverage row. A
successful load alone does not establish byte-exact active continuation.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys

import oni_dlc as dlc
from oni_native_matrix import digest, fingerprint, invoke

TARGETS = ("linux", "windows", "proton")
DEFAULT_GAME_SLOT = Path(__file__).resolve().parent / "game_slot.sh"
PROBE_FLAGS = ("ManagedFrameIntercepted", "ManagedSnapshotIntercepted", "DirectNativeMessageIntercepted",
               "ManagedSaveIntercepted", "NativeMixtureRoundtripPassed", "SettingsSerializationRoundtripPassed")


def plan() -> dict:
    return {"kind": "dlc-compatibility-plan", "passed": False,
            "cases": [{"target": target, **dlc.selection(mask), "status": "not-run",
                       "checks": ["startup", "world-generation", "save", "cold-load", "menu-load"]}
                      for target in ("linux", "windows") for mask in range(32)]}


def owner_evidence(owner: dict | None) -> bool:
    if not isinstance(owner, dict):
        return False
    for field in ("MaximumRelativeMassError", "MaximumRelativeEnergyError"):
        value = owner.get(field)
        if type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= 1e-12:
            return False
    pipe = owner.get("PipeSave")
    return (type(owner.get("State")) is int and owner["State"] == 1 and owner.get("Faulted") is False and owner.get("Error") == ""
            and type(owner.get("OwnedCells")) is int and owner["OwnedCells"] > 0
            and type(owner.get("CompletedFrames")) is int and owner["CompletedFrames"] > 0
            and type(owner.get("Publications")) is int and owner["Publications"] > 0
            and not owner.get("RendererError") and not owner.get("PresentationError")
            and isinstance(pipe, dict) and pipe.get("Active") is True and pipe.get("AuditClosed") is True)


def verify_case(data: dict, target: str, mask: int, loading: bool, menu: bool) -> None:
    if target not in TARGETS or menu and not loading:
        raise ValueError("Select an actual platform and a load before verifying menu reload")
    mode = "colony-owner-load" if loading else "colony-owner"
    if not isinstance(data, dict) or data.get("status") != "passed-" + mode or data.get("exit_code") != 0:
        raise ValueError("The real game rejected the compatibility case")
    if data.get("owner_environment_overrides") or data.get("experimental_spaced_out_control"):
        raise ValueError("Development admission cannot establish ordinary DLC compatibility")
    probe = data.get("integration_probe") or {}
    expected_player = "LinuxPlayer" if target == "linux" else "WindowsPlayer"
    if not isinstance(probe, dict) or probe.get("Platform") != expected_player or any(probe.get(flag) is not True for flag in PROBE_FLAGS):
        raise ValueError("Compatibility evidence came from the wrong Unity player")
    if target == "linux":
        if data.get("platform") != "native Linux":
            raise ValueError("Linux OS evidence is missing")
    elif (data.get("target_platform") != "WindowsPlayer" or data.get("real_unity_player") is not True
          or data.get("host_platform") != ("windows" if target == "windows" else "linux")
          or data.get("actual_windows_os") is not (target == "windows")):
        raise ValueError("Windows OS and Proton evidence must identify their actual host")
    report = data.get("colony_report")
    dlc.verify_report(mask, report, saved=True)
    if report.get("Status") != "passed" or report.get("GameStarted") is not True:
        raise ValueError("The colony did not start")
    if type(report.get("WorldCount")) is not int or report["WorldCount"] < (2 if mask & 1 else 1):
        raise ValueError("The requested single/multiple asteroid layout was not generated")
    if not owner_evidence(data.get("colony_owner")):
        raise ValueError("The colony has no healthy audited MacClosedWorld owner and mixed pipes")
    if loading:
        if report.get("LoadContentCompatibilityChecked") is not True or report.get("LoadContentCompatible") is not True:
            raise ValueError("The loader did not verify the source save's content")
        if dlc.mask_for_ids(report.get("RequiredLoadDlcIds")) != mask or report.get("UnavailableLoadDlcIds") != []:
            raise ValueError("The loaded save belongs to a different DLC selection")
        restored = data["colony_owner"].get("RestoredOwner") or {}
        if not isinstance(restored, dict) or restored.get("Reencoded") is not True:
            raise ValueError("The saved physical owner did not reencode exactly")
    if menu:
        reload = data["colony_owner"].get("OwnerMenuReload") or {}
        if not isinstance(reload, dict) or any(reload.get(field) is not True for field in ("Requested", "Verified", "ShutdownObserved", "MainMenuObserved")):
            raise ValueError("The in-process menu reload was not verified")


def run_matrix(runner: Path, package: Path, output: Path, *, target: str, masks: list[int] | None = None,
               frames: int = 30, timeout: int = 1800, proton: Path | None = None,
               game_dir: Path | None = None, game_slot: Path | None = None) -> dict:
    if target not in TARGETS or (target == "windows" and os.name != "nt") or (target != "windows" and not sys.platform.startswith("linux")):
        raise ValueError("Use the Windows runner on Windows; Linux/Proton runners require Linux")
    if target == "proton" and proton is None or target != "proton" and proton is not None:
        raise ValueError("Only the Proton target requires --proton")
    if target == "windows" and game_dir is not None:
        raise ValueError("The Windows OS runner uses the installed Steam game; --game-dir is for Linux/Proton")
    if target == "windows" and game_slot is not None:
        raise ValueError("The shared Linux game slot is only for Linux/Proton runs")
    if target != "windows":
        game_slot = (game_slot or DEFAULT_GAME_SLOT).resolve()
        if not game_slot.is_file():
            raise ValueError("Supply the game-slot wrapper (tools/game_slot.sh) with --game-slot before launching a game")
    masks = list(range(32)) if masks is None else list(masks)
    if not masks or len(set(masks)) != len(masks):
        raise ValueError("Select distinct DLC masks")
    for mask in masks:
        dlc.ids_for_mask(mask)
    if type(frames) is not int or not 1 <= frames <= 10000 or type(timeout) is not int or not 30 <= timeout <= 7200:
        raise ValueError("Expected 1..10000 frames and a 30..7200 second per-game timeout")
    runner, package, output = runner.resolve(), package.resolve(), output.resolve()
    if output.is_relative_to(package) or package.is_relative_to(output):
        raise ValueError("Keep evidence separate from the immutable package")

    def snapshot():
        frozen = fingerprint(runner, package, windows=target == "proton")
        names = ["tools/oni_dlc_matrix.py"]
        if target == "windows":
            names.append("tools/run_colony_validation.ps1")
        frozen["runner_files"].update({name: digest(runner / name) for name in names})
        if game_slot:
            frozen["game_slot"] = {"path": str(game_slot), "sha256": digest(game_slot)}
        return frozen

    frozen = snapshot()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    report = {"kind": "actual-unity-dlc-compatibility-matrix", "passed": False, "target": target,
              "full_target_coverage_passed": False, "actual_windows_os": target == "windows",
              "byte_exact_active_continuation_verified": False, "inputs": frozen,
              "masks": masks, "frames": frames,
              "cases": [{**dlc.selection(mask), "status": "not-run", "runs": []} for mask in masks]}

    def write():
        (output / "matrix.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def case(row, name, loading, menu, source=None):
        if snapshot() != frozen:
            raise ValueError("Frozen runner or package changed before " + name)
        run = output / f"{row['mask']:02d}-{name}"
        if target == "windows":
            command = ["pwsh", "-NoProfile", "-File", str(runner / "tools/run_colony_validation.ps1"),
                       "-ColonyOwner", "-Package", str(package), "-DlcMask", str(row["mask"]),
                       "-Output", str(run), "-Frames", str(frames), "-TimeoutSeconds", str(timeout), "-Python", sys.executable]
            command += ["-LoadSave", str(source)] if loading else ["-SaveColony"]
            if menu:
                command.append("-ReloadInProcess")
        else:
            mode = "colony-owner-load" if loading else "colony-owner"
            executable = [str(runner / "tools/oni_proton_headless.py"), "--proton", str(proton.resolve())] if target == "proton" else [str(runner / "tools/oni_headless.py"), "run", "--private-display"]
            command = [sys.executable, *executable, "--package", str(package), "--dlc-mask", str(row["mask"]),
                       "--mode", mode, "--output", str(run), "--frames", str(frames), "--timeout", str(timeout)]
            command += ["--load-save", str(source)] if loading else ["--save-colony"]
            if menu:
                command.append("--reload-in-process")
            if game_dir:
                command += ["--game-dir", str(game_dir.resolve())]
            # Queue each individual Unity process through the shared game/CPU
            # locks, releasing between phases so the release chain can run.
            command = [str(game_slot), "low", *command]
        entry = {"phase": name, "status": "running", "command": command}
        row["runs"].append(entry)
        write()
        # The queue can outlive a game's own timeout. The inner harness starts
        # its bounded clock only after it acquires the game slot.
        code = invoke(command, output / (run.name + ".log"), None if game_slot else timeout + 360)
        entry["exit_code"] = code
        result = run / "result.json"
        if not result.is_file():
            entry["status"] = "missing-result"
            raise ValueError("Game produced no result for " + run.name)
        entry.update(result=str(result), result_sha256=digest(result), status="failed")
        data = json.loads(result.read_text(encoding="utf-8-sig"))
        if code != 0:
            raise ValueError("Rejected compatibility case: " + run.name)
        verify_case(data, target, row["mask"], loading, menu)
        if snapshot() != frozen:
            raise ValueError("Frozen runner or package changed during " + run.name)
        entry["status"] = "passed"
        write()
        return data, run

    write()
    try:
        for row in report["cases"]:
            row["status"] = "running"
            data, run = case(row, "new-save", False, False)
            source_value = data.get("host_saved_file") if target != "linux" else data["colony_report"].get("ColonySavePath")
            if not isinstance(source_value, str) or not Path(source_value).is_absolute():
                raise ValueError("Source colony has no absolute save receipt")
            source = Path(source_value).resolve()
            if not source.is_relative_to(run) or not source.is_file():
                raise ValueError("Source save escaped its disposable run")
            source_hash = digest(source)
            if target != "linux" and data.get("host_saved_file_sha256") != source_hash:
                raise ValueError("Source save differs from its host file receipt")
            row["source_save_sha256"] = source_hash
            for name, menu in (("cold-load", False), ("menu-load", True)):
                case(row, name, True, menu, source)
                if digest(source) != source_hash:
                    raise ValueError("Original source save changed during reload")
            row["status"] = "passed"
            write()
        report["passed"] = True
        report["full_target_coverage_passed"] = set(masks) == set(range(32))
    except (Exception, KeyboardInterrupt) as error:
        row["status"] = "failed"
        report["failure"] = str(error) or type(error).__name__
        write()
        raise
    write()
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("plan", help="Print the 64 OS/content rows without starting a game")
    check = sub.add_parser("check", help="Check one completed OS/content result")
    check.add_argument("--result", type=Path, required=True)
    check.add_argument("--target", choices=TARGETS, required=True)
    check.add_argument("--mask", type=int, choices=range(32), required=True)
    check.add_argument("--loading", action="store_true")
    check.add_argument("--menu", action="store_true")
    run = sub.add_parser("run")
    run.add_argument("--runner-source", type=Path, required=True)
    run.add_argument("--package", type=Path, required=True)
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--target", choices=TARGETS, required=True)
    run.add_argument("--masks", type=int, nargs="+", choices=range(32), help="Default: all 32 combinations")
    run.add_argument("--frames", type=int, default=30)
    run.add_argument("--timeout", type=int, default=1800)
    run.add_argument("--proton", type=Path)
    run.add_argument("--game-dir", type=Path)
    run.add_argument("--game-slot", type=Path, help="Linux/Proton per-game queue (default: tools/game_slot.sh)")
    args = parser.parse_args()
    if args.command == "plan":
        print(json.dumps(plan(), indent=2))
        return 0
    if args.command == "check":
        try:
            verify_case(json.loads(args.result.read_text(encoding="utf-8-sig")), args.target,
                        args.mask, args.loading, args.menu)
        except (OSError, ValueError) as error:
            print("oni_dlc_matrix:", error, file=sys.stderr)
            return 2
        return 0
    try:
        report = run_matrix(args.runner_source, args.package, args.output, target=args.target, masks=args.masks,
                            frames=args.frames, timeout=args.timeout, proton=args.proton, game_dir=args.game_dir,
                            game_slot=args.game_slot)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("oni_dlc_matrix:", error, file=sys.stderr)
        return 2
    print("Passed", len(report["cases"]), "DLC combinations on", args.target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
