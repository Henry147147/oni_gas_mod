#!/usr/bin/env python3
"""Check saved two-part primary writes using exact fractions; launch no solver."""
import copy
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

CHECKS = 0


def check(value, why):
    global CHECKS
    CHECKS += 1
    if not value:
        raise ValueError(why)


def number(text):
    value = float(text)
    check(math.isfinite(value), "Nonfinite primary operand")
    return F(value)


def parse(raw):
    data = dict(rows=[], repeated=None, count=None)
    for line in raw.decode("ascii").splitlines():
        row = line.split()
        check(bool(row) and data['count'] is None, "Empty/trailing record")
        if row[0] == 'PRADD':
            check(len(row) == 9 and data['repeated'] is None, "Primary record shape/order")
            data['rows'].append((int(row[1]), *map(number, row[2:8]), int(row[8])))
        elif row[0] == 'PRREPEATED':
            check(len(row) == 5 and data['repeated'] is None, "Repeated-update record shape/duplicate")
            data['repeated'] = (*map(number, row[1:4]), int(row[4]))
        elif row[0] == 'PRPASS':
            check(len(row) == 2 and data['repeated'] is not None, "Missing controls/pass shape")
            data['count'] = int(row[1])
        else:
            raise ValueError("Unknown primary record")
    return data


def review(data):
    check(data['count'] == len(data['rows']) == 264, "Primary coverage/count changed")
    tiny = F(1, 1 << 1074)
    initial = [(F(1), F(0), F(1, 1 << 54)),
               (F(1), F(1, 1 << 54), F(1, 1 << 54)),
               (F(1), F(1, 1 << 54), F(-1)),
               (F(-1), F(-1, 1 << 54), F(1)),
               (F(0), F(0), tiny),
               (F(1, 1 << 1022), F(0), -tiny),
               (F(1 << 1000), F(1 << 945), F(1 << 800)),
               (F(1), F(1, 1 << 100), F(1, 1 << 1000))]
    rounded, discarded = 0, 0
    for n, (identity, value, low, increment, high, remainder, tail, flag) in enumerate(data['rows']):
        check(identity == n and flag in (0, 1), "Primary ordering/flag")
        if n < len(initial):
            check((value, low, increment) == initial[n], "Corner input changed")
        check(F(float(value + low)) == value, "Noncanonical incoming primary pair")
        check(value + low + increment == high + remainder + tail, "Unreported primary storage error")
        check(F(float(high + remainder)) == high, "Noncanonical outgoing primary pair")
        half = F(math.ulp(float(remainder))) / 2 if remainder else F(0)
        check(abs(tail) <= half, "Third part exceeds half a low-part ulp")
        exact = value + increment
        check(flag == int(exact != F(float(exact))), "High-part rounding flag differs")
        rounded += flag
        discarded += int(tail != 0)
    check(0 < rounded < len(data['rows']) and discarded > 0, "Empty rounding/discard coverage")
    high, low, plain, count = data['repeated']
    check(count == 100000 and plain == 1 << 40, "Lost-update control changed")
    exact = F(1 << 40) + F(count, 1 << 20)
    check(high == F(float(exact)) and high + low == exact and high > plain,
          "Repeated heat updates were lost")


def main():
    raw = Path(sys.argv[1]).read_bytes()
    check(len(raw) < 1024 * 1024, "Primary capture cap")
    data = parse(raw)
    review(data)
    saved_checks = CHECKS
    mutations = ('input', 'increment', 'high', 'low', 'tail', 'flag', 'missing', 'identity', 'count', 'repeated')
    for mutation in mutations:
        bad = copy.deepcopy(data)
        if mutation in ('input', 'increment', 'high', 'low', 'tail', 'flag', 'identity'):
            index = next(n for n, row in enumerate(bad['rows']) if row[6] != 0) if mutation == 'tail' else 0
            row = list(bad['rows'][index])
            position = {'identity': 0, 'input': 1, 'increment': 3, 'high': 4, 'low': 5, 'tail': 6, 'flag': 7}[mutation]
            if mutation in ('low', 'tail'):
                row[position] = F(0)
            elif mutation == 'flag':
                row[position] = 1 - row[position]
            else:
                row[position] += 1
            bad['rows'][index] = tuple(row)
        elif mutation == 'missing':
            bad['rows'].pop()
        elif mutation == 'count':
            bad['count'] += 1
        else:
            row = list(bad['repeated']); row[1] = F(0); bad['repeated'] = tuple(row)
        rejected = False
        try:
            review(bad)
        except ValueError:
            rejected = True
        check(rejected, 'Altered primary capture admitted: ' + mutation)
    print(json.dumps(dict(Passed=True, SavedChecks=saved_checks,
                          CorruptionsRejected=len(mutations), SavedSha256=hashlib.sha256(raw).hexdigest(),
                          NativeProcessLaunched=False)))


if __name__ == '__main__':
    main()
