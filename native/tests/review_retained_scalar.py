"""Check retained-scalar raw results using exact rational arithmetic; no native launch."""
import json
import math
from pathlib import Path
import struct
import sys
from fractions import Fraction


def number(bits):
    assert isinstance(bits, str) and len(bits) == 16
    value = struct.unpack('>d', bytes.fromhex(bits))[0]
    assert math.isfinite(value)
    assert value == 0 or abs(value) >= sys.float_info.min
    return Fraction(value)


def review(path):
    raw = Path(path).read_bytes()
    assert len(raw) <= 65536
    rows = []; rejects = []; passes = []
    for line in raw.decode('ascii').splitlines():
        if line.startswith('ROW '): rows.append(json.loads(line[4:]))
        elif line.startswith('REJECT '): rejects.append(json.loads(line[7:]))
        elif line.startswith('PASS '): passes.append(line[5:])
        else: raise AssertionError('Unexpected output')
    assert len(rows) == 124 and len(rejects) == 7
    assert passes == ['separated_normal_increments', 'exact_cancellation', 'explicit_third_component', 'repeated_sub_ulp_storage', 'typed_atomic_rejection']
    groups = {'separated': 0, 'cancellation': 0, 'remainder': 0, 'accumulate': 0}
    third = 0; predecessor = (Fraction(1), Fraction(0))
    for row in rows:
        assert len(row) == 9 and row[0] in groups
        groups[row[0]] += 1
        old_high, old_low, increment, high, low, delta_high, delta_low, remainder = map(number, row[1:])
        assert old_high + old_low + increment == high + low + remainder
        assert high + low - old_high - old_low == delta_high + delta_low
        assert increment - remainder == delta_high + delta_low
        assert increment != 0 and (delta_high + delta_low) * increment > 0
        # Normalized high must be the nearest binary64 to the actual stored pair.
        assert Fraction(float(high + low)) == high
        third += remainder != 0
        if row[0] == 'accumulate':
            assert (old_high, old_low) == predecessor
            predecessor = (high, low)
    assert groups == {'separated': 40, 'cancellation': 10, 'remainder': 10, 'accumulate': 64}
    assert third > 0 and sum(predecessor) == Fraction(1) + Fraction(1, 2**64)
    for witness in rejects:
        assert witness['failed'] and witness['capture_valid'] and witness['code'] == 2
        assert witness['model_identity'] == 'retained-scalar-v1' and witness['numeric_slots'] <= 512
        assert witness['stage'] in (1, 14)
    return {'EvidenceValid': True, 'NativeLaunchedByReview': False, 'ExactRationalRows': len(rows),
            'ExplicitNonzeroRemainders': third, 'TypedRejections': len(rejects), 'Bytes': len(raw)}


if __name__ == '__main__':
    print(json.dumps(review(sys.argv[1])))
