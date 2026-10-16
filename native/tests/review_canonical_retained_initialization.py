"""Decode captured retained-owner initialization evidence; never launch native code.

The binary decoder covers the primary values and owner geometry prefix of the
complete diagnostic snapshot. Other diagnostic fields remain opaque and hashed.
EOS results are checked as scalar projections, not as exact retained EOS solves.
"""
from collections import Counter
from fractions import Fraction
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

spec = importlib.util.spec_from_file_location('retained_number_decoder', Path(__file__).with_name('review_retained_scalar_product.py'))
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


class SnapshotPrefix:
    def __init__(self, raw):
        assert 0 < len(raw) <= 2 * 1024 * 1024
        self.raw, self.offset = raw, 0
        self.low_offsets = []

    def integer(self):
        assert self.offset + 8 <= len(self.raw)
        result = struct.unpack_from('<Q', self.raw, self.offset)[0]
        self.offset += 8
        return result

    def text(self, expected):
        length = self.integer()
        assert length <= 256 and self.offset + length <= len(self.raw)
        assert self.raw[self.offset:self.offset + length] == expected.encode('ascii')
        self.offset += length

    def number(self):
        bits = f'{self.integer():016x}'
        decoder.number(bits)
        return bits

    def array(self, count, read):
        assert self.integer() == count
        return [read() for _ in range(count)]

    def pair(self):
        self.text('high')
        high = self.number()
        self.text('low')
        self.low_offsets.append(self.offset)
        low = self.number()
        assert Fraction(float(decoder.number(high) + decoder.number(low))) == decoder.number(high)
        return [high, low]

    def geometry(self):
        self.text('length_m')
        self.text('value')
        pair = self.pair()
        self.text('expansion_terms')
        expansion = self.array(4, self.number)
        self.text('expansion_count')
        count = self.integer()
        self.text('roundoff_remainders')
        remainders = self.array(2, self.number)
        self.text('volume_terms_m3')
        volume = self.array(8, self.number)
        self.text('volume_term_count')
        volume_count = self.integer()
        self.text('projected_volume_m3')
        projected = self.number()
        self.text('projection_residual_terms_m3')
        residual = self.array(9, self.number)
        return dict(pair=pair, expansion=expansion, count=count, remainders=remainders,
                    volume=volume, volume_count=volume_count, projected=projected, residual=residual)

    def decode(self):
        self.text('ma-canonical-material-diagnostics-v2')
        self.text('primary')
        primary = {}
        for name, count in [('relative_positions_m', 8), ('momentum', 8), ('internal_energy_j', 7)]:
            self.text(name)
            primary[name] = self.array(count, self.pair)
        for name in ('origin_m', 'completed_time_seconds'):
            self.text(name)
            primary[name] = self.pair()
        self.text('owner_geometry')
        geometry = self.array(7, self.geometry)
        self.text('registry')
        assert self.offset < len(self.raw)
        return primary, geometry


def check_geometry(g, area, length):
    pair, expansion, rest, volume, residual = (list(map(decoder.number, g[name]))
                                            for name in ('pair', 'expansion', 'remainders', 'volume', 'residual'))
    count, volume_count = g['count'], g['volume_count']
    assert 1 <= count <= 4 and 1 <= volume_count <= 8
    assert all(expansion[:count]) and not any(expansion[count:])
    assert all(volume[:volume_count]) and not any(volume[volume_count:])
    assert length > 0 and sum(expansion) == length == sum(pair) + sum(rest)
    assert sum(volume) == area * length
    assert g['residual'][:8] == g['volume']
    projected = decoder.number(g['projected'])
    assert projected > 0 and residual[-1] == -projected
    assert sum(residual) == area * length - projected
    assert abs(projected - area * length) <= Fraction(2, 2**52) * area * length


def check_case(meta, nodes, cells, rows, raw):
    parsed = SnapshotPrefix(raw)
    primary, geometry = parsed.decode()
    case = meta['case']
    area = decoder.number(meta['area'])
    assert area == Fraction(1.3 if case == 0 else .625)
    assert primary['origin_m'] == ['3ff0000000000000', '0000000000000000']
    assert primary['completed_time_seconds'] == ['3fc0000000000000', '0000000000000000']
    expected = [Fraction(-1), -Fraction(1, 2**60), Fraction(0), Fraction(1, 4),
                Fraction(1), Fraction(5, 4), Fraction(2), Fraction(3)]
    assert [decoder.number(row[2]) for row in nodes] == expected
    assert nodes[1][3] == nodes[2][3]
    for j, row in enumerate(nodes):
        assert row[3] == row[4] and row[8] == row[6]
        assert primary['relative_positions_m'][j] == row[4:6]
        assert primary['momentum'][j] == row[6:8]
        assert sum(map(decoder.number, row[4:6])) + 1 == expected[j]
        requested = Fraction(0) if case == 0 else Fraction(3 + j, 32)
        assert decoder.number(row[9]) == requested
        assert abs(decoder.number(row[10]) - requested) <= Fraction(256, 2**52) * abs(requested)
    for i, cell in enumerate(cells):
        assert primary['internal_energy_j'][i] == cell[3:5] and cell[3] == cell[5]
        if case == 1:
            assert cell[2] == cell[3] and decoder.number(cell[4]) == 0
        check_geometry(geometry[i], area, expected[i + 1] - expected[i])
        assert cell[6] == geometry[i]['projected']
        volume, temperature, pressure, mass = map(decoder.number, cell[6:10])
        assert mass > 0 and mass == volume
        assert abs(temperature - 305) <= Fraction(2, 10**10) * 305
        represented_u = decoder.number(cell[3]) + decoder.number(cell[4])
        projected_u = mass * (decoder.number(meta['offset']) + decoder.number(meta['cv']) * temperature)
        assert abs(represented_u - projected_u) <= Fraction(2, 10**10) * abs(projected_u)
        ideal_pressure = mass * decoder.number(meta['gas_constant']) * temperature / (decoder.number(meta['molar_mass']) * volume)
        assert pressure > 0 and abs(pressure - ideal_pressure) <= Fraction(2, 10**10) * ideal_pressure
    matrix = [[None] * 8 for _ in range(8)]
    for j in range(8):
        products, remainders = Fraction(0), Fraction(0)
        for p in range(8):
            row = rows[j * 8 + p]
            coefficient, velocity, high, low, r0, r1 = map(decoder.number, row[3:])
            assert velocity == decoder.number(nodes[p][9])
            assert coefficient >= 0 and coefficient * velocity == high + low
            assert Fraction(float(coefficient * velocity)) == high
            products += high + low
            remainders += r0 + r1
            matrix[j][p] = coefficient
        assert products == sum(map(decoder.number, primary['momentum'][j])) + remainders
        assert matrix[j][j] > 0
    assert all(matrix[j][p] == matrix[p][j] for j in range(8) for p in range(8))
    if case == 1:
        assert any(decoder.number(pair[1]) != 0 for pair in primary['momentum'])
    return {'Case': case, 'SnapshotSha256': hashlib.sha256(raw).hexdigest(), 'SnapshotBytes': len(raw),
            'DecodedPrefixBytes': parsed.offset, 'CompleteSnapshotSemanticsDecoded': False,
            'TinyPositiveVolume': float(decoder.number(geometry[1]['projected']))}, parsed.low_offsets[1]


def check_witness(name, witness):
    assert witness['schema'] == 'ma-canonical-failure-witness-v1'
    assert witness['failed'] is True and witness['capture_valid'] is True
    assert witness['model_identity'] == 'retained-owner-init-first' and witness['completed_step'] == 0
    assert witness['code'] == (1 if name == 'GUARD' else 2)
    assert witness['stage'] == (13 if name == 'GUARD' else 3)
    context = witness['context']
    assert context['factorizations'] == context['constitutive_charges'] == context['top_level_queries'] == 0
    assert context['newton_updates'] == context['backtracks'] == context['oracle_iterations'] == 0
    assert context['node'] == context['owner'] == (None if name == 'GUARD' else 0)
    assert 0 < context['predicate_sequence']
    assert len(witness['scalars']) <= 248 and len(witness['rows']) <= 8
    scalars, row_keys, slots = {}, set(), 0
    for field, index, value in witness['scalars']:
        assert type(field) is int and 0 <= field < 88
        assert index is None or type(index) is int and 0 <= index < (1 << 32) - 1
        assert (field, index) not in scalars
        scalars[field, index] = decoder.witness_number(value)
        slots += 1
    for field, index, values in witness['rows']:
        assert type(field) is int and 0 <= field < 88 and (field, index) not in row_keys
        assert index is None or type(index) is int and 0 <= index < (1 << 32) - 1
        assert len(values) <= 33
        row_keys.add((field, index))
        for value in values:
            decoder.witness_number(value)
        slots += len(values)
    assert slots == witness['numeric_slots'] <= 512
    if name == 'RELAY':
        left, right = Fraction(scalars[46, 2]), Fraction(scalars[47, 2])
        assert left != 0 and right != 0 and 0 < abs(left * right) < Fraction(sys.float_info.min)
    return slots


def review(path, directory):
    if not __debug__:
        raise RuntimeError('Run this verifier without Python -O')
    raw = Path(path).read_bytes()
    assert len(raw) <= 131072
    records, passes = {key: [] for key in ('META', 'NODE', 'CELL', 'ROW', 'GUARD', 'RELAY')}, []
    for line in raw.decode('ascii').splitlines():
        name, content = line.split(' ', 1)
        if name == 'PASS':
            passes.append(content)
        else:
            records[name].append(json.loads(content, object_pairs_hook=decoder.unique, parse_constant=decoder.invalid_constant,
                                            parse_int=lambda value: -0.0 if value == '-0' else int(value)))
    assert passes == ['retained_geometry_initialization', 'retained_initial_momentum_and_supplied_u',
                      'owned_view_and_control_independent_diagnostics', 'closed_step_boundary_and_relay_atomicity']
    assert {name: len(values) for name, values in records.items()} == dict(META=2, NODE=16, CELL=14, ROW=128, GUARD=1, RELAY=1)
    assert [meta['case'] for meta in records['META']] == [0, 1]
    cases, corruptions = [], 0
    for case, meta in enumerate(records['META']):
        assert set(meta) == {'case', 'snapshot', 'area', 'cv', 'molar_mass', 'offset', 'gas_constant'}
        assert meta['snapshot'] == f'case-{case}.snapshot.bin'
        nodes = [row for row in records['NODE'] if row[0] == case]
        cells = [row for row in records['CELL'] if row[0] == case]
        rows = [row for row in records['ROW'] if row[0] == case]
        assert len(nodes) == 8 and all(len(row) == 11 and row[:2] == [case, j] for j, row in enumerate(nodes))
        assert len(cells) == 7 and all(len(row) == 10 and row[:2] == [case, i] for i, row in enumerate(cells))
        assert len(rows) == 64 and all(len(row) == 9 and row[:3] == [case, j, p]
                                       for row, (j, p) in zip(rows, ((j, p) for j in range(8) for p in range(8))))
        snapshot = (Path(directory) / meta['snapshot']).read_bytes()
        result, low_offset = check_case(meta, nodes, cells, rows, snapshot)
        cases.append(result)
        corrupt = bytearray(snapshot)
        corrupt[low_offset] ^= 1
        try:
            check_case(meta, nodes, cells, rows, corrupt)
        except (AssertionError, ValueError):
            corruptions += 1
        else:
            raise AssertionError('A corrupted primary low bit was accepted')
    witnesses = sum(check_witness(name, records[name][0]) for name in ('GUARD', 'RELAY'))
    return dict(EvidenceValid=True, NativeLaunchedByReview=False, EOSInitializationInvoked=True,
                PhysicalAdvanceInvoked=False, OldFailedGateRetried=False, Cases=cases,
                ExactMomentumProductRows=len(records['ROW']), WitnessNumbersChecked=witnesses,
                CorruptedSnapshotCopiesRejected=corruptions, RawOutputBytes=len(raw))


if __name__ == '__main__':
    print(json.dumps(review(sys.argv[1], sys.argv[2])))
