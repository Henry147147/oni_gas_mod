"""Write and check the managed game contract for a packaged mod assembly.

Thin wrapper over tools/GameContract (docs/game-update-resilience.md). The
contract holds member names, signatures and hashes from the licensed game,
never game code; it ships beside the mod's DLLs as game-contract.json.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "tools/GameContract"
FILE_NAME = "game-contract.json"
# The private installations the team builds against (tools/ONI_ENVIRONMENT.md).
DEFAULT_GAMES = {"windows-x64": Path("/mnt/data/share/oni/OxygenNotIncluded"),
                 "linux-x64": Path("/mnt/data/share/oni/OxygenNotIncluded-linux")}


def game_root(platform: str, explicit: Path | None = None) -> Path:
    """The installation to write a platform's contract against."""
    candidates = [explicit] if explicit else []
    environment = os.environ.get("ONI_GAME_DIR_WINDOWS" if platform == "windows-x64" else "ONI_GAME_DIR_LINUX")
    if environment:
        candidates.append(Path(environment))
    if platform == "linux-x64" and os.environ.get("ONI_GAME_DIR"):
        candidates.append(Path(os.environ["ONI_GAME_DIR"]))
    candidates.append(DEFAULT_GAMES[platform])
    for candidate in candidates:
        if candidate and (candidate / "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll").is_file():
            return candidate.resolve()
    raise ValueError(f"No {platform} ONI installation found for the game contract; pass its root explicitly")


def _run(*arguments: str) -> None:
    command = ["dotnet", "run", "--project", str(PROJECT), "-c", "Release", "-p:UseSharedCompilation=false", "--", *arguments]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        raise ValueError("GameContract " + arguments[0] + " failed:\n" + (result.stdout + result.stderr)[-4000:])
    print(result.stdout.strip(), flush=True)


def generate(managed: Path, games: dict[str, Path], output: Path) -> None:
    """Write the contract for the mod assembly in `managed` against each platform's game."""
    arguments = ["generate", "--mod", str(managed.resolve())]
    for platform, root in sorted(games.items()):
        arguments += ["--game", f"{platform}={root}"]
    _run(*arguments, "--output", str(output.resolve()))
    for platform, root in sorted(games.items()):
        _run("verify", "--mod", str(managed.resolve()), "--game", f"{platform}={root}", "--contract", str(output.resolve()))


def generate_single(managed: Path, platform: str, root: Path, output: Path, raw: bool = False) -> None:
    """One platform's contract, optionally with full values for diffs."""
    _run("generate", "--mod", str(managed.resolve()), "--game", f"{platform}={root}", "--output", str(output.resolve()), *(["--raw"] if raw else []))


def differences(managed: Path, platform: str, root: Path, contract: Path) -> list[str]:
    """What the game check would report for this installation; empty when verified."""
    command = ["dotnet", "run", "--project", str(PROJECT), "-c", "Release", "-p:UseSharedCompilation=false", "--",
               "verify", "--mod", str(managed.resolve()), "--game", f"{platform}={root}", "--contract", str(contract.resolve())]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    lines = result.stdout.splitlines()
    if result.returncode not in (0, 1) or not any(line.startswith(platform + ":") for line in lines):
        raise ValueError("GameContract verify failed:\n" + (result.stdout + result.stderr)[-4000:])
    return [line.strip() for line in lines if line.startswith("  ")]
