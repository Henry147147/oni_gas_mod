"""Run the actual production Harmony wrappers in the installed game's Mono.

Only public runtime entry points are used. No game code, assembly or captured
payload is part of this test source; fake game endpoints make the guards,
exceptions and callback ordering deterministic.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--runner", type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parent
    game = args.game_dir.resolve()
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run(["dotnet", "build", str(project), "-c", "Release", "-p:OniGameDir=" + str(game), "-p:CreditPython=" + sys.executable, "--artifacts-path", str(output / "managed")], check=True)
    assemblies = list((output / "managed/bin/ColonyPacingCreditHarmonyTests").rglob("ColonyPacingCreditHarmonyTests.dll"))
    if len(assemblies) != 1:
        raise ValueError(f"Expected one test assembly: {assemblies}")
    assembly = assemblies[0]
    data = game / "OxygenNotIncluded_Data"
    runtime_name = "EmbedRuntime/mono-2.0-bdwgc.dll" if os.name == "nt" else "x86_64/libmonobdwgc-2.0.so"
    candidates = [data / "MonoBleedingEdge", game / "MonoBleedingEdge"]
    runtime = next((candidate for candidate in candidates if (candidate / runtime_name).is_file()), None)
    if runtime is None:
        raise FileNotFoundError("A host-compatible installed game Mono runtime is required; Windows Mono under Wine uses the same runner source compiled as PE.")
    if args.runner:
        runner = args.runner.resolve()
    else:
        runner = output / ("mono-runner.exe" if os.name == "nt" else "mono-runner")
        compiler = os.environ.get("CXX") or ("cl" if os.name == "nt" and shutil.which("cl") else "c++")
        if Path(compiler).stem.lower() in ("cl", "clang-cl"):
            command = [compiler, "/nologo", "/EHsc", "/std:c++17", str(project / "mono_runner.cpp"), "/Fe:" + str(runner)]
        else:
            command = [compiler, "-std=c++17", "-O2", str(project / "mono_runner.cpp"), "-o", str(runner)]
            if os.name != "nt": command += ["-ldl"]
        subprocess.run(command, cwd=output, check=True)
    command = [str(runner), str(runtime / runtime_name), str(runtime / "lib"), str(runtime / "etc"), str(data / "Managed"), str(assembly)]
    environment = os.environ.copy()
    for key in ("MA_COLONY_PACING_CREDIT", "MA_COLONY_PACING_CREDIT_LOG", "MA_COLONY_PACING_LOSS_LOG", "MA_TEST_CREDIT_REQUIRE_ENABLED"):
        environment.pop(key, None)
    subprocess.run(command, env=environment, check=True)
    environment["MA_COLONY_PACING_CREDIT"] = "1"
    environment["MA_COLONY_PACING_CREDIT_LOG"] = "1"
    subprocess.run(command, env=environment, check=True)
    environment["MA_COLONY_PACING_CREDIT"] = "0"
    environment["MA_TEST_CREDIT_REQUIRE_ENABLED"] = "1"
    negative = subprocess.run(command, env=environment, capture_output=True, text=True)
    if negative.returncode == 0 or "Harmony retained check" not in negative.stderr:
        raise RuntimeError("Disabled negative control did not reject the enabled Harmony oracle:\n" + negative.stdout + negative.stderr)
    print("PASS credit opt-out negative control rejects the enabled Harmony oracle", flush=True)
    environment.pop("MA_TEST_CREDIT_REQUIRE_ENABLED")
    subprocess.run(command, env=environment, check=True)
    environment["MA_COLONY_PACING_LOSS_LOG"] = "1"
    subprocess.run(command, env=environment, check=True)
    environment.pop("MA_COLONY_PACING_CREDIT")
    environment.pop("MA_COLONY_PACING_CREDIT_LOG")
    subprocess.run(command, env=environment, check=True)
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    (output / "mono-provenance.json").write_text(json.dumps({"scope": "Actual installed game Mono against fake endpoints; no actual game/Windows OS acceptance and no Mono allocation claim", "mono": digest(runtime / runtime_name), "harmony": digest(data / "Managed/0Harmony.dll"), "test": digest(assembly), "runner_source": digest(project / "mono_runner.cpp"), "runner": digest(runner)}, indent=2) + "\n")


if __name__ == "__main__":
    main()
