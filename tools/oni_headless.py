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
FAILURE_WORDS = re.compile(r"fail|error|exception", re.IGNORECASE)
# Mono "  at Type.Method (...)", "(wrapper ...)" and Unity "Type:Method (...)" frames.
STACK_FRAME = re.compile(r"^\s+at |^\s*\(wrapper |^[\w.`+<>]+:[\w.`+<>|]+ ?\(")
# Mod code, Harmony-generated replacements (DMD<...>) or Harmony itself.
MOD_FRAME = re.compile(r"MixedAtmospheres\.|DMD<|HarmonyLib\.")
HARMONY_FAILURE = re.compile(r"HarmonyException|Patching exception", re.IGNORECASE)
SHUTDOWN_ABORT = "Thread was being aborted"
MANIFEST_REVISION = re.compile(r"[0-9a-f]{40}(?:[0-9a-f]{24})?")


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
    return {"pid": pid, "start_ticks": fields[19], "state": fields[0]}


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


def stop_process(record: dict | None) -> None:
    """Stop a recorded process group; a process that already exited is not an error."""
    if not alive(record):
        return
    pid = record["pid"]
    try:
        if os.getpgid(pid) != pid:
            raise RuntimeError(f"Refusing to signal a process group not owned by the runner: {pid}")
        os.killpg(pid, signal.SIGTERM)
        deadline = time.monotonic() + 5
        while alive(record) and time.monotonic() < deadline:
            time.sleep(.1)
        if alive(record):
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


def desktop_env(state: Path, session: dict) -> dict:
    desktop = state / "tools/desktop/usr"
    env = dict(os.environ, DISPLAY=session["display"], XAUTHORITY=str(state / "Xauthority"),
               LIBGL_ALWAYS_SOFTWARE="1",
               LD_LIBRARY_PATH=f"{desktop}/lib/x86_64-linux-gnu:{desktop}/lib/i386-linux-gnu",
               IMLIB2_LOADER_PATH=f"{desktop}/lib/x86_64-linux-gnu/imlib2/loaders",
               XDG_DATA_DIRS=f"{desktop}/share:/usr/share")
    env.pop("WAYLAND_DISPLAY", None)
    return env


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


def verify_package(package: Path) -> dict:
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
    required = {"MixedAtmospheres.Oni.dll", "MixedAtmospheres.Core.dll", "native/linux-x64/libmixed_atmospheres.so"}
    if not required.issubset(manifest["files"]):
        raise ValueError("The package must include pinned Linux native and managed libraries.")
    actual = {p.relative_to(package).as_posix() for p in package.rglob("*") if p.is_file()
              and p != package / "package-manifest.json"}
    if actual != set(manifest["files"]) or any(p.is_symlink() for p in package.rglob("*")):
        raise ValueError("The package contains unpinned files or symlinks; create a fresh package.")
    if any(p.name in ("colony-validation.json", "integration-probe.json", "game-contract-local")
           for p in package.iterdir()):
        raise ValueError("Use a clean package, without previous configuration or runtime reports.")
    return manifest


def stage_package(package: Path, mod: Path, manifest: dict) -> None:
    """Authenticate the launch copy against the already verified source manifest."""
    shutil.copytree(package, mod, symlinks=True)
    copied_manifest = verify_package(mod)
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


def probe_passed(probe: dict | None) -> bool:
    return bool(probe) and probe.get("Platform") == "LinuxPlayer" and all(probe.get(flag) is True for flag in PROBE_FLAGS)


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
             player_exit: int | None = None, identifiers: tuple[str, ...] = ()) -> tuple[str, int]:
    # Positive evidence of a mod failure outranks the Steam block so it is never
    # hidden; result.json records steam_blocked separately.
    if "[Mixed Atmospheres] Integration probe failed:" in log or "[Mixed Atmospheres] Colony validation failed:" in log:
        return "failed-runtime-validation", 2
    if mod_failure_lines(log, identifiers):
        return "failed-mod-log-errors", 2
    if probe is not None and not probe_passed(probe):
        return "failed-integration-probe", 2
    if steam_blocked(log):
        return "blocked-steam-authentication", 3
    if timed_out:
        return "timeout", 2
    if player_exit not in (None, 0):
        return "failed-game-exit", 2
    if not probe_passed(probe):
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


def screenshot(state: Path, session: dict, path: Path) -> None:
    run_checked([str(state / "tools/desktop/usr/bin/scrot"), "-o", str(path)], env=desktop_env(state, session))


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



def playtest(args, state: Path, session: dict) -> int:
    package = args.package.resolve()
    manifest = verify_package(package)
    if args.mode in ("native-observer", "native-domain", "native-liquid-domain", "native-heat-domain", "native-transfer-domain", "native-actor-domain", "native-carbon-domain", "native-conduction-domain", "native-gravity-domain", "native-checkpoint-domain", "native-paired-checkpoint-domain") and not manifest.get("native_observer_enabled", False):
        raise ValueError("Native integration modes require a package built with --native-observer")
    game = args.game_dir.resolve()
    if (game / "OxygenNotIncluded").read_bytes()[:4] != b"\x7fELF":
        raise ValueError("Supply the native Linux ONI installation.")
    if not alive(session.get("steam")) or not alive(session.get("xvfb")):
        raise RuntimeError("Run 'start' and sign in to Steam first.")
    if alive(session.get("game")):
        raise RuntimeError("A previous retained game is running; use 'stop-game' first.")
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
    mod = profile / "mods/Dev" / name
    stage_package(package, mod, manifest)
    write_json(profile / "mods/mods.json", {"version": 1, "mods": [{
        "label": {"distribution_platform": "Dev", "id": name, "title": "Mixed Atmospheres - Integration Probe",
                  "version": mono_label_version(name)}, "status": 1, "enabled": True,
        "enabledForDlc": ["", "EXPANSION1_ID"], "crash_count": 0, "reinstall_path": None,
        "staticID": "MixedAtmospheres.IntegrationProbe"}]})
    if args.mode != "menu":
        write_json(mod / "colony-validation.json", {
            "Mode": args.mode, "AutoStartNewColony": True, "ReuseGeneratedWorld": False,
            "CompletedFrames": args.frames, "TimeoutSeconds": args.timeout,
            "QuitWhenComplete": not args.keep_open, "Seed": "147147", "ColonyName": name,
            "PrivateOwnedLoadRejection": args.mode == "load-rejection",
        })
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
    env = desktop_env(state, session)
    env.update(SteamAppId="457140", SteamGameId="457140")
    # ONI's Linux root is Unity persistentDataPath. Bind a fresh home and set
    # all XDG roots; the actual user's saves and mods are never writable.
    arguments = sandbox_command(home, state, output, game, thin, args.keep_open)
    (output / "runtime").mkdir(mode=0o700)
    record = spawn(arguments, output / "console.log", env)
    retain = False
    try:
        session.update(game=record, latest_run=str(output))
        write_json(state / "session.json", session)
        print(f"Playtest PID {record['pid']}; evidence: {output}", flush=True)
        deadline = time.monotonic() + args.timeout + 60
        probe_file = mod / "integration-probe.json"
        colony_file = mod / "game-contract-local/colony-validation/colony-validation.json"
        timed_out = False
        menu_seen = None
        while alive(record):
            log = (output / "Player.log").read_text(errors="replace") if (output / "Player.log").exists() else ""
            if args.mode == "menu" and MAIN_MENU_MARKER in log:
                # The mod's contract export runs after the marker. Wait for its
                # own completion/failure line (bounded), then let trailing
                # exception and stack lines reach Player.log.
                menu_seen = time.monotonic() if menu_seen is None else menu_seen
                if contract_export(log) or time.monotonic() - menu_seen >= MENU_EXPORT_WAIT_SECONDS:
                    time.sleep(MENU_SETTLE_SECONDS)
                    break
            if args.keep_open and colony_file.exists():
                try:
                    if read_json(colony_file).get("Status") in ("passed", "failed"):
                        break
                except json.JSONDecodeError:
                    pass  # Unity may currently be writing the report.
            if time.monotonic() >= deadline:
                timed_out = True
                break
            time.sleep(1)
        screenshot(state, session, output / "desktop-final.png")
        probe = read_json(probe_file) if probe_file.exists() else None
        report = read_json(colony_file) if colony_file.exists() else None
        log = (output / "Player.log").read_text(errors="replace") if (output / "Player.log").exists() else ""
        player_exit = CHILDREN[record["pid"]].poll()
        status, code = classify(args.mode, log, probe, report, timed_out, player_exit, (name,))
        observer_file = mod / "native-backend-observation-report.json"
        try:
            observer_report = read_json(observer_file) if observer_file.exists() else None
        except (OSError, json.JSONDecodeError):
            observer_report = None
        observer_ok = native_observer_passed(manifest, observer_report)
        domain_report = None
        domain_ok = False
        if args.mode in ("native-domain", "native-liquid-domain", "native-heat-domain", "native-transfer-domain", "native-actor-domain", "native-carbon-domain", "native-conduction-domain", "native-gravity-domain", "native-checkpoint-domain", "native-paired-checkpoint-domain"):
            try:
                domain_report = read_json(colony_file.parent / "native-domain.json")
            except (OSError, json.JSONDecodeError):
                pass
            domain_ok = native_domain_passed(domain_report, observer_report, args.frames,
                liquid=args.mode == "native-liquid-domain", heat=args.mode == "native-heat-domain",
                transfers=args.mode == "native-transfer-domain", actors=args.mode == "native-actor-domain",
                carbon=args.mode == "native-carbon-domain", conduction=args.mode == "native-conduction-domain", gravity=args.mode == "native-gravity-domain", checkpoint=args.mode in ("native-checkpoint-domain", "native-paired-checkpoint-domain"), paired=args.mode == "native-paired-checkpoint-domain")
            if code == 0 and not domain_ok:
                status, code = "failed-native-domain", 2
        elif code == 0 and not observer_ok:
            status, code = "failed-native-observer", 2
        retain_on_success = args.keep_open and code == 0
        result = {
            "status": status, "exit_code": code, "mode": args.mode, "output": str(output),
            "observed_player_exit_code": player_exit, "timed_out": timed_out,
            "platform": "native Linux", "display": session["display"], "software_rendering": True,
            "steam_blocked": steam_blocked(log),
            "integration_probe_failed": not probe_passed(probe),
            "mod_failure_lines": mod_failure_lines(log, (name,))[:50],
            "main_menu_reached": MAIN_MENU_MARKER in log,
            "main_menu_contract_export": contract_export(log),
            "colony_started": bool(report and report.get("GameStarted")),
            "authoritative_colony_simulation_ready": False, "full_mod_completion_verified": False,
            "integration_probe": probe, "colony_report": report, "package_manifest": manifest,
            "native_backend_observation": observer_report,
            "native_backend_observer_passed": observer_ok,
            "native_domain": domain_report, "bounded_native_domain_passed": domain_ok,
            "game_hashes": {p: digest(game / p) for p in (
                "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll", "OxygenNotIncluded_Data/Plugins/libSimDLL.so")},
            **revision_fields(manifest),
            "command": arguments, "retained_game": retain_on_success,
        }
        write_json(output / "result.json", result)
        retain = retain_on_success
        print(f"{status}: {output / 'result.json'}")
        return code
    finally:
        if not retain:
            stop_process(record)


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
    play.add_argument("--mode", choices=("menu", "presentation", "snapshot-copy", "native-observer", "native-domain", "native-liquid-domain", "native-heat-domain", "native-transfer-domain", "native-actor-domain", "native-carbon-domain", "native-conduction-domain", "native-gravity-domain", "native-checkpoint-domain", "native-paired-checkpoint-domain", "load-rejection"), default="menu")
    play.add_argument("--frames", type=int, choices=range(1, 10001), default=30, metavar="1..10000")
    play.add_argument("--timeout", type=int, choices=range(30, 7201), default=600, metavar="30..7200")
    play.add_argument("--output", type=Path)
    play.add_argument("--keep-open", action="store_true")
    args = parser.parse_args()
    if args.command == "run" and args.mode == "native-actor-domain" and args.frames < 80:
        parser.error("The registered actor scene requires at least 80 native frames")
    if args.command == "run" and (reason := unsupported_mode(args.mode)):
        print(f"oni_headless: {reason}", file=sys.stderr)
        return 2
    state = args.state.resolve()
    state.mkdir(parents=True, exist_ok=True, mode=0o700)
    os.chmod(state, 0o700)
    if args.command in LOCKED_COMMANDS:
        with locked(state / LOCK_NAME):
            # Read session.json only while holding the shared lock.
            return locked_command(args, state, read_session(state))
    session = read_session(state)
    if args.command == "status":
        print(json.dumps({"state": str(state), "display": session.get("display"),
                          "processes": {name: alive(session.get(name)) for name in ("xvfb", "wm", "steam", "vnc", "web", "game")},
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
        for name in (("game",) if args.command == "stop-game" else ("game", "steam", "web", "vnc", "wm", "xvfb")):
            stop_process(session.get(name))
        print("Stopped owned processes; profiles and evidence retained.")
    elif args.command == "run":
        return playtest(args, state, session)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"oni_headless: {error}", file=sys.stderr)
        sys.exit(2)
