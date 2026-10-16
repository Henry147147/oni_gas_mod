"""Exact rational review of captured retained products; never launches native code."""
from collections import Counter
from fractions import Fraction
import json
import math
from pathlib import Path
import struct
import sys


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key')
        result[key] = value
    return result


def invalid_constant(value):
    raise ValueError('Non-JSON constant: ' + value)


def floating(bits):
    assert isinstance(bits, str) and len(bits) == 16 and bits == bits.lower()
    return struct.unpack('>d', bytes.fromhex(bits))[0]


def number(bits):
    value = floating(bits)
    assert math.isfinite(value) and (value == 0 or abs(value) >= sys.float_info.min)
    return Fraction(value)


def witness_number(value):
    flags, bits_text, decimal = value
    assert type(flags) is int
    parsed = floating(bits_text)
    bits = int(bits_text, 16)
    exponent = (bits >> 52) & 2047
    fraction = bits & ((1 << 52) - 1)
    expected = 1 | (2 if exponent != 2047 else 0) | (4 if bits >> 63 else 0)
    expected |= 8 if exponent == 0 and fraction == 0 else 0
    expected |= 16 if exponent == 0 and fraction != 0 else 0
    assert flags == expected
    if exponent == 2047:
        assert decimal is None
    else:
        assert type(decimal) in (int, float)
        assert struct.pack('>d', float(decimal)).hex() == bits_text
    return parsed


def review(path):
    if not __debug__:
        raise RuntimeError('Run this verifier without Python -O')
    raw = Path(path).read_bytes()
    assert len(raw) <= 131072
    rows, rejects, passes = [], [], []
    for line in raw.decode('ascii').splitlines():
        if line.startswith('SCALE '):
            rows.append(json.loads(line[6:], object_pairs_hook=unique, parse_constant=invalid_constant))
        elif line.startswith('REJECT '):
            rejects.append(json.loads(line[7:], object_pairs_hook=unique, parse_constant=invalid_constant,
                                      parse_int=lambda value: -0.0 if value == '-0' else int(value)))
        elif line.startswith('PASS '):
            passes.append(line[5:])
        else:
            raise ValueError('Unexpected captured output')
    assert passes == ['signed_scaled_components', 'nonzero_product_errors', 'zero_identity_and_range_edges',
                      'typed_atomic_product_rejection', 'first_failure_is_preserved']
    assert len(rows) == 136 and Counter(row[0] for row in rows) == {'scaled': 120, 'rounding': 8, 'edge': 8}
    high_errors = low_errors = remainders = 0
    for row in rows:
        assert len(row) == 14
        high, low, coefficient, p0, p1, p2, p3, after_high, after_low, r0, r1, r2, r3 = map(number, row[1:])
        assert Fraction(float(high + low)) == high
        assert high * coefficient == p0 + p1
        assert low * coefficient == p2 + p3
        assert Fraction(float(high * coefficient)) == p0
        assert Fraction(float(low * coefficient)) == p2
        assert (high + low) * coefficient == after_high + after_low + r0 + r1 + r2 + r3
        assert Fraction(float(after_high + after_low)) == after_high
        if (high + low) * coefficient != 0:
            assert ((high + low) * coefficient) * (after_high + after_low) > 0
        else:
            assert after_high == after_low == r0 == r1 == r2 == r3 == 0
        high_errors += p1 != 0
        low_errors += p3 != 0
        remainders += any(value != 0 for value in (r0, r1, r2, r3))
    assert high_errors > 0 and low_errors > 0 and remainders > 0
    expected = ['overflow', 'subnormal-product', 'zero-product-underflow', 'low-component-underflow',
                'zero-error-underflow', 'half-minimum-error', 'subnormal-error', 'infinite-coefficient',
                'nan-coefficient', 'unsupported-rounding']
    assert [name for name, _ in rejects] == expected
    numbers = 0
    for name, witness in rejects:
        assert witness['schema'] == 'ma-canonical-failure-witness-v1'
        assert witness['failed'] is True and witness['capture_valid'] is True and witness['code'] == 2
        assert witness['model_identity'] == 'retained-scalar-product-v1'
        assert witness['stage'] == (1 if name == 'unsupported-rounding' else 14)
        assert len(witness['scalars']) <= 248 and len(witness['rows']) <= 8
        scalars = {}
        slots = 0
        for field, index, value in witness['scalars']:
            assert type(field) is int and 0 <= field < 84
            assert index is None or type(index) is int and 0 <= index < (1 << 32) - 1
            assert (field, index) not in scalars
            scalars[field, index] = witness_number(value)
            slots += 1
        for field, index, values in witness['rows']:
            assert field == 30 and type(index) is int and len(values) <= 33
            for value in values:
                witness_number(value)
            slots += len(values)
        assert slots == witness['numeric_slots'] <= 512
        numbers += slots
        if name in ('infinite-coefficient', 'nan-coefficient'):
            assert (48, None) not in scalars  # No arithmetic result was evaluated.
            assert not math.isfinite(scalars[47, 0])
        if name in ('zero-product-underflow', 'low-component-underflow'):
            assert scalars[48, None] == 0
            assert Fraction(scalars[46, 2]) * Fraction(scalars[47, 2]) != 0
            assert (47, 3) not in scalars  # FMA was not evaluated after the erased product.
        if name in ('zero-error-underflow', 'half-minimum-error'):
            assert scalars[48, None] == 0
            exact_error = Fraction(scalars[46, 2]) * Fraction(scalars[47, 2]) + Fraction(scalars[47, 3])
            assert 0 < abs(exact_error) <= Fraction(1, 2**1075)
    return {'EvidenceValid': True, 'NativeLaunchedByReview': False, 'ExactRationalProductRows': len(rows),
            'HighProductErrors': high_errors, 'LowProductErrors': low_errors, 'RowsWithExplicitRemainders': remainders,
            'TypedRejections': len(rejects), 'WitnessNumbersChecked': numbers, 'Bytes': len(raw)}


if __name__ == '__main__':
    print(json.dumps(review(sys.argv[1])))
