"""Review saved managed boundary-account operands with exact rational arithmetic."""
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import struct
import sys


def check(value, reason):
    if not value:
        raise ValueError(reason)


def decode(text):
    check(len(text) == 16 and all(c in '0123456789abcdef' for c in text), 'Invalid binary64 bits')
    value = struct.unpack('>d', bytes.fromhex(text))[0]
    check(math.isfinite(value), 'Nonfinite account operand')
    return F(value)


def review(data):
    records = [json.loads(line[8:]) for line in data.decode('utf-8-sig').splitlines() if line.startswith('ACCOUNT ')]
    names = {'tiny-tail', 'negative-tail', 'tie-even', 'tie-odd', 'negative-tie', 'subnormal',
             'normal-boundary', 'overflow-cancel', 'overflow', 'zero'}
    check(len(records) == 10 and {r['Name'] for r in records} == names, 'Missing account controls')
    overflows = 0
    for record in records:
        check(set(record) == {'Name', 'Input', 'Units', 'Representable', 'RoundedHex'}, 'Unexpected record fields')
        exact = sum((decode(value) for value in record['Input']), F(0)); units = exact * (1 << 1074)
        check(units.denominator == 1 and record['Units'] == str(units.numerator), 'Exact boundary-account operands differ')
        try:
            rounded = float(exact)
        except OverflowError:
            check(record['Representable'] is False and record['RoundedHex'] == '0000000000000000', 'Overflow was hidden as a value')
            overflows += 1
        else:
            check(record['Representable'] is True and math.isfinite(rounded), 'Finite account was reported outside range')
            expected = struct.pack('>d', rounded).hex()
            check(record['RoundedHex'] == expected, 'Boundary-account rounding differs from exact rational result')
    check(overflows == 1, 'Changed overflow control')
    return {'Passed': True, 'ExactCases': len(records), 'FiniteRoundedCases': len(records) - overflows,
            'OverflowCases': overflows, 'NativeExecution': False, 'PhysicalConservationClaim': False}


def main():
    data = Path(sys.argv[1]).read_bytes(); result = review(data); rows = data.decode('utf-8-sig').splitlines()
    index = next(i for i, line in enumerate(rows) if line.startswith('ACCOUNT ') and json.loads(line[8:])['Name'] == 'tiny-tail')
    original = json.loads(rows[index][8:]); mutations = []
    changed = dict(original); changed['Units'] = str(int(original['Units']) + 1); mutations.append(changed)
    changed = dict(original); changed['RoundedHex'] = format(int(original['RoundedHex'], 16) + 1, '016x'); mutations.append(changed)
    for corrupted in mutations:
        altered = rows.copy(); altered[index] = 'ACCOUNT ' + json.dumps(corrupted)
        try:
            review('\n'.join(altered).encode())
        except ValueError:
            continue
        raise ValueError('Corrupt account output was accepted')
    result['CorruptionRejections'] = len(mutations); result['InputSha256'] = hashlib.sha256(data).hexdigest()
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
