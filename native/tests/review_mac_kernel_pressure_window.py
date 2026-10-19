"""Independent saved endpoint review; launches no native process.

Fixed-mass pressure windows only. Reintegrates the continuous gas ODE and
checks the shared displacement and original 128-epsilon energy account.
"""
import copy
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

CHECKS = 0
EPS = F(2) ** -52
CASES = ((0, False, 0), (2, False, 1), (3, True, 1), (3, True, 2))


def check(ok, why):
    global CHECKS
    CHECKS += 1
    if not ok:
        raise ValueError(why)


def number(raw):
    value = float(raw)
    check(math.isfinite(value), 'Nonfinite saved number')
    return F(value)


def parse(raw):
    result = dict(cells={}, faces={}, windows={}, inverse=None, admission=None, thread=None, balance=None, rollback=None, caloric=None, passes=[])
    for line in raw.decode('ascii').splitlines():
        row = line.split(); check(bool(row), 'Empty record'); tag = row[0]
        if tag in ('CELL', 'FACE'):
            key = tuple(map(int, row[1:3])); field = result['cells' if tag == 'CELL' else 'faces']
            check(len(row) == (9 if tag == 'CELL' else 6) and key not in field, 'Endpoint record shape/identity')
            field[key] = tuple(map(number, row[3:]))
        elif tag == 'WINDOW':
            key = int(row[1]); check(len(row) == 18 and key not in result['windows'], 'Window shape/identity')
            result['windows'][key] = (*map(int, row[2:11]), *map(number, row[11:17]), int(row[17]))
        elif tag == 'BALANCE':
            check(result['balance'] is None and len(row) == 8, 'Balance shape/identity')
            result['balance'] = (int(row[1]), *map(number,row[2:]))
        elif tag in ('INVERSE', 'ADMISSION', 'THREAD', 'ROLLBACK', 'CALORIC'):
            field = tag.lower(); check(result[field] is None and len(row) == (8 if tag == 'THREAD' else 2), 'Control shape/identity')
            result[field] = tuple(map(int, row[1:]))
        elif tag == 'PASS':
            result['passes'].append(line)
        else:
            check(False, 'Unknown saved record')
    return result


def initial():
    return [1 + 1e-4 * math.sin(2 * math.pi * (x + .5) / 16) for x in range(16)] * 2


def ode(pressure, mass, count):
    # Independent incidence assembly on the 16x2 torus. No native moment,
    # constitutive-work, Schur, pressure-root or mortar formulas are reused.
    edges = [((x-1) % 16 + 16*y, x+16*y) for y in range(2) for x in range(16)]
    edges += [(x+16*((y-1) % 2), x+16*y) for y in range(2) for x in range(16)]
    inertia = [.5*(mass[l]+mass[r]) for l, r in edges]
    def rhs(state):
        p = [pressure[i] * state[i]**-1.4 for i in range(32)]; out = [0.]*96
        for face, (left, right) in enumerate(edges):
            volume = state[32+face]/inertia[face]
            out[left] += volume; out[right] -= volume; out[32+face] = p[left]-p[right]
        return out
    state = [1.]*32 + [0.]*64; h = .1/count
    for _ in range(count):
        a = rhs(state); b = rhs([s+.5*h*k for s,k in zip(state,a)])
        c = rhs([s+.5*h*k for s,k in zip(state,b)]); d = rhs([s+h*k for s,k in zip(state,c)])
        state = [s+(h/6)*(aa+2*bb+2*cc+dd) for s,aa,bb,cc,dd in zip(state,a,b,c,d)]
    return state[:32], edges, inertia


def review(data, references):
    check(set(data['windows']) == set(range(4)), 'Window coverage')
    check(set(data['cells']) == {(c,i) for c in range(4) for i in range(32)}, 'Cell coverage')
    check(set(data['faces']) == {(c,i) for c in range(4) for i in range(64)}, 'Face coverage')
    pressure = initial()
    for case, (level, fast, mask_kind) in enumerate(CASES):
        row = data['windows'][case]
        lev, coarse, fine, intervals, cn, fn, outer, inner, cycles, pe, te, resolution, residual, bound, closure, storage = row
        expected_fine = (32 if mask_kind == 2 else 8 if mask_kind == 1 else 0)
        check((lev, coarse, fine, intervals) == (level, 32-expected_fine, expected_fine, 32-expected_fine+expected_fine*2**level), 'Prescribed time classes changed')
        check(0 <= cn <= 24 and 0 <= outer <= 24*384 and 0 <= fn <= 24*2**level*100 and inner >= 0 and cycles >= 0 and storage > 0, 'Iteration/storage controls')
        check((coarse > 0 and outer > 0) or (coarse == 0 and outer == 0 and cn == 0), 'Coarse elimination control')
        r1, r2, edges, inertias = references[fast]
        divergence = [F(0)]*32; kinetic = F(0); energy_terms = []; actual_pe = actual_te = actual_resolution = 0.
        for face, (l, r) in enumerate(edges):
            inertia, momentum, q = data['faces'][(case,face)]
            check(inertia == F(inertias[face]), 'Fixed MAC inertia changed')
            kinetic += momentum*momentum/(2*inertia); divergence[l] += q; divergence[r] -= q
        for i in range(32):
            mass, u0, u, v, saved_r1, saved_r2 = data['cells'][(case,i)]
            check(mass == F(.01 if fast and 6 <= i%16 < 10 else 1), 'Scientific mass changed')
            expected_u0 = F(2.5*pressure[i]); check(abs(u0-expected_u0) <= 2*EPS*expected_u0, 'Initial gas disturbance changed')
            check(v > 0 and u > 0, 'Nonpositive endpoint')
            check(abs(v-1-divergence[i]) <= 128*EPS, 'Shared-face geometric closure')
            check(abs(saved_r1-F(r1[i])) <= 128*EPS and abs(saved_r2-F(r2[i])) <= 128*EPS, 'Saved reference differs from independent continuous ODE')
            p = .4*float(u)/float(v); p2 = pressure[i]*float(saved_r2)**-1.4; p1 = pressure[i]*float(saved_r1)**-1.4
            actual_pe = max(actual_pe, abs(p-p2)/1e-4); actual_resolution = max(actual_resolution, abs(p1-p2)/1e-4)
            u2 = float(u0)*float(saved_r2)**-.4
            actual_te = max(actual_te, abs(float(u)-u2)/(float(mass)*2.5))
            energy_terms.extend((u,-u0))
        actual_residual = sum(energy_terms)+kinetic
        actual_bound = 128*EPS*(sum(map(abs,energy_terms))+abs(kinetic))
        check(abs(actual_residual) <= actual_bound, 'Original interval energy account')
        check(abs(residual-actual_residual) <= 16*EPS*abs(kinetic), 'Saved energy defect')
        check(abs(bound-actual_bound) <= 8*EPS*actual_bound, 'Original 128-epsilon bound changed')
        check(0 <= closure <= 128*EPS, 'Constitutive/geometric closure')
        for reported, actual, allowance in ((pe,actual_pe,128*float(EPS)/1e-4), (te,actual_te,128*float(EPS)*100), (resolution,actual_resolution,128*float(EPS)/1e-4)):
            check(abs(float(reported)-actual) <= allowance, 'Saved comparison metric')
        check(actual_pe < .01 and actual_te < .1 and actual_resolution < .001, 'Resolved pressure/temperature tolerance')
    check(data['inverse'] == (7,) and data['admission'] == (12,) and data['caloric'] == (1,), 'Failure/clearing/caloric controls')
    check(data['rollback'] == (1,), 'Mid-window rollback control')
    balance = data['balance']; check(balance is not None and balance[0] == 117, 'Wall/body/vacuum control coverage')
    for residual,bound in zip(balance[1::2],balance[2::2]):
        check(bound > 0 and abs(residual) <= bound, 'Reported wall/body/vacuum account')
    thread = data['thread']; check(thread is not None and thread[:2] == (33389,44461) and all(v > 0 for v in thread[2:]), 'Twelve-worker/cache controls')
    check(data['passes'] == ['PASS pressure-only nonlinear windows, shared faces, energy and resolved references'], 'Native completion marker')


def main():
    raw = Path(sys.argv[1]).read_bytes(); check(len(raw) < 1024*1024, 'Capture cap'); data = parse(raw)
    references = {}
    for fast in (False, True):
        mass = [.01 if fast and 6 <= i%16 < 10 else 1 for i in range(32)]
        r1, edges, inertia = ode(initial(),mass,1000); r2,_,_ = ode(initial(),mass,2000)
        references[fast] = (r1,r2,edges,inertia)
    review(data,references); checks = CHECKS
    mutations = ('mass','source','energy','volume','face_inertia','momentum','flux','reference','rate','intervals','error','bound','missing','inverse','admission','thread','balance','rollback','caloric')
    for mutation in mutations:
        bad = copy.deepcopy(data)
        if mutation in ('mass','source','energy','volume','reference'):
            row = list(bad['cells'][(2,0)]); row[{'mass':0,'source':1,'energy':2,'volume':3,'reference':5}[mutation]] *= 2; bad['cells'][(2,0)] = tuple(row)
        elif mutation in ('face_inertia','momentum','flux'):
            row = list(bad['faces'][(2,0)]); row[{'face_inertia':0,'momentum':1,'flux':2}[mutation]] += 1; bad['faces'][(2,0)] = tuple(row)
        elif mutation in ('rate','intervals','error','bound'):
            row = list(bad['windows'][2]); row[{'rate':0,'intervals':3,'error':9,'bound':13}[mutation]] += 1; bad['windows'][2] = tuple(row)
        elif mutation == 'missing': del bad['faces'][(2,0)]
        elif mutation == 'inverse': bad['inverse'] = (0,)
        elif mutation == 'admission': bad['admission'] = (0,)
        elif mutation == 'balance': bad['balance'] = (117,F(1),*bad['balance'][2:])
        elif mutation == 'rollback': bad['rollback'] = (0,)
        elif mutation == 'caloric': bad['caloric'] = (0,)
        else: bad['thread'] = (33388,*bad['thread'][1:])
        rejected = False
        try: review(bad,references)
        except ValueError: rejected = True
        check(rejected, 'Altered evidence admitted: '+mutation)
    print(json.dumps(dict(Passed=True,SavedChecks=checks,CorruptionsRejected=len(mutations),SavedSha256=hashlib.sha256(raw).hexdigest(),NativeProcessLaunched=False,Scope='Fixed-mass pressure windows only; no full-fluid subcycling, owner or release acceptance')))


if __name__ == '__main__':
    main()
