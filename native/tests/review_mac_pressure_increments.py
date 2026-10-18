"""Saved-only pressure increment and restarted-node review at 100 digits."""
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
    check(key not in target, 'Duplicate saved record')
    target[key] = value


def parse(raw):
    data = {k: {} for k in ('materials', 'queries', 'qmasses', 'roots', 'cells', 'masses', 'nodes', 'controls')}
    for line in raw.decode('ascii').splitlines():
        row = line.split(); check(bool(row), 'Empty saved record'); kind = row[0]
        if kind == 'CWDEF':
            check(len(row) == 11 and row[1] == '0', 'Material shape')
            unique(data['materials'], int(row[2]), (int(row[3]), *map(number, row[4:])))
        elif kind == 'PQUERY':
            check(len(row) == 12, 'Increment query shape')
            unique(data['queries'], int(row[1]), (int(row[2]), *map(number, row[3:])))
        elif kind in ('PQMASS', 'PROOTMASS'):
            check(len(row) == 4, 'Mass shape')
            unique(data['qmasses' if kind == 'PQMASS' else 'masses'], tuple(map(int, row[1:3])), number(row[3]))
        elif kind == 'PROOT':
            check(len(row) == 7, 'Root shape')
            unique(data['roots'], int(row[1]), tuple(map(number, row[2:])))
        elif kind == 'PROOTCELL':
            check(len(row) == 9, 'Cell shape')
            unique(data['cells'], int(row[1]), tuple(map(number, row[2:])))
        elif kind == 'PROOTNODE':
            check(len(row) == 7, 'Node shape')
            unique(data['nodes'], int(row[1]), (*map(int, row[2:4]), *map(number, row[4:])))
        elif kind == 'PCOUPLED':
            check(len(row) == 6, 'Coupled restart shape')
            unique(data['controls'], kind + row[1], tuple(map(number, row[2:])))
        elif kind == 'PWALL':
            check(len(row) == 7, 'Fixed-wall control shape')
            unique(data['controls'], kind + row[1], tuple(map(number, row[2:])))
        elif kind in ('PUNAVAILABLE', 'POWNER'):
            unique(data['controls'], kind, tuple(map(number, row[1:])))
        elif kind == 'PASS':
            unique(data['controls'], kind, True)
        else:
            raise ValueError('Unknown record: ' + kind)
    return data


def anchored_change(materials, masses, p0, vg0, dv):
    """Integrate the EOS adiabat from consistent p0/headspace, independently."""
    p0, vg0, dv = map(dec, (p0, vg0, dv))
    cv = sum((dec(m) * dec(materials[i][2]) for i, m in masses.items()), D(0))
    nr = sum((dec(m) / dec(materials[i][1]) * dec(R) for i, m in masses.items() if materials[i][0] == 0), D(0))
    liquids = [(dec(m) / dec(materials[i][4]), dec(materials[i][5])) for i, m in masses.items() if materials[i][0] == 1]
    if not liquids:
        # Secant series, accumulated from its first nonconstant term.
        x = dv / vg0; k = nr / cv; coefficient = D(1); power = D(1); shift = D(0)
        for n in range(1, 32):
            coefficient *= -(k + n) / (n + 1); power *= x; shift += coefficient * power
        return p0 * shift
    def liquid(p):
        volume = sum((nominal / (1 + p / bulk) for nominal, bulk in liquids), D(0))
        energy = sum((nominal * bulk * ((1 + p / bulk).ln() - p / (bulk + p)) for nominal, bulk in liquids), D(0))
        return volume, energy
    vl0, el0 = liquid(p0); target = vl0 + vg0 + dv
    if nr == 0 and target >= sum((n for n, _ in liquids), D(0)):
        return el0 / dv - p0
    beta = cv / (cv + nr) if nr else D(0)
    thermal0 = cv * p0 * vg0 / nr if nr else D(0)
    def state(p):
        vl, el = liquid(p)
        vg = vg0 * (p0 / p) ** beta if nr else D(0)
        thermal = thermal0 * (p / p0) ** (1 - beta) if nr else D(0)
        return vl + vg, thermal + el
    lo, hi = D(0), p0
    for _ in range(128):
        if state(hi)[0] <= target:
            break
        hi *= 2
    else:
        raise ValueError('Independent pressure bracket exhausted')
    for _ in range(400):
        mid = (lo + hi) / 2
        if state(mid)[0] > target:
            lo = mid
        else:
            hi = mid
    endpoint = (lo + hi) / 2
    return -(state(endpoint)[1] - thermal0 - el0) / dv - p0


def review(data):
    check(len(data['materials']) == 4 and set(data['queries']) == set(range(24)), 'Material/query coverage')
    for i, row in data['queries'].items():
        material, energy, volume, dv, p0, t0, vg0, work, derivative, change = row
        check(material == i // 8 and volume == 1 and dv == F(math.ldexp(-1 if i % 2 == 0 else 1, (-12, -20, -40, -70)[i % 8 // 2])), 'Scientific query inputs changed')
        masses = {k: m for (q, k), m in data['qmasses'].items() if q == i}
        check(masses and all(m > 0 for m in masses.values()), 'Missing positive constituents')
        expected = anchored_change(data['materials'], masses, p0, vg0, dv)
        check(change != 0 and abs(dec(change) - expected) <= 128 * dec(EPS) * abs(expected), 'Continuous pressure increment differs from independent anchored adiabat')
        check(abs(p0 + change - work) <= 32 * EPS * max(abs(p0 + change), abs(work)), 'Increment exceeds original absolute reference bound')
        check(derivative < 0 and t0 > 0 and energy != 0, 'Invalid ordinary work response')
    check(set(data['roots']) == set(range(4)), 'Missing root timesteps')
    for i, (dt, restarts, iterations, pressure_iterations, residual) in data['roots'].items():
        check(dt == F((.01, .025, .05, .1)[i]) and restarts == int(i != 0), 'Missing ordinary/restarted path')
        check(iterations > 0 and pressure_iterations > 0 and 0 <= residual <= 16 * EPS, 'Root relaxed its volume bound')
    check(set(data['cells']) == set(range(256)) and set(data['nodes']) == set(range(512)), 'Incomplete restarted state')
    volume_flux = {i: F(0) for i in data['cells']}; momentum = []; energy_terms = []
    dt = F(.1)
    for i, (u0, u1, volume, dv, residual, p0, change) in data['cells'].items():
        check(volume == F(float(1 + dv)) and abs(residual) <= 16 * EPS * volume and p0 > 0, 'Stored volume or local root bound differs')
        species = {k for (cell, k) in data['masses'] if cell == i}
        check(species == {k for k, d in data['materials'].items() if d[0] == 0} and
            all(data['masses'][i, k] > 0 for k in species), 'Missing cell constituent or positive trace')
        energy_terms.extend((u1, -u0))
    for f, (l, r, inertia, velocity, difference) in data['nodes'].items():
        x, y = (f // 2) % 16, (f // 2) // 16
        expected_l = y * 16 + (x - 1) % 16 if f % 2 == 0 else ((y - 1) % 16) * 16 + x
        check((l, r) == (expected_l, y * 16 + x), 'Periodic node topology changed')
        pl, dl = data['cells'][l][-2:]; pr, dr = data['cells'][r][-2:]
        expected = F(float(pl - pr + dl - dr))
        check(difference == expected and velocity == F(float(F(float(dt * difference)) / inertia)), 'Publication used a different pressure difference')
        ml = sum((m for (cell, _), m in data['masses'].items() if cell == l), F(0)); mr = sum((m for (cell, _), m in data['masses'].items() if cell == r), F(0))
        check(abs(inertia - (ml + mr) / 2) <= 2 * EPS * inertia, 'Node half mass differs')
        flux = dt * velocity / 2; volume_flux[l] += flux; volume_flux[r] -= flux
        momentum.append(inertia * velocity); energy_terms.append(inertia * velocity**2 / 2)
    for i, row in data['cells'].items():
        check(abs(row[3] - volume_flux[i]) <= 16 * EPS * row[2], 'Actual midpoint sweep does not close the root')
    check(abs(sum(momentum, F(0))) <= 128 * EPS * sum(map(abs, momentum), F(0)), 'Closed momentum drift')
    check(abs(sum(energy_terms, F(0))) <= 128 * EPS * sum(map(abs, energy_terms), F(0)), 'Actual U plus K drift')
    controls = data['controls']
    check(controls['PUNAVAILABLE'] == (1,) and controls['POWNER'] == (1, 0, 1, 1) and controls['PASS'], 'Native availability/owner continuation controls missing')
    for i in range(3):
        dt, restarts, iterations, residual = controls['PCOUPLED' + str(i)]
        check(dt == F(.1 if i == 0 else .5) and restarts == 1 and iterations > 0 and 0 <= residual <= 16 * EPS,
            'Coupled gas, mixed or liquid restart missing')
    for i in range(2):
        gravity, steps, restarts, retries, accounted = controls['PWALL' + str(i)]
        check(gravity == F(0 if i == 0 else 9.81) and steps > 0 and restarts > 0 and retries >= 0 and accounted == 1,
            'Native fixed-wall/gravity account controls missing')


def main():
    raw = Path(sys.argv[1]).read_bytes(); check(len(raw) < 2 * 1024 * 1024, 'Saved pipe cap')
    with localcontext() as ctx:
        ctx.prec = 100; data = parse(raw); review(data); saved = CHECKS
        for kind in ('increment', 'node', 'volume', 'tail'):
            bad = copy.deepcopy(data)
            if kind == 'increment':
                values = list(bad['queries'][23]); values[-1] *= 2; bad['queries'][23] = tuple(values)
            elif kind == 'node':
                values = list(bad['nodes'][0]); values[-1] += 1; bad['nodes'][0] = tuple(values)
            elif kind == 'volume':
                values = list(bad['cells'][0]); values[2] += F(1, 1000); bad['cells'][0] = tuple(values)
            else:
                del bad['masses'][next(k for k, m in bad['masses'].items() if m < F(1, 10**20))]
            rejected = False
            try:
                review(bad)
            except ValueError:
                rejected = True
            check(rejected, 'Altered pressure evidence accepted: ' + kind)
    print(json.dumps(dict(Passed=True, SavedChecks=saved, CorruptionsRejected=4, SavedSha256=hashlib.sha256(raw).hexdigest(),
        NativeProcessLaunched=False, Scope='Anchored pressure query and periodic material root; owning continuation is native-only; no large-step accuracy or colony acceptance')))


if __name__ == '__main__':
    main()
