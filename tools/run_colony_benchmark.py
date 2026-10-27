#!/usr/bin/env python3
"""Run the colony owner performance suite (docs/performance-acceptance.md).

Each case runs `ma_colony_benchmark` once and keeps its JSON; the summary
records the revision, host and load. Take the CPU lock yourself, e.g.
`tools/run_locked.sh whole tools/run_colony_benchmark.py --suite release`.
Synthetic cases are reproducible from the source; `--world-dump` adds private
captured worlds (never commit them). This is solver-plus-native-adapter
evidence, not an in-game, managed-adapter or FPS measurement.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]

# name: (arguments, timeout seconds). Frame counts keep each case bounded.
SUITES = {
    'smoke': {
        'smoke': (['--preset', 'smoke'], 120),
    },
    'release': {
        # The release targets: 100,000 and 250,000 active cells, 32 duplicants.
        '100k': (['--preset', '100k', '--frames', '30', '--warmup', '5'], 3600),
        '100k-cap4000': (['--preset', '100k', '--frames', '30', '--warmup', '5', '--pressure-iterations', '4000'], 3600),
        '100k-no-hydrogen': (['--preset', '100k', '--no-hydrogen-rooms', '--frames', '30', '--warmup', '5'], 3600),
        '250k-no-hydrogen': (['--preset', '250k', '--no-hydrogen-rooms', '--frames', '12', '--warmup', '2'], 3600),
        # 10,000-cell decompression, 1,000-cell flood and 1,000-cell boil.
        'events': (['--preset', '100k', '--no-hydrogen-rooms', '--events', '--warmup', '3', '--event-limit-s', '45'], 3600),
    },
}


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
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', type=Path, default=ROOT / 'build-production', help='Build folder holding ma_colony_benchmark')
    parser.add_argument('--suite', choices=sorted(SUITES), default='release')
    parser.add_argument('--case', action='append', help='Run only these cases of the suite')
    parser.add_argument('--world-dump', type=Path, action='append', default=[], help='Private captured world (repeatable)')
    parser.add_argument('--workers', type=int, default=0, help='0: physical cores (the game default)')
    parser.add_argument('--output', type=Path, help='Result folder (default artifacts/performance/<revision>-<time>)')
    args = parser.parse_args()
    executable = (args.build / 'ma_colony_benchmark').resolve(strict=True)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=ROOT, text=True).strip())
    output = args.output or ROOT / 'artifacts' / 'performance' / f'{revision[:12]}-{time.strftime("%Y%m%d-%H%M%S")}'
    output.mkdir(parents=True, exist_ok=True)
    cases = dict(SUITES[args.suite])
    if args.case:
        unknown = set(args.case) - set(cases)
        if unknown:
            parser.error(f'Unknown cases: {sorted(unknown)}')
        cases = {name: cases[name] for name in args.case}
    for dump in args.world_dump:
        cases[f'dump-{dump.stem}'] = (['--world-dump', str(dump.resolve(strict=True)), '--frames', '20', '--warmup', '2'], 3600)
    summary = {'revision': revision, 'dirty': dirty, 'host': host(), 'executable': str(executable),
               'suite': args.suite, 'workers': args.workers, 'cases': {}}
    status = 0
    for name, (arguments, timeout) in cases.items():
        result = output / f'{name}.json'
        command = [str(executable), *arguments, '--json', str(result)]
        if args.workers:
            command += ['--workers', str(args.workers)]
        before = load()
        start = time.monotonic()
        try:
            run = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
            code, stderr = run.returncode, run.stderr
        except subprocess.TimeoutExpired:
            code, stderr = None, 'timeout'
        record = {'command': command[1:], 'exit': code, 'wall_s': round(time.monotonic() - start, 3),
                  'load_before': before, 'load_after': load()}
        if stderr.strip():
            record['stderr'] = stderr.strip()[-2000:]
        if result.is_file():
            record['result'] = json.loads(result.read_text())
        if code not in (0, 1):  # 1: the owner faulted (recorded in the result)
            status = 1
        summary['cases'][name] = record
        print(f'{name}: exit {code}, {record["wall_s"]} s', file=sys.stderr)
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(output / 'summary.json')
    return status


if __name__ == '__main__':
    sys.exit(main())
