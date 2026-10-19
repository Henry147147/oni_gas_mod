#!/usr/bin/env python3
"""Review saved primary-U state updates with exact fractions; launch no solver."""
import copy
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

CHECKS = 0


def check(ok, why):
    global CHECKS
    CHECKS += 1
    if not ok:
        raise ValueError(why)


def number(text):
    value = float(text)
    check(math.isfinite(value), 'Nonfinite saved primary-U value')
    return F(value)


def scaled(mantissa, exponent):
    m = number(mantissa)
    e = int(exponent)
    check(str(e) == exponent and -16384 < e < 16384, 'Noncanonical review exponent')
    check(not m or F(1, 2) <= abs(m) < 1, 'Noncanonical saved significand')
    return m * F(2) ** e


def parse(raw):
    rows, control, passed, void = [], None, False, False
    for line in raw.decode('ascii').splitlines():
        row = line.split()
        check(bool(row) and not passed, 'Empty/trailing primary-U record')
        if row[0] == 'UCARRY':
            check(len(row) == 13 and control is None, 'Primary-U update shape/order')
            rows.append((*map(int, row[1:4]), number(row[4]), scaled(row[5], row[6]),
                         number(row[7]), number(row[8]), scaled(row[9], row[10]), *map(int, row[11:])))
        elif line == 'PASS carried U in void strips and occupied-volume checkpoints':
            check(not void and control is None and len(rows)==186,'Void-U control order/duplicate')
            void=True
        elif row[0] == 'URANGE':
            check(row == ['URANGE', '-0.5', '-9223372036854775809', '6'] and control is None,
                  'Range/atomicity controls changed')
            control = tuple(row)
        else:
            check(line == 'PASS carried primary-U heat, checkpoint, continuation, range and atomicity' and control is not None,
                  'Missing primary-U native controls')
            passed = True
    check(passed, 'Missing primary-U completion')
    return dict(rows=rows, control=control, void=void)


def review(data):
    check(len(data['rows']) == 186 and data['control'] is not None and data['void'], 'Primary-U coverage changed')
    prior, initial, counts = {}, {}, {}
    for index, row in enumerate(data['rows']):
        kind, kernel, step, high0, low0, q, high1, low1, rounded, discarded = row
        check((kind, kernel, step) == (index // 62, (index // 31 % 2) * 2, index % 31), 'Primary-U case order changed')
        key = kind, kernel
        if step == 0:
            check(low0 == 0 and high0 != 0, 'Initial primary-U fixture changed')
            initial[key] = high0
        else:
            check((high0, low0) == prior[key], 'Carried primary-U continuation lost state')
        quantum = F(math.nextafter(float(initial[key]), math.inf)) - initial[key]
        check(q == (-1 if step % 3 == 0 else 1) * quantum / 8, 'Small heat input changed')
        check(high0 + low0 + q == high1 + low1, 'Small heat write lost energy')
        check(F(float(high0 + low0)) == high0 and F(float(high1 + low1)) == high1, 'Noncanonical primary-U state')
        check(rounded == 2 * int(low1 != 0) and discarded == 0, 'Primary-U write ledger differs')
        prior[key] = high1, low1
        counts[key] = counts.get(key, 0) + rounded
    check(set(prior) == {(kind, kernel) for kind in range(3) for kernel in (0, 2)}, 'Missing material/kernel controls')
    for key, value in prior.items():
        check(value[1] != 0 and counts[key] > 0, 'Empty primary-U carry coverage')
        check(value == prior[key[0], 2 - key[1]], 'Heat state differs between selected kernels')


def main():
    raw = Path(sys.argv[1]).read_bytes()
    check(len(raw) < 1024 * 1024, 'Primary-U capture cap')
    data = parse(raw)
    review(data)
    saved_checks = CHECKS
    mutations = ('kind', 'kernel', 'step', 'high', 'low', 'heat', 'after', 'remainder', 'rounded', 'discarded', 'missing', 'control')
    for mutation in mutations:
        bad = copy.deepcopy(data)
        if mutation == 'missing':
            bad['rows'].pop()
        elif mutation == 'control':
            bad['control'] = None
        else:
            row = list(bad['rows'][0])
            at = {'kind': 0, 'kernel': 1, 'step': 2, 'high': 3, 'low': 4, 'heat': 5,
                  'after': 6, 'remainder': 7, 'rounded': 8, 'discarded': 9}[mutation]
            row[at] += 1
            bad['rows'][0] = tuple(row)
        rejected = False
        try:
            review(bad)
        except ValueError:
            rejected = True
        check(rejected, 'Altered primary-U capture accepted: ' + mutation)
    print(json.dumps(dict(Passed=True, SavedChecks=saved_checks, CorruptionsRejected=len(mutations),
                          SavedSha256=hashlib.sha256(raw).hexdigest(), NativeProcessLaunched=False)))


if __name__ == '__main__':
    main()
