"""Verify saved front diagnostics, including evidence of a failed front."""
from pathlib import Path
from fractions import Fraction as F
from decimal import Decimal as D, localcontext
import copy
import hashlib
import json
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import review_mac_closed_world as closed

base = closed.base
check, near, number, unique = base.check, base.close, base.number, base.unique


def parse(raw):
    check(0 < len(raw) <= 2097152, 'Front diagnostic stream bound')
    data = dict(materials={}, fractions={}, config=None, masses={}, fronts=[], speed=None)
    for line in raw.decode('ascii').splitlines():
        row = line.split(); check(bool(row), 'Empty record')
        if row[0] == 'VFMATERIAL':
            check(len(row) == 4, 'Material shape'); unique(data['materials'], int(row[1]), tuple(map(number, row[2:])))
        elif row[0] == 'VFFRACTION':
            check(len(row) == 3, 'Fraction shape'); unique(data['fractions'], int(row[1]), number(row[2]))
        elif row[0] == 'VFCONFIG':
            check(len(row) == 7 and data['config'] is None, 'Config shape'); data['config'] = tuple(map(number, row[1:]))
        elif row[0] == 'VFOCCUPIED':
            check(len(row) == 4, 'Mass shape'); unique(data['masses'], tuple(map(int, row[1:3])), number(row[3]))
        elif row[0] == 'VFRONT':
            check(len(row) == 10, 'Front shape'); data['fronts'].append(tuple(map(number, row[1:])))
        elif row[0] == 'VSPEED':
            check(len(row) == 3 and data['speed'] is None, 'Speed shape'); data['speed'] = tuple(map(number, row[1:]))
        else:
            raise ValueError('Unknown front record: ' + row[0])
    return data


def review(data):
    check(data['config'] is not None and len(data['fronts']) == 2 and data['speed'] is not None, 'Complete front diagnostic')
    n, h, dt, sound, gamma, duration = data['config']
    check(n.denominator == 1 and 16 <= n <= 2048 and n % 4 == 0 and h == F(float(8 / n)), 'Original grid')
    n = int(n)
    check(data['fractions'] == {0: F(.22), 1: F(.78), 7: F(1e-28)} and set(data['materials']) == {0, 1, 7}, 'Original mixture')
    coefficient = sum((closed.dec(f) * closed.dec(F(8.31446261815324)) / closed.dec(data['materials'][i][0])
                       for i, f in data['fractions'].items()), D(0))
    capacity = sum((closed.dec(f * data['materials'][i][1]) for i, f in data['fractions'].items()), D(0))
    g = 1 + coefficient / capacity
    c = (g * 100000).sqrt()
    time = D('.5') / c
    front_speed = 2 / (g - 1)
    for actual, expected in ((gamma, g), (sound, c), (duration, time), (dt, D('.025') * closed.dec(h) / c)):
        check(abs(closed.dec(actual) - expected) <= D('2e-14') * abs(expected), 'Original gas/fan/timestep constants')
    check(set(data['masses']) == {(e, i) for e in range(3) for i in range(n)}, 'Exact profile coverage')
    for i in range(n):
        check(data['masses'][0, i] == (h if i < n // 4 else 0), 'Initial true vacuum or density changed')
    measured = []
    for endpoint, row in enumerate(data['fronts'], 1):
        count, clock, front, exact, occupied, steps, rejected, residual, bound = row
        expected_clock = time * (D('.8') if endpoint == 1 else 1)
        check(count == n and abs(closed.dec(clock) - expected_clock) <= D('2e-14') * expected_clock, 'Original observation time')
        check(steps.denominator == rejected.denominator == 1 and steps > 0 and rejected == 0, 'Nominal stepping receipt')
        check(abs(residual) <= bound and bound >= 0, 'Native energy receipt')
        values = [data['masses'][endpoint, i] for i in range(n)]
        check(all(value >= 0 for value in values), 'Negative density')
        near(sum(values), F(2), sum(values) + 2, 'Closed profile mass')
        positive = [i for i, value in enumerate(values) if value > 0]
        actual_front = (positive[-1] + 1) * h if positive else F(0)
        check(front == actual_front and occupied == len(positive), 'Front uses every strictly positive mass without a cutoff')
        expected_front = 2 + c * front_speed * expected_clock
        check(abs(closed.dec(exact) - expected_front) <= D('2e-14') * expected_front, 'Independent vacuum-front oracle')
        measured.append(front)
    speed = (closed.dec(measured[1]) - closed.dec(measured[0])) / (D('.2') * time * c)
    for actual, expected in zip(data['speed'], (speed, front_speed)):
        check(abs(closed.dec(actual) - expected) <= D('2e-14') * max(D(1), abs(expected)), 'Independent recent speed')
    position_error = abs(closed.dec(measured[1]) - (2 + c * front_speed * time))
    recent_displacement_error = abs(speed - front_speed) * c * D('.2') * time
    return dict(EvidenceValid=True, FrontGatePassed=False, GridCount=n,
                CellScaleChecksPassed=position_error <= closed.dec(h) and recent_displacement_error <= closed.dec(h),
                FrontPositionErrorM=str(position_error), RecentSpeedOverSound=str(speed), ExactSpeedOverSound=str(front_speed),
                NoDensityCutoff=True, NativeProcessLaunched=False,
                Scope='One grid, original ideal-gas/vacuum problem. Verifies actual positive support and oracle; no grid/time refinement, full physics or production acceptance.')


def main():
    raw = Path(sys.argv[1]).read_bytes(); data = parse(raw)
    with localcontext() as context:
        context.prec = 100
        result = review(data); checks = base.CHECKS; rejected = []
        for kind in ('clipped-front', 'lost-tail', 'fake-speed', 'changed-timestep', 'lost-mass'):
            bad = copy.deepcopy(data)
            if kind == 'clipped-front':
                row = list(bad['fronts'][1]); row[2] = row[3]; bad['fronts'][1] = tuple(row)
            elif kind == 'lost-tail':
                bad['masses'][2, int(bad['config'][0]) - 1] = F(0)
            elif kind == 'fake-speed':
                bad['speed'] = (bad['speed'][1], bad['speed'][1])
            elif kind == 'changed-timestep':
                row = list(bad['config']); row[2] *= 2; bad['config'] = tuple(row)
            else:
                bad['masses'][2, 0] *= F(99, 100)
            try:
                review(bad)
            except (ValueError, KeyError, AssertionError):
                rejected.append(kind)
            else:
                raise ValueError('Corrupt front evidence accepted: ' + kind)
    result.update(SavedChecks=checks, CorruptionsRejected=rejected, InputSha256=hashlib.sha256(raw).hexdigest(),
                  AnalysisSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    with Path(sys.argv[2]).open('x') as output:
        json.dump(result, output, indent=2); output.write('\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
