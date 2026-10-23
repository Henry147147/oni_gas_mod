#!/usr/bin/env python3
"""Run licensed Linux ONI on a private, rendered virtual desktop.

Steam authentication stays in Steam. Every game run gets a new disposable
profile; reports never equate a scoped probe with a completed overhaul.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from oni_native_domain_evidence import native_domain_passed
from oni_tile_hover import tile_hover_passed

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_STATE = Path.home() / ".local/share/oni-headless"
DEFAULT_GAME = Path("/mnt/data/share/oni/OxygenNotIncluded-linux")
STEAM_URL = "https://cdn.akamai.steamstatic.com/client/installer/steam.deb"
PROBE_FLAGS = (
    "ManagedFrameIntercepted", "ManagedSnapshotIntercepted", "DirectNativeMessageIntercepted",
    "ManagedSaveIntercepted", "NativeMixtureRoundtripPassed", "SettingsSerializationRoundtripPassed",
)
CHILDREN: dict[int, subprocess.Popen] = {}
# Every command that reads and rewrites session.json or signals recorded
# processes holds this one lock for its whole duration.
LOCK_NAME = "runner.lock"
LOCKED_COMMANDS = ("setup", "start", "stop", "stop-game", "run")
STEAM_BLOCK_MARKERS = ("Steam not initialized in time", "SteamAPI_Init() failed", "SteamAPI_Init failed")
MAIN_MENU_MARKER = "-- MAIN MENU --"
# Logged by MainMenuContractExportPatch (NativeContractCapture.cs) after the
# main-menu marker; success follows RuntimeCoverageCapture's export.
EXPORT_DONE_MARKER = "[Mixed Atmospheres] Native contract captured without native initialization:"
# The second form is the newer warning-level wording; both fail a run.
EXPORT_FAILED_MARKERS = ("[Mixed Atmospheres] Native contract export failed:",
                         "[Mixed Atmospheres] Native contract diagnostics capture failed")
MENU_EXPORT_WAIT_SECONDS = 120
MENU_SETTLE_SECONDS = 5
MOD_IDENTIFIERS = ("Mixed Atmospheres", "MixedAtmospheres")
FAILURE_WORDS = re.compile(r"(?<![\w./\\-])(?:fail(?:ed|ure|ures|ing)?|errors?|[\w.]*exception)\b", re.IGNORECASE)
# Mono "  at Type.Method (...)", "(wrapper ...)" and Unity "Type:Method (...)" frames.
STACK_FRAME = re.compile(r"^\s+at |^\s*\(wrapper |^[\w.`+<>]+:[\w.`+<>|]+ ?\(")
# Mod code, Harmony-generated replacements (DMD<...>) or Harmony itself.
MOD_FRAME = re.compile(r"MixedAtmospheres\.|DMD<|HarmonyLib\.")
HARMONY_FAILURE = re.compile(r"HarmonyException|Patching exception", re.IGNORECASE)
SHUTDOWN_ABORT = "Thread was being aborted"
MANIFEST_REVISION = re.compile(r"[0-9a-f]{40}(?:[0-9a-f]{24})?")
# Mirrors ColonyValidationLimits (ColonyValidationConfiguration.cs). Ten ONI
# cycles are about 30,000 native 0.2 s frames; long runs are an explicit opt-in.
FRAME_LIMIT, LONG_RUN_FRAME_LIMIT, LONG_RUN_DEFAULT_FRAMES = 10000, 100000, 30000
TIMEOUT_LIMIT, LONG_RUN_TIMEOUT_LIMIT = 7200, 48 * 3600
TELEMETRY_INTERVAL_DEFAULT, TELEMETRY_INTERVAL_LIMITS = 300, (10, 10000)
SCENARIOS = ("terrain", "heat", "phase", "freeze-melt", "deoxidizer", "pipes", "door", "sublimation", "falling", "breathing", "airflow",
             "electrolysis", "vent-overpressure", "space-heater", "bottle-emptier", "gas-generator", "germs", "puft",
             "plant", "wheezewort", "life-support", "mask-station", "polymer-press", "oxyfern", "debris-heat", "building-melt", "geyser", "rocket-exhaust", "meteor", "sensor", "filter", "mixed-pipes", "mixed-pipe-filter", "mixed-pipe-boundary", "mixed-pipe-output", "mixed-pipe-pumps", "mixed-pipe-thermal", "mixed-pipe-catalogue", "pacu", "liquid-pump", "base")


# Base-game starting cluster layouts (worldgen/clusters names; identities only).
BASE_CLUSTERS = ("SandstoneDefault", "Oceania", "SandstoneFrozen", "ForestDefault", "ForestLush", "ForestHot", "Volcanic", "Badlands", "Oasis")


def scenario_list(value: str) -> str:
    names = value.split(",")
    if not names or any(name not in SCENARIOS for name in names) or len(set(names)) != len(names):
        raise argparse.ArgumentTypeError("scenarios: comma-separated, distinct, from " + ", ".join(SCENARIOS))
    return value


LONG_RUN_MODES = ("snapshot-copy", "native-observer", "native-domain", "native-liquid-domain", "native-mixed-domain",
                  "native-configured-domain", "native-configured-water-domain", "native-configured-vacuum-domain", "colony-owner", "colony-owner-load",
                  "colony-vanilla")
FLOOR_ARENAS = ("f1", "f2", "f3", "f4", "f5")
TELEMETRY_FILE = "development-telemetry.jsonl"
TELEMETRY_KIND = "mixed-atmospheres-development-telemetry"
# The game writes a telemetry heartbeat at least every 60 s of wall time.
TELEMETRY_STALL_SECONDS = 900
TELEMETRY_PROGRESS_SECONDS = 300
TELEMETRY_FINAL_WAIT_SECONDS = 30
TELEMETRY_CLOCK_CHECK_SECONDS = 30
TELEMETRY_MAXIMUM_BYTES = 64 << 20
RUNNER_GRACE_SECONDS, LONG_RUN_GRACE_SECONDS = 60, 600


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, value: dict) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def identity(pid: int) -> dict:
    # Parse after the final ')' because Linux process names can contain spaces.
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return {"pid": pid, "start_ticks": fields[19], "state": fields[0],
            "process_group": int(fields[2]), "session": int(fields[3])}


def alive(record: dict | None) -> bool:
    if not record:
        return False
    child = CHILDREN.get(record["pid"])
    if child is not None and child.poll() is not None:
        return False
    try:
        current = identity(record["pid"])
        return current["start_ticks"] == record["start_ticks"] and current["state"] != "Z"
    except (FileNotFoundError, ProcessLookupError):
        return False


def process_group_alive(record: dict | None) -> bool:
    """Find live members even when the recorded launcher has already exited."""
    if not record:
        return False
    pid = record["pid"]
    try:
        leader = identity(pid)
        if leader["start_ticks"] != record["start_ticks"]:
            return False  # The PID now names an unrelated process.
        if leader["process_group"] != pid or leader["session"] != pid:
            raise RuntimeError(f"Refusing to signal a process group not owned by the runner: {pid}")
    except (FileNotFoundError, ProcessLookupError):
        pass
    # Linux reserves a process-group ID while its members exist. A launcher
    # can disappear while its game survives in that same session and group.
    for path in Path("/proc").iterdir():
        if not path.name.isdecimal():
            continue
        try:
            member = identity(int(path.name))
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
        if (member["process_group"] == pid and member["session"] == pid and
                int(member["start_ticks"]) >= int(record["start_ticks"]) and member["state"] != "Z"):
            return True
    return False


def stop_process(record: dict | None) -> None:
    """Stop the whole owned group, including children of an exited launcher."""
    if not process_group_alive(record):
        return
    pid = record["pid"]
    try:
        os.killpg(pid, signal.SIGTERM)
        deadline = time.monotonic() + 5
        while process_group_alive(record) and time.monotonic() < deadline:
            time.sleep(.1)
        if process_group_alive(record):
            os.killpg(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass  # Exited between the identity check and the signal.
    child = CHILDREN.get(pid)
    if child is not None:
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass


def _stop_unrecorded_child(process: subprocess.Popen) -> None:
    """Clean up a fresh direct child before publishing its handle or identity."""
    try:
        process.terminate()
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            process.kill()
        except ProcessLookupError:
            pass
        process.wait(timeout=5)


def spawn(arguments: list[str], log: Path, env: dict) -> dict:
    process = None
    try:
        with log.open("ab") as stream:
            process = subprocess.Popen(arguments, stdout=stream, stderr=subprocess.STDOUT,
                                       stdin=subprocess.DEVNULL, env=env, start_new_session=True)
        record = identity(process.pid)
        record["log"] = str(log)
        CHILDREN[process.pid] = process
        return record
    except BaseException as error:
        if process is not None:
            if CHILDREN.get(process.pid) is process:
                del CHILDREN[process.pid]
            try:
                _stop_unrecorded_child(process)
            except BaseException as cleanup_error:
                raise RuntimeError(f"Spawn failed ({error}); unrecorded child {process.pid} "
                                   f"cleanup failed: {cleanup_error}") from error
        raise


def run_checked(arguments: list[str], **kwargs) -> None:
    subprocess.run(arguments, check=True, **kwargs)


@contextmanager
def cleanup_on_termination():
    """Let run-owned process/prefix cleanup execute on a normal SIGTERM."""
    def terminate(signum, frame):
        raise SystemExit(128+signum)
    previous=signal.signal(signal.SIGTERM,terminate)
    try:
        yield
    finally:
        signal.signal(signal.SIGTERM,previous)


@contextmanager
def locked(path: Path):
    with path.open("a") as stream:
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError("Another runner operation is active.") from None
        yield


def read_session(state: Path) -> dict:
    session_file = state / "session.json"
    return read_json(session_file) if session_file.exists() else {}


def desktop_env(state: Path, session: dict, gpu: bool = False) -> dict:
    desktop = state / "tools/desktop/usr"
    env = dict(os.environ, DISPLAY=session["display"], XAUTHORITY=str(session.get("xauthority") or state / "Xauthority"),
               LIBGL_ALWAYS_SOFTWARE="1",
               # Software rendering otherwise takes every core from the fluid
               # solver while the paced game keeps drawing.
               LP_NUM_THREADS=os.environ.get("LP_NUM_THREADS", "4"),
               LD_LIBRARY_PATH=f"{desktop}/lib/x86_64-linux-gnu:{desktop}/lib/i386-linux-gnu",
               IMLIB2_LOADER_PATH=f"{desktop}/lib/x86_64-linux-gnu/imlib2/loaders",
               XDG_DATA_DIRS=f"{desktop}/share:/usr/share")
    env.pop("WAYLAND_DISPLAY", None)
    if gpu:
        # A hardware X display (the NVIDIA driver's GLX): no software
        # rasteriser, and the display's own access control.
        for name in ("LIBGL_ALWAYS_SOFTWARE", "LP_NUM_THREADS", "XAUTHORITY"):
            env.pop(name, None)
    return env


def start_private_display(state: Path, output: Path, *, window_manager: bool = False) -> tuple[dict, str, dict]:
    display = next((f":{number}" for number in range(123, 200)
                    if not Path(f"/tmp/.X11-unix/X{number}").exists()
                    and not Path(f"/tmp/.X{number}-lock").exists()), None)
    if display is None:
        raise RuntimeError("No private X display is available")
    auth = output / "Xauthority"
    auth.touch(mode=0o600)
    subprocess.run(["xauth", "-f", str(auth), "add", display, "MIT-MAGIC-COOKIE-1", secrets.token_hex(16)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    env = desktop_env(state, {"display": display})
    env["XAUTHORITY"] = str(auth)
    record = spawn([str(state / "tools/desktop/usr/bin/Xvfb"), display,
        "-screen", "0", "1280x720x24", "-nolisten", "tcp", "-auth", str(auth), "-noreset"],
        output / "Xvfb.log", env)
    try:
        for _ in range(100):
            if not alive(record):
                raise RuntimeError("Private Xvfb exited; inspect Xvfb.log")
            if Path("/tmp/.X11-unix/X" + display[1:]).exists():
                if window_manager:
                    wm_home = output / "window-manager-home"
                    wm_home.mkdir(mode=0o700)
                    wm_env = dict(env, HOME=str(wm_home), XDG_CONFIG_HOME=str(wm_home / ".config"),
                                  XDG_CACHE_HOME=str(wm_home / ".cache"))
                    record["window_manager"] = spawn([str(state / "tools/desktop/usr/bin/openbox")], output / "openbox.log", wm_env)
                    time.sleep(.2)
                    if not alive(record["window_manager"]):
                        raise RuntimeError("Private Openbox exited; inspect openbox.log")
                return record, display, env
            time.sleep(.1)
        raise TimeoutError("Private Xvfb did not become ready")
    except BaseException:
        stop_private_display(record)
        raise


def stop_private_display(record: dict | None) -> None:
    if record is None:
        return
    try:
        if record.get("window_manager") is not None:
            stop_process(record["window_manager"])
    finally:
        stop_process(record)


@contextmanager
def run_display(state: Path, session: dict, output: Path, private: bool = False, gpu_display: str | None = None):
    record = None
    screen = session
    env = desktop_env(state, session)
    try:
        if gpu_display:
            screen = dict(session, display=gpu_display, xauthority="", gpu=True)
            env = desktop_env(state, screen, gpu=True)
        elif private:
            record, display, env = start_private_display(state, output, window_manager=True)
            screen = dict(session, display=display, xauthority=str(output / "Xauthority"))
        yield screen, env, record
    finally:
        if record is not None:
            stop_private_display(record)


def steam_env(state: Path, session: dict) -> dict:
    env = desktop_env(state, session)
    # HOME is redirected only in this child's environment for Steam's private
    # profile. The calling shell and user's real home are never changed.
    private_home = state / "steam-home"
    env.update(HOME=str(private_home), XDG_CONFIG_HOME=str(private_home / ".config"),
               XDG_DATA_HOME=str(private_home / ".local/share"),
               XDG_CACHE_HOME=str(private_home / ".cache"), XDG_RUNTIME_DIR=str(state / "runtime"),
               PRESSURE_VESSEL_BWRAP="/usr/bin/bwrap", STEAM_RUNTIME_BWRAP="/usr/bin/bwrap",
               LIBGL_DRIVERS_PATH=f"/usr/lib/x86_64-linux-gnu/dri:{state}/tools/desktop/usr/lib/i386-linux-gnu/dri")
    return env


def setup(state: Path) -> None:
    """Extract distro packages locally; never invoke privileged installation."""
    tools = state / "tools"
    debs = tools / "debs"
    debs.mkdir(parents=True, exist_ok=True)
    packages = ["xvfb", "xserver-common", "scrot", "xdotool", "openbox", "x11vnc", "novnc",
                "libgl1:i386", "libdrm2:i386", "libglx-mesa0:i386"]
    plan = subprocess.check_output(["apt-get", "-s", "--no-install-recommends", "install", *packages], text=True)
    required = [line.split()[1] + "=" + line.split("(", 1)[1].split()[0]
                for line in plan.splitlines() if line.startswith("Inst ")]
    # Include requested packages even if installed: their local binaries remain
    # reproducible and don't rely on a later host package removal.
    run_checked(["apt-get", "download", *packages, *required], cwd=debs)
    for package in sorted(debs.glob("*.deb")):
        run_checked(["dpkg-deb", "-x", str(package), str(tools / "desktop")])
    bootstrap = tools / "steam.deb"
    if not bootstrap.exists():
        run_checked(["curl", "-fL", "--max-time", "120", STEAM_URL, "-o", str(bootstrap)])
    run_checked(["dpkg-deb", "-x", str(bootstrap), str(tools / "steam-root")])
    steam = state / "steam-home/.local/share/Steam"
    steam.mkdir(parents=True, exist_ok=True)
    if not (steam / "steam.sh").exists():
        run_checked(["tar", "xf", str(tools / "steam-root/usr/lib/steam/bootstraplinux_ubuntu12_32.tar.xz"),
                     "-C", str(steam)])
    steam_links = state / "steam-home/.steam"
    steam_links.mkdir(exist_ok=True)
    for name, target in (("steam", steam), ("root", steam), ("sdk32", steam / "linux32"), ("sdk64", steam / "linux64")):
        link = steam_links / name
        if not link.exists() and not link.is_symlink():
            link.symlink_to(target)
    venv = tools / "web-venv"
    if not (venv / "bin/python").exists():
        run_checked([sys.executable, "-m", "venv", str(venv)])
    if not (venv / "bin/websockify").exists():
        run_checked([str(venv / "bin/pip"), "install", "websockify"])
    write_json(tools / "setup-evidence.json", {
        "steam_bootstrap_url": STEAM_URL, "steam_bootstrap_sha256": digest(bootstrap),
        "packages": {p.name: digest(p) for p in sorted(debs.glob("*.deb"))},
        "privileged_install": False,
    })
    print(f"Local tools ready: {tools}")


def start(state: Path) -> None:
    session_file = state / "session.json"
    session = read_json(session_file) if session_file.exists() else {}
    for relative in ("runtime", "steam-home/.config", "steam-home/.cache"):
        (state / relative).mkdir(parents=True, exist_ok=True)
    os.chmod(state / "runtime", 0o700)
    desktop = state / "tools/desktop/usr"
    if not (desktop / "bin/Xvfb").exists():
        raise RuntimeError("Run 'setup' first.")
    try:
        if not alive(session.get("xvfb")):
            display = next(n for n in range(121, 200) if not Path(f"/tmp/.X11-unix/X{n}").exists()
                           and not Path(f"/tmp/.X{n}-lock").exists())
            session["display"] = f":{display}"
            run_checked(["xauth", "-f", str(state / "Xauthority"), "add", session["display"],
                         "MIT-MAGIC-COOKIE-1", secrets.token_hex(16)], stdout=subprocess.DEVNULL)
            env = desktop_env(state, session)
            session["xvfb"] = spawn([str(desktop / "bin/Xvfb"), session["display"], "-screen", "0",
                                     "1280x720x24", "-nolisten", "tcp", "-auth", str(state / "Xauthority"),
                                     "-noreset"], state / "Xvfb.log", env)
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if not alive(session["xvfb"]):
                    raise RuntimeError("Xvfb exited; inspect Xvfb.log.")
                if subprocess.run(["xdpyinfo"], env=env, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL).returncode == 0:
                    break
                time.sleep(.1)
            else:
                raise RuntimeError("Virtual display did not become ready.")
        env = desktop_env(state, session)
        commands = {
            "wm": [str(desktop / "bin/openbox"), "--sm-disable"],
            "steam": ["dbus-run-session", "--", str(state / "steam-home/.local/share/Steam/steam.sh"),
                      "-console", "-cef-disable-gpu"],
            "vnc": [str(desktop / "bin/x11vnc"), "-display", session["display"], "-auth", str(state / "Xauthority"),
                    "-localhost", "-rfbport", "5907", "-nopw", "-forever", "-shared", "-noxdamage"],
            "web": [str(state / "tools/web-venv/bin/websockify"), "--web", str(desktop / "share/novnc"),
                    "127.0.0.1:6080", "127.0.0.1:5907"],
        }
        for name, arguments in commands.items():
            if not alive(session.get(name)):
                session[name] = spawn(arguments, state / f"{name}.log", steam_env(state, session) if name == "steam" else env)
                time.sleep(.2)
                if not alive(session[name]):
                    raise RuntimeError(f"{name} exited during startup; inspect {name}.log.")
    finally:
        write_json(session_file, session)
    print("Desktop: http://127.0.0.1:6080/vnc.html?autoconnect=true&resize=scale")
    print("Sign in to Steam with the account that owns ONI. 'status' reports process health, not authentication.")


def verify_package(package: Path, platform: str = "linux-x64") -> dict:
    libraries={"linux-x64":"native/linux-x64/libmixed_atmospheres.so",
               "win-x64":"native/win-x64/mixed_atmospheres.dll"}
    if platform not in libraries:
        raise ValueError("Expected a supported Linux or Windows x64 probe platform")
    package = package.resolve()
    manifest = read_json(package / "package-manifest.json")
    if manifest.get("kind") != "integration-probe" or manifest.get("release_ready") is not False:
        raise ValueError("Supply a private integration probe package.")
    revision = manifest.get("source_revision")
    if (not isinstance(revision, str) or not MANIFEST_REVISION.fullmatch(revision)
            or not isinstance(manifest.get("source_dirty"), bool)):
        raise ValueError("The package manifest does not record source_revision/source_dirty; "
                         "repackage it with tools/package_probe.py.")
    for relative, expected in manifest["files"].items():
        file = (package / relative).resolve()
        if not file.is_relative_to(package) or not file.is_file() or digest(file) != expected:
            raise ValueError(f"Package hash or path mismatch: {relative}")
    required = {"MixedAtmospheres.Oni.dll", "MixedAtmospheres.Core.dll", libraries[platform]}
    if not required.issubset(manifest["files"]):
        raise ValueError("The package must include pinned " + ("Linux" if platform=="linux-x64" else "Windows") + " native and managed libraries.")
    actual = {p.relative_to(package).as_posix() for p in package.rglob("*") if p.is_file()
              and p != package / "package-manifest.json"}
    if actual != set(manifest["files"]) or any(p.is_symlink() for p in package.rglob("*")):
        raise ValueError("The package contains unpinned files or symlinks; create a fresh package.")
    if any(p.name in ("colony-validation.json", "integration-probe.json", "game-contract-local")
           for p in package.iterdir()):
        raise ValueError("Use a clean package, without previous configuration or runtime reports.")
    return manifest


def stage_package(package: Path, mod: Path, manifest: dict, platform: str = "linux-x64") -> None:
    """Authenticate the launch copy against the already verified source manifest."""
    shutil.copytree(package, mod, symlinks=True)
    copied_manifest = verify_package(mod,platform)
    if copied_manifest != manifest:
        raise ValueError("Copied package manifest differs from the verified source manifest.")


def mono_label_version(value: str) -> int:
    # KMod's local labels use this pinned Mono string hash.
    first = second = 5381
    for index, char in enumerate(value):
        if index % 2:
            second = ((second * 33) ^ ord(char)) & 0xffffffff
        else:
            first = ((first * 33) ^ ord(char)) & 0xffffffff
    result = (first + second * 1566083941) & 0xffffffff
    return result - 2**32 if result >= 2**31 else result


def steam_blocked(log: str) -> bool:
    return any(marker in log for marker in STEAM_BLOCK_MARKERS)


def probe_passed(probe: dict | None, expected_platform: str = "LinuxPlayer") -> bool:
    return bool(probe) and probe.get("Platform") == expected_platform and all(probe.get(flag) is True for flag in PROBE_FLAGS)


def contract_export(log: str) -> str | None:
    """Return 'completed', 'failed' or None for the mod's export after the main-menu marker."""
    menu = log.find(MAIN_MENU_MARKER)
    if menu < 0:
        return None
    after = log[menu:]
    if any(marker in after for marker in EXPORT_FAILED_MARKERS):
        return "failed"
    return "completed" if EXPORT_DONE_MARKER in after else None


def mod_failure_lines(log: str, identifiers: tuple[str, ...] = ()) -> list[str]:
    """Return Player.log lines showing a mod-originated error, exception or Harmony failure.

    A line fails when it names the mod and says fail/error/exception, or is a
    Harmony patching exception. An exception header fails when its stack has a
    mod, Harmony or Harmony-patched (DMD<...>) frame. Game-only lines are
    ignored, as is a worker exception whose own header says "Thread was being
    aborted". A nearby abort never suppresses a mod or Harmony failure.
    """
    names = MOD_IDENTIFIERS + tuple(name for name in identifiers if name)
    lines = log.splitlines()
    found: dict[int, str] = {}
    header = None
    attributed: set[int] = set()
    for index, line in enumerate(lines):
        if STACK_FRAME.match(line):
            if (header is not None and header not in attributed and MOD_FRAME.search(line)
                    and "exception" in lines[header].lower() and SHUTDOWN_ABORT not in lines[header]):
                attributed.add(header)
                found[header] = lines[header]
                found[index] = line
            continue
        header = index
        if HARMONY_FAILURE.search(line) or (any(name in line for name in names) and FAILURE_WORDS.search(line)):
            found[index] = line
    return [found[index].strip()[:500] for index in sorted(found)]


def classify(mode: str, log: str, probe: dict | None, report: dict | None, timed_out: bool,
             player_exit: int | None = None, identifiers: tuple[str, ...] = (),
             expected_platform: str = "LinuxPlayer") -> tuple[str, int]:
    # Positive evidence of a mod failure outranks the Steam block so it is never
    # hidden; result.json records steam_blocked separately.
    if "[Mixed Atmospheres] Integration probe failed:" in log or "[Mixed Atmospheres] Colony validation failed:" in log:
        return "failed-runtime-validation", 2
    if mod_failure_lines(log, identifiers):
        return "failed-mod-log-errors", 2
    if probe is not None and not probe_passed(probe, expected_platform):
        return "failed-integration-probe", 2
    if steam_blocked(log):
        return "blocked-steam-authentication", 3
    if timed_out:
        return "timeout", 2
    if player_exit not in (None, 0):
        return "failed-game-exit", 2
    if not probe_passed(probe, expected_platform):
        return "failed-integration-probe", 2
    if MAIN_MENU_MARKER not in log:
        return "failed-before-main-menu", 2
    if contract_export(log) != "completed":
        return "failed-main-menu-contract-export", 2
    if mode == "menu":
        return "passed-scoped-main-menu", 0
    if not report or report.get("Status") not in ("passed", "failed"):
        return "missing-completed-colony-report", 2
    if report["Status"] == "failed":
        return "failed-colony-validation", 2
    # A vanilla-load check passes when ONI's own loader refused the save.
    if mode == "colony-owner-load" and report.get("Stage") == "vanilla-load-refused":
        return "passed-vanilla-load-refused", 0 if report.get("VanillaLoadResult") == -1 else 2
    if report.get("GameStarted") is not True:
        return "failed-colony-not-started", 2
    return "passed-" + mode, 0


def unsupported_mode(mode: str, platform: str | None = None) -> str | None:
    if mode == "load-rejection" and (platform or sys.platform).startswith("linux"):
        return ("load-rejection cannot run on Linux: the mod's owned load boundary requires the Windows "
                "Assembly-CSharp and Assembly-CSharp-firstpass SHA-256 hashes (OwnedReservoirLoadBoundary.cs, "
                "OwnedReservoirLoadValidationCounters.cs), which the Linux assemblies do not match.")
    return None


def sandbox_command(home: Path, state: Path, output: Path, game: Path, thin: Path, keep_open: bool) -> list[str]:
    """Return the bwrap command for one game run.

    The root and the private tool/Steam tree are read-only; a fresh home, XDG
    roots and the evidence directory are writable. PID namespace and /proc stay
    the host's: Steamworks reads ~/.steam/steam.pid and checks that process,
    and steamclient.so reads its /proc entries. /dev (Steam's /dev/shm IPC
    objects) and /tmp are shared. Steam's own initialization and ownership
    checks are unchanged.
    """
    arguments = ["bwrap", "--ro-bind", "/", "/", "--bind", str(home), str(Path.home()),
                 "--ro-bind", str(state), str(state), "--bind", str(output), str(output),
                 "--bind", "/tmp", "/tmp", "--bind", "/proc", "/proc", "--dev-bind", "/dev", "/dev",
                 "--ro-bind", str(game), str(game),
                 "--chdir", str(thin),
                 "--setenv", "HOME", str(Path.home()),
                 "--setenv", "XDG_CONFIG_HOME", str(home / ".config"),
                 "--setenv", "XDG_DATA_HOME", str(home / ".local/share"),
                 "--setenv", "XDG_CACHE_HOME", str(home / ".cache"),
                 "--setenv", "XDG_RUNTIME_DIR", str(output / "runtime"),
                 "--unsetenv", "DBUS_SESSION_BUS_ADDRESS",
                 str(thin / "OxygenNotIncluded"), "-force-glcore", "-screen-fullscreen", "0",
                 "-screen-width", "1280", "-screen-height", "720", "-logFile", str(output / "Player.log")]
    if not keep_open:
        arguments[1:1] = ["--die-with-parent"]
    return arguments


def git_output(*arguments: str) -> str | None:
    try:
        checkout=subprocess.check_output(["git","rev-parse","--show-toplevel"],cwd=ROOT,text=True,stderr=subprocess.DEVNULL)
        # A frozen archive may live under a changing checkout. That enclosing
        # repository cannot identify the archived runner's source revision.
        if Path(checkout.strip()).resolve()!=ROOT.resolve():
            return None
        return subprocess.check_output(["git", *arguments], cwd=ROOT, text=True, stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        return None


def revision_fields(manifest: dict) -> dict:
    """The package's recorded source tree, kept separate from the runner's checkout."""
    head = git_output("rev-parse", "HEAD")
    return {"source_revision": manifest["source_revision"], "source_dirty": manifest["source_dirty"],
            "source_status": manifest.get("source_status"),
            "runner_git_revision": head.strip() if head else None,
            "runner_git_status": git_output("status", "--short")}


def player_renderer(output: Path) -> str | None:
    """The renderer Unity reported in Player.log ("llvmpipe ..." or the GPU)."""
    try:
        match = re.search(r"^Renderer: (.+)$", (output / "Player.log").read_text(errors="replace"), re.M)
    except OSError:
        return None
    return match.group(1).strip() if match else None


def screenshot(state: Path, session: dict, path: Path) -> None:
    run_checked([str(state / "tools/desktop/usr/bin/scrot"), "-o", str(path)], env=desktop_env(state, session, session.get("gpu") is True))


def native_observer_passed(manifest: dict, report: dict | None) -> bool:
    if not manifest.get("native_observer_enabled", False):
        return True
    if not isinstance(report, dict):
        return False
    reading = report.get("Observation", {})
    return (report.get("Status") == "observing" and report.get("OriginalNativePhysicsActive") is True
            and report.get("AuthoritativeFluidOwner") is False and reading.get("ForwardingOnly") is True
            and reading.get("Attached") is True and reading.get("RecoveryRequired") is False
            and reading.get("AuthoritativeFluidOwner") is False and reading.get("OniBuild") == 744825)


def run_limits_error(args) -> str | None:
    """Return the parser error for frame/timeout/long-run limits, or None."""
    long_run = getattr(args, "long_run", False) is True
    interval = getattr(args, "telemetry_interval_frames", None)
    frames, timeout = (LONG_RUN_FRAME_LIMIT, LONG_RUN_TIMEOUT_LIMIT) if long_run else (FRAME_LIMIT, TIMEOUT_LIMIT)
    if type(args.frames) is not int or not 1 <= args.frames <= frames:
        return f"--frames requires 1..{frames}" + ("" if long_run else f"; up to {LONG_RUN_FRAME_LIMIT} requires --long-run")
    if type(args.timeout) is not int or not 30 <= args.timeout <= timeout:
        return f"--timeout requires 30..{timeout} seconds" + ("" if long_run else f"; up to {LONG_RUN_TIMEOUT_LIMIT} requires --long-run")
    if interval is not None and not long_run:
        return "--telemetry-interval-frames requires --long-run"
    if interval is not None and (type(interval) is not int or not TELEMETRY_INTERVAL_LIMITS[0] <= interval <= TELEMETRY_INTERVAL_LIMITS[1]):
        return "--telemetry-interval-frames requires {}..{} native frames".format(*TELEMETRY_INTERVAL_LIMITS)
    if long_run and args.mode not in LONG_RUN_MODES:
        return "--long-run admits only " + ", ".join(LONG_RUN_MODES)
    fixtures = ("save_colony", "resave_before_resume", "verify_save_io_recovery", "verify_natural_autosave", "verify_persistent_actors",
                "verify_live_breathing", "verify_transient_particles", "subnormal_carbon_particle", "reload_in_process")
    # A colony-owner long run may start from a prepared save (the ten-cycle run).
    if long_run and (any(getattr(args, name, False) is True for name in fixtures) or
                     (getattr(args, "load_save", None) and args.mode != "colony-owner-load")):
        return "--long-run excludes fixed-window save, reload, actor, breathing and particle controls"
    return None


def colony_configuration(args, name: str, load_fixture: Path | None, inspection_seconds: int) -> dict:
    """The private colony-validation.json request; short runs keep their original keys."""
    configuration = {
        "Mode": args.mode, "AutoStartNewColony": True, "ReuseGeneratedWorld": False,
        "CompletedFrames": args.frames, "TimeoutSeconds": args.timeout,
        "QuitWhenComplete": not args.keep_open and not inspection_seconds, "Seed": "147147", "ColonyName": name,
        "PrivateOwnedLoadRejection": args.mode == "load-rejection",
        "LoadSavePath": str(load_fixture) if load_fixture else "",
        "SaveOnComplete": bool(getattr(args, "save_colony", False)),
        "SaveBeforeResume": bool(getattr(args, "resave_before_resume", False)),
        **({"VerifyNaturalAutosave": True} if getattr(args,"verify_natural_autosave",False) is True else {}),
        **({"VerifyPersistentActors": True} if getattr(args,"verify_persistent_actors",False) is True else {}),
        **({"VerifyLiveBreathing": True} if getattr(args,"verify_live_breathing",False) is True else {}),
        **({"VerifyTransientParticles": True} if getattr(args,"verify_transient_particles",False) is True else {}),
        **({"SubnormalCarbonParticle": True} if getattr(args,"subnormal_carbon_particle",False) is True else {}),
        **({"VerifySaveIoRecovery": True} if getattr(args, "verify_save_io_recovery", False) else {}),
        **({"ReloadInProcess": True} if getattr(args, "reload_in_process", False) else {}),
        **({"Scenario": args.scenario} if getattr(args, "scenario", None) else {}),
        **({"Cluster": "clusters/" + args.cluster} if isinstance(getattr(args, "cluster", None), str) else {}),
        **({"OverwriteSave": True} if getattr(args, "overwrite_save", False) is True else {}),
        **({"VanillaLoad": True} if getattr(args, "vanilla_load", False) is True else {}),
        **({"Speed": args.speed} if isinstance(getattr(args, "speed", None), int) else {}),
        **({"Floor": args.floor} if isinstance(getattr(args, "floor", None), str) else {}),
    }
    if getattr(args, "long_run", False) is True:
        interval = getattr(args, "telemetry_interval_frames", None)
        configuration.update(LongRun=True, TelemetryIntervalFrames=TELEMETRY_INTERVAL_DEFAULT if interval is None else interval)
    return configuration


class TelemetryWatch:
    """Host-clock watchdog for a long run's append-only development telemetry.

    The run stalls when the file stops growing, or when a started game's clock
    stops advancing: a modal pause still emits zero-time native frames and
    therefore telemetry lines.
    """

    def __init__(self, path: Path, started: float, stall_seconds: int = TELEMETRY_STALL_SECONDS):
        self.path, self.stall_seconds = path, stall_seconds
        self.size, self.changed = -1, started
        self.clock, self.clock_changed = None, started
        self.reason: str | None = None

    def poll(self, now: float, line: dict | None = None) -> bool:
        """Record growth and, given the latest line, game-clock progress; True once stalled."""
        try:
            size = self.path.stat().st_size
        except OSError:
            size = -1
        if size != self.size:
            self.size, self.changed = size, now
        if line is not None:
            game = line.get("Game") or {}
            clock = game.get("TimeSeconds")
            if game.get("Started") is not True or type(clock) not in (int, float):
                self.clock, self.clock_changed = None, now
            elif clock != self.clock:
                self.clock, self.clock_changed = clock, now
        self.reason = ("telemetry file stopped growing" if now - self.changed >= self.stall_seconds else
                       "game clock stopped advancing" if self.clock is not None and now - self.clock_changed >= self.stall_seconds else None)
        return self.reason is not None


def last_telemetry_line(path: Path, limit: int = 1 << 16) -> dict | None:
    """The last complete telemetry object, reading only the file's tail."""
    try:
        with path.open("rb") as stream:
            stream.seek(0, os.SEEK_END)
            size = stream.tell()
            stream.seek(max(0, size - limit))
            tail = stream.read()
    except OSError:
        return None
    for line in reversed(tail.split(b"\n")):
        try:
            value = json.loads(line)
        except (ValueError, UnicodeDecodeError):
            continue
        if isinstance(value, dict) and value.get("Kind") == TELEMETRY_KIND:
            return value
    return None


def telemetry_final(path: Path) -> bool:
    line = last_telemetry_line(path)
    return line is not None and line.get("Reason") == "final"


def telemetry_progress(line: dict) -> str:
    """One compact host-side progress line for a running long run."""
    game, domain, latency, memory = (line.get(key) or {} for key in ("Game", "NativeDomain", "Latency", "Memory"))
    wait = (latency.get("NativeFrameWaitMs") or {}).get("P95")
    return (f"long-run telemetry #{line.get('Sequence')} {line.get('Reason')}: frames={line.get('NativeFramesProcessed')} "
            f"cycle={game.get('Cycle')} paused={game.get('Paused')} duplicants={game.get('Duplicants')} "
            f"domain_frames={domain.get('ValidationFrames')} state={domain.get('State')} "
            f"wait_p95_ms={wait} rss={memory.get('ResidentBytes')} heap={memory.get('ManagedHeapBytes')}")


def telemetry_summary(path: Path) -> dict:
    """Bounded review of the development telemetry file; never a performance verdict."""
    summary = {"path": str(path), "present": path.is_file(), "lines": 0, "sessions": [], "reasons": {}, "invalid_lines": 0,
               "final_line": False, "first": None, "last": None, "maxima": {}, "development_only": True, "error": None}
    if not summary["present"]:
        return summary
    if path.stat().st_size > TELEMETRY_MAXIMUM_BYTES:
        summary["error"] = f"telemetry exceeds {TELEMETRY_MAXIMUM_BYTES} bytes"
        return summary
    maxima: dict[str, float] = {}

    def keep(key: str, value) -> None:
        if type(value) in (int, float) and (key not in maxima or value > maxima[key]):
            maxima[key] = value

    with path.open("rb") as stream:
        for raw in stream:
            try:
                line = json.loads(raw)
            except (ValueError, UnicodeDecodeError):
                summary["invalid_lines"] += 1
                continue
            if not isinstance(line, dict) or line.get("Kind") != TELEMETRY_KIND or line.get("DevelopmentOnly") is not True:
                summary["invalid_lines"] += 1
                continue
            summary["lines"] += 1
            summary["first"] = summary["first"] or line
            summary["last"] = line
            if line.get("Session") not in summary["sessions"]:
                summary["sessions"].append(line.get("Session"))
            reason = str(line.get("Reason"))
            summary["reasons"][reason] = summary["reasons"].get(reason, 0) + 1
            summary["final_line"] = summary["final_line"] or reason == "final"
            for name, value in (line.get("Latency") or {}).items():
                if isinstance(value, dict):
                    keep(name + ".Max", value.get("Max"))
                    keep(name + ".P95", value.get("P95"))
            memory, domain = line.get("Memory") or {}, line.get("NativeDomain") or {}
            for key in ("ResidentBytes", "PeakResidentBytes", "ManagedHeapBytes"):
                keep("Memory." + key, memory.get(key))
            for key in ("MaximumRelativeMassError", "MaximumRelativeEnergyError", "RejectedAttempts", "IntervalRejectedAttempts"):
                keep("NativeDomain." + key, domain.get(key))
    summary["maxima"] = maxima
    return summary


def long_run_telemetry_passed(summary: dict, report: dict | None) -> bool:
    """A long run must leave one complete, error-free telemetry session ending in a final line."""
    last = summary.get("last") or {}
    return (summary.get("present") is True and summary.get("error") is None and summary.get("invalid_lines") == 0
            and summary.get("lines", 0) >= 2 and len(summary.get("sessions", [])) == 1 and summary.get("final_line") is True
            and last.get("Reason") == "final" and isinstance(report, dict) and report.get("LongRun") is True
            and report.get("DevelopmentTelemetryError") == "" and report.get("DevelopmentTelemetryTruncated") is False
            and report.get("DevelopmentTelemetryLines") == summary.get("lines"))


def completed_inspection(report: dict, seconds: int, started: float | None, now: float) -> tuple[float | None, bool]:
    """Hold only a completed scene, measured by the host's monotonic clock."""
    if report.get("Status") == "failed":
        return started, True
    if report.get("Status") != "passed":
        return None, False
    started = now if started is None else started
    return started, now - started >= seconds



def playtest(args, state: Path, session: dict) -> int:
    private_display = getattr(args, "private_display", False) is True
    inspection_seconds = getattr(args, "inspection_seconds", 0)
    if type(inspection_seconds) is not int or not 0 <= inspection_seconds <= 300 or (inspection_seconds and (args.keep_open or args.mode == "menu")):
        raise ValueError("Completed inspection requires 1..300 seconds, a colony mode and automatic cleanup.")
    if private_display and args.keep_open:
        raise ValueError("A private test display requires automatic game cleanup; omit --keep-open.")
    gpu_display = getattr(args, "gpu_display", None)
    if gpu_display and private_display:
        raise ValueError("--gpu-display and --private-display are alternatives.")
    package = args.package.resolve()
    manifest = verify_package(package)
    if args.mode in ("native-observer", "colony-owner", "colony-owner-load", "colony-vanilla", "native-configured-domain", "native-configured-water-domain", "native-configured-vacuum-domain", "native-domain", "native-solid-heat-domain", "native-mixed-domain", "native-mixed-heat-domain", "native-viscous-domain", "native-import-domain", "native-liquid-import-domain", "native-liquid-domain", "native-liquid-transfer-domain", "native-heat-domain", "native-liquid-heat-domain", "native-transfer-domain", "native-radius-domain", "native-rectangular-domain", "native-actor-domain", "native-liquid-actor-domain", "native-carbon-domain", "native-carbon-particle-domain", "native-breathing-read-domain", "native-pressure-read-domain", "native-liquid-pressure-read-domain", "native-conduction-domain", "native-gravity-domain", "native-liquid-gravity-domain", "native-checkpoint-domain", "native-paired-checkpoint-domain", "native-save-domain", "native-load-domain") and not manifest.get("native_observer_enabled", False):
        raise ValueError("Native integration modes require a package built with --native-observer")
    game = args.game_dir.resolve()
    if (game / "OxygenNotIncluded").read_bytes()[:4] != b"\x7fELF":
        raise ValueError("Supply the native Linux ONI installation.")
    if not alive(session.get("steam")) or not alive(session.get("xvfb")):
        raise RuntimeError("Run 'start' and sign in to Steam first.")
    if process_group_alive(session.get("game")):
        raise RuntimeError("A previous retained game is running; use 'stop-game' first.")
    if alive(session.get("game_display")):
        stop_private_display(session["game_display"])
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + secrets.token_hex(3)
    output = (args.output or state / "runs" / run_id).resolve()
    if output.exists():
        raise ValueError("Use a fresh output directory.")
    if output.is_relative_to(game):
        raise ValueError("Keep evidence outside the licensed game installation.")
    output.mkdir(parents=True, mode=0o700)
    home = output / "home"
    home.mkdir()
    # ~/.steam/{steam.pid,root,steam,sdk64} resolve to the private Steam install
    # through the read-only state bind; libsteam_api finds the client here.
    (home / ".steam").symlink_to(state / "steam-home/.steam")
    profile = home / ".config/unity3d/Klei/Oxygen Not Included"
    name = "MA-Linux-" + run_id
    mod_id = getattr(args, "mod_id", None) or "MixedAtmospheres.IntegrationProbe"
    if not isinstance(mod_id, str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", mod_id) or mod_id in (".", ".."):
        raise ValueError("Test mod ID must be one safe local directory name")
    mod = profile / "mods/Dev" / mod_id
    stage_package(package, mod, manifest)
    write_json(profile / "mods/mods.json", {"version": 1, "mods": [{
        "label": {"distribution_platform": "Dev", "id": mod_id, "title": "Mixed Atmospheres - Integration Probe",
                  "version": mono_label_version(mod_id)}, "status": 1, "enabled": True,
        "enabledForDlc": ["", "EXPANSION1_ID"], "crash_count": 0, "reinstall_path": None,
        "staticID": "MixedAtmospheres.IntegrationProbe"}]})
    load_fixture = None
    load_save = getattr(args, "load_save", None)
    if load_save:
        original_hash = digest(load_save)
        load_fixture = output / "input-colony.sav"
        shutil.copy2(load_save, load_fixture)
        if digest(load_fixture) != original_hash or digest(load_save) != original_hash:
            raise ValueError("Source colony save changed while staging the private input")
    if args.mode != "menu":
        write_json(mod / "colony-validation.json", colony_configuration(args, name, load_fixture, inspection_seconds))
    thin = output / "game"
    thin.mkdir()
    for file in game.iterdir():
        if file.name == "OxygenNotIncluded":
            shutil.copy2(file, thin / file.name)
            (thin / file.name).chmod(0o755)
        else:
            (thin / file.name).symlink_to(file)
    # Standard Steamworks development launch identifier. Steam still checks
    # the running client's account/license; this is not an authentication bypass.
    (thin / "steam_appid.txt").write_text("457140\n")
    with run_display(state, session, output, private_display, gpu_display) as (screen, env, display_record):
        env.update(SteamAppId="457140", SteamGameId="457140")
        # ONI's Linux root is Unity persistentDataPath. Bind a fresh home and set
        # all XDG roots; the actual user's saves and mods are never writable.
        arguments = sandbox_command(home, state, output, game, thin, args.keep_open)
        (output / "runtime").mkdir(mode=0o700)
        record = spawn(arguments, output / "console.log", env)
        retain = False
        try:
            session.update(game=record, game_display=display_record, latest_run=str(output))
            write_json(state / "session.json", session)
            print(f"Playtest PID {record['pid']}; evidence: {output}", flush=True)
            long_run = getattr(args, "long_run", False) is True
            deadline = time.monotonic() + args.timeout + (LONG_RUN_GRACE_SECONDS if long_run else RUNNER_GRACE_SECONDS) + inspection_seconds
            probe_file = mod / "integration-probe.json"
            colony_file = mod / "game-contract-local/colony-validation/colony-validation.json"
            telemetry_file = colony_file.parent / TELEMETRY_FILE
            watch = TelemetryWatch(telemetry_file, time.monotonic()) if long_run else None
            next_progress = time.monotonic() + TELEMETRY_PROGRESS_SECONDS
            next_clock_check = time.monotonic()
            timed_out = telemetry_stalled = False
            menu_seen = completed_seen = None
            inspection_started = None
            while alive(record):
                # Only the menu gate reads Player.log while running; a long
                # colony log is read once after the game exits.
                log = (output / "Player.log").read_text(errors="replace") if args.mode == "menu" and (output / "Player.log").exists() else ""
                if args.mode == "menu" and MAIN_MENU_MARKER in log:
                    # The mod's contract export runs after the marker. Wait for its
                    # own completion/failure line (bounded), then let trailing
                    # exception and stack lines reach Player.log.
                    menu_seen = time.monotonic() if menu_seen is None else menu_seen
                    if contract_export(log) or time.monotonic() - menu_seen >= MENU_EXPORT_WAIT_SECONDS:
                        time.sleep(MENU_SETTLE_SECONDS)
                        break
                if watch is not None:
                    line = None
                    if time.monotonic() >= next_clock_check:
                        next_clock_check = time.monotonic() + TELEMETRY_CLOCK_CHECK_SECONDS
                        line = last_telemetry_line(telemetry_file)
                    if watch.poll(time.monotonic(), line):
                        timed_out = telemetry_stalled = True
                        break
                    if time.monotonic() >= next_progress:
                        next_progress = time.monotonic() + TELEMETRY_PROGRESS_SECONDS
                        if (line := line or last_telemetry_line(telemetry_file)) is not None:
                            print(telemetry_progress(line), flush=True)
                if (args.keep_open or inspection_seconds) and colony_file.exists():
                    try:
                        current_report = read_json(colony_file)
                        if args.keep_open and current_report.get("Status") in ("passed", "failed"):
                            # A retained long run first records its final
                            # telemetry line on the next Unity frame (bounded).
                            completed_seen = time.monotonic() if completed_seen is None else completed_seen
                            if not long_run or telemetry_final(telemetry_file) or time.monotonic() - completed_seen >= TELEMETRY_FINAL_WAIT_SECONDS:
                                time.sleep(2 if long_run else 0)
                                break
                        if inspection_seconds:
                            inspection_started, inspected = completed_inspection(current_report, inspection_seconds, inspection_started, time.monotonic())
                            if inspected:
                                break
                    except json.JSONDecodeError:
                        pass  # Unity may currently be writing the report.
                if time.monotonic() >= deadline:
                    timed_out = True
                    break
                time.sleep(1)
            screenshot(state, screen, output / "desktop-final.png")
            probe = read_json(probe_file) if probe_file.exists() else None
            report = read_json(colony_file) if colony_file.exists() else None
            log = (output / "Player.log").read_text(errors="replace") if (output / "Player.log").exists() else ""
            player_exit = CHILDREN[record["pid"]].poll()
            status, code = classify(args.mode, log, probe, report, timed_out, player_exit, (mod_id,))
            observer_file = mod / "native-backend-observation-report.json"
            try:
                observer_report = read_json(observer_file) if observer_file.exists() else None
            except (OSError, json.JSONDecodeError):
                observer_report = None
            observer_ok = native_observer_passed(manifest, observer_report)
            domain_report = None
            domain_ok = False
            hover_report = None
            hover_ok = False
            if args.mode == "tile-hover":
                try:
                    hover_report = read_json(colony_file.parent / "tile-hover-contents.json")
                except (OSError, json.JSONDecodeError):
                    pass
                hover_ok = tile_hover_passed(hover_report, colony_file.parent)
                if code == 0 and not hover_ok:
                    status, code = "failed-tile-hover-evidence", 2
            if args.mode in ("native-configured-domain", "native-configured-water-domain", "native-configured-vacuum-domain", "native-domain", "native-solid-heat-domain", "native-mixed-domain", "native-mixed-heat-domain", "native-viscous-domain", "native-import-domain", "native-liquid-import-domain", "native-liquid-domain", "native-liquid-transfer-domain", "native-heat-domain", "native-liquid-heat-domain", "native-transfer-domain", "native-radius-domain", "native-rectangular-domain", "native-actor-domain", "native-liquid-actor-domain", "native-carbon-domain", "native-carbon-particle-domain", "native-breathing-read-domain", "native-pressure-read-domain", "native-liquid-pressure-read-domain", "native-conduction-domain", "native-gravity-domain", "native-liquid-gravity-domain", "native-checkpoint-domain", "native-paired-checkpoint-domain", "native-save-domain", "native-load-domain"):
                try:
                    domain_report = read_json(colony_file.parent / "native-domain.json")
                except (OSError, json.JSONDecodeError):
                    pass
                domain_ok = native_domain_passed(domain_report, observer_report, args.frames,
                    mixed_phase=args.mode in ("native-mixed-domain", "native-mixed-heat-domain", "native-viscous-domain"),
                    configured=args.mode in ("native-configured-domain", "native-configured-water-domain", "native-configured-vacuum-domain"), vacuum=args.mode == "native-configured-vacuum-domain",
                    initial_inventory=args.mode in ("native-import-domain", "native-liquid-import-domain"), liquid=args.mode in ("native-configured-water-domain", "native-liquid-pressure-read-domain", "native-liquid-import-domain", "native-liquid-domain", "native-liquid-transfer-domain", "native-liquid-actor-domain", "native-liquid-heat-domain"), liquid_transfers=args.mode == "native-liquid-transfer-domain", solid_heat=args.mode=="native-solid-heat-domain", heat=args.mode in ("native-heat-domain", "native-mixed-heat-domain"), liquid_heat=args.mode == "native-liquid-heat-domain",
                    transfers=args.mode in ("native-transfer-domain", "native-radius-domain", "native-rectangular-domain"), radial_consumption=args.mode == "native-radius-domain", rectangular_consumption=args.mode == "native-rectangular-domain", actors=args.mode in ("native-actor-domain", "native-liquid-actor-domain"), liquid_actors=args.mode == "native-liquid-actor-domain",
                    pressure_read=args.mode in ("native-pressure-read-domain", "native-liquid-pressure-read-domain"), breathing_read=args.mode == "native-breathing-read-domain", carbon_particle=args.mode == "native-carbon-particle-domain", carbon=args.mode == "native-carbon-domain", conduction=args.mode in ("native-conduction-domain", "native-mixed-heat-domain"), gravity=args.mode == "native-gravity-domain", liquid_gravity=args.mode == "native-liquid-gravity-domain", checkpoint=args.mode in ("native-checkpoint-domain", "native-paired-checkpoint-domain"), paired=args.mode == "native-paired-checkpoint-domain",
                    persistent_actors=getattr(args,"verify_persistent_actors",False) is True,
                    live_breathing=getattr(args,"verify_live_breathing",False) is True,
                    transient_particles=getattr(args,"verify_transient_particles",False) is True,
                    subnormal_carbon_particle=getattr(args,"subnormal_carbon_particle",False) is True,
                    natural_autosave=getattr(args,"verify_natural_autosave",False) is True, saved=args.mode == "native-save-domain" or args.save_colony or getattr(args,"verify_natural_autosave",False) is True, restored=args.mode == "native-load-domain",
                    save_io_recovery=bool(getattr(args,"verify_save_io_recovery",False)),
                    reload_in_process=bool(getattr(args,"reload_in_process",False)))
                if code == 0 and not domain_ok:
                    status, code = "failed-native-domain", 2
            elif code == 0 and not observer_ok:
                status, code = "failed-native-observer", 2
            telemetry = telemetry_summary(telemetry_file) if long_run else None
            telemetry_ok = long_run and long_run_telemetry_passed(telemetry, report)
            if code == 0 and long_run and not telemetry_ok:
                status, code = "failed-long-run-telemetry", 2
            retain_on_success = args.keep_open and code == 0
            result = {
                "status": status, "exit_code": code, "mode": args.mode, "output": str(output), "mod_id": mod_id,
                "observed_player_exit_code": player_exit, "timed_out": timed_out,
                "platform": "native Linux", "display": screen["display"], "steam_display": session["display"],
                "private_display": private_display, "private_display_process": display_record, "software_rendering": not gpu_display,
                "renderer": player_renderer(output),
                "inspection_seconds_requested": inspection_seconds,
                "inspection_seconds_held": time.monotonic() - inspection_started if inspection_started is not None else 0,
                "steam_blocked": steam_blocked(log),
                "integration_probe_failed": not probe_passed(probe),
                "mod_failure_lines": mod_failure_lines(log, (mod_id,))[:50],
                "main_menu_reached": MAIN_MENU_MARKER in log,
                "main_menu_contract_export": contract_export(log),
                "colony_started": bool(report and report.get("GameStarted")),
                "authoritative_colony_simulation_ready": False, "full_mod_completion_verified": False,
                "integration_probe": probe, "colony_report": report, "package_manifest": manifest,
                "native_backend_observation": observer_report,
                "native_backend_observer_passed": observer_ok,
                "native_domain": domain_report, "bounded_native_domain_passed": domain_ok,
                "tile_hover": hover_report, "tile_hover_passed": hover_ok,
                "long_run": long_run, "telemetry_stalled": telemetry_stalled,
                "telemetry_stall_reason": watch.reason if watch is not None else None,
                "development_telemetry": telemetry, "development_telemetry_passed": telemetry_ok,
                "load_save_fixture": {"path": str(load_fixture), "sha256": digest(load_fixture)} if load_fixture else None,
                "game_hashes": {p: digest(game / p) for p in (
                    "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll", "OxygenNotIncluded_Data/Plugins/libSimDLL.so")},
                **revision_fields(manifest),
                "command": arguments, "retained_game": retain_on_success,
            }
            write_json(output / "result.json", result)
            retain = retain_on_success
            print(f"{status}: {output / 'result.json'}")
            return_code = code
        finally:
            if not retain:
                stop_process(record)

    if private_display:
        result["private_display_cleanup_verified"] = not alive(display_record) and not alive(display_record.get("window_manager"))
        write_json(output / "result.json", result)
    return return_code


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state", type=Path, default=DEFAULT_STATE)
    commands = parser.add_subparsers(dest="command", required=True)
    for action in ("setup", "start", "status", "stop", "stop-game"):
        commands.add_parser(action)
    capture = commands.add_parser("screenshot")
    capture.add_argument("--output", type=Path)
    inputs = commands.add_parser("input")
    inputs.add_argument("arguments", nargs=argparse.REMAINDER, help="xdotool arguments, e.g. key Escape")
    play = commands.add_parser("run")
    play.add_argument("--game-dir", type=Path, default=os.environ.get("ONI_GAME_DIR", str(DEFAULT_GAME)))
    play.add_argument("--package", type=Path, required=True)
    play.add_argument("--mode", choices=("menu", "presentation", "tile-hover", "snapshot-copy", "native-observer", "colony-owner", "colony-owner-load", "native-configured-domain", "native-configured-water-domain", "native-configured-vacuum-domain", "native-domain", "native-solid-heat-domain", "native-mixed-domain", "native-mixed-heat-domain", "native-viscous-domain", "native-import-domain", "native-liquid-import-domain", "native-liquid-domain", "native-liquid-transfer-domain", "native-heat-domain", "native-liquid-heat-domain", "native-transfer-domain", "native-radius-domain", "native-rectangular-domain", "native-actor-domain", "native-liquid-actor-domain", "native-carbon-domain", "native-carbon-particle-domain", "native-breathing-read-domain", "native-pressure-read-domain", "native-liquid-pressure-read-domain", "native-conduction-domain", "native-gravity-domain", "native-liquid-gravity-domain", "native-checkpoint-domain", "native-paired-checkpoint-domain", "native-save-domain", "native-load-domain", "load-rejection", "colony-vanilla"), default="menu")
    play.add_argument("--frames", type=int, metavar="1..10000 (1..100000 with --long-run)",
                      help="Native frames to complete (default 30, or 30000 = ten ONI cycles with --long-run)")
    play.add_argument("--timeout", type=int, metavar="30..7200 (30..172800 with --long-run)",
                      help="Game-side validation limit in seconds (default 600, or 172800 = 48 h with --long-run)")
    play.add_argument("--long-run", action="store_true",
                      help="Explicit development long run (about 30,000 frames is ten ONI cycles) in "
                           + ", ".join(LONG_RUN_MODES) + "; writes bounded development telemetry")
    play.add_argument("--telemetry-interval-frames", type=int, metavar="10..10000",
                      help=f"Native frames per development telemetry line (default {TELEMETRY_INTERVAL_DEFAULT}); requires --long-run")
    play.add_argument("--output", type=Path)
    play.add_argument("--keep-open", action="store_true")
    play.add_argument("--inspection-seconds", type=int, choices=range(0,301), default=0, metavar="0..300",
                      help="Keep a completed colony visible for bounded mouse/visual inspection, then clean up normally")
    play.add_argument("--private-display", action="store_true", help="Keep the game on a separate disposable display, leaving Steam available for login")
    play.add_argument("--gpu-display", metavar="DISPLAY", default=os.environ.get("ONI_GPU_DISPLAY") or None,
                      help="Render the game on this hardware X display (e.g. :99, an NVIDIA headless server) instead of the "
                           "software-rendered desktop; defaults to $ONI_GPU_DISPLAY")
    play.add_argument("--resave-before-resume", action="store_true", help="Verify ordinary saving of an initially paused restored colony")
    play.add_argument("--save-colony", action="store_true", help="Also exercise ordinary colony save admission after a native scene")
    play.add_argument("--verify-natural-autosave",action="store_true",help="Wait for a real day-rollover autosave; requires native-domain and 3100..5000 frames")
    play.add_argument("--verify-persistent-actors",action="store_true",help="Save three live point actors; requires a gas/water source, source save or natural autosave, and at least eighty frames")
    play.add_argument("--subnormal-carbon-particle",action="store_true",help="Use the smallest positive binary32 parcel in the original CO2 particle scene")
    play.add_argument("--verify-transient-particles",action="store_true",help="Save an in-flight original CO2 particle and verify exact restore and one native deposition")
    play.add_argument("--verify-live-breathing",action="store_true",help="Place a real organic duplicant in the owned gas room, or observe its saved reload; source requires 20–25 frames")
    play.add_argument("--verify-save-io-recovery", action="store_true", help="Exercise a declined real disk write before the source colony save")
    play.add_argument("--mod-id", help="Stable private Dev mod identity; defaults to MixedAtmospheres.IntegrationProbe")
    play.add_argument("--reload-in-process", action="store_true", help="Return through the main menu and load again in the same Unity process; requires initial autosave control")
    play.add_argument("--load-save", type=Path, help="Completed private colony save for native-load-domain")
    play.add_argument("--vanilla-load", action="store_true", help="colony-owner-load: hand the save to ONI's own loader, which must refuse it")
    play.add_argument("--overwrite-save", action="store_true", help="colony-owner --save-colony: save halfway and again at the end to the same file")
    play.add_argument("--cluster", choices=BASE_CLUSTERS,
                      help="colony-owner only: the base-game starting asteroid's cluster layout (default Terra, SandstoneDefault)")
    play.add_argument("--floor", choices=FLOOR_ARENAS,
                      help="one fixed comparison arena of docs/base-game-floor.md: colony-vanilla (ONI's own simulation) or colony-owner")
    play.add_argument("--speed", type=int, choices=(1, 2, 3),
                      help="the game speed the colony keeps: 1 normal, 2 fast, 3 ultra (default: the game's own)")
    play.add_argument("--scenario", type=scenario_list,
                      help="colony-owner only: in-game system scenarios, comma-separated (docs/colony-owner.md#scenarios)")
    args = parser.parse_args()
    if args.command == "run":
        long_run = args.long_run is True
        args.frames = (LONG_RUN_DEFAULT_FRAMES if long_run else 30) if args.frames is None else args.frames
        args.timeout = (LONG_RUN_TIMEOUT_LIMIT if long_run else 600) if args.timeout is None else args.timeout
        if reason := run_limits_error(args):
            parser.error(reason)
    if args.command=="run" and args.subnormal_carbon_particle and args.mode!="native-carbon-particle-domain" and not (args.verify_transient_particles and args.mode in ("native-domain","native-load-domain") and not args.verify_live_breathing):
        parser.error("Subnormal carbon input requires the carbon-particle scene or transient-particle save/load without a live producer")
    if args.command=="run" and args.verify_transient_particles and (args.mode not in ("native-domain","native-load-domain") or args.mode=="native-domain" and not args.save_colony or args.mode=="native-load-domain" and not args.resave_before_resume):
        parser.error("Transient particle validation requires a gas source save or a reload with a frame-zero resave")
    if args.command=="run" and args.verify_live_breathing and (args.mode not in ("native-domain","native-load-domain") or
            args.verify_persistent_actors or args.mode=="native-domain" and not 20<=args.frames<=25):
        parser.error("Live breathing requires a 20–25-frame gas source or its saved load, without fixture actors")
    if args.command=="run" and args.verify_persistent_actors and (args.mode not in ("native-domain","native-liquid-domain") or not (args.save_colony or args.verify_natural_autosave) or args.frames<80):
        parser.error("Persistent actor validation requires a gas/water source, a source save or natural autosave, and at least eighty frames")
    if args.command == "run" and args.verify_natural_autosave and (args.mode != "native-domain" or not 3100 <= args.frames <= 5000 or args.save_colony):
        parser.error("Natural autosave requires native-domain, 3100..5000 frames and no --save-colony")
    if args.command == "run" and args.mode in ("native-carbon-particle-domain", "native-breathing-read-domain") and args.frames < 30:
        parser.error("The CO2 particle and breathing-reader controls require at least 30 frames")
    if args.command == "run" and args.mode in ("native-actor-domain", "native-liquid-actor-domain") and args.frames < 80:
        parser.error("The registered actor scene requires at least 80 native frames")
    if args.command == "run" and (reason := unsupported_mode(args.mode)):
        print(f"oni_headless: {reason}", file=sys.stderr)
        return 2
    state = args.state.resolve()
    if args.command == "run" and args.reload_in_process and (args.mode != "native-load-domain" or not args.resave_before_resume):
        parser.error("--reload-in-process requires native-load-domain --resave-before-resume")
    if args.command == "run" and args.resave_before_resume and args.mode != "native-load-domain":
        parser.error("--resave-before-resume requires native-load-domain")
    if args.command == "run" and args.verify_save_io_recovery and (args.mode != "native-save-domain" or args.frames<16):
        parser.error("--verify-save-io-recovery requires native-save-domain and at least sixteen frames")
    if args.command == "run" and args.save_colony and (not (args.mode.startswith("native-") or args.mode == "colony-owner") or args.mode in ("native-observer", "native-load-domain")):
        parser.error("--save-colony requires an initial native domain scene")
    if args.command == "run" and isinstance(getattr(args, "cluster", None), str) and args.mode != "colony-owner":
        parser.error("--cluster needs --mode colony-owner")
    if args.command == "run" and getattr(args, "scenario", None) and args.mode != "colony-owner":
        parser.error("--scenario needs --mode colony-owner")
    if args.command == "run" and ((args.mode == "colony-vanilla") != (getattr(args, "floor", None) is not None and args.mode != "colony-owner") or
                                  getattr(args, "floor", None) and getattr(args, "scenario", None)):
        parser.error("--mode colony-vanilla needs --floor; --floor needs colony-vanilla or colony-owner, without --scenario")
    if args.command == "run" and (bool(args.load_save) != (args.mode in ("native-load-domain", "colony-owner-load")) or (args.load_save and not args.load_save.is_file())):
        parser.error("native-load-domain requires --load-save pointing to a completed colony save; other modes cannot use it")
    state.mkdir(parents=True, exist_ok=True, mode=0o700)
    os.chmod(state, 0o700)
    if args.command in LOCKED_COMMANDS:
        with locked(state / LOCK_NAME):
            # Read session.json only while holding the shared lock.
            return locked_command(args, state, read_session(state))
    session = read_session(state)
    if args.command == "status":
        print(json.dumps({"state": str(state), "display": session.get("display"),
                          "processes": {name: (process_group_alive if name == "game" else alive)(session.get(name))
                                        for name in ("xvfb", "wm", "steam", "vnc", "web", "game")},
                          "latest_run": session.get("latest_run"), "steam_authentication": "check Steam UI"}, indent=2))
    elif args.command == "screenshot":
        path = args.output or Path(tempfile.gettempdir()) / f"oni-desktop-{secrets.token_hex(4)}.png"
        screenshot(state, session, path.resolve())
        print(path.resolve())
    elif args.command == "input":
        run_checked([str(state / "tools/desktop/usr/bin/xdotool"), *args.arguments], env=desktop_env(state, session))
    return 0


def locked_command(args, state: Path, session: dict) -> int:
    if args.command == "setup":
        setup(state)
    elif args.command == "start":
        start(state)
    elif args.command in ("stop", "stop-game"):
        for name in (("game", "game_display") if args.command == "stop-game" else ("game", "game_display", "steam", "web", "vnc", "wm", "xvfb")):
            if name == "game_display": stop_private_display(session.get(name))
            else: stop_process(session.get(name))
        print("Stopped owned processes; profiles and evidence retained.")
    elif args.command == "run":
        with cleanup_on_termination():
            return playtest(args, state, session)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"oni_headless: {error}", file=sys.stderr)
        sys.exit(2)
