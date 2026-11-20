#!/usr/bin/env python3
"""Game-free speed check of large colonies (docs/performance-acceptance.md).

Runs `ma_colony_benchmark` on generated late-game colonies and on private
captures of real colonies, and reports the owner's frame time, what the flow
stage's regions cost and an estimate of the game speed that frame time allows.
No game process is involved.

  tools/run_locked.sh high python3 tools/colony_speed.py --build build-production

Cases:
  late-base, late-largest, late-spacedout   generated colonies (reproducible
                                            from the source); each has a hall
                                            whose thin air leaks to space
  late-*-sealed                             the same colonies without that hall
  replay-<name>                             every folder local/perf/<name>/
                                            holding `owner`, `inputs.txt` and
                                            `diseases.bin` (private captures)

`colony_speed.py capture NAME SAVE RUN_HOME` makes such a folder from a save
and the home folder of one harness run of it made with
MA_COLONY_INPUT_JOURNAL_DIR=/home/henry (see docs/colony-owner.md): the
owner image is taken from the save itself, the journal is cut to --frames.
Captures hold game state and stay private (local/ is ignored).

The estimate assumes a tick takes the owner frame plus the part of ONI's
native frame that precedes each hand-off (about 25 ms on the Linux harness);
it is calibrated on harness runs and is not a measurement of the game.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
PRESETS = ('late-base', 'late-largest', 'late-spacedout')
SYNTHETIC = (*PRESETS, *(f'{preset}-sealed' for preset in PRESETS))
CAPTURES = ROOT / 'local' / 'perf'
REGRESSION_REFERENCE = ROOT / 'tools' / 'colony_regression_reference.json'


def saved_owner(save: bytes) -> bytes:
    """The colony owner's image (MACOLN01 ... SHA-256) inside a save file."""
    if save.startswith(b'MACOLN01'):
        image = save
    else:
        if len(save) < 8:
            raise ValueError('Not a save file')
        image = None
        for at in range(16 + struct.unpack_from('<I', save, 4)[0], len(save) - 2):
            # A zlib stream starts with 0x78 and a header that is a multiple of 31.
            if save[at] != 0x78 or (save[at] * 256 + save[at + 1]) % 31:
                continue
            try:
                stream = zlib.decompressobj()
                body = stream.decompress(save[at:], 256 << 20)
                if not stream.eof or stream.unused_data:
                    continue
            except zlib.error:
                continue
            start = body.find(b'MACOLN01')
            if start < 4:
                continue
            size = struct.unpack_from('<I', body, start - 4)[0]
            image = body[start:start + size]
            break
        if image is None:
            raise ValueError('The save holds no colony owner')
    if len(image) < 80 or hashlib.sha256(image[:-64]).hexdigest().encode() != image[-64:]:
        raise ValueError('The colony owner image does not match its digest')
    return image


def cut_journal(source: Path, target: Path, frames: int) -> int:
    """Copy a journal up to and including its first `frames` advanced frames."""
    if frames < 1:
        raise ValueError('Capture needs at least one frame')
    kept = 0
    with source.open('rb') as journal, target.open('wb') as out:
        for line in journal:
            if line.startswith(b'frame ') and b' flush 0 ' in line:
                if kept == frames:
                    break
                kept += 1
            out.write(line)
    return kept


def capture(name: str, save: Path, home: Path, frames: int) -> Path:
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]*', name):
        raise ValueError('Capture name must contain only letters, numbers, _ or - and start with a letter or number')
    if frames < 1:
        raise ValueError('Capture needs at least one frame')
    folder = CAPTURES / name
    journals = sorted(home.glob('inputs-*.txt'))
    diseases = sorted(home.glob('diseases-*.bin'))
    if len(journals) != 1 or len(diseases) != 1:
        raise SystemExit(f'{home} must hold one inputs-*.txt and one diseases-*.bin')
    save_bytes = save.read_bytes()
    owner = saved_owner(save_bytes)
    disease_bytes = diseases[0].read_bytes()
    if len(disease_bytes) < 12:
        raise ValueError('Disease table is too short')
    CAPTURES.mkdir(parents=True, exist_ok=True)
    if folder.exists():
        raise FileExistsError(folder)
    temporary = Path(tempfile.mkdtemp(prefix='.capture-', dir=CAPTURES))
    try:
        (temporary / 'owner').write_bytes(owner)
        kept = cut_journal(journals[0], temporary / 'inputs.txt', frames)
        if kept == 0:
            raise ValueError('The input journal has no advanced frames')
        (temporary / 'diseases.bin').write_bytes(disease_bytes)
        (temporary / 'capture.json').write_text(json.dumps({
            'save': save.name, 'save_sha256': hashlib.sha256(save_bytes).hexdigest(), 'frames': kept,
            'note': 'private capture: owner image of the save and the inputs the game journaled after loading it'}, indent=2) + '\n')
        temporary.rename(folder)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return folder


def captures() -> dict[str, Path]:
    found = {}
    if CAPTURES.is_dir():
        for folder in sorted(CAPTURES.iterdir()):
            if folder.name.startswith('.'):
                continue
            if all((folder / part).is_file() for part in ('owner', 'inputs.txt', 'diseases.bin')):
                found[f'replay-{folder.name}'] = folder
    return found


def arguments_for(case: str, folders: dict[str, Path], frames: int | None, warmup: int | None, workers: int) -> list[str]:
    if case in SYNTHETIC:
        arguments = ['--preset', case.removesuffix('-sealed')]
        if case.endswith('-sealed'):
            arguments.append('--no-vented-hall')
    else:
        folder = folders[case]
        arguments = ['--owner-replay', str(folder / 'owner'), str(folder / 'inputs.txt'), str(folder / 'diseases.bin'),
                     '--frames', '300', '--warmup', '20']
    if frames is not None:
        arguments += ['--frames', str(frames)]
    if warmup is not None:
        arguments += ['--warmup', str(warmup)]
    if workers:
        arguments += ['--workers', str(workers)]
    return arguments


def row(case: str, result: dict) -> dict:
    """The figures the table and the comparison use."""
    flow = result.get('flow', {})
    owner = result['owner_ms']
    estimate = result.get('estimate', {})
    regions = flow.get('top_regions') or [{}]
    values = {
        'case': case, 'faulted': bool(result.get('faulted')),
        'owner_ms': owner['mean'], 'owner_p95_ms': owner['p95'],
        'flow_ms': result['stage_ms']['flow']['mean'],
        'slowest_region_ms': flow.get('summed_region_ms', {}).get('slowest_region', {}).get('mean', 0.0),
        'all_regions_ms': flow.get('summed_region_ms', {}).get('all_regions', {}).get('mean', 0.0),
        'worst_region_intervals': regions[0].get('intervals_per_frame', 0.0),
        'flow_intervals': flow.get('per_frame', {}).get('flow_intervals', 0.0),
        'pressure_iterations': flow.get('per_frame', {}).get('pressure_iterations', 0.0),
        'speed': estimate.get('game_per_real_mean', 0.0), 'speed_p95_frame': estimate.get('game_per_real_p95_frame', 0.0),
        'budget_ms': estimate.get('owner_ms_budget_for_3x', 0.0),
    }
    for key in ('owner_ms', 'owner_p95_ms', 'speed', 'speed_p95_frame'):
        value = values[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
            raise ValueError(f'{case}: invalid {key}')
    if not values['faulted'] and (values['owner_ms'] == 0 or values['speed'] == 0):
        raise ValueError(f'{case}: a successful case needs measured owner time and speed')
    return values


def validate_estimate(case: str, result: dict) -> None:
    """Reject a benchmark speed that disagrees with its measured frame time."""
    owner = result['owner_ms']
    estimate = result['estimate']
    frame_seconds = result['frame_seconds']
    overhead = estimate['native_overhead_ms']
    if any(isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value)
           for value in (frame_seconds, overhead, owner['mean'], owner['p95'])) or frame_seconds <= 0 or overhead < 0:
        raise ValueError(f'{case}: invalid speed estimate inputs')
    frame_ms = frame_seconds * 1000
    for name, owner_ms in (('game_per_real_mean', owner['mean']),
                           ('game_per_real_p95_frame', owner['p95'])):
        reported = estimate[name]
        if (isinstance(reported, bool) or not isinstance(reported, (int, float)) or not math.isfinite(reported)
                or owner_ms + overhead <= 0 or abs(reported - frame_ms / (owner_ms + overhead)) > .01):
            raise ValueError(f'{case}: inconsistent {name} speed estimate')


def table(rows: list[dict]) -> str:
    lines = ['case                 owner ms  (p95)   flow  slowest region (intervals)  all regions  est. speed (p95 frame)',
             '-' * 104]
    for r in rows:
        lines.append(f"{r['case']:<20} {r['owner_ms']:>8.1f} {r['owner_p95_ms']:>6.1f} {r['flow_ms']:>6.1f} "
                     f"{r['slowest_region_ms']:>15.1f} ({r['worst_region_intervals']:>5.1f}) {r['all_regions_ms']:>12.1f} "
                     f"{r['speed']:>10.2f}x ({r['speed_p95_frame']:.2f}x)" + ('  FAULTED' if r['faulted'] else ''))
    return '\n'.join(lines)


def below(rows: list[dict], required: float) -> list[str]:
    """Cases that faulted or whose estimated speed is under `required`."""
    return [r['case'] for r in rows if r['faulted'] or not math.isfinite(r['speed']) or r['speed'] < required]


def compare(rows: list[dict], baseline: dict) -> str:
    lines = ['case                 owner ms: before -> now      flow intervals: before -> now']
    for r in rows:
        old = baseline.get(r['case'])
        if not old:
            continue
        change = 100 * (r['owner_ms'] / old['owner_ms'] - 1) if old['owner_ms'] else 0.0
        lines.append(f"{r['case']:<20} {old['owner_ms']:>8.1f} -> {r['owner_ms']:<8.1f} ({change:+.1f} %)   "
                     f"{old['flow_intervals']:>8.1f} -> {r['flow_intervals']:<8.1f}")
    return '\n'.join(lines)


def verify_capture(folder: Path, expected: dict) -> None:
    for name in ('owner', 'inputs.txt', 'diseases.bin'):
        digest = hashlib.sha256()
        with (folder / name).open('rb') as stream:
            for block in iter(lambda: stream.read(1 << 20), b''):
                digest.update(block)
        if digest.hexdigest() != expected[name]:
            raise ValueError(f'{folder.name}/{name} differs from the reference input')


def verify_regression(result: dict, expected: dict, workers: int) -> None:
    keys = ['mode', 'frames', 'warmup']
    if expected['mode'] == 'owner-replay':
        keys.extend(('first_frame', 'last_frame', 'final_owner_fnv'))
    else:
        keys.append('final_views_fnv')
    for key in keys:
        if result.get(key) != expected[key]:
            raise ValueError(f'{key}: expected {expected[key]}, got {result.get(key)}')
    if result.get('workers') != workers or result.get('faulted') is not False:
        raise ValueError('Incorrect worker count or faulted owner')
    for key, bound in [('relative_mass_error', 1e-8), ('relative_energy_error', 1e-6)]:
        value = result.get(key)
        if type(value) not in (int, float) or not 0 <= value <= bound:
            raise ValueError(f'{key}: {value} outside [0, {bound}]')


def host() -> dict:
    model = platform.processor()
    cpuinfo = Path('/proc/cpuinfo')
    if cpuinfo.is_file():
        model = next((line.split(':', 1)[1].strip() for line in cpuinfo.read_text().splitlines()
                      if line.startswith('model name')), model)
    return {'cpu': model, 'logical_cpus': os.cpu_count(), 'system': platform.platform()}


def load() -> list[float] | None:
    try:
        return [float(v) for v in Path('/proc/loadavg').read_text().split()[:3]]
    except OSError:
        return None


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == 'capture':
        parser = argparse.ArgumentParser(prog='colony_speed.py capture', description='Make a private replay capture from a save and a journaled run of it')
        parser.add_argument('name')
        parser.add_argument('save', type=Path)
        parser.add_argument('home', type=Path, help="The run's home folder (holds inputs-*.txt and diseases-*.bin)")
        parser.add_argument('--frames', type=int, default=600, help='Journaled frames to keep')
        args = parser.parse_args(sys.argv[2:])
        print(capture(args.name, args.save, args.home, args.frames))
        return 0
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', type=Path, default=ROOT / 'build-production', help='Build folder holding ma_colony_benchmark')
    parser.add_argument('--executable', type=Path, help='A specific ma_colony_benchmark (overrides --build)')
    parser.add_argument('--case', action='append', help='Run only these cases (default: all generated colonies and every capture)')
    parser.add_argument('--frames', type=int, help='Frames per case (default: 300)')
    parser.add_argument('--warmup', type=int, help='Frames left out of the figures (default: 50 generated, 20 replayed)')
    parser.add_argument('--workers', type=int, default=0, help='0: physical cores (the game default)')
    parser.add_argument('--output', type=Path, help='Result folder (default artifacts/performance/speed-<revision>-<time>)')
    parser.add_argument('--baseline', type=Path, help='summary.json of an earlier run to compare with')
    parser.add_argument('--require', type=float, help='Fail unless every case is estimated at this many game seconds per second')
    parser.add_argument('--regression', action='store_true',
                        help='Verify the fixed generated and private replay windows against the checked-in reference')
    parser.add_argument('--max-slowdown-percent', type=float,
                        help='With --regression on the reference host, fail on a larger mean or p95 owner slowdown')
    args = parser.parse_args()
    reference = json.loads(REGRESSION_REFERENCE.read_text()) if args.regression else None
    if reference and (args.frames is not None or args.warmup is not None or args.workers):
        parser.error('--regression sets the frames, warmup and worker count from the reference')
    if args.max_slowdown_percent is not None and (not reference or not math.isfinite(args.max_slowdown_percent)
                                                  or args.max_slowdown_percent < 0):
        parser.error('--max-slowdown-percent requires --regression and a finite nonnegative percentage')
    if args.require is not None and (not math.isfinite(args.require) or args.require <= 0):
        parser.error('--require must be a finite positive speed')
    if args.frames is not None and args.frames < 1:
        parser.error('--frames must be positive')
    if args.warmup is not None and args.warmup < 0:
        parser.error('--warmup cannot be negative')
    executable = (args.executable or args.build / 'ma_colony_benchmark').resolve(strict=True)
    folders = captures()
    cases = args.case or (list(reference['cases']) if reference else [*SYNTHETIC, *folders])
    if reference and any(case not in reference['cases'] for case in cases):
        parser.error('No regression reference for a requested case')
    unknown = [c for c in cases if c not in SYNTHETIC and c not in folders]
    if unknown:
        parser.error(f'Unknown cases: {unknown} (captures found: {sorted(folders)})')
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=ROOT, text=True).strip())
    output = args.output or ROOT / 'artifacts' / 'performance' / f'speed-{revision[:12]}-{time.strftime("%Y%m%d-%H%M%S")}'
    output.mkdir(parents=True, exist_ok=not args.regression)
    workers = reference['workers'] if reference else args.workers
    summary = {'revision': revision, 'dirty': dirty, 'host': host(), 'executable': str(executable), 'workers': workers, 'cases': {}}
    comparable = False
    if reference:
        summary['cpu_affinity'] = sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None
        comparable = summary['host'] == reference['host'] and summary['cpu_affinity'] == reference['cpu_affinity']
        summary.update(reference_revision=reference['revision'], comparable_timing_host=comparable,
                       max_slowdown_percent=args.max_slowdown_percent)
        if args.max_slowdown_percent is not None and not comparable:
            parser.error('Timing gate requires the same host configuration and high CPU lock as the reference')
        for case in cases:
            if case in folders:
                try:
                    verify_capture(folders[case], reference['cases'][case]['sha256'])
                except (OSError, ValueError) as error:
                    parser.error(str(error))
    rows, status = [], 0
    for case in cases:
        result = output / f'{case}.json'
        result.unlink(missing_ok=True)  # An explicit --output may hold an earlier successful run.
        expected = reference['cases'][case] if reference else None
        frames = expected['frames'] if expected else args.frames
        warmup = expected['warmup'] if expected else args.warmup
        command = [str(executable), *arguments_for(case, folders, frames, warmup, workers), '--json', str(result)]
        before, start = load(), time.monotonic()
        run = subprocess.run(command, capture_output=True, text=True)
        record = {'command': command[1:], 'exit': run.returncode, 'wall_s': round(time.monotonic() - start, 3),
                  'load_before': before, 'load_after': load()}
        if run.stderr.strip():
            record['stderr'] = run.stderr.strip()[-2000:]
        if result.is_file():
            try:
                measured = json.loads(result.read_text())
                validate_estimate(case, measured)
                if expected:
                    verify_regression(measured, expected, workers)
                    if comparable:
                        delta = {k: 100 * (measured['owner_ms'][k] / expected['owner_ms'][k] - 1) for k in ('mean', 'p95')}
                        record['owner_change_percent'] = delta
                        print(f"{case}: owner mean {delta['mean']:+.1f}%, p95 {delta['p95']:+.1f}% vs reference", file=sys.stderr)
                        if args.max_slowdown_percent is not None and max(delta.values()) > args.max_slowdown_percent:
                            raise ValueError('Owner slowdown exceeds requested percentage')
                record['row'] = row(case, measured)
                rows.append(record['row'])
                if record['row']['faulted']:
                    status = 1
            except (ValueError, TypeError, KeyError, AttributeError) as error:
                record['result_error'] = str(error)
                status = 1
        else:
            record['result_error'] = 'benchmark wrote no result JSON'
            status = 1
        if run.returncode != 0:
            status = 1
        summary['cases'][case] = record
        print(f'{case}: exit {run.returncode}, {record["wall_s"]} s', record.get('result_error', ''), file=sys.stderr)
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(table(rows))
    if args.baseline:
        earlier = json.loads(args.baseline.read_text())
        print()
        print(compare(rows, {name: record['row'] for name, record in earlier['cases'].items() if 'row' in record}))
    print(output / 'summary.json')
    if args.require is not None:
        slow = below(rows, args.require)
        missing = [case for case in cases if 'row' not in summary['cases'][case]]
        if slow or missing:
            print(f'Below {args.require}x: {slow}; missing results: {missing}', file=sys.stderr)
            status = 1
    return status


if __name__ == '__main__':
    sys.exit(main())
