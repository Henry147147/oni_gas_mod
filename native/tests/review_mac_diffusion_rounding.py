"""Review saved transfer-rounding controls without launching a native process."""
import copy
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

EPS = F(1, 2**52)
QUANTUM = F(1, 2**1074)
CHECKS = 0


def check(ok, why):
    global CHECKS
    CHECKS += 1
    if not ok:
        raise ValueError(why)


def number(token):
    x = float(token)
    check(math.isfinite(x), 'Nonfinite saved operand')
    return F(x)


def wide(mantissa, exponent):
    e = int(exponent)
    check(-10000 < e < 10000, 'Unbounded saved exponent')
    return number(mantissa) * F(2)**e


def parse(raw):
    data = dict(binary={}, controls={}, fronts={}, mass={})
    for line in raw.decode('ascii').splitlines():
        row = line.split()
        check(bool(row), 'Empty record')
        kind = row[0]
        if kind == 'QROUND':
            check(len(row) == 15, 'Binary record shape')
            key = int(row[1]); table = data['binary']
            value = dict(d=number(row[2]), dt=number(row[3]), steps=int(row[4]), initial=number(row[5]),
                         left=number(row[6]), right=number(row[7]), total=int(row[8]),
                         counts=(int(row[9]), int(row[12])), errors=(wide(*row[10:12]), wide(*row[13:15])))
        elif kind in ('QREFINE', 'QTERNARY'):
            check(len(row) == 5, 'Control shape')
            key = kind; table = data['controls']; value = tuple(map(int, row[1:]))
        elif kind == 'QFRONT':
            check(len(row) == 17, 'Front record shape')
            key = (int(row[1]), int(row[2]), int(row[4]), int(row[5])); table = data['fronts']
            value = (int(row[3]), int(row[6]), int(row[7]), int(row[8]), wide(*row[9:11]), wide(*row[11:13]), tuple(map(number, row[13:17])))
        elif kind == 'QMASS':
            check(len(row) == 7, 'Inventory record shape')
            key = tuple(map(int, row[1:6])); table = data['mass']; value = number(row[6])
        elif kind == 'PASS':
            key = kind; table = data['controls']; value = True
        else:
            raise ValueError('Unknown saved record: ' + kind)
        check(key not in table, 'Duplicate saved record'); table[key] = value
    return data


def review(data, large=False):
    check(data['controls'].get('PASS'), 'Missing native completion')
    if large:
        expected = {(128, 64, m, c): 400 for m in (1, 100) for c in (0, 1)}
        check(not data['binary'] and set(data['controls']) == {'PASS'}, 'Unexpected large-run controls')
    else:
        expected = {(64, 1, m, 0): 400 for m in (1, 100)} | {(256, 1, m, 1): 40 for m in (1, 100)}
        check(set(data['binary']) == {2, 4}, 'Missing quantized binary control')
        cells, rejected, steps, count = data['controls']['QREFINE']
        check((cells, rejected, steps) == (16384, 1, 1) and count == 4 * 31 * 512, 'Range refinement lost shared budget/rounding')
        steps, rejected, count, unchanged = data['controls']['QTERNARY']
        check(steps > 0 and rejected > 0 and count == 6 * steps and unchanged == 1, 'Missing zero-net-mass retry control')
        for units, row in data['binary'].items():
            check(row['d'] > 0 and row['dt'] > 0 and row['initial'] == units * QUANTUM, 'Binary input differs')
            left, right, elapsed, steps, error = units, 0, 0., 0, F(0)
            duration, coefficient = float(row['dt']), float(row['d'])
            # Independent quantized Heun for equal-property binary diffusion.
            # Each face is rounded once, then applied with opposite signs.
            while elapsed < duration:
                dt = min(duration - elapsed, .18 / coefficient)
                force = row['d'] * F(dt)
                first = force * (left - right); q0 = round(first)
                predictor_left, predictor_right = left - q0, right + q0
                corrected = force * ((left - right) + (predictor_left - predictor_right)) / 2
                q1 = round(corrected)
                error += abs(first - q0) + abs(corrected - q1)
                left -= q1; right += q1; elapsed += dt; steps += 1
            check(row['steps'] == steps and row['total'] == 4 * steps and row['counts'] == (2 * steps, 2 * steps), 'Rounded face count differs')
            check((row['left'], row['right']) == (left * QUANTUM, right * QUANTUM), 'Quantized Heun endpoint differs')
            check(row['left'] + row['right'] == row['initial'], 'Quantized face lost constituent mass')
            for actual in row['errors']:
                exact = error * QUANTUM
                check(0 < actual <= steps * QUANTUM, 'Transfer error exceeds counted half-quanta')
                check(abs(actual - exact) <= 128 * EPS * (actual + exact), 'Reported projection error differs from analytical transfer')
    check(set(data['fronts']) == set(expected), 'Missing spreading-front scene')
    expected_keys = set()
    for key, (frames, rounded, energy_rounded, positive, residual, bound, metrics) in data['fronts'].items():
        w, h, multiplier, composed = key
        check(frames == expected[key] and 0 < positive <= w * h and rounded >= 0 and energy_rounded >= 0, 'Invalid front completion')
        check(all(0 <= error <= tolerance for error, tolerance in zip(metrics, (F(1, 10), F(1, 100), F(1, 100), F(1, 100)))), 'Bulk production metric exceeds its unchanged bound')
        if multiplier == 1 or composed:
            check(rounded > 0 and energy_rounded > 0, 'Front never exercised counted mass/energy rounding')
        keys = {(*key, i) for i in range(w * h)}; expected_keys |= keys
        check(keys <= data['mass'].keys(), 'Incomplete saved inventory')
        amounts = [data['mass'][(*key, i)] for i in range(w * h)]
        check(all(x >= 0 for x in amounts) and sum(x > 0 for x in amounts) == positive, 'Invalid front inventory/occupancy')
        initial, final = F(1e-28), sum(amounts, F(0))
        exact_residual, exact_bound = final - initial, 128 * EPS * (final + initial)
        check(abs(exact_residual) <= exact_bound, 'Front failed unchanged strict endpoint mass bound')
        check(abs(residual - exact_residual) <= 4 * EPS * abs(exact_residual), 'Reported full-range residual differs')
        check(abs(bound - exact_bound) <= 4 * EPS * exact_bound, 'Reported endpoint bound differs')
    check(set(data['mass']) == expected_keys, 'Extra saved inventory')


def main():
    raw = Path(sys.argv[1]).read_bytes(); check(len(raw) < 4 * 1024 * 1024, 'Saved pipe cap')
    large = len(sys.argv) == 3 and sys.argv[2] == '--large-fronts'
    data = parse(raw); review(data, large); saved = CHECKS
    corruptions = ['inventory', 'mass_bound', 'rounding_count'] + ([] if large else ['quantized_endpoint', 'rounding_error', 'shared_budget'])
    for kind in corruptions:
        bad = copy.deepcopy(data); front = next(iter(bad['fronts']))
        if kind == 'inventory': bad['mass'][(*front, 0)] += F(1e-28)
        elif kind == 'mass_bound':
            row = list(bad['fronts'][front]); row[5] *= 2; bad['fronts'][front] = tuple(row)
        elif kind == 'rounding_count':
            row = list(bad['fronts'][front]); row[1] = 0; bad['fronts'][front] = tuple(row)
        elif kind == 'quantized_endpoint': bad['binary'][2]['right'] += QUANTUM
        elif kind == 'rounding_error': bad['binary'][2]['errors'] = (F(0), F(0))
        else: bad['controls']['QREFINE'] = (16384, 0, 1, 1)
        rejected = False
        try: review(bad, large)
        except ValueError: rejected = True
        check(rejected, 'Corrupted saved rounding evidence accepted: ' + kind)
    print(json.dumps(dict(Passed=True, SavedChecks=saved, CorruptionsRejected=len(corruptions),
                         SavedSha256=hashlib.sha256(raw).hexdigest(), NativeProcessLaunched=False,
                         Scope='Counted diffusion transfers; unchanged strict endpoint inventory bound; no colony performance acceptance')))


if __name__ == '__main__':
    main()
