"""Check a packaged adapter with ONI's Windows Mono, on Windows or under Wine."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("game-dir", "adapter", "runner", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--wine", type=Path, help="Wine wrapper configured with a private prefix and Z: drive")
    args = parser.parse_args()
    if args.wine is None and os.name != "nt":
        parser.error("Pass --wine for a Windows player test on this host.")

    game = args.game_dir.resolve()
    output = args.output.resolve()
    adapter = args.adapter.resolve(strict=True)
    command = [str(args.wine.resolve(strict=True))] if args.wine else []
    command.append(str(args.runner.resolve(strict=True)))
    subprocess.run(["dotnet", "build", str(Path(__file__).resolve().parent), "-c", "Release",
                    "-p:UseSharedCompilation=false", "-p:OniGameDir=" + str(game),
                    "--artifacts-path", str(output)], check=True)
    test, = (output / "bin/WindowsHarmonyPageTests").rglob("WindowsHarmonyPageTests.dll")
    mono = game / "MonoBleedingEdge"

    def player_path(path):
        return "Z:" + str(path).replace("/", "\\") if args.wine else str(path)

    command.extend(player_path(p) for p in (mono / "EmbedRuntime/mono-2.0-bdwgc.dll",
                   mono / "lib", mono / "etc", game / "OxygenNotIncluded_Data/Managed", test))
    subprocess.run(command, check=True, env={**os.environ, "MA_TEST_WINDOWS_ADAPTER": player_path(adapter)})


if __name__ == "__main__":
    main()
