"""Review saved interval endpoints; never builds or launches the native test."""
import copy
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

EPS = F(1, 2**52)
R = F(float('8.31446261815324'))
TREF = F(float('298.15'))
CHECKS = 0


def check(ok, why):
    global CHECKS
    CHECKS += 1
    if not ok:
        raise ValueError(why)


def number(token):
    value = float(token)
    check(math.isfinite(value), 'Nonfinite saved operand')
    return F(value)


def dec(value):
    return D(value.numerator) / D(value.denominator)


def unique(target, key, value):
    check(key not in target, 'Duplicate record')
    target[key] = value


def parse(raw):
    data = {k: {} for k in ('cases', 'models', 'cells', 'mass', 'passive', 'faces', 'comparisons', 'controls')}
    for line in raw.decode('ascii').splitlines():
        row = line.split(); check(bool(row), 'Empty record'); kind = row[0]
        if kind == 'ICASE':
            check(len(row) == 16, 'Interval case shape')
            unique(data['cases'], int(row[1]), (tuple(map(int, row[2:6])), *map(number, row[6:])))
        elif kind == 'IGAS':
            check(len(row) == 7, 'Model shape')
            unique(data['models'], tuple(map(int, row[1:3])), (int(row[3]), *map(number, row[4:])))
        elif kind == 'ICELL':
            check(len(row) == 8, 'Cell shape')
            unique(data['cells'], tuple(map(int, row[1:4])), (int(row[4]), *map(number, row[5:])))
        elif kind in ('IMASS', 'IPASSIVE', 'IFACE'):
            check(len(row) == 6, 'Inventory/face shape')
            table = {'IMASS': 'mass', 'IPASSIVE': 'passive', 'IFACE': 'faces'}[kind]
            unique(data[table], tuple(map(int, row[1:5])), number(row[5]))
        elif kind == 'ICOMPARISON':
            check(len(row) == 4, 'Comparison shape')
            unique(data['comparisons'], int(row[1]), (number(row[2]), int(row[3])))
        elif kind in ('IAUDITS', 'IFALLBACK', 'IENDURANCE', 'ISAVEGUARDS', 'IEOSGUARD', 'IPOSTFALLBACK'):
            check(len(row) == 3, 'Control shape'); unique(data['controls'], kind, tuple(map(int, row[1:])))
        elif kind == 'PASS':
            unique(data['controls'], 'PASS', True)
        else:
            raise ValueError('Unknown record: ' + kind)
    return data


def review(data):
    check(set(data['cases']) == set(range(5)), 'Missing interval/endurance/catalogue case')
    for case, endpoint, i in data['cells']:
        check(case in data['cases'] and endpoint in (0, 1) and 0 <= i < data['cases'][case][0][0] * data['cases'][case][0][1],
              'Out-of-domain cell record')
    for table in ('mass', 'passive'):
        for case, endpoint, i, identity in data[table]:
            check((case, endpoint, i) in data['cells'] and identity >= 0, 'Out-of-domain inventory record')
            if table == 'mass': check((case, identity) in data['models'], 'Unmodelled saved constituent')
            else: check(0 < identity < 2**64, 'Passive identity outside its wire range')
    for case, endpoint, axis, face in data['faces']:
        check(case in data['cases'] and endpoint in (0, 1) and axis in (0, 1) and face >= 0, 'Out-of-domain face record')
    for case, species in data['models']:
        check(case in data['cases'] and species >= 0, 'Out-of-domain model record')
    check(set(data['comparisons']) == {0, 1, 2}, 'Missing reference comparison')
    for error, continuation in data['comparisons'].values():
        check(0 <= error < F(1, 10**8) and continuation == 1, 'Reference/continuation native control failed')
    controls = data['controls']
    check(controls['IAUDITS'][0] == 518 and controls['IAUDITS'][1] > 0, 'Exact audit escalation not exercised')
    check(controls['IFALLBACK'] == (4, 1) and controls['IENDURANCE'] == (10000, 1) and controls['PASS'], 'Missing fallback/rollback/endurance control')
    check(controls['ISAVEGUARDS'] == (3, 1), 'Missing version-4 setting admission controls')
    check(controls['IEOSGUARD'] == (1, 1), 'Missing material temperature boundary refinement')
    check(controls['IPOSTFALLBACK'] == (2, 1), 'Missing shared budget and atomic mid-step fallback control')
    for case, values in data['cases'].items():
        (w, h, px, py), volume, steps, rejected, kernel, residual, bound, ds, ds_bound, wx, wy = values
        check(w > 0 and h > 0 and px in (0, 1) and py in (0, 1) and volume > 0 and kernel == 1, 'Invalid case topology/kernel')
        check(steps == (10000 if case == 3 else 5) and rejected == 0, 'Unexpected interval steps/rejections')
        n = w * h
        check({key for key in data['cells'] if key[0] == case} == {(case, e, i) for e in (0, 1) for i in range(n)}, 'Incomplete cells')
        all_masses, all_passive, momentum = [{}, {}], [{}, {}], [[], []]
        energies, entropies, entropy_scale = [], [], D(0)
        cell_mass = [{}, {}]
        for endpoint in (0, 1):
            for i in range(n):
                wall, u, t, p = data['cells'][case, endpoint, i]
                check(wall in (0, 1) and wall == data['cells'][case, 0, i][0], 'Changed wall')
                masses = {k[3]: v for k, v in data['mass'].items() if k[:3] == (case, endpoint, i)}
                passive = {k[3]: v for k, v in data['passive'].items() if k[:3] == (case, endpoint, i)}
                for species, amount in masses.items():
                    check(amount > 0, 'Nonpositive saved constituent')
                    all_masses[endpoint][species] = all_masses[endpoint].get(species, F(0)) + amount
                for identity, amount in passive.items():
                    check(amount > 0, 'Nonpositive saved passive')
                    all_passive[endpoint][identity] = all_passive[endpoint].get(identity, F(0)) + amount
                cell_mass[endpoint][i] = sum(masses.values(), F(0))
                if wall:
                    check(u == t == p == 0 and not masses and not passive, 'Wall contains fluid')
                    continue
                check(masses and t > 0 and p > 0, 'Occupied gas lost inventory')
                capacity, offset, moles = F(0), F(0), F(0)
                for species, amount in masses.items():
                    phase, molar, cv, e0 = data['models'][case, species]
                    check(phase == 0 and molar > 0 and cv > 0, 'Non-gas/unmodelled constituent')
                    capacity += amount * cv; offset += amount * e0; moles += amount / molar
                exact_t = (u - offset) / capacity
                check(exact_t > 0 and abs(exact_t - t) <= 32 * EPS * exact_t, 'Saved EOS temperature differs')
                exact_p = moles * R * exact_t / volume
                check(abs(exact_p - p) <= 32 * EPS * exact_p, 'Saved EOS pressure differs')
                entropy = dec(capacity) * (dec(exact_t) / dec(TREF)).ln()
                for species, amount in masses.items():
                    molar = data['models'][case, species][1]; ni = amount / molar
                    entropy += dec(ni * R) * (dec(volume) / dec(ni)).ln()
                sign = D(-1 if endpoint == 0 else 1)
                entropies.append(sign * entropy); entropy_scale += abs(dec(u)) / dec(exact_t)
                energies.append((-1 if endpoint == 0 else 1) * u)
        for table in (all_masses, all_passive):
            check(set(table[0]) == set(table[1]), 'Lost constituent/passive identity')
            for identity, amount in table[0].items():
                check(abs(amount - table[1][identity]) <= 128 * EPS * (amount + table[1][identity]), 'Closed constituent/passive drift')
        # Both hazardous traces are real accounts, with no absolute allowance.
        check(any(0 < q < F(1, 10**20) for q in all_masses[0].values()), 'Missing constituent trace')
        check(any(0 < q < F(1, 10**20) for q in all_passive[0].values()), 'Missing passive trace')
        for endpoint in (0, 1):
            mass = cell_mass[endpoint]
            for axis in (0, 1):
                fw, fh = (w + (0 if px else 1), h) if axis == 0 else (w, h + (0 if py else 1))
                expected = {(case, endpoint, axis, f) for f in range(fw * fh)}
                check({k for k in data['faces'] if k[:3] == (case, endpoint, axis)} == expected, 'Incomplete face coverage')
                for f in range(fw * fh):
                    x, y = f % fw, f // fw; left = right = None
                    if axis == 0:
                        if x > 0 or px: left = (x + w - 1) % w + y * w
                        if x < w: right = x + y * w
                    else:
                        if y > 0 or py: left = x + ((y + h - 1) % h) * w
                        if y < h: right = x + y * w
                    if left is not None and data['cells'][case, endpoint, left][0]: left = None
                    if right is not None and data['cells'][case, endpoint, right][0]: right = None
                    j = data['faces'][case, endpoint, axis, f]
                    inertia = (mass.get(left, F(0)) + mass.get(right, F(0))) / 2
                    if left is None or right is None: check(j == 0, 'Fixed face has momentum')
                    if inertia == 0: check(j == 0, 'Massless face has momentum')
                    elif j != 0: energies.append((-1 if endpoint == 0 else 1) * j * j / (2 * inertia))
                    momentum[axis].append((-1 if endpoint == 0 else 1) * j)
        energy_residual, energy_scale = sum(energies, F(0)), sum(map(abs, energies), F(0))
        check(abs(energy_residual) <= 128 * EPS * energy_scale, 'Actual stored U+K drift')
        check(abs(residual - energy_residual) <= 4 * EPS * energy_scale and abs(residual) <= bound, 'Reported energy residual differs')
        check(abs(bound - 128 * EPS * energy_scale) <= 8 * EPS * (128 * EPS * energy_scale), 'Reported energy bound differs')
        for terms, wall in zip(momentum, (wx, wy)):
            terms = terms + [-wall]
            check(abs(sum(terms, F(0))) <= 128 * EPS * sum(map(abs, terms), F(0)), 'Momentum/wall drift')
        actual_ds = sum(entropies, D(0)); actual_bound = dec(128 * EPS) * entropy_scale
        check(actual_ds >= -actual_bound, 'Actual stored-state entropy decreased')
        check(abs(dec(ds) - actual_ds) <= actual_bound, 'Reported relative entropy differs')
        check(abs(dec(ds_bound) - actual_bound) <= dec(32 * EPS) * actual_bound, 'Reported entropy allowance differs')


def main():
    raw = Path(sys.argv[1]).read_bytes(); check(len(raw) < 2 * 1024 * 1024, 'Saved pipe cap')
    with localcontext() as context:
        context.prec = 100; data = parse(raw); review(data); saved = CHECKS
        for kind in ('energy', 'momentum', 'trace', 'pressure', 'continuation'):
            bad = copy.deepcopy(data)
            if kind == 'energy':
                row = list(bad['cells'][0, 1, 0]); row[1] += 1; bad['cells'][0, 1, 0] = tuple(row)
            elif kind == 'momentum': bad['faces'][0, 1, 0, 0] += 1
            elif kind == 'trace': del bad['mass'][next(k for k, v in bad['mass'].items() if k[:2] == (0, 1) and v < F(1, 10**20))]
            elif kind == 'pressure':
                row = list(bad['cells'][1, 1, 0]); row[3] *= 2; bad['cells'][1, 1, 0] = tuple(row)
            else: bad['comparisons'][0] = (F(0), 0)
            rejected = False
            try: review(bad)
            except ValueError: rejected = True
            check(rejected, 'Corrupted interval evidence accepted: ' + kind)
    print(json.dumps(dict(Passed=True, SavedChecks=saved, CorruptionsRejected=5, SavedSha256=hashlib.sha256(raw).hexdigest(),
        NativeProcessLaunched=False, Scope='Selected donor-cell gas interval port; no default strong-flow, free-surface or colony acceptance')))


if __name__ == '__main__':
    main()
