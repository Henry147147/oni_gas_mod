"""Windows checks for complete publication of manually reviewed hover receipts."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
WRITER = ROOT / 'tools/write_tile_hover_receipt.ps1'
PWSH = os.environ.get('MA_PWSH') or shutil.which('pwsh')
NAMES = ('default-all-gases', 'exact-threshold', 'below-threshold', 'ui-scale-125')
FLAGS = ('-AllNonTraceRowsVisible', '-TraceRowsOmitted', '-HazardWarningVisible', '-NoSelection')


def held(case=0, sequence=0):
    return {'Kind': 'actual-native-tile-hover-contents-fixture', 'Status': 'pending',
            'Case': case, 'Captured': True, 'Error': '', 'CallbackError': '',
            'Observation': {'Case': case, 'Cell': 300 + sequence, 'Revision': 100 + sequence,
                            'PublicationGeneration': 200 + sequence, 'ScreenWidth': 2560, 'ScreenHeight': 1440,
                            'DistinctHoverFrames': 3, 'NoSelection': True, 'ShadowBarSelected': False}}


def expected(report):
    result = {name: report['Observation'][name] for name in
              ('Case', 'Cell', 'Revision', 'PublicationGeneration', 'ScreenWidth', 'ScreenHeight')}
    result.update({name[1:]: True for name in FLAGS})
    return result


def literal(path):
    return "'" + str(path).replace("'", "''") + "'"


@unittest.skipUnless(os.name == 'nt' and PWSH, 'Requires Windows and PowerShell 7')
class HoverReceiptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / 'local/receipt-protocol-tests'
        parent.mkdir(parents=True, exist_ok=True)
        cls.evidence = Path(tempfile.mkdtemp(prefix='first-', dir=parent)).resolve()
        assert cls.evidence.is_relative_to(parent.resolve())

    def fixture(self, name, report=None):
        directory = self.evidence / name
        directory.mkdir()
        source = directory / 'held.json'
        source.write_text(json.dumps(report or held()), encoding='utf-8')
        return source, directory

    def run_writer(self, source, directory, flags=FLAGS, case=0):
        result = subprocess.run([str(PWSH), '-NoLogo', '-NoProfile', '-NonInteractive', '-File', str(WRITER),
                                 '-ReviewedReport', str(source), '-OutputDirectory', str(directory),
                                 '-ExpectedCase', str(case), *flags], capture_output=True, timeout=15,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        (directory / f'writer-{len(list(directory.glob("writer-*.stdout")))}.stdout').write_bytes(result.stdout)
        (directory / f'writer-{len(list(directory.glob("writer-*.stderr")))}.stderr').write_bytes(result.stderr)
        return result

    def assert_unpublished(self, directory):
        self.assertEqual(list(directory.glob('*.continue.json')), [])
        self.assertEqual(list(directory.glob('.hover-receipt-*.tmp')), [])

    def test_atomic_publication_matches_eager_reader(self):
        count = 64
        publications = self.evidence / 'eager-reader'
        publications.mkdir()
        expected_files = {}
        commands = ["$ErrorActionPreference = 'Stop'"]
        for sequence in range(count):
            case = sequence % 4
            report = held(case, sequence)
            source, directory = self.fixture(f'eager-reader/{sequence:03}', report)
            expected_files[directory / (NAMES[case] + '.continue.json')] = expected(report)
            commands.append(f'& {literal(WRITER)} -ReviewedReport {literal(source)} -OutputDirectory {literal(directory)} '
                            f'-ExpectedCase {case} ' + ' '.join(FLAGS) + ' | Out-Null')
        driver = publications / 'producer.ps1'
        driver.write_text('\n'.join(commands) + '\n', encoding='utf-8')
        observed = {}
        observed_live = 0
        with (publications / 'producer.stdout').open('wb') as stdout, (publications / 'producer.stderr').open('wb') as stderr:
            process = subprocess.Popen([str(PWSH), '-NoLogo', '-NoProfile', '-NonInteractive', '-File', str(driver)],
                                       stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW)
            deadline = time.monotonic() + 30
            try:
                while process.poll() is None:
                    self.assertLess(time.monotonic(), deadline, 'Receipt producer exceeded fixed control deadline')
                    # Read immediately on first appearance, as the native poller
                    # does. A locked or partial final file is a control failure.
                    for path in publications.rglob('*.continue.json'):
                        if path not in observed:
                            live_before_read = process.poll() is None
                            observed[path] = json.loads(path.read_bytes())
                            self.assertEqual(observed[path], expected_files[path])
                            observed_live += live_before_read
                self.assertEqual(process.wait(timeout=5), 0)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
        for path, value in expected_files.items():
            self.assertEqual(json.loads(path.read_bytes()), value)
        self.assertGreater(observed_live, 0, 'Reader never observed a live producer before reading a receipt')
        self.assertEqual(list(publications.rglob('.hover-receipt-*.tmp')), [])
        (publications / 'review.json').write_text(json.dumps({
            'WriterSha256': hashlib.sha256(WRITER.read_bytes()).hexdigest(),
            'PublishedReceipts': count, 'ProducerLiveImmediatelyBeforeRead': observed_live,
            'EveryReceiptCompleteAndBound': True, 'ActualGameOrPixelReview': False,
        }, indent=2) + '\n', encoding='utf-8')

    def test_requires_all_explicit_pixel_review_assertions(self):
        for missing in FLAGS:
            with self.subTest(missing=missing):
                source, directory = self.fixture('missing-' + missing[1:])
                result = self.run_writer(source, directory, tuple(flag for flag in FLAGS if flag != missing))
                self.assertNotEqual(result.returncode, 0)
                self.assert_unpublished(directory)

    def test_rejects_stale_incomplete_or_unbound_review(self):
        changes = [('case', lambda r: r.update(Case=1)),
                   ('captured', lambda r: r.update(Captured=False)),
                   ('failed', lambda r: r.update(Status='failed', Error='earlier failure')),
                   ('selection', lambda r: r['Observation'].update(NoSelection=False)),
                   ('shadow', lambda r: r['Observation'].update(ShadowBarSelected=True)),
                   ('frames', lambda r: r['Observation'].update(DistinctHoverFrames=2)),
                   ('revision', lambda r: r['Observation'].update(Revision='100')),
                   ('callback', lambda r: r.update(CallbackError='earlier hover failure'))]
        for name, change in changes:
            with self.subTest(name=name):
                report = held()
                change(report)
                source, directory = self.fixture('invalid-' + name, report)
                self.assertNotEqual(self.run_writer(source, directory).returncode, 0)
                self.assert_unpublished(directory)

    def test_refuses_overwrite_and_preserves_existing_bytes(self):
        source, directory = self.fixture('existing')
        self.assertEqual(self.run_writer(source, directory).returncode, 0)
        destination = directory / (NAMES[0] + '.continue.json')
        before = destination.read_bytes()
        source.write_text(json.dumps(held(sequence=1)), encoding='utf-8')
        self.assertNotEqual(self.run_writer(source, directory).returncode, 0)
        self.assertEqual(destination.read_bytes(), before)
        self.assertEqual(list(directory.glob('.hover-receipt-*.tmp')), [])


if __name__ == '__main__':
    unittest.main()
