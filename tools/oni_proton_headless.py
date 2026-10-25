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
import math
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
CONFIGURED_MODES = ("native-configured-domain", "native-configured-water-domain", "native-configured-vacuum-domain")
OWNER_MODES = ("colony-owner", "colony-owner-load")
FORWARD_MODES = ("menu", "colony-owner-inactive")
MODES = tuple(SCENES.values()) + CONFIGURED_MODES + OWNER_MODES + FORWARD_MODES + ("native-load-domain",)
GAME_FILES = {
    "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll":
        "a58e04d0ffdf89b86fb28b71ad900625b3b539db30d67f8c6269f73a9f5ae599",
    "OxygenNotIncluded_Data/Plugins/x86_64/SimDLL.dll":
        "031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727",
}
BASE_GAME_DISABLED_DLC = ("EXPANSION1_ID", "DLC2_ID", "DLC3_ID", "DLC4_ID", "DLC5_ID")


def base_game_preferences(profile: Path) -> None:
    # The normal main-menu DLC toggle persists <id>.ENABLED in KPlayerPrefs.
    # This is a fresh disposable profile; no entitlement or game code changes.
    (profile / "kplayerprefs.yaml").write_text("strings: {}\nints:\n" +
        "".join(f"  {identity}.ENABLED: 0\n" for identity in BASE_GAME_DISABLED_DLC) + "floats: {}\n")


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
    return dict(expected_platform=PLATFORM,mixed_phase=mode in ("native-mixed-domain", "native-mixed-heat-domain", "native-viscous-domain"),
        configured=mode in CONFIGURED_MODES, vacuum=mode=="native-configured-vacuum-domain",
        initial_inventory=mode in ("native-import-domain", "native-liquid-import-domain"), liquid=mode in ("native-liquid-pressure-read-domain", "native-liquid-domain", "native-liquid-import-domain", "native-configured-water-domain"), liquid_transfers=mode == "native-liquid-transfer-domain",
        solid_heat=mode=="native-solid-heat-domain", heat=mode in ("native-heat-domain", "native-mixed-heat-domain"), liquid_heat=mode == "native-liquid-heat-domain",
        transfers=mode in ("native-transfer-domain", "native-radius-domain", "native-rectangular-domain"), radial_consumption=mode == "native-radius-domain", rectangular_consumption=mode == "native-rectangular-domain", actors=mode in ("native-actor-domain", "native-liquid-actor-domain"),
        liquid_actors=mode == "native-liquid-actor-domain", pressure_read=mode in ("native-pressure-read-domain", "native-liquid-pressure-read-domain"), breathing_read=mode == "native-breathing-read-domain", carbon_particle=mode == "native-carbon-particle-domain", carbon=mode == "native-carbon-domain",
        conduction=mode in ("native-conduction-domain", "native-mixed-heat-domain"), gravity=mode == "native-gravity-domain",
        liquid_gravity=mode == "native-liquid-gravity-domain",
        checkpoint=mode in ("native-checkpoint-domain", "native-paired-checkpoint-domain"),
        persistent_actors=getattr(args,"verify_persistent_actors",False) is True,
        live_breathing=getattr(args,"verify_live_breathing",False) is True,
        transient_particles=getattr(args,"verify_transient_particles",False) is True,
        subnormal_carbon_particle=getattr(args,"subnormal_carbon_particle",False) is True,
        paired=mode == "native-paired-checkpoint-domain", natural_autosave=getattr(args,"verify_natural_autosave",False) is True, saved=mode == "native-save-domain" or args.save_colony or getattr(args,"verify_natural_autosave",False) is True,
        restored=mode == "native-load-domain", save_io_recovery=args.verify_save_io_recovery,
        reload_in_process=args.reload_in_process)


def optional_json(path: Path) -> dict | None:
    try:
        return common.read_json(path)
    except (OSError, json.JSONDecodeError):
        return None


def colony_owner_passed(owner: dict | None) -> bool:
    """Require actual owned frames, closed ledgers, and completed scenario checks."""
    if not isinstance(owner, dict) or owner.get("State") != 1 or owner.get("Faulted") is not False or owner.get("Error") != "":
        return False
    if not all(type(owner.get(key)) is int and owner[key] > 0 for key in ("CompletedFrames", "OwnedCells", "Publications")):
        return False
    for key in ("MaximumRelativeMassError", "MaximumRelativeEnergyError"):
        value = owner.get(key)
        if type(value) not in (float, int) or not math.isfinite(value) or not 0 <= value <= 1e-12:
            return False
    if owner.get("PresentationError") or owner.get("RendererError"):
        return False
    return all(item.get("Passed") is True for item in owner.get("Scenarios", []))


def owner_timing(samples: list[dict]) -> dict:
    def statistics(values):
        values = sorted(values)
        return None if not values else dict(samples=len(values), mean=sum(values)/len(values),
            p95=values[math.ceil(.95*len(values))-1], maximum=values[-1])
    adjacent = [(a, b) for a, b in zip(samples, samples[1:])
                if a["session"] == b["session"] and b["frame"] == a["frame"] + 1]
    return dict(owner_milliseconds=statistics([s["owner_ms"] for s in samples]),
        observed_milliseconds=statistics([(b["wall"]-a["wall"])*1000 for a,b in adjacent]),
        sampled_frames=len(samples), nonadjacent_intervals=max(0,len(samples)-1-len(adjacent)))


def validate_arguments(args) -> None:
    inspection = getattr(args, "inspection_seconds", 0)
    if type(inspection) is not int or not 0 <= inspection <= 300 or (inspection and getattr(args, "mode", None) == "menu"):
        raise ValueError("Completed inspection requires 1..300 seconds and a colony mode")
    if getattr(args,"subnormal_carbon_particle",False) is True and args.mode!="native-carbon-particle-domain" and not (getattr(args,"verify_transient_particles",False) is True and args.mode in ("native-domain","native-load-domain") and not getattr(args,"verify_live_breathing",False)):
        raise ValueError("Subnormal carbon input requires the carbon-particle scene or transient-particle save/load without a live producer")
    if getattr(args,"verify_transient_particles",False) is True and (args.mode not in ("native-domain","native-load-domain") or args.mode=="native-domain" and not args.save_colony or args.mode=="native-load-domain" and not args.resave_before_resume):
        raise ValueError("Transient particle validation requires a gas source save or a reload with a frame-zero resave")
    if getattr(args,"verify_live_breathing",False) is True and (args.mode not in ("native-domain","native-load-domain") or
            getattr(args,"verify_persistent_actors",False) is True or args.mode=="native-domain" and not 20<=args.frames<=25):
        raise ValueError("Live breathing requires a 20–25-frame gas source or its saved load, without fixture actors")
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
    if bool(args.load_save) != (args.mode in ("native-load-domain", "colony-owner-load")):
        raise ValueError("A load mode requires --load-save; other scenes cannot use it")
    if args.load_save and not args.load_save.is_file():
        raise ValueError("--load-save must name an existing completed colony save")
    if args.resave_before_resume and args.mode != "native-load-domain":
        raise ValueError("--resave-before-resume requires native-load-domain")
    if args.reload_in_process and args.mode != "colony-owner-load" and (args.mode != "native-load-domain" or not args.resave_before_resume):
        raise ValueError("--reload-in-process requires colony-owner-load or native-load-domain --resave-before-resume")
    if args.verify_save_io_recovery and (args.mode != "native-save-domain" or args.frames < 16):
        raise ValueError("Save I/O recovery requires native-save-domain and at least sixteen frames")
    if args.save_colony and args.mode in ("native-load-domain", *FORWARD_MODES):
        raise ValueError("--save-colony requires an initial domain scene")
    if (getattr(args, "scenario", None) or getattr(args, "cluster", None)) and args.mode != "colony-owner":
        raise ValueError("--scenario and --cluster require colony-owner")
    if getattr(args,"quit_from_menu",False) and (args.mode not in OWNER_MODES or inspection):
        raise ValueError("--quit-from-menu requires an owner mode without inspection")
    settings_preset=getattr(args,"settings_preset",None)
    if settings_preset and (args.mode!="colony-owner" or not settings_preset.is_file()):
        raise ValueError("--settings-preset requires colony-owner and an existing preset file")


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
    inspection_seconds = getattr(args, "inspection_seconds", 0)
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
    result = {"kind": "actual-unity-colony-owner-proton-run" if args.mode in OWNER_MODES else "actual-unity-bounded-native-proton-run", "status": "initializing", "exit_code": 2,
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
        if args.mode in OWNER_MODES:
            # Defaults and saved settings must stand on their own in this route.
            env = {key: value for key, value in env.items() if not key.startswith("MA_")}
            result["owner_environment_overrides"] = False
        settings_preset=getattr(args,"settings_preset",None)
        if settings_preset:
            preset_hash=common.digest(settings_preset)
            preset_fixture=output/"input-settings.json"
            shutil.copy2(settings_preset,preset_fixture)
            if common.digest(preset_fixture)!=preset_hash or common.digest(settings_preset)!=preset_hash:
                raise ValueError("Source settings preset changed during staging")
            env["MA_COLONY_SETTINGS_PRESET"]=windows_path(preset_fixture)
            result["settings_preset_fixture"]={"source":str(settings_preset.resolve()),"sha256":preset_hash}
            result["owner_environment_overrides"]=True
        if getattr(args,"settings_open",None):
            env["MA_SETTINGS_OPEN"]=args.settings_open
            result["settings_window_control"]=args.settings_open
            if args.mode in OWNER_MODES:result["owner_environment_overrides"]=True
        result["compatibility_environment"] = compatibility_settings(env)
        result["display"] = display
        prefix_started = True
        initialize_prefix(sandbox_command(home, state, output, game, proton, ["run", "wineboot", "-u"]), output, env)
        profile = output / "compatdata/pfx/drive_c/users/steamuser/Documents/Klei/OxygenNotIncluded"
        if not profile.resolve().is_relative_to(output):
            raise ValueError("Proton profile escaped this run")
        mod = profile / "mods/Dev" / MOD_ID
        common.stage_package(package, mod, manifest, "win-x64")
        if args.mode in OWNER_MODES:
            base_game_preferences(profile)
            result["disabled_dlc_preferences"] = list(BASE_GAME_DISABLED_DLC)
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
            "CompletedFrames": args.frames, "TimeoutSeconds": args.timeout, "QuitWhenComplete": not inspection_seconds,
            "Seed": "147147", "ColonyName": "MA-Proton-" + run_id,
            "LoadSavePath": windows_path(load_fixture) if load_fixture else "",
            "SaveOnComplete": args.save_colony, "SaveBeforeResume": args.resave_before_resume,
                "VerifySaveIoRecovery": args.verify_save_io_recovery, "ReloadInProcess": args.reload_in_process,
                **({"VerifyNaturalAutosave": True} if getattr(args,"verify_natural_autosave",False) is True else {}),
                **({"VerifyPersistentActors": True} if getattr(args,"verify_persistent_actors",False) is True else {}),
                **({"VerifyLiveBreathing": True} if getattr(args,"verify_live_breathing",False) is True else {}),
                **({"VerifyTransientParticles": True} if getattr(args,"verify_transient_particles",False) is True else {}),
                **({"SubnormalCarbonParticle": True} if getattr(args,"subnormal_carbon_particle",False) is True else {}),
                **({"Scenario": args.scenario} if getattr(args, "scenario", None) else {}),
                **({"Cluster": "clusters/" + args.cluster} if getattr(args, "cluster", None) else {}),
                **({"Speed": args.speed} if getattr(args, "speed", None) else {})})
            if getattr(args,"quit_from_menu",False):
                configuration=common.read_json(mod / "colony-validation.json")
                configuration["QuitFromMainMenu"]=True
                common.write_json(mod / "colony-validation.json",configuration)
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
        deadline = time.monotonic() + args.timeout + 60 + inspection_seconds
        inspection_started = None
        colony_file = mod / "game-contract-local/colony-validation/colony-validation.json"
        owner_samples = []
        completion_seen = None
        timed_out = False
        stopped_for_mod_failure = False
        menu_seen = None
        while common.alive(record):
            if args.mode in OWNER_MODES:
                completed_report=optional_json(colony_file)
                if completion_seen is None and completed_report and completed_report.get("Status")=="passed":
                    completion_seen=time.monotonic()
                current_owner = optional_json(colony_file.parent / "colony-owner.json")
                if current_owner and current_owner.get("CompletedFrames", 0) > 0:
                    identity = (current_owner.get("Session"), current_owner["CompletedFrames"])
                    if not owner_samples or identity != (owner_samples[-1]["session"], owner_samples[-1]["frame"]):
                        owner_samples.append(dict(session=identity[0], frame=identity[1], wall=time.monotonic(),
                                                  owner_ms=current_owner["LastAdvanceMilliseconds"]))
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
            if inspection_seconds:
                current_report = optional_json(colony_file)
                if current_report:
                    inspection_started, inspected = common.completed_inspection(
                        current_report, inspection_seconds, inspection_started, time.monotonic())
                    if inspected:
                        break
            if time.monotonic() >= deadline:
                timed_out = True
                break
            time.sleep(.05 if args.mode in OWNER_MODES else 1)
        subprocess.run([str(state / "tools/desktop/usr/bin/scrot"), "-o", str(output / "desktop-final.png")],
                       env=desktop, check=True, timeout=20)
        probe = optional_json(mod / "integration-probe.json")
        colony_file = mod / "game-contract-local/colony-validation/colony-validation.json"
        report = optional_json(colony_file)
        domain = optional_json(colony_file.parent / "native-domain.json")
        owner = optional_json(colony_file.parent / "colony-owner.json")
        if args.mode in OWNER_MODES:
            common.write_json(output / "owner-frame-samples.json", owner_samples)
            result["owner_timing"] = owner_timing(owner_samples)
        observation = optional_json(mod / "native-backend-observation-report.json")
        log = (output / "Player.log").read_text(errors="replace") if (output / "Player.log").exists() else ""
        player_exit = common.CHILDREN[record["pid"]].poll()
        result["completion_to_player_exit_seconds"] = time.monotonic()-completion_seen if completion_seen is not None and player_exit is not None else None
        result["quit_from_menu_requested"] = getattr(args,"quit_from_menu",False)
        result["quit_from_menu_observed"] = "Quit control reached the main menu after its colony." in log
        status, code = common.classify(args.mode, log, probe, report, timed_out, player_exit, (MOD_ID,), PLATFORM)
        if code==0 and getattr(args,"quit_from_menu",False) and not result["quit_from_menu_observed"]:
            status,code="failed-menu-quit-control",2
        domain_ok = args.mode not in OWNER_MODES + FORWARD_MODES and native_domain_passed(domain, observation, args.frames, **domain_options(args))
        if code == 0 and args.mode not in OWNER_MODES + FORWARD_MODES and not domain_ok:
            status, code = "failed-native-domain", 2
        if code==0 and args.mode in OWNER_MODES + FORWARD_MODES and not common.native_observer_passed(manifest,observation):
            status,code="failed-native-observer",2
        owner_ok = args.mode in OWNER_MODES and colony_owner_passed(owner)
        if code == 0 and args.mode in OWNER_MODES and not owner_ok:
            status, code = "failed-colony-owner-evidence", 2
        if args.mode == "colony-owner":
            pipes = (owner or {}).get("PipeSave") or {}
            defaults_ok = (owner or {}).get("OwnerConfigFlags") == 13 and pipes.get("Active") is True and pipes.get("AuditClosed") is True
            result["new_colony_defaults_passed"] = defaults_ok
            if code == 0 and not defaults_ok:
                status, code = "failed-new-colony-defaults", 2
        checkpoint = (domain or {}).get("ColonyCheckpoint") or {}
        # Authenticate the file and export a host path without altering raw
        # Windows game reports. Linux loaders must never guess at C: paths.
        saved_file = (report or {}).get("ColonySavePath") if args.mode in OWNER_MODES else checkpoint.get("SavedFile") or checkpoint.get("InitialAutosaveFile")
        if code == 0 and saved_file:
            saved = host_save_path(saved_file, output)
            result["host_saved_file"] = str(saved)
            result["host_saved_file_sha256"] = common.digest(saved)
            if checkpoint.get("SavedFileSha256") and result["host_saved_file_sha256"] != checkpoint["SavedFileSha256"]:
                status, code = "failed-save-file-hash", 2
        if args.mode in OWNER_MODES:
            result["profile_saves"] = [dict(path=str(host_save_path(windows_path(path), output)),
                sha256=common.digest(path), bytes=path.stat().st_size,
                autosave="auto_save" in path.parts) for path in sorted(profile.rglob("*.sav"))]
        if load_fixture and (common.digest(args.load_save) != source_hash or common.digest(load_fixture) != source_hash):
            status, code = "failed-input-save-mutated", 2
        if common.verify_package(package, "win-x64") != manifest:
            raise ValueError("The immutable source package changed during the game run")
        result.update(status=status, exit_code=code, timed_out=timed_out, observed_player_exit_code=player_exit,
            inspection_seconds_requested=inspection_seconds,
            inspection_seconds_held=time.monotonic()-inspection_started if inspection_started is not None else 0,
            stopped_early_for_mod_failure=stopped_for_mod_failure,
            real_unity_player=bool(probe and probe.get("Platform")==PLATFORM),
            integration_probe=probe, colony_report=report, native_domain=domain,
            colony_owner=owner, colony_owner_passed=owner_ok,
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
    parser.add_argument("--scenario", type=common.scenario_list, help="colony-owner: comma-separated gameplay scenario batch")
    parser.add_argument("--cluster", choices=common.BASE_CLUSTERS, help="colony-owner: base-game starting asteroid (default Terra)")
    parser.add_argument("--speed", type=int, choices=(1, 2, 3), help="Normal, fast or ultra game speed")
    parser.add_argument("--timeout", type=int, choices=range(30, 7201), default=1800, metavar="30..7200")
    parser.add_argument("--inspection-seconds", type=int, choices=range(0, 301), default=0, metavar="0..300",
        help="Keep a completed colony visible for bounded input/visual inspection before normal cleanup")
    parser.add_argument("--save-colony", action="store_true")
    parser.add_argument("--quit-from-menu",action="store_true",help="After the owner control, return to the main menu and quit normally")
    parser.add_argument("--settings-preset",type=Path,help="Explicit non-default colony settings control; staged preset JSON file")
    parser.add_argument("--settings-open",choices=("mods","0","1","2","3"),help="Open the actual settings UI through its existing validation hook")
    parser.add_argument("--load-save", type=Path)
    parser.add_argument("--resave-before-resume", action="store_true")
    parser.add_argument("--reload-in-process", action="store_true")
    parser.add_argument("--verify-natural-autosave",action="store_true",help="Wait for natural day rollover; native-domain and 3100..5000 frames")
    parser.add_argument("--verify-persistent-actors",action="store_true",help="Save three live point actors; gas/water source, source save or natural autosave, and at least eighty frames")
    parser.add_argument("--subnormal-carbon-particle",action="store_true",help="Use the smallest positive binary32 parcel in the original CO2 particle scene")
    parser.add_argument("--verify-transient-particles",action="store_true",help="Save an in-flight original CO2 particle and verify exact restore and one native deposition")
    parser.add_argument("--verify-live-breathing",action="store_true",help="Place a real organic duplicant in the owned gas room, or observe its saved reload; source requires 20–25 frames")
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
