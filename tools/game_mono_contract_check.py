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
        return subprocess.run([str(host), str(game), str(mod), str(managed), str(managed), args.platform]).returncode


if __name__ == "__main__":
    sys.exit(main())
