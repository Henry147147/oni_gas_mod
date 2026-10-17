#!/usr/bin/env python3
"""Test the licensed Windows ONI player under Proton on a Linux virtual display.

Uses the existing private authenticated Steam client, a fresh Wine prefix and
profile, and the same evidence gates as native Linux/Windows runs. Proton
evidence is WindowsPlayer evidence on Linux, not a test on the Windows OS.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path, PureWindowsPath
import secrets
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oni_headless as common
from oni_native_domain_evidence import native_domain_passed
from oni_native_matrix import SCENES

PLATFORM = "WindowsPlayer"
MOD_ID = "MixedAtmospheres.IntegrationProbe"
MODES = tuple(SCENES.values()) + ("native-load-domain", "menu")
GAME_FILES = {
    "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll":
        "a58e04d0ffdf89b86fb28b71ad900625b3b539db30d67f8c6269f73a9f5ae599",
    "OxygenNotIncluded_Data/Plugins/x86_64/SimDLL.dll":
        "031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727",
}


def windows_path(path: Path) -> str:
    return "Z:\\" + str(path.resolve()).lstrip("/").replace("/", "\\")


def host_save_path(value: str, output: Path) -> Path:
    """Resolve only saves inside this invocation, never another prefix/profile."""
    path = PureWindowsPath(value)
    if not path.is_absolute() or ".." in path.parts:
        raise ValueError("Expected an absolute private Windows save path")
    if path.drive.upper() == "C:":
        candidate = output / "compatdata/pfx/drive_c" / Path(*path.parts[1:])
    elif path.drive.upper() == "Z:":
        candidate = Path("/") / Path(*path.parts[1:])
    else:
        raise ValueError("Expected a private C: or Z: save path")
    candidate = candidate.resolve()
    if not candidate.is_relative_to(output.resolve()) or not candidate.is_file():
        raise ValueError("Windows save escaped this invocation or does not exist")
    return candidate


def private_output(output: Path, protected: tuple[Path, ...]) -> None:
    if output.exists():
        raise ValueError("Use a fresh output directory")
    if any(output.is_relative_to(path) or path.is_relative_to(output) for path in protected):
        raise ValueError("Keep the private run outside the game, Steam state, package and Proton tool")


def sandbox_command(home: Path, state: Path, output: Path, game: Path,
                    proton: Path, tail: list[str]) -> list[str]:
    # Steam IPC stays in the host PID namespace, /tmp and /dev/shm. Only the
    # disposable profile/evidence and Proton's dist.lock/fixups are writable.
    return ["bwrap", "--die-with-parent", "--ro-bind", "/", "/",
            "--bind", str(home), str(Path.home()), "--bind", str(output), str(output),
            "--bind", str(proton), str(proton), "--ro-bind", str(state), str(state),
            "--ro-bind", str(game), str(game), "--bind", "/tmp", "/tmp",
            "--bind", "/proc", "/proc", "--dev-bind", "/dev", "/dev",
            "--chdir", str(game), "--setenv", "HOME", str(Path.home()),
            "--setenv", "XDG_CONFIG_HOME", str(home / ".config"),
            "--setenv", "XDG_DATA_HOME", str(home / ".local/share"),
            "--setenv", "XDG_CACHE_HOME", str(home / ".cache"),
            "--setenv", "XDG_RUNTIME_DIR", str(output / "runtime"),
            "--unsetenv", "DBUS_SESSION_BUS_ADDRESS", str(proton / "proton"), *tail]


def game_env(state: Path, output: Path, game: Path, display: str) -> dict:
    env = dict(os.environ)
    # Desktop extraction libraries are for Xvfb/scrot, not Proton's Wine.
    for key in ("WAYLAND_DISPLAY", "LD_LIBRARY_PATH", "WINEPREFIX", "WINESERVER"):
        env.pop(key, None)
    env.update(DISPLAY=display, XAUTHORITY=str(output / "Xauthority"), LIBGL_ALWAYS_SOFTWARE="1",
        STEAM_COMPAT_CLIENT_INSTALL_PATH=str(state / "steam-home/.local/share/Steam"),
        STEAM_COMPAT_DATA_PATH=str(output / "compatdata"), STEAM_COMPAT_INSTALL_PATH=str(game),
        STEAM_COMPAT_APP_ID="457140", SteamAppId="457140", SteamGameId="457140",
        PROTON_USE_WINED3D=env.get("PROTON_USE_WINED3D", "1"), WINEDEBUG="-all", PROTON_LOG="1", PROTON_LOG_DIR=str(output))
    return env


def compatibility_settings(env: dict) -> dict:
    # Record only the nonsecret runtime switches used by these controlled
    # comparisons. Never serialize the inherited process environment.
    keys = ("MONOMOD_RunningOnWine", "MONOMOD_DMDType", "PROTON_USE_WINED3D", "WINEDEBUG")
    return {key: env[key] for key in keys if key in env}


def start_display(state: Path, output: Path) -> tuple[dict, str, dict]:
    return common.start_private_display(state, output)


def initialize_prefix(command: list[str], output: Path, env: dict) -> None:
    # Proton takes pfx.lock before creating the prefix itself. Its parent is
    # normally supplied by Steam; a direct private invocation must create it.
    (output / "compatdata").mkdir(mode=0o700)
    record = common.spawn(command, output / "prefix-initialization.log", env)
    try:
        deadline = time.monotonic() + 180
        while common.alive(record):
            if time.monotonic() > deadline:
                raise TimeoutError("Private Proton prefix initialization timed out")
            time.sleep(.2)
        if common.CHILDREN[record["pid"]].wait(timeout=5) != 0:
            raise RuntimeError("Private Proton prefix initialization failed; inspect its log")
    finally:
        common.stop_process(record)


def domain_options(args) -> dict:
    mode = args.mode
    return dict(expected_platform=PLATFORM,
        initial_inventory=mode in ("native-import-domain", "native-liquid-import-domain"), liquid=mode in ("native-liquid-pressure-read-domain", "native-liquid-domain", "native-liquid-import-domain"), liquid_transfers=mode == "native-liquid-transfer-domain",
        heat=mode == "native-heat-domain", liquid_heat=mode == "native-liquid-heat-domain",
        transfers=mode == "native-transfer-domain", actors=mode in ("native-actor-domain", "native-liquid-actor-domain"),
        liquid_actors=mode == "native-liquid-actor-domain", pressure_read=mode in ("native-pressure-read-domain", "native-liquid-pressure-read-domain"), breathing_read=mode == "native-breathing-read-domain", carbon_particle=mode == "native-carbon-particle-domain", carbon=mode == "native-carbon-domain",
        conduction=mode == "native-conduction-domain", gravity=mode == "native-gravity-domain",
        liquid_gravity=mode == "native-liquid-gravity-domain",
        checkpoint=mode in ("native-checkpoint-domain", "native-paired-checkpoint-domain"),
        persistent_actors=getattr(args,"verify_persistent_actors",False) is True,
        paired=mode == "native-paired-checkpoint-domain", natural_autosave=getattr(args,"verify_natural_autosave",False) is True, saved=mode == "native-save-domain" or args.save_colony or getattr(args,"verify_natural_autosave",False) is True,
        restored=mode == "native-load-domain", save_io_recovery=args.verify_save_io_recovery,
        reload_in_process=args.reload_in_process)


def optional_json(path: Path) -> dict | None:
    try:
        return common.read_json(path)
    except (OSError, json.JSONDecodeError):
        return None


def validate_arguments(args) -> None:
    if getattr(args,"graphics_api","glcore") not in ("glcore","d3d11"):
        raise ValueError("Graphics API must be glcore or d3d11")
    if getattr(args,"verify_persistent_actors",False) is True and (args.mode not in ("native-domain","native-liquid-domain") or
            not (args.save_colony or getattr(args,"verify_natural_autosave",False) is True) or args.frames<80):
        raise ValueError("Persistent actor validation requires a gas/water source, a source save or natural autosave, and at least eighty frames")
    if getattr(args,"verify_natural_autosave",False) is True and (args.mode != "native-domain" or not 3100 <= args.frames <= 5000 or args.save_colony):
        raise ValueError("Natural autosave requires native-domain, 3100..5000 frames and no manual source save")
    if args.mode in ("native-carbon-particle-domain", "native-breathing-read-domain") and args.frames < 30:
        raise ValueError("The CO2 particle and breathing-reader controls require at least 30 frames")
    if args.mode in ("native-actor-domain", "native-liquid-actor-domain") and args.frames < 80:
        raise ValueError("Registered actor scenes require at least 80 frames")
    if bool(args.load_save) != (args.mode == "native-load-domain"):
        raise ValueError("native-load-domain requires --load-save; other scenes cannot use it")
    if args.load_save and not args.load_save.is_file():
        raise ValueError("--load-save must name an existing completed colony save")
    if args.resave_before_resume and args.mode != "native-load-domain":
        raise ValueError("--resave-before-resume requires native-load-domain")
    if args.reload_in_process and (args.mode != "native-load-domain" or not args.resave_before_resume):
        raise ValueError("--reload-in-process requires native-load-domain --resave-before-resume")
    if args.verify_save_io_recovery and (args.mode != "native-save-domain" or args.frames < 16):
        raise ValueError("Save I/O recovery requires native-save-domain and at least sixteen frames")
    if args.save_colony and args.mode in ("native-load-domain", "menu"):
        raise ValueError("--save-colony requires an initial domain scene")


def cleanup_run(record, xvfb, prefix_started: bool, proton: Path, output: Path) -> None:
    errors = []
    try:
        common.stop_process(record)
    except BaseException as error:
        errors.append("game: " + repr(error))
    try:
        if prefix_started and (output / "compatdata/pfx").is_dir():
            env = dict(os.environ, WINEPREFIX=str(output / "compatdata/pfx"), WINEDEBUG="-all")
            env.pop("LD_LIBRARY_PATH", None)
            server=str(proton / "files/bin/wineserver")
            killed=subprocess.run([server,"-k"],env=env,check=False,timeout=30,
                                  stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            # Wine returns 1 when no server exists. Confirm retirement with a
            # bounded wait; neither a real kill error nor a lingering prefix
            # server can be treated as successful cleanup.
            if killed.returncode not in (0,1) or (killed.returncode==1 and killed.stderr):
                raise subprocess.CalledProcessError(killed.returncode,[server,"-k"],stderr=killed.stderr)
            subprocess.run([server,"-w"],env=env,check=True,timeout=30,
                           stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    except BaseException as error:
        errors.append("private Wine prefix: " + repr(error))
    try:
            common.stop_private_display(xvfb)
    except BaseException as error:
        errors.append("private Xvfb: " + repr(error))
    if errors:
        raise RuntimeError("Run cleanup failed: " + "; ".join(errors))


def playtest(args, state: Path, session: dict) -> int:
    validate_arguments(args)
    package, game, proton = args.package.resolve(), args.game_dir.resolve(), args.proton.resolve()
    manifest = common.verify_package(package, "win-x64")
    if args.mode != "menu" and manifest.get("native_observer_enabled") is not True:
        raise ValueError("Native scenes require a Windows package built with --native-observer")
    if (game / "OxygenNotIncluded.exe").read_bytes()[:2] != b"MZ":
        raise ValueError("Supply the licensed Windows ONI installation")
    game_hashes = {name: common.digest(game / name) for name in GAME_FILES}
    if game_hashes != GAME_FILES:
        raise ValueError("The installed Windows game does not match the pinned native/managed profile")
    for name in ("proton", "version", "files/bin/wineserver"):
        if not (proton / name).is_file():
            raise ValueError("Supply a complete installed Proton tool directory")
    if not common.alive(session.get("steam")):
        raise RuntimeError("Start the private Steam client and sign in first")
    if common.alive(session.get("game")):
        raise RuntimeError("Another retained game is running")
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + secrets.token_hex(3)
    output = (args.output or common.ROOT / "artifacts/proton-runs" / run_id).resolve()
    private_output(output, (game, proton, package, state))
    output.mkdir(parents=True, mode=0o700)
    home = output / "home"
    home.mkdir(mode=0o700)
    (home / ".steam").symlink_to(state / "steam-home/.steam")
    (output / "runtime").mkdir(mode=0o700)
    result = {"kind": "actual-unity-bounded-native-proton-run", "status": "initializing", "exit_code": 2,
        "mode": args.mode, "output": str(output), "host_platform": "linux", "target_platform": PLATFORM,
        "actual_windows_os": False, "real_unity_player": False, "software_rendering": True,
        "first_playable_colony_passed": False, "whole_colony_fluid_authority": False,
        "authoritative_colony_simulation_ready": False, "full_mod_completion_verified": False,
        "package_manifest": manifest, "game_hashes": game_hashes,
        "proton_version": (proton / "version").read_text().strip(), **common.revision_fields(manifest)}
    common.write_json(output / "result.json", result)
    xvfb = record = None
    prefix_started = False
    try:
        xvfb, display, desktop = start_display(state, output)
        env = game_env(state, output, game, display)
        result["compatibility_environment"] = compatibility_settings(env)
        result["display"] = display
        prefix_started = True
        initialize_prefix(sandbox_command(home, state, output, game, proton, ["run", "wineboot", "-u"]), output, env)
        profile = output / "compatdata/pfx/drive_c/users/steamuser/Documents/Klei/OxygenNotIncluded"
        if not profile.resolve().is_relative_to(output):
            raise ValueError("Proton profile escaped this run")
        mod = profile / "mods/Dev" / MOD_ID
        common.stage_package(package, mod, manifest, "win-x64")
        common.write_json(profile / "mods/mods.json", {"version": 1, "mods": [{
            "label": {"distribution_platform": "Dev", "id": MOD_ID, "title": "Mixed Atmospheres - Integration Probe",
                      "version": common.mono_label_version(MOD_ID)}, "status": 1, "enabled": True,
            "enabledForDlc": ["", "EXPANSION1_ID"], "crash_count": 0, "reinstall_path": None,
            "staticID": MOD_ID}]})
        load_fixture = None
        if args.load_save:
            source_hash = common.digest(args.load_save)
            load_fixture = output / "input-colony.sav"
            shutil.copy2(args.load_save, load_fixture)
            if common.digest(load_fixture) != source_hash or common.digest(args.load_save) != source_hash:
                raise ValueError("Source colony changed during staging")
            result["load_save_fixture"] = {"source": str(args.load_save.resolve()), "path": str(load_fixture), "sha256": source_hash}
        if args.mode != "menu":
            common.write_json(mod / "colony-validation.json", {
            "Mode": args.mode, "AutoStartNewColony": True, "ReuseGeneratedWorld": False,
            "CompletedFrames": args.frames, "TimeoutSeconds": args.timeout, "QuitWhenComplete": True,
            "Seed": "147147", "ColonyName": "MA-Proton-" + run_id,
            "LoadSavePath": windows_path(load_fixture) if load_fixture else "",
            "SaveOnComplete": args.save_colony, "SaveBeforeResume": args.resave_before_resume,
                "VerifySaveIoRecovery": args.verify_save_io_recovery, "ReloadInProcess": args.reload_in_process,
                **({"VerifyNaturalAutosave": True} if getattr(args,"verify_natural_autosave",False) is True else {}),
                **({"VerifyPersistentActors": True} if getattr(args,"verify_persistent_actors",False) is True else {})})
        graphics_api = getattr(args,"graphics_api","glcore")
        command = sandbox_command(home, state, output, game, proton,
            ["run", windows_path(game / "OxygenNotIncluded.exe"), "-force-" + graphics_api, "-screen-fullscreen", "0",
             "-screen-width", "1280", "-screen-height", "720", "-logFile", windows_path(output / "Player.log")])
        result.update(status="running", command=command, graphics_api=graphics_api)
        common.write_json(output / "result.json", result)
        record = common.spawn(command, output / "console.log", env)
        session.update(game=record, latest_run=str(output))
        common.write_json(state / "session.json", session)
        print(f"WindowsPlayer under Proton PID {record['pid']}; evidence: {output}", flush=True)
        deadline = time.monotonic() + args.timeout + 60
        timed_out = False
        stopped_for_mod_failure = False
        menu_seen = None
        while common.alive(record):
            player_log=output/"Player.log"
            log=player_log.read_text(errors="replace") if player_log.exists() else ""
            if common.mod_failure_lines(log,(MOD_ID,)):
                stopped_for_mod_failure = True
                break
            if args.mode=="menu" and common.MAIN_MENU_MARKER in log:
                menu_seen=time.monotonic() if menu_seen is None else menu_seen
                if common.contract_export(log) or time.monotonic()-menu_seen>=common.MENU_EXPORT_WAIT_SECONDS:
                    time.sleep(common.MENU_SETTLE_SECONDS)
                    break
            if time.monotonic() >= deadline:
                timed_out = True
                break
            time.sleep(1)
        subprocess.run([str(state / "tools/desktop/usr/bin/scrot"), "-o", str(output / "desktop-final.png")],
                       env=desktop, check=True, timeout=20)
        probe = optional_json(mod / "integration-probe.json")
        colony_file = mod / "game-contract-local/colony-validation/colony-validation.json"
        report = optional_json(colony_file)
        domain = optional_json(colony_file.parent / "native-domain.json")
        observation = optional_json(mod / "native-backend-observation-report.json")
        log = (output / "Player.log").read_text(errors="replace") if (output / "Player.log").exists() else ""
        player_exit = common.CHILDREN[record["pid"]].poll()
        status, code = common.classify(args.mode, log, probe, report, timed_out, player_exit, (MOD_ID,), PLATFORM)
        domain_ok = args.mode!="menu" and native_domain_passed(domain, observation, args.frames, **domain_options(args))
        if code == 0 and args.mode!="menu" and not domain_ok:
            status, code = "failed-native-domain", 2
        if code==0 and args.mode=="menu" and not common.native_observer_passed(manifest,observation):
            status,code="failed-native-observer",2
        checkpoint = (domain or {}).get("ColonyCheckpoint") or {}
        # Authenticate the file and export a host path without altering raw
        # Windows game reports. Linux loaders must never guess at C: paths.
        saved_file = checkpoint.get("SavedFile") or checkpoint.get("InitialAutosaveFile")
        if code == 0 and saved_file:
            saved = host_save_path(saved_file, output)
            result["host_saved_file"] = str(saved)
            result["host_saved_file_sha256"] = common.digest(saved)
            if checkpoint.get("SavedFileSha256") and result["host_saved_file_sha256"] != checkpoint["SavedFileSha256"]:
                status, code = "failed-save-file-hash", 2
        if load_fixture and (common.digest(args.load_save) != source_hash or common.digest(load_fixture) != source_hash):
            status, code = "failed-input-save-mutated", 2
        if common.verify_package(package, "win-x64") != manifest:
            raise ValueError("The immutable source package changed during the game run")
        result.update(status=status, exit_code=code, timed_out=timed_out, observed_player_exit_code=player_exit,
            stopped_early_for_mod_failure=stopped_for_mod_failure,
            real_unity_player=bool(probe and probe.get("Platform")==PLATFORM),
            integration_probe=probe, colony_report=report, native_domain=domain,
            native_backend_observation=observation, bounded_native_domain_passed=domain_ok,
            steam_blocked=common.steam_blocked(log), main_menu_reached=common.MAIN_MENU_MARKER in log,
            main_menu_contract_export=common.contract_export(log), mod_failure_lines=common.mod_failure_lines(log, (MOD_ID,))[:50])
        common.write_json(output / "result.json", result)
        print(f"{status}: {output / 'result.json'}", flush=True)
        return code
    except BaseException as error:
        result.update(status="failed-runner", exit_code=2, error=repr(error))
        common.write_json(output / "result.json", result)
        raise
    finally:
        try:
            cleanup_run(record, xvfb, prefix_started, proton, output)
        except BaseException as error:
            result.update(status_before_cleanup=result["status"], status="failed-cleanup", exit_code=2,
                          cleanup_error=repr(error))
            common.write_json(output / "result.json", result)
            raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state", type=Path, default=common.DEFAULT_STATE)
    parser.add_argument("--game-dir", type=Path, default="/mnt/data/share/oni/OxygenNotIncluded")
    parser.add_argument("--proton", type=Path, required=True, help="Installed complete Proton tool root")
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--mode", choices=MODES, default="native-domain")
    parser.add_argument("--graphics-api",choices=("glcore","d3d11"),default="glcore",
        help="Unity graphics API; OpenGL Core avoids the reproduced software Direct3D11 clock/startup failures")
    parser.add_argument("--frames", type=int, choices=range(1, 10001), default=120, metavar="1..10000")
    parser.add_argument("--timeout", type=int, choices=range(30, 7201), default=1800, metavar="30..7200")
    parser.add_argument("--save-colony", action="store_true")
    parser.add_argument("--load-save", type=Path)
    parser.add_argument("--resave-before-resume", action="store_true")
    parser.add_argument("--reload-in-process", action="store_true")
    parser.add_argument("--verify-natural-autosave",action="store_true",help="Wait for natural day rollover; native-domain and 3100..5000 frames")
    parser.add_argument("--verify-persistent-actors",action="store_true",help="Save three live point actors; gas/water source, source save or natural autosave, and at least eighty frames")
    parser.add_argument("--verify-save-io-recovery", action="store_true")
    args = parser.parse_args()
    state = args.state.resolve()
    # This is the native Linux runner's lock, held for the entire game lifecycle.
    with common.cleanup_on_termination(), common.locked(state / common.LOCK_NAME):
        return playtest(args, state, common.read_session(state))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"oni_proton_headless: {error}", file=sys.stderr)
        sys.exit(2)
