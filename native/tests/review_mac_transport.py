"""Review saved MAC operands; never launches a native process.

Balances use exact binary64 Fractions. Wave profiles are compared with an
80-digit Fourier recurrence as well as the continuous translated sine. This
checks a transport component, not pressure, free surfaces or colony ownership.
"""
import copy
from decimal import Decimal, localcontext
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

EPS = F(1, 2**52)
CHECKS = 0


def check(ok, reason):
    global CHECKS
    CHECKS += 1
    if not ok:
        raise ValueError(reason)


def close(actual, expected, scale, reason, factor=128):
    check(abs(actual - expected) <= factor * EPS * scale, reason)


def number(token):
    value = float(token)
    check(math.isfinite(value), 'Nonfinite saved operand')
    return F(value)


def unique(mapping, key, value):
    check(key not in mapping, 'Duplicate saved record')
    mapping[key] = value


def parse(raw):
    cases, waves, samples, passes = {}, {}, {}, []
    timestep = None
    for line in raw.decode('ascii').splitlines():
        row = line.split()
        check(bool(row), 'Empty saved record')
        kind = row[0]
        if kind == 'CASE':
            check(len(row) == 8, 'CASE shape')
            identifier, w, h, px, py = map(int, row[1:6])
            check(w > 0 and h > 0 and px in (0, 1) and py in (0, 1), 'CASE geometry')
            unique(cases, identifier, dict(w=w, h=h, px=px, py=py,
                residual=number(row[6]), bound=number(row[7]), walls={}, cells={},
                masses={}, passives={}, faces={}, duals={}))
        elif kind in ('WALL', 'CELL', 'MASS', 'PASSIVE', 'FACE', 'DUAL'):
            identifier = int(row[1]); check(identifier in cases, 'Orphan case record')
            case = cases[identifier]
            if kind == 'WALL':
                check(len(row) == 4 and int(row[3]) in (0, 1), 'WALL shape')
                unique(case['walls'], int(row[2]), int(row[3]))
            elif kind == 'CELL':
                check(len(row) == 5, 'CELL shape')
                unique(case['cells'], tuple(map(int, row[2:4])), number(row[4]))
            elif kind in ('MASS', 'PASSIVE'):
                check(len(row) == 6, 'Constituent/metadata shape')
                value = number(row[5]); check(value > 0, 'Nonpositive constituent/metadata')
                unique(case['masses' if kind == 'MASS' else 'passives'], tuple(map(int, row[2:5])), value)
            elif kind == 'FACE':
                check(len(row) == 16, 'FACE shape')
                unique(case['faces'], tuple(map(int, row[2:4])), tuple(map(number, row[4:])))
            else:
                check(len(row) == 6, 'DUAL shape')
                unique(case['duals'], tuple(map(int, row[2:5])), number(row[5]))
        elif kind == 'WAVE':
            check(len(row) == 6, 'WAVE shape')
            unique(waves, (int(row[1]), number(row[2])), (int(row[3]), number(row[4]), number(row[5])))
        elif kind == 'SAMPLE':
            check(len(row) == 6, 'SAMPLE shape')
            unique(samples, (int(row[1]), number(row[2]), int(row[3])), (number(row[4]), number(row[5])))
        elif kind == 'TIMESTEP':
            check(len(row) == 3 and timestep is None, 'TIMESTEP shape/duplicate')
            timestep = tuple(map(number, row[1:]))
        elif kind == 'PASS':
            passes.append(line)
        else:
            raise ValueError('Unknown saved record: ' + kind)
    check(set(cases) == {0, 1} and len(passes) == 7 and len(set(passes)) == 7, 'Missing case/pass groups')
    return cases, waves, samples, timestep


def topology(case):
    w, h, px, py = (case[k] for k in ('w', 'h', 'px', 'py'))
    check(set(case['walls']) == set(range(w * h)), 'Missing/extra wall records')
    faces = [[], []]
    active = lambda x, y: None if case['walls'][x + y * w] else x + y * w
    for y in range(h):
        for x in range(w + (not px)):
            faces[0].append((active((x - 1) % w, y) if x or px else None,
                             active(x, y) if x < w else None))
    for y in range(h + (not py)):
        for x in range(w):
            faces[1].append((active(x, (y - 1) % h) if y or py else None,
                             active(x, y) if y < h else None))
    check(set(case['faces']) == {(a, i) for a in (0, 1) for i in range(len(faces[a]))}, 'Missing/extra face records')
    return faces


def expected_duals(case):
    """Construct half-cell boundaries from primal fluxes, without native tables."""
    w, h, px, py = (case[k] for k in ('w', 'h', 'px', 'py'))
    nx, ny = w + (not px), h + (not py)
    xf = lambda x, y: x % nx + (y % h) * nx
    yf = lambda x, y: x % w + (y % ny) * w
    fx = lambda x, y: case['faces'][0, xf(x, y)][0]
    fy = lambda x, y: case['faces'][1, yf(x, y)][0]
    result = {}
    for y in range(h):
        for x in range(w):
            if not case['walls'][x + y * w]:
                result[0, xf(x, y), xf(x + 1, y)] = (fx(x, y) + fx(x + 1, y)) / 2
                result[1, yf(x, y), yf(x, y + 1)] = (fy(x, y) + fy(x, y + 1)) / 2
    for y in range(0 if py else 1, h):
        for x in range(nx):
            value = (fy(x - 1, y) if x or px else 0) + (fy(x, y) if x < w else 0)
            result[0, xf(x, y - 1), xf(x, y)] = value / 2
    for x in range(0 if px else 1, w):
        for y in range(ny):
            value = (fx(x, y - 1) if y or py else 0) + (fx(x, y) if y < h else 0)
            result[1, yf(x - 1, y), yf(x, y)] = value / 2
    check(set(result) == set(case['duals']), 'Dual boundary topology differs')
    for key, value in result.items():
        close(case['duals'][key], value, abs(value), 'Dual flux differs from half-cell geometry')
    return result


def packets(old_mass, edges):
    outgoing = [F(0) for _ in old_mass]
    incoming = [[] for _ in old_mass]
    for left, right, flux in edges:
        check(0 <= left < len(old_mass) and 0 <= right < len(old_mass) and left != right, 'Invalid flux endpoint')
        if flux:
            donor, target = (left, right) if flux > 0 else (right, left)
            outgoing[donor] += abs(flux); incoming[target].append((donor, abs(flux)))
    for i, mass in enumerate(old_mass):
        check(outgoing[i] <= mass + 128 * EPS * mass, 'Outgoing donor mass exceeded')
        check(mass > 0 or not outgoing[i], 'Empty donor')
        incoming[i].append((i, mass - outgoing[i]))
    return incoming


def review_case(case):
    faces = topology(case); expected_duals(case)
    nc = case['w'] * case['h']
    check(set(case['cells']) == {(e, i) for e in (0, 1) for i in range(nc)}, 'Missing/extra material cells')
    for values in (case['masses'], case['passives']):
        check(all(e in (0, 1) and 0 <= i < nc and k >= 0 for e, i, k in values), 'Invalid inventory index')
    mass = [[sum((v for (end, cell, _), v in case['masses'].items() if end == e and cell == i), F(0))
             for i in range(nc)] for e in (0, 1)]
    for i in range(nc):
        check((mass[0][i] == mass[1][i] == 0) if case['walls'][i] else min(mass[0][i], mass[1][i]) > 0, 'Wall/occupied inventory differs')
    primal = []
    for axis in (0, 1):
        for i, adjacent in enumerate(faces[axis]):
            row = case['faces'][axis, i]; flux, j0, j1, i0, i1 = row[:5]
            for endpoint, inertia in ((0, i0), (1, i1)):
                expect = sum((mass[endpoint][c] for c in adjacent if c is not None), F(0)) / 2
                close(inertia, expect, expect, 'Face inertia differs from adjacent half masses')
            if None in adjacent:
                check(flux == j0 == j1 == 0, 'Constrained normal face carries mass/momentum')
            else:
                primal.append((*adjacent, flux))
    material_packets = packets(mass[0], primal)
    for key in ('masses', 'passives'):
        values = case[key]; identities = {k for _, _, k in values}
        for identity in identities:
            old_total = sum((values.get((0, i, identity), F(0)) for i in range(nc)), F(0))
            new_total = sum((values.get((1, i, identity), F(0)) for i in range(nc)), F(0))
            close(new_total, old_total, old_total + new_total, 'Global constituent/passive balance')
            for i, donors in enumerate(material_packets):
                expect = sum((amount * values.get((0, donor, identity), F(0)) / mass[0][donor]
                              for donor, amount in donors if amount), F(0))
                actual = values.get((1, i, identity), F(0))
                close(actual, expect, abs(expect) + abs(actual), 'Constituent/passive packet mismatch')
    cell_heat = [F(0) for _ in range(nc)]
    energy_terms = [case['cells'][e, i] * (1 if e else -1) for e in (0, 1) for i in range(nc)]
    for axis in (0, 1):
        rows = [case['faces'][axis, i] for i in range(len(faces[axis]))]
        weights = packets([row[3] for row in rows], [(l, r, f) for (a, l, r), f in case['duals'].items() if a == axis])
        momentum_terms = []
        for i, (row, donors) in enumerate(zip(rows, weights)):
            _, j0, j1, i0, i1, k0, k1, kt, qm, qw, impulse, dm = row
            check(min(i0, i1, k0, k1, kt, qm, qw) >= 0, 'Negative inertia/kinetic/heat')
            transported_mass = sum((amount for _, amount in donors), F(0))
            close(transported_mass, i1, transported_mass + i1, 'Dual mass continuity')
            close(dm, transported_mass - i1, transported_mass + i1, 'Saved dual mass residual')
            provisional_j = sum((amount * rows[d][1] / rows[d][3] for d, amount in donors if amount), F(0))
            provisional_u = provisional_j / i1 if i1 else F(0)
            fixed = None in faces[axis][i]
            expect_j = F(0) if fixed else provisional_j
            jscale = sum((abs(amount * rows[d][1] / rows[d][3]) for d, amount in donors if amount), F(0))
            close(j1, expect_j, jscale + abs(j1), 'Dual momentum packet mismatch')
            close(impulse, expect_j - provisional_j, jscale + abs(impulse), 'Wall impulse mismatch')
            for inertia, momentum, actual in ((i0, j0, k0), (i1, j1, k1)):
                expect = momentum * momentum / (2 * inertia) if inertia else F(0)
                close(actual, expect, actual + expect, 'Stored resolved kinetic energy')
            expect_kt = sum((amount * rows[d][5] / rows[d][3] for d, amount in donors if amount), F(0))
            close(kt, expect_kt, kt + expect_kt, 'Transported kinetic packet mismatch')
            expect_qm = sum((amount * (rows[d][1] / rows[d][3] - provisional_u)**2 / 2
                             for d, amount in donors if amount), F(0))
            expect_qw = provisional_j * provisional_u / 2 if fixed else F(0)
            # A near-zero variance subtracts rounded velocities. Its allowed
            # error is tied to actual packet K, never an arbitrary unit floor.
            close(qm, expect_qm, kt + expect_kt + expect_qm, 'Positive variance heat mismatch', 1024)
            close(qw, expect_qw, qw + expect_qw, 'Wall projection heat mismatch')
            close(kt, k1 + qm + qw, kt + k1 + qm + qw, 'Local kinetic-to-heat closure', 1024)
            total = sum((mass[1][c] for c in faces[axis][i] if c is not None), F(0))
            for c in faces[axis][i]:
                if c is not None and total:
                    cell_heat[c] += (qm + qw) * mass[1][c] / total
            momentum_terms.extend((j1, -j0, -impulse)); energy_terms.extend((k1, -k0))
        close(sum(momentum_terms, F(0)), F(0), sum(map(abs, momentum_terms), F(0)), 'Global J plus wall impulse')
    for i, donors in enumerate(material_packets):
        terms = [amount * case['cells'][0, d] / mass[0][d] for d, amount in donors if amount] + [cell_heat[i]]
        expect = sum(terms, F(0)); actual = case['cells'][1, i]
        close(actual, expect, abs(actual) + sum(map(abs, terms), F(0)), 'Primal U plus assigned heat', 256)
    residual = sum(energy_terms, F(0)); scale = sum(map(abs, energy_terms), F(0))
    close(residual, F(0), scale, 'Physical global energy balance')
    close(case['residual'], residual, scale, 'Saved energy residual', 2)
    close(case['bound'], 128 * EPS * scale, 128 * EPS * scale, 'Saved energy bound', 512)


PI = Decimal('3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117')


def sincos(x):
    x %= 2 * PI
    if x > PI:
        x -= 2 * PI
    sine, cosine, st, ct = x, Decimal(1), x, Decimal(1)
    for k in range(1, 180):
        st *= -x * x / ((2 * k) * (2 * k + 1)); ct *= -x * x / ((2 * k - 1) * (2 * k))
        sine += st; cosine += ct
        if max(abs(st), abs(ct)) < Decimal('1e-85'):
            return sine, cosine
    raise ValueError('High-precision trigonometric series did not converge')


def decimal_value(value):
    return Decimal(value.numerator) / Decimal(value.denominator)


def review_waves(waves, samples, timestep):
    keys = {(n, F(float(c))) for n, c in ((16, .2), (32, .2), (64, .2), (16, .1), (16, .05))}
    check(set(waves) == keys and set(samples) == {(n, c, x) for n, c in keys for x in range(n)}, 'Missing/extra wave profiles')
    for n, c in sorted(keys):
        with localcontext() as ctx:
            ctx.prec = 80
            h = Decimal(1) / n; dt = decimal_value(c) * h / Decimal('.75'); duration = Decimal('.125')
            st, ct = sincos(2 * PI * h); sinc = sincos(PI * h)[0] / (PI * h)
            real, imag, time, steps = Decimal(1), Decimal(0), Decimal(0), 0
            while time < duration:
                step = min(dt, duration - time); nu = Decimal('.5') * step / h
                ar, ai = 1 - nu + nu * ct, -nu * st
                real, imag = real * ar - imag * ai, real * ai + imag * ar
                time += step; steps += 1
            saved_steps, verr, terr = waves[n, c]
            check(abs(saved_steps - steps) <= 1 and saved_steps <= math.ceil(.125 / (float(c) / n / .75)) + 2, 'Wave step bound')
            continuous_errors = [F(0), F(0)]
            for x in range(n):
                s, co = sincos(2 * PI * (Decimal(x) + Decimal('.5')) * h)
                transported = sinc * (s * real + co * imag)
                uy, tag = samples[n, c, x]
                check(abs(decimal_value(uy) - Decimal('.25') * transported) <= Decimal('5e-12'), 'Shear differs from independent upwind recurrence')
                check(abs(decimal_value(tag) - (1 + Decimal('.2') * transported)) <= Decimal('2e-11'), 'Passive differs from independent upwind recurrence')
                continuous = F(sinc * sincos(2 * PI * ((Decimal(x) + Decimal('.5')) * h - Decimal('.0625')))[0])
                continuous_errors[0] += abs(uy - continuous / 4) / n
                continuous_errors[1] += abs(tag - (1 + continuous / 5)) / n
            close(verr, continuous_errors[0], F(1, 4), 'Continuous shear error differs from samples', 1024)
            close(terr, continuous_errors[1], F(1), 'Continuous passive error differs from samples', 1024)
    c = F(.2)
    for a, b in ((16, 32), (32, 64)):
        check(all(0 < waves[b, c][j] < F(7, 10) * waves[a, c][j] for j in (1, 2)), 'Joint grid refinement bound')
    differences = []
    for ca, cb in ((.2, .1), (.1, .05)):
        differences.append(sum((abs(samples[16, F(ca), x][0] - samples[16, F(cb), x][0]) for x in range(16)), F(0)) / 16)
    check(timestep is not None, 'Missing timestep result')
    for actual, expect in zip(timestep, differences):
        close(actual, expect, F(1, 4), 'Fixed-grid timestep difference differs from profiles', 1024)
    check(0 < differences[1] < F(7, 10) * differences[0], 'Fixed-grid timestep refinement bound')


def review(data):
    cases, waves, samples, timestep = data
    for case in cases.values():
        review_case(case)
    review_waves(waves, samples, timestep)


def main():
    path = Path(sys.argv[1]); raw = path.read_bytes()
    check(len(raw) <= 131072, 'Saved stdout exceeds declared cap')
    data = parse(raw); review(data)
    checks = CHECKS
    for control in ('dual-flux', 'wave-profile', 'trace-loss'):
        altered = copy.deepcopy(data)
        if control == 'dual-flux':
            altered[0][0]['duals'][0, 0, 1] += F(1, 100)
        elif control == 'wave-profile':
            key = (16, F(.2), 0); a, b = altered[2][key]; altered[2][key] = (a + F(1, 100), b)
        else:
            case = altered[0][1]; key = next(k for k in case['masses'] if k[0] == 1 and case['masses'][k] < F(1, 10**20))
            case['masses'][key] /= 10
        rejected = False
        try:
            review(altered)
        except ValueError:
            rejected = True
        check(rejected, 'Corruption was accepted: ' + control)
    print(json.dumps(dict(Passed=True, SavedSha256=hashlib.sha256(raw).hexdigest(),
        RationalAndProfileChecks=checks, CorruptionsRejected=3, NativeProcessLaunched=False,
        Scope='Regular 2D MAC transport only; no pressure, free surfaces, game or performance acceptance',
        IndependentAgentReview=False)))


if __name__ == '__main__':
    main()
