#!/usr/bin/env python3
"""Independently review a saved range-primary capture; never launch a solver."""
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
    check(math.isfinite(value), "Nonfinite primary number")
    return F(value)


def scaled(mantissa, exponent):
    m, e = number(mantissa), int(exponent)
    check(abs(e) <= 10000 and (m == 0 and e == 0 or F(1, 2) <= abs(m) < 1), "Noncanonical range operand")
    return m * F(2) ** e


def parse(raw):
    rows, count = [], None
    for line in raw.decode("ascii").splitlines():
        fields = line.split()
        check(bool(fields) and count is None, "Empty/trailing record")
        if fields[0] == "PRFULL":
            check(len(fields) == 14 and (not rows or len(rows[-1]['tails']) == rows[-1]['tail_count']), "Primary shape or missing tails")
            rows.append(dict(identity=int(fields[1]), high=number(fields[2]), low=scaled(*fields[3:5]),
                             increment=scaled(*fields[5:7]), stored=number(fields[7]), remainder=scaled(*fields[8:10]),
                             projection=number(fields[10]), full=int(fields[11]), rounded=int(fields[12]),
                             tail_count=int(fields[13]), tails=[]))
        elif fields[0] == "PRTAIL":
            check(len(fields) == 3 and rows and len(rows[-1]['tails']) < rows[-1]['tail_count'], "Tail shape/order")
            rows[-1]['tails'].append(scaled(*fields[1:3]))
        elif fields[0] == "PRFULLPASS":
            check(len(fields) == 2, "Pass shape")
            count = int(fields[1])
        else:
            raise ValueError("Unknown range-primary record")
    return rows, count


def review(data):
    rows, count = data
    check(count == len(rows) == 1014, "Range-primary coverage/count changed")
    full, discarded = 0, 0
    for n, row in enumerate(rows):
        check(row['identity'] == n and row['rounded'] in (0, 1) and row['full'] in (0, 1), "Primary ordering/flag")
        check(len(row['tails']) == row['tail_count'] <= 16, "Primary tail coverage")
        h, l, q, s, r = (row[k] for k in ('high', 'low', 'increment', 'stored', 'remainder'))
        exact = h + l + q
        tail = sum(row['tails'], F(0))
        check(exact == s + r + tail, "Unreported primary storage error")
        check(F(float(s + r)) == s, "Noncanonical stored primary pair")
        check(F(float(r)) == row['projection'] and row['full'] == int(F(float(r)) != r), "Remainder projection/flag differs")
        check(row['rounded'] == int(exact != s), "High-part rounding flag differs")
        scale = abs(h) + abs(l) + abs(q) + abs(s) + abs(r)
        check(abs(tail) <= scale / (1 << 45), "Primary error exceeds unchanged 128-epsilon operand account")
        full += row['full']
        discarded += tail != 0
    check(full >= 515 and discarded >= 947, "Missing flagged-range/discarded-tail coverage")
    for n, exponent in enumerate((-1075, -2000, -4096)):
        a, b = rows[1000 + 2*n:1002 + 2*n]
        tiny = F(2) ** (exponent - 1)
        check((a['high'], a['low'], a['increment'], a['stored'], a['remainder']) == (1, 0, tiny, 1, tiny), "Below-range retention control changed")
        check((b['high'], b['low'], b['increment'], b['stored'], b['remainder']) == (1, tiny, -1, 0, tiny), "Cancellation control changed")


def main():
    raw = Path(sys.argv[1]).read_bytes()
    check(len(raw) <= 2*1024*1024, "Primary capture cap")
    data = parse(raw)
    review(data)
    measured_checks = CHECKS
    mutations = ('high', 'low', 'increment', 'stored', 'remainder', 'projection', 'full', 'rounded', 'tail', 'count', 'missing', 'identity')
    for name in mutations:
        bad = copy.deepcopy(data)
        rows, count = bad
        if name in rows[0]:
            row = rows[1000] if name == 'full' else rows[0]
            row[name] = 1-row[name] if name in ('full', 'rounded') else row[name] + 1
        elif name == 'tail':
            next(row for row in rows if row['tails'])['tails'][0] += 1
        elif name == 'count':
            bad = rows, count-1
        elif name == 'missing':
            rows.pop()
        elif name == 'identity':
            rows[0]['identity'] = 1
        try:
            review(bad)
        except ValueError:
            continue
        raise ValueError("Accepted corrupted range-primary " + name)
    print(json.dumps(dict(passed=True, records=len(data[0]), checks=measured_checks,
                          corruptions_rejected=len(mutations), sha256=hashlib.sha256(raw).hexdigest()), sort_keys=True))


if __name__ == '__main__':
    main()
