"""Review saved production parcel-rounding records using exact fractions."""
import copy
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

CHECKS = 0
QUANTUM = F(2) ** -1074
NORMAL = F(2) ** -1022
EPS = F(2) ** -52


def check(ok, why):
    global CHECKS
    CHECKS += 1
    if not ok:
        raise ValueError(why)


def number(value):
    x = float(value)
    check(math.isfinite(x), 'Nonfinite saved value')
    return F(x)


def wide(mantissa, exponent):
    power = int(exponent)
    check(-10000 < power < 10000, 'Unbounded saved exponent')
    return number(mantissa) * F(2) ** power


def expansion(row, start, count):
    return sum((wide(*row[i:i + 2]) for i in range(start, start + 2 * count, 2)), F(0))


def parse(raw):
    data = dict(products={}, mass={}, merge=None, threads=None, front=None, passed=False)
    for line in raw.decode('ascii').splitlines():
        row = line.split(); check(bool(row), 'Empty saved record')
        kind = row[0]
        if kind in ('TPRODUCT', 'TMERGE'):
            start = 6 if kind == 'TPRODUCT' else 3
            components = int(row[start]); check(0 <= components <= 64, 'Invalid expansion size')
            check(len(row) == start + 3 + 2 * components, 'Product record shape')
            error = expansion(row, start + 1, components)
            bound = wide(*row[-2:])
            if kind == 'TPRODUCT':
                key = int(row[1]); check(key not in data['products'], 'Duplicate product')
                data['products'][key] = (*map(number, row[2:5]), int(row[5]), error, bound)
            else:
                check(data['merge'] is None, 'Duplicate merged ledger')
                data['merge'] = (int(row[1]), int(row[2]), error, bound)
        elif kind == 'TMASS':
            check(len(row) == 3, 'Mass record shape'); key = int(row[1])
            check(key not in data['mass'], 'Duplicate inventory'); data['mass'][key] = number(row[2])
        elif kind == 'TTHREAD':
            check(len(row) == 6 and data['threads'] is None, 'Thread record shape')
            data['threads'] = tuple(map(int, row[1:]))
        elif kind == 'TFRONT':
            check(len(row) == 7 and data['front'] is None, 'Front record shape')
            data['front'] = (*map(int, row[1:6]), number(row[6]))
        elif kind == 'PASS':
            check(not data['passed'], 'Duplicate completion'); data['passed'] = True
        else:
            raise ValueError('Unknown saved record: ' + kind)
    return data


def review(data):
    check(data['passed'], 'Missing native completion')
    check(set(data['products']) == set(range(1080)), 'Missing product controls')
    total_count, total_error = 0, F(0)
    for a, b, stored, count, error, bound in data['products'].values():
        exact = a * b
        check(stored == F(float(exact)), 'Product is not correctly rounded binary64')
        expected = abs(exact - stored) if abs(stored) <= NORMAL else F(0)
        check(count == int(expected != 0), 'Actual projection count differs')
        check(error == expected, 'Exact product-error expansion differs')
        check(bound == count * QUANTUM / 2 and error <= bound, 'Counted half-quantum bound differs')
        total_count += count; total_error += error
    check(data['merge'] == (1080, total_count, total_error, total_count * QUANTUM / 2), 'Merged exact ledger differs')
    cells, threads, first, second, split = data['threads']
    check(cells == 33389 and threads == 12 and min(first, second, split) > 0, 'Missing threaded accepted-ledger control')
    cells, frames, steps, rounded, positive, initial = data['front']
    check((cells, frames) == (8192, 160) and steps >= frames and rounded > 0 and positive > 100 and initial == F(1e-28),
          'Spreading-front control differs')
    check(set(data['mass']) == set(range(cells)), 'Incomplete room inventory')
    amounts = list(data['mass'].values()); check(all(x >= 0 for x in amounts), 'Negative room inventory')
    check(sum(x > 0 for x in amounts) == positive, 'Front occupancy differs')
    final = sum(amounts, F(0))
    check(abs(final - initial) <= 128 * EPS * (final + initial), 'Front failed unchanged strict endpoint mass bound')


def main():
    raw = Path(sys.argv[1]).read_bytes(); check(len(raw) < 4 * 1024 * 1024, 'Saved pipe cap')
    data = parse(raw); review(data); saved = CHECKS
    mutations = ('product', 'count', 'error', 'bound', 'merge', 'threads', 'inventory')
    key = next(k for k, v in data['products'].items() if v[3])
    for kind in mutations:
        bad = copy.deepcopy(data)
        if kind in ('product', 'count', 'error', 'bound'):
            row = list(bad['products'][key]); index = {'product': 2, 'count': 3, 'error': 4, 'bound': 5}[kind]
            row[index] += 1 if kind == 'count' else QUANTUM; bad['products'][key] = tuple(row)
        elif kind == 'merge':
            row = list(bad['merge']); row[2] += QUANTUM; bad['merge'] = tuple(row)
        elif kind == 'threads':
            row = list(bad['threads']); row[1] = 1; bad['threads'] = tuple(row)
        else:
            bad['mass'][0] += F(1e-28)
        rejected = False
        try:
            review(bad)
        except ValueError:
            rejected = True
        check(rejected, 'Corrupted saved evidence accepted: ' + kind)
    print(json.dumps(dict(Passed=True, SavedChecks=saved, CorruptionsRejected=len(mutations),
                         SavedSha256=hashlib.sha256(raw).hexdigest(), NativeProcessLaunched=False,
                         Scope='Primal parcel products and trace fronts; no real-time acceptance')))


if __name__ == '__main__':
    main()
