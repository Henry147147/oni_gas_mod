"""Review saved flagged-sweep controls with exact rational transport operands."""
import copy
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

CHECKS = 0
EPS = F(2) ** -52
QUANTUM = F(2) ** -1074
NORMAL = F(2) ** -1022


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


def near(actual, expected, why):
    check(abs(actual - expected) <= 128 * EPS * (abs(actual) + abs(expected)), why)


def parse(raw):
    data = dict(cases={}, shares={}, writes={}, version=None, ledgers={}, thread=None, cross=None, refine=None, controls=None, passed=False)
    for line in raw.decode('ascii').splitlines():
        row = line.split(); check(bool(row), 'Empty saved record')
        kind = row[0]
        if kind == 'RVERSION':
            check(len(row) == 2 and data['version'] is None, 'Version record shape'); data['version'] = int(row[1])
        elif kind == 'RCASE':
            check(len(row) == 10, 'Case record shape'); key = int(row[1])
            check(key not in data['cases'], 'Duplicate case')
            data['cases'][key] = (int(row[2]), *map(number, row[3:9]), int(row[9]))
        elif kind == 'RSHARE':
            check(len(row) == 11, 'Share record shape'); key = int(row[1]); check(key not in data['shares'], 'Duplicate share')
            data['shares'][key] = (int(row[2]), *map(number, row[3:10]), int(row[10]))
        elif kind == 'RWRITE':
            check(len(row) == 3, 'Write record shape'); key = int(row[1]); check(key not in data['writes'], 'Duplicate write count')
            data['writes'][key] = int(row[2])
        elif kind == 'RLEDGER':
            check(len(row) == 8, 'Ledger record shape'); key = (int(row[1]), row[2])
            check(key not in data['ledgers'], 'Duplicate ledger')
            data['ledgers'][key] = (int(row[3]), wide(*row[4:6]), wide(*row[6:8]))
        elif kind == 'RTHREAD':
            check(len(row) == 5 and data['thread'] is None, 'Thread record shape')
            data['thread'] = (*map(int, row[1:4]), number(row[4]))
        elif kind == 'RREFINE':
            check(len(row) == 5 and data['refine'] is None, 'Refinement record shape')
            data['refine'] = (*map(int, row[1:3]), *map(number, row[3:5]))
        elif kind == 'RCROSS':
            check(len(row) == 7 and data['cross'] is None, 'Cross-block record shape')
            data['cross'] = (*map(int, row[1:6]), number(row[6]))
        elif kind == 'RCONTROLS':
            check(len(row) == 4 and data['controls'] is None, 'Control record shape')
            data['controls'] = tuple(map(int, row[1:]))
        elif kind == 'PASS':
            check(not data['passed'], 'Duplicate completion'); data['passed'] = True
        else:
            raise ValueError('Unknown saved record: ' + kind)
    return data


def review(data, legacy=False):
    check(data['version'] is None if legacy else data['version'] in (2, 3), 'Wrong saved-evidence version')
    check(data['passed'] and data['controls'] == (1, 1, 1), 'Missing native completion/continuation controls')
    check(set(data['cases']) == set(range(9)), 'Missing signed/quantized transport case')
    quantities = (F(0), F(2)**450, F(2)**450, F(2)**450, F(2)**450, F(2)**-600, F(2)**54, F(2)**55, F(2)**56)
    ledger_ids = set()

    def ledgers(case, cells, axis, inertia, j, dt, amount, arrival, total, volume=F(1)):
        rows = {name: value for (cid, name), value in data['ledgers'].items() if cid == case}
        mass = {key for key in rows if key.startswith('mass:')}
        names = mass | {'momentum_x', 'momentum_y', 'energy_u_k_g'} | ({'passive:2147483648'} if amount else set())
        check(len(mass) == 3 and set(rows) == names, 'Conserved quantity schema differs')
        count_sum = 0
        for name, (count, error, bound) in rows.items():
            ledger_ids.add((case, name)); count_sum += count
            check(count >= 0 and 0 <= error <= bound and bound == count * QUANTUM / 2, 'Counted half-quantum ledger differs')
            if name == ('momentum_x' if axis == 0 else 'momentum_y'):
                expected_count, expected_error = cells, cells * dt * j * j / (inertia * volume)
            elif name == 'energy_u_k_g':
                stock = j * j / (2 * inertia)
                stock_error = abs(stock - F(float(stock))) if stock < NORMAL else F(0)
                expected_count = cells * (1 + int(stock_error != 0))
                expected_error = cells * (stock_error + dt * abs(j)**3 / (2 * inertia**2 * volume))
            elif name == 'passive:2147483648' and amount * dt * abs(j) / (inertia * volume) < NORMAL:
                expected_count, expected_error = 1, abs(amount * dt * abs(j) / (inertia * volume) - arrival)
            else:
                expected_count, expected_error = 0, F(0)
            check(count == expected_count, 'Accepted retained/face product count differs')
            near(error, expected_error, 'Projection error differs from independent physical operands')
        check(count_sum == total, 'Owner/quantity rounding counts disagree')
        if not legacy:
            # Version 3 carries both U writes in their persistent low parts;
            # the three constituent writes remain unresolved. Version 2 used
            # high U only. A zero parcel produces no later primary write.
            bulk_writes = 6 if data['version'] == 3 else 8
            check(data['writes'].get(case) == bulk_writes * cells + int(amount != 0 and arrival != 0), 'Accepted unresolved write count differs')

    for case, (axis, inertia, j, dt, amount, actual, reference, total) in data['cases'].items():
        check(axis == (1 if case in (3, 4) else 0) and j == (-1 if case in (2, 4) else 1) * F(2)**-700 and
              dt == F(.002) and amount == quantities[case] and F(2)**400 < inertia < F(2)**450, 'Scientific inputs differ')
        check(inertia == data['cases'][0][1], 'Inertia changed between signed controls')
        expected = amount * dt * abs(j) / inertia
        if case in (1, 2, 3, 4):
            check(actual == reference and reference > 0, 'Representable reference arrival was erased')
            near(actual, expected, 'Normal arrival differs from exact rational transport')
        else:
            check(actual == F(float(expected)) and reference == 0, 'Quantized arrival differs from nearest-even storage')
        ledgers(case, 6, axis, inertia, j, dt, amount, actual, total)
    cells, workers, total, arrival = data['thread']
    check(cells == 33389 and workers == 12 and arrival == data['cases'][1][5], 'Threaded range control differs')
    axis, inertia, j, dt, amount, _, _, _ = data['cases'][1]
    ledgers(100, cells, axis, inertia, j, dt, amount, arrival, total)
    cells, workers, donor, target, total, arrival = data['cross']
    check((cells, workers, donor, target) == (33389, 12, 777, 970) and arrival == data['thread'][3], 'Cross-block physical transfer differs')
    ledgers(101, cells, 1, inertia, j, dt, amount, arrival, total)
    attempts, rejected, arrival, reference = data['refine']
    check((attempts, rejected) == (2, 1) and arrival == reference and arrival > 0, 'Range restart lost its shared attempt or strict endpoint')
    near(arrival, amount * dt * abs(j) / inertia * F(2)**500, 'Representable final sweep refinement differs')
    if legacy:
        check(not data['shares'] and not data['writes'], 'New records appeared in legacy evidence')
    else:
        check(set(data['shares']) == set(range(4)), 'Missing normal-sweep/share controls')
        for case, (axis, inertia, j, dt, volume, amount, actual, reference, total) in data['shares'].items():
            check(axis == (1 if case == 3 else 0) and j == (-1 if case == 2 else 1) * F(2)**-200 and
                  dt == F(.002) and volume == F(2)**450 and amount == (F(2)**450, F(16), F(32), F(64))[case] and
                  inertia == data['cases'][0][1], 'Share-control inputs differ')
            expected = amount * dt * abs(j) / (inertia * volume)
            if case == 0:
                check(actual == reference and reference > 0, 'Normal sweep/share lost a representable reference transfer')
                near(actual, expected, 'Normal-sweep arrival differs from exact rational transport')
            else:
                check(actual == F(float(expected)) and reference == 0, 'Share quantization differs from nearest-even storage')
            ledgers(200 + case, 6, axis, inertia, j, dt, amount, actual, total, volume)
        check(set(data['writes']) == set(range(9)) | {100, 101, 200, 201, 202, 203}, 'Extra/missing primary-write counters')
    check(set(data['ledgers']) == ledger_ids, 'Extra unaudited quantity records')


def main():
    raw = Path(sys.argv[1]).read_bytes(); check(len(raw) < 1024 * 1024, 'Saved pipe cap')
    legacy = len(sys.argv) == 3 and sys.argv[2] == '--legacy'
    data = parse(raw); review(data, legacy); saved = CHECKS
    mutations = ('arrival', 'quantized', 'count', 'error', 'bound', 'budget', 'threads') + (() if legacy else ('share', 'write', 'missing_share'))
    for kind in mutations:
        bad = copy.deepcopy(data)
        if kind in ('arrival', 'quantized'):
            key = 1 if kind == 'arrival' else 7; row = list(bad['cases'][key]); row[5] = 0; bad['cases'][key] = tuple(row)
        elif kind in ('count', 'error', 'bound'):
            key = (1, 'momentum_x'); row = list(bad['ledgers'][key]); index = {'count': 0, 'error': 1, 'bound': 2}[kind]
            row[index] += 1 if kind == 'count' else QUANTUM; bad['ledgers'][key] = tuple(row)
        elif kind == 'budget':
            row = list(bad['refine']); row[1] = 0; bad['refine'] = tuple(row)
        elif kind == 'share':
            row = list(bad['shares'][0]); row[6] = 0; bad['shares'][0] = tuple(row)
        elif kind == 'write':
            bad['writes'][200] = 0
        elif kind == 'missing_share':
            bad['shares'].clear()
        else:
            row = list(bad['thread']); row[1] = 1; bad['thread'] = tuple(row)
        rejected = False
        try:
            review(bad, legacy)
        except ValueError:
            rejected = True
        check(rejected, 'Corrupted saved evidence accepted: ' + kind)
    print(json.dumps(dict(Passed=True, SavedChecks=saved, CorruptionsRejected=len(mutations),
                         SavedSha256=hashlib.sha256(raw).hexdigest(), NativeProcessLaunched=False,
                         Scope='Flagged sweeps and dual products; exact operands; no active-cell performance acceptance')))


if __name__ == '__main__':
    main()
