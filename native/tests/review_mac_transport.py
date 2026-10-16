"""Review saved MAC operands; never launches a native process.

Balances use exact binary64 Fractions. Wave profiles are compared with an
80-digit Fourier recurrence as well as the continuous translated sine. This
checks a transport component, not pressure, free surfaces or colony ownership.
"""
import copy
from decimal import Decimal, localcontext
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

EPS = F(1, 2**52)
CHECKS = 0


def check(ok, reason):
    global CHECKS
    CHECKS += 1
    if not ok:
        raise ValueError(reason)


def close(actual, expected, scale, reason, factor=128):
    check(abs(actual - expected) <= factor * EPS * scale, reason)


def number(token):
    value = float(token)
    check(math.isfinite(value), 'Nonfinite saved operand')
    return F(value)


def unique(mapping, key, value):
    check(key not in mapping, 'Duplicate saved record')
    mapping[key] = value


def scaled_energy(mantissa, exponent):
    m=number(mantissa);e=int(exponent)
    check(-16384<=e<=16384 and ((m==0 and e==0) or F(1,2)<=abs(m)<1),'Scaled energy shape/range')
    return m*F(2)**e


def prepare_scaled_parcels(case):
    for key, (reference, prototype_energy) in case['scaled_parcels'].items():
        donor, _, volume, energy = case['parcels'][key]
        check(reference > 0 and volume > 0 and donor in case['rcells'], 'Invalid scaled parcel reference')
        close(energy, prototype_energy * volume / reference, abs(energy), 'Scaled parcel energy differs', 8)
    for field, prototypes in (('pmasses', case['scaled_masses']), ('ppassives', case['scaled_passives'])):
        for key, prototype in prototypes.items():
            check(key[:2] in case['scaled_parcels'] and prototype > 0, 'Orphan/nonpositive scaled parcel constituent')
            reference, _ = case['scaled_parcels'][key[:2]]; volume = case['parcels'][key[:2]][2]
            actual = case[field][key]
            check(actual > 0, 'Positive scaled parcel constituent disappeared')
            close(actual, prototype * volume / reference, actual, 'Scaled parcel constituent differs', 8)


def prepare_scaled_energy(case):
    check(set(case['widefaces']).issubset(case['faces']),'Orphan wide face energy')
    rounded_faces=0
    for key,(mask,values) in case['widefaces'].items():
        row=case['faces'][key];expected=0
        for i,value in enumerate(values):
            projected=F(float(value));check(row[5+i]==projected,'Legacy face energy projection differs')
            if projected!=value:expected|=1<<i
        check(mask==expected and 0<mask<32,'Wide face rounding mask differs')
        rounded_faces+=mask.bit_count();case['faces'][key]=row[:5]+values+row[10:]
    if case['wide_report'] is None:
        check(not case['widefaces'],'Wide face values have no full report')
        return
    count,unresolved,legacy_mixing,legacy_wall,values=case['wide_report']
    projections=(case['residual'],case['bound'],legacy_mixing,legacy_wall)
    check(all(a==F(float(b)) for a,b in zip(projections,values)),'Legacy energy summary projection differs')
    check(count==rounded_faces+sum(a!=b for a,b in zip(projections,values)) and unresolved>=0,'Energy summary projection counts differ')
    case['residual_projection'],case['bound_projection']=case['residual'],case['bound']
    case['residual'],case['bound']=values[:2]


def parse(raw, required_passes=7, required_cases=(0,1)):
    cases, waves, samples, passes = {}, {}, {}, []
    timestep = None
    for line in raw.decode('ascii').splitlines():
        row = line.split()
        check(bool(row), 'Empty saved record')
        kind = row[0]
        if kind == 'CASE':
            check(len(row) == 8, 'CASE shape')
            identifier, w, h, px, py = map(int, row[1:6])
            check(w > 0 and h > 0 and px in (0, 1) and py in (0, 1), 'CASE geometry')
            unique(cases, identifier, dict(w=w, h=h, px=px, py=py,
                residual=number(row[6]), bound=number(row[7]), walls={}, cells={},
                masses={}, passives={}, faces={}, duals={}, rplan=None, rcells={}, rmasses={}, rpassives={}, parcels={}, pmasses={}, ppassives={},
                widefaces={},wide_report=None,owner_energy=None,scaled_parcels={},scaled_masses={},scaled_passives={}))
        elif kind in ('SPARCEL', 'SPMASS', 'SPPASSIVE'):
            identifier=int(row[1]);check(identifier in cases,'Orphan scaled parcel record');case=cases[identifier]
            if kind=='SPARCEL':
                check(len(row)==11,'Scaled parcel shape');key=tuple(map(int,row[2:4]))
                unique(case['parcels'],key,(int(row[4]),int(row[5]),number(row[6]),scaled_energy(row[9],row[10])))
                unique(case['scaled_parcels'],key,(number(row[7]),number(row[8])))
            else:
                check(len(row)==8,'Scaled parcel constituent shape');key=tuple(map(int,row[2:5]))
                unique(case['pmasses' if kind=='SPMASS' else 'ppassives'],key,scaled_energy(row[6],row[7]))
                unique(case['scaled_masses' if kind=='SPMASS' else 'scaled_passives'],key,number(row[5]))
        elif kind in ('EFACE','ECASE','EOWNER'):
            identifier=int(row[1]);check(identifier in cases,'Orphan scaled energy record');case=cases[identifier]
            if kind=='EFACE':
                check(len(row)==15,'Scaled face shape');unique(case['widefaces'],tuple(map(int,row[2:4])),
                    (int(row[4]),tuple(scaled_energy(row[i],row[i+1]) for i in range(5,15,2))))
            elif kind=='ECASE':
                check(len(row)==14 and case['wide_report'] is None,'Scaled energy report shape/duplicate')
                case['wide_report']=(int(row[2]),int(row[3]),number(row[4]),number(row[5]),tuple(scaled_energy(row[i],row[i+1]) for i in range(6,14,2)))
            else:
                check(len(row)==11 and case['owner_energy'] is None,'Scaled owner shape/duplicate')
                case['owner_energy']=(int(row[2]),tuple(scaled_energy(row[i],row[i+1]) for i in range(3,11,2)))
        elif kind in ('RPLAN', 'RCELL', 'RMASS', 'RPASSIVE', 'PARCEL', 'PMASS', 'PPASSIVE'):
            identifier = int(row[1]); check(identifier in cases, 'Orphan reconstruction record'); case = cases[identifier]
            if kind == 'RPLAN':
                check(len(row) == 5 and case['rplan'] is None, 'Reconstruction plan shape/duplicate')
                case['rplan'] = (number(row[2]), int(row[3]), int(row[4]))
            elif kind == 'RCELL':
                check(len(row) == 7, 'Retained cell shape'); unique(case['rcells'], int(row[2]), tuple(map(number, row[3:])))
            elif kind in ('RMASS', 'RPASSIVE'):
                check(len(row) == 5, 'Retained inventory shape'); v = number(row[4]); check(v > 0, 'Nonpositive retained inventory')
                unique(case['rmasses' if kind == 'RMASS' else 'rpassives'], tuple(map(int, row[2:4])), v)
            elif kind == 'PARCEL':
                check(len(row) == 8, 'Parcel shape'); unique(case['parcels'], tuple(map(int, row[2:4])), (int(row[4]), int(row[5]), number(row[6]), number(row[7])))
            else:
                check(len(row) == 6, 'Parcel inventory shape'); v = number(row[5]); check(v > 0, 'Nonpositive parcel inventory')
                unique(case['pmasses' if kind == 'PMASS' else 'ppassives'], tuple(map(int, row[2:5])), v)
        elif kind in ('WALL', 'CELL', 'MASS', 'PASSIVE', 'FACE', 'DUAL'):
            identifier = int(row[1]); check(identifier in cases, 'Orphan case record')
            case = cases[identifier]
            if kind == 'WALL':
                check(len(row) == 4 and int(row[3]) in (0, 1), 'WALL shape')
                unique(case['walls'], int(row[2]), int(row[3]))
            elif kind == 'CELL':
                check(len(row) == 5, 'CELL shape')
                unique(case['cells'], tuple(map(int, row[2:4])), number(row[4]))
            elif kind in ('MASS', 'PASSIVE'):
                check(len(row) == 6, 'Constituent/metadata shape')
                value = number(row[5]); check(value > 0, 'Nonpositive constituent/metadata')
                unique(case['masses' if kind == 'MASS' else 'passives'], tuple(map(int, row[2:5])), value)
            elif kind == 'FACE':
                check(len(row) == 16, 'FACE shape')
                unique(case['faces'], tuple(map(int, row[2:4])), tuple(map(number, row[4:])))
            else:
                check(len(row) == 6, 'DUAL shape')
                unique(case['duals'], tuple(map(int, row[2:5])), number(row[5]))
        elif kind == 'WAVE':
            check(len(row) == 6, 'WAVE shape')
            unique(waves, (int(row[1]), number(row[2])), (int(row[3]), number(row[4]), number(row[5])))
        elif kind == 'SAMPLE':
            check(len(row) == 6, 'SAMPLE shape')
            unique(samples, (int(row[1]), number(row[2]), int(row[3])), (number(row[4]), number(row[5])))
        elif kind == 'TIMESTEP':
            check(len(row) == 3 and timestep is None, 'TIMESTEP shape/duplicate')
            timestep = tuple(map(number, row[1:]))
        elif kind == 'PASS':
            passes.append(line)
        else:
            raise ValueError('Unknown saved record: ' + kind)
    check(set(cases) == set(required_cases) and len(passes) == required_passes and len(set(passes)) == required_passes, 'Missing case/pass groups')
    for case in cases.values():
        prepare_scaled_parcels(case)
        prepare_scaled_energy(case)
    return cases, waves, samples, timestep


def topology(case):
    w, h, px, py = (case[k] for k in ('w', 'h', 'px', 'py'))
    check(set(case['walls']) == set(range(w * h)), 'Missing/extra wall records')
    faces = [[], []]
    active = lambda x, y: None if case['walls'][x + y * w] else x + y * w
    for y in range(h):
        for x in range(w + (not px)):
            faces[0].append((active((x - 1) % w, y) if x or px else None,
                             active(x, y) if x < w else None))
    for y in range(h + (not py)):
        for x in range(w):
            faces[1].append((active(x, (y - 1) % h) if y or py else None,
                             active(x, y) if y < h else None))
    check(set(case['faces']) == {(a, i) for a in (0, 1) for i in range(len(faces[a]))}, 'Missing/extra face records')
    return faces


def expected_duals(case):
    """Construct half-cell boundaries from primal fluxes, without native tables."""
    w, h, px, py = (case[k] for k in ('w', 'h', 'px', 'py'))
    nx, ny = w + (not px), h + (not py)
    xf = lambda x, y: x % nx + (y % h) * nx
    yf = lambda x, y: x % w + (y % ny) * w
    fx = lambda x, y: case['faces'][0, xf(x, y)][0]
    fy = lambda x, y: case['faces'][1, yf(x, y)][0]
    result = {}
    for y in range(h):
        for x in range(w):
            if not case['walls'][x + y * w]:
                result[0, xf(x, y), xf(x + 1, y)] = (fx(x, y) + fx(x + 1, y)) / 2
                result[1, yf(x, y), yf(x, y + 1)] = (fy(x, y) + fy(x, y + 1)) / 2
    for y in range(0 if py else 1, h):
        for x in range(nx):
            value = (fy(x - 1, y) if x or px else 0) + (fy(x, y) if x < w else 0)
            result[0, xf(x, y - 1), xf(x, y)] = value / 2
    for x in range(0 if px else 1, w):
        for y in range(ny):
            value = (fx(x, y - 1) if y or py else 0) + (fx(x, y) if y < h else 0)
            result[1, yf(x - 1, y), yf(x, y)] = value / 2
    check(set(result) == set(case['duals']), 'Dual boundary topology differs')
    for key, value in result.items():
        close(case['duals'][key], value, abs(value), 'Dual flux differs from half-cell geometry')
    return result


def packets(old_mass, edges):
    outgoing = [F(0) for _ in old_mass]
    incoming = [[] for _ in old_mass]
    for left, right, flux in edges:
        check(0 <= left < len(old_mass) and 0 <= right < len(old_mass) and left != right, 'Invalid flux endpoint')
        if flux:
            donor, target = (left, right) if flux > 0 else (right, left)
            outgoing[donor] += abs(flux); incoming[target].append((donor, abs(flux)))
    for i, mass in enumerate(old_mass):
        check(outgoing[i] <= mass + 128 * EPS * mass, 'Outgoing donor mass exceeded')
        check(mass > 0 or not outgoing[i], 'Empty donor')
        incoming[i].append((i, mass - outgoing[i]))
    return incoming


def review_case(case):
    if case['rplan'] is None and any(case[k] for k in ('rcells', 'rmasses', 'rpassives', 'parcels', 'pmasses', 'ppassives')):
        raise ValueError('Reconstruction payload has no plan record')
    faces = topology(case); expected_duals(case)
    nc = case['w'] * case['h']
    check(set(case['cells']) == {(e, i) for e in (0, 1) for i in range(nc)}, 'Missing/extra material cells')
    for values in (case['masses'], case['passives']):
        check(all(e in (0, 1) and 0 <= i < nc and k >= 0 for e, i, k in values), 'Invalid inventory index')
    mass = [[sum((v for (end, cell, _), v in case['masses'].items() if end == e and cell == i), F(0))
             for i in range(nc)] for e in (0, 1)]
    for i in range(nc):
        check((mass[0][i] == mass[1][i] == 0) if case['walls'][i] else min(mass[0][i], mass[1][i]) > 0, 'Wall/occupied inventory differs')
    primal = []
    for axis in (0, 1):
        for i, adjacent in enumerate(faces[axis]):
            row = case['faces'][axis, i]; flux, j0, j1, i0, i1 = row[:5]
            for endpoint, inertia in ((0, i0), (1, i1)):
                expect = sum((mass[endpoint][c] for c in adjacent if c is not None), F(0)) / 2
                close(inertia, expect, expect, 'Face inertia differs from adjacent half masses')
            if None in adjacent:
                check(flux == j0 == j1 == 0, 'Constrained normal face carries mass/momentum')
            else:
                primal.append((*adjacent, flux))
    material_packets = packets(mass[0], primal)
    reconstructed = review_reconstructed_primal(case, faces) if case['rplan'] is not None else None
    for key in ('masses', 'passives'):
        values = case[key]; identities = {k for _, _, k in values}
        for identity in identities:
            old_total = sum((values.get((0, i, identity), F(0)) for i in range(nc)), F(0))
            new_total = sum((values.get((1, i, identity), F(0)) for i in range(nc)), F(0))
            close(new_total, old_total, old_total + new_total, 'Global constituent/passive balance')
            for i, donors in enumerate(material_packets):
                expect = reconstructed[key][i].get(identity, F(0)) if reconstructed else sum((amount * values.get((0, donor, identity), F(0)) / mass[0][donor]
                              for donor, amount in donors if amount), F(0))
                actual = values.get((1, i, identity), F(0))
                close(actual, expect, abs(expect) + abs(actual), 'Constituent/passive packet mismatch')
    cell_heat = [F(0) for _ in range(nc)]
    energy_terms = [case['cells'][e, i] * (1 if e else -1) for e in (0, 1) for i in range(nc)]
    for axis in (0, 1):
        rows = [case['faces'][axis, i] for i in range(len(faces[axis]))]
        weights = packets([row[3] for row in rows], [(l, r, f) for (a, l, r), f in case['duals'].items() if a == axis])
        momentum_terms = []
        for i, (row, donors) in enumerate(zip(rows, weights)):
            _, j0, j1, i0, i1, k0, k1, kt, qm, qw, impulse, dm = row
            check(min(i0, i1, k0, k1, kt, qm, qw) >= 0, 'Negative inertia/kinetic/heat')
            transported_mass = sum((amount for _, amount in donors), F(0))
            close(transported_mass, i1, transported_mass + i1, 'Dual mass continuity')
            close(dm, transported_mass - i1, transported_mass + i1, 'Saved dual mass residual')
            provisional_j = sum((amount * rows[d][1] / rows[d][3] for d, amount in donors if amount), F(0))
            provisional_u = provisional_j / i1 if i1 else F(0)
            fixed = None in faces[axis][i]
            expect_j = F(0) if fixed else provisional_j
            jscale = sum((abs(amount * rows[d][1] / rows[d][3]) for d, amount in donors if amount), F(0))
            close(j1, expect_j, jscale + abs(j1), 'Dual momentum packet mismatch')
            close(impulse, expect_j - provisional_j, jscale + abs(impulse), 'Wall impulse mismatch')
            for inertia, momentum, actual in ((i0, j0, k0), (i1, j1, k1)):
                expect = momentum * momentum / (2 * inertia) if inertia else F(0)
                close(actual, expect, actual + expect, 'Stored resolved kinetic energy')
            expect_kt = sum((amount * rows[d][5] / rows[d][3] for d, amount in donors if amount), F(0))
            close(kt, expect_kt, kt + expect_kt, 'Transported kinetic packet mismatch')
            expect_qm = sum((amount * (rows[d][1] / rows[d][3] - provisional_u)**2 / 2
                             for d, amount in donors if amount), F(0))
            expect_qw = provisional_j * provisional_u / 2 if fixed else F(0)
            # A near-zero variance subtracts rounded velocities. Its allowed
            # error is tied to actual packet K, never an arbitrary unit floor.
            close(qm, expect_qm, kt + expect_kt + expect_qm, 'Positive variance heat mismatch', 1024)
            close(qw, expect_qw, qw + expect_qw, 'Wall projection heat mismatch')
            close(kt, k1 + qm + qw, kt + k1 + qm + qw, 'Local kinetic-to-heat closure', 1024)
            total = sum((mass[1][c] for c in faces[axis][i] if c is not None), F(0))
            for c in faces[axis][i]:
                if c is not None and total:
                    cell_heat[c] += (qm + qw) * mass[1][c] / total
            momentum_terms.extend((j1, -j0, -impulse)); energy_terms.extend((k1, -k0))
        close(sum(momentum_terms, F(0)), F(0), sum(map(abs, momentum_terms), F(0)), 'Global J plus wall impulse')
    for i, donors in enumerate(material_packets):
        terms = reconstructed['energy'][i] + [cell_heat[i]] if reconstructed else [amount * case['cells'][0, d] / mass[0][d] for d, amount in donors if amount] + [cell_heat[i]]
        expect = sum(terms, F(0)); actual = case['cells'][1, i]
        close(actual, expect, abs(actual) + sum(map(abs, terms), F(0)), 'Primal U plus assigned heat', 256)
    residual = sum(energy_terms, F(0)); scale = sum(map(abs, energy_terms), F(0))
    close(residual, F(0), scale, 'Physical global energy balance')
    close(case['residual'], residual, scale, 'Saved energy residual', 2)
    close(case['bound'], 128 * EPS * scale, 128 * EPS * scale, 'Saved energy bound', 512)


def review_reconstructed_primal(case, faces):
    nc = case['w'] * case['h']; nominal, backtracks, unresolved = case['rplan']
    check(nominal > 0 and backtracks >= 0 and 0 <= unresolved <= nc, 'Invalid reconstruction plan metadata')
    check(set(case['rcells']) == set(range(nc)), 'Retained cell coverage')
    check(set(case['parcels']) == {k for k, v in case['faces'].items() if v[0]}, 'Reconstructed nonzero face coverage')
    q = {}; incoming = [[] for _ in range(nc)]; outgoing = [[] for _ in range(nc)]
    for key, (donor, target, volume, energy) in case['parcels'].items():
        check(key in case['faces'] and 0 <= donor < nc and 0 <= target < nc and volume > 0, 'Invalid reconstructed parcel geometry')
        pair = faces[key[0]][key[1]]; check((donor, target) in (pair, pair[::-1]) and None not in pair, 'Parcel endpoints differ from face')
        q[key] = volume if donor == pair[0] else -volume; outgoing[donor].append(key); incoming[target].append(key)
        mass = sum((v for (a, f, _), v in case['pmasses'].items() if (a, f) == key), F(0))
        close(case['faces'][key][0], mass if donor == pair[0] else -mass, mass, 'Primal mass flux differs from constituent parcel')
    for field, length in (('rmasses', 2), ('rpassives', 2), ('pmasses', 3), ('ppassives', 3)):
        check(all(len(k) == length and k[-1] >= 0 and (0 <= k[0] < nc if length == 2 else k[:2] in case['parcels']) for k in case[field]), 'Invalid retained/parcel inventory index')
    targets = {'masses': [{} for _ in range(nc)], 'passives': [{} for _ in range(nc)], 'energy': [[] for _ in range(nc)]}
    def neighbor(i, axis, direction):
        x, y = i % case['w'], i // case['w']
        if axis == 0:
            if not case['px'] and (x + direction < 0 or x + direction >= case['w']): return None
            x = (x + direction) % case['w']
        else:
            if not case['py'] and (y + direction < 0 or y + direction >= case['h']): return None
            y = (y + direction) % case['h']
        n = x + y * case['w']; return None if case['walls'][n] else n
    nx = case['w'] + (not case['px']); ny = case['h'] + (not case['py'])
    xf = lambda x, y: x % nx + (y % case['h']) * nx
    yf = lambda x, y: x % case['w'] + (y % ny) * case['w']
    for i in range(nc):
        volume, retained_volume, retained_energy, theta = case['rcells'][i]
        check(0 <= theta <= 1 and retained_volume >= 0 and (volume == 0 if case['walls'][i] else volume > 0), 'Invalid retained volume/limiter')
        out_volume = sum((case['parcels'][k][2] for k in outgoing[i]), F(0))
        in_volume = sum((case['parcels'][k][2] for k in incoming[i]), F(0))
        close(retained_volume + out_volume, volume, retained_volume + out_volume + volume, 'Reconstructed donor volume')
        target = F(0) if case['walls'][i] else nominal
        close(retained_volume + in_volume, target, retained_volume + in_volume + target, 'Reconstructed target volume')
        adjacent = [neighbor(i, a, d) for a, d in ((0, -1), (0, 1), (1, -1), (1, 1))]
        for key in ('masses', 'passives', 'energy'):
            if key == 'energy':
                identities = (None,)
                old = lambda c, _: case['cells'][0, c]
                retained = lambda c, _: case['rcells'][c][2]
                parcel = lambda k, _: case['parcels'][k][3]
            else:
                rkey, pkey = ('rmasses', 'pmasses') if key == 'masses' else ('rpassives', 'ppassives')
                identities = {k[-1] for field in (case[key], case[rkey], case[pkey]) for k in field}
                old = lambda c, s: case[key].get((0, c, s), F(0))
                retained = lambda c, s: case[rkey].get((c, s), F(0))
                parcel = lambda k, s: case[pkey].get((*k, s), F(0))
            for identity in identities:
                centre = old(i, identity); rvalue = retained(i, identity)
                terms = [rvalue] + [parcel(k, identity) for k in outgoing[i]]
                close(sum(terms, F(0)), centre, sum(map(abs, terms), F(0)) + abs(centre), 'Complete reconstructed donor inventory')
                if key == 'energy': targets[key][i] = [rvalue] + [parcel(k, identity) for k in incoming[i]]
                else: targets[key][i][identity] = rvalue + sum((parcel(k, identity) for k in incoming[i]), F(0))
                if not volume: continue
                values = [centre if n is None else old(n, identity) * volume / case['rcells'][n][0] for n in adjacent]
                sx = (values[1] - values[0]) / 2 if adjacent[0] is not None and adjacent[1] is not None else F(0)
                sy = (values[3] - values[2]) / 2 if adjacent[2] is not None and adjacent[3] is not None else F(0)
                for k in outgoing[i]:
                    donor, _, part, _ = case['parcels'][k]; pair = faces[k[0]][k[1]]; sign = 1 if donor == pair[0] else -1
                    normal = sign * (1 - part / volume) / 2; x, y = i % case['w'], i // case['w']
                    if k[0] == 0: cx, cy = normal, -(q.get((1, yf(x, y)), F(0)) + q.get((1, yf(x, y + 1)), F(0))) / (4 * volume)
                    else: cx, cy = -(q.get((0, xf(x, y)), F(0)) + q.get((0, xf(x + 1, y)), F(0))) / (4 * volume), normal
                    terms = (centre, theta * cx * sx, theta * cy * sy)
                    expected = sum(terms, F(0)) * part / volume; actual = parcel(k, identity)
                    scale = sum(map(abs, terms), F(0)) * part / volume
                    close(actual, expected, scale + abs(actual), 'Common-limiter reconstructed parcel', 1024)
    return targets


PI = Decimal('3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117')


def sincos(x):
    x %= 2 * PI
    if x > PI:
        x -= 2 * PI
    sine, cosine, st, ct = x, Decimal(1), x, Decimal(1)
    for k in range(1, 180):
        st *= -x * x / ((2 * k) * (2 * k + 1)); ct *= -x * x / ((2 * k - 1) * (2 * k))
        sine += st; cosine += ct
        if max(abs(st), abs(ct)) < Decimal('1e-85'):
            return sine, cosine
    raise ValueError('High-precision trigonometric series did not converge')


def decimal_value(value):
    return Decimal(value.numerator) / Decimal(value.denominator)


def review_waves(waves, samples, timestep):
    keys = {(n, F(float(c))) for n, c in ((16, .2), (32, .2), (64, .2), (16, .1), (16, .05))}
    check(set(waves) == keys and set(samples) == {(n, c, x) for n, c in keys for x in range(n)}, 'Missing/extra wave profiles')
    for n, c in sorted(keys):
        with localcontext() as ctx:
            ctx.prec = 80
            h = Decimal(1) / n; dt = decimal_value(c) * h / Decimal('.75'); duration = Decimal('.125')
            st, ct = sincos(2 * PI * h); sinc = sincos(PI * h)[0] / (PI * h)
            real, imag, time, steps = Decimal(1), Decimal(0), Decimal(0), 0
            while time < duration:
                step = min(dt, duration - time); nu = Decimal('.5') * step / h
                ar, ai = 1 - nu + nu * ct, -nu * st
                real, imag = real * ar - imag * ai, real * ai + imag * ar
                time += step; steps += 1
            saved_steps, verr, terr = waves[n, c]
            check(abs(saved_steps - steps) <= 1 and saved_steps <= math.ceil(.125 / (float(c) / n / .75)) + 2, 'Wave step bound')
            continuous_errors = [F(0), F(0)]
            for x in range(n):
                s, co = sincos(2 * PI * (Decimal(x) + Decimal('.5')) * h)
                transported = sinc * (s * real + co * imag)
                uy, tag = samples[n, c, x]
                check(abs(decimal_value(uy) - Decimal('.25') * transported) <= Decimal('5e-12'), 'Shear differs from independent upwind recurrence')
                check(abs(decimal_value(tag) - (1 + Decimal('.2') * transported)) <= Decimal('2e-11'), 'Passive differs from independent upwind recurrence')
                continuous = F(sinc * sincos(2 * PI * ((Decimal(x) + Decimal('.5')) * h - Decimal('.0625')))[0])
                continuous_errors[0] += abs(uy - continuous / 4) / n
                continuous_errors[1] += abs(tag - (1 + continuous / 5)) / n
            close(verr, continuous_errors[0], F(1, 4), 'Continuous shear error differs from samples', 1024)
            close(terr, continuous_errors[1], F(1), 'Continuous passive error differs from samples', 1024)
    c = F(.2)
    for a, b in ((16, 32), (32, 64)):
        check(all(0 < waves[b, c][j] < F(7, 10) * waves[a, c][j] for j in (1, 2)), 'Joint grid refinement bound')
    differences = []
    for ca, cb in ((.2, .1), (.1, .05)):
        differences.append(sum((abs(samples[16, F(ca), x][0] - samples[16, F(cb), x][0]) for x in range(16)), F(0)) / 16)
    check(timestep is not None, 'Missing timestep result')
    for actual, expect in zip(timestep, differences):
        close(actual, expect, F(1, 4), 'Fixed-grid timestep difference differs from profiles', 1024)
    check(0 < differences[1] < F(7, 10) * differences[0], 'Fixed-grid timestep refinement bound')


def review(data):
    cases, waves, samples, timestep = data
    for case in cases.values():
        review_case(case)
    review_waves(waves, samples, timestep)


def main():
    path = Path(sys.argv[1]); raw = path.read_bytes()
    check(len(raw) <= 131072, 'Saved stdout exceeds declared cap')
    data = parse(raw); review(data)
    checks = CHECKS
    for control in ('dual-flux', 'wave-profile', 'trace-loss'):
        altered = copy.deepcopy(data)
        if control == 'dual-flux':
            altered[0][0]['duals'][0, 0, 1] += F(1, 100)
        elif control == 'wave-profile':
            key = (16, F(.2), 0); a, b = altered[2][key]; altered[2][key] = (a + F(1, 100), b)
        else:
            case = altered[0][1]; key = next(k for k in case['masses'] if k[0] == 1 and case['masses'][k] < F(1, 10**20))
            case['masses'][key] /= 10
        rejected = False
        try:
            review(altered)
        except ValueError:
            rejected = True
        check(rejected, 'Corruption was accepted: ' + control)
    print(json.dumps(dict(Passed=True, SavedSha256=hashlib.sha256(raw).hexdigest(),
        RationalAndProfileChecks=checks, CorruptionsRejected=3, NativeProcessLaunched=False,
        Scope='Regular 2D MAC transport only; no pressure, free surfaces, game or performance acceptance',
        IndependentAgentReview=False)))


if __name__ == '__main__':
    main()
