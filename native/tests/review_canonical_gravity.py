"""Independent exact review of the new gravity controls; no native launch."""
from fractions import Fraction
import importlib.util
import json
import math
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location('retained_step_decoder', Path(__file__).with_name('review_canonical_retained_step.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
N = base.N


def inverse(matrix):
    n = len(matrix)
    rows = [row[:] + [Fraction(i == j) for j in range(n)] for i, row in enumerate(matrix)]
    for j in range(n):
        assert rows[j][j] != 0
        factor = rows[j][j]
        rows[j] = [value / factor for value in rows[j]]
        for i in range(n):
            if i != j:
                factor = rows[i][j]
                rows[i] = [a - factor * b for a, b in zip(rows[i], rows[j])]
    assert all(rows[i][:n] == [Fraction(i == j) for j in range(n)] for i in range(n))
    return [row[n:] for row in rows]


def hydrostatic_check(data, primary0, primary1, ledger, rows):
    n = data['meta']['nodes']
    h, g, area = N(data['meta']['h']), N(data['meta']['g']), N(data['meta']['area'])
    matrix = [[N(data['rows'][j * n + p][3]) for p in range(n)] for j in range(n)]
    inv = inverse(matrix)
    weights = list(map(sum, matrix))
    assert all(N(row[3]) == 0 for row in data['nodes0'])
    assert len(rows) == 7 and all(row[:2] == [1, i] and len(row) == 7 for i, row in enumerate(rows))
    p, rho, alpha, beta, length = ([N(row[j]) for row in rows] for j in range(2, 7))
    assert p[0] == 250000 and all(value > 0 for value in p)
    eps = Fraction(1, 2**52)
    for i in range(7):
        assert abs(rho[i] - alpha[i] * p[i] - beta[i]) <= 32 * eps * rho[i]
        if i < 3:
            assert all(N(value) == 0 for value in data['cells0'][i][8:11]) and N(data['cells0'][i][11]) > 0
        else:
            assert all(N(value) > 0 for value in data['cells0'][i][8:11]) and N(data['cells0'][i][11]) == 0
        if i:
            defect = p[i - 1] - p[i] - g * (rho[i - 1] * length[i - 1] + rho[i] * length[i]) / 2
            assert abs(defect) <= 128 * eps * (abs(p[i - 1]) + abs(p[i]))
    before_pressure = [N(row[5]) for row in data['cells0']]
    initial_pressure_force = [Fraction(0)] * n
    work_pressure_force = [Fraction(0)] * n
    for i in range(7):
        initial = Fraction(float(area * before_pressure[i]))
        work = Fraction(float(area * N(data['cells1'][i][12])))
        initial_pressure_force[i] -= initial
        initial_pressure_force[i + 1] += initial
        work_pressure_force[i] -= work
        work_pressure_force[i + 1] += work
    exterior = [N(row[5]) for row in data['nodes1']]
    body = [N(row[8]) for row in data['nodes1']]
    force = [N(row[7]) for row in data['nodes1']]
    velocity = [N(row[3]) for row in data['nodes1']]
    z = [N(row[4]) for row in data['nodes1']]
    pi = [base.pair_value(pair) for pair in primary1['momentum']]
    lu = [sum(a * b for a, b in zip(row, velocity)) for row in matrix]
    lz = [sum(a * b for a, b in zip(row, z)) for row in matrix]
    f0 = [initial_pressure_force[j] + exterior[j] + body[j] for j in range(n)]
    r = [2 * lz[j] / h**2 - work_pressure_force[j] - exterior[j] - body[j] for j in range(n)]
    assembly = [force[j] - work_pressure_force[j] - exterior[j] - body[j] for j in range(n)]
    # rpi+c1 = Lu1-h*Factual, measured from complete state and decoded velocity.
    compatibility = [lu[j] - h * force[j] for j in range(n)]
    def quadratic(values):
        result = sum(values[j] * inv[j][p] * values[p] for j in range(n) for p in range(n))
        assert result >= 0
        return result
    # Measure any positive monotonicity-work defect of the finite EOS/force
    # projections. It remains explicit, using the existing 2e-10 work accuracy.
    monotonicity = sum(z[j] * (work_pressure_force[j] - initial_pressure_force[j]) for j in range(n))
    positive_monotonicity = max(Fraction(0), monotonicity)
    pressure_work_scale = sum(abs(z[j]) * (abs(work_pressure_force[j]) + abs(initial_pressure_force[j])) for j in range(n))
    assert positive_monotonicity <= Fraction(2, 10**10) * pressure_work_scale
    # The measured nonlinear equation gives ||u1||_L bounded by
    # h||F0|| + 2h||R|| + h||assembly|| + ||rpi+c1|| + sqrt(2*positive_defect).
    # Squared Cauchy with five summands avoids a numerical square-root allowance.
    squared = 5 * (h**2 * quadratic(f0) + 4 * h**2 * quadratic(r) + h**2 * quadratic(assembly) + quadratic(compatibility) + 2 * positive_monotonicity)
    for j in range(n):
        assert velocity[j]**2 <= inv[j][j] * squared
    # Initial EOS deviations are measured, not silently replaced by target p.
    pressure_errors = [abs(a - b) for a, b in zip(before_pressure, p)]
    for i, error in enumerate(pressure_errors):
        # Liquid density->pressure conditioning introduces the stated bulk scale.
        scale = p[i] + (Fraction(2200000000) if i < 3 else 0)
        assert error <= 128 * eps * scale
    assert all(base.pair_value(pair) == 0 for pair in primary0['momentum'])
    return dict(MaximumInitialPressureDeviationPa=float(max(pressure_errors)),
                MaximumInitialForceImbalanceN=float(max(map(abs, f0))),
                MaximumSpeedMPerS=float(max(map(abs, velocity))),
                ProvedMaximumSpeedBoundMPerS=math.sqrt(float(max(inv[j][j] * squared for j in range(n)))),
                PositiveFinitePressureMonotonicityWorkJ=float(positive_monotonicity),
                MassResponseBoundCheckedExactly=True)


def review(path, directory):
    assert __debug__, 'Run this verifier without Python -O'
    raw = Path(path).read_bytes()
    assert len(raw) <= 131072
    records = {key: [] for key in ('CASE', 'NODE', 'CELL', 'INPUT', 'ROW', 'SUMMARY', 'HYDRO', 'WITNESS')}
    passes = []
    for line in raw.decode('ascii').splitlines():
        name, content = line.split(' ', 1)
        if name == 'PASS':
            passes.append(content)
        else:
            records[name].append(json.loads(content, object_pairs_hook=base.decoder.unique, parse_constant=base.decoder.invalid_constant,
                                            parse_int=lambda text: -0.0 if text == '-0' else int(text)))
    assert passes == ['uniform_freefall', 'supported_hydrostatic_column', 'mixed_boundary_gravity', 'typed_atomic_gravity_rejections']
    assert [row['case'] for row in records['CASE']] == [0, 1, 2] and [row['case'] for row in records['SUMMARY']] == [0, 1, 2]
    cases, corruptions = [], 0
    for case, meta in enumerate(records['CASE']):
        n = meta['nodes']
        assert N(meta['g']) == Fraction(9.81) and N(meta['area']) == Fraction(3, 4)
        assert meta['left_kind'] == [0, 0, 1][case] and meta['right_kind'] == [0, 0, 2][case]
        data = dict(meta=meta, summary=records['SUMMARY'][case])
        for kind in ('NODE', 'CELL'):
            for endpoint in (0, 1):
                values = [row for row in records[kind] if row[:2] == [case, endpoint]]
                count = n if kind == 'NODE' else 7
                size = (4 if endpoint == 0 else 9) if kind == 'NODE' else (12 if endpoint == 0 else 17)
                assert len(values) == count and all(row[:3] == [case, endpoint, j] and len(row) == size for j, row in enumerate(values))
                data[kind.lower() + 's' + str(endpoint)] = values
        data['inputs'] = [row for row in records['INPUT'] if row[0] == case]
        data['rows'] = [row for row in records['ROW'] if row[0] == case]
        assert len(data['inputs']) == n and all(row[:2] == [case, j] and len(row) == 3 for j, row in enumerate(data['inputs']))
        assert len(data['rows']) == n * n and all(row[:3] == [case, j, p] and len(row) == 4
                                                for row, (j, p) in zip(data['rows'], ((j, p) for j in range(n) for p in range(n))))
        before, after = ((Path(directory) / f'case-{case}-{endpoint}.bin').read_bytes() for endpoint in ('before', 'after'))
        report, low_offset = base.check_case(case, data, before, after, gravity_fixture=True)
        p0, _, _, _, _, _ = base.Reader(before).decode(n)
        p1, _, ledger, _, _, _ = base.Reader(after).decode(n)
        h, g = N(meta['h']), N(meta['g'])
        if case == 0:
            expected_u = Fraction(1, 4) - g * h
            expected_z = h / 4 - g * h**2 / 2
            scale = max(Fraction(1, 4), g * h)
            velocities = [N(row[3]) for row in data['nodes1']]
            displacements = [base.total(value['terms']) for value in ledger['displacement']]
            assert all(abs(value - expected_u) <= Fraction(2, 10**10) * scale for value in velocities)
            assert all(abs(value - expected_z) <= Fraction(2, 10**10) * h * scale for value in displacements)
            report['FreefallMaximumVelocityErrorMPerS'] = float(max(abs(value - expected_u) for value in velocities))
            report['FreefallMaximumDisplacementErrorM'] = float(max(abs(value - expected_z) for value in displacements))
        elif case == 1:
            report['Hydrostatic'] = hydrostatic_check(data, p0, p1, ledger, records['HYDRO'])
        else:
            assert p0['completed_time_seconds'][0] == p1['completed_time_seconds'][0] and N(p1['completed_time_seconds'][1]) != 0
        cases.append(report)
        corrupt = bytearray(after)
        corrupt[low_offset] ^= 1
        try:
            base.check_case(case, data, before, corrupt, gravity_fixture=True)
        except (AssertionError, ValueError):
            corruptions += 1
        else:
            raise AssertionError('Corrupted normal primary low bit was accepted')
    assert [row[0] for row in records['WITNESS']] == ['negative-gravity', 'nonfinite-gravity', 'subnormal-gravity']
    slots = 0
    for index, (_, witness) in enumerate(records['WITNESS']):
        assert witness['failed'] is True and witness['capture_valid'] is True and witness['completed_step'] == 0
        assert witness['model_identity'] == 'retained-gravity-first-controls' and witness['stage'] == 4
        assert witness['code'] == (1 if index == 0 else 2)
        assert witness['context']['constitutive_charges'] == witness['context']['factorizations'] == 0
        values, used = {}, 0
        for field, position, value in witness['scalars']:
            assert 0 <= field < 93 and (field, position) not in values
            values[field, position] = base.decoder.witness_number(value)
            used += 1
        assert witness['rows'] == [] and used == witness['numeric_slots'] <= 512
        rejected = values[88, None]
        assert rejected == -1 if index == 0 else math.isinf(rejected) if index == 1 else 0 < rejected < sys.float_info.min
        slots += used
    return dict(EvidenceValid=True, NativeLaunchedByReview=False, Cases=cases, CorruptedNormalLowBitsRejected=corruptions,
                WitnessNumbersChecked=slots, NativeOutcomeProvenanceNotEstablishedByDataReview=True,
                GameAcceptance=False, PerformanceAcceptance=False)


if __name__ == '__main__':
    print(json.dumps(review(sys.argv[1], sys.argv[2])))
