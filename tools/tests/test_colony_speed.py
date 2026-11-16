"""Controls for the game-free colony speed check's own logic; no timing claims."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
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


class ReportTests(unittest.TestCase):
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


if __name__ == '__main__':
    unittest.main()
