"""Saved-only empty-cell accounting. This does not certify front accuracy."""
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
EPS = base.EPS


def full(row, index):
    return base.scaled_energy(row[index], row[index + 1])


def parse(raw):
    check(0 < len(raw) <= 2097152, 'Vacuum stream bound')
    data = dict(materials={}, grids={}, cells={}, masses={}, passives={}, faces={}, results={}, empty=None, retry=None, depletion=None, complete=None)
    for line in raw.decode('ascii').splitlines():
        row = line.split()
        check(bool(row), 'Empty record')
        tag = row[0]
        if tag == 'VMATERIAL':
            check(len(row) == 8, 'Material shape')
            unique(data['materials'], int(row[1]), (int(row[2]), *map(number, row[3:])))
        elif tag == 'VGRID':
            check(len(row) == 10, 'Grid shape')
            unique(data['grids'], int(row[1]), (*map(int, row[2:9]), number(row[9])))
        elif tag == 'VCELL':
            check(len(row) == 8, 'Cell shape')
            unique(data['cells'], tuple(map(int, row[1:4])), tuple(map(number, row[4:])))
        elif tag in ('VMASS', 'VPASSIVE'):
            check(len(row) == 6, 'Inventory shape')
            unique(data['masses' if tag == 'VMASS' else 'passives'], tuple(map(int, row[1:5])), number(row[5]))
        elif tag == 'VFACE':
            check(len(row) == 7, 'Face shape')
            unique(data['faces'], tuple(map(int, row[1:5])), full(row, 5))
        elif tag == 'VRESULT':
            check(len(row) == 18, 'Result shape')
            unique(data['results'], int(row[1]), (*map(int, row[2:6]), *[full(row, i) for i in range(6, 16, 2)], *map(number, row[16:])))
        elif tag == 'VEMPTY':
            check(data['empty'] is None and row[1:] == ['2', '4', '1', '1'], 'Empty/invalid fixture coverage')
            data['empty'] = tuple(map(int, row[1:]))
        elif tag == 'VRETRY':
            check(len(row) == 3 and data['retry'] is None and int(row[1]) > 0 and row[2] == '1', 'Vacuum CFL/atomicity receipt')
            data['retry'] = tuple(map(int, row[1:]))
        elif tag == 'VDEPLETION':
            check(data['depletion'] is None and row[1:] == ['1', '6'], 'Complete exact-translation/depletion control')
            data['depletion'] = (1, 6)
        elif tag == 'VCOMPLETE':
            check(len(row) == 2 and data['complete'] is None, 'Completion shape')
            data['complete'] = int(row[1])
        else:
            raise ValueError('Unknown vacuum record: ' + tag)
    check(data['complete'] == 12 and data['empty'] == (2, 4, 1, 1) and data['retry'] is not None and data['depletion'] == (1, 6), 'Fixed completion coverage')
    check(set(data['grids']) == set(data['results']) == set(range(12)) and len(data['materials']) == 4, 'Case/material coverage')
    return data


def review(data):
    occupied = vacant = 0
    expected_cells, expected_faces = set(), set()
    for case, (w, h, px, py, axis, direction, mode, gravity) in data['grids'].items():
        check((axis, direction, mode) == (case // 6, -1 if case % 6 < 3 else 1, case % 3), 'Fixed case identity')
        check((w, h, px, py) == ((12, 2, 0, 1) if axis == 0 else (2, 12, 1, 0)), 'Fixed grid identity')
        check(gravity == (F(9.81) if mode == 2 and axis == 1 else 0), 'Fixed gravity')
        mass, energies, entropy, allowance = {}, {}, [D(0), D(0)], [D(0), D(0)]
        for endpoint in (0, 1):
            for cell in range(w * h):
                key = (case, endpoint, cell)
                expected_cells.add(key)
                energy, pressure, temperature, saved_s = data['cells'][key]
                amounts = {s: value for (c, e, i, s), value in data['masses'].items() if (c, e, i) == key}
                passives = {s: value for (c, e, i, s), value in data['passives'].items() if (c, e, i) == key}
                check(all(value > 0 for value in amounts.values()) and all(value > 0 for value in passives.values()), 'Nonpositive sparse inventory')
                mass[endpoint, cell] = sum(amounts.values(), F(0))
                energies[key] = energy
                coordinate = cell % w if axis == 0 else cell // w
                if endpoint == 0:
                    check(bool(amounts) == (coordinate < 3 if direction > 0 else coordinate >= 9), 'Initial exact vacuum geometry')
                if not amounts:
                    check(not passives and (energy, pressure, temperature, saved_s) == (0, 0, 0, 0), 'Exact vacuum has an inventory or EOS field')
                    vacant += 1
                    continue
                check(len(amounts) == 3 and min(amounts.values()) < F(1, 10**25), 'Bulk gases/helium trace coverage')
                check(set(passives) == {17, 2**31} and passives[2**31] < F(1, 10**25), 'Positive passive/trace coverage')
                p, t, _, s = closed.equilibrium(amounts, data['materials'], energy, F(1))
                check(p > 0 and D(1) <= t <= D(1000000), 'Occupied EOS validity')
                for actual, expected in ((pressure, p), (temperature, t)):
                    check(abs(closed.dec(actual) - expected) <= D('5e-10') * abs(expected), 'Independent mixture EOS')
                bound = closed.dec(128 * EPS * abs(energy)) / t
                check(abs(closed.dec(saved_s) - s) <= bound, 'Independent mixture entropy')
                entropy[endpoint] += s
                allowance[endpoint] += bound
                if endpoint == 0:
                    near(pressure, F(100000), abs(pressure) + 100000, 'Initial gas pressure')
                    near(temperature, F(300), abs(temperature) + 300, 'Initial gas temperature')
                occupied += 1
        for field in ('masses', 'passives'):
            identities = {s for c, e, i, s in data[field] if c == case}
            for identity in identities:
                terms = [data[field].get((case, e, i, identity), F(0)) * (1 if e else -1) for e in (0, 1) for i in range(w * h)]
                near(sum(terms), 0, sum(map(abs, terms)), 'Original constituent/passive account')
        arrived = sum(mass[0, i] == 0 and mass[1, i] > 0 for i in range(w * h))
        remaining = sum(mass[1, i] == 0 for i in range(w * h))
        report = data['results'][case]
        check(report[:4] == (arrived, remaining, 2, 0) and arrived > 0 and remaining > 0, 'Actual arrival/vacuum/nominal-step receipts')
        energy_terms = [energies[case, e, i] * (1 if e else -1) for e in (0, 1) for i in range(w * h)]
        for e in (0, 1):
            for i in range(w * h):
                energy_terms.append((1 if e else -1) * mass[e, i] * gravity * (F(i // w) + F(1, 2)))
        for component in (0, 1):
            width, height = (w if px else w + 1, h) if component == 0 else (w, h if py else h + 1)
            momenta = []
            for e in (0, 1):
                for face in range(width * height):
                    key = (case, e, component, face)
                    expected_faces.add(key)
                    j = data['faces'][key]
                    x, y = face % width, face // width
                    if component == 0:
                        left = (x + w - 1) % w + y * w if x > 0 or px else None
                        right = x + y * w if x < w else None
                    else:
                        left = x + ((y + h - 1) % h) * w if y > 0 or py else None
                        right = x + y * w if y < h else None
                    halves = [0. if i is None else .5 * float(mass[e, i]) for i in (left, right)]
                    inertia = F(halves[0] + halves[1])
                    if inertia == 0 or left is None or right is None:
                        check(j == 0, 'Massless/fixed face carries momentum')
                    if inertia:
                        energy_terms.append((1 if e else -1) * j * j / (2 * inertia))
                    momenta.append((1 if e else -1) * j)
            momenta.append(-report[4 + component])
            if component == 1:
                momenta.append(-report[6])
            near(sum(momenta), 0, sum(map(abs, momenta)), 'Original momentum/wall/body account')
        residual = sum(energy_terms)
        bound = 128 * EPS * sum(map(abs, energy_terms))
        check(abs(residual) <= bound, 'Original closed U+K+G operand bound')
        near(report[7], residual, sum(map(abs, energy_terms)), 'Actual interval energy residual')
        check(report[8] >= abs(report[7]), 'Reported composed energy bound')
        difference = entropy[1] - entropy[0]
        allowed = sum(allowance)
        check(difference >= -allowed, 'Original entropy inequality')
        check(abs(closed.dec(report[9]) - difference) <= allowed, 'Actual endpoint entropy receipt')
        near(report[10], F(allowed), abs(report[10]) + F(allowed), 'Original entropy allowance')
    check(set(data['cells']) == expected_cells and set(data['faces']) == expected_faces, 'Exact cell/face coverage')
    check(all(key[:3] in expected_cells for key in (*data['masses'], *data['passives'])), 'Sparse record outside grid')
    return dict(Passed=True, Cases=12, OccupiedReadings=occupied, ExactVacuumReadings=vacant,
                FrontAccuracyAssessed=False, NativeProcessLaunched=False,
                Scope='Two-step closed gas/empty-cell inventory, EOS, entropy and impulse controls; no vacuum-front, liquid-interface, colony or performance acceptance.')


def main():
    raw = Path(sys.argv[1]).read_bytes()
    data = parse(raw)
    with localcontext() as context:
        context.prec = 100
        result = review(data)
        checks = base.CHECKS
        rejected = []
        for kind in ('vacuum-energy', 'trace-mass', 'massless-momentum', 'wall-receipt', 'missing-cell'):
            bad = copy.deepcopy(data)
            if kind == 'vacuum-energy':
                bad['cells'][0, 0, 0] = (F(1, 10**100), 0, 0, 0)
            elif kind == 'trace-mass':
                key = next(k for k, value in bad['masses'].items() if k[1] == 1 and value < F(1, 10**25))
                bad['masses'][key] *= F(101, 100)
            elif kind == 'massless-momentum':
                bad['faces'][0, 0, 0, 1] = F(2)**-1200
            elif kind == 'wall-receipt':
                row = list(bad['results'][0]); row[4] += 1; bad['results'][0] = tuple(row)
            else:
                del bad['cells'][0, 1, 0]
            try:
                review(bad)
            except (ValueError, KeyError, AssertionError):
                rejected.append(kind)
            else:
                raise ValueError('Corrupted vacuum capture accepted: ' + kind)
    result.update(SavedChecks=checks, CorruptionsRejected=rejected, InputSha256=hashlib.sha256(raw).hexdigest(),
                  AnalysisSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    with Path(sys.argv[2]).open('x') as output:
        json.dump(result, output, indent=2); output.write('\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
