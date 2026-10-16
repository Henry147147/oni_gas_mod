"""Independent JSON decoding/bit checks only; never launches a native target."""
import hashlib
import json
from pathlib import Path
import struct
from decimal import Decimal, localcontext, ROUND_HALF_EVEN

import argparse

def review_bytes(raw: bytes):
    if not __debug__:
        raise RuntimeError("Run this verifier without Python -O")
    if len(raw) > 65536:
        raise ValueError("Diagnostics stdout exceeds the 64 KiB capture bound")


    def unique(pairs):
        value = {}
        for key, entry in pairs:
            if key in value:
                raise ValueError('Duplicate JSON key')
            value[key] = entry
        return value


    def constant(value):
        raise ValueError('Non-JSON numeric constant: ' + value)


    lines = raw.decode('ascii').splitlines()
    assert len(lines) == 17 and all(line.startswith('PASS ') for line in lines[:16])
    report = json.loads(lines[-1], object_pairs_hook=unique, parse_constant=constant,
                        parse_int=lambda value: -0.0 if value == '-0' else int(value))
    assert report['Passed'] is True and report['Tests'] == 16
    assert set(report) == {'Passed', 'Tests', 'MaximalWitness', 'NonfiniteWitness', 'EnvironmentTraceWitnesses', 'DecimalBoundaryWitness'}

    counts = {'numbers': 0, 'finite_roundtrips': 0, 'nonfinite_raw_values': 0}


    def number(value):
        flags, bits_text, decimal = value
        assert type(flags) is int and len(bits_text) == 16 and bits_text == bits_text.lower()
        bits = int(bits_text, 16)
        exponent = bits >> 52 & 2047
        fraction = bits & ((1 << 52) - 1)
        expected = 1 | (2 if exponent != 2047 else 0) | (4 if bits >> 63 else 0)
        expected |= 8 if exponent == 0 and fraction == 0 else 0
        expected |= 16 if exponent == 0 and fraction != 0 else 0
        assert flags == expected
        if exponent == 2047:
            assert decimal is None
            counts['nonfinite_raw_values'] += 1
        else:
            assert type(decimal) in (int, float)
            assert struct.pack('>d', float(decimal)).hex() == bits_text
            counts['finite_roundtrips'] += 1
        counts['numbers'] += 1


    def witness(value):
        assert value['schema'] == 'ma-canonical-failure-witness-v1'
        assert value['failed'] is True and value['capture_valid'] is True
        assert value['stage'] == 7 and value['code'] == 2
        assert value['context'] == {'node': 5, 'owner': 2, 'newton_updates': 3, 'backtracks': 4,
                                    'factorizations': 5, 'top_level_queries': 6, 'constitutive_charges': 7,
                                    'oracle_iterations': 8, 'predicate_sequence': 9}
        slots = 0
        assert len(value['scalars']) <= 248 and len(value['rows']) <= 8
        for field, index, entry in value['scalars']:
            assert 0 <= field < 46 and (index is None or 0 <= index < (1 << 32) - 1)
            number(entry)
            slots += 1
        for field, index, entries in value['rows']:
            assert 0 <= field < 46 and 0 <= index < (1 << 32) - 1 and len(entries) <= 33
            for entry in entries:
                number(entry)
                slots += 1
        assert value['numeric_slots'] == slots <= 512


    maximum = report['MaximalWitness']
    assert maximum['model_identity'] == '\\' * 256 and maximum['reason'] == '"' * 256
    assert maximum['completed_step'] == (1 << 64) - 1
    assert len(maximum['scalars']) == 248 and [len(row[2]) for row in maximum['rows']] == [33] * 8
    witness(maximum)
    witness(report['NonfiniteWitness'])
    assert [entry[2][1] for entry in report['NonfiniteWitness']['scalars']] == ['7ff8000000012345', 'fff0000000000000']
    assert len(report['EnvironmentTraceWitnesses']) == 3
    for trace in report['EnvironmentTraceWitnesses']:
        witness(trace)
        assert trace['scalars'][0][2][1] == '0000000000000001'
    boundary = report['DecimalBoundaryWitness']
    witness(boundary)
    expected_patterns = [sign | exponent << 52 | fraction
                         for exponent in (0, 1, 2, 997, 998, 999, 1009, 1023, 1039, 2045, 2046)
                         for fraction in (0, 1, (1 << 51) - 1, 1 << 51, (1 << 52) - 1)
                         for sign in (0, 1 << 63)]
    for center in (.1, 1e-4, 1e16, 1e17, 1e-5):
        bits = struct.unpack('>Q', struct.pack('>d', center))[0]
        expected_patterns.extend((bits - 1, bits, bits + 1))
    assert len(expected_patterns) == 125 and len(boundary['scalars']) == 125
    for index, (field, observed_index, entry) in enumerate(boundary['scalars']):
        assert field == 31 and index == observed_index and int(entry[1], 16) == expected_patterns[index]
    assert counts == {'numbers': 642, 'finite_roundtrips': 640, 'nonfinite_raw_values': 2}

    # A second parse retains exact decimal tokens instead of converting them to
    # binary64. Derive the exact rational from raw bits with Decimal, independently
    # of the C++ base-1e9 coefficient algorithm, then quantize to 17 significant
    # digits with nearest/ties-even. This checks rounding, not just round trips.
    decimal_report = json.loads(lines[-1], object_pairs_hook=unique, parse_constant=constant,
                                parse_float=Decimal, parse_int=Decimal)
    decimal_witnesses = [decimal_report['MaximalWitness'], decimal_report['NonfiniteWitness'],
                         *decimal_report['EnvironmentTraceWitnesses'], decimal_report['DecimalBoundaryWitness']]
    exact_decimal_checks = 0
    for evidence in decimal_witnesses:
        entries = [entry[2] for entry in evidence['scalars']]
        entries.extend(number for row in evidence['rows'] for number in row[2])
        for _, text, observed in entries:
            bits = int(text, 16)
            exponent = bits >> 52 & 2047
            fraction = bits & ((1 << 52) - 1)
            if exponent == 2047:
                assert observed is None
                continue
            if exponent == 0 and fraction == 0:
                expected = Decimal('-0' if bits >> 63 else '0')
            else:
                significand = fraction if exponent == 0 else (1 << 52) | fraction
                power = -1074 if exponent == 0 else exponent - 1075
                with localcontext() as context:
                    context.prec = 1200
                    exact = Decimal(significand) * Decimal(2) ** power
                    if bits >> 63:
                        exact = -exact
                    quantum = Decimal(1).scaleb(exact.adjusted() - 16)
                    expected = exact.quantize(quantum, rounding=ROUND_HALF_EVEN)
            assert isinstance(observed, Decimal) and observed == expected
            assert observed.is_signed() == bool(bits >> 63)
            exact_decimal_checks += 1
    assert exact_decimal_checks == 640
    prefix = '"MaximalWitness":'
    start = lines[-1].index(prefix) + len(prefix)
    _, length = json.JSONDecoder().raw_decode(lines[-1][start:])
    assert length <= 32768 and len(raw) <= 65536
    result = {'Passed': True, 'NativeTargetLaunched': False, 'DecodedWitnesses': 6,
              **counts, 'MaximalWitnessBytes': length, 'StdoutBytes': len(raw),
              'ExactDecimalRoundingChecks': exact_decimal_checks, 'BoundaryPatterns': len(expected_patterns),
              'StdoutSha256': hashlib.sha256(raw).hexdigest()}
    return result


def main():
    parser = argparse.ArgumentParser(description="Decode canonical diagnostic fixtures and verify binary64 bits and exact decimal rounding; launches no native code.")
    parser.add_argument("capture", type=Path, help="stdout capture from ma_canonical_diagnostics_tests")
    arguments = parser.parse_args()
    with arguments.capture.open("rb") as capture_stream:
        raw = capture_stream.read(65537)
    print(json.dumps(review_bytes(raw)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
