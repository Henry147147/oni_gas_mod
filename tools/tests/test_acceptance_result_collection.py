"""No-game regressions for actual release collector results and dependencies."""
import contextlib, io, json, sys, tempfile, unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock
sys.path.insert(0, str(Path(__file__).parents[1]))
import acceptance_suite as suite
REVISION = 'a' * 40

class ResultCollectionTests(unittest.TestCase):

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def fixture(self, name, status):
        p = self.root / name
        p.mkdir()
        values = dict(zip(suite.SETTINGS_KEYS, suite.SETTINGS_EXPECTED))
        d = {'status': status, 'source_revision': REVISION, 'source_dirty': False, 'observed_player_exit_code': 0, 'mod_failure_lines': [], 'colony_owner': {'State': 1, 'Faulted': False, 'Error': '', 'ColonySettings': {'Values': values}}}
        (p / 'result.json').write_text(json.dumps(d))
        for name in suite.SETTINGS_SHOTS:
            (p / (name + '.png')).write_bytes(b'fixture')
        return p

    def runner(self, steps):
        layout = SimpleNamespace(source=Path.cwd(), runs=self.root / 'runs', summary=self.root / 'summary.json', tag='dry')
        infra = SimpleNamespace(gpu_display=':99', proton=self.root / 'proton', linux_game=self.root / 'linux', windows_game=self.root / 'windows')
        return suite.Suite(REVISION, 'release', ('windows',), layout, infra, steps, keep_going=True)

    def test_settings_results_are_collected_and_passing_source_unlocks_load(self):
        source = suite.Step('windows-settings-source', 'windows', 'proton-settings', (), {'stage': 'source'})
        load = suite.Step('windows-settings-load', 'windows', 'proton-settings', (source.name,), {'stage': 'load'})
        outputs = {source.name: self.fixture('source', 'passed-colony-owner'), load.name: self.fixture('load', 'passed-colony-owner-load')}
        run = self.runner([source, load])
        with mock.patch.object(run, 'execute', side_effect=lambda step: suite.proton_checks(step, outputs[step.name], REVISION, {})) as execute, contextlib.redirect_stdout(io.StringIO()):
            code = run.run()
        self.assertEqual(code, 0)
        self.assertEqual(execute.call_count, 2)
        for name, label in [(source.name, 'passed-colony-owner'), (load.name, 'passed-colony-owner-load')]:
            self.assertEqual(run.records[name]['status'], 'passed')
            self.assertEqual(run.records[name]['runner_status'], label)
            self.assertTrue(all(run.records[name]['checks'].values()))

    def test_real_batch_failure_keeps_its_actual_failed_checks(self):
        step = suite.Step('windows-b2', 'windows', 'proton-batch')
        out = self.fixture('batch', 'failed-runtime-validation')
        run = self.runner([step])
        with mock.patch.object(run, 'execute', side_effect=lambda selected: suite.proton_checks(selected, out, REVISION, {})), contextlib.redirect_stdout(io.StringIO()):
            code = run.run()
        self.assertEqual(code, 2)
        row = run.records[step.name]
        self.assertEqual(row['status'], 'failed')
        self.assertFalse(row['checks']['status'])
        self.assertNotIn('completed_without_error', row['checks'])
        self.assertEqual(row['runner_status'], 'failed-runtime-validation')

    def test_ordinary_ui_result_metadata_cannot_shadow_collector_verdict(self):
        step = suite.Step('windows-spacedout-ui', 'windows', 'proton-spacedout-ui')
        out = self.fixture('ui', 'passed-scoped-main-menu')
        checks, details = suite.proton_checks(step, out, REVISION, {})
        run = self.runner([step])
        with mock.patch.object(run, 'execute', return_value=(checks, details)), contextlib.redirect_stdout(io.StringIO()):
            code = run.run()
        self.assertEqual(code, 2)
        row = run.records[step.name]
        self.assertEqual(row['status'], 'failed')
        self.assertIn('ordinary_spaced_out_save', row['checks'])
        self.assertNotIn('completed_without_error', row['checks'])
        self.assertEqual(row['runner_status'], 'passed-scoped-main-menu')
if __name__ == '__main__':
    unittest.main()
