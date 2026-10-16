"""Exact review of captured retained pressure states; launches no native code.

Decode primary/geometry prefixes and the final retained-step diagnostic section.
Other complete snapshot fields remain opaque and are hashed. Reconstruct the
physical and finite accounting identities independently with exact Fractions.
"""
from collections import Counter
from fractions import Fraction
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

spec = importlib.util.spec_from_file_location('number_decoder', Path(__file__).with_name('review_retained_scalar_product.py'))
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)
N = decoder.number


def total(values):
    return sum(map(N, values), Fraction(0))


def pair_value(pair):
    return total(pair)


def neumaier(values):
    head, correction = 0.0, 0.0
    for bits in values:
        value = decoder.floating(bits)
        after = head + value
        error = (head - after) + value if abs(head) >= abs(value) else (value - after) + head
        correction = correction + error
        head = after
    return struct.pack('>d', head + correction).hex()


class Reader:
    def __init__(self, raw):
        assert 0 < len(raw) <= 2 * 1024 * 1024
        self.raw, self.offset, self.low_offsets = raw, 0, []

    def integer(self, maximum=(1 << 64) - 1):
        assert self.offset + 8 <= len(self.raw)
        value = struct.unpack_from('<Q', self.raw, self.offset)[0]
        self.offset += 8
        assert value <= maximum
        return value

    def text(self, expected=None):
        count = self.integer(256)
        assert self.offset + count <= len(self.raw)
        value = self.raw[self.offset:self.offset + count].decode('ascii')
        self.offset += count
        if expected is not None:
            assert value == expected
        return value

    def number(self):
        bits = f'{self.integer():016x}'
        N(bits)
        return bits

    def array(self, maximum, read, exact=None):
        count = self.integer(maximum)
        if exact is not None:
            assert count == exact
        return [read() for _ in range(count)]

    def pair(self):
        self.text('high')
        high = self.number()
        self.text('low')
        self.low_offsets.append((self.offset, self.raw[self.offset:self.offset + 8]))
        low = self.number()
        assert Fraction(float(N(high) + N(low))) == N(high)
        return [high, low]

    def difference(self):
        self.text('value')
        pair = self.pair()
        self.text('expansion_terms')
        expansion = self.array(4, self.number, 4)
        self.text('expansion_count')
        count = self.integer(4)
        self.text('roundoff_remainders')
        remainders = self.array(2, self.number, 2)
        assert all(N(value) != 0 for value in expansion[:count]) and not any(map(N, expansion[count:]))
        assert total(expansion) == pair_value(pair) + total(remainders)
        if count == 0:
            assert pair_value(pair) == 0
        else:
            assert pair_value(pair) * total(expansion) > 0
        return dict(pair=pair, expansion=expansion, count=count, remainders=remainders)

    def expression(self):
        self.text('terms')
        values = self.array(32768, self.number)
        assert all(N(value) != 0 for value in values)
        self.text('projection')
        projection = self.number()
        assert neumaier(values) == projection
        return dict(terms=values, projection=projection)

    def geometry(self):
        self.text('length_m')
        length = self.difference()
        self.text('volume_terms_m3')
        volume = self.array(8, self.number, 8)
        self.text('volume_term_count')
        count = self.integer(8)
        self.text('projected_volume_m3')
        projected = self.number()
        self.text('projection_residual_terms_m3')
        residual = self.array(9, self.number, 9)
        assert count > 0 and all(N(value) != 0 for value in volume[:count]) and not any(map(N, volume[count:]))
        assert residual[:8] == volume and N(residual[-1]) == -N(projected) and N(projected) > 0
        return dict(length=length, volume=volume, projection=projected, residual=residual)

    def update(self):
        self.text('name')
        name = self.text()
        self.text('node')
        node = self.integer((1 << 32) - 1)
        self.text('owner')
        owner = self.integer((1 << 32) - 1)
        self.text('before')
        before = self.pair()
        self.text('increment')
        increment = self.pair()
        self.text('update')
        update = self.difference()
        self.text('represented_change')
        change = self.difference()
        assert pair_value(before) + pair_value(increment) == total(update['expansion'])
        assert total(change['expansion']) == pair_value(update['pair']) - pair_value(before)
        if pair_value(increment) != 0:
            assert pair_value(increment) * total(change['expansion']) > 0
        return dict(name=name, node=node, owner=owner, before=before, increment=increment, update=update, change=change)

    def construction(self, n):
        self.text('node')
        node = self.integer(n - 1)
        self.text('product_terms')
        products = self.array(n, lambda: self.array(2, self.number, 2), n)
        self.text('sum_remainders')
        remainders = self.array(n, lambda: self.array(2, self.number, 2), n)
        self.text('value')
        value = self.pair()
        assert sum(map(total, products)) == pair_value(value) + sum(map(total, remainders))
        return dict(node=node, products=products, remainders=remainders, value=value)

    def named_expression(self):
        self.text('name')
        name = self.text()
        self.text('node')
        node = self.integer((1 << 32) - 1)
        self.text('owner')
        owner = self.integer((1 << 32) - 1)
        self.text('expression')
        return (name, node, owner), self.expression()

    def decode(self, n):
        self.text('ma-canonical-material-diagnostics-v3')
        self.text('primary')
        primary = {}
        for name, count in [('relative_positions_m', n), ('momentum', n), ('internal_energy_j', 7)]:
            self.text(name)
            primary[name] = self.array(count, self.pair, count)
        for name in ('origin_m', 'completed_time_seconds'):
            self.text(name)
            primary[name] = self.pair()
        primary_lows = self.low_offsets[:]
        self.text('owner_geometry')
        geometry = self.array(7, self.geometry, 7)
        self.text('registry')
        prefix_bytes = self.offset
        tag = struct.pack('<Q', len('retained')) + b'retained'
        assert self.raw.count(tag) == 1
        offset = self.raw.index(tag)
        assert offset > prefix_bytes
        self.offset = offset
        self.text('retained')
        retained = {}
        for name in ('origin_change', 'time_change'):
            self.text(name)
            retained[name] = self.difference()
        for name in ('relative_position_change', 'momentum_change', 'velocity_change', 'internal_energy_change'):
            self.text(name)
            retained[name] = self.array(n if name != 'internal_energy_change' else 7, self.difference)
        for name in ('displacement', 'volume_change'):
            self.text(name)
            retained[name] = self.array(n if name == 'displacement' else 7, self.expression)
        self.text('updates')
        retained['updates'] = self.array(256, self.update)
        self.text('constrained_momentum')
        retained['constructions'] = self.array(2, lambda: self.construction(n))
        self.text('expressions')
        values = self.array(1024, self.named_expression)
        assert len(dict(values)) == len(values)
        retained['expressions'] = dict(values)
        assert self.offset == len(self.raw)
        return primary, geometry, retained, primary_lows, prefix_bytes, len(self.raw) - offset


def check_case(case, data, raw_before, raw_after):
    n, species = data['meta']['nodes'], data['meta']['species']
    assert n == 9 + case and species == (4 if case == 0 else 2)
    p0, g0, _, _, prefix0, suffix0 = Reader(raw_before).decode(n)
    p1, g1, ledger, low_offsets, prefix1, suffix1 = Reader(raw_after).decode(n)
    h, area = N(data['meta']['h']), N(data['meta']['area'])
    u0, u1 = [N(row[3]) for row in data['nodes0']], [N(row[3]) for row in data['nodes1']]
    z, exterior, reaction, force = ([N(row[i]) for row in data['nodes1']] for i in range(4, 8))
    x0, x1 = ([pair_value(pair) for pair in p[name]] for p, name in [(p0, 'relative_positions_m'), (p1, 'relative_positions_m')])
    pi0, pi1 = ([pair_value(pair) for pair in p['momentum']] for p in (p0, p1))
    origin0, origin1 = pair_value(p0['origin_m']), pair_value(p1['origin_m'])
    dx = [origin1 - origin0 + b - a for a, b in zip(x0, x1)]
    dpi, duv, mean = [b - a for a, b in zip(pi0, pi1)], [b - a for a, b in zip(u0, u1)], [(a + b) / 2 for a, b in zip(u0, u1)]
    matrix = [[N(data['rows'][j * n + p][3]) for p in range(n)] for j in range(n)]
    assert all(matrix[j][p] == matrix[p][j] and matrix[j][p] >= 0 for j in range(n) for p in range(n))
    assert all(matrix[j][p] == 0 for j in range(n) for p in range(n) if abs(j - p) > 1)
    multiply = lambda vector: [sum((a * b for a, b in zip(row, vector)), Fraction(0)) for row in matrix]
    lu0, lu1, ldu = multiply(u0), multiply(u1), multiply(duv)
    c0, c1 = [a - b for a, b in zip(lu0, pi0)], [a - b for a, b in zip(lu1, pi1)]
    absent = (1 << 32) - 1
    expressions = ledger['expressions']
    checked = 0

    def expect(name, value, node=absent, owner=absent, summary=None):
        nonlocal checked
        record = expressions[name, node, owner]
        assert total(record['terms']) == value, (case, name, node, owner)
        if summary is not None:
            assert record['projection'] == data['summary'][summary]
        checked += 1
        return value

    for name, expected in [('origin_change', origin1 - origin0), ('time_change', pair_value(p1['completed_time_seconds']) - pair_value(p0['completed_time_seconds']))]:
        assert total(ledger[name]['expansion']) == expected
    assert total(ledger['time_change']['expansion']) == h
    if case != 1:
        assert p0['completed_time_seconds'][0] == p1['completed_time_seconds'][0] and N(p1['completed_time_seconds'][1]) != 0
    for name, expected in [('relative_position_change', [b - a for a, b in zip(x0, x1)]), ('momentum_change', dpi), ('velocity_change', duv),
                           ('internal_energy_change', [pair_value(b) - pair_value(a) for a, b in zip(p0['internal_energy_j'], p1['internal_energy_j'])])]:
        assert len(ledger[name]) == len(expected)
        assert [total(value['expansion']) for value in ledger[name]] == expected
    assert [total(value['terms']) for value in ledger['displacement']] == dx
    assert [origin0 + value for value in x0] == [N(row[2]) for row in data['inputs']]
    if case == 2:
        assert any(a[0] == b[0] and pair_value(a) != pair_value(b) for a, b in zip(p0['relative_positions_m'], p1['relative_positions_m']))
    constrained = {j for j in range(n) if j == 0 and data['meta']['left_kind'] != 0 or j == n - 1 and data['meta']['right_kind'] != 0}
    for j in constrained:
        assert dx[j] == z[j]
    pwork = [N(row[12]) for row in data['cells1']]
    pressure = [Fraction(0) for _ in range(n)]
    pressure_magnitude = [Fraction(0) for _ in range(n)]
    owner_nodes = [(min(e for e in range(n - 1) if e * 7 // (n - 1) == i),
                    1 + max(e for e in range(n - 1) if e * 7 // (n - 1) == i)) for i in range(7)]
    internal_changes, owner_defects = [], []
    for i, (left, right) in enumerate(owner_nodes):
        for primary, geometry, cells in [(p0, g0, data['cells0']), (p1, g1, data['cells1'])]:
            expected_length = pair_value(primary['relative_positions_m'][right]) - pair_value(primary['relative_positions_m'][left])
            assert total(geometry[i]['length']['expansion']) == expected_length > 0
            assert total(geometry[i]['volume']) == area * expected_length
            assert geometry[i]['projection'] == cells[i][3]
        assert data['cells0'][i][8:12] == data['cells1'][i][8:12]
        assert all(N(value) > 0 for value in data['cells1'][i][8:8 + species])
        delta_volume = total(g1[i]['volume']) - total(g0[i]['volume'])
        assert total(ledger['volume_change'][i]['terms']) == delta_volume
        delta_u = pair_value(p1['internal_energy_j'][i]) - pair_value(p0['internal_energy_j'][i])
        internal_changes.append(delta_u)
        incidence = area * (dx[right] - dx[left])
        cap = Fraction(float(area * pwork[i]))
        pressure[left] -= cap
        pressure[right] += cap
        pressure_magnitude[left] += cap
        pressure_magnitude[right] += cap
        left_work, right_work = -cap * dx[left], cap * dx[right]
        owner_defects.append(delta_u + left_work + right_work)
        expect('owner.incidence-volume', incidence, owner=i)
        expect('owner.left-work', left_work, owner=i)
        expect('owner.right-work', right_work, owner=i)
        expect('owner.cap-work-defect', left_work + right_work - pwork[i] * incidence, owner=i)
        expect('owner.work-defect', owner_defects[-1], owner=i)
        expect('owner.stored-energy-defect', delta_u - N(data['cells1'][i][13]), owner=i)
        expect('owner.secant-defect', N(data['cells1'][i][13]) + pwork[i] * N(data['cells1'][i][14]), owner=i)
        expect('owner.volume-work-defect', pwork[i] * (incidence - N(data['cells1'][i][14])), owner=i)
    rpi = [dpi[j] - h * force[j] for j in range(n)]
    kinematic = [dx[j] - h * mean[j] for j in range(n)]
    assembly = [force[j] - exterior[j] - reaction[j] - pressure[j] for j in range(n)]
    for j in range(n):
        expect('node.compatibility0', c0[j], node=j)
        expect('node.compatibility1', c1[j], node=j)
        expect('node.mass-delta-velocity', ldu[j], node=j)
        required = ldu[j] + c0[j] - c1[j] if j in constrained else h * force[j]
        expect('node.requested-impulse', required, node=j)
        expect('node.momentum-residual', rpi[j], node=j)
        expect('node.kinematic-defect', kinematic[j], node=j)
        expect('node.force-assembly-defect', assembly[j], node=j)
        if j not in constrained:
            expect('node.free-addition-defect', dpi[j] - required, node=j)
        else:
            linearity = ldu[j] + lu0[j] - lu1[j]
            expect('node.constrained-linearity-defect', linearity, node=j)
            expect('node.constrained-join-defect', required - dpi[j] - linearity, node=j)
            expect('node.constrained-impulse-defect', required - dpi[j], node=j)
        expect('node.reaction-impulse-defect', h * (pressure[j] + exterior[j] + reaction[j]) - required, node=j)
    independent_force_ratio = Fraction(0)
    for j in range(n):
        if j in constrained:
            continue
        free = [p for p in range(n) if p not in constrained]
        lz = sum(matrix[j][p] * z[p] for p in free)
        lu = sum(matrix[j][p] * u0[p] for p in free)
        coupling = sum(matrix[j][p] * u1[p] for p in constrained)
        residual = 2 * lz / h**2 - (lu + pi0[j]) / h + coupling / h - pressure[j] - exterior[j]
        scale = (2 * sum(abs(matrix[j][p] * z[p]) for p in free) / h**2
                 + sum(abs(matrix[j][p] * u0[p]) for p in free) / h
                 + sum(abs(N(value)) for value in p0['momentum'][j]) / h
                 + sum(abs(matrix[j][p] * u1[p]) for p in constrained) / h
                 + pressure_magnitude[j] + abs(exterior[j]))
        assert scale > 0 and abs(residual) <= Fraction(2, 10**12) * scale
        independent_force_ratio = max(independent_force_ratio, abs(residual) / scale)
    k0, k1 = N(data['summary']['k0']), N(data['summary']['k1'])
    for velocity, lu, kinetic in [(u0, lu0, k0), (u1, lu1, k1)]:
        exact_kinetic = sum(a * b for a, b in zip(velocity, lu)) / 2
        scale = sum(abs(velocity[j] * matrix[j][p] * velocity[p]) for j in range(n) for p in range(n)) / 2
        assert exact_kinetic >= 0 and abs(kinetic - exact_kinetic) <= Fraction(256, 2**52) * scale
    e_kinetic = k1 - k0 - sum(a * b for a, b in zip(mean, dpi))
    residual_work = sum(a * b for a, b in zip(mean, rpi))
    kinematic_work = -sum(a * b for a, b in zip(kinematic, force))
    assembly_work = sum(a * b for a, b in zip(dx, assembly))
    ports = [exterior[0] * dx[0], exterior[-1] * dx[-1], reaction[0] * z[0], reaction[-1] * z[-1]]
    energy_error = k1 - k0 + sum(internal_changes) - sum(ports)
    accounted = residual_work + kinematic_work + e_kinetic + sum(owner_defects) + assembly_work
    assert energy_error == accounted
    expect('energy.kinetic-defect', e_kinetic)
    expect('energy.momentum-residual-work', residual_work)
    expect('energy.kinematic-defect-work', kinematic_work)
    expect('energy.force-assembly-work', assembly_work)
    expect('energy.physical-error', energy_error, summary='energy_error')
    expect('energy.accounted-error', accounted)
    expect('energy.accounting-remainder', Fraction(0), summary='energy_remainder')
    momentum_error = sum(dpi) - h * (exterior[0] + exterior[-1] + reaction[0] + reaction[-1])
    expect('momentum.physical-error', momentum_error, summary='momentum_error')
    expect('momentum.assembly-impulse', h * sum(assembly))
    expect('momentum.accounting-remainder', Fraction(0), summary='momentum_remainder')
    weights = list(map(sum, matrix))
    first_change = sum(a * b for a, b in zip(weights, dx))
    first_error = first_change - h * sum((a + b) / 2 for a, b in zip(pi0, pi1))
    expect('first-moment.change', first_change, summary='first_change')
    expect('first-moment.error', first_error, summary='first_error')
    expect('first-moment.accounting-remainder', Fraction(0), summary='first_remainder')
    relative = sum(b * l1 - a * l0 for a, b, l0, l1 in zip(x0, x1, lu0, lu1))
    physical = sum((origin1 + b) * l1 - (origin0 + a) * l0 for a, b, l0, l1 in zip(x0, x1, lu0, lu1))
    expect('bilinear.relative-change', relative, summary='relative_bilinear')
    expect('bilinear.physical-change', physical, summary='bilinear_change')
    expect('bilinear.accounting-remainder', Fraction(0), summary='bilinear_remainder')
    for prefix in ('energy', 'momentum', 'first', 'bilinear'):
        assert abs(N(data['summary'][prefix + '_remainder'])) <= N(data['summary'][prefix + '_bound'])
    assert abs(N(data['summary']['energy_error'])) <= N(data['summary']['energy_budget'])
    assert N(data['summary']['force_ratio']) <= Fraction(2, 10**12)
    assert N(data['summary']['thermo_ratio']) <= Fraction(2, 10**10) and N(data['summary']['work_ratio']) <= Fraction(2, 10**10)
    anchor = 0 if data['meta']['left_kind'] == 1 else n - 1 if data['meta']['right_kind'] == 1 else n // 2
    seen = set()
    for update in ledger['updates']:
        name, node, owner = update['name'], update['node'], update['owner']
        assert (name, node, owner) not in seen
        seen.add((name, node, owner))
        if name == 'origin':
            before, after, increment = p0['origin_m'], p1['origin_m'], z[anchor]
        elif name == 'relative-position':
            before, after, increment = p0['relative_positions_m'][node], p1['relative_positions_m'][node], z[node] - z[anchor]
        elif name == 'free-impulse':
            assert node not in constrained
            before, after, increment = p0['momentum'][node], p1['momentum'][node], h * force[node]
        elif name == 'internal-energy':
            before, after, increment = p0['internal_energy_j'][owner], p1['internal_energy_j'][owner], N(data['cells1'][owner][13])
        else:
            assert name == 'completed-time'
            before, after, increment = p0['completed_time_seconds'], p1['completed_time_seconds'], h
        assert update['before'] == before and update['update']['pair'] == after and pair_value(update['increment']) == increment
    assert Counter(name for name, _, _ in seen) == {'origin': 1, 'relative-position': n, 'free-impulse': n - len(constrained), 'internal-energy': 7, 'completed-time': 1}
    assert {row['node'] for row in ledger['constructions']} == constrained
    for row in ledger['constructions']:
        j = row['node']
        assert row['value'] == p1['momentum'][j]
        assert [total(value) for value in row['products']] == [matrix[j][p] * u1[p] for p in range(n)]
    low_offset = next(offset for offset, bits in low_offsets if struct.unpack('<d', bits)[0] != 0)
    return dict(Case=case, Nodes=n, Species=species, ExactExpressionIdentities=checked, StoredUpdateCertificates=len(seen),
                NamedExpressions=len(expressions), ExpressionTerms=sum(len(value['terms']) for value in expressions.values()),
                IndependentForceRatio=float(independent_force_ratio),
                BeforeSha256=hashlib.sha256(raw_before).hexdigest(), AfterSha256=hashlib.sha256(raw_after).hexdigest(),
                BeforeBytes=len(raw_before), AfterBytes=len(raw_after), DecodedPrefixBytes=[prefix0, prefix1],
                DecodedRetainedSuffixBytes=[suffix0, suffix1], CompleteSnapshotSemanticsDecoded=False), low_offset


def review(path, directory):
    assert __debug__, 'Run this verifier without Python -O'
    raw = Path(path).read_bytes()
    assert len(raw) <= 131072
    records = {name: [] for name in ('CASE', 'NODE', 'CELL', 'INPUT', 'ROW', 'SUMMARY', 'WITNESS')}
    passes = []
    for line in raw.decode('ascii').splitlines():
        name, content = line.split(' ', 1)
        if name == 'PASS':
            passes.append(content)
        else:
            records[name].append(json.loads(content, object_pairs_hook=decoder.unique, parse_constant=decoder.invalid_constant,
                                            parse_int=lambda text: -0.0 if text == '-0' else int(text)))
    assert passes == ['four_species_retained_pressure', 'gas_liquid_prescribed_port', 'small_retained_geometry_and_time', 'typed_atomic_step_rejections']
    assert [row['case'] for row in records['CASE']] == [0, 1, 2] and [row['case'] for row in records['SUMMARY']] == [0, 1, 2]
    cases, corruptions = [], 0
    for case, meta in enumerate(records['CASE']):
        n = meta['nodes']
        data = dict(meta=meta, summary=records['SUMMARY'][case])
        for kind in ('NODE', 'CELL'):
            for endpoint in (0, 1):
                values = [row for row in records[kind] if row[:2] == [case, endpoint]]
                count = n if kind == 'NODE' else 7
                length = (4 if endpoint == 0 else 8) if kind == 'NODE' else (12 if endpoint == 0 else 17)
                assert len(values) == count and all(row[:3] == [case, endpoint, j] and len(row) == length for j, row in enumerate(values))
                data[kind.lower() + 's' + str(endpoint)] = values
        data['inputs'] = [row for row in records['INPUT'] if row[0] == case]
        data['rows'] = [row for row in records['ROW'] if row[0] == case]
        assert len(data['inputs']) == n and all(row[:2] == [case, j] and len(row) == 3 for j, row in enumerate(data['inputs']))
        assert len(data['rows']) == n * n and all(row[:3] == [case, j, p] and len(row) == 4
                                                for row, (j, p) in zip(data['rows'], ((j, p) for j in range(n) for p in range(n))))
        before, after = ((Path(directory) / f'case-{case}-{endpoint}.bin').read_bytes() for endpoint in ('before', 'after'))
        report, low_offset = check_case(case, data, before, after)
        cases.append(report)
        corrupt = bytearray(after)
        corrupt[low_offset] ^= 1
        try:
            check_case(case, data, before, corrupt)
        except (AssertionError, ValueError):
            corruptions += 1
        else:
            raise AssertionError('A corrupted normal primary low bit was accepted')
    assert [row[0] for row in records['WITNESS']] == ['zero-step', 'alias', 'unsupported-rounding']
    numbers = 0
    for index, (_, witness) in enumerate(records['WITNESS']):
        assert witness['failed'] is True and witness['capture_valid'] is True and witness['completed_step'] == 1
        assert witness['model_identity'] == 'retained-pressure-first-controls' and witness['code'] == (1 if index < 2 else 2)
        assert witness['stage'] == [6, 13, 1][index]
        assert witness['context']['constitutive_charges'] == witness['context']['factorizations'] == 0
        slots = 0
        for field, _, value in witness['scalars']:
            assert 0 <= field < 88
            decoder.witness_number(value)
            slots += 1
        for field, _, values in witness['rows']:
            assert 0 <= field < 88 and len(values) <= 33
            for value in values:
                decoder.witness_number(value)
            slots += len(values)
        assert slots == witness['numeric_slots'] <= 512
        numbers += slots
    return dict(EvidenceValid=True, NativeLaunchedByReview=False, Cases=cases, CorruptedNormalLowBitsRejected=corruptions,
                WitnessNumbersChecked=numbers, OldFailedGateRetried=False, GameAcceptance=False, PerformanceAcceptance=False)


if __name__ == '__main__':
    print(json.dumps(review(sys.argv[1], sys.argv[2])))
