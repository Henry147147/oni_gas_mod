"""Exact rational checks of captured state differences; no native launch."""
from collections import Counter
from fractions import Fraction
import importlib.util
import json
from pathlib import Path
import sys

# Reuse only the raw binary64/JSON diagnostic decoder, never its product driver.
spec = importlib.util.spec_from_file_location('retained_number_decoder', Path(__file__).with_name('review_retained_scalar_product.py'))
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


def bit_range(value):
    numerator = abs(value.numerator)
    denominator_exponent = value.denominator.bit_length() - 1
    assert value.denominator == 1 << denominator_exponent and numerator != 0
    trailing = (numerator & -numerator).bit_length() - 1
    return trailing - denominator_exponent, numerator.bit_length() - 1 - denominator_exponent


def review(path):
    if not __debug__:
        raise RuntimeError('Run this verifier without Python -O')
    raw = Path(path).read_bytes()
    assert len(raw) <= 131072
    rows, rejects, passes = [], [], []
    for line in raw.decode('ascii').splitlines():
        if line.startswith('DIFFERENCE '):
            rows.append(json.loads(line[11:], object_pairs_hook=decoder.unique, parse_constant=decoder.invalid_constant))
        elif line.startswith('REJECT '):
            rejects.append(json.loads(line[7:], object_pairs_hook=decoder.unique, parse_constant=decoder.invalid_constant,
                                      parse_int=lambda value: -0.0 if value == '-0' else int(value)))
        elif line.startswith('PASS '):
            passes.append(line[5:])
        else:
            raise ValueError('Unexpected captured output')
    assert passes == ['head_cancellation_retains_small_difference', 'low_cancellation_retains_small_difference',
                      'separated_scales_have_explicit_remainders', 'completed_update_difference_chain',
                      'zero_alias_and_identity_edges', 'typed_atomic_difference_rejection', 'first_failure_is_preserved']
    assert len(rows) == 290
    assert Counter(row[0] for row in rows) == {'head-cancellation': 20, 'low-cancellation': 20, 'separated': 160, 'chain': 64, 'edge': 26}
    counts = Counter()
    remainders = 0
    previous = (Fraction(1), Fraction(0))
    for row in rows:
        assert len(row) == 14 and type(row[7]) is int and 0 <= row[7] <= 4
        left_high, left_low, right_high, right_low, high, low = map(decoder.number, row[1:7])
        expansion = list(map(decoder.number, row[8:12]))
        rest = list(map(decoder.number, row[12:14]))
        count = row[7]
        counts[count] += 1
        assert Fraction(float(left_high + left_low)) == left_high
        assert Fraction(float(right_high + right_low)) == right_high
        assert all(value != 0 for value in expansion[:count]) and all(value == 0 for value in expansion[count:])
        exact = left_high + left_low - right_high - right_low
        assert exact == sum(expansion) == high + low + sum(rest)
        assert Fraction(float(high + low)) == high
        assert rest == expansion[:max(0, count - 2)] + [Fraction(0)] * (2 - max(0, count - 2))
        assert high + low == sum(expansion[max(0, count - 2):count])
        for a, b in zip(expansion[:count], expansion[1:count]):
            assert abs(a) < abs(b) and bit_range(a)[1] < bit_range(b)[0]
        if exact == 0:
            assert count == 0 and high == low == 0
        else:
            assert count > 0 and exact * expansion[count - 1] > 0 and exact * (high + low) > 0
        remainders += any(value != 0 for value in rest)
        if row[0] in ('head-cancellation', 'low-cancellation'):
            assert left_high == right_high and exact != 0
        if row[0] == 'chain':
            assert (right_high, right_low) == previous
            assert exact == Fraction(1, 2**70) and rest == [0, 0]
            previous = left_high, left_low
    assert sum(previous) == Fraction(1) + Fraction(1, 2**64)
    assert counts == {0: 24, 1: 106, 4: 160} and remainders == 160
    assert [name for name, _ in rejects] == ['overflow', 'subnormal-head-difference', 'subnormal-low-difference', 'unsupported-rounding']
    numbers = 0
    for name, witness in rejects:
        assert witness['schema'] == 'ma-canonical-failure-witness-v1'
        assert witness['failed'] is True and witness['capture_valid'] is True and witness['code'] == 2
        assert witness['model_identity'] == 'retained-scalar-difference-v1'
        assert witness['stage'] == (1 if name == 'unsupported-rounding' else 14)
        assert len(witness['scalars']) <= 248 and len(witness['rows']) <= 8
        scalars = {}
        slots = 0
        for field, index, value in witness['scalars']:
            assert type(field) is int and 0 <= field < 84
            assert index is None or type(index) is int and 0 <= index < (1 << 32) - 1
            assert (field, index) not in scalars
            scalars[field, index] = decoder.witness_number(value)
            slots += 1
        for field, index, values in witness['rows']:
            assert field == 30 and index is None and len(values) <= 12
            for value in values:
                decoder.witness_number(value)
            slots += len(values)
        assert slots == witness['numeric_slots'] <= 512
        numbers += slots
        if name.startswith('subnormal-'):
            exact_sum = Fraction(scalars[46, 2]) + Fraction(scalars[47, 2])
            assert exact_sum == Fraction(scalars[48, None])
            assert 0 < abs(exact_sum) < Fraction(sys.float_info.min)
        if name == 'unsupported-rounding':
            assert (48, None) not in scalars
    return {'EvidenceValid': True, 'NativeLaunchedByReview': False, 'ExactRationalDifferenceRows': len(rows),
            'ExpansionCounts': dict(counts), 'RowsWithExplicitRemainders': remainders,
            'TypedRejections': len(rejects), 'WitnessNumbersChecked': numbers, 'Bytes': len(raw)}


if __name__ == '__main__':
    print(json.dumps(review(sys.argv[1])))
