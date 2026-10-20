#!/usr/bin/env python3
"""Review saved retained/gather operands without launching native code."""
import copy
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

EPS = F(1, 1 << 52)
CHECKS = 0


def check(value, why):
    global CHECKS
    CHECKS += 1
    if not value:
        raise ValueError(why)


def number(text):
    value = float(text)
    check(math.isfinite(value), "Nonfinite saved operand")
    return F(value)


def parse(raw):
    result = dict(initial={}, final={}, adds=[], quantities={}, total=None, contract=None, passed=False)
    for line in raw.decode("ascii").splitlines():
        row = line.split()
        check(bool(row), "Empty record")
        tag = row[0]
        if tag == "GCONTRACT":
            check(len(row) == 2 and result['contract'] is None, "Contract shape/duplicate")
            result['contract'] = int(row[1])
        elif tag in ("GINIT", "GEND"):
            check(len(row) == 4, "Cell record shape")
            target = result['initial' if tag == 'GINIT' else 'final']
            key = (row[1], int(row[2]))
            check(key not in target, "Duplicate cell")
            target[key] = number(row[3])
        elif tag == "GADD":
            check(len(row) == 7, "Addition record shape")
            result['adds'].append((int(row[1]), row[2], *map(number, row[3:6]), int(row[6])))
        elif tag == "GQUANTITY":
            check(len(row) == 7 and row[1] not in result['quantities'], "Quantity shape/duplicate")
            result['quantities'][row[1]] = (int(row[2]), *map(number, row[3:]))
        elif tag == "GTOTAL":
            check(len(row) == 6 and result['total'] is None, "Total shape/duplicate")
            result['total'] = tuple(map(int, row[1:]))
        elif tag == "GPASS":
            check(len(row) == 1 and not result['passed'], "Pass shape/duplicate")
            result['passed'] = True
        else:
            raise ValueError("Unknown saved record: " + tag)
    return result


def review(data):
    check(data['contract'] == 128 and data['passed'], "Missing unchanged contract/pass")
    check(set(data['initial']) == set(data['final']), "Endpoint shapes differ")
    names = {name for name, _ in data['initial']}
    check(len(names) == 5 and len(data['initial']) == 40, "Fixture shape changed")
    check(set(data['quantities']) == names | {'momentum_x', 'momentum_y', 'energy_u_k_g'}, "Quantity coverage")
    counts = dict.fromkeys(names, 0)
    errors = dict.fromkeys(names, F(0))
    halves = dict.fromkeys(names, F(0))
    positions = dict.fromkeys(names, 0)
    previous = {}
    dt = F(1, 1024)
    check(len(data['adds']) in (80, 160), "Missing/extra storage writes")
    per_cell = len(data['adds']) // 40  # Historical gathers have two; full transport has four.
    for expected_id, (identity, name, before, amount, stored, flag) in enumerate(data['adds']):
        check(identity == expected_id and name in names, "Addition ordering/identity")
        check(min(before, stored) >= 0, "Negative stored inventory")
        cell, side = divmod(positions[name], per_cell)
        check(cell < 8, "Extra cell storage writes")
        x, y = cell % 4, cell // 4
        initial = data['initial'][name, cell]
        if per_cell == 4 and side < 2:
            expected_before = initial if side == 0 else previous[name]
            expected_amount = -F(float(initial * (dt if side == 0 else dt / 2)))
        else:
            incoming = side - (2 if per_cell == 4 else 0)
            if incoming == 0:
                kept = F(float(initial - F(float(initial * dt))))
                expected_before = previous[name] if per_cell == 4 else F(float(kept - F(float(initial * dt / 2))))
                neighbor, share = (3 if x == 0 else x - 1) + 4 * y, dt
            else:
                expected_before = previous[name]
                neighbor, share = x + (4 if y == 0 else 0), dt / 2
            expected_amount = F(float(data['initial'][name, neighbor] * share))
        check(before == expected_before and amount == expected_amount, "Gather differs from the shared donor-cell geometry")
        exact = before + amount
        check(F(float(exact)) == stored, "Addition is not nearest-even binary64")
        error = abs(exact - stored)
        check(flag == int(error != 0), "Inexact flag differs from exact fractions")
        if error:
            half = F(math.ulp(float(stored))) / 2
            check(error <= half, "Storage error exceeds half an ulp")
            counts[name] += 1
            errors[name] += error
            halves[name] += half
        if side == per_cell - 1:
            check(stored == data['final'][name, cell], "Gather differs from its stored endpoint")
        previous[name] = stored
        positions[name] += 1
    check(len(data['adds']) == 40 * per_cell, "Missing/extra writes")
    for name, (count, charge, bound, stage, tiny) in data['quantities'].items():
        check(count == counts.get(name, 0), "Published inventory count differs")
        check(0 <= charge <= bound and stage >= 0 and tiny >= 0, "Charge outside interval budget")
        if name not in names:
            check(count == 0, "Gather counted momentum or primary U")
            continue
        check({i for q, i in data['initial'] if q == name} == set(range(8)), "Cell coverage")
        check(positions[name] == 8 * per_cell, "Missing cell writes")
        check(errors[name] <= halves[name] <= charge, "Error/half-ulp charge understated")
        check(charge <= halves[name] * (1 + 16 * EPS) + F(1, 1 << 1074), "Unjustified charge enlargement")
        initial = [data['initial'][name, i] for i in range(8)]
        final = [data['final'][name, i] for i in range(8)]
        check(min(initial + final) >= 0, "Negative endpoint")
        # Two positive incoming faces: the gather's absolute operand magnitude
        # equals its final value. The published stage enters as a cancelling pair.
        exact_stage = 2 * sum(final)
        check(abs(stage - exact_stage) <= 16 * EPS * exact_stage, "Stage magnitude differs from saved gather")
        scale = sum(map(abs, initial + final)) + 2 * stage
        exact_bound = 128 * EPS * scale + tiny
        check(abs(bound - exact_bound) <= 8 * EPS * exact_bound, "The 128-epsilon interval bound changed")
        check(abs(sum(final) - sum(initial)) <= bound, "Endpoint inventory account failed")
    check(data['total'] == (len(data['adds']), sum(counts.values()), 1, 1, 1), "Aggregate/cache/rollback controls missing")
    check(sum(counts.values()) > 0, "Empty rounding coverage")


def main():
    raw = Path(sys.argv[1]).read_bytes()
    check(len(raw) < 1024 * 1024, "Capture cap")
    data = parse(raw)
    review(data)
    saved_checks = CHECKS
    mutations = ('before', 'incoming', 'stored', 'flag', 'count', 'charge', 'bound', 'stage', 'missing', 'duplicate', 'initial', 'final', 'total', 'contract')
    for mutation in mutations:
        bad = copy.deepcopy(data)
        name = next(q for q, n in bad['quantities'].items() if n[0] > 0)
        if mutation in ('before', 'incoming', 'stored', 'flag'):
            row = list(bad['adds'][0])
            if mutation != 'flag':
                index = {'before': 2, 'incoming': 3, 'stored': 4}[mutation]
                row[index] = F(math.nextafter(float(row[index]), math.inf))
            else:
                row[5] = 1 - row[5]
            bad['adds'][0] = tuple(row)
        elif mutation in ('count', 'charge', 'bound', 'stage'):
            row = list(bad['quantities'][name])
            index = {'count': 0, 'charge': 1, 'bound': 2, 'stage': 3}[mutation]
            row[index] = row[index] + 1 if mutation == 'count' else 0 if mutation == 'charge' else row[index] * 2
            bad['quantities'][name] = tuple(row)
        elif mutation == 'missing':
            bad['adds'].pop()
        elif mutation == 'duplicate':
            bad['adds'].append(bad['adds'][0])
        elif mutation in ('initial', 'final'):
            key = next(iter(bad[mutation]))
            bad[mutation][key] *= 2
        elif mutation == 'total':
            bad['total'] = (80, 0, 1, 1, 1)
        else:
            bad['contract'] = 129
        rejected = False
        try:
            review(bad)
        except ValueError:
            rejected = True
        check(rejected, 'Altered capture admitted: ' + mutation)
    print(json.dumps(dict(Passed=True, SavedChecks=saved_checks, CorruptionsRejected=len(mutations), SavedSha256=hashlib.sha256(raw).hexdigest(), NativeProcessLaunched=False)))


if __name__ == '__main__':
    main()
