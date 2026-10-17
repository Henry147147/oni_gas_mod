#!/usr/bin/env python3
"""Audit captured Newtonian stress, momentum and heat without loading native code."""
from __future__ import annotations
import argparse
import copy
from decimal import Decimal, localcontext
import hashlib
import json
from pathlib import Path


def require(value, message):
    if not value:
        raise ValueError(message)


def number(text):
    result = Decimal.from_float(float(text))
    require(result.is_finite(), 'Nonfinite saved viscous operand')
    return result


def scaled(mantissa, exponent):
    m, e = number(mantissa), int(exponent)
    require((m == 0 and e == 0) or Decimal('.5') <= abs(m) < 1, 'Invalid saved range scalar')
    return m * Decimal(2) ** e


def parse(path):
    data = {key: {} for key in ('materials', 'cases', 'cells', 'masses', 'passives', 'faces', 'events', 'dofs', 'heated')}
    terminal = False
    def put(table, key, value):
        require(key not in data[table], 'Duplicate saved viscosity record')
        data[table][key] = value
    for line in path.read_text().splitlines():
        fields = line.split()
        require(fields and not terminal, 'Empty or post-terminal data')
        kind, values = fields[0], fields[1:]
        if kind == 'VMATERIAL':
            require(len(values) == 4, 'Malformed material');put('materials', values[0], tuple(number(v) for v in values[1:]))
        elif kind == 'VCASE':
            require(len(values) == 8, 'Malformed case');put('cases', int(values[0]), tuple(number(v) for v in values[1:]))
        elif kind == 'VCELL':
            require(len(values) == 8, 'Malformed cell');put('cells', tuple(map(int, values[:3])), tuple(number(v) for v in values[3:]))
        elif kind == 'VMASS':
            require(len(values) == 5, 'Malformed mass');put('masses', (*map(int, values[:3]), values[3]), number(values[4]))
        elif kind == 'VPASSIVE':
            require(len(values) == 5, 'Malformed passive');put('passives', tuple(map(int, values[:4])), number(values[4]))
        elif kind == 'VFACE':
            require(len(values) == 7, 'Malformed face');put('faces', tuple(map(int, values[:4])), (number(values[4]), scaled(*values[5:])))
        elif kind == 'VEVENT':
            require(len(values) == 12, 'Malformed event');put('events', tuple(map(int, values[:2])), (*map(int, values[2:5]), *map(number, values[5:])))
        elif kind == 'VDOF':
            require(len(values) == 12, 'Malformed degree of freedom')
            put('dofs', tuple(map(int, values[:3])), (*map(int, values[3:6]), number(values[6]), number(values[7]), scaled(*values[8:10]), scaled(*values[10:12])))
        elif kind == 'VHEATED':
            require(len(values) == 3, 'Malformed heat support');put('heated', tuple(map(int, values)), True)
        elif kind in ('VMODE', 'VSTAGE', 'VREFINE'):
            require(all(number(v).is_finite() for v in values), 'Malformed prerequisite reading')
        elif kind == 'PASS':
            terminal = line == 'PASS physical viscosity saved controls complete'
        else:
            raise ValueError('Unknown saved viscous record ' + kind)
    require(terminal and set(data['cases']) == {0, 1} and len(data['cells']) == 16 and len(data['faces']) == 48
            and len(data['events']) == 36 and len(data['dofs']) == 144 and len(data['passives']) == 32
            and len(data['masses']) == 56 and set(data['materials']) == {'nitrogen', 'oxygen', 'helium', 'water'}, 'Incomplete saved viscosity cases')
    return data


def review(data):
    checks = 0
    eps = Decimal(2) ** -52
    materials = data['materials']
    def check(value, why):
        nonlocal checks
        checks += 1;require(value, why)
    def account(terms, why):
        check(abs(sum(terms)) <= 128 * eps * sum(abs(v) for v in terms), why)
    def close(a, b, scale, why):
        check(abs(a-b) <= 128 * eps * scale, why)
    def equilibrium(u, amounts):
        capacity = sum(mass * materials[name][0] for name, mass in amounts.items())
        offset = sum(mass * materials[name][1] for name, mass in amounts.items())
        moles = sum(mass / materials[name][2] for name, mass in amounts.items() if name != 'water')
        gas_constant = number('8.31446261815324');water = amounts.get('water', Decimal(0));bulk = Decimal(2200000000)
        if not water:
            temperature = (u-offset)/capacity
            return temperature, Decimal(1), moles*gas_constant*temperature, capacity, moles
        def at(pressure):
            ratio = 1+pressure/bulk
            temperature = (u-offset-water*bulk/1000*(ratio.ln()+1/ratio-1))/capacity
            liquid = water/1000/ratio
            return pressure*(liquid-1)+moles*gas_constant*temperature, temperature, liquid
        low, high = Decimal(0), bulk
        check(at(low)[0] > 0 and at(high)[0] < 0, 'Independent viscous EOS root is not bracketed')
        for _ in range(300):
            mid = (low+high)/2
            if mid in (low, high):break
            if at(mid)[0] > 0:low = mid
            else:high = mid
        pressure = (low+high)/2;_, temperature, liquid = at(pressure)
        return temperature, 1-liquid, pressure, capacity, moles
    for case, (duration, shear, bulk, heat, entropy, wx, wy) in data['cases'].items():
        check(duration == number('.75') and shear == number('.4') and bulk == number('.2') and heat > 0 and entropy > 0, 'Wrong viscous stage profile')
        energy = [];momenta = [[], []];state = {};amounts_by_cell = {};exact = {};deposited = [Decimal(0)]*4
        for endpoint in (0, 1):
            for cell in range(4):
                u, temperature, pressure, gas, liquid = data['cells'][case, endpoint, cell]
                amounts = {name: mass for (c, e, i, name), mass in data['masses'].items() if (c, e, i) == (case, endpoint, cell)}
                check(set(amounts) == ({'nitrogen', 'oxygen', 'helium', 'water'} if case else {'nitrogen', 'oxygen', 'helium'})
                      and all(v > 0 for v in amounts.values()), 'Missing phase or positive trace')
                check(all(mass == data['masses'][case, 0, cell, name] for name, mass in amounts.items()), 'Viscosity changed species mass')
                for identity in (17, 0x80000000):
                    check(data['passives'][case, endpoint, cell, identity] == data['passives'][case, 0, cell, identity], 'Viscosity changed passive inventory')
                exact[endpoint, cell] = equilibrium(u, amounts)
                t, vg, p, _, _ = exact[endpoint, cell]
                for a, b in ((temperature, t), (pressure, p), (gas, vg), (liquid, 1-vg)):
                    check(abs(a-b) <= Decimal('2e-10')*max(abs(a), abs(b)), 'Saved viscous EOS reading disagrees with conserved state')
                amounts_by_cell[cell] = amounts
                energy.append(u if endpoint else -u)
            for axis in (0, 1):
                for face in range(6):
                    inertia, momentum = data['faces'][case, endpoint, axis, face]
                    if axis == 0:
                        x, y = face % 3, face // 3
                        neighbours = ([x-1+2*y] if x > 0 else []) + ([x+2*y] if x < 2 else [])
                    else:
                        x, y = face % 2, face // 2
                        neighbours = ([x+2*(y-1)] if y > 0 else []) + ([x+2*y] if y < 2 else [])
                    expected = sum(sum(amounts_by_cell[i].values())/2 for i in neighbours)
                    close(inertia, expected, abs(inertia)+expected, 'Face inertia ignores supporting constituent masses')
                    sign = Decimal(1) if endpoint else Decimal(-1)
                    energy.append(sign*momentum**2/(2*inertia));momenta[axis].append(sign*momentum)
                    if endpoint == 0:state[axis, face] = momentum
        event_heat = [];wall = [[], []];stress_operands = [[], []]
        for event in range(18):
            kind, location, sweep, weight, strain, inverse, impulse, q, wall_x, wall_y = data['events'][case, event]
            slot = event if event < 9 else 17-event
            expected_kind, expected_location = (slot % 2, slot // 2) if slot < 8 else (2, 3)
            check((kind, location, sweep) == (expected_kind, expected_location, int(event >= 9)), 'Wrong viscous stress order or location')
            if kind < 2:
                x, y = location % 2, location // 2
                stencil = [(0, x+3*y, -1), (0, x+1+3*y, 1), (1, x+2*y, 1 if kind == 0 else -1), (1, x+2*(y+1), -1 if kind == 0 else 1)]
                support = {location};expected_weight = shear if kind == 0 else shear/3+bulk
            else:
                stencil = [(0, 1, -1), (0, 4, 1), (1, 2, -1), (1, 3, 1)];support = {0, 1, 2, 3};expected_weight = shear
            close(weight, expected_weight, abs(weight)+abs(expected_weight), 'Stress weight does not follow symmetric Newtonian tensor')
            check({cell for (c, e, cell) in data['heated'] if (c, e) == (case, event)} == support, 'Heat goes to the wrong fluid support')
            strain_terms = [];a = Decimal(0);dofs = []
            for index, (axis, face, gradient) in enumerate(stencil):
                actual_axis, actual_face, fixed, inertia, coefficient, before, after = data['dofs'][case, event, index]
                expected_fixed = int(face % 3 in (0, 2)) if axis == 0 else int(face // 2 in (0, 2))
                check((actual_axis, actual_face, coefficient, fixed) == (axis, face, Decimal(gradient), expected_fixed), 'Asymmetric or unconstrained stress stencil')
                check(before == state[axis, face] and inertia == data['faces'][case, 0, axis, face][0], 'Stress uses stale face state or inertia')
                if fixed:check(before == after == 0, 'Viscosity moved an impermeable face')
                else:
                    strain_terms.append(coefficient*before/inertia);a += coefficient**2/inertia
                dofs.append((axis, face, fixed, inertia, coefficient, before, after))
            s = sum(strain_terms);magnitude = sum(abs(v) for v in strain_terms)
            close(strain, s, magnitude+abs(strain), 'Wrong strain contraction')
            close(inverse, a, abs(inverse)+a, 'Wrong modal inertia')
            decay = 1-(-weight*a*duration/2).exp();expected_impulse = s/a*decay
            expected_heat = s*s/(2*a)*(1-(-weight*a*duration).exp())
            close(impulse, expected_impulse, abs(impulse)+magnitude/a*decay, 'Wrong exact viscous impulse')
            close(q, expected_heat, abs(q)+magnitude*magnitude/a*(1-(-weight*a*duration).exp()), 'Wrong kinetic-to-heat transfer')
            check(q >= 0, 'Viscosity extracted heat')
            local_energy = [q];local_wall = [Decimal(0), Decimal(0)]
            for axis, face, fixed, inertia, coefficient, before, after in dofs:
                delta = -coefficient*impulse
                if fixed:local_wall[axis] -= delta
                else:account([after, -before, -delta], 'Requested viscous impulse was not represented')
                stress_operands[axis].append(-delta)
                local_energy += [after**2/(2*inertia), -before**2/(2*inertia)]
                state[axis, face] = after
            account(local_energy, 'Viscous mode lost kinetic plus heat energy')
            for axis, observed in enumerate((wall_x, wall_y)):
                close(observed, local_wall[axis], abs(observed)+abs(local_wall[axis]), 'Wrong wall reaction')
                wall[axis].append(observed)
            for cell in support:deposited[cell] += q/len(support)
            event_heat.append(q)
        account([heat, -sum(event_heat)], 'Wrong cumulative viscous heat')
        for axis, observed in enumerate((wx, wy)):
            account([observed, *(-v for v in wall[axis])], 'Wrong cumulative wall impulse')
            account([*momenta[axis], -observed, *stress_operands[axis]], 'Complete momentum/traction account failed')
            for face in range(6):check(state[axis, face] == data['faces'][case, 1, axis, face][1], 'Final canonical momentum differs from last stress event')
        entropy_min = Decimal(0);entropy_max = Decimal(0)
        for cell in range(4):
            before = data['cells'][case, 0, cell][0];after = data['cells'][case, 1, cell][0]
            account([after, -before, -deposited[cell]], 'Stored cell heating differs from supported kinetic dissipation')
            check(after >= before, 'Viscous heat cooled a cell')
            entropy_min += (after-before)/exact[1, cell][0];entropy_max += (after-before)/exact[0, cell][0]
        check(entropy_min*(1-128*eps) <= entropy <= entropy_max*(1+128*eps), 'Claimed entropy lower bound exceeds conserved-state heating bounds')
        account(energy, 'Complete stored U+K account failed')
    return checks


def corruptions(data):
    changes = [
        lambda d: d['cells'].__setitem__((1, 1, 0), (d['cells'][1, 1, 0][0]+1000, *d['cells'][1, 1, 0][1:])),
        lambda d: d['masses'].__setitem__((1, 1, 0, 'helium'), d['masses'][1, 1, 0, 'helium']*Decimal('1.01')),
        lambda d: d['events'].__setitem__((0, 0), (*d['events'][0, 0][:3], d['events'][0, 0][3]*Decimal('1.01'), *d['events'][0, 0][4:])),
        lambda d: d['events'].__setitem__((0, 0), (*d['events'][0, 0][:6], d['events'][0, 0][6]*Decimal('1.01'), *d['events'][0, 0][7:])),
        lambda d: d['events'].__setitem__((0, 0), (*d['events'][0, 0][:7], d['events'][0, 0][7]*Decimal('1.01'), *d['events'][0, 0][8:])),
        lambda d: d['heated'].pop((1, 8, 0)),
        lambda d: d['dofs'].__setitem__((0, 0, 0), (*d['dofs'][0, 0, 0][:4], Decimal(-2), *d['dofs'][0, 0, 0][5:])),
        lambda d: d['cases'].__setitem__(1, (*d['cases'][1][:4], d['cases'][1][4]*2, *d['cases'][1][5:])),
    ]
    for change in changes:
        bad = copy.deepcopy(data);change(bad)
        try:review(bad)
        except (ValueError, KeyError):continue
        raise ValueError('Corrupted viscous operands passed')
    return len(changes)


def main():
    parser = argparse.ArgumentParser(description=__doc__);parser.add_argument('capture', type=Path);parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with localcontext() as context:
        context.prec = 90;data = parse(args.capture);checks = review(data);negative = corruptions(data)
    report = dict(kind='saved-only-Newtonian-viscosity-review', passed=True, decimal_precision=90, checks=checks,
                  corruptions_rejected=negative, capture_sha256=hashlib.sha256(args.capture.read_bytes()).hexdigest(),
                  native_code_loaded=False, actual_game_verified=False)
    result = json.dumps(report, indent=2)+'\n'
    if args.output:args.output.write_text(result)
    print(result, end='')


if __name__ == '__main__':
    main()
