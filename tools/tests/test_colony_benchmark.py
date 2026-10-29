"""Durable dispatch and actual-smoke provenance controls; no timing claims."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch


TOOLS = Path(os.environ.get('MA_TEST_BENCHMARK_TOOL_ROOT', Path(__file__).resolve().parents[1]))
def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
runner = load('colony_benchmark_runner', Path(os.environ.get('MA_TEST_BENCHMARK_RUNNER', TOOLS / 'run_colony_benchmark.py')))
checker = load('colony_benchmark_checker', TOOLS / 'check_colony_benchmark.py')


class RunnerDispatchTests(unittest.TestCase):
    def invoke(self, options):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            executable = folder / 'fake-benchmark'
            executable.touch()
            dump = folder / 'world.dump'
            dump.touch()
            output = folder / 'results'
            calls = []
            def process(command, **kwargs):
                calls.append(command)
                Path(command[command.index('--json') + 1]).write_text(json.dumps({'argument_recording_fake': True}))
                return SimpleNamespace(returncode=0, stderr='')
            options = [str(dump) if v == '<dump>' else v for v in options]
            with patch.object(runner.sys, 'argv', ['runner', '--executable', str(executable), '--output', str(output), *options]), \
                 patch.object(runner.subprocess, 'check_output', side_effect=['frozen-revision\n', '']), \
                 patch.object(runner.subprocess, 'run', side_effect=process), \
                 patch.object(runner, 'host', return_value={'scope': 'fake-process control'}), \
                 patch.object(runner, 'load', return_value=None), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                code = runner.main()
            return code, calls, json.loads((output / 'summary.json').read_text())

    def test_release_cases_keep_inputs_and_request_current_fully_active_owner(self):
        code, calls, summary = self.invoke(['--suite', 'release'])
        self.assertEqual(code, 0)
        self.assertEqual(summary.get('configured_profile'), 9)
        self.assertEqual(summary.get('activity'), 'always')
        self.assertEqual(len(calls), len(runner.SUITES['release']))
        for command, (arguments, _) in zip(calls, runner.SUITES['release'].values()):
            self.assertEqual(command[1:1 + len(arguments)], arguments)
            self.assertIn('--profile', command)
            self.assertEqual(command[command.index('--profile') + 1], '9')
            self.assertIn('--always-simulate', command)

    def test_smoke_defaults_to_current_tiers(self):
        code, calls, summary = self.invoke(['--suite', 'smoke'])
        self.assertEqual(code, 0)
        self.assertEqual(summary.get('activity'), 'tiers')
        self.assertIn('--profile', calls[0])
        self.assertEqual(calls[0][calls[0].index('--profile') + 1], '9')
        self.assertNotIn('--always-simulate', calls[0])

    def test_explicit_legacy_comparison_does_not_send_new_policy_switch(self):
        code, calls, summary = self.invoke(['--suite', 'smoke', '--profile', '8'])
        self.assertEqual(code, 0)
        self.assertEqual(summary['configured_profile'], 8)
        self.assertEqual(summary['activity'], 'always')
        self.assertEqual(calls[0][calls[0].index('--profile') + 1], '8')
        self.assertNotIn('--always-simulate', calls[0])

    def test_tiered_release_is_explicit_and_legacy_cannot_claim_tiers(self):
        _, calls, summary = self.invoke(['--suite', 'release', '--case', '100k', '--activity', 'tiers'])
        self.assertEqual(summary['activity'], 'tiers')
        self.assertNotIn('--always-simulate', calls[0])
        with self.assertRaises(SystemExit) as error:
            self.invoke(['--profile', '8', '--activity', 'tiers'])
        self.assertEqual(error.exception.code, 2)

    def test_checkpoint_replay_keeps_its_saved_profile(self):
        _, calls, summary = self.invoke(['--suite', 'smoke', '--world-dump', '<dump>'])
        self.assertEqual(len(calls), 2)
        replay = next(command for command in calls if '--world-dump' in command)
        self.assertNotIn('--profile', replay)
        self.assertNotIn('--always-simulate', replay)
        self.assertEqual(len(summary['cases']), 2)


class SmokeProvenanceTests(unittest.TestCase):
    def valid(self):
        return {'profile': 9, 'owner_flags': 29, 'activity_tiers_enabled': True, 'fully_active_stress': False,
                'frames': 3, 'census_committed': 3, 'native_substeps': 6, 'activity_observation': {'actor_cells': 12}, 'faulted': False}

    def test_current_smoke_is_admitted(self):
        checker.verify(self.valid())

    def test_wrong_profile_policy_custody_or_no_evolution_is_refused(self):
        for field, wrong in [('profile', 8), ('owner_flags', 13), ('activity_tiers_enabled', False),
                             ('fully_active_stress', True), ('frames', 2), ('census_committed', 2),
                             ('native_substeps', 0), ('native_substeps', True), ('faulted', True),
                             ('activity_observation', {'actor_cells': 0}), ('activity_observation', {})]:
            with self.subTest(field=field, wrong=wrong):
                value = self.valid(); value[field] = wrong
                with self.assertRaises(ValueError):
                    checker.verify(value)
        with self.assertRaises(ValueError):
            checker.verify({})

    def test_actual_command_keeps_emulator_and_fixture_arguments(self):
        value = self.valid()
        result = SimpleNamespace(returncode=0, stdout=json.dumps(value), stderr='')
        with patch.object(checker.subprocess, 'run', return_value=result) as process, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(checker.main(['--', 'wine-emulator', 'benchmark.exe', '--preset', 'smoke']), 0)
        self.assertEqual(process.call_args.args[0], ['wine-emulator', 'benchmark.exe', '--preset', 'smoke'])
        result.returncode = 7
        with patch.object(checker.subprocess, 'run', return_value=result), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(checker.main(['--', 'benchmark']), 7)


if __name__ == '__main__':
    unittest.main()
