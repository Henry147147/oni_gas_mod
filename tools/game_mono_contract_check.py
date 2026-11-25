#!/usr/bin/env python3
"""Check a mod folder's game contract inside the game's own Mono runtime.

Compiles tools/game_mono_contract_check.c and runs it with the licensed Linux
installation's Mono, without Unity (docs/game-update-resilience.md#verification).
The runtime is laid out in a private temporary folder of links so its config
finds libmono-native.so; nothing in the installation is changed.
"""
from __future__ import annotations

import argparse
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
    parser.add_argument("--other-mod", action="store_true", help="Control: first load tools/GameContractOtherMod, which patches three game "
                        "methods the mod depends on as an earlier mod would; every difference must then be reported as another mod's patch "
                        "and the notice must name that mod (docs/game-update-resilience.md#other-mods)")
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
        result = subprocess.run(command + [str(work / "other/GameContractOtherMod.dll")], capture_output=True, text=True)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        if result.returncode != 0:
            return result.returncode
        # The runtime may print complaints of its own before the report (Unity's natives are absent here).
        report, _, notice = result.stdout[max(result.stdout.find(args.platform + " (game Mono)"), 0):].partition("\nnotice:\n")
        differences = [line.strip() for line in report.splitlines()[1:]]
        failures = []
        if len(differences) < 3 or any(not line.startswith("patched by another mod: ") for line in differences):
            failures.append("every difference must be reported as another mod's patch, and at least the three patched methods")
        if "3 game methods it uses were patched by" not in notice or "GameContractOtherMod (3 methods)" not in notice:
            failures.append("the notice must count the three patched methods once each")
        for name in ("SaveLoader::Save(", "CustomGameSettings::SetQualitySetting(", "LoadScreen::DoLoad("):
            if not any(name in line for line in differences):
                failures.append(f"{name}...) is not among the differences")
        for text in ("changed game code it depends on", "neither is out of date", "Try disabling"):
            if text not in notice:
                failures.append(f"the notice lacks {text!r}")
        for failure in failures:
            print("FAILED: " + failure, file=sys.stderr)
        if not failures:
            print(f"other-mod control passed: {len(differences)} differences, all attributed to the other mod")
        return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
