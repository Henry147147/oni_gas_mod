"""Exact-rational review of saved physical mutation operands; launches no native code."""
import hashlib
import json
import math
from fractions import Fraction as F
from pathlib import Path
import sys

EPS = F(1, 1 << 52)


def check(value, reason):
    if not value:
        raise ValueError(reason)


def number(token):
    value = float(token)
    check(math.isfinite(value), 'Nonfinite exported operand')
    return F(value)


def near(actual, expected, scale, tolerance=128 * EPS):
    check(abs(actual - expected) <= tolerance * scale, 'Independent numerical value differs')


def review(data):
    species, names, cases, cells, masses, passives, edits, mass_deltas, passive_deltas = {}, {}, {}, {}, {}, {}, {}, {}, {}
    groups = 0
    for line in data.decode('utf-8-sig').splitlines():
        p = line.split()
        check(p, 'Empty record')
        tag = p.pop(0)
        if tag == 'PASS':
            groups += 1
            continue
        if tag == 'SPECIES':
            key = int(p.pop(0)); name = p.pop(0); check(key not in species and len(p) == 5 and name not in names, 'Species record shape')
            names[name] = key
            species[key] = tuple(map(number, p))
        elif tag == 'CASE':
            key = int(p.pop(0)); check(key not in cases and len(p) == 12, 'Case record shape')
            cases[key] = tuple(map(number, p))
        elif tag == 'EDIT':
            check(len(p) == 2, 'Edit record shape'); edits.setdefault(int(p[0]), []).append(int(p[1]))
        elif tag == 'CELL':
            key = tuple(map(int, p[:3])); check(key not in cells and len(p) == 11, 'Cell record shape')
            cells[key] = tuple(map(number, p[3:]))
        elif tag in ('MASS', 'PASSIVE'):
            key = tuple(map(int, p[:4])); target = masses if tag == 'MASS' else passives
            check(key not in target and len(p) == 5, 'Constituent/metadata record shape')
            value = number(p[4]); check(value > 0, 'Nonpositive exported constituent/metadata')
            target[key] = value
        elif tag in ('MASS_DELTA', 'PASSIVE_DELTA'):
            key = tuple(map(int, p[:2])); target = mass_deltas if tag == 'MASS_DELTA' else passive_deltas
            check(key not in target and len(p) == 3, 'Declared delta record shape'); target[key] = number(p[2])
        else:
            raise ValueError('Unknown exported record ' + tag)
    check(groups == 7 and set(cases) == {0, 1, 2, 3}, 'Incomplete physical mutation suite')
    check(edits == {0: [0, 2], 1: [0, 1], 2: [2], 3: [0]}, 'Changed case geometry')
    check(set(species) == set(range(len(species))) and len(species) > 4, 'Missing species definitions')
    check(set(cells) == {(c, e, i) for c in cases for e in (0, 1) for i in range(4)}, 'Missing complete cells')
    identities = {key[3] for key in passives} | {key[1] for key in passive_deltas}
    checks = 0
    for (case, endpoint, cell), values in cells.items():
        u, px, py, mass, kinetic, potential, temperature, pressure = values
        row = {s: masses.get((case, endpoint, cell, s), F(0)) for s in species}
        near(mass, sum(row.values()), sum(row.values()))
        near(kinetic, (px * px + py * py) / (2 * mass), abs(kinetic))
        near(potential, mass * F(float(9.81)) * F(2 * (cell // 2) + 1, 2), abs(potential))
        capacity = sum(m * species[s][1] for s, m in row.items())
        offset = sum(m * species[s][2] for s, m in row.items())
        near(temperature, (u - offset) / capacity, temperature, F('2e-10'))
        liquid = sum(m / species[s][3] for s, m in row.items() if m and species[s][4] == 1)
        n = sum(m / species[s][0] for s, m in row.items() if species[s][4] == 0)
        check(0 <= liquid < 1 and n > 0 and temperature > 0, 'Unsupported exported phase state')
        near(pressure, n * F(float(8.31446261815324)) * temperature / (1 - liquid), pressure, F('2e-10'))
        checks += 6
    max_energy_ratio = F(0)
    for case, budget in cases.items():
        material, work, heat, px_budget, py_budget, energy_residual, bound, px_residual, py_residual, du, dk, dpe = budget
        for i in range(4):
            if i not in edits[case]:
                check(cells[case, 0, i] == cells[case, 1, i], 'Unedited physical row changed')
                for source in (masses, passives):
                    a = {key[3]: value for key, value in source.items() if key[:3] == (case, 0, i)}
                    b = {key[3]: value for key, value in source.items() if key[:3] == (case, 1, i)}
                    check(a == b, 'Unedited constituent/metadata changed')
        for s in species:
            old = [masses.get((case, 0, i, s), F(0)) for i in edits[case]]
            new = [masses.get((case, 1, i, s), F(0)) for i in edits[case]]
            declared = mass_deltas.get((case, s), F(0)); actual = sum(new) - sum(old)
            near(actual, declared, sum(old) + sum(new) + abs(declared))
            if declared:
                check(actual * declared > 0, 'Erased/reversed material account')
            checks += 1
        for identity in identities:
            old = [passives.get((case, 0, i, identity), F(0)) for i in edits[case]]
            new = [passives.get((case, 1, i, identity), F(0)) for i in edits[case]]
            declared = passive_deltas.get((case, identity), F(0)); actual = sum(new) - sum(old)
            near(actual, declared, sum(old) + sum(new) + abs(declared))
            if declared:
                check(actual * declared > 0, 'Erased/reversed metadata account')
            checks += 1
        components = [sum(cells[case, 1, i][column] - cells[case, 0, i][column] for i in edits[case]) for column in (0, 4, 5)]
        for actual, reported in zip(components, (du, dk, dpe)):
            near(reported, actual, abs(actual))
        actual_energy = sum(components) - material - work - heat
        scale = sum(abs(cells[case, e, i][column]) for e in (0, 1) for i in edits[case] for column in (0, 4, 5)) + abs(material) + abs(work) + abs(heat)
        near(bound, 128 * EPS * scale, 128 * EPS * scale)
        check(abs(actual_energy) <= bound, 'Physical total energy is unbalanced')
        near(energy_residual, actual_energy, abs(actual_energy))
        max_energy_ratio = max(max_energy_ratio, abs(actual_energy) / bound)
        for column, declared, reported in ((1, px_budget, px_residual), (2, py_budget, py_residual)):
            old = [cells[case, 0, i][column] for i in edits[case]]; new = [cells[case, 1, i][column] for i in edits[case]]
            residual = sum(new) - sum(old) - declared
            near(residual, F(0), sum(map(abs, old + new)) + abs(declared))
            near(reported, residual, abs(residual))
        checks += 9
    check(cells[0, 1, 0][1] == F(float.fromhex('0x1.999999999999cp-2')), 'Primary momentum counterexample changed')
    check(passives[1, 1, 1, 0x80000000] == F(float(1e-30)) / 8, 'Positive metadata trace did not move')
    check(all(key[1] == 0 for key in passives if key[0] == 3), 'Explicit metadata removal is incomplete')
    check(mass_deltas[2, names['oxygen']] == F(1, 8), 'Declared oxygen supply changed')
    return {'Passed': True, 'Cases': len(cases), 'NativeGroups': groups, 'IndependentChecks': checks,
            'MaximumEnergyBoundRatio': float(max_energy_ratio), 'NativeExecution': False,
            'EntropyProcessOrPowerValidation': False, 'ProductionOwnerAccepted': False}


def main():
    data = Path(sys.argv[1]).read_bytes(); result = review(data)
    # A positive trace deletion and a false heat declaration must each fail the
    # independent saved-data check; no native invocation is repeated.
    text = data.decode('utf-8-sig'); rows = text.splitlines(); mutations = []
    for index, line in enumerate(rows):
        if line.startswith('PASSIVE 1 1 1 2147483648 '):
            changed = rows.copy(); changed[index] = line.rsplit(' ', 1)[0] + ' 1e-200'; mutations.append('\n'.join(changed).encode())
        if line.startswith('CASE 0 '):
            changed = rows.copy(); values = line.split(); values[4] = '0'; changed[index] = ' '.join(values); mutations.append('\n'.join(changed).encode())
    check(len(mutations) == 2, 'Missing corruption controls')
    for changed in mutations:
        try:
            review(changed)
        except ValueError:
            continue
        raise ValueError('Corrupted saved mutation record was accepted')
    result['CorruptionRejections'] = len(mutations); result['InputSha256'] = hashlib.sha256(data).hexdigest()
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
