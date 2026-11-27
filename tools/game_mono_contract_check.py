#!/usr/bin/env python3
"""Check a mod folder's game contract inside the game's own Mono runtime.

Compiles tools/game_mono_contract_check.c and runs it with the licensed Linux
installation's Mono, without Unity (docs/game-update-resilience.md#verification).
The runtime is laid out in a private temporary folder of links so its config
finds libmono-native.so; nothing in the installation is changed.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path("/mnt/data/share/oni/OxygenNotIncluded-linux"))
    parser.add_argument("--mod", type=Path, required=True, help="Folder with MixedAtmospheres.Oni.dll and game-contract.json")
    parser.add_argument("--managed-game", type=Path, help="Installation whose assemblies are checked (default --game); "
                        "the Windows copy runs its assemblies on the Linux Mono runtime")
    parser.add_argument("--platform", choices=("linux-x64", "windows-x64"), default="linux-x64")
    parser.add_argument("--other-mod", nargs="?", const="patched", choices=("patched", "removed"),
                        help="Control: first load tools/GameContractOtherMod as an earlier mod (docs/game-update-resilience.md#other-mods). "
                        "'patched' (the default): it patches three game methods the mod depends on; every difference must be another "
                        "mod's patch, attributed to the stand-in, and the build must be verified. 'removed': it patches two of them and "
                        "removes its patches again (MA_OTHER_MOD_REMOVES_PATCHES=1); the patch mark stays, no patch accounts for it, so "
                        "the build must be refused as changed game code while the load entries stay verified")
    args = parser.parse_args()
    game, mod = args.game.resolve(), args.mod.resolve()
    with tempfile.TemporaryDirectory() as folder:
        work = Path(folder)
        host = work / "game_mono_contract_check"
        subprocess.run(["cc", "-O2", "-Wall", "-o", str(host), str(ROOT / "tools/game_mono_contract_check.c"), "-ldl"], check=True)
        managed = work / "Managed"; managed.mkdir()
        for file in ((args.managed_game or game).resolve() / "OxygenNotIncluded_Data/Managed").iterdir():
            (managed / file.name).symlink_to(file)
        (work / "lib").symlink_to(game / "OxygenNotIncluded_Data/MonoBleedingEdge/x86_64")
        command = [str(host), str(game), str(mod), str(managed), str(managed), args.platform]
        if not args.other_mod:
            return subprocess.run(command).returncode
        subprocess.run(["dotnet", "build", str(ROOT / "tools/GameContractOtherMod"), "-c", "Release", "-o", str(work / "other"), "--nologo", "-v", "quiet",
                        f"-p:OniGameDir={(args.managed_game or game).resolve()}", f"-p:BaseIntermediateOutputPath={work / 'other-obj'}/"], check=True)
        env = {**os.environ, "MA_OTHER_MOD_REMOVES_PATCHES": "1" if args.other_mod == "removed" else "0"}
        result = subprocess.run(command + [str(work / "other/GameContractOtherMod.dll")], capture_output=True, text=True, env=env)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        if result.returncode != 0:
            return result.returncode
        failures = judge_other_mod(args.other_mod, result.stdout[max(result.stdout.find(args.platform + " (game Mono)"), 0):])
        for failure in failures:
            print("FAILED: " + failure, file=sys.stderr)
        if not failures:
            print(f"other-mod control ({args.other_mod}) passed")
        return 1 if failures else 0


def judge_other_mod(kind: str, report: str) -> list[str]:
    """The failures of an --other-mod control, judged from GameContractCheck.SelfCheck's text.

    The runtime may print complaints of its own before the report (Unity's natives are absent here);
    the caller passes the text from the report's first line on.
    """
    head, _, verified = report.partition("\nverified:")
    head, _, notice = head.partition("\nnotice:\n")
    # A game newer than the contract's build (docs/game-update-resilience.md#tolerated-newer-builds):
    # the entries forgiven for the build number follow the differences.
    head, newer, _ = head.partition("\nnewer than the contract's build")
    differences = [line.strip() for line in head.splitlines()[1:] if line.strip()]
    failures = []
    if kind == "patched":
        if len(differences) < 3 or any(not line.startswith("patched by another mod: ") for line in differences):
            failures.append("every difference must be reported as another mod's patch, and at least the three patched methods")
        for name in ("SaveLoader::Save(", "CustomGameSettings::SetQualitySetting(", "LoadScreen::DoLoad("):
            if not any(name in line for line in differences):
                failures.append(f"{name}...) is not among the differences")
        if notice or not verified:
            failures.append("the build must be verified, not refused")
        if "3 game methods the mod uses carry other mods' Harmony patches" not in verified:
            failures.append("the verified line must count the three patched methods once each")
        lines = [line.strip() for line in verified.splitlines()[1:]]
        for method, owners in (("SaveLoader.Save", ("example.first-mod", "example.second-mod")),
                               ("CustomGameSettings.SetQualitySetting", ("example.first-mod",)),
                               ("LoadScreen.DoLoad", ("example.first-mod",))):
            line = next((line for line in lines if line.startswith(method + " <- ")), "")
            if not line.startswith(method + " <- GameContractOtherMod (Harmony: ") or any(owner not in line for owner in owners):
                failures.append(f"{method} must be attributed to the stand-in and its Harmony IDs {', '.join(owners)}")
        if len(lines) != 3:
            failures.append(f"the verified line must list exactly three methods, not {len(lines)}")
    else:
        if verified or not notice:
            failures.append("the build must be refused")
        if any(line.startswith("patched by another mod: ") for line in differences):
            failures.append("a removed patch must not be reported as another mod's patch")
        for name in ("changed SaveLoader::Save(", "changed CustomGameSettings::SetQualitySetting("):
            if not any(line.startswith(name) for line in differences):
                failures.append(f"{name}...) is not among the differences")
        if any("LoadScreen::DoLoad(" in line or "MainMenu::ResumeGame(" in line for line in differences):
            failures.append("the load entries must stay verified, so that the load guard is installed")
        # On a newer game the same changed code needs a mod update; on a known build it is the game's files.
        expected = (("The game update changed", "SaveLoader.Save", "An update of Mixed Atmospheres is needed") if newer
                    else ("differ from that version", "SaveLoader.Save", "Verify integrity of game files"))
        for text in expected:
            if text not in notice:
                failures.append(f"the notice lacks {text!r}")
    return failures

if __name__ == "__main__":
    sys.exit(main())
