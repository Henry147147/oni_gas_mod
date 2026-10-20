"""Saved-only viscosity/conductivity review, exact accounts and 80-digit EOS."""
import copy
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


heat = module('matched_heat_reference', 'review_mac_shock_heat.py')
acoustic = module('matched_heat_acoustic', 'review_mac_acoustic_compression.py')
base = heat.base
check, close, number, unique, dec = base.check, base.close, base.number, base.unique, heat.dec
KINDS = {'HMLAW', 'HMCP', 'HMSPEED', 'HMPRESSURE', 'HMSENSOR', 'HMDIFFUSION'}


def split(raw):
    ordinary = []
    extra = {k: {} for k in ('laws', 'cp', 'speeds', 'pressures', 'sensors', 'diffusion', 'weak')}
    rejected = None
    for line in raw.decode('ascii').splitlines():
        r = line.split()
        check(bool(r), 'Empty matched-heat record')
        kind = r[0]
        if kind == 'HMLAW':
            check(len(r) == 3, 'Matching law shape')
            unique(extra['laws'], int(r[1]), int(r[2]))
        elif kind in ('HMCP', 'HMSPEED', 'HMDIFFUSION'):
            check(len(r) == 5, 'Matching scaled field shape')
            field = {'HMCP': 'cp', 'HMSPEED': 'speeds', 'HMDIFFUSION': 'diffusion'}[kind]
            unique(extra[field], tuple(map(int, r[1:3])), base.scaled_energy(r[3], r[4]))
        elif kind == 'HMPRESSURE':
            check(len(r) == 4, 'Matching source pressure shape')
            unique(extra['pressures'], tuple(map(int, r[1:3])), number(r[3]))
        elif kind == 'HMSENSOR':
            check(len(r) == 6, 'Matching axis sensor shape')
            unique(extra['sensors'], tuple(map(int, r[1:4])), base.scaled_energy(r[4], r[5]))
        elif kind == 'HMWEAK':
            check(len(r) == 4, 'Matching weak-wave shape')
            unique(extra['weak'], int(r[1]), tuple(map(number, r[2:])))
        elif kind == 'HMREJECTED':
            check(len(r) == 2 and rejected is None, 'Matching rejection shape')
            rejected = int(r[1])
        else:
            ordinary.append(line)
    return ('\n'.join(ordinary) + '\n').encode('ascii'), extra, rejected


def parse(raw, required_grids=range(13)):
    ordinary, extra, rejected = split(raw)
    check(rejected == 7, 'Missing immutable rejection controls')
    data = heat.parse(ordinary, required_passes=8, required_grids=required_grids, controls=False)
    return data, extra


def review_component(data, extra, expected_law=1):
    grids = set(data['grids'])
    check(set(extra['laws']) == grids and set(extra['laws'].values()) == {expected_law}, 'Missing/changed matching law')
    acoustic.review_pressure_sensors(data, extra)
    expected_cells = set()
    expected_events = set()
    diffusivity = {}
    for gid, g in data['grids'].items():
        w, h, spacing, depth, px, py, dt = g[:7]
        volume = spacing * spacing * depth
        walls = {i: data['walls'][gid, i] for i in range(w * h)}
        for cell in walls:
            expected_cells.add((gid, cell))
            cp, speed = extra['cp'][gid, cell], extra['speeds'][gid, cell]
            if walls[cell]:
                check(cp == speed == 0, 'Solid matching coefficients must be zero')
                continue
            masses = {s: m for (i, e, c, s), m in data['masses'].items() if (i, e, c) == (gid, 0, cell)}
            u = data['cells'][gid, 0, cell][0]
            p, t, _, _ = heat.eos(masses, data['materials'], u, volume)
            _, _, cv, _, moles = heat.ref.eos_values(masses, data['materials'], p)
            cp_expected = cv + moles * dec(heat.ref.R)
            check(cp > 0, 'Nonpositive matching Cp capacity')
            heat.near(cp, cp_expected, 'Independent constant-pressure capacity')
            compliance = D(0)
            for i, m in masses.items():
                phase, _, _, _, rho, bulk = data['materials'][i]
                if phase:
                    compliance += dec(m) / dec(rho) / dec(bulk) / (1 + p / dec(bulk)) ** 2
            if moles:
                vg = moles * dec(heat.ref.R) * t / p
                compliance += cv / cp_expected * vg / p
            sound = dec(volume) / (dec(sum(masses.values(), F(0))) * compliance).sqrt() / 2
            heat.near(speed, sound, 'Independent matching half-sound speed')
        xw, yh = w + (not px), h + (not py)
        for axis in (0, 1):
            velocities = {f: heat.rounded_scaled(j / mass) if mass else F(0)
                          for (ig, a, e, f), (mass, _, j) in data['faces'].items()
                          if (ig, a, e) == (gid, axis, 0)}
            for cell in walls:
                if walls[cell]:
                    continue
                x, y = cell % w, cell // w
                l = x + y * xw if axis == 0 else x + y * w
                r = (x + 1) % xw + y * xw if axis == 0 else x + ((y + 1) % yh) * w
                closing = max(F(0), heat.rounded_scaled(velocities[l] - velocities[r]))
                # The acoustic term ramps in with the compression: v / (v + c / 100).
                half = extra['speeds'][gid, cell]
                c = heat.rounded_scaled(half * extra['sensors'][gid, axis, cell])
                ramp = closing / heat.rounded_scaled(closing + heat.rounded_scaled(half * F(0.02))) if closing else F(0)
                diffusivity[gid, axis, cell] = heat.rounded_scaled(closing + heat.rounded_scaled(c * ramp)) if closing else F(0)
        expected_events.update((gid, i) for i in range(g[7]))
    check(set(extra['cp']) == set(extra['speeds']) == expected_cells, 'Missing/orphan Cp or sound-speed field')
    check(set(extra['diffusion']) == expected_events, 'Missing/orphan face diffusion field')

    def conductance(gid, event, axis, face, left, right, closing):
        del face
        check(closing > 0, 'Matching heat activated outside compression')
        g = data['grids'][gid]
        speed = max(diffusivity[gid, axis, left], diffusivity[gid, axis, right])
        if expected_law != 2:
            close(extra['diffusion'][gid, event], speed, speed, 'Matching frozen face speed', 8)
        a, b = extra['cp'][gid, left], extra['cp'][gid, right]
        if expected_law == 2:
            dl, dr = diffusivity[gid, axis, left], diffusivity[gid, axis, right]
            check(dl > 0 and dr > 0, 'Series heat crosses an inactive half cell')
            resistance = g[2] / (2 * a * dl) + g[2] / (2 * b * dr)
            equivalent_speed = g[2] / resistance / (2 * a * b / (a + b))
            close(extra['diffusion'][gid, event], equivalent_speed, equivalent_speed, 'Series equivalent face speed', 16)
            return g[6] / 2 / resistance
        return g[6] / 2 / g[2] * speed * 2 * a * b / (a + b)

    for key in data['pairs']:
        heat.review_pair(data, key)
    for gid in data['grids']:
        activation = None if expected_law != 2 else lambda g, a, f, l, r, c: diffusivity[g, a, l] > 0 and diffusivity[g, a, r] > 0
        heat.review_grid(data, gid, False, conductance, activation)


def review(data, extra):
    review_component(data, extra)
    for gid in range(3, 8):
        events, total = data['grids'][gid][7], data['grids'][gid][9]
        check(total == 0 and (events > 0) == (gid == 6), 'Inactive matching control changed')
    subrange = data['grids'][8]
    check(subrange[9] > 0 and float(subrange[9]) == 0 and heat.retained_grid_heat(data,8,subrange[8]), 'Missing matched subrange heat and unresolved storage')
    check(set(extra['weak']) == {10, 11, 12}, 'Missing weak-wave scaling controls')
    previous = None
    for gid, amplitude in ((10, number('1e-3')), (11, number('1e-5')), (12, number('1e-7'))):
        a, value = extra['weak'][gid]
        check(a == amplitude, 'Weak-wave input amplitude changed')
        expected = data['grids'][gid][9] / (a * a)
        close(value, expected, expected, 'Weak-wave amplitude-normalized heat', 8)
        check(value > 0 and (previous is None or abs(value - previous) < number('1e-5') * previous), 'First-order heat term in weak waves')
        previous = value


def main():
    raw = Path(sys.argv[1]).read_bytes()
    check(len(raw) <= 2097152, 'Matching saved pipe cap')
    with localcontext() as context:
        context.prec = 80
        data, extra = parse(raw)
        review(data, extra)
        checks = base.CHECKS + acoustic.base.CHECKS
        corruptions = ('heat', 'trace', 'capacity', 'tangent', 'sensor', 'geometry')
        for corruption in corruptions:
            bad, other = copy.deepcopy(data), copy.deepcopy(extra)
            if corruption == 'heat':
                key = min(bad['pairs'])
                row = list(bad['pairs'][key]); row[18] *= F(17, 16); bad['pairs'][key] = tuple(row)
            elif corruption == 'trace':
                key = min((k for k in bad['masses'] if k[1] == 1 and bad['materials'][k[3]][0] == 0), key=lambda k: bad['masses'][k])
                bad['masses'][key] *= 2
            elif corruption in ('capacity', 'tangent'):
                field = 'cp' if corruption == 'capacity' else 'speeds'
                other[field][0, 0] *= F(17, 16)
            elif corruption == 'sensor':
                other['sensors'][0, 0, 0] += F(1, 8)
            else:
                key = min(bad['events']); row = list(bad['events'][key]); row[2] = row[3]; bad['events'][key] = tuple(row)
            caught = False
            try:
                review(bad, other)
            except (ValueError, KeyError):
                caught = True
            check(caught, 'Altered matched-heat record accepted')
    result = dict(Passed=True, Groups=8, SavedChecks=checks, CorruptionsRejected=list(corruptions),
                  SavedSha256=hashlib.sha256(raw).hexdigest(), NativeProcessLaunched=False, IndependentAgentReview=False,
                  Scope='Separate occupied viscosity-matched heat component; no owner, strong-flow, production or game acceptance')
    if len(sys.argv) > 2:
        with Path(sys.argv[2]).open('x') as stream:
            json.dump(result, stream, indent=2)
    print(json.dumps(result))


if __name__ == '__main__':
    main()
