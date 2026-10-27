#!/usr/bin/env python3
"""Build and verify standalone components; optionally compile the ONI probe.

Uses only the Python standard library. No game DLLs are copied into artifacts.
On Windows run inside an x64 Visual Studio developer command environment.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(arguments: list[str], env: dict[str, str] | None = None) -> None:
    print("+ " + " ".join(arguments), flush=True)
    subprocess.run(arguments, cwd=ROOT, env=env, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--game-dir", type=Path, default=os.environ.get("ONI_GAME_DIR"))
    parser.add_argument("--sanitizers", action="store_true")
    parser.add_argument("--native-only", action="store_true")
    parser.add_argument("--oni-native-observer", action="store_true", help="Build forwarding SimDLL hooks and relocation/ABI tests; no licensed game required")
    args = parser.parse_args()
    directory = (args.build_dir or ROOT / ("build-windows" if os.name == "nt" else "build")).resolve()
    if platform.system() not in ("Windows", "Linux"):
        parser.error("Only Windows and Linux are targeted at this milestone")
    run([sys.executable, "tools/coverage_inventory.py", "validate"])
    run([sys.executable, "-m", "unittest", "discover", "-s", "tools/tests", "-v"])
    run(["cmake", "-S", str(ROOT), "-B", str(directory), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         "-DMA_SANITIZERS=" + ("ON" if args.sanitizers else "OFF"), "-DMA_BUILD_STAGGERED_EXPERIMENTS=OFF",
         "-DMA_BUILD_ONI_NATIVE_BACKEND=" + ("ON" if args.oni_native_observer else "OFF")])
    run(["cmake", "--build", str(directory), "--parallel", "8"])
    run(["ctest", "--test-dir", str(directory), "--output-on-failure"])
    if args.native_only:
        return
    if args.sanitizers:
        parser.error("Use --native-only with --sanitizers; managed host ASan preloading is a separate test configuration")
    env = os.environ.copy()
    variable = "PATH" if os.name == "nt" else "LD_LIBRARY_PATH"
    env[variable] = str(directory) + os.pathsep + env.get(variable, "")
    env["MA_TEST_NATIVE_LIBRARY"] = str(directory / ("mixed_atmospheres.dll" if os.name == "nt" else "libmixed_atmospheres.so"))
    run(["dotnet", "run", "--project", "managed/MixedAtmospheres.Tests", "-c", "Release"], env)
    run(["dotnet", "run", "--project", "managed/MixedAtmospheres.Presentation.Tests", "-c", "Release"], env)
    run(["dotnet", "run", "--project", "tools/ColonyConduitTests", "-c", "Release"], env)
    run(["dotnet", "run", "--project", "tools/ColonyWorldLayoutTests", "-c", "Release"], env)
    run(["dotnet", "run", "--project", "tools/SettingsTests", "-c", "Release"], env)
    run(["dotnet", "run", "--project", "tools/OwnedReservoirCheckpointTests", "-c", "Release"], env)
    run(["dotnet", "run", "--project", "tools/OwnedReservoirSavedInspectionTests", "-c", "Release"], env)
    run(["dotnet", "run", "--project", "tools/OwnedReservoirInflationTests", "-c", "Release"], env)
    run(["dotnet", "run", "--project", "tools/OwnedReservoirLayoutTests", "-c", "Release"], env)
    run(["dotnet", "run", "--project", "tools/OwnedReservoirFilePromotionTests", "-c", "Release",
         "--", str(directory / "file-promotion-checks")], env)
    if args.game_dir:
        run(["dotnet", "build", "managed/MixedAtmospheres.Oni", "-c", "Release", "--nologo",
             "-p:OniGameDir=" + str(Path(args.game_dir).resolve())])
        run(["dotnet", "run", "--project", "tools/ColonyConduitHookTests", "-c", "Release",
             "-p:OniGameDir=" + str(Path(args.game_dir).resolve()), "--", str(Path(args.game_dir).resolve())])
        run(["dotnet", "run", "--project", "tools/OwnedSnapshotTests", "-c", "Release",
             "-p:OniGameDir=" + str(Path(args.game_dir).resolve()), "--", str(Path(args.game_dir).resolve())])
        run(["dotnet", "run", "--project", "tools/OwnedReservoirTests", "-c", "Release",
             "-p:OniGameDir=" + str(Path(args.game_dir).resolve()), "--", str(Path(args.game_dir).resolve())])
        run(["dotnet", "run", "--project", "tools/OwnedReservoirMarkerTests", "-c", "Release",
             "-p:OniGameDir=" + str(Path(args.game_dir).resolve()), "--", str(Path(args.game_dir).resolve())])
        run(["dotnet", "run", "--project", "tools/MixtureInspectionTests", "-c", "Release",
             "-p:OniGameDir=" + str(Path(args.game_dir).resolve()), "--", str(Path(args.game_dir).resolve())])
        run(["dotnet", "run", "--project", "tools/NativeDescriptionTests", "-c", "Release",
             "-p:OniGameDir=" + str(Path(args.game_dir).resolve()), "--", str(Path(args.game_dir).resolve())])
        run(["dotnet", "run", "--project", "tools/NitrogenRegistrationTests", "-c", "Release",
             "-p:OniGameDir=" + str(Path(args.game_dir).resolve()), "--", str(Path(args.game_dir).resolve())])


if __name__ == "__main__":
    main()
