#!/usr/bin/env python3
"""Private release acceptance for one pushed, clean revision.

Tiers (user decision 2026-10-26):

- ``quick``: the 15 Linux gameplay scenario batches, the nine base-game
  starting asteroids at 3,000 frames each, a 3,000-frame (one-cycle) run on the
  unchanged frozen prepared save, and the WindowsPlayer-under-Proton set.
- ``release``: everything in ``quick`` plus the 30,000-frame ten-cycle run on
  the same frozen save.

The revision is checked out as a detached worktree under
``artifacts/acceptance/<short>/source`` (with the shared ``local`` symlink);
the Linux and Windows packages are built, tested and packaged from it, and it
is the runner checkout for every game. Each game goes through
``tools/game_slot.sh low`` with ``--gpu-display``. Run outputs live under
``artifacts/acceptance/<short>/runs/<tag>/`` and are never reused; the frozen
save is copied and hash-checked before and after, never handed to a game.
The checks are those of the earlier private drivers; one summary JSON per tag
records every step.

    python3 tools/acceptance_suite.py plan --tier quick
    python3 tools/acceptance_suite.py run REVISION --tier quick --keep-going
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
GAME_SLOT = TOOLS / "game_slot.sh"
RUN_LOCKED = TOOLS / "run_locked.sh"

FROZEN_SAVE = Path("local/saves/base-defaults-p5-v1.sav")
FROZEN_SAVE_SHA256 = "0da56203653f39a57c63e53781a4af578f89edc6cf6f5153b455cfc5a0f0d378"
VALIDATION = "mods/Dev/MixedAtmospheres.IntegrationProbe/game-contract-local/colony-validation"
LINUX_PROFILE = "home/.config/unity3d/Klei/Oxygen Not Included"
PROTON_PROFILE = "compatdata/pfx/drive_c/users/steamuser/Documents/Klei/OxygenNotIncluded"

LINUX_BATCHES = (
    "terrain,heat,phase,freeze-melt,door",
    "sublimation,falling,airflow,debris-heat,building-melt",
    "breathing,life-support,mask-station,suit,germ-exposure",
    "electrolysis,electrolyzer-blocked,deoxidizer,rust-deoxidizer,space-heater",
    "gas-generator,coal-generator,polymer-press,oil-refinery,deodorizer",
    "skimmer,terrarium,bottle-emptier,pipes,filter",
    "sensor,vent-overpressure,liquid-pump,meteor,geyser",
    "germs,world-germs,puft,pacu,plant",
    "wheezewort,oxyfern,hazards,rocket-exhaust,mixed-pipes",
    "mixed-pipe-filter,mixed-pipe-boundary,mixed-pipe-output,mixed-pipe-teardown,mixed-pipe-valves",
    "mixed-pipe-gas-vents,mixed-pipe-biology,mixed-pipe-all-germs,mixed-pipe-pumps,mixed-pipe-thermal",
    "mixed-pipe-cold,mixed-pipe-machines,mixed-pipe-heat-machines,mixed-pipe-retire,mixed-pipe-readers",
    "mixed-pipe-colony,mixed-pipe-catalogue,pitcher-pump",
    "fault-containment",
    "base",
)
ASTEROIDS = ("SandstoneDefault", "Oceania", "SandstoneFrozen", "ForestDefault", "ForestLush",
             "ForestHot", "Volcanic", "Badlands", "Oasis")
ASTEROID_FRAMES = 3000
# The WindowsPlayer set repeats these original batches (1-based indices above).
WINDOWS_BATCHES = (2, 3, 4, 6, 7, 10, 11, 12)
CYCLE_FRAMES = {"one-cycle": 3000, "ten-cycle": 30000}
PREPARED_CYCLE_FRAMES = 3132
LINUX_TIMEOUT = 6800
PROTON_BATCH_TIMEOUT = 1800
LONG_RUN_TIMEOUT = 172800
TIERS = ("quick", "release")
PLATFORMS = ("linux", "windows")
FORBIDDEN_DLL_IMPORTS = ("libstdc++", "libgcc", "libwinpthread")

# The explicit non-default colony preset of the WindowsPlayer settings control.
SETTINGS_PRESET = {
    "Format": "MixedAtmospheres.Preset", "Version": 1, "Name": "Windows Precise Strict", "Description": "",
    "Colony": {
        "solver.stepsPerFrame": 2, "solver.pressureTolerance": 1e-10, "solver.pressureIterationCap": 1600,
        "solver.heatIterationCap": 3200, "solver.flowParts": 2, "solver.gasHeatMixing": 8,
        "solver.liquidStiffness": 0.04, "solver.sharpenLiquidInterfaces": True, "solver.interfaceDetail": 0,
        "solver.phaseRelaxationSeconds": 1, "solver.traceThresholdKg": 1e-11,
        "breathing.minimumOxygenFraction": 0.18, "breathing.minimumOxygenPressurePa": 18000,
        "breathing.maximumCo2Fraction": 0.003, "breathing.maximumCo2PressurePa": 300,
        "breathing.maximumOxygenFraction": 1},
    "Preferences": {
        "display.readingPrecision": "friendly", "display.patternContrast": 0.04, "display.traceMoleFraction": 0.01,
        "panel.expanded": True, "panel.showBoundary": True, "keys.cycleOverlay": "F5", "keys.toggleDetails": "F8",
        "keys.findFluid": "F9", "keys.toggleBoundary": "F10", "keys.openSettings": "None"},
}
SETTINGS_KEYS = ("solver.stepsPerFrame", "solver.pressureTolerance", "solver.pressureIterationCap",
                 "solver.heatIterationCap", "breathing.minimumOxygenFraction", "breathing.minimumOxygenPressurePa",
                 "breathing.maximumCo2Fraction", "breathing.maximumCo2PressurePa")
SETTINGS_EXPECTED = [2, 1e-10, 1600, 3200, .18, 18000, .003, 300]
SETTINGS_SHOTS = ("solver-readonly", "breathing-readonly", "performance")
UI_SHOTS = ("menu", "destination", "colony-owner-active", "saved")


class Refused(SystemExit):
    """A precondition that protects immutable inputs or outputs failed."""


@dataclass(frozen=True)
class Step:
    name: str
    platform: str
    kind: str
    deps: tuple[str, ...] = ()
    options: dict = field(default_factory=dict, compare=False, hash=False)


@dataclass
class Infra:
    linux_game: Path
    windows_game: Path
    proton: Path
    mingw_root: Path
    wine_root: Path
    gpu_display: str


@dataclass
class Layout:
    base: Path
    tag: str

    @property
    def source(self) -> Path: return self.base / "source"
    @property
    def packages(self) -> Path: return self.base / "packages"
    @property
    def linux_package(self) -> Path: return self.packages / "linux"
    @property
    def windows_package(self) -> Path: return self.packages / "windows"
    @property
    def windows_zip(self) -> Path: return self.packages / "windows.zip"
    @property
    def runs(self) -> Path: return self.base / "runs" / self.tag
    @property
    def summary(self) -> Path: return self.runs / "summary.json"
    @property
    def wine_prefix(self) -> Path: return self.base / "wine-prefix"
    @property
    def wine(self) -> Path: return self.base / "wine-run.sh"


# ---------------------------------------------------------------- the plan

def plan(tier: str, platforms: tuple[str, ...] = PLATFORMS) -> list[Step]:
    if tier not in TIERS:
        raise ValueError("tier is quick or release")
    if not platforms or any(p not in PLATFORMS for p in platforms):
        raise ValueError("platforms are linux and/or windows")
    steps: list[Step] = []
    if "linux" in platforms:
        steps.append(Step("linux-build", "linux", "build"))
    if "windows" in platforms:
        steps.append(Step("windows-build", "windows", "build"))
    if "linux" in platforms:
        build = ("linux-build",)
        steps += [Step(f"linux-b{i}", "linux", "batch", build, {"scenario": batch})
                  for i, batch in enumerate(LINUX_BATCHES, 1)]
        steps += [Step(f"linux-{name}", "linux", "asteroid", build, {"cluster": name, "frames": ASTEROID_FRAMES})
                  for name in ASTEROIDS]
        steps.append(Step("linux-one-cycle", "linux", "cycle", build, {"frames": CYCLE_FRAMES["one-cycle"]}))
        if tier == "release":
            steps.append(Step("linux-ten-cycle", "linux", "cycle", build, {"frames": CYCLE_FRAMES["ten-cycle"]}))
    if "windows" in platforms:
        build = ("windows-build",)
        steps += [Step(f"windows-b{i}", "windows", "proton-batch", build, {"scenario": LINUX_BATCHES[i - 1]})
                  for i in WINDOWS_BATCHES]
        steps += [
            Step("windows-contact", "windows", "proton-contact", build),
            Step("windows-base-source", "windows", "proton-base-source", build),
            Step("windows-prepared-cycle", "windows", "proton-prepared-cycle", ("windows-base-source",)),
            Step("windows-autosave-cold", "windows", "proton-autosave-cold", ("windows-prepared-cycle",)),
            Step("windows-settings-source", "windows", "proton-settings", build, {"stage": "source"}),
            Step("windows-settings-load", "windows", "proton-settings", ("windows-settings-source",), {"stage": "load"}),
            Step("windows-spacedout-source", "windows", "proton-spacedout", build, {"stage": "source"}),
            Step("windows-spacedout-cold", "windows", "proton-spacedout", ("windows-spacedout-source",), {"stage": "cold"}),
            Step("windows-spacedout-menu", "windows", "proton-spacedout", ("windows-spacedout-source",), {"stage": "menu"}),
            Step("windows-spacedout-ui", "windows", "proton-spacedout-ui", build),
            Step("windows-spacedout-ui-load", "windows", "proton-spacedout-ui-load", ("windows-spacedout-ui",)),
        ]
    return steps


def select(steps: list[Step], only: str | None) -> list[Step]:
    if not only:
        return steps
    wanted = [name.strip() for name in only.split(",") if name.strip()]
    known = {step.name for step in steps}
    unknown = [name for name in wanted if name not in known]
    if unknown:
        raise ValueError("unknown steps: " + ", ".join(unknown))
    by_name = {step.name: step for step in steps}
    needed: set[str] = set()
    pending = list(wanted)
    while pending:
        name = pending.pop()
        if name not in needed:
            needed.add(name)
            pending.extend(by_name[name].deps)
    # Dependencies (the builds, source saves) run in the same tag; builds reuse a verified package.
    return [step for step in steps if step.name in needed]


# ---------------------------------------------------------------- helpers

def now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, ValueError):
        return {}
    return value if isinstance(value, dict) else {}


def write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    os.replace(temporary, path)


def clean_env(extra: dict | None = None) -> dict:
    """Never inherit mod overrides: every MA_ variable is removed."""
    env = {key: value for key, value in os.environ.items() if not key.startswith("MA_")}
    env.update(extra or {})
    return env


def git(*arguments: str, cwd: Path = ROOT) -> str:
    return subprocess.check_output(["git", *arguments], cwd=cwd, text=True).strip()


def common_root() -> Path:
    """The main checkout: shared build infrastructure lives in its ignored build folders."""
    return Path(git("rev-parse", "--path-format=absolute", "--git-common-dir")).parent


def finite(value, bound: float) -> bool:
    return type(value) in (int, float) and math.isfinite(value) and 0 <= value <= bound


def save_header(data: bytes) -> tuple[int, dict]:
    """ONI save prefix: build, header length, header version, compression; then JSON game info."""
    build, length, _, _ = struct.unpack_from("<IiIi", data)
    return build, json.loads(data[16:16 + length])


def embedded_header(data: bytes) -> dict:
    start = data.find(b'{"')
    depth = 0
    for index in range(start, len(data)):
        if data[index] == ord("{"):
            depth += 1
        elif data[index] == ord("}"):
            depth -= 1
            if depth == 0:
                return json.loads(data[start:index + 1])
    raise ValueError("No save header")


def verify_package(package: Path, revision: str) -> dict:
    manifest = read_json(package / "package-manifest.json")
    if manifest.get("source_revision") != revision or manifest.get("source_dirty") is not False:
        raise Refused(f"Package {package} is not a clean build of {revision}")
    files = manifest.get("files")
    if not isinstance(files, dict) or not files:
        raise Refused(f"Package {package} has no file inventory")
    for name, digest in files.items():
        if not (package / name).is_file() or sha256(package / name) != digest:
            raise Refused(f"Package file {name} differs from its manifest")
    present = {path.relative_to(package).as_posix() for path in package.rglob("*") if path.is_file()}
    if present - set(files) - {"package-manifest.json"}:
        raise Refused(f"Package {package} has unlisted files")
    return manifest


def package_hashes(package: Path, manifest: dict) -> dict:
    return {name: sha256(package / name) for name in manifest["files"]}


def verify_source(layout: Layout, revision: str) -> None:
    if git("rev-parse", "HEAD", cwd=layout.source) != revision:
        raise Refused("The runner checkout is not at the accepted revision")
    if git("status", "--porcelain", cwd=layout.source):
        raise Refused("The runner checkout must stay clean")


# ---------------------------------------------------------------- source and builds

def resolve_revision(revision: str, fetch: bool = True) -> str:
    if fetch:
        subprocess.run(["git", "fetch", "-q", "origin"], cwd=ROOT, check=True)
    full = git("rev-parse", "--verify", revision + "^{commit}")
    if subprocess.run(["git", "merge-base", "--is-ancestor", full, "origin/main"], cwd=ROOT).returncode:
        raise Refused(f"{full} is not on origin/main; push it first")
    return full


def prepare_source(layout: Layout, revision: str) -> None:
    source = layout.source
    if source.exists():
        verify_source(layout, revision)
    else:
        source.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "worktree", "add", "-q", "--detach", str(source), revision], cwd=ROOT, check=True)
    shared = (ROOT / "local").resolve()
    link = source / "local"
    if not link.is_symlink():
        link.symlink_to(shared, target_is_directory=True)
    elif link.resolve() != shared:
        raise Refused("The runner checkout's local link points elsewhere")
    verify_source(layout, revision)


def build_command(command: list[str]) -> list[str]:
    """Heavy builds take the high CPU half and run niced (TEAM.md)."""
    return [str(RUN_LOCKED), "high", "nice", "-n", "10", *command]


def run_build(commands: list[tuple[str, list[str]]], cwd: Path, env: dict, log: Path, record: dict) -> bool:
    record.setdefault("commands", [])
    with log.open("a") as stream:
        for name, command in commands:
            stream.write(f"\n[{now()}] {name}: {' '.join(command)}\n")
            stream.flush()
            start = stream.tell()
            returncode = subprocess.run(build_command(command), cwd=cwd, env=env, stdout=stream,
                                        stderr=subprocess.STDOUT).returncode
            stream.flush()
            with log.open(errors="replace") as reader:
                reader.seek(start)
                output = reader.read()
            entry = {"name": name, "returncode": returncode}
            if name == "ctest":
                match = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", output)
                entry["summary"] = match.group(0) if match else None
                entry["passed"] = returncode == 0 and bool(match) and match.group(2) == "0"
            if name == "hook-tests":
                entry["pass_lines"] = len(re.findall(r"^PASS ", output, re.M))
                entry["fail_lines"] = len(re.findall(r"^FAIL", output, re.M))
                entry["passed"] = returncode == 0 and entry["pass_lines"] > 0 and entry["fail_lines"] == 0
            record["commands"].append(entry)
            if returncode or entry.get("passed") is False:
                return False
    return True


def finish_package(package: Path, revision: str, record: dict, layout: Layout) -> None:
    manifest = verify_package(package, revision)
    verify_source(layout, revision)
    record.update(package=str(package), manifest_sha256=sha256(package / "package-manifest.json"),
                  file_count=len(manifest["files"]), files=manifest["files"],
                  native_observer_enabled=manifest.get("native_observer_enabled"))


def build_linux(layout: Layout, revision: str, infra: Infra) -> tuple[dict, dict]:
    record_path = layout.packages / "linux-build.json"
    package = layout.linux_package
    if package.exists():
        record = read_json(record_path)
        verify_package(package, revision)
        if record.get("passed") is not True:
            raise Refused("An earlier Linux build at this revision did not pass; remove it deliberately")
        return {"package_verified": True, "build_passed": True}, {**record, "reused_verified_package": True}
    layout.packages.mkdir(parents=True, exist_ok=True)
    record = {"revision": revision, "started": now(), "platform": "linux"}
    game = str(infra.linux_game)
    env = clean_env({"MSBUILDDISABLENODEREUSE": "1", "UseSharedCompilation": "false", "DOTNET_CLI_TELEMETRY_OPTOUT": "1"})
    managed = layout.source / "build-managed-linux"
    commands = [
        ("configure", ["cmake", "-S", ".", "-B", "build-linux", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                       "-DMA_BUILD_ONI_NATIVE_BACKEND=ON"]),
        ("build", ["cmake", "--build", "build-linux", "-j", "8"]),
        ("ctest", ["ctest", "--test-dir", "build-linux", "--output-on-failure", "-j", "8"]),
        ("managed", ["dotnet", "build", "managed/MixedAtmospheres.Oni", "-c", "Release", "--nologo",
                     "-p:UseSharedCompilation=false", "-p:OniGameDir=" + game, "--artifacts-path", str(managed)]),
        ("hook-tests", ["dotnet", "run", "--project", "tools/ColonyConduitHookTests", "-c", "Release",
                        "-p:UseSharedCompilation=false", "-p:OniGameDir=" + game, "--artifacts-path", str(managed),
                        "--", game]),
        ("package", [sys.executable, "tools/package_probe.py", "--native", "build-linux/libmixed_atmospheres.so",
                     "--native-observer", "build-linux/libmixed_atmospheres_oni.so",
                     "--managed-dir", str(managed / "bin/MixedAtmospheres.Oni/release"), "--output", str(package)]),
    ]
    passed = run_build(commands, layout.source, env, layout.packages / "linux-build.log", record)
    if passed:
        finish_package(package, revision, record, layout)
    record.update(passed=passed, finished=now())
    write_json(record_path, record)
    return {"package_verified": passed, "build_passed": passed}, {**record, "reused_verified_package": False}


def write_wine_wrapper(layout: Layout, infra: Infra) -> None:
    wine = infra.wine_root / "usr/lib/x86_64-linux-gnu/wine"
    text = ("#!/bin/sh\nset -eu\n"
            f"export WINEPREFIX='{layout.wine_prefix}'\nexport WINEDEBUG=-all\n"
            f"export WINEDLLPATH='{wine}'\nexport WINESERVER='{wine}/wineserver'\n"
            f"exec '{wine}/wine' \"$@\"\n")
    if layout.wine.exists():
        if layout.wine.read_text() != text:
            raise Refused("The Wine wrapper of this revision changed")
        return
    layout.wine.write_text(text)
    layout.wine.chmod(0o755)


def prepare_wine_prefix(layout: Layout, infra: Infra, env: dict, log: Path, record: dict) -> bool:
    """A fresh private prefix: unpacked Wine lacks the prefix copy of its own zlib1.dll."""
    wine_lib = infra.wine_root / "usr/lib/x86_64-linux-gnu/wine"
    boot = {**env, "WINEDLLOVERRIDES": "mscoree,mshtml="}  # no optional Mono/Gecko GUI installs
    server = [str(wine_lib / "wineserver"), "-w"]
    with log.open("a") as stream:
        stream.write(f"\n[{now()}] wine-prefix: {layout.wine_prefix}\n")
        stream.flush()
        first = subprocess.run(build_command([str(layout.wine), "wineboot", "-u"]), env=boot, stdout=stream,
                               stderr=subprocess.STDOUT).returncode
        subprocess.run(server, env=boot, stdout=stream, stderr=subprocess.STDOUT)
        system32 = layout.wine_prefix / "drive_c/windows/system32"
        if not system32.is_dir():
            record.setdefault("commands", []).append({"name": "wine-prefix", "returncode": first})
            return False
        if not (system32 / "zlib1.dll").exists():
            shutil.copy2(wine_lib / "x86_64-windows/zlib1.dll", system32 / "zlib1.dll")
        second = subprocess.run(build_command([str(layout.wine), "wineboot", "-u"]), env=boot, stdout=stream,
                                stderr=subprocess.STDOUT).returncode
        subprocess.run(server, env=boot, stdout=stream, stderr=subprocess.STDOUT)
    record.setdefault("commands", []).append({"name": "wine-prefix", "returncode": second, "first_boot_returncode": first})
    return second == 0


def build_windows(layout: Layout, revision: str, infra: Infra) -> tuple[dict, dict]:
    record_path = layout.packages / "windows-build.json"
    package, archive = layout.windows_package, layout.windows_zip
    if package.exists() or archive.exists():
        record = read_json(record_path)
        verify_package(package, revision)
        if record.get("passed") is not True or not archive.is_file() or sha256(archive) != record.get("zip_sha256"):
            raise Refused("An earlier Windows build at this revision did not pass or its ZIP changed")
        return {"package_verified": True, "build_passed": True,
                "imports_system_only": record.get("imports_system_only") is True}, {**record, "reused_verified_package": True}
    layout.packages.mkdir(parents=True, exist_ok=True)
    write_wine_wrapper(layout, infra)
    wine_lib = infra.wine_root / "usr/lib/x86_64-linux-gnu/wine"
    env = clean_env({"WINEPREFIX": str(layout.wine_prefix), "WINESERVER": str(wine_lib / "wineserver"),
                     "WINEDATADIR": str(infra.wine_root / "usr/share/wine"), "WINEDEBUG": "-all",
                     "MSBUILDDISABLENODEREUSE": "1", "UseSharedCompilation": "false", "DOTNET_CLI_TELEMETRY_OPTOUT": "1"})
    env.pop("DISPLAY", None)
    record = {"revision": revision, "started": now(), "platform": "windows"}
    game = str(infra.windows_game)
    managed = layout.source / "build-managed-windows"
    build = layout.source / "build-windows"
    wine = str(layout.wine)
    commands = [
        ("configure", ["cmake", "-S", ".", "-B", "build-windows", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                       "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake",
                       "-DMA_BUILD_ONI_NATIVE_BACKEND=ON", "-DMA_MINGW_ROOT=" + str(infra.mingw_root),
                       "-DMA_WINE=" + wine]),
        ("build", ["cmake", "--build", "build-windows", "-j", "8"]),
    ]
    tests = [
        ("wine-cmd", [wine, "cmd", "/c", "exit", "0"]),
        ("airflow-boundary", [wine, str(build / "ma_oni_colony_tests.exe"), "--airflow-boundary-only"]),
        ("ctest", ["ctest", "--test-dir", "build-windows", "--output-on-failure", "-j", "8"]),
        ("managed", ["dotnet", "build", "managed/MixedAtmospheres.Oni", "-c", "Release", "--nologo",
                     "-p:UseSharedCompilation=false", "-p:OniGameDir=" + game, "--artifacts-path", str(managed)]),
        ("hook-tests", ["dotnet", "run", "--project", "tools/ColonyConduitHookTests", "-c", "Release",
                        "-p:UseSharedCompilation=false", "-p:OniGameDir=" + game, "--artifacts-path", str(managed),
                        "--", game]),
    ]
    try:
        log = layout.packages / "windows-build.log"
        passed = (run_build(commands, layout.source, env, log, record) and prepare_wine_prefix(layout, infra, env, log, record)
                  and run_build(tests, layout.source, env, log, record))
        if passed:
            objdump = str(infra.mingw_root / "bin/x86_64-w64-mingw32-objdump")
            imports = {}
            for name in ("mixed_atmospheres.dll", "mixed_atmospheres_oni.dll"):
                dump = subprocess.check_output([objdump, "-p", str(build / name)], text=True)
                imports[name] = sorted(set(re.findall(r"DLL Name: (\S+)", dump)))
            record["imports"] = imports
            record["imports_system_only"] = not any(token in value.lower() for values in imports.values()
                                                    for value in values for token in FORBIDDEN_DLL_IMPORTS)
            commands = [
                ("package", [sys.executable, "tools/package_probe.py", "--native", "build-windows/mixed_atmospheres.dll",
                             "--native-observer", "build-windows/mixed_atmospheres_oni.dll",
                             "--managed-dir", str(managed / "bin/MixedAtmospheres.Oni/release"), "--output", str(package)]),
                ("zip", [sys.executable, "tools/package_windows.py", "--package", str(package), "--output", str(archive)]),
            ]
            passed = run_build(commands, layout.source, env, layout.packages / "windows-build.log", record)
        if passed:
            finish_package(package, revision, record, layout)
            record["zip_sha256"] = sha256(archive)
    finally:
        subprocess.run([str(wine_lib / "wineserver"), "-k"], env=env, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)
    imports_ok = record.get("imports_system_only") is True
    record.update(passed=passed and imports_ok, finished=now())
    write_json(record_path, record)
    return {"package_verified": passed, "build_passed": passed,
            "imports_system_only": imports_ok}, {**record, "reused_verified_package": False}


# ---------------------------------------------------------------- game commands

def linux_command(step: Step, package: Path, output: Path, infra: Infra, load: Path | None = None) -> list[str]:
    command = [sys.executable, "tools/oni_headless.py", "run", "--package", str(package)]
    if step.kind == "cycle":
        return command + ["--mode", "colony-owner-load", "--load-save", str(load), "--long-run",
                          "--frames", str(step.options["frames"]), "--timeout", str(LONG_RUN_TIMEOUT),
                          "--gpu-display", infra.gpu_display, "--output", str(output)]
    command += ["--mode", "colony-owner", "--gpu-display", infra.gpu_display, "--timeout", str(LINUX_TIMEOUT),
                "--output", str(output)]
    if step.kind == "batch":
        return command + ["--scenario", step.options["scenario"]]
    return command + ["--cluster", step.options["cluster"], "--frames", str(step.options["frames"])]


def proton_command(package: Path, output: Path, infra: Infra, *arguments: str) -> list[str]:
    return [sys.executable, "tools/oni_proton_headless.py", "--proton", str(infra.proton), "--package", str(package),
            "--gpu-display", infra.gpu_display, *arguments, "--output", str(output)]


def proton_arguments(step: Step, load: Path | None = None) -> list[str]:
    kind = step.kind
    if kind == "proton-batch":
        return ["--mode", "colony-owner", "--scenario", step.options["scenario"], "--timeout", str(PROTON_BATCH_TIMEOUT)]
    if kind == "proton-contact":
        return ["--mode", "colony-owner", "--scenario", "windows-contact", "--quit-from-menu", "--timeout", "1200"]
    if kind == "proton-base-source":
        return ["--mode", "colony-owner", "--scenario", "base", "--frames", "32", "--save-colony", "--quit-from-menu",
                "--timeout", "1200"]
    if kind == "proton-prepared-cycle":
        return ["--mode", "colony-owner-load", "--load-save", str(load), "--frames", str(PREPARED_CYCLE_FRAMES),
                "--quit-from-menu", "--timeout", "3600"]
    if kind == "proton-autosave-cold":
        return ["--mode", "colony-owner-load", "--load-save", str(load), "--frames", "120", "--reload-in-process",
                "--quit-from-menu", "--timeout", "1800"]
    if kind == "proton-settings":
        common = ["--settings-open", "1", "--inspection-seconds", "120", "--timeout", "900"]
        if step.options["stage"] == "source":
            return common + ["--mode", "colony-owner", "--frames", "40", "--save-colony", "--settings-preset", str(load)]
        return common + ["--mode", "colony-owner-load", "--frames", "32", "--load-save", str(load)]
    if kind == "proton-spacedout":
        base = ["--dlc-mask", "1", "--frames", "8", "--timeout", "1800"]
        if step.options["stage"] == "source":
            return ["--mode", "colony-owner", *base, "--scenario", "spaced-out-worlds", "--save-colony", "--quit-from-menu"]
        menu = ["--reload-in-process"] if step.options["stage"] == "menu" else []
        return ["--mode", "colony-owner-load", *base, "--load-save", str(load), "--quit-from-menu", *menu]
    if kind == "proton-spacedout-ui":
        return ["--mode", "menu", "--settings-open", "1", "--inspection-seconds", "300", "--timeout", "900"]
    if kind == "proton-spacedout-ui-load":
        return ["--mode", "colony-owner-load", "--dlc-mask", "1", "--load-save", str(load), "--frames", "8",
                "--quit-from-menu", "--timeout", "1800"]
    raise ValueError("Unknown WindowsPlayer step " + kind)


def windows_command(step: Step, package: Path, output: Path, infra: Infra, source: Path,
                    load: Path | None = None) -> list[str]:
    """The WindowsPlayer command; the prepared cycle and UI controls go through this tool's wrappers."""
    runner = proton_command(package, output, infra, *proton_arguments(step, load))[2:]
    this = [sys.executable, str(Path(__file__).resolve())]
    if step.kind == "proton-prepared-cycle":
        return [*this, "_proton-longrun", "--source", str(source), "--", *runner]
    if step.kind in ("proton-settings", "proton-spacedout-ui"):
        stage = "settings" if step.kind == "proton-settings" else "ordinary-ui"
        return [*this, "_ui-stage", stage, "--source", str(source), "--output", str(output),
                "--gpu-display", infra.gpu_display, "--", *runner]
    return [sys.executable, "tools/oni_proton_headless.py", *runner]


def slot(command: list[str]) -> list[str]:
    return [str(GAME_SLOT), "low", *command]


# ---------------------------------------------------------------- checks

def linux_checks(rc: int, output: Path) -> tuple[dict, dict]:
    """The suite driver's criterion: the harness classified the requested gate as passed."""
    result = read_json(output / "result.json")
    owner = read_json(output / LINUX_PROFILE / VALIDATION / "colony-owner.json")
    scenarios = owner.get("Scenarios") or []
    details = {"classification": result.get("status", result.get("classification")),
               "timed_out": result.get("timed_out"), "mod_failure_lines": result.get("mod_failure_lines"),
               "owner": {key: owner.get(key) for key in (
                   "State", "Faulted", "CompletedFrames", "RejectedAttempts", "SkippedRelaxations",
                   "MaximumRelativeMassError", "MaximumRelativeEnergyError", "TraceSinkKg", "TraceSinkJoules",
                   "Error", "OwnerConfigFlags", "Saves", "Loads")},
               "scenarios": [{"Name": s.get("Name"), "Finished": s.get("Finished"), "Passed": s.get("Passed"),
                              "failed_checks": [{"Name": c.get("Name"), "Detail": c.get("Detail")}
                                                for c in s.get("Checks") or [] if c.get("Passed") is not True]}
                             for s in scenarios]}
    return {"harness_pass": rc == 0}, details


def cycle_checks(rc: int, output: Path, frames: int, hashes: dict, package: Path, manifest: dict,
                 manifest_sha: str, source: Path, copy: Path, expected: str = FROZEN_SAVE_SHA256) -> tuple[dict, dict]:
    """The prepared-save driver's acceptance, unchanged; ``frames`` is 3,000 or 30,000."""
    root = output / LINUX_PROFILE / VALIDATION
    owner, colony = read_json(root / "colony-owner.json"), read_json(root / "colony-validation.json")
    player = output / "Player.log"
    text = player.read_text(errors="replace") if player.exists() else ""
    autosaves = re.findall(r"Saved to \[([^\n]*[\\/]auto_save[\\/][^\n]*\.sav)\]", text)
    existing = [{"path": str(p), "bytes": p.stat().st_size, "sha256": sha256(p)}
                for p in sorted({Path(x) for x in autosaves}) if p.is_file() and p.stat().st_size > 0]
    after = package_hashes(package, manifest)
    profile = owner.get("RestoredOwner") or {}
    family = 5 if profile.get("PhysicalVersion") == 23 else None
    checks = {
        "harness_pass": rc == 0,
        "all_three_alive": colony.get("Duplicants") == 3,
        "owner_healthy": owner.get("State") == 1 and owner.get("Faulted") is False and owner.get("Error") == "",
        "zero_rejected_attempts": owner.get("RejectedAttempts") == 0,
        "zero_skipped_relaxations": owner.get("SkippedRelaxations") == 0,
        "ledgers_close_at_existing_native_bounds": finite(owner.get("MaximumRelativeMassError"), 1e-8)
            and finite(owner.get("MaximumRelativeEnergyError"), 1e-6),
        "new_frames": owner.get("CompletedFrames", 0) - colony.get("RestoredFrame", 0) >= frames,
        "natural_autosave_log_present": bool(autosaves),
        "natural_autosave_files_present": bool(existing),
        "default_config_flags": owner.get("OwnerConfigFlags") == 13,
        "saved_default_profile_five": family == 5 and profile.get("LiquidLaw") == 1
            and profile.get("PressureIterationCap") == 400 and profile.get("HeatIterationCap") == 800
            and profile.get("GasHeatMixing") == 8,
        "frozen_package_unchanged": after == hashes and sha256(package / "package-manifest.json") == manifest_sha,
        "original_save_unchanged": sha256(source) == expected,
        "copied_save_unchanged": sha256(copy) == expected,
    }
    telemetry = root / "development-telemetry.jsonl"
    lines = []
    if telemetry.exists():
        for line in telemetry.read_text().splitlines():
            try:
                lines.append(json.loads(line))
            except ValueError:
                lines.append({})
    games = [x.get("Game", {}) for x in lines if x.get("Game", {}).get("Started")]
    cycles = [games[0].get("Cycle"), games[-1].get("Cycle")] if games else []
    delta = cycles[1] - cycles[0] if games and all(type(x) is int for x in cycles) else None
    checks["no_invincibility"] = bool(games) and all(x.get("DebugInvincible") is False for x in games)
    checks["all_samples_three_alive"] = bool(games) and all(x.get("Duplicants") == 3 for x in games)
    checks["actual_game_cycles"] = delta is not None and delta >= frames // 3000
    checks["telemetry_sequences_complete"] = len(lines) >= 2 and [x.get("Sequence") for x in lines] == list(range(1, len(lines) + 1))
    perf = [tuple(map(int, x)) for x in re.findall(r"perf frames (\d+)-(\d+):", text)]
    gaps = [{"previous": list(a), "next": list(b)} for a, b in zip(perf, perf[1:]) if b[0] != a[1] + 1]
    checks["performance_windows_complete"] = bool(perf) and not gaps and all(b - a + 1 == 150 for a, b in perf)
    details = {"natural_autosaves": autosaves, "existing_natural_autosaves": existing, "game_cycles": cycles,
               "game_cycle_delta": delta,
               "game_time_in_cycles": [games[0].get("TimeInCycles"), games[-1].get("TimeInCycles")] if games else [],
               "saved_profile_family": family,
               "owner": {key: owner.get(key) for key in ("State", "Faulted", "CompletedFrames", "RejectedAttempts",
                                                         "SkippedRelaxations", "MaximumRelativeMassError",
                                                         "MaximumRelativeEnergyError", "TraceSinkKg", "TraceSinkJoules",
                                                         "OwnerConfigFlags", "Error")},
               "restored_frame": colony.get("RestoredFrame"), "duplicants": colony.get("Duplicants"),
               "performance_windows": {"count": len(perf), "first": list(perf[0]) if perf else None,
                                       "last": list(perf[-1]) if perf else None, "gaps": gaps}}
    return checks, details


def proton_common(result: dict, revision: str, files: dict) -> dict:
    return {
        "exit_code_zero": result.get("exit_code") == 0,
        "observed_player_exit_zero": result.get("observed_player_exit_code") == 0,
        "clean_exact_source": result.get("source_revision") == revision and result.get("source_dirty") is False,
        "clean_exact_runner": result.get("runner_git_revision") == revision and result.get("runner_git_status") == "",
        "no_mod_failure_lines": result.get("mod_failure_lines") == [],
        "not_timed_out": result.get("timed_out") is False,
        "labelled_proton_not_windows_os": result.get("actual_windows_os") is False and result.get("target_platform") == "WindowsPlayer",
        "package_is_bundle": (result.get("package_manifest") or {}).get("files") == files,
    }


def proton_owner(result: dict, status: str, revision: str, files: dict) -> dict:
    owner = result.get("colony_owner") or {}
    return {**proton_common(result, revision, files),
            "status": result.get("status") == status,
            "no_ma_environment_overrides": result.get("owner_environment_overrides") is False,
            "owner_healthy": owner.get("State") == 1 and owner.get("Faulted") is False and owner.get("Error") == "",
            "zero_rejected_attempts": owner.get("RejectedAttempts") == 0,
            "zero_skipped_relaxations": owner.get("SkippedRelaxations") == 0,
            "ledgers_close_at_existing_native_bounds": finite(owner.get("MaximumRelativeMassError"), 1e-8)
                and finite(owner.get("MaximumRelativeEnergyError"), 1e-6),
            "default_config_flags": owner.get("OwnerConfigFlags") == 13,
            "all_scenarios_passed": all(s.get("Finished") is True and s.get("Passed") is True
                                        for s in owner.get("Scenarios", []))}


def owner_details(result: dict) -> dict:
    owner = result.get("colony_owner") or {}
    return {"status": result.get("status"),
            "owner": {key: owner.get(key) for key in ("CompletedFrames", "RejectedAttempts", "SkippedRelaxations",
                                                      "MaximumRelativeMassError", "MaximumRelativeEnergyError",
                                                      "OwnerConfigFlags", "Error")},
            "scenarios": [{"Name": s.get("Name"), "Finished": s.get("Finished"), "Passed": s.get("Passed"),
                           "failed_checks": [{"Name": c.get("Name"), "Detail": c.get("Detail")}
                                             for c in s.get("Checks") or [] if c.get("Passed") is not True]}
                          for s in owner.get("Scenarios", [])],
            "mod_failure_lines": result.get("mod_failure_lines")}


def proton_checks(step: Step, output: Path, revision: str, files: dict) -> tuple[dict, dict]:
    result = read_json(output / "result.json")
    if not result:
        return {"result_present": False}, {}
    report = result.get("colony_report") or {}
    owner = result.get("colony_owner") or {}
    kind = step.kind
    quit_menu = {"menu_then_desktop_quit": result.get("quit_from_menu_observed") is True}
    if kind == "proton-batch":
        checks = {**proton_owner(result, "passed-colony-owner", revision, files),
                  "five_original_scenarios": len(owner.get("Scenarios", [])) == 5}
    elif kind == "proton-contact":
        checks = {**proton_owner(result, "passed-colony-owner", revision, files), **quit_menu,
                  "windows_contact_scenario": [s.get("Name") for s in owner.get("Scenarios", [])] == ["windows-contact"]}
    elif kind == "proton-base-source":
        saved = Path(result.get("host_saved_file") or "/nonexistent")
        checks = {**proton_owner(result, "passed-colony-owner", revision, files), **quit_menu,
                  "three_alive": report.get("Duplicants") == 3,
                  "saved_file_recorded": saved.is_file() and sha256(saved) == result.get("host_saved_file_sha256")}
    elif kind == "proton-prepared-cycle":
        checks = {**proton_owner(result, "passed-colony-owner-load", revision, files), **quit_menu,
                  "long_run": report.get("LongRun") is True, "three_alive": report.get("Duplicants") == 3,
                  "full_cycle_frames": owner.get("CompletedFrames", 0) - report.get("RestoredFrame", 0) >= PREPARED_CYCLE_FRAMES}
    elif kind == "proton-autosave-cold":
        checks = {**proton_owner(result, "passed-colony-owner-load", revision, files), **quit_menu,
                  "three_alive": report.get("Duplicants") == 3}
    elif kind == "proton-settings":
        source = step.options["stage"] == "source"
        values = (owner.get("ColonySettings") or {}).get("Values", {})
        checks = {"passed": result.get("status") == ("passed-colony-owner" if source else "passed-colony-owner-load"),
                  "source": result.get("source_revision") == revision and result.get("source_dirty") is False,
                  "healthy": owner.get("State") == 1 and owner.get("Faulted") is False and owner.get("Error") == "",
                  "settings_restored": [values.get(key) for key in SETTINGS_KEYS] == SETTINGS_EXPECTED,
                  "normal_desktop_exit": result.get("observed_player_exit_code") == 0,
                  "no_mod_errors": result.get("mod_failure_lines") == [],
                  "screenshots": all((output / (name + ".png")).is_file() for name in SETTINGS_SHOTS)}
        return checks, {"status": result.get("status"), "settings": values}
    elif kind in ("proton-spacedout", "proton-spacedout-ui-load"):
        sys.path.insert(0, str(TOOLS))
        import oni_dlc_matrix
        loading = kind == "proton-spacedout-ui-load" or step.options.get("stage") in ("cold", "menu")
        menu = step.options.get("stage") == "menu"
        try:
            oni_dlc_matrix.verify_case(result, "proton", 1, loading, menu)
            verified, reason = True, None
        except (ValueError, KeyError, TypeError) as error:
            verified, reason = False, str(error)
        checks = {**proton_common(result, revision, files), **quit_menu,
                  "ordinary_spaced_out_case": verified,
                  "no_ma_environment_overrides": result.get("owner_environment_overrides") is False,
                  "owner_world_germs": (owner.get("WorldGerms") or {}).get("Supported") is True}
        details = owner_details(result)
        details["verify_case_error"] = reason
        return checks, details
    elif kind == "proton-spacedout-ui":
        evidence = read_json(output / "ordinary-ui-evidence.json")
        header = evidence.get("header") or {}
        save = Path(evidence.get("save") or "/nonexistent")
        wrapper = result.get("ordinary_spacedout_ui") or {}
        checks = {**proton_common(result, revision, files),
                  "status": result.get("status") == "passed-scoped-main-menu",
                  "no_validation_config": wrapper.get("colony_validation_absent") is True
                      and not any(output.rglob("colony-validation.json")),
                  "no_physics_overrides": wrapper.get("physics_environment_overrides") is False,
                  "owner_started": bool(evidence.get("owner_start")),
                  "embark_and_manual_save": evidence.get("saves", 0) >= 2,
                  "desktop_quit_after_save": evidence.get("quit_after_last_save") is True,
                  "ordinary_spaced_out_save": header.get("dlcIds") == ["EXPANSION1_ID"] and header.get("isAutoSave") is False
                      and header.get("sandboxEnabled") is False and header.get("numberOfDuplicants") == 3
                      and save.is_file() and sha256(save) == evidence.get("save_sha256"),
                  "screenshots": all((output / ("ordinary-" + name + ".png")).is_file() for name in UI_SHOTS)}
        return checks, {"status": result.get("status"), "evidence": evidence}
    else:
        raise ValueError("Unknown WindowsPlayer step " + kind)
    return checks, owner_details(result)


# ---------------------------------------------------------------- inputs of dependent steps

def stage_frozen_save(source: Path, copy: Path, expected: str = FROZEN_SAVE_SHA256) -> Path:
    """Copy the unchanged frozen save; the game only ever sees (and the runner re-copies) the copy."""
    if not source.is_file() or sha256(source) != expected:
        raise Refused("The prepared save differs from the fixed handoff source hash")
    if copy.exists():
        raise Refused(f"Refusing to reuse {copy}")
    shutil.copy2(source, copy)
    if sha256(copy) != expected or sha256(source) != expected:
        raise Refused("The copied prepared save differs")
    return copy


def base_source_save(output: Path, revision: str) -> Path:
    """Admit only the completed, unchanged base fixture save (prepared-cycle driver)."""
    result = read_json(output / "result.json")
    owner = result.get("colony_owner") or {}
    if (result.get("status") != "passed-colony-owner" or result.get("exit_code") != 0
            or result.get("source_revision") != revision or result.get("source_dirty") is not False
            or result.get("owner_environment_overrides") is not False):
        raise Refused("Base source did not pass the clean default route")
    if ((result.get("colony_report") or {}).get("Duplicants") != 3 or owner.get("OwnerConfigFlags") != 13
            or not (owner.get("PipeSave") or {}).get("AuditClosed")
            or not all(s.get("Passed") is True for s in owner.get("Scenarios", []))):
        raise Refused("Base source did not prove three alive, defaults and its checks")
    save = Path(result["host_saved_file"])
    data = save.read_bytes()
    _, header = save_header(data)
    if (hashlib.sha256(data).hexdigest() != result.get("host_saved_file_sha256")
            or header.get("numberOfDuplicants") != 3 or header.get("sandboxEnabled") is not False):
        raise Refused("Base save hash, living duplicants or sandbox header differs")
    return save


def prepared_autosave(output: Path, revision: str) -> Path:
    """Admit exactly one actual natural autosave of the completed prepared cycle."""
    result = read_json(output / "result.json")
    report = result.get("colony_report") or {}
    if report.get("LongRun") is not True or report.get("Duplicants") != 3:
        raise Refused("Prepared long run did not keep three duplicants alive")
    if (result.get("status") != "passed-colony-owner-load" or result.get("exit_code") != 0
            or result.get("source_revision") != revision or result.get("source_dirty") is not False
            or result.get("owner_environment_overrides") is not False):
        raise Refused("Prepared cycle did not pass the clean default route")
    autos = []
    for file in output.rglob("*.sav"):
        try:
            _, info = save_header(file.read_bytes())
        except (OSError, ValueError, struct.error):
            continue
        if info.get("isAutoSave") is True:
            autos.append(file)
    if len(autos) != 1:
        raise Refused(f"Expected exactly one natural autosave; found {len(autos)}")
    save = autos[0].resolve()
    build, header = save_header(save.read_bytes())
    if (not save.is_relative_to(output.resolve()) or build != 744825 or header.get("isAutoSave") is not True
            or header.get("numberOfCycles", 0) < 1 or header.get("numberOfDuplicants") != 3):
        raise Refused("The autosave header does not prove a day rollover with three duplicants")
    return save


def settings_source_save(output: Path) -> Path:
    result = read_json(output / "result.json")
    save = Path(result.get("host_saved_file") or "/nonexistent")
    if not save.is_file() or sha256(save) != result.get("host_saved_file_sha256"):
        raise Refused("Settings source save changed")
    return save


def spacedout_source_save(output: Path) -> Path:
    result = read_json(output / "result.json")
    save = Path(result.get("host_saved_file") or "/nonexistent")
    if result.get("status") != "passed-colony-owner" or not save.is_file() or sha256(save) != result.get("host_saved_file_sha256"):
        raise Refused("The Spaced Out source run did not record its saved colony")
    return save


def ui_manual_save(output: Path) -> Path:
    evidence = read_json(output / "ordinary-ui-evidence.json")
    save = Path(evidence.get("save") or "/nonexistent")
    if evidence.get("saves", 0) < 2 or not save.is_file() or sha256(save) != evidence.get("save_sha256"):
        raise Refused("The ordinary UI run did not record its manual save")
    return save


def ui_evidence(output: Path) -> dict:
    """Read back the clicked New Game: owner start, saves, desktop quit and the last save's header."""
    log = (output / "Player.log").read_text(errors="replace") if (output / "Player.log").exists() else ""
    start = re.search(r"[^\n]*\[Mixed Atmospheres\] Colony owner started:[^\n]*", log)
    saves = re.findall(r"Saved to \[C:\\([^\]]*\.sav)\]", log)
    evidence = {"owner_start": start.group(0) if start else None, "saves": len(saves),
                "quit_after_last_save": bool(saves) and "Game.OnApplicationQuit" in log[log.rfind("Saved to"):]}
    if saves:
        save = output / "compatdata/pfx/drive_c" / Path(*saves[-1].split("\\"))
        if save.is_file():
            data = save.read_bytes()
            info = embedded_header(data)
            evidence.update(save=str(save), save_sha256=hashlib.sha256(data).hexdigest(),
                            header={key: info.get(key) for key in ("baseName", "numberOfCycles", "numberOfDuplicants",
                                                                 "isAutoSave", "dlcIds", "clusterId", "sandboxEnabled")
                                    if key in info})
    write_json(output / "ordinary-ui-evidence.json", evidence)
    return evidence


# ---------------------------------------------------------------- the run

class Suite:
    def __init__(self, revision: str, tier: str, platforms: tuple[str, ...], layout: Layout, infra: Infra,
                 steps: list[Step], keep_going: bool):
        self.revision, self.tier, self.platforms = revision, tier, platforms
        self.layout, self.infra, self.steps, self.keep_going = layout, infra, steps, keep_going
        self.records: dict[str, dict] = {}
        self.packages: dict[str, dict] = {}
        self.summary = {"kind": "private-release-acceptance", "revision": revision, "tier": tier,
                        "platforms": list(platforms), "tag": layout.tag, "tool": str(Path(__file__).resolve()),
                        "tool_revision": git("rev-parse", "HEAD"), "started": now(), "source": str(layout.source),
                        "gpu_display": infra.gpu_display,
                        "proton": str(infra.proton), "linux_game": str(infra.linux_game),
                        "windows_game": str(infra.windows_game),
                        "frozen_save": {"path": str(FROZEN_SAVE), "sha256": FROZEN_SAVE_SHA256},
                        "tier_steps": [step.name for step in plan(tier, PLATFORMS)],
                        "requested_steps": [step.name for step in steps], "steps": []}

    def save(self) -> None:
        records = [self.records[step.name] for step in self.steps if step.name in self.records]
        tier = self.summary["tier_steps"]
        passed = {r["name"] for r in records if r["status"] == "passed"}
        self.summary.update(steps=records, packages=self.packages, updated=now(),
                            failed=[r["name"] for r in records if r["status"] == "failed"],
                            skipped=[r["name"] for r in records if r["status"] == "skipped"],
                            not_run=[name for name in tier if name not in passed and name not in self.records],
                            tier_passed=all(name in passed for name in tier))
        write_json(self.layout.summary, self.summary)

    def record(self, step: Step, status: str, **values) -> dict:
        record = {"name": step.name, "platform": step.platform, "kind": step.kind, "status": status, **values}
        self.records[step.name] = record
        self.save()
        print(f"[acceptance] {now()} {step.name}: {status}" +
              (f" {sorted(k for k, v in record.get('checks', {}).items() if not v)}" if status == "failed" else ""),
              flush=True)
        return record

    def run(self) -> int:
        self.layout.runs.mkdir(parents=True)
        self.save()
        stopped = False
        for step in self.steps:
            if stopped:
                break
            missing = [dep for dep in step.deps if self.records.get(dep, {}).get("status") != "passed"]
            if missing:
                self.record(step, "skipped", reason="dependency did not pass in this tag: " + ", ".join(missing))
                continue
            print(f"[acceptance] {now()} start {step.name}", flush=True)
            started = now()
            try:
                checks, details = self.execute(step)
                status = "passed" if checks and all(checks.values()) else "failed"
                self.record(step, status, started=started, finished=now(), checks=checks, **details)
            except Refused as error:
                self.record(step, "failed", started=started, finished=now(), checks={"preconditions": False},
                            error=str(error.code if isinstance(error.code, str) else error))
            except Exception as error:  # a checker or helper defect must not hide later steps
                self.record(step, "failed", started=started, finished=now(), checks={"completed_without_error": False},
                            error=repr(error))
            if self.records[step.name]["status"] == "failed" and not self.keep_going:
                stopped = True
        self.summary["finished"] = now()
        self.save()
        print(f"[acceptance] summary {self.layout.summary}: tier_passed={self.summary['tier_passed']} "
              f"failed={self.summary['failed']} skipped={self.summary['skipped']}", flush=True)
        requested = all(self.records.get(s.name, {}).get("status") == "passed" for s in self.steps)
        return 0 if requested else 2

    # Each step returns (checks, details).
    def execute(self, step: Step) -> tuple[dict, dict]:
        if step.kind == "build":
            build = build_linux if step.platform == "linux" else build_windows
            checks, record = build(self.layout, self.revision, self.infra)
            self.packages[step.platform] = {key: record.get(key) for key in (
                "package", "manifest_sha256", "file_count", "zip_sha256", "imports", "imports_system_only",
                "commands", "passed", "reused_verified_package")}
            return checks, {"build": self.packages[step.platform]}
        verify_source(self.layout, self.revision)
        package = self.layout.linux_package if step.platform == "linux" else self.layout.windows_package
        manifest = verify_package(package, self.revision)
        output = self.layout.runs / step.name
        log = self.layout.runs / (step.name + ".log")
        if output.exists() or log.exists():
            raise Refused(f"Refusing to reuse run output {output}")
        if step.platform == "linux":
            return self.linux(step, package, manifest, output, log)
        return self.windows(step, package, manifest, output, log)

    def game(self, command: list[str], log: Path, env: dict) -> int:
        with log.open("x") as stream:
            stream.write(f"[acceptance] {now()} {' '.join(command)}\n")
            stream.flush()
            return subprocess.run(slot(command), cwd=self.layout.source, env=env, stdout=stream,
                                  stderr=subprocess.STDOUT).returncode

    def linux(self, step: Step, package: Path, manifest: dict, output: Path, log: Path) -> tuple[dict, dict]:
        env = clean_env({"MA_COLONY_FAULT_DUMP": str(output / "fault-dump.bin")})
        common = {"output": str(output), "log": str(log), "package_source": manifest["source_revision"]}
        if step.kind != "cycle":
            rc = self.game(linux_command(step, package, output, self.infra), log, env)
            checks, details = linux_checks(rc, output)
            return checks, {**common, "returncode": rc, **details}
        source = ROOT / FROZEN_SAVE
        copy = stage_frozen_save(source, self.layout.runs / (step.name + "-input.sav"))
        manifest_sha = sha256(package / "package-manifest.json")
        hashes = package_hashes(package, manifest)
        if hashes != manifest["files"]:
            raise Refused("Frozen package file hashes do not match its manifest")
        env["MA_COLONY_DUPE_LOG"] = "1"
        rc = self.game(linux_command(step, package, output, self.infra, load=copy), log, env)
        checks, details = cycle_checks(rc, output, step.options["frames"], hashes, package, manifest, manifest_sha,
                                       source, copy)
        return checks, {**common, "returncode": rc, "copied_save": str(copy), **details}

    def windows(self, step: Step, package: Path, manifest: dict, output: Path, log: Path) -> tuple[dict, dict]:
        runs, kind = self.layout.runs, step.kind
        load = None
        if kind == "proton-prepared-cycle":
            load = base_source_save(runs / "windows-base-source", self.revision)
        elif kind == "proton-autosave-cold":
            load = prepared_autosave(runs / "windows-prepared-cycle", self.revision)
        elif kind == "proton-settings":
            if step.options["stage"] == "source":
                load = runs / "windows-settings-preset.json"
                if load.exists():
                    raise Refused(f"Refusing to reuse {load}")
                write_json(load, SETTINGS_PRESET)
            else:
                load = settings_source_save(runs / "windows-settings-source")
        elif kind == "proton-spacedout" and step.options["stage"] != "source":
            load = spacedout_source_save(runs / "windows-spacedout-source")
        elif kind == "proton-spacedout-ui-load":
            load = ui_manual_save(runs / "windows-spacedout-ui")
        preset = kind == "proton-settings" and step.options["stage"] == "source"
        load_sha = sha256(load) if load is not None and not preset else None
        command = windows_command(step, package, output, self.infra, self.layout.source, load)
        rc = self.game(command, log, clean_env())
        if kind == "proton-spacedout-ui" and output.exists():
            ui_evidence(output)
        checks, details = proton_checks(step, output, self.revision, manifest["files"])
        if load_sha is not None:
            checks["input_save_unchanged"] = load.is_file() and sha256(load) == load_sha
            details["input_save"] = {"path": str(load), "sha256": load_sha}
        return checks, {"output": str(output), "log": str(log), "returncode": rc, **details}


# ---------------------------------------------------------------- in-slot helpers

def proton_runner(source: Path):
    sys.path.insert(0, str(source / "tools"))
    import oni_proton_headless
    return oni_proton_headless


def longrun_wrapper(source: Path, arguments: list[str]) -> int:
    """Only the staged validation request changes: LongRun and 150-frame telemetry (prepared-cycle control)."""
    runner = proton_runner(source)
    if "--mode" not in arguments or arguments[arguments.index("--mode") + 1] != "colony-owner-load":
        raise SystemExit("Prepared owner load only")
    if any(flag in arguments for flag in ("--save-colony", "--resave-before-resume", "--reload-in-process",
                                          "--scenario", "--settings-preset", "--trace-shutdown")):
        raise SystemExit("No fixed-window or settings fixtures in a long run")
    output = Path(arguments[arguments.index("--output") + 1]).resolve()
    original = runner.common.write_json
    requested = {}

    def request(path, value):
        if Path(path).name == "colony-validation.json" and isinstance(value, dict) and value.get("Mode") == "colony-owner-load":
            value = {**value, "LongRun": True, "TelemetryIntervalFrames": 150}
            requested["value"] = value
        return original(path, value)

    runner.common.write_json = request
    sys.argv = ["oni_proton_headless.py", *arguments]
    try:
        code = runner.main()
    finally:
        path = output / "result.json"
        if path.exists():
            shutil.copy2(path, output / "result.canonical.json")
            result = json.loads(path.read_text())
            result["private_long_run_validation_control"] = {
                "LongRun": True, "TelemetryIntervalFrames": 150, "wrapper": str(Path(__file__).resolve()),
                "canonical_result_preserved": "result.canonical.json",
                "scope": "Only staged validation configuration. Enables the existing compiled alive-peak guard and "
                         "development telemetry; no environment/settings/physics/rate/bound changes."}
            if requested:
                original(output / "long-run-request.json", requested["value"])
            original(path, result)
    return code


def ordinary_ui_wrapper(source: Path, arguments: list[str]) -> int:
    """Normal New Game with Spaced Out! preferences, no colony validation and no MA_ variables."""
    runner = proton_runner(source)
    if arguments[arguments.index("--mode") + 1] != "menu":
        raise SystemExit("Ordinary menu only")
    if any(flag in arguments for flag in ("--scenario", "--load-save", "--save-colony", "--spaced-out-control",
                                          "--settings-preset", "--dlc-mask")):
        raise SystemExit("No colony fixtures")
    output = Path(arguments[arguments.index("--output") + 1]).resolve()
    original_init = runner.initialize_prefix

    def initialize(command, run_output, env):
        original_init(command, run_output, env)
        profile = run_output / PROTON_PROFILE
        if not profile.resolve().is_relative_to(run_output.resolve()):
            raise SystemExit("Profile escaped run")
        runner.common.oni_dlc.write_preferences(profile, 1)

    original_env = runner.game_env
    original_display = runner.start_display
    original_command = runner.sandbox_command

    def display(state, out, gpu_display=None):
        result = original_display(state, out, gpu_display)
        if gpu_display:
            subprocess.run(["xrandr", "--display", gpu_display, "--fb", "1920x1080"], check=True)
        return result

    def command(home, state, out, game, proton, tail):
        tail = list(tail)
        if "-screen-width" in tail:
            tail[tail.index("-screen-width") + 1] = "1920"
            tail[tail.index("-screen-height") + 1] = "1080"
        return original_command(home, state, out, game, proton, tail)

    runner.initialize_prefix = initialize
    runner.game_env = lambda *a, **k: {key: value for key, value in original_env(*a, **k).items() if not key.startswith("MA_")}
    runner.start_display = display
    runner.sandbox_command = command
    sys.argv = ["oni_proton_headless.py", *arguments]
    code = runner.main()
    path = output / "result.json"
    data = json.loads(path.read_text())
    validation = output / PROTON_PROFILE / "mods/Dev" / runner.MOD_ID / "colony-validation.json"
    data["ordinary_spacedout_ui"] = {"wrapper": str(Path(__file__).resolve()), "colony_validation_absent": not validation.exists(),
                                     "physics_environment_overrides": False,
                                     "scope": "Normal New Game and save clicks; only isolated DLC preferences and the settings-window hold."}
    runner.common.write_json(path, data)
    return code


class Desktop:
    """xdotool/scrot on the GPU display, in the 1280x720 coordinates of the recorded clicks."""

    def __init__(self, source: Path, display: str, output: Path, pause: float):
        sys.path.insert(0, str(source / "tools"))
        import oni_headless
        self.env = oni_headless.desktop_env(oni_headless.DEFAULT_STATE, {"display": display}, gpu=True)
        self.bin = oni_headless.DEFAULT_STATE / "tools/desktop/usr/bin"
        self.output, self.pause = output, pause

    def run(self, *command: str) -> str:
        return subprocess.run(list(command), env=self.env, check=True, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE).stdout

    def click(self, x: int, y: int) -> None:
        width, height = map(int, self.run(str(self.bin / "xdotool"), "getdisplaygeometry").split())
        self.run(str(self.bin / "xdotool"), "mousemove", str(x * width // 1280), str(y * height // 720), "click", "1")
        time.sleep(self.pause)

    def key(self, key: str) -> None:
        self.run(str(self.bin / "xdotool"), "key", "--clearmodifiers", key)
        time.sleep(.3)

    def shot(self, name: str) -> None:
        full = self.output / (name + "-full.png")
        self.run(str(self.bin / "scrot"), "-o", str(full))
        self.run("ffmpeg", "-loglevel", "error", "-y", "-i", str(full), "-vf", "scale=1280:720", str(self.output / (name + ".png")))
        full.unlink(missing_ok=True)


def ui_stage(stage: str, source: Path, output: Path, display: str, arguments: list[str]) -> int:
    """Run one WindowsPlayer UI control and drive it with the recorded clicks, in one game-slot hold."""
    this = [sys.executable, str(Path(__file__).resolve())]
    runner = [sys.executable, "tools/oni_proton_headless.py", *arguments] if stage == "settings" else \
        [*this, "_proton-ordinary-ui", "--source", str(source), "--", *arguments]
    console = output.parent / (output.name + ".runner.log")
    with console.open("x") as stream:
        process = subprocess.Popen(runner, cwd=source, stdout=stream, stderr=subprocess.STDOUT)

    def log() -> str:
        try:
            return (output / "Player.log").read_text(errors="replace")
        except OSError:
            return ""

    def wait(predicate, limit: float = 900, stop_on_exit: bool = True) -> None:
        deadline = time.monotonic() + limit
        while time.monotonic() < deadline:
            if predicate():
                return
            if stop_on_exit and process.poll() is not None:
                raise RuntimeError("The runner exited before the UI evidence")
            time.sleep(.25)
        raise TimeoutError("UI control timed out")

    desktop = None
    try:
        wait(lambda: output.is_dir())
        desktop = Desktop(source, display, output, .5 if stage == "settings" else .4)
        if stage == "settings":
            settings_clicks(desktop, output, log, wait)
        else:
            ordinary_clicks(desktop, log, wait)
    except BaseException:
        if desktop is not None:
            try:
                desktop.shot("control-error")
            except Exception:
                pass
        raise
    finally:
        rc = process.wait(timeout=1000)
    return rc


def settings_clicks(desktop: Desktop, output: Path, log, wait) -> None:
    wait(lambda: "Settings window opened (Solver)" in log())
    ids = desktop.run(str(desktop.bin / "xdotool"), "search", "--onlyvisible", "--name", "Oxygen Not Included").split()
    desktop.run(str(desktop.bin / "xdotool"), "windowfocus", "--sync", ids[-1])
    desktop.run(str(desktop.bin / "xdotool"), "key", "Escape")
    wait(lambda: "Settings window opened (Solver, colony settings read-only)" in log())
    report = output / PROTON_PROFILE / VALIDATION / "colony-validation.json"
    wait(lambda: read_json(report).get("Status") == "passed")
    time.sleep(1)
    desktop.shot("solver-readonly")
    desktop.click(642, 188); desktop.shot("breathing-readonly")
    desktop.click(904, 188); desktop.shot("performance")
    desktop.click(934, 128); time.sleep(.5)
    desktop.click(1255, 40); time.sleep(.5)
    desktop.click(641, 487); time.sleep(.5)
    desktop.click(641, 406)


def ordinary_clicks(desktop: Desktop, log, wait) -> None:
    """The recorded ordinary Classic New Game with Spaced Out!, explicit Save, then Quit to desktop."""
    shot = lambda name: desktop.shot("ordinary-" + name)
    wait(lambda: "-- MAIN MENU --" in log())
    time.sleep(8); shot("menu-settings")
    desktop.click(934, 128); time.sleep(1.5)
    desktop.click(641, 534); time.sleep(1.5)
    desktop.click(1224, 78); time.sleep(2); shot("menu")
    desktop.click(119, 166); time.sleep(2)
    desktop.click(577, 360); time.sleep(2); shot("style")
    # Classic style: the automated cluster controls exercise Spaced Out style.
    desktop.click(512, 360); time.sleep(2.5); shot("destination")
    desktop.click(912, 232); time.sleep(1.5)
    desktop.click(874, 692); time.sleep(4); shot("worldgen")
    wait(lambda: "Colony owner started:" in log(), 400)
    time.sleep(3); shot("duplicants-owner-active")
    desktop.click(903, 624); time.sleep(8)
    desktop.click(637, 449); time.sleep(4); shot("colony-owner-active")
    # --settings-open reopens the read-only settings window over the colony; it
    # would swallow the pause-menu clicks (bfb1168 recorded only the embark save).
    if "colony settings read-only)" in log():
        desktop.click(934, 128); time.sleep(1.5); shot("settings-closed")
    desktop.key("Escape"); time.sleep(1)
    desktop.click(1255, 40); time.sleep(1.5); shot("pause")
    desktop.click(641, 263); time.sleep(1.5); shot("save")
    saved = log().count("Saved to")
    desktop.click(641, 389)
    try:
        wait(lambda: log().count("Saved to") > saved, 40)
    except TimeoutError:
        pass
    time.sleep(1); shot("saved")
    desktop.click(1255, 40); time.sleep(1.5)
    desktop.click(641, 487); time.sleep(1.5); shot("quit")
    desktop.click(641, 406)


# ---------------------------------------------------------------- command line

def next_tag(base: Path, tier: str) -> str:
    number = 1
    while (base / "runs" / f"{tier}-v{number}").exists():
        number += 1
    return f"{tier}-v{number}"


def infra_from(args) -> Infra:
    try:
        common = common_root()
    except (OSError, subprocess.CalledProcessError):
        common = ROOT
    return Infra(
        linux_game=Path(args.linux_game or os.environ.get("ONI_GAME_DIR") or "/mnt/data/share/oni/OxygenNotIncluded-linux"),
        windows_game=Path(args.windows_game or os.environ.get("ONI_WINDOWS_GAME_DIR") or "/mnt/data/share/oni/OxygenNotIncluded"),
        proton=Path(args.proton or common / "build-proton-official10/proton-10.0-4b"),
        mingw_root=Path(args.mingw_root or common / "build-oni-cross-tools/root/usr"),
        wine_root=Path(args.wine_root or common / "build-wine/root"),
        gpu_display=args.gpu_display)


def describe(step: Step) -> dict:
    """A dry-run description of one step's game command (paths are placeholders)."""
    package = Path("<package>")
    output = Path("<runs>") / step.name
    infra = Infra(Path("<linux-game>"), Path("<windows-game>"), Path("<proton>"), Path("<mingw>"), Path("<wine>"), ":99")
    if step.kind == "build":
        command = ["<build, CTest, hook tests and package>"]
    elif step.platform == "linux":
        command = linux_command(step, package, output, infra, load=Path("<runs>") / (step.name + "-input.sav"))
    else:
        command = windows_command(step, package, output, infra, Path("<source>"), Path("<input>"))
    return {"name": step.name, "kind": step.kind, "deps": list(step.deps),
            "command": slot(command) if step.kind != "build" else command}


def parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("plan", "run"):
        command = sub.add_parser(name)
        if name == "run":
            command.add_argument("revision", help="A pushed commit on origin/main")
            command.add_argument("--tag", help="New run label (default: the next free <tier>-vN)")
            command.add_argument("--keep-going", action="store_true", help="Run every step even after a failure")
            command.add_argument("--no-fetch", action="store_true", help="Check against the current origin/main")
        command.add_argument("--tier", choices=TIERS, default="quick")
        command.add_argument("--platform", choices=("both", *PLATFORMS), default="both")
        command.add_argument("--only", help="Comma-separated step names (see plan)")
        command.add_argument("--gpu-display", default=os.environ.get("ONI_GPU_DISPLAY") or ":99")
        command.add_argument("--linux-game")
        command.add_argument("--windows-game")
        command.add_argument("--proton")
        command.add_argument("--mingw-root")
        command.add_argument("--wine-root")
    longrun = sub.add_parser("_proton-longrun")
    longrun.add_argument("--source", type=Path, required=True)
    ordinary = sub.add_parser("_proton-ordinary-ui")
    ordinary.add_argument("--source", type=Path, required=True)
    ui = sub.add_parser("_ui-stage")
    ui.add_argument("stage", choices=("settings", "ordinary-ui"))
    ui.add_argument("--source", type=Path, required=True)
    ui.add_argument("--output", type=Path, required=True)
    ui.add_argument("--gpu-display", required=True)
    return parser


def split_runner(argv: list[str]) -> tuple[list[str], list[str]]:
    """Internal wrappers take the runner's own arguments after a literal --."""
    if "--" in argv:
        at = argv.index("--")
        return argv[:at], argv[at + 1:]
    return argv, []


def main(argv: list[str] | None = None) -> int:
    own, runner = split_runner(list(sys.argv[1:] if argv is None else argv))
    args = parser().parse_args(own)
    if args.command == "_proton-longrun":
        os.chdir(args.source)
        return longrun_wrapper(args.source.resolve(), runner)
    if args.command == "_proton-ordinary-ui":
        os.chdir(args.source)
        return ordinary_ui_wrapper(args.source.resolve(), runner)
    if args.command == "_ui-stage":
        return ui_stage(args.stage, args.source.resolve(), args.output.resolve(), args.gpu_display, runner)
    if runner:
        raise SystemExit("Unexpected arguments after --")
    platforms = PLATFORMS if args.platform == "both" else (args.platform,)
    steps = select(plan(args.tier, platforms), args.only)
    if args.command == "plan":
        print(json.dumps({"tier": args.tier, "platforms": list(platforms), "steps": [describe(s) for s in steps]}, indent=2))
        return 0
    revision = resolve_revision(args.revision, fetch=not args.no_fetch)
    base = ROOT / "artifacts/acceptance" / revision[:7]
    tag = args.tag or next_tag(base, args.tier)
    if not re.fullmatch(r"[A-Za-z0-9_-]+", tag):
        raise SystemExit("unsafe tag")
    layout = Layout(base, tag)
    if layout.runs.exists():
        raise Refused(f"Refusing to reuse {layout.runs}")
    infra = infra_from(args)
    needed = [("linux game", infra.linux_game)] if "linux" in platforms else []
    if "windows" in platforms:
        needed += [("windows game", infra.windows_game), ("proton", infra.proton), ("mingw", infra.mingw_root),
                   ("wine", infra.wine_root)]
    for name, path in needed:
        if not path.exists():
            raise Refused(f"Missing {name}: {path}")
    prepare_source(layout, revision)
    print(f"[acceptance] {revision} tier {args.tier} tag {tag}: {len(steps)} steps; summary {layout.summary}", flush=True)
    return Suite(revision, args.tier, platforms, layout, infra, steps, args.keep_going).run()


if __name__ == "__main__":
    sys.exit(main())
