#!/usr/bin/env python3
"""Collect native evidence without converting the unresolved Cartesian gate to PASS.

Build separately first. Output must be a new directory: logs are captured from
fresh subprocesses, never accepted from an earlier run. Uses only the standard
library and standalone native binaries; no game assemblies are read.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TESTS = {
    "physics_and_abi": "ma_tests",
    "integrated_fluid_world": "ma_fluid_tests",
    "integrated_fluid_free_surfaces": "ma_free_surface_tests",
    "gas_vacuum_consistency": "ma_gas_vacuum_tests",
    "pressure_preconditioning_and_rollback": "ma_pressure_tests",
    "mixture_property_compatibility": "ma_mixture_properties_tests",
    "retained_occupied_staggered": "ma_occupied_staggered_tests",
}
EXPERIMENT_TESTS = {
    "positive_staggered_rezone": "ma_occupied_staggered_remap_tests",
    "retained_hybrid_geometry": "ma_occupied_staggered_hybrid_tests",
    "protected_hybrid_material_rezone": "ma_occupied_staggered_hybrid_remap_tests",
    "protected_hybrid_first_boundary_event": "ma_occupied_staggered_hybrid_event_tests",
    "protected_hybrid_passive_diagnostic": "ma_occupied_staggered_hybrid_passive_tests",
    "protected_hybrid_nonlinear_passive": "ma_occupied_staggered_hybrid_nonlinear_tests",
}
REFERENCE_TESTS = {"liquid_reference_thermodynamics": "liquid_reference_tests"}
LEGACY = "positive_staggered_rezone"
LEGACY_SOURCE = "native/tests/occupied_staggered_remap_tests.cpp"
LEGACY_SOURCE_SHA256 = "f2184c67aac6191a7d63923b57faa45f6d1fbf09f15f49298546de223a3b37ba"
LEGACY_PASSES = (
    "independent dyadic primal/corner/node oracle",
    "bitwise identity, vacuum and uniform translation",
    "Cartesian planner and positive free cuts",
    "independent fixed-wall impulse and K-to-U",
    "independent gravitational first moment and entropy/domain",
    "positive denormal shares and atomic failures",
)
LEGACY_FAILURE = (
    "manufactured carrier and free-boundary refinement: "
    "Nonuniform free boundary has an unbounded per-call smoothing defect as dt tends to zero"
)
LEGACY_SUMMARY = "6/7 remap-only groups passed; no composed timestep/front gate claimed"
GROUP_SUMMARIES = {
    "retained_occupied_staggered": "17/17 retained material-state groups passed; no Eulerian remap/front gate claimed",
    "retained_hybrid_geometry": "9/9 hybrid A0 geometry groups passed; no mechanical rezone, pressure adapter or front gate claimed",
    "protected_hybrid_material_rezone": "12/12 hybrid A1 material-rezone groups passed; no repeated-rate, pressure adapter or front gate claimed",
    "protected_hybrid_first_boundary_event": "4/4 hybrid first-event test groups passed; inner-cap projection jumps remain; no whole-A2/repeated-rate/refinement/pressure/front gate claimed",
    "protected_hybrid_passive_diagnostic": "PASSIVE_DIAGNOSTIC cases=128 trend_failures=0; exact physical outer identity retained; inner cap jumps remain; no whole-A2/nonlinear/pressure/front gate claimed",
    "protected_hybrid_nonlinear_passive": "NONLINEAR_PASSIVE cases=304; native physical/identity checks passed; independent nonlinear oracle required; all-node-rate/whole-A2/pressure/front unvalidated",
}
NEW_SOURCES = {
    "occupied_staggered_remap.cpp", "occupied_staggered_hybrid.cpp", "occupied_staggered_hybrid_remap.cpp",
    "occupied_staggered_remap_tests.cpp", "occupied_staggered_hybrid_tests.cpp",
    "occupied_staggered_hybrid_remap_tests.cpp", "occupied_staggered_hybrid_event_tests.cpp",
    "occupied_staggered_hybrid_passive_tests.cpp",
    "occupied_staggered_hybrid_nonlinear_tests.cpp",
}
NONLINEAR = "protected_hybrid_nonlinear_passive"
SANITIZER = re.compile(
    r"\b(?:AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|MemorySanitizer|ThreadSanitizer|Sanitizer)\b|"
    r"\bruntime error:", re.I
)
RESULT_ROW = re.compile(
    r"^\s*\d+/\d+\s+Test\s+#(\d+):\s+(\S+)\s+\.{2,}\s*(.+?)\s+[\d.]+\s+sec\s*$", re.M
)
TOTAL_ROW = re.compile(r"^\d+% tests passed, (\d+) tests failed out of (\d+)\s*$", re.M)
RESULT_OVERRIDES = {
    "WILL_FAIL", "DISABLED", "PASS_REGULAR_EXPRESSION", "FAIL_REGULAR_EXPRESSION",
    "SKIP_RETURN_CODE", "SKIP_REGULAR_EXPRESSION",
}


class EvidenceError(ValueError):
    """The requested evidence contract was not met."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise EvidenceError(message)


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path: Path, value: object) -> None:
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")


@dataclass(frozen=True)
class Run:
    arguments: tuple[str, ...]
    cwd: str
    returncode: int | None
    timed_out: bool
    stdout: bytes
    log: str

    @property
    def text(self) -> str:
        try:
            # Parse platform line endings consistently; the saved bytes/hash stay
            # exactly as emitted by the subprocess, including Windows CRLF.
            return self.stdout.decode("utf-8").replace("\r\n", "\n")
        except UnicodeError as error:
            raise EvidenceError("Subprocess output was not UTF-8; preserve raw log and investigate") from error


def capture(arguments: list[str], cwd: Path, output: Path, name: str, timeout: float) -> Run:
    require(Path(name).name == name and name.endswith(".log"), "Log name must be a local .log filename")
    started = time.monotonic()
    # Exclusive creation happens before launching: a stale log cannot be consumed
    # or overwritten, even if it contains exactly the expected old failure text.
    with (output / name).open("xb") as stream:
        try:
            completed = subprocess.run(arguments, cwd=cwd, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, timeout=timeout, check=False)
            raw, code, timed_out = completed.stdout, completed.returncode, False
        except subprocess.TimeoutExpired as error:
            raw, code, timed_out = error.stdout or b"", None, True
        stream.write(raw)
    result = Run(tuple(arguments), str(cwd), code, timed_out, raw, name)
    write_json(output / (name + ".json"), {
        "arguments": arguments, "cwd": str(cwd), "returncode": code, "timed_out": timed_out,
        "elapsed_seconds": time.monotonic() - started,
        "stdout_sha256": hashlib.sha256(raw).hexdigest(), "log": name,
    })
    return result


def clean_run(run: Run, expected_code: int) -> str:
    require(not run.timed_out, f"Timed out: {run.log}")
    require(run.returncode == expected_code,
            f"{run.log}: expected exit {expected_code}, got {run.returncode}")
    require(SANITIZER.search(run.text) is None, f"Sanitizer diagnostic: {run.log}")
    return run.text


def validate_discovery(text: str, expected: dict[str, str], build: Path) -> dict[str, list[str]]:
    require(SANITIZER.search(text) is None, "Sanitizer diagnostic during discovery")
    try:
        document = json.loads(text)
    except (ValueError, TypeError) as error:
        raise EvidenceError("CTest discovery was not JSON") from error
    require(isinstance(document, dict) and document.get("kind") == "ctestInfo", "Wrong CTest discovery kind")
    tests = document.get("tests")
    require(isinstance(tests, list), "Missing CTest discovery tests")
    names = [test.get("name") for test in tests if isinstance(test, dict)]
    require(all(isinstance(name, str) and name for name in names) and
            len(names) == len(tests) == len(set(names)), "Malformed or duplicate test registrations")
    require(set(names) == set(expected), f"Wrong registered tests: expected {sorted(expected)}, got {sorted(names)}")
    commands = {}
    for test in tests:
        name = test["name"]
        command = test.get("command")
        require(isinstance(command, list) and len(command) == 1 and isinstance(command[0], str),
                f"Wrapped, missing or argument-modified native test command: {name}")
        binary = Path(command[0]).resolve()
        require(binary.parent == build.resolve() and binary.name in (expected[name], expected[name] + ".exe"),
                f"Wrong native executable: {name}")
        require(binary.is_file(), f"Native executable missing: {binary}")
        properties = test.get("properties", [])
        require(isinstance(properties, list), f"Malformed properties: {name}")
        property_names = [prop.get("name") for prop in properties if isinstance(prop, dict)]
        require(all(isinstance(key, str) and key for key in property_names) and
                len(property_names) == len(properties) == len(set(property_names)), f"Malformed or duplicate properties: {name}")
        require(not RESULT_OVERRIDES.intersection(property_names), f"Test result override forbidden: {name}")
        for prop in properties:
            if prop["name"] == "WORKING_DIRECTORY":
                require(Path(prop["value"]).resolve() == build.resolve(), f"Wrong native working directory: {name}")
        commands[name] = command
    return commands


def native_lines(text: str, identifier: int) -> str:
    return "\n".join(re.findall(rf"^{identifier}: (.*)$", text, re.M))


def validate_legacy_body(text: str) -> None:
    require(SANITIZER.search(text) is None, "Sanitizer diagnostic in legacy body")
    passes = re.findall(r"^PASS (.*)$", text, re.M)
    failures = re.findall(r"^FAIL (.*)$", text, re.M)
    summaries = re.findall(r"^\d+/\d+ remap-only groups passed.*$", text, re.M)
    require(passes == list(LEGACY_PASSES), "Legacy six PASS groups missing, reordered, duplicated or changed")
    require(failures == [LEGACY_FAILURE], "Legacy failure differs from the preserved strict assertion")
    require(summaries == [LEGACY_SUMMARY], "Legacy 6/7 result missing, duplicated or changed")


def validate_direct_legacy(run: Run) -> None:
    validate_legacy_body(clean_run(run, 1))


def validate_ctest(run: Run, expected: set[str], failed: set[str] | None = None) -> dict[str, int]:
    failed = failed or set()
    require(failed <= expected, "Invalid expected failed-test set")
    text = clean_run(run, 8 if failed else 0)
    rows = RESULT_ROW.findall(text)
    names = [row[1] for row in rows]
    require(len(names) == len(set(names)) and set(names) == expected, f"Wrong executed test set: {run.log}")
    require(TOTAL_ROW.findall(text) == [(str(len(failed)), str(len(expected)))], f"Wrong CTest summary: {run.log}")
    identifiers = {}
    for number, name, status in rows:
        require(status == ("***Failed" if name in failed else "Passed"), f"Wrong test disposition: {name}: {status}")
        identifiers[name] = int(number)
        body = native_lines(text, int(number))
        if name == LEGACY:
            validate_legacy_body(body)
        if name in GROUP_SUMMARIES:
            require(body.splitlines().count(GROUP_SUMMARIES[name]) == 1, f"Missing or duplicate scoped group summary: {name}")
    require(len(set(identifiers.values())) == len(identifiers), "Duplicate CTest output identifiers")
    return identifiers


def validate_reference(run: Run) -> None:
    identifiers = validate_ctest(run, set(REFERENCE_TESTS))
    body = native_lines(run.text, identifiers["liquid_reference_thermodynamics"])
    passes = re.findall(r"^PASS (.*)$", body, re.M)
    require(len(passes) == len(set(passes)) == 26 and not re.findall(r"^FAIL ", body, re.M),
            "Independent thermodynamic reference must execute all 26 distinct passing groups")


def nonlinear_output(run: Run, build: Path, before: set[Path], *, enclosing_ctest_exit: int = 0) -> Path:
    """Accept only this subprocess's newly created native dataset, once."""
    require(enclosing_ctest_exit in (0, 8), "Invalid enclosing CTest disposition")
    text = clean_run(run, enclosing_ctest_exit)
    paths = re.findall(r"^(?:\d+: )?NONLINEAR_OUTPUT (.+)$", text, re.M)
    require(len(paths) == 1, "Missing or duplicate fresh nonlinear output marker")
    path = Path(paths[0]).resolve()
    parent = (build / "hybrid-nonlinear-profiles").resolve()
    require(path.parent == parent and path not in before and path.is_dir(), "Stale or foreign nonlinear dataset")
    after = {p.resolve() for p in parent.iterdir() if p.is_dir()}
    require(after - before == {path}, "Nonlinear run created an unexpected dataset set")
    require(not (path / "FIRST_FAILURE.json").exists(), "Native nonlinear numerical failure exists")
    try:
        metadata = json.loads((path / "COMPLETE.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise EvidenceError("Missing complete nonlinear dataset") from error
    require(metadata == {"schema": 1, "cases": 304, "nonlinear": 288, "uniform": 16,
                         "native_physical_checks": True, "independent_oracle_required": True, "whole_A2": False},
            "Wrong nonlinear completion metadata")
    return path


def validate_nonlinear_oracle(run: Run, output: Path) -> None:
    clean_run(run, 0)
    require(not (output / "FIRST_FAILURE.json").exists(), "Independent nonlinear numerical failure exists")
    result = json.loads((output / "summary.json").read_text(encoding="utf-8"))
    require(result.get("oracle_passed") is True and result.get("native_cases") == 304 and
            result.get("whole_A2") is False and result.get("all_node_rate") is False and
            result.get("pressure_front") is False, "Incomplete or overclaimed nonlinear oracle result")


def read_cache(build: Path) -> dict[str, str]:
    values = {}
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("//", "#")):
            continue
        match = re.fullmatch(r"([^:=]+):[^=]+=(.*)", line)
        require(match is not None, "Malformed CMake cache entry")
        key, value = match.groups()
        require(key not in values, f"Duplicate CMake cache key: {key}")
        values[key] = value
    return values


def validate_compile_graph(build: Path, experimental: bool) -> None:
    commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    require(isinstance(commands, list), "Missing compile command graph")
    names = {Path(command["file"]).name for command in commands}
    require({"occupied_staggered.cpp", "occupied_staggered_tests.cpp"} <= names, "Default retained state/test omitted")
    require(NEW_SOURCES <= names if experimental else not NEW_SOURCES.intersection(names),
            "Compile graph disagrees with experiment option")
    for item in commands:
        name = Path(item["file"]).name
        if name in NEW_SOURCES or name in {"occupied_staggered.cpp", "occupied_staggered_tests.cpp"}:
            command = item.get("command", "")
            msvc = "/W4" in command
            require((msvc and ("/WX" in command or "-WX" in command) and "/permissive-" in command) or
                    (not msvc and all(flag in command for flag in
                                     ("-Wall", "-Wextra", "-Wpedantic", "-Wconversion", "-Wshadow", "-Werror"))),
                    f"Strict warning flags missing: {name}")
        output = (item.get("output", "") + " " + item.get("command", "")).replace("\\", "/")
        if "CMakeFiles/ma_core.dir/" in output or "CMakeFiles/mixed_atmospheres.dir/" in output:
            require(not name.startswith("occupied_staggered"), "Experimental source entered ma_core/DLL")


def source_paths() -> list[Path]:
    native = [path for path in (ROOT / "native").rglob("*") if path.is_file() and
              (path.suffix in {".cpp", ".hpp", ".h", ".md"} or path.name == "CMakeLists.txt")]
    explicit = [ROOT / path for path in (
        "CMakeLists.txt", "tools/build.py", "tools/validate_staggered_experiments.py",
        "tools/tests/test_staggered_evidence.py", "tools/check_hybrid_nonlinear_oracle.py",
        "tools/tests/test_hybrid_nonlinear_oracle.py", ".github/workflows/staggered-experiments.yml",
    )]
    return sorted(set(native + explicit))


def inventory(paths: list[Path], base: Path) -> list[dict[str, object]]:
    return [{"file": str(path.relative_to(base)).replace("\\", "/"), "bytes": path.stat().st_size,
             "sha256": sha256(path)} for path in paths]


def main(argv: list[str] | None = None) -> int:
    require(sys.version_info >= (3, 11), "Python 3.11 or newer is required")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--reference-build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--mode", choices=("default", "experimental"), required=True)
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--timeout-seconds", type=float, default=300)
    parser.add_argument("--oracle-timeout-seconds", type=float, default=1800)
    args = parser.parse_args(argv)
    require(math.isfinite(args.timeout_seconds) and args.timeout_seconds > 0, "Timeout must be finite and positive")
    require(math.isfinite(args.oracle_timeout_seconds) and args.oracle_timeout_seconds > 0, "Oracle timeout must be finite and positive")
    build, reference, output = args.build_dir.resolve(), args.reference_build_dir.resolve(), args.output_dir.resolve()
    require(build != reference, "Reference must use its separate build project")
    output.mkdir(parents=True, exist_ok=False)
    summary = {
        "schema_version": 1, "run_id": str(uuid.uuid4()), "utc": datetime.now(timezone.utc).isoformat(),
        "mode": args.mode, "procedure_succeeded": False, "strict_cartesian_gate": "NOT_RUN",
        "whole_a2": False, "full_physics_accepted": False, "game_assemblies_used": False,
        "build_dir": str(build), "reference_build_dir": str(reference),
        "python": sys.version, "platform": platform.platform(),
        "sanitizer_environment": {key: os.environ.get(key) for key in ("ASAN_OPTIONS", "UBSAN_OPTIONS")},
    }
    runs: list[Run] = []

    def execute(arguments: list[str], cwd: Path, name: str) -> Run:
        result = capture(arguments, cwd, output, name, args.timeout_seconds)
        runs.append(result)
        return result

    def nonlinear_before() -> set[Path]:
        parent = build / "hybrid-nonlinear-profiles"
        return {p.resolve() for p in parent.iterdir() if p.is_dir()} if parent.is_dir() else set()

    def run_nonlinear_oracle(run: Run, before: set[Path], label: str, *, enclosing_ctest_exit: int = 0) -> None:
        data = nonlinear_output(run, build, before, enclosing_ctest_exit=enclosing_ctest_exit)
        archived = output / (label + "-nonlinear-input")
        shutil.copytree(data, archived)
        # Explicit fresh Python subprocess; no native CTest command wrapper or
        # parser-only success can substitute for the independent numeric gate.
        oracle_output = output / (label + "-nonlinear-oracle")
        oracle = capture([sys.executable, str(ROOT / "tools/check_hybrid_nonlinear_oracle.py"),
                          "--input", str(archived), "--output", str(oracle_output)],
                         ROOT, output, label + "-nonlinear-oracle.log", args.oracle_timeout_seconds)
        runs.append(oracle)
        validate_nonlinear_oracle(oracle, oracle_output)
        summary["bounded_nonlinear_passive"] = "PASSED_BOUNDED_ONLY"

    failure = None
    try:
        paths = source_paths()
        before = inventory(paths, ROOT)
        write_json(output / "source-before.json", before)
        for path in paths:
            archived = output / "source-snapshot" / path.relative_to(ROOT)
            archived.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, archived)
            require(sha256(archived) == sha256(path), "Source archive copy differs")
        require(sha256(ROOT / LEGACY_SOURCE) == LEGACY_SOURCE_SHA256, "Preserved strict Cartesian test source changed")
        cache, reference_cache = read_cache(build), read_cache(reference)
        experimental = args.mode == "experimental"
        require(cache.get("MA_BUILD_TESTS") == "ON", "Native tests must be enabled")
        require(cache.get("MA_BUILD_STAGGERED_EXPERIMENTS") == ("ON" if experimental else "OFF"), "CMake option disagrees with requested scope")
        require(Path(cache["CMAKE_HOME_DIRECTORY"]).resolve() == ROOT, "Build has foreign source root")
        require(Path(reference_cache["CMAKE_HOME_DIRECTORY"]).resolve() == ROOT / "native/experimental", "Reference has foreign source root")
        validate_compile_graph(build, experimental)
        for label, directory in (("native", build), ("reference", reference)):
            for filename in ("CMakeCache.txt", "compile_commands.json"):
                shutil.copyfile(directory / filename, output / (label + "-" + filename))
        clean_run(execute([args.cmake, "--version"], ROOT, "cmake-version.log"), 0)
        clean_run(execute([args.ctest, "--version"], ROOT, "ctest-version.log"), 0)
        compiler = cache["CMAKE_CXX_COMPILER"]
        compiler_args = [compiler] if Path(compiler).name.lower() in {"cl", "cl.exe"} else [compiler, "--version"]
        clean_run(execute(compiler_args, ROOT, "compiler-version.log"), 0)
        expected = DEFAULT_TESTS | (EXPERIMENT_TESTS if experimental else {})
        discovered = execute([args.ctest, "--test-dir", str(build), "--show-only=json-v1"], ROOT, "native-discovery.log")
        commands = validate_discovery(clean_run(discovered, 0), expected, build)
        timeout = str(max(1, int(args.timeout_seconds) - 1))
        base_command = [args.ctest, "--test-dir", str(build), "--timeout", timeout, "-V"]
        positive_command = base_command + (["-E", "^" + LEGACY + "$"] if experimental else [])
        before_nonlinear = nonlinear_before() if experimental else set()
        positive = execute(positive_command, ROOT, "applicable-ctest.log")
        validate_ctest(positive, set(expected) - ({LEGACY} if experimental else set()))
        if experimental:
            run_nonlinear_oracle(positive, before_nonlinear, "applicable")
        summary["applicable_tests_passed"] = len(expected) - int(experimental)
        if experimental:
            direct = execute(commands[LEGACY], build, "strict-cartesian-direct.log")
            summary["strict_direct_exit"] = direct.returncode
            summary["strict_cartesian_gate"] = "UNEXPECTED_PASS" if direct.returncode == 0 else "UNVERIFIED_FAILURE"
            validate_direct_legacy(direct)
            negative = execute(base_command + ["-R", "^" + LEGACY + "$"], ROOT, "strict-cartesian-ctest.log")
            summary["strict_ctest_exit"] = negative.returncode
            validate_ctest(negative, {LEGACY}, {LEGACY})
            before_nonlinear = nonlinear_before()
            unfiltered = execute(base_command, ROOT, "unfiltered-ctest.log")
            summary["unfiltered_ctest_exit"] = unfiltered.returncode
            validate_ctest(unfiltered, set(expected), {LEGACY})
            # The actual enclosing CTest remains exit8. Dataset validation
            # receives that already-verified disposition without rewriting it.
            run_nonlinear_oracle(unfiltered, before_nonlinear, "unfiltered", enclosing_ctest_exit=8)
            summary.update(strict_cartesian_gate="FAILED", expected_failure_reproduced=True, strict_groups="6/7")
        else:
            summary.update(strict_cartesian_gate="NOT_BUILT_OPT_IN_REQUIRED", expected_failure_reproduced=False)
        reference_discovered = execute([args.ctest, "--test-dir", str(reference), "--show-only=json-v1"], ROOT, "reference-discovery.log")
        validate_discovery(clean_run(reference_discovered, 0), REFERENCE_TESTS, reference)
        reference_run = execute([args.ctest, "--test-dir", str(reference), "--timeout", timeout, "-V"], ROOT, "reference-ctest.log")
        validate_reference(reference_run)
        summary["reference_groups_passed"] = 26
        binary_files = sorted(path for directory in (build, reference) for path in directory.iterdir()
                              if path.is_file() and (path.suffix in {".exe", ".dll", ".lib", ".a", ".so"} or
                                                     path.name in set(expected.values()) | set(REFERENCE_TESTS.values())))
        write_json(output / "binaries.json", [{"file": str(path), "bytes": path.stat().st_size,
                                               "sha256": sha256(path)} for path in binary_files])
        if experimental:
            profiles = build / "hybrid-passive-profiles"
            require(profiles.is_dir(), "Passive profile output missing")
            shutil.copytree(profiles, output / "passive-profiles")
        after = inventory(paths, ROOT)
        write_json(output / "source-after.json", after)
        require(before == after, "Source changed during validation")
        for run in runs:
            require(sha256(output / run.log) == hashlib.sha256(run.stdout).hexdigest(), "Captured log changed after execution")
        summary["procedure_succeeded"] = True
    except (EvidenceError, OSError, KeyError, TypeError, ValueError) as error:
        failure = f"{type(error).__name__}: {error}"
        summary["failure"] = failure
    finally:
        # Even native/independent first failure keeps complete raw packets and
        # states; never delete/relabel it or rely on a former passing dataset.
        if args.mode == "experimental":
            profiles = build / "hybrid-nonlinear-profiles"
            if profiles.is_dir():
                try:
                    shutil.copytree(profiles, output / "all-nonlinear-runs", dirs_exist_ok=False)
                except OSError as error:
                    summary["procedure_succeeded"] = False
                    summary["failure_capture_error"] = f"{type(error).__name__}: {error}"
                    failure = failure or summary["failure_capture_error"]
        write_json(output / "summary.json", summary)
        frozen = sorted(path for path in output.rglob("*") if path.is_file())
        write_json(output / "evidence-freeze.json", inventory(frozen, output))
    print(json.dumps(summary, indent=2, sort_keys=True))
    if failure:
        print(f"Evidence procedure FAILED: {failure}", file=sys.stderr)
    return 0 if summary["procedure_succeeded"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (EvidenceError, OSError) as error:
        print(f"Evidence procedure FAILED before fresh collection: {error}", file=sys.stderr)
        sys.exit(1)
