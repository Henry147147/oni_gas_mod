#!/usr/bin/env python3
"""Read-only ONI installation discovery and managed/native boundary inspection.

Only standard-library Python is required. No game, Steam, or authentication
process is launched. Reports go to stdout; no project/game files are written.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys


APP_ID = "457140"
PRUNE = {".git", ".cache", ".npm", "node_modules", ".venv", "venv", "__pycache__"}
API_TYPES = ("Sim", "SimMessages", "ConduitTemperatureManager")
ASSEMBLIES = (
    "Assembly-CSharp.dll", "Assembly-CSharp-firstpass.dll", "0Harmony.dll",
    "UnityEngine.CoreModule.dll", "UnityEngine.dll",
)
MARKERS = {"OxygenNotIncluded.exe", "OxygenNotIncluded", "OxygenNotIncluded.x86_64"}


def resolve(path: Path) -> Path:
    return path.expanduser().resolve()


def vdf_value(text: str, key: str) -> str | None:
    match = re.search(r'"' + re.escape(key) + r'"\s*"([^"\r\n]*)"', text)
    return match.group(1).replace("\\\\", "\\") if match else None


def read_small(path: Path, limit: int = 1024 * 1024) -> str:
    with path.open("rb") as stream:
        return stream.read(limit).decode("utf-8", errors="replace")


def default_roots() -> list[Path]:
    home = Path.home()
    return [
        Path.cwd(), home / ".local/share/Steam", home / ".steam/steam",
        home / ".steam/root", home / ".local/share/SteamCMD", home / "steamcmd",
        Path("/root"), Path("/opt"), Path("/mnt"), Path("/media"),
        Path("/srv"), Path("/home/steam"),
    ]


def discover(roots: list[Path], max_files: int) -> tuple[set[Path], list[dict], list[dict]]:
    """Find game markers/manifests and follow Steam library paths, never log VDFs."""
    games: set[Path] = set()
    manifests: list[dict] = []
    scans: list[dict] = []
    pending = list(roots)
    visited: set[Path] = set()
    while pending:
        root = resolve(pending.pop(0))
        if root in visited:
            continue
        visited.add(root)
        scan = {"root": str(root), "exists": root.is_dir(), "files_checked": 0,
                "read_errors": 0, "limit_reached": False}
        scans.append(scan)
        if not root.is_dir():
            continue

        def onerror(_error: OSError) -> None:
            scan["read_errors"] += 1

        for directory, dirs, files in os.walk(root, followlinks=False, onerror=onerror):
            dirs[:] = [name for name in dirs if name not in PRUNE]
            parent = Path(directory)
            for filename in files:
                scan["files_checked"] += 1
                if scan["files_checked"] > max_files:
                    scan["limit_reached"] = True
                    break
                path = parent / filename
                if filename in MARKERS:
                    games.add(resolve(parent))
                elif filename == "Assembly-CSharp.dll" and parent.name == "Managed":
                    if parent.parent.name == "OxygenNotIncluded_Data":
                        games.add(resolve(parent.parent.parent))
                    elif "OxygenNotIncluded.app" in path.parts:
                        app_index = path.parts.index("OxygenNotIncluded.app")
                        games.add(Path(*path.parts[:app_index + 1]))
                elif filename == "libraryfolders.vdf":
                    try:
                        contents = read_small(path)
                        # Read only library locations, never print login/account data.
                        for match in re.finditer(r'"path"\s*"([^"\r\n]+)"', contents):
                            pending.append(Path(match.group(1).replace("\\\\", "\\")) / "steamapps")
                    except OSError:
                        scan["read_errors"] += 1
                elif filename == f"appmanifest_{APP_ID}.acf":
                    try:
                        contents = read_small(path)
                        item = {"path": str(path), "steam_app_id": vdf_value(contents, "appid"),
                                "steam_build_id": vdf_value(contents, "buildid")}
                        install_dir = vdf_value(contents, "installdir")
                        if install_dir:
                            game = resolve(parent / "common" / install_dir)
                            item["game_path"] = str(game)
                            item["game_path_exists"] = game.is_dir()
                            if game.is_dir():
                                games.add(game)
                        manifests.append(item)
                    except OSError:
                        scan["read_errors"] += 1
            if scan["limit_reached"]:
                break
    return games, manifests, scans


def binary_info(path: Path) -> dict:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        header = stream.read(4096)
        digest.update(header)
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    item = {"path": str(path), "bytes": path.stat().st_size, "sha256": digest.hexdigest()}
    if header.startswith(b"\x7fELF") and len(header) >= 20:
        endian = "<" if header[5] == 1 else ">"
        item["format"] = "ELF"
        item["bits"] = {1: 32, 2: 64}.get(header[4])
        item["machine"] = struct.unpack(endian + "H", header[18:20])[0]
    elif header.startswith(b"MZ") and len(header) >= 64:
        item["format"] = "PE"
        pe_offset = struct.unpack("<I", header[60:64])[0]
        with path.open("rb") as stream:
            stream.seek(pe_offset)
            pe = stream.read(6)
        if pe[:4] == b"PE\0\0":
            item["machine"] = struct.unpack("<H", pe[4:6])[0]
    elif header[:4] in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe"):
        item["format"] = "Mach-O"
    else:
        item["format"] = "unknown"
    return item


def unity_version(path: Path) -> str | None:
    try:
        with path.open("rb") as stream:
            contents = stream.read(1024 * 1024)
        match = re.search(rb"(?:20\d\d|6000)\.\d+\.\d+[abfp]\d+", contents)
        return match.group().decode("ascii") if match else None
    except OSError:
        return None


def api_excerpt(source: str) -> str:
    """Extract visible declarations from ILSpy C#, excluding method bodies.

    This is a convenience excerpt, not a C# parser or ABI verification. Includes
    nested fields and interop attributes. Multiline declarations are joined.
    """
    result: list[str] = []
    pending: list[str] = []
    start = re.compile(r"^(?:public|private|internal|protected)\s")
    for line in source.splitlines():
        stripped = line.strip()
        if stripped.startswith("//"):
            continue
        if stripped.startswith("["):
            if any(word in stripped for word in ("DllImport", "StructLayout", "MarshalAs", "FieldOffset")):
                result.append(stripped)
            continue
        if pending or start.match(stripped):
            pending.append(stripped)
            joined = " ".join(pending)
            if "{" in joined or ";" in joined or "=>" in joined or ")" in joined:
                declaration = joined.split("{", 1)[0].split("=>", 1)[0].strip()
                result.append(declaration)
                pending = []
    if pending:
        result.append(" ".join(pending))
    return "\n".join(result)


def dump_api(assembly: Path, executable: str, types: list[str]) -> dict:
    result = {}
    for name in types:
        try:
            command = [executable, "-t", name, str(assembly)]
            process = subprocess.run(command, capture_output=True, text=True, timeout=90)
            result[name] = {"command": command, "exit_code": process.returncode}
            if process.returncode == 0:
                result[name]["declarations"] = api_excerpt(process.stdout)
            else:
                result[name]["error"] = process.stderr.strip()[:4000]
        except (OSError, subprocess.TimeoutExpired) as error:
            result[name] = {"error": str(error)}
    return result


def native_exports(path: Path) -> dict:
    is_pe = path.suffix.lower() == ".dll"
    executable = shutil.which("objdump" if is_pe else "nm")
    if not executable:
        return {"error": "Binary inspection tool unavailable; inspect exports with a platform-native tool."}
    try:
        command = [executable, "-p", str(path)] if is_pe else [executable, "-D", "--defined-only", str(path)]
        process = subprocess.run(command, capture_output=True, text=True, timeout=30)
        return {"command": command, "exit_code": process.returncode,
                "simulation_symbols": sorted(set(re.findall(
                    r"\b(?:SIM_|ConduitTemperatureManager_|SYSINFO_)[A-Za-z_][A-Za-z0-9_]*", process.stdout))),
                "error": process.stderr.strip()[:2000] if process.returncode else None}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"error": str(error)}


def inspect_game(game: Path, args: argparse.Namespace) -> dict:
    data_candidates = [game / "OxygenNotIncluded_Data", game / "Contents/Resources/Data"]
    data = next((path for path in data_candidates if path.is_dir()), None)
    record = {"game_path": str(game), "exists": game.is_dir(), "data_path": str(data) if data else None,
              "oni_build": None, "unity_version": None, "managed_assemblies": [], "native_libraries": [],
              "runtime_evidence": [], "errors": []}
    if data is None:
        record["errors"].append("ONI data directory not found; installation is not verified.")
        return record
    managed = data / "Managed"
    for filename in ASSEMBLIES:
        path = managed / filename
        if path.is_file():
            try:
                record["managed_assemblies"].append(binary_info(path))
            except OSError as error:
                record["errors"].append(str(error))
    record["unity_version"] = unity_version(data / "globalgamemanagers")
    # Native plugin locations vary by platform; inspect only game-owned trees.
    for directory in [data / "Plugins", game / "Contents/PlugIns"]:
        if not directory.is_dir():
            continue
        for parent, _dirs, files in os.walk(directory, followlinks=False):
            for filename in files:
                if re.match(r"^(?:lib)?SimDLL(?:\.dll|\.so(?:\.\d+)*|\.dylib)?$", filename, flags=re.IGNORECASE):
                    try:
                        path = Path(parent) / filename
                        info = binary_info(path)
                        if args.native_exports:
                            info["export_inspection"] = native_exports(path)
                        record["native_libraries"].append(info)
                    except OSError as error:
                        record["errors"].append(str(error))
    for path in [game / "MonoBleedingEdge", game / "Contents/Frameworks/MonoBleedingEdge",
                 data / "il2cpp_data", data / "Managed"]:
        if path.exists():
            record["runtime_evidence"].append(str(path))
    assembly = managed / "Assembly-CSharp.dll"
    if args.dump_api and assembly.is_file():
        if args.ilspycmd:
            record["managed_api_excerpts"] = dump_api(assembly, args.ilspycmd, args.api_type or list(API_TYPES))
            try:
                process = subprocess.run([args.ilspycmd, "-t", "BuildWatermark", str(assembly)],
                                         capture_output=True, text=True, timeout=90)
                match = re.search(r"\+\s*(\d{5,8})u\b", process.stdout) if process.returncode == 0 else None
                if match:
                    record["oni_build"] = int(match.group(1))
                    record["oni_build_source"] = "BuildWatermark.GetBuildText uint literal (decompiled metadata evidence)"
            except (OSError, subprocess.TimeoutExpired) as error:
                record["errors"].append(str(error))
        else:
            record["errors"].append("API dump requested but ilspycmd is unavailable.")
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, action="append", default=[], help="Explicit game root; repeatable.")
    parser.add_argument("--scan-root", type=Path, action="append", help="Replace default search roots; repeatable.")
    parser.add_argument("--max-files", type=int, default=250000, help="Limit files checked per root.")
    parser.add_argument("--dump-api", action="store_true", help="Run installed ILSpy and include declaration excerpts.")
    parser.add_argument("--native-exports", action="store_true", help="Use nm/objdump to list ELF/PE simulation exports.")
    parser.add_argument("--api-type", action="append", help="Type to inspect; repeatable; defaults to Sim/SimMessages/conduit thermals.")
    local_ilspy = Path.home() / ".local/share/oni-mod-tools/ilspycmd"
    parser.add_argument("--ilspycmd", default=shutil.which("ilspycmd") or (str(local_ilspy) if local_ilspy.is_file() else None),
                        help="Installed ilspycmd executable path.")
    parser.add_argument("--player-log", type=Path, help="Optional user-selected Player.log; extract only ONI build markers.")
    args = parser.parse_args()
    if args.max_files <= 0:
        parser.error("--max-files must be positive")
    games, manifests, scans = discover(args.scan_root or default_roots(), args.max_files)
    games.update(resolve(path) for path in args.game)
    report = {
        "schema_version": 1,
        "host": {"system": platform.system(), "machine": platform.machine(), "python": platform.python_version()},
        "tools": {name: shutil.which(name) for name in ("dotnet", "steam", "steamcmd", "ilspycmd", "readelf", "nm", "objdump")},
        "scan_policy": {"follow_directory_symlinks": False, "pruned_directory_names": sorted(PRUNE)},
        "scans": scans, "steam_manifests": manifests,
        "installations": [inspect_game(path, args) for path in sorted(games)],
        "proof_missing": [
            "Steam build IDs are not ONI build numbers; verify the game banner or a selected Player.log.",
            "Paths and decompiled declarations do not prove native ABI layouts, callback timing, or hook correctness.",
            "API excerpts are heuristic; inspect full declarations locally before implementing native interop.",
        ],
    }
    report["tools"]["ilspycmd"] = args.ilspycmd
    if args.player_log:
        try:
            contents = read_small(resolve(args.player_log), 16 * 1024 * 1024)
            report["log_build_markers"] = sorted(set(re.findall(r"\bU\d{1,3}-\d{5,8}\b", contents)))
        except OSError as error:
            report["log_error"] = str(error)
    if not any(item["managed_assemblies"] for item in report["installations"]):
        report["proof_missing"].insert(0, "No ONI managed assemblies found; no in-game/native compatibility has been verified.")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
