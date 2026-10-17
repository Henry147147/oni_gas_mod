"""Data-only review of the closed MAC pressure/transport controls.

Reuses the separate Fraction transport reconstruction. Equilibrium and
isentropic work are recomputed here using 80-digit arithmetic, with no native
EOS calls. Test water is an explicit zero-expansion elastic surrogate.
"""
import copy
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location('saved_mac', Path(__file__).with_name('review_mac_transport.py'))
base = importlib.util.module_from_spec(spec); spec.loader.exec_module(base)
check, close, number, unique = base.check, base.close, base.number, base.unique
EPS = base.EPS
R = F(float('8.31446261815324'))


def dec(value):
    return D(value.numerator) / D(value.denominator)


def parse(raw, required_passes=8, require_acoustic=True, required_cases=(0,1), required_pressure_cases=(0,1)):
    ordinary, materials, pressure, cells, faces, waves, samples = [], {}, {}, {}, {}, {}, {}
    interval = None
    extended_pressure = {}
    common = {'CASE', 'WALL', 'CELL', 'MASS', 'PASSIVE', 'FACE', 'DUAL', 'PASS', 'RPLAN', 'RCELL', 'RMASS', 'RPASSIVE', 'PARCEL', 'PMASS', 'PPASSIVE', 'EFACE', 'ECASE', 'EOWNER', 'SPARCEL', 'SPMASS', 'SPPASSIVE', 'XFACE', 'XDUAL', 'XVOLUME'}
    common.update(('MCONFIG','MRCELL','MRECON','MRFACE','MRDUAL','MREVENT'))
    for line in raw.decode('ascii').splitlines():
        row = line.split(); check(bool(row), 'Empty record')
        kind = row[0]
        if kind in common:
            ordinary.append(line)
        elif kind == 'MATERIAL':
            check(len(row) == 8 and int(row[2]) in (0, 1), 'Material shape')
            unique(materials, int(row[1]), (int(row[2]), *map(number, row[3:])))
        elif kind == 'PCASE':
            check(len(row) == 11, 'Pressure case shape')
            unique(pressure, int(row[1]), tuple(map(number, row[2:])))
        elif kind == 'PCELL':
            check(len(row) == 13, 'Pressure cell shape')
            unique(cells, tuple(map(int, row[1:3])), tuple(map(number, row[3:])))
        elif kind == 'PFACE':
            check(len(row) == 7, 'Pressure face shape')
            unique(faces, tuple(map(int, row[1:4])), tuple(map(number, row[4:])))
        elif kind == 'XPFACE':
            check(len(row)==13,'Full pressure face shape')
            unique(extended_pressure,tuple(map(int,row[1:4])),(int(row[4]),tuple(base.scaled_energy(row[i],row[i+1]) for i in range(5,11,2)),number(row[11]),number(row[12])))
        elif kind == 'ACOUSTIC':
            check(len(row) == 13, 'Acoustic shape')
            values = tuple(map(number, row[2:]))
            check(values[5].denominator == values[6].denominator == 1, 'Noninteger acoustic counts')
            unique(waves, int(row[1]), values)
        elif kind == 'ASAMPLE':
            check(len(row) == 5, 'Acoustic sample shape')
            unique(samples, tuple(map(int, row[1:3])), tuple(map(number, row[3:])))
        elif kind == 'INTERVAL':
            check(len(row) == 6 and interval is None, 'Interval shape/duplicate')
            interval = tuple(map(number, row[1:]))
        else:
            raise ValueError('Unknown record: ' + kind)
    cases, _, _, _ = base.parse(('\n'.join(ordinary) + '\n').encode('ascii'), required_passes=required_passes, required_cases=required_cases)
    for key,(mask,values,root_in,root_out) in extended_pressure.items():
        check(key in faces and key[0] in cases,'Orphan full pressure face'); expected=0
        for bit,(legacy,full) in enumerate(zip(faces[key],values)):
            check(legacy==F(float(full)),'Pressure face projection differs')
            if legacy!=full:expected|=1<<bit
        check(mask==expected and mask>0,'Pressure face projection mask differs');faces[key]=values
        cases[key[0]].setdefault('root_projection',{})[key[1:]]=(root_in,root_out)
    for identifier,case in cases.items():
        check(case['momentum_mode'] in (0,1,2),'Closed step omitted its momentum mode')
        if case['owner_energy'] is not None:
            count,values=case['owner_energy'];row=pressure[identifier];projections=row[3:7]
            check(all(a==F(float(b)) for a,b in zip(projections,values)),'Owner energy projections differ')
            transport_count=case['wide_report'][0] if case['wide_report'] else 0
            check(count==transport_count+sum(a!=b for a,b in zip(projections,values)),'Owner energy projection counts differ')
            pressure[identifier]=row[:3]+values+row[7:]
    check(set(pressure) == set(required_pressure_cases) and len(materials) == 4 and (set(waves) == {16, 32, 64} if require_acoustic else not waves), 'Missing pressure/material/acoustic records')
    check(set(samples) == {(n, x) for n in waves for x in range(n)}, 'Missing/extra acoustic samples')
    if require_acoustic:
        check(interval is not None and interval[0] == interval[1] + interval[2] and interval[1] > 0 and interval[2] > 1, 'Interval retry counts')
        check(abs(interval[3]) <= interval[4] and interval[4] > 0, 'Interval energy bound')
    else:
        check(interval is None, 'Unexpected interval record')
    for phase, molar, cv, _, rho, bulk in materials.values():
        check(molar > 0 and cv > 0 and ((rho == bulk == 0) if phase == 0 else (rho > 0 and bulk > 0)), 'Invalid material coefficients')
    return cases, materials, pressure, cells, faces, waves, samples


def eos_values(masses, materials, pressure):
    liquid, elastic, capacity, offset, moles = D(0), D(0), D(0), D(0), D(0)
    for species, amount_f in masses.items():
        phase, molar_f, cv_f, off_f, rho_f, bulk_f = materials[species]
        amount, molar, cv, off = map(dec, (amount_f, molar_f, cv_f, off_f))
        capacity += amount * cv; offset += amount * off
        if phase == 0:
            moles += amount / molar
        else:
            rho, bulk = dec(rho_f), dec(bulk_f); q = pressure / bulk
            liquid += amount / rho / (1 + q)
            elastic += amount / rho * bulk * ((1 + q).ln() - q / (1 + q))
    return liquid, elastic, capacity, offset, moles


def equilibrium(masses, materials, energy_f, volume_f):
    energy, volume = dec(energy_f), dec(volume_f)
    def point(p):
        liquid, elastic, capacity, offset, moles = eos_values(masses, materials, p)
        temperature = (energy - offset - elastic) / capacity
        gas_volume = moles * dec(R) * temperature / p if p else D(0)
        return liquid + gas_volume - volume, temperature, gas_volume
    check(all(s in materials for s in masses), 'Constituent without saved material identity')
    if all(materials[species][0] == 0 for species in masses):
        _, _, capacity, offset, moles = eos_values(masses, materials, D(0))
        check(capacity > 0 and moles > 0 and volume > 0, 'Invalid occupied ideal-gas reference')
        temperature = (energy - offset) / capacity
        p = moles * dec(R) * temperature / volume
        gas_volume = volume
    else:
        lo, hi = D(0), D(1)
        for _ in range(100):
            if point(hi)[0] <= 0:
                break
            hi *= 2
        else:
            raise ValueError('Independent EOS pressure bracket failed')
        for _ in range(240):
            mid = (lo + hi) / 2
            if point(mid)[0] > 0:
                lo = mid
            else:
                hi = mid
        p = (lo + hi) / 2; _, temperature, gas_volume = point(p)
    check(temperature > 0 and p > 0 and gas_volume > 0, 'Independent EOS left occupied positive branch')
    _, _, capacity, _, _ = eos_values(masses, materials, p)
    entropy = capacity * (temperature / dec(F(298.15))).ln()
    for species, amount in masses.items():
        phase, molar, *_ = materials[species]
        if phase == 0:
            n = dec(amount) / dec(molar); entropy += n * dec(R) * (gas_volume / n).ln()
    return p, temperature, gas_volume, entropy


def isentropic_work(masses, materials, energy, volume, change):
    p0, t0, vg0, _ = equilibrium(masses, materials, energy, volume)
    _, _, capacity, offset, moles = eos_values(masses, materials, p0)
    alpha = capacity / (capacity + moles * dec(R)); target = dec(volume + change)
    def state(p):
        liquid, elastic, *_ = eos_values(masses, materials, p)
        vg = vg0 * (alpha * (p0 / p).ln()).exp(); temperature = p * vg / (moles * dec(R))
        return liquid + vg, offset + capacity * temperature + elastic
    lo, hi = D(0), p0
    for _ in range(100):
        if state(hi)[0] <= target:
            break
        hi *= 2
    else:
        raise ValueError('Independent isentropic bracket failed')
    for _ in range(240):
        mid = (lo + hi) / 2
        if state(mid)[0] > target:
            lo = mid
        else:
            hi = mid
    if not change:
        return p0
    return -(state((lo + hi) / 2)[1] - dec(energy)) / dec(change)


def review_pressure(case_id, data):
    cases, materials, pressure_cases, pcells, pfaces, *_ = data
    case = cases[case_id]; pairs = base.topology(case)
    dt, spacing, depth, pres, pbound, total_res, total_bound, wx, wy = pressure_cases[case_id]
    volume, area = spacing * spacing * depth, spacing * depth
    if case['rplan']:
        close(case['rplan'][0], volume, volume, 'Reconstruction/pressure nominal volume')
    check(dt > 0 and volume > 0, 'Invalid pressure geometry/time')
    check(set(k[1] for k in pcells if k[0] == case_id) == {i for i, wall in case['walls'].items() if not wall}, 'Pressure cell coverage')
    check({k[1:] for k in pfaces if k[0] == case_id} == set(case['faces']), 'Pressure face coverage')
    mass = [{s: m for (end, c, s), m in case['masses'].items() if end == e and c == i} for e in (0, 1) for i in range(len(case['walls']))]
    nc = len(case['walls']); sweeps = [F(0) for _ in range(nc)]; reactions = [[], []]
    pterms, all_terms = [], []
    for axis in (0, 1):
        momentum_terms = []
        for f, adjacent in enumerate(pairs[axis]):
            row = case['faces'][axis, f]
            flux, j_pressure, j_final, i0, i1 = row[:5]; j_initial, swept, helper_u = pfaces[case_id, axis, f]
            pl = F(0) if adjacent[0] is None else pcells[case_id, adjacent[0]][3]
            pr = F(0) if adjacent[1] is None else pcells[case_id, adjacent[1]][3]
            impulse = dt * area * (pl - pr); fixed = None in adjacent
            close(j_pressure, F(0) if fixed else j_initial + impulse, abs(j_initial) + abs(impulse) + abs(j_pressure), 'Canonical pressure impulse')
            if fixed:
                check(j_initial == j_pressure == swept == 0, 'Fixed pressure face moved')
                reactions[axis].append(-impulse)
            else:
                expect_q = dt * area * (j_initial + j_pressure) / (2 * i0)
                close(swept, expect_q, abs(swept) + abs(expect_q), 'Canonical midpoint volume sweep')
                sweeps[adjacent[0]] += swept; sweeps[adjacent[1]] -= swept
                if swept:
                    donor = adjacent[0] if swept > 0 else adjacent[1]
                    expect_f = (1 if swept > 0 else -1) * sum((m for (pa, pf, _), m in case['pmasses'].items() if (pa, pf) == (axis, f)), F(0)) if case['rplan'] else swept * sum(mass[donor].values(), F(0)) / pcells[case_id, donor][1]
                    close(flux, expect_f, abs(flux) + abs(expect_f), 'Material donor density mass flux')
                else:
                    check(flux == 0, 'Mass flow without a sweep')
            if i0:
                if (axis,f) in case.get('root_projection',{}):
                    root_in,root_out=case['root_projection'][axis,f]
                    expected_in=F(float(j_initial/i0))
                    close(root_in,expected_in,abs(expected_in),'Pressure root input projection')
                    corrected=root_out+j_initial/i0-root_in
                    close(helper_u,corrected,abs(helper_u)+abs(corrected),'Pressure helper full input correction')
                close(helper_u, j_pressure / i0, abs(j_initial / i0) + abs(impulse / i0) + abs(helper_u), 'Pressure helper/canonical velocity', 256)
            k0 = j_initial * j_initial / (2 * i0) if i0 else F(0)
            pterms.extend((row[5], -k0)); all_terms.extend((row[6], -k0))
            momentum_terms.extend((j_pressure, -j_initial, impulse if fixed else F(0)))
        close(sum(momentum_terms, F(0)), F(0), sum(map(abs, momentum_terms), F(0)), 'Pressure momentum/wall closure', 256)
    for actual, terms in zip((wx, wy), reactions):
        close(actual, sum(terms, F(0)), sum(map(abs, terms), F(0)), 'Reported pressure wall reaction')
    entropy0 = entropy1 = D(0); entropy_rounding = D(0)
    for i in range(nc):
        if case['walls'][i]:
            continue
        u0, vlag, delta, work, endpoint_p, final_p, final_t, saved_s0, saved_s1, saved_geo = pcells[case_id, i]
        if case['rplan']: check(case['rcells'][i][0] == vlag, 'Reconstruction source volume differs from physical pressure endpoint')
        u_lag, u_final = case['cells'][0, i], case['cells'][1, i]
        close(vlag, volume + delta, vlag + volume + abs(delta), 'Pressure endpoint volume addition', 32)
        close(vlag - volume - sweeps[i], F(0), vlag + volume + abs(sweeps[i]), 'Fixed-grid remap volume closure')
        close(saved_geo, vlag - volume - sweeps[i], vlag + volume + abs(sweeps[i]), 'Saved geometric residual')
        close(u_lag, u0 - work * delta, abs(u_lag) + abs(u0) + abs(work * delta), 'Stored pressure work')
        independent_work = isentropic_work(mass[i], materials, u0, volume, delta)
        check(abs(dec(work) - independent_work) <= D('5e-10') * abs(independent_work), 'Pressure work differs from independent isentrope')
        e0 = equilibrium(mass[i], materials, u0, volume)
        elag = equilibrium(mass[i], materials, u_lag, vlag)
        e1 = equilibrium(mass[nc + i], materials, u_final, volume)
        for actual, expected in ((endpoint_p, elag[0]), (final_p, e1[0]), (final_t, e1[1])):
            check(abs(dec(actual) - expected) <= D('5e-10') * abs(expected), 'Saved EOS reading differs from independent equilibrium')
        for actual, expected in ((saved_s0, e0[3]), (saved_s1, e1[3])):
            check(abs(dec(actual) - expected) <= D('5e-10') * max(abs(expected), dec((abs(u0) + abs(u_final))) / min(e0[1], e1[1])), 'Saved entropy reading')
        entropy0 += e0[3]; entropy1 += e1[3]
        entropy_rounding += dec(128 * EPS * (abs(u0) + abs(u_lag) + abs(u_final))) / min(e0[1], e1[1])
        pterms.extend((u_lag, -u0)); all_terms.extend((u_final, -u0))
    check(entropy1 >= entropy0 - entropy_rounding, 'Composed entropy decreases beyond stored-U roundoff')
    pscale = sum(map(abs, pterms), F(0)); pactual = sum(pterms, F(0))
    close(pactual, F(0), pscale, 'Actual pressure-stage energy')
    close(pres, pactual, pscale, 'Saved pressure energy residual', 2)
    close(pbound, 128 * EPS * pscale, 128 * EPS * pscale, 'Pressure energy bound', 512)
    close(total_bound, pbound + case['bound'], pbound + case['bound'], 'Two-stage energy bound')
    total = sum(all_terms, F(0)); check(abs(total) <= total_bound, 'Actual composed energy')
    close(total_res, total, sum(map(abs, all_terms), F(0)), 'Saved composed energy residual', 2)


def atan(x):
    term, result = x, x
    for k in range(1, 240):
        term *= -x * x; item = term / (2 * k + 1); result += item
        if abs(item) < D('1e-85'):
            return result
    raise ValueError('Independent atan did not converge')


def review_acoustics(data):
    *_, waves, samples = data
    for n, values in waves.items():
        gamma, rho, sound, duration, dt, steps, rejected, perr, uerr, energy_res, bound, = values
        check(rejected == 0 and n // 8 <= steps <= n // 8 + 2, 'Declared implicit acoustic path retried or exceeded step bound')
        close(sound * sound, gamma * 100000 / rho, sound * sound, 'Acoustic sound speed')
        close(duration * sound, F(1, 4), F(1, 4), 'Acoustic endpoint')
        close(dt * sound * n, F(2), F(2), 'Acoustic Courant exceeds explicit bound')
        check(abs(energy_res) <= bound and bound > 0, 'Acoustic interval energy bound')
        h = D(1) / n; pi = base.PI; sinc = base.sincos(pi * h)[0] / (pi * h)
        phase = 2 * (n // 8) * atan(dec(dt * sound) * n * base.sincos(pi * h)[0])
        errors = [D(0), D(0)]
        for x in range(n):
            p, u = samples[n, x]
            actuals = ((dec(p) - 100000) / D('.1'), dec(u * rho * sound) / D('.1'))
            for axis, position in enumerate((D(x) + D('.5'), D(x))):
                expected_discrete = sinc * base.sincos(2 * pi * position * h - phase)[0]
                check(abs(actuals[axis] - expected_discrete) <= D('5e-5'), 'Acoustic profile differs from independent midpoint Fourier mode')
                expected_continuous = sinc * base.sincos(2 * pi * (position * h - D('.25')))[0]
                errors[axis] += abs(actuals[axis] - expected_continuous) / n
        for actual, expected in zip((perr, uerr), errors):
            check(abs(dec(actual) - expected) <= D('2e-7'), 'Acoustic row/report continuous error differs')
    for a, b in ((16, 32), (32, 64)):
        check(all(0 < waves[b][j] < F(7, 10) * waves[a][j] for j in (7, 8)), 'Joint pressure/velocity refinement fails .7 bound')


def review(data):
    for identifier, case in data[0].items():
        base.review_case(case); review_pressure(identifier, data)
    review_acoustics(data)


def main():
    raw = Path(sys.argv[1]).read_bytes(); check(len(raw) <= base.saved_pipe_cap(raw,131072), 'Saved pipe limit')
    with localcontext() as context:
        context.prec = 80
        data = parse(raw); review(data); checks = base.CHECKS
        for control in ('work-pressure', 'pressure-trace', 'acoustic-profile'):
            altered = copy.deepcopy(data)
            if control == 'work-pressure':
                key = (1, 0); row = list(altered[3][key]); row[3] *= F(101, 100); altered[3][key] = tuple(row)
            elif control == 'pressure-trace':
                case = altered[0][1]; key = next(k for k, v in case['masses'].items() if k[0] == 1 and v < F(1, 10**20)); case['masses'][key] /= 10
            else:
                p, u = altered[6][16, 0]; altered[6][16, 0] = (p + 1, u)
            rejected = False
            try:
                review(altered)
            except ValueError:
                rejected = True
            check(rejected, 'Altered result was accepted: ' + control)
    print(json.dumps(dict(Passed=True, SavedSha256=hashlib.sha256(raw).hexdigest(), SavedChecks=checks,
        CorruptionsRejected=3, NativeProcessLaunched=False, IndependentAgentReview=False,
        Scope='Closed occupied zero-gravity MAC EOS pressure/transport only; no production/game/performance acceptance')))


if __name__ == '__main__':
    main()
