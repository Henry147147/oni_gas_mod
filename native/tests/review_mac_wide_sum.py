"""Saved-operand review of wide exact sums, using bounded symbolic clusters."""
import copy
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

CHECKS = 0
COUNT = 5003
BASE = -28000000000000
HUGE_BASE = -(1 << 255)


def check(ok, why):
    global CHECKS
    CHECKS += 1
    if not ok:
        raise ValueError(why)


def scalar(m, e):
    value = float(m)
    check(math.isfinite(value) and (value == 0 or .5 <= abs(value) < 1), 'Invalid scaled significand')
    power = int(e)
    check(str(power) == e and len(e) <= 100 and (value != 0 or power == 0), 'Invalid exponent')
    return F(value), power


def parse(raw):
    data = dict(sums={}, colony=None, account=None, prefixes=None, passed=False)
    for line in raw.decode('ascii').splitlines():
        row = line.split()
        check(bool(row), 'Empty record')
        kind = row[0]
        if kind == 'WIDE':
            check(len(row) == 7 and int(row[1]) not in data['sums'], 'Duplicate or malformed wide sum')
            data['sums'][int(row[1])] = dict(n=int(row[2]), k=int(row[3]), sign=int(row[4]),
                value=scalar(*row[5:7]), inputs={}, parts={})
        elif kind in ('INPUT', 'PART'):
            check(len(row) == 5 and int(row[1]) in data['sums'], 'Orphan or malformed component')
            target = data['sums'][int(row[1])]['inputs' if kind == 'INPUT' else 'parts']
            index = int(row[2]); check(index not in target, 'Duplicate component')
            target[index] = scalar(*row[3:5])
        elif kind in ('COLONY_COUNT', 'ACCOUNT_TAIL', 'REFERENCE_PREFIXES'):
            key = dict(COLONY_COUNT='colony', ACCOUNT_TAIL='account', REFERENCE_PREFIXES='prefixes')[kind]
            check(data[key] is None, 'Duplicate native control')
            data[key] = tuple(map(int, row[1:]))
        elif kind == 'PASS':
            check(not data['passed'], 'Duplicate native pass'); data['passed'] = True
        else:
            raise ValueError('Unknown record: ' + kind)
    return data


def inputs(identifier):
    base = HUGE_BASE if identifier == 3 else BASE
    order = list(range(COUNT))
    if identifier == 1:
        order.reverse()
    elif identifier == 2:
        seed = 0x64cdbe973f120a85
        for i in range(COUNT - 1, 0, -1):
            seed = (seed * 6364136223846793005 + 1442695040888963407) % (1 << 64)
            j = seed % (i + 1); order[i], order[j] = order[j], order[i]
    result = []
    for i in order:
        m = F(5, 8) if i % 2 == 0 else F(3, 4)
        if identifier == 2 and i % 3 == 0:
            m = -m
        e = base + 512 * i
        result.append((m, e))
        if identifier == 2 and i % 37 == 0:
            result.append((F(1, 2), e - 52 - i % 4))
            if i % 74 == 0:
                result.append((-m, e))
    return result


def clusters(values, base):
    result = {}
    for m, e in values:
        index = (e - base + 256) // 512
        delta = e - base - 512 * index
        check(0 <= index < COUNT and abs(delta) <= 128 and m != 0, 'Component escaped bounded clusters')
        result[index] = result.get(index, F(0)) + m * F(2) ** delta
    return {i: v for i, v in result.items() if v}


def review(data):
    check(set(data['sums']) == set(range(4)), 'Missing wide cases')
    prefixes = 0
    for identifier, record in data['sums'].items():
        source = inputs(identifier)
        check(record['n'] == len(source) and set(record['inputs']) == set(range(len(source))), 'Missing source operands')
        check([record['inputs'][i] for i in range(len(source))] == source, 'Source fixture changed')
        check(4096 < record['k'] <= record['n'] and set(record['parts']) == set(range(record['k'])), 'Missing retained components')
        base = HUGE_BASE if identifier == 3 else BASE
        expected = clusters(source, base)
        actual = clusters(record['parts'].values(), base)
        check(actual == expected, 'Exact symbolic sum lost a low term or cancellation')
        leading = max(expected); value = expected[leading]
        check(record['sign'] == (1 if value > 0 else -1), 'Exact sign differs')
        m, e = record['value']; delta = e - base - 512 * leading
        check(abs(delta) <= 128, 'Estimate exponent differs')
        check(abs(m * F(2) ** delta - value) <= 4 * F(1, 2**52) * abs(value), 'Estimate differs from exact leading cluster')
        if identifier < 3:
            prefixes += 2 * len(source) + 2
    check(data['prefixes'] == (prefixes,) and data['colony'] == (250000, 0) and
        data['account'] == (2, 1, -1) and data['passed'], 'Native reference, stress, or exact-bound controls incomplete')


def main():
    raw = Path(sys.argv[1]).read_bytes()
    check(len(raw) <= 8 * 1024 * 1024, 'Capture exceeds review cap')
    data = parse(raw); review(data); saved_checks = CHECKS
    for corruption in ('tail', 'exponent', 'count', 'sign'):
        bad = copy.deepcopy(data)
        if corruption == 'tail':
            del bad['sums'][0]['parts'][0]
        elif corruption == 'exponent':
            m, e = bad['sums'][3]['parts'][0]; bad['sums'][3]['parts'][0] = (m, e + 1)
        elif corruption == 'count':
            bad['sums'][1]['k'] -= 1
        else:
            bad['sums'][2]['sign'] *= -1
        rejected = False
        try:
            review(bad)
        except ValueError:
            rejected = True
        check(rejected, 'Corrupt saved evidence accepted: ' + corruption)
    print(json.dumps(dict(Passed=True, SavedSha256=hashlib.sha256(raw).hexdigest(), SavedChecks=saved_checks,
        CorruptionsRejected=4, NativeProcessLaunched=False,
        Scope='Exact wide accumulator storage and cancellation only; colony-count sum is not a fluid or performance acceptance test')))


if __name__ == '__main__':
    main()
