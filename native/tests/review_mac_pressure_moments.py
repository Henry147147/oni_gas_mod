"""Review saved temporal wave endpoints, without launching native code."""
import copy
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

CHECKS = 0
EPS = F(2) ** -52
CASES = ((16, 16, .004, .00005), (32, 2, .008, .0001),
         (64, 2, .02, .0001), (256, 2, .1, .0001), (512, 2, .1, .0001))


def check(ok, why):
    global CHECKS
    CHECKS += 1
    if not ok:
        raise ValueError(why)


def number(value):
    x = float(value)
    check(math.isfinite(x), 'Nonfinite saved number')
    return F(x)


def near(actual, expected, why, ulps=8):
    check(abs(actual - expected) <= ulps * EPS * (abs(actual) + abs(expected)), why)


def parse(raw):
    data = dict(version=None, gas_constant=None, thermo={}, waves={}, references={}, states={}, physics={}, oracle=None, thread=None, trace=None, passes=[])
    for line in raw.decode('ascii').splitlines():
        row = line.split(); check(bool(row), 'Empty saved record'); tag = row[0]
        if tag == 'MVERSION':
            check(len(row) == 3 and data['version'] is None, 'Version shape')
            data['version'] = int(row[1]); data['gas_constant'] = number(row[2])
        elif tag == 'MTHERMO':
            check(len(row) == 5 and row[1] not in data['thermo'], 'Material identity/shape')
            data['thermo'][row[1]] = tuple(map(number, row[2:]))
        elif tag == 'MWAVE':
            check(len(row) == 13 and int(row[1]) not in data['waves'], 'Wave identity/shape')
            data['waves'][int(row[1])] = (int(row[2]), int(row[3]), *map(number, row[4:11]), int(row[11]), int(row[12]))
        elif tag == 'MREFERENCE':
            check(len(row) == 8 and int(row[1]) not in data['references'], 'Reference identity/shape')
            data['references'][int(row[1])] = tuple(map(number, row[2:]))
        elif tag == 'MSTATE':
            check(len(row) == 13, 'Endpoint shape'); key = (int(row[1]), row[2], int(row[3]))
            check(key not in data['states'], 'Duplicate endpoint'); data['states'][key] = tuple(map(number, row[4:]))
        elif tag == 'MPHYSICS':
            check(len(row) == 6 and row[1] not in data['physics'], 'Physical control identity/shape')
            data['physics'][row[1]] = tuple(map(number, row[2:]))
        elif tag == 'MORACLE':
            check(len(row) == 4 and data['oracle'] is None, 'Oracle shape'); data['oracle'] = tuple(map(number, row[1:]))
        elif tag == 'MTHREAD':
            check(len(row) == 6 and data['thread'] is None, 'Thread shape'); data['thread'] = tuple(map(int, row[1:]))
        elif tag == 'MTRACE':
            check(len(row) == 4 and data['trace'] is None, 'Trace shape'); data['trace'] = tuple(map(int, row[1:]))
        elif tag == 'PASS':
            data['passes'].append(line)
        else:
            check(False, 'Unknown saved record')
    return data


def metrics(reference, candidate):
    # All operands are exact fractions of the saved binary64 endpoints.
    result = [F(0)] * 5
    for a, b in zip(reference, candidate):
        result[0] = max(result[0], abs(a[1] - b[1]))
        result[1] = max(result[1], abs(a[2] - b[2]) / 20)
        result[2] = max(result[2], abs(a[3] - b[3]) / a[3])
        result[3] = max(result[3], *(abs(a[k] - b[k]) / a[k] for k in range(4, 7)))
        result[4] = max(result[4], *(abs(a[k] - b[k]) / a[k] for k in range(7, 9)))
    return result


def review(data):
    check(data['version'] == 1 and data['gas_constant'] == F(8.31446261815324), 'Unsupported format/gas constant')
    check(set(data['thermo']) == {'nitrogen', 'oxygen', 'helium'}, 'Material coverage')
    for name, capacity, molar_mass in (('nitrogen', 1040, .0280134), ('oxygen', 918, .0319988), ('helium', 5193, .004002602)):
        cv, offset, mw = data['thermo'][name]
        check(mw == F(molar_mass), 'Fixture molar mass changed')
        near(cv, F(capacity) - data['gas_constant'] / mw, 'Fixture heat capacity changed')
        near(offset, -F(capacity) * F(298.15), 'Fixture energy reference changed')
    check(set(data['waves']) == set(range(5)) and set(data['references']) == {0, 3}, 'Wave/reference coverage')
    check(set(data['physics']) == {'hydrostatic_gravity', 'room_step', 'mixed_gas_liquid', 'filled_liquid_gravity', 'empty_cells'}, 'Physical coverage')
    for values in data['physics'].values():
        check(all(v >= 0 for v in values) and values[0] <= F(.1) and all(v <= F(.01) for v in values[1:]), 'Physical accuracy bound')
    check(data['oracle'] is not None and all(v >= 0 for v in data['oracle']) and
          data['oracle'][0] < F(1e-8) and data['oracle'][1] < F(1e-8) and data['oracle'][2] < F(1e-10), 'Strict fine-step oracle bound')
    check(data['thread'] is not None and data['thread'][0:2] == (33389, 12) and
          all(v > 0 for v in data['thread'][2:]), 'Actual twelve-worker pressure solve missing')
    check(data['trace'] is not None and data['trace'][0] == 4096 and data['trace'][1] > 0 and 0 < data['trace'][2] <= 2048,
          'Spreading subnormal trace control missing')
    check(data['passes'] == ['PASS version-7 temporal admission, rollback and legacy fallback guards',
                            'PASS version-7 physical/accelerated diffusion and automatic/Jacobi continuation',
                            'PASS resolved moment waves, physical controls, threads and persisted continuation'], 'Completion controls')
    expected_keys = set()
    names = ('nitrogen', 'oxygen', 'helium')
    for case, (w, h, dt, fine) in enumerate(CASES):
        row = data['waves'][case]
        check(row[:4] == (w, h, F(dt), F(fine)), 'Temporal gate inputs changed')
        check(row[9] == row[10] and 5 <= row[9] <= 5 * (math.ceil(.1 / dt) + 1), 'Wave silently subcycled globally')
        kinds = ('R', 'C', 'F') if case in (0, 3) else ('R', 'C')
        profiles = {}
        for kind in kinds:
            profile = []
            for i in range(w * h):
                key = (case, kind, i); expected_keys.add(key)
                check(key in data['states'], 'Missing saved endpoint'); values = data['states'][key]
                check(len(values) == 9 and all(v > 0 for v in values[1:]), 'Invalid endpoint domain')
                U, T, p, o2 = values[:4]; masses = values[4:7]
                capacity = sum(m * data['thermo'][name][0] for m, name in zip(masses, names))
                offset = sum(m * data['thermo'][name][1] for m, name in zip(masses, names))
                nR = sum(m / data['thermo'][name][2] for m, name in zip(masses, names)) * data['gas_constant']
                near(T, (U - offset) / capacity, 'Saved temperature disagrees with caloric state', 32)
                near(p, nR * T, 'Saved pressure disagrees with EOS', 32)
                near(o2, masses[1] / data['thermo']['oxygen'][2] * data['gas_constant'] * T, 'Saved oxygen pressure disagrees with EOS', 32)
                profile.append(values)
            profiles[kind] = profile
        errors = metrics(profiles['R'], profiles['C'])
        for reported, actual in zip(row[4:9], errors):
            near(reported, actual, 'Reported wave error differs from saved endpoints')
        check(errors[0] <= F(.1) and all(v <= F(.01) for v in errors[1:]), 'Original temporal error bound')
        if case in (0, 3):
            ref = data['references'][case]; check(ref[:2] == (F(fine), F(fine / 2)), 'Resolution inputs changed')
            errors = metrics(profiles['F'], profiles['R'])
            for reported, actual in zip(ref[2:], errors[:4]):
                near(reported, actual, 'Reported reference error differs from saved endpoints')
            check(errors[0] <= F(.01) and all(v <= F(.001) for v in errors[1:]), 'Fine oracle is unresolved')
    check(set(data['states']) == expected_keys, 'Extra/unknown saved endpoints')


def main():
    raw = Path(sys.argv[1]).read_bytes(); check(len(raw) < 8 * 1024 * 1024, 'Saved pipe cap')
    data = parse(raw); review(data); saved = CHECKS
    mutations = ('temperature', 'pressure', 'oxygen', 'constituent', 'passive', 'reported', 'timestep', 'substeps', 'missing', 'resolution', 'threads', 'trace', 'material', 'constant')
    for kind in mutations:
        bad = copy.deepcopy(data)
        if kind in ('temperature', 'pressure', 'oxygen', 'constituent', 'passive'):
            index = {'temperature': 1, 'pressure': 2, 'oxygen': 3, 'constituent': 6, 'passive': 8}[kind]
            row = list(bad['states'][(0, 'C', 0)]); row[index] *= 2; bad['states'][(0, 'C', 0)] = tuple(row)
        elif kind in ('reported', 'timestep', 'substeps'):
            row = list(bad['waves'][0]); index = {'reported': 5, 'timestep': 2, 'substeps': 9}[kind]
            row[index] *= 2; bad['waves'][0] = tuple(row)
        elif kind == 'missing':
            del bad['states'][(3, 'R', 0)]
        elif kind == 'resolution':
            row = list(bad['references'][0]); row[1] *= 2; bad['references'][0] = tuple(row)
        elif kind == 'threads':
            row = list(bad['thread']); row[1] = 1; bad['thread'] = tuple(row)
        elif kind == 'trace':
            row = list(bad['trace']); row[1] = 0; bad['trace'] = tuple(row)
        elif kind == 'material':
            row = list(bad['thermo']['helium']); row[0] *= 2; bad['thermo']['helium'] = tuple(row)
        else:
            bad['gas_constant'] *= 2
        rejected = False
        try:
            review(bad)
        except ValueError:
            rejected = True
        check(rejected, 'Corrupted evidence accepted: ' + kind)
    print(json.dumps(dict(Passed=True, SavedChecks=saved, CorruptionsRejected=len(mutations),
                         SavedSha256=hashlib.sha256(raw).hexdigest(), NativeProcessLaunched=False,
                         Scope='Opt-in pressure moments at fixed schedules; no automatic local stepping or real-time acceptance')))


if __name__ == '__main__':
    main()
