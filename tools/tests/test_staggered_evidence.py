"""Fail-closed evidence plumbing; these synthetic transcripts are not physics proof."""
from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import validate_staggered_experiments as evidence


# Literal native output from the preserved strict failure, independent of the
# parser's expected-name constants. Diagnostics between group rows are omitted.
LEGACY_OUTPUT = """PASS independent dyadic primal/corner/node oracle
PASS bitwise identity, vacuum and uniform translation
PASS Cartesian planner and positive free cuts
PASS independent fixed-wall impulse and K-to-U
PASS independent gravitational first moment and entropy/domain
PASS positive denormal shares and atomic failures
FAIL manufactured carrier and free-boundary refinement: Nonuniform free boundary has an unbounded per-call smoothing defect as dt tends to zero
6/7 remap-only groups passed; no composed timestep/front gate claimed
"""


def run(text: str, code: int = 0, timed_out: bool = False) -> evidence.Run:
    return evidence.Run(("synthetic-transcript-only",), ".", code, timed_out, text.encode(), "synthetic.log")


def ctest_output(names: list[str], failures: set[str] | None = None) -> str:
    failures = failures or set()
    lines = []
    for identifier, name in enumerate(names, 1):
        if name == evidence.LEGACY:
            body = LEGACY_OUTPUT
        elif name == "liquid_reference_thermodynamics":
            body = "\n".join(f"PASS synthetic reference fixture {index}" for index in range(evidence.REFERENCE_GROUPS))
        else:
            body = evidence.GROUP_SUMMARIES.get(name, "PASS synthetic fixture")
        lines += [f"{identifier}: {line}" for line in body.splitlines()]
        status = "***Failed" if name in failures else "Passed"
        lines.append(f"{identifier}/{len(names)} Test #{identifier}: {name} ............ {status} 0.01 sec")
    lines.append(f"{round(100 * (len(names) - len(failures)) / len(names))}% tests passed, {len(failures)} tests failed out of {len(names)}")
    return "\n".join(lines) + "\n"


class StaggeredEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name).resolve()
        self.expected = evidence.DEFAULT_TESTS | evidence.EXPERIMENT_TESTS
        for name in self.expected.values():
            (self.directory / name).write_bytes(b"synthetic discovery fixture; never executed")
        self.discovery = {"kind": "ctestInfo", "tests": [
            {"name": name, "command": [str(self.directory / binary)],
             "properties": [{"name": "WORKING_DIRECTORY", "value": str(self.directory)}]}
            for name, binary in self.expected.items()
        ]}

    def discover(self, value=None, expected=None):
        return evidence.validate_discovery(json.dumps(self.discovery if value is None else value),
                                           self.expected if expected is None else expected, self.directory)

    def test_default_seven_and_opt_in_thirteen_are_distinct_complete_sets(self):
        self.assertEqual(len(self.discover()), 13)
        default = copy.deepcopy(self.discovery)
        default["tests"] = [test for test in default["tests"] if test["name"] in evidence.DEFAULT_TESTS]
        self.assertEqual(len(self.discover(default, evidence.DEFAULT_TESTS)), 7)
        self.assertIn("retained_occupied_staggered", self.discover(default, evidence.DEFAULT_TESTS))
        with self.assertRaises(evidence.EvidenceError):
            self.discover(default)

    def test_missing_legacy_or_retained_registration_cannot_be_skip(self):
        for name in (evidence.LEGACY, "retained_occupied_staggered"):
            value = copy.deepcopy(self.discovery)
            value["tests"] = [test for test in value["tests"] if test["name"] != name]
            with self.subTest(name=name), self.assertRaises(evidence.EvidenceError):
                self.discover(value)

    def test_duplicate_foreign_and_malformed_registration_rejected(self):
        for mutation in (lambda tests: tests.append(copy.deepcopy(tests[0])),
                         lambda tests: tests[0].update(name="unreviewed-test"),
                         lambda tests: tests[0].update(name=None)):
            value = copy.deepcopy(self.discovery)
            mutation(value["tests"])
            with self.assertRaises(evidence.EvidenceError):
                self.discover(value)

    def test_wrapped_argument_modified_foreign_or_missing_binary_rejected(self):
        for command in ([sys.executable, "wrapper.py"], [str(self.directory / "ma_tests"), "--skip"],
                        [str(self.directory.parent / "ma_tests")], [str(self.directory / "absent")]):
            value = copy.deepcopy(self.discovery)
            value["tests"][0]["command"] = command
            with self.subTest(command=command), self.assertRaises(evidence.EvidenceError):
                self.discover(value)

    def test_result_overrides_and_foreign_working_directory_rejected(self):
        for prop in ("WILL_FAIL", "DISABLED", "PASS_REGULAR_EXPRESSION", "SKIP_RETURN_CODE"):
            value = copy.deepcopy(self.discovery)
            value["tests"][0]["properties"].append({"name": prop, "value": False})
            with self.subTest(prop=prop), self.assertRaises(evidence.EvidenceError):
                self.discover(value)
        value = copy.deepcopy(self.discovery)
        value["tests"][0]["properties"][0]["value"] = str(self.directory.parent)
        with self.assertRaises(evidence.EvidenceError):
            self.discover(value)

    def test_direct_literal_failure_is_accepted_only_with_real_failure_exit(self):
        evidence.validate_direct_legacy(run(LEGACY_OUTPUT, 1))
        for code in (0, 2, -11, None):
            with self.subTest(code=code), self.assertRaises(evidence.EvidenceError):
                evidence.validate_direct_legacy(run(LEGACY_OUTPUT, code))

    def test_different_failure_missing_group_extra_group_duplicate_or_pass_rejected(self):
        changes = (
            LEGACY_OUTPUT.replace("as dt tends to zero", "different reason"),
            LEGACY_OUTPUT.replace("PASS Cartesian planner and positive free cuts\n", ""),
            LEGACY_OUTPUT + "FAIL unrelated regression\n",
            LEGACY_OUTPUT + "PASS Cartesian planner and positive free cuts\n",
            LEGACY_OUTPUT.replace("6/7", "7/7").replace("FAIL manufactured", "PASS manufactured"),
            LEGACY_OUTPUT + "6/7 remap-only groups passed; no composed timestep/front gate claimed\n",
        )
        for text in changes:
            with self.subTest(text=text), self.assertRaises(evidence.EvidenceError):
                evidence.validate_direct_legacy(run(text, 1))

    def test_all_sanitizer_forms_fail_even_with_correct_exit_and_group_text(self):
        for diagnostic in ("ERROR: AddressSanitizer: heap-buffer-overflow", "runtime error: signed integer overflow",
                           "AddressSanitizer:DEADLYSIGNAL", "UndefinedBehaviorSanitizer: signal",
                           "ERROR: LeakSanitizer: detected memory leaks",
                           "LeakSanitizer has encountered a fatal error", "AddressSanitizer CHECK failed",
                           "Sanitizer CHECK failed: internal allocator", "ThreadSanitizer: unexpected memory mapping"):
            with self.subTest(diagnostic=diagnostic), self.assertRaises(evidence.EvidenceError):
                evidence.validate_direct_legacy(run(LEGACY_OUTPUT + diagnostic + "\n", 1))

    def test_windows_crlf_is_parsed_without_altering_captured_bytes(self):
        raw = LEGACY_OUTPUT.replace("\n", "\r\n").encode()
        result = evidence.Run(("fixture",), ".", 1, False, raw, "windows.log")
        evidence.validate_direct_legacy(result)
        self.assertEqual(result.stdout, raw)
        text = ctest_output([evidence.LEGACY], {evidence.LEGACY}).replace("\n", "\r\n")
        evidence.validate_ctest(run(text, 8), {evidence.LEGACY}, {evidence.LEGACY})

    def test_valid_default_and_applicable_ctest_require_complete_groups(self):
        names = list(evidence.DEFAULT_TESTS)
        evidence.validate_ctest(run(ctest_output(names)), set(names))
        names = [name for name in self.expected if name != evidence.LEGACY]
        evidence.validate_ctest(run(ctest_output(names)), set(names))
        malformed = ctest_output(names).replace("17/17 retained material-state", "16/17 retained material-state")
        with self.assertRaises(evidence.EvidenceError):
            evidence.validate_ctest(run(malformed), set(names))

    def test_real_ctest_negative_and_unfiltered_failure_are_not_turned_to_pass(self):
        for names in ([evidence.LEGACY], list(self.expected)):
            text = ctest_output(names, {evidence.LEGACY})
            evidence.validate_ctest(run(text, 8), set(names), {evidence.LEGACY})
            with self.assertRaises(evidence.EvidenceError):
                evidence.validate_ctest(run(text, 0), set(names), {evidence.LEGACY})
        unexpected_pass = ctest_output([evidence.LEGACY])
        with self.assertRaises(evidence.EvidenceError):
            evidence.validate_ctest(run(unexpected_pass, 0), {evidence.LEGACY}, {evidence.LEGACY})

    def test_actual_ctest_adjacent_failed_padding_format_retains_strict_failure(self):
        # Actual MSVC CTest row has no space between dot padding and disposition.
        text = ctest_output([evidence.LEGACY], {evidence.LEGACY})
        text = text.replace("............ ***Failed", "........***Failed")
        evidence.validate_ctest(run(text, 8), {evidence.LEGACY}, {evidence.LEGACY})
        with self.assertRaises(evidence.EvidenceError):
            evidence.validate_ctest(run(text.replace("***Failed", "***Timeout"), 8), {evidence.LEGACY}, {evidence.LEGACY})

    def test_ctest_wrong_test_extra_failure_wrong_status_or_summary_rejected(self):
        names = list(self.expected)
        good = ctest_output(names, {evidence.LEGACY})
        faults = (
            good.replace("positive_staggered_rezone ............", "other_test ............"),
            ctest_output(names, {evidence.LEGACY, "retained_hybrid_geometry"}),
            good.replace("***Failed", "***Timeout"),
            good.replace("1 tests failed out of 13", "0 tests failed out of 13"),
            good.replace("6/7 remap-only", "7/7 remap-only"),
        )
        for text in faults:
            with self.subTest(text=text), self.assertRaises(evidence.EvidenceError):
                evidence.validate_ctest(run(text, 8), set(names), {evidence.LEGACY})

    def test_timeout_is_failure_even_if_partial_log_looks_complete(self):
        with self.assertRaisesRegex(evidence.EvidenceError, "Timed out"):
            evidence.validate_direct_legacy(run(LEGACY_OUTPUT, 1, timed_out=True))
        with self.assertRaisesRegex(evidence.EvidenceError, "Timed out"):
            evidence.validate_ctest(run(ctest_output(list(evidence.DEFAULT_TESTS)), timed_out=True), set(evidence.DEFAULT_TESTS))

    def test_reference_requires_every_distinct_executed_group(self):
        text = ctest_output(list(evidence.REFERENCE_TESTS))
        evidence.validate_reference(run(text))
        last = evidence.REFERENCE_GROUPS - 1
        for faulty in (text.replace(f"1: PASS synthetic reference fixture {last}\n", ""),
                       text.replace(f"fixture {last}", f"fixture {last - 1}"), text + "runtime error: divide by zero\n"):
            with self.assertRaises(evidence.EvidenceError):
                evidence.validate_reference(run(faulty))

    def test_no_tests_exceptions_and_non_utf8_cannot_satisfy_disposition(self):
        with self.assertRaises(evidence.EvidenceError):
            evidence.validate_ctest(run("No tests were found!!!\n"), set(evidence.DEFAULT_TESTS))
        text = ctest_output(list(evidence.DEFAULT_TESTS)).replace("Passed", "***Exception: SegFault", 1)
        with self.assertRaises(evidence.EvidenceError):
            evidence.validate_ctest(run(text), set(evidence.DEFAULT_TESTS))
        bad = evidence.Run(("fixture",), ".", 1, False, b"\xff", "bad.log")
        with self.assertRaises(evidence.EvidenceError):
            evidence.validate_direct_legacy(bad)

    def test_capture_uses_fresh_subprocess_bytes_and_records_actual_exit(self):
        output = self.directory / "fresh"
        output.mkdir()
        result = evidence.capture([sys.executable, "-c", f"import sys; sys.stdout.buffer.write({LEGACY_OUTPUT.encode()!r}); sys.exit(1)"],
                                  self.directory, output, "actual.log", 10)
        evidence.validate_direct_legacy(result)
        self.assertEqual((output / "actual.log").read_bytes(), result.stdout)
        metadata = json.loads((output / "actual.log.json").read_text())
        self.assertEqual(metadata["returncode"], 1)
        self.assertEqual(metadata["stdout_sha256"], evidence.sha256(output / "actual.log"))

    def test_stale_valid_failure_log_is_refused_before_launch(self):
        output = self.directory / "stale"
        output.mkdir()
        (output / "old.log").write_text(LEGACY_OUTPUT)
        marker = self.directory / "should-not-run"
        with self.assertRaises(FileExistsError):
            evidence.capture([sys.executable, "-c", f"from pathlib import Path; Path({str(marker)!r}).touch()"],
                             self.directory, output, "old.log", 10)
        self.assertFalse(marker.exists())
        with self.assertRaises(evidence.EvidenceError):
            evidence.capture([sys.executable], self.directory, output, "../old.log", 10)

    def test_existing_output_directory_cannot_relabel_an_old_run(self):
        output = self.directory / "existing"
        output.mkdir()
        with self.assertRaises(FileExistsError):
            evidence.main(["--build-dir", str(self.directory / "build"), "--reference-build-dir", str(self.directory / "reference"),
                           "--output-dir", str(output), "--mode", "experimental"])

    def test_actual_subprocess_timeout_is_recorded_and_rejected(self):
        output = self.directory / "timeout"
        output.mkdir()
        result = evidence.capture([sys.executable, "-c", "import time; print('partial', flush=True); time.sleep(5)"],
                                  self.directory, output, "timeout.log", .1)
        self.assertTrue(result.timed_out)
        self.assertIsNone(result.returncode)
        with self.assertRaisesRegex(evidence.EvidenceError, "Timed out"):
            evidence.clean_run(result, 0)
        self.assertTrue(json.loads((output / "timeout.log.json").read_text())["timed_out"])

    def test_compile_graph_keeps_default_retained_and_omits_unfinished_sources(self):
        commands = [{"file": str(self.directory / name), "command": "c++ -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror"}
                    for name in ("occupied_staggered.cpp", "occupied_staggered_tests.cpp")]
        compile_file = self.directory / "compile_commands.json"
        compile_file.write_text(json.dumps(commands))
        evidence.validate_compile_graph(self.directory, False)
        with self.assertRaises(evidence.EvidenceError):
            evidence.validate_compile_graph(self.directory, True)
        commands += [{"file": str(self.directory / name), "command": "c++ -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror"}
                     for name in evidence.NEW_SOURCES]
        compile_file.write_text(json.dumps(commands))
        evidence.validate_compile_graph(self.directory, True)
        with self.assertRaises(evidence.EvidenceError):
            evidence.validate_compile_graph(self.directory, False)
        commands[0]["command"] = "c++ -Wall"
        compile_file.write_text(json.dumps(commands))
        with self.assertRaises(evidence.EvidenceError):
            evidence.validate_compile_graph(self.directory, True)

    def test_compile_graph_rejects_experimental_source_in_core(self):
        commands = [{"file": str(self.directory / name),
                     "command": "c++ -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror"}
                    for name in sorted({"occupied_staggered.cpp", "occupied_staggered_tests.cpp"} | evidence.NEW_SOURCES)]
        injected = next(item for item in commands if Path(item["file"]).name == "occupied_staggered_hybrid_remap.cpp")
        injected["output"] = "CMakeFiles/ma_core.dir/native/src/occupied_staggered_hybrid_remap.cpp.o"
        (self.directory / "compile_commands.json").write_text(json.dumps(commands))
        with self.assertRaises(evidence.EvidenceError):
            evidence.validate_compile_graph(self.directory, True)

    def nonlinear_fixture(self):
        parent = self.directory / "hybrid-nonlinear-profiles"
        parent.mkdir()
        dataset = parent / "run-1"
        dataset.mkdir()
        metadata = {"schema": 1, "cases": 304, "nonlinear": 288, "uniform": 16,
                    "native_physical_checks": True, "independent_oracle_required": True, "whole_A2": False}
        (dataset / "COMPLETE.json").write_text(json.dumps(metadata))
        return dataset, f"13: NONLINEAR_OUTPUT {dataset}\n"

    def test_nonlinear_output_must_be_exactly_one_new_native_child(self):
        dataset, text = self.nonlinear_fixture()
        self.assertEqual(evidence.nonlinear_output(run(text), self.directory, set()), dataset)
        self.assertEqual(evidence.nonlinear_output(run(text, 8), self.directory, set(), enclosing_ctest_exit=8), dataset)
        self.assertEqual(run(text, 8).returncode, 8)
        for transcript, previous in ((text, {dataset}), (text+text, set()), ("", set()),
                                     (text.replace(str(dataset), str(self.directory)), set())):
            with self.subTest(transcript=transcript), self.assertRaises(evidence.EvidenceError):
                evidence.nonlinear_output(run(transcript), self.directory, previous)
        (dataset.parent / "unexpected-run").mkdir()
        with self.assertRaises(evidence.EvidenceError):
            evidence.nonlinear_output(run(text), self.directory, set())

    def test_nonlinear_failure_incomplete_metadata_timeout_or_sanitizer_is_fatal(self):
        dataset, text = self.nonlinear_fixture()
        for result in (run(text, 2), run(text, timed_out=True), run(text+"runtime error: failure\n")):
            with self.assertRaises(evidence.EvidenceError):
                evidence.nonlinear_output(result, self.directory, set())
        failure = dataset / "FIRST_FAILURE.json"
        failure.write_text("{}")
        with self.assertRaises(evidence.EvidenceError):
            evidence.nonlinear_output(run(text), self.directory, set())
        failure.unlink()
        (dataset / "COMPLETE.json").write_text('{"cases":304}')
        with self.assertRaises(evidence.EvidenceError):
            evidence.nonlinear_output(run(text), self.directory, set())

    def test_native_completion_never_substitutes_for_independent_oracle(self):
        output = self.directory / "oracle"
        output.mkdir()
        summary = {"oracle_passed": True, "native_cases": 304, "whole_A2": False,
                   "all_node_rate": False, "pressure_front": False}
        (output / "summary.json").write_text(json.dumps(summary))
        evidence.validate_nonlinear_oracle(run("fresh synthetic oracle"), output)
        for mutation in ({"oracle_passed": False}, {"native_cases": 303}, {"whole_A2": True},
                         {"all_node_rate": True}, {"pressure_front": True}):
            with self.subTest(mutation=mutation), self.assertRaises(evidence.EvidenceError):
                (output / "summary.json").write_text(json.dumps(summary | mutation))
                evidence.validate_nonlinear_oracle(run("native completed"), output)
        (output / "summary.json").write_text(json.dumps(summary))
        (output / "FIRST_FAILURE.json").write_text("{}")
        with self.assertRaises(evidence.EvidenceError):
            evidence.validate_nonlinear_oracle(run("native completed"), output)


if __name__ == "__main__":
    unittest.main()
