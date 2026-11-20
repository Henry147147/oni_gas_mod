"""Controls for the game-free colony speed check's own logic; no timing claims."""
import contextlib
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import zlib

TOOLS = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('colony_speed', TOOLS / 'colony_speed.py')
speed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(speed)


def owner_image(payload: bytes = b'state' * 40) -> bytes:
    body = b'MACOLN01' + payload
    return body + hashlib.sha256(body).hexdigest().encode()


def result(owner=50.0, p95=60.0, speed_mean=2.6, speed_p95=2.3, faulted=False):
    return {'faulted': faulted, 'owner_ms': {'mean': owner, 'p95': p95}, 'stage_ms': {'flow': {'mean': owner - 10}},
            'flow': {'summed_region_ms': {'slowest_region': {'mean': 20.0}, 'all_regions': {'mean': 44.0}},
                     'per_frame': {'flow_intervals': 220.0, 'pressure_iterations': 2800.0},
                     'top_regions': [{'intervals_per_frame': 2.0}]},
            'estimate': {'game_per_real_mean': speed_mean, 'game_per_real_p95_frame': speed_p95, 'owner_ms_budget_for_3x': 41.7}}


class SavedOwnerTests(unittest.TestCase):
    def test_a_bare_image_is_returned(self):
        image = owner_image()
        self.assertEqual(speed.saved_owner(image), image)

    def test_the_image_is_found_inside_a_save(self):
        image = owner_image()
        # A header of the stated length, an unrelated stream, then the block that holds the owner.
        header = b'x' * 24
        other = zlib.compress(b'another component' * 10)
        block = zlib.compress(b'prefix' + struct.pack('<I', len(image)) + image + b'suffix')
        save = b'KSAV' + struct.pack('<I', len(header)) + b'\0' * 8 + header + other + block
        self.assertEqual(speed.saved_owner(save), image)

    def test_a_changed_image_is_refused(self):
        image = bytearray(owner_image())
        image[20] ^= 1
        with self.assertRaises(ValueError):
            speed.saved_owner(bytes(image))

    def test_a_save_without_an_owner_is_refused(self):
        save = b'KSAV' + struct.pack('<I', 4) + b'\0' * 12 + zlib.compress(b'no owner here' * 20)
        with self.assertRaises(ValueError):
            speed.saved_owner(save)


class JournalTests(unittest.TestCase):
    def test_the_cut_keeps_whole_frames_and_their_inputs(self):
        lines = [b'session 1 restored_frames 3\n', b'energy 2\n']
        for frame in range(4, 9):
            lines += [f'frame {frame} flush 0 seconds 0x1p-3 ticket {frame}\n'.encode(), b'T 1 2 3\n', b'H 4 5 6\n']
        with tempfile.TemporaryDirectory() as temporary:
            source, target = Path(temporary) / 'in.txt', Path(temporary) / 'out.txt'
            source.write_bytes(b''.join(lines))
            self.assertEqual(speed.cut_journal(source, target, 2), 2)
            self.assertEqual(target.read_bytes(), b''.join(lines[:2 + 2 * 3]))
            self.assertEqual(speed.cut_journal(source, target, 50), 5)
            self.assertEqual(target.read_bytes(), b''.join(lines))

    def test_a_flush_does_not_count_as_a_frame(self):
        lines = [b'frame 3 flush 1 seconds 0x0p+0\n', b'T 1\n', b'frame 4 flush 0 seconds 0x1p-3\n', b'T 2\n', b'frame 5 flush 0 seconds 0x1p-3\n']
        with tempfile.TemporaryDirectory() as temporary:
            source, target = Path(temporary) / 'in.txt', Path(temporary) / 'out.txt'
            source.write_bytes(b''.join(lines))
            self.assertEqual(speed.cut_journal(source, target, 1), 1)
            self.assertEqual(target.read_bytes(), b''.join(lines[:4]))

    def test_zero_frames_are_not_a_capture(self):
        with tempfile.TemporaryDirectory() as temporary:
            source, target = Path(temporary) / 'in.txt', Path(temporary) / 'out.txt'
            source.write_bytes(b'frame 1 flush 0 seconds 0x1p-3\n')
            with self.assertRaises(ValueError):
                speed.cut_journal(source, target, 0)


class CaptureTests(unittest.TestCase):
    def inputs(self, folder: Path, journal: bytes = b'session 1 restored_frames 0\nframe 1 flush 0 seconds 0x1p-3\n'):
        save = folder / 'game.sav'
        save.write_bytes(owner_image())
        home = folder / 'home'
        home.mkdir()
        (home / 'inputs-1.txt').write_bytes(journal)
        (home / 'diseases-1.bin').write_bytes(b'\0' * 12)
        return save, home

    def test_capture_stays_under_private_root_and_is_complete(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            save, home = self.inputs(root)
            with patch.object(speed, 'CAPTURES', root / 'private'):
                for name in ('../leak', 'nested/leak', '.hidden', 'bad name'):
                    with self.subTest(name=name), self.assertRaises(ValueError):
                        speed.capture(name, save, home, 1)
                self.assertFalse((root / 'leak').exists())
                folder = speed.capture('colony_1', save, home, 1)
                self.assertEqual(set(p.name for p in folder.iterdir()), {'owner', 'inputs.txt', 'diseases.bin', 'capture.json'})
                self.assertEqual(json.loads((folder / 'capture.json').read_text())['frames'], 1)
                self.assertEqual(list((root / 'private').glob('.capture-*')), [])

    def test_invalid_inputs_leave_no_visible_or_temporary_capture(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            save, home = self.inputs(root, journal=b'session 1 restored_frames 0\n')
            with patch.object(speed, 'CAPTURES', root / 'private'):
                with self.assertRaisesRegex(ValueError, 'no advanced frames'):
                    speed.capture('empty', save, home, 1)
                self.assertEqual(list((root / 'private').iterdir()), [])
                save.write_bytes(b'not a save')
                with self.assertRaises(ValueError):
                    speed.capture('invalid', save, home, 1)
                self.assertEqual(list((root / 'private').iterdir()), [])


class ReportTests(unittest.TestCase):
    def test_regression_rejects_a_short_shifted_or_divergent_healthy_replay(self):
        expected = dict(mode='owner-replay', first_frame=700, last_frame=719, frames=20,
                        warmup=5, final_owner_fnv='0123456789abcdef')
        valid = dict(expected, workers=12, faulted=False, relative_mass_error=0., relative_energy_error=0.)
        speed.verify_regression(valid, expected, 12)
        for field, value in [('frames', 19), ('first_frame', 701), ('last_frame', 718),
                             ('final_owner_fnv', 'fedcba9876543210'), ('relative_mass_error', math.nan),
                             ('relative_energy_error', 1.1e-6), ('faulted', True), ('workers', 1)]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                speed.verify_regression({**valid, field: value}, expected, 12)

    def test_swapped_journal_fails_input_identity_before_replay(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            expected = {}
            for name in ('owner', 'inputs.txt', 'diseases.bin'):
                (folder / name).write_bytes(name.encode())
                expected[name] = hashlib.sha256(name.encode()).hexdigest()
            speed.verify_capture(folder, expected)
            (folder / 'inputs.txt').write_bytes(b'other journal from the same save')
            with self.assertRaisesRegex(ValueError, 'inputs.txt differs'):
                speed.verify_capture(folder, expected)

    def test_p95_slowdown_alone_can_fail_an_explicit_performance_budget(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            exe = root / 'benchmark'
            exe.touch()
            expected = dict(mode='steady', frames=20, warmup=5, final_views_fnv='abc')
            reference = root / 'reference.json'
            affinity = sorted(speed.os.sched_getaffinity(0)) if hasattr(speed.os, 'sched_getaffinity') else None
            reference.write_text(json.dumps(dict(revision='parent', host={}, cpu_affinity=affinity, workers=12,
                cases={'late-base': dict(expected, owner_ms={'mean': 50, 'p95': 50})})))
            measured = dict(result(speed_mean=2.667, speed_p95=2.353), **expected, workers=12,
                            frame_seconds=.2, relative_mass_error=0, relative_energy_error=0)
            measured['estimate']['native_overhead_ms'] = 25

            def run(command, **_):
                Path(command[command.index('--json') + 1]).write_text(json.dumps(measured))
                return SimpleNamespace(returncode=0, stderr='')

            output = root / 'report'
            options = ['colony_speed.py', '--executable', str(exe), '--output', str(output),
                       '--regression', '--max-slowdown-percent', '10']
            with patch.object(speed, 'REGRESSION_REFERENCE', reference), patch.object(sys, 'argv', options), \
                 patch.object(speed, 'host', return_value={}), patch.object(speed, 'captures', return_value={}), \
                 patch.object(speed.subprocess, 'run', side_effect=run), \
                 patch.object(speed.subprocess, 'check_output', side_effect=['candidate', '']), \
                 contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(speed.main(), 1)
            record = json.loads((output / 'summary.json').read_text())['cases']['late-base']
            self.assertAlmostEqual(record['owner_change_percent']['p95'], 20)
            self.assertIn('slowdown', record['result_error'])

    def test_rows_carry_the_figures(self):
        r = speed.row('late-base', result())
        self.assertEqual((r['owner_ms'], r['owner_p95_ms'], r['flow_ms'], r['slowest_region_ms'], r['worst_region_intervals'], r['speed']),
                         (50.0, 60.0, 40.0, 20.0, 2.0, 2.6))
        self.assertIn('late-base', speed.table([r]))

    def test_the_requirement_names_slow_and_faulted_cases(self):
        rows = [speed.row('fast', result(speed_mean=3.2)), speed.row('slow', result(speed_mean=2.9)),
                speed.row('broken', result(speed_mean=3.5, faulted=True))]
        self.assertEqual(speed.below(rows, 3.0), ['slow', 'broken'])
        self.assertEqual(speed.below(rows, 2.0), ['broken'])

    def test_nonfinite_measurement_cannot_pass_the_speed_gate(self):
        invalid = result(speed_mean=math.nan)
        with self.assertRaisesRegex(ValueError, 'invalid speed'):
            speed.row('invalid', invalid)
        self.assertEqual(speed.below([{'case': 'invalid', 'faulted': False, 'speed': math.nan}], 3.0), ['invalid'])

    def test_reported_speed_must_agree_with_measured_owner_frames(self):
        measured = result(owner=50.0, p95=60.0, speed_mean=2.667, speed_p95=2.353)
        measured['frame_seconds'] = .2
        measured['estimate']['native_overhead_ms'] = 25.0
        speed.validate_estimate('replay-colony', measured)
        measured['estimate']['game_per_real_mean'] = 3.1
        with self.assertRaisesRegex(ValueError, 'inconsistent game_per_real_mean'):
            speed.validate_estimate('replay-colony', measured)
        measured['estimate']['game_per_real_mean'] = 2.667
        measured['estimate']['game_per_real_p95_frame'] = 3.1
        with self.assertRaisesRegex(ValueError, 'inconsistent game_per_real_p95_frame'):
            speed.validate_estimate('replay-colony', measured)

    def test_the_comparison_skips_cases_the_baseline_lacks(self):
        rows = [speed.row('a', result(owner=40.0)), speed.row('b', result())]
        text = speed.compare(rows, {'a': speed.row('a', result(owner=50.0))})
        self.assertIn('-20.0 %', text)
        self.assertNotIn('\nb ', text)

    def test_generated_cases_take_their_preset_and_overrides(self):
        self.assertEqual(speed.arguments_for('late-base', {}, None, None, 0), ['--preset', 'late-base'])
        self.assertEqual(speed.arguments_for('late-base', {}, 40, 10, 12), ['--preset', 'late-base', '--frames', '40', '--warmup', '10', '--workers', '12'])
        self.assertEqual(speed.arguments_for('late-largest-sealed', {}, None, None, 0), ['--preset', 'late-largest', '--no-vented-hall'])
        replay = speed.arguments_for('replay-x', {'replay-x': Path('/captures/x')}, None, None, 0)
        self.assertEqual(replay[:4], ['--owner-replay', '/captures/x/owner', '/captures/x/inputs.txt', '/captures/x/diseases.bin'])

    def test_failed_or_incomplete_replay_does_not_reuse_a_previous_result(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            executable = folder / 'benchmark'
            executable.touch()
            output = folder / 'results'
            output.mkdir()
            (output / 'late-base.json').write_text(json.dumps(result(speed_mean=4.0)))
            options = ['colony_speed.py', '--executable', str(executable), '--case', 'late-base', '--output', str(output), '--require', '3']
            for exit_code in (1, 0):
                (output / 'late-base.json').write_text(json.dumps(result(speed_mean=4.0)))
                with self.subTest(exit_code=exit_code), patch.object(sys, 'argv', options), \
                     patch.object(speed, 'captures', return_value={}), \
                     patch.object(speed.subprocess, 'run', return_value=SimpleNamespace(returncode=exit_code, stderr='replay failed')), \
                     patch.object(speed.subprocess, 'check_output', side_effect=['revision\n', '']), \
                     patch.object(speed, 'host', return_value={}), patch.object(speed, 'load', return_value=None), \
                     contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(speed.main(), 1)
                self.assertFalse((output / 'late-base.json').exists())
                summary = json.loads((output / 'summary.json').read_text())
                self.assertNotIn('row', summary['cases']['late-base'])
                self.assertIn('result_error', summary['cases']['late-base'])

    def test_impossible_three_x_claim_fails_the_cli_gate(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            executable = folder / 'benchmark'
            executable.touch()
            output = folder / 'results'
            measured = result(owner=200.0, p95=220.0, speed_mean=3.5, speed_p95=3.2)
            measured['frame_seconds'] = .2
            measured['estimate']['native_overhead_ms'] = 25.0

            def forge(command, **_):
                Path(command[command.index('--json') + 1]).write_text(json.dumps(measured))
                return SimpleNamespace(returncode=0, stderr='')

            options = ['colony_speed.py', '--executable', str(executable), '--case', 'late-base',
                       '--output', str(output), '--require', '3']
            with patch.object(sys, 'argv', options), patch.object(speed, 'captures', return_value={}), \
                 patch.object(speed.subprocess, 'run', side_effect=forge), \
                 patch.object(speed.subprocess, 'check_output', side_effect=['revision\n', '']), \
                 patch.object(speed, 'host', return_value={}), patch.object(speed, 'load', return_value=None), \
                 contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(speed.main(), 1)
            record = json.loads((output / 'summary.json').read_text())['cases']['late-base']
            self.assertNotIn('row', record)
            self.assertIn('inconsistent game_per_real_mean', record['result_error'])


if __name__ == '__main__':
    unittest.main()
