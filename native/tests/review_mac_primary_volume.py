#!/usr/bin/env python3
"""Review saved ideal-gas carried-volume responses without running native code."""
from __future__ import annotations
from decimal import Decimal, localcontext
from fractions import Fraction
import argparse
import copy
import json
import math
from pathlib import Path


def binary(text: str) -> Fraction:
    value = float(text)
    if not math.isfinite(value):
        raise ValueError('Nonfinite scalar')
    return Fraction.from_float(value)


def scaled(mantissa: str, exponent: str) -> Fraction:
    power = int(exponent)
    if abs(power) > 2500:
        raise ValueError('Capture exponent exceeds this reviewer scope')
    return binary(mantissa) * (Fraction(2) ** power)


def primary(text: str) -> tuple[Fraction, Fraction]:
    parts = text.split('~')
    if len(parts) == 1:
        return binary(parts[0]), Fraction(0)
    if len(parts) != 3:
        raise ValueError('Malformed primary value')
    high, low = binary(parts[0]), scaled(parts[1], parts[2])
    if not low or binary(str(float(high + low))) != high:
        raise ValueError('Noncanonical primary correction')
    return high, low


def decimal(value: Fraction) -> Decimal:
    return Decimal(value.numerator) / Decimal(value.denominator)


def parse(text: str) -> dict:
    definitions, masses, cases, errors = {}, {}, [], {}
    ended = False
    for line in text.splitlines():
        fields = line.split()
        if not fields:
            continue
        kind = fields[0]
        if ended:
            raise ValueError('Trailing records')
        if kind == 'PVDEF' and len(fields) == 5:
            key = int(fields[1])
            if key in definitions:
                raise ValueError('Duplicate material definition')
            definitions[key] = tuple(binary(v) for v in fields[2:])
        elif kind == 'PVMASS' and len(fields) == 4:
            key = int(fields[1]), int(fields[2])
            if key in masses:
                raise ValueError('Duplicate material inventory')
            masses[key] = binary(fields[3])
        elif kind == 'PVCASE' and len(fields) == 21:
            if int(fields[1]) != len(cases):
                raise ValueError('Incomplete case coverage')
            cases.append(fields)
        elif kind == 'PVERROR' and len(fields) == 4:
            errors.setdefault(int(fields[1]), []).append(scaled(fields[2], fields[3]))
        elif kind == 'PASS':
            continue
        elif fields == ['ALL', 'PASS']:
            ended = True
        else:
            raise ValueError('Unknown or malformed record')
    if not ended or len(definitions) != 3 or len(masses) != 6 or len(cases) != 80:
        raise ValueError('Capture coverage differs')
    if set(errors) - set(range(len(cases))):
        raise ValueError('Orphan storage error')
    return dict(definitions=definitions, masses=masses, cases=cases, errors=errors)


def review(data: dict) -> int:
    checks = 0

    def require(value: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not value:
            raise ValueError(message)

    with localcontext() as context:
        # Resolves the 2^-2001 correction and the unchanged tiny-entropy case.
        context.prec = 750
        gas_constant = binary('8.31446261815324')
        previous = None
        for fields in data['cases']:
            case, family = int(fields[1]), int(fields[2])
            dv = binary(fields[3])
            v0, v1, u0, u1 = map(primary, fields[4:8])
            t0, t1, pressure = map(binary, fields[8:11])
            reported_s = scaled(fields[11], fields[12])
            allowance = scaled(fields[13], fields[14])
            reported_energy = scaled(fields[15], fields[16])
            energy_bound = scaled(fields[17], fields[18])
            rounded, discarded = int(fields[19]), int(fields[20])
            full_v0, full_v1 = sum(v0), sum(v1)
            full_u0, full_u1 = sum(u0), sum(u1)
            error = sum(data['errors'].get(case, []), Fraction(0))
            require(full_v0 > 0 and full_v1 > 0, 'Nonpositive material volume')
            require(full_v0 + dv - full_v1 == error, 'Shared volume write does not close exactly')
            require(rounded == int(full_v0 + dv != v1[0]), 'High volume rounding count differs')
            require(discarded == int(error != 0), 'Discarded volume count differs')
            if error:
                # The low part has one binary64 significand even below range.
                low = abs(v1[1])
                require(low > 0, 'Discarded a representable low part')
                exponent = low.numerator.bit_length() - low.denominator.bit_length()
                if low < Fraction(2) ** exponent:
                    exponent -= 1
                require(abs(error) <= Fraction(2) ** (exponent - 53), 'Discarded volume exceeds half a low quantum')
            inventory = {key: value for (profile, key), value in data['masses'].items() if profile == family}
            require(len(inventory) == 3 and all(v > 0 for v in inventory.values()), 'Missing gas inventory')
            cv = sum((mass * data['definitions'][key][1] for key, mass in inventory.items()), Fraction(0))
            offset = sum((mass * data['definitions'][key][2] for key, mass in inventory.items()), Fraction(0))
            nr = sum((mass / data['definitions'][key][0] * gas_constant for key, mass in inventory.items()), Fraction(0))
            true_t0, true_t1 = (full_u0 - offset) / cv, (full_u1 - offset) / cv
            require(true_t0 > 0 and true_t1 > 0, 'Nonpositive caloric temperature')
            for reported, actual in [(t0, true_t0), (t1, true_t1)]:
                require(abs(reported - actual) <= 128 * Fraction(2) ** -52 * actual, 'EOS temperature differs')
            entropy = decimal(cv) * decimal(true_t1 / true_t0).ln() + decimal(nr) * decimal(full_v1 / full_v0).ln()
            # This is the original stored-U allowance, not a new entropy bound.
            original = Fraction(2) ** -45 * (abs(u0[0]) / t0 + abs(u1[0]) / t1)
            require(abs(allowance - original) <= original * 8 * Fraction(2) ** -52, 'Original entropy allowance changed')
            require(entropy >= -decimal(allowance), 'Complete stored-state entropy fails the original gate')
            require(abs(decimal(reported_s) - entropy) <= decimal(allowance) / 128, 'Reported entropy does not observe carried state')
            work = binary(str(float(pressure * dv)))
            energy = full_u1 - full_u0 + work
            operand_bound = Fraction(2) ** -45 * (abs(u0[0]) + abs(u0[1]) + abs(u1[0]) + abs(u1[1]) + abs(work))
            require(abs(energy_bound - operand_bound) <= operand_bound * 8 * Fraction(2) ** -52, 'Pressure-work energy allowance changed')
            require(abs(energy) <= energy_bound, 'Actual U plus pressure work fails')
            require(abs(reported_energy - energy) <= energy_bound / 128, 'Reported energy residual differs')
            if family == 0:
                if previous is not None:
                    require((v0, u0) == previous, 'Carried-state continuation differs')
                previous = v1, u1
                require((case % 2 == 0 and v1[1] != 0) or (case % 2 == 1 and v1 == (Fraction(1, 8), Fraction(0))),
                        'Captured volume failed its exact return')
        require(data['cases'][77][4] == data['cases'][77][5], 'Flagged idle volume changed')
        require(any(data['errors'].get(78, [])), 'Missing flagged third-part volume control')
    return checks


def corruptions(data: dict) -> int:
    changes = [
        lambda d: d['cases'][0].__setitem__(5, d['cases'][0][5].split('~')[0]),
        lambda d: d['cases'][0].__setitem__(19, '0'),
        lambda d: d['cases'][0].__setitem__(13, '0'),
        lambda d: d['cases'][0].__setitem__(12, str(int(d['cases'][0][12]) + 40)),
        lambda d: d['cases'][1].__setitem__(6, d['cases'][0][6]),
        lambda d: d['cases'][77].__setitem__(5, d['cases'][77][5].split('~')[0]),
        lambda d: d['errors'].pop(78),
        lambda d: d['cases'][78].__setitem__(20, '0'),
    ]
    for index, change in enumerate(changes):
        altered = copy.deepcopy(data)
        change(altered)
        try:
            review(altered)
        except (ValueError, ArithmeticError):
            continue
        raise ValueError(f'Altered volume capture {index} passed')
    return len(changes)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    data = parse(args.capture.read_text())
    print(json.dumps(dict(passed=True, records=80, checks=review(data), corruptions=corruptions(data))))


if __name__ == '__main__':
    main()
