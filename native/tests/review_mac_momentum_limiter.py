"""Saved-data-only exact-fraction checks of compatible momentum corrections."""
import copy
from fractions import Fraction as F
import hashlib
import json
from pathlib import Path
import sys

EPS = F(1, 2**52)
def check(ok, why):
    if not ok: raise ValueError(why)
def number(text):
    value = float(text)
    check(value == value and abs(value) != float('inf'), 'Nonfinite saved number')
    return F(value)
def encoded(row, index):
    exponent = int(row[index + 1]); check(-16384 < exponent < 16384, 'Review exponent capacity exceeded')
    return number(row[index]) * F(2) ** exponent
def bounded(residual, operands, why):
    check(abs(residual) <= 128 * EPS * sum(map(abs, operands)), why)
def kinetic(mass, j): return j * j / (2 * mass)

def parse(raw):
    data = dict(pairs={}, profiles={}, smooth={}, rejected=None, passes=[])
    for line in raw.decode('ascii').splitlines():
        r = line.split(); check(bool(r), 'Empty native record')
        if r[0] == 'PAIR':
            check(len(r) == 35, 'Pair record shape'); key = int(r[1]); check(key not in data['pairs'], 'Duplicate pair')
            data['pairs'][key] = (number(r[2]), number(r[3]), *map(int, r[4:7]), *[encoded(r, i) for i in range(7, 35, 2)])
        elif r[0] == 'PROFILE':
            check(len(r) == 7, 'Profile record shape'); key = tuple(map(int, r[1:3])); check(key not in data['profiles'], 'Duplicate profile cell')
            data['profiles'][key] = tuple(map(number, r[3:]))
        elif r[0] == 'SMOOTH':
            check(len(r) == 9, 'Smooth record shape'); key = tuple(map(int, r[1:3])); check(key not in data['smooth'], 'Duplicate smooth result')
            data['smooth'][key] = (number(r[3]), int(r[4]), encoded(r, 5), encoded(r, 7))
        elif r[0] == 'REJECTED':
            check(len(r) == 2 and data['rejected'] is None, 'Rejected record shape'); data['rejected'] = int(r[1])
        elif r[0] == 'PASS': data['passes'].append(line)
        else: raise ValueError('Unknown native record: ' + r[0])
    check(set(data['pairs']) == set(range(12)) and len(set(data['passes'])) == len(data['passes']) == 6 and data['rejected'] == 6,
          'Incomplete native groups/pairs/rejections')
    return data

def review(data):
    checks = 0
    for key, row in data['pairs'].items():
        il, ir, backoffs, unresolved_j, unresolved_q, jl, jr, ql, qr, request, alpha, impulse, al, ar, hl, hr, work, dl, dr = row
        check(il > 0 and ir > 0 and ql >= 0 and qr >= 0 and hl >= 0 and hr >= 0 and 0 <= alpha <= 1, 'Pair domain/heat positivity')
        check(0 <= backoffs <= 64 and 0 <= unresolved_j <= 2 and 0 <= unresolved_q <= 2, 'Pair diagnostic range')
        ul, ur = jl / il, jr / ir
        a = (ul - ur) * request / 2
        costs = (request * request / (2 * il) - a, request * request / (2 * ir) - a)
        cap = min([F(1)] + [q / cost for q, cost in zip((ql, qr), costs) if cost > 0 and q < cost])
        bounded(alpha - cap, (alpha, cap), 'Limiter differs from independent sufficient heat bound')
        bounded(impulse - request * alpha, (impulse, request * alpha), 'Applied impulse differs from request/limiter')
        bounded(al - jl + impulse, (al, jl, impulse), 'Left primary momentum correction')
        bounded(ar - jr - impulse, (ar, jr, impulse), 'Right primary momentum correction')
        expected_work = (ul + ur) * impulse / 2
        bounded(work - expected_work, (work, expected_work), 'Compatible energy flux')
        for inertia, before, after, q, h, delta, direction in ((il, jl, al, ql, hl, dl, 1), (ir, jr, ar, qr, hr, dr, -1)):
            expected_delta = (ul - ur) * impulse / 2 - impulse * impulse / (2 * inertia)
            # These separate small accounts cannot hide a bad delta in large Q/K.
            bounded(delta - expected_delta, (delta, (ul - ur) * impulse / 2, impulse * impulse / (2 * inertia)), 'Remainder correction formula')
            bounded(h - q - delta, (h, q, delta), 'Stored remainder update')
            old_k, new_k = kinetic(inertia, before), kinetic(inertia, after)
            bounded(new_k - old_k + h - q + direction * work, (new_k, old_k, h, q, work), 'Local kinetic/heat/work conservation')
            checks += 3
        bounded(al + ar - jl - jr, (al, ar, jl, jr), 'Pair total momentum')
        checks += 9
    base, mirror, boost = (data['pairs'][i] for i in (6, 7, 8))
    check(base[12] == mirror[13] and base[13] == mirror[12] and base[14] == mirror[15] and base[16] == -mirror[16], 'Reflection control')
    check(base[14:16] == boost[14:16] and boost[16] == base[16] + 1024 * base[11], 'Galilean heat/work control')
    check(data['pairs'][3][10] > 0 and data['pairs'][3][10] < 1 and data['pairs'][4][10] == data['pairs'][4][11] == 0, 'Limited and blocked controls')
    check(data['pairs'][5][14] > 0 and data['pairs'][5][15] > 0, 'Zero-budget dissipative control')
    check(data['pairs'][9][11] > 0 and data['pairs'][9][12] < 0 and data['pairs'][10][10] > 0 and data['pairs'][10][10] < F(2)**-1074,
          'Below-range impulse/limiter control')
    check(data['pairs'][11][3] == 2, 'Unresolved primary J control')
    checks += 6
    check(set(data['smooth']) == {(32, 1), (64, 1), (128, 1), (128, 0)}, 'Missing refinement grids/donor control')
    check(set(data['profiles']) == {(n, i) for n in (32, 64, 128) for i in range(n)}, 'Missing profile cell')
    errors = {}
    for n in (32, 64, 128):
        error = old_energy = new_energy = 0
        for i in range(n):
            mass, old, new, internal = data['profiles'][n, i]
            check(mass == F(1, n) and internal > 0, 'Smooth cell inertia/internal energy')
            error += mass * abs(new - old)
            old_energy += mass + mass * old * old / 2
            new_energy += internal + mass * new * new / 2
            checks += 1
        recorded, limited, residual, bound = data['smooth'][n, 1]
        bounded(recorded - error, (recorded, error), 'One-period profile norm')
        bounded(new_energy - old_energy, (new_energy, old_energy), 'One-period stored U+K')
        check(abs(residual) <= bound and limited >= 0, 'Native interval account/limiter count')
        errors[n] = error; checks += 3
    check(errors[64] < F(35, 100) * errors[32] and errors[128] < F(35, 100) * errors[64] and
          errors[128] < F(3, 10) * data['smooth'][128, 0][0], 'Unchanged smooth refinement/donor gate')
    checks += 1
    return dict(Passed=True,Checks=checks,Groups=6,Pairs=12,ProfileCells=224,
                ErrorRatios=[float(errors[64]/errors[32]),float(errors[128]/errors[64])])

def main():
    if len(sys.argv) != 3: raise ValueError('Usage: review_mac_momentum_limiter.py native.stdout.raw fresh-review.json')
    path = Path(sys.argv[1]); raw = path.read_bytes(); data = parse(raw); result = review(data)
    mutations = []
    for name, mutate in (
        ('impulse', lambda d: d['pairs'].__setitem__(0, (*d['pairs'][0][:11], d['pairs'][0][11] * 2, *d['pairs'][0][12:]))),
        ('heat', lambda d: d['pairs'].__setitem__(0, (*d['pairs'][0][:14], -d['pairs'][0][14], *d['pairs'][0][15:]))),
        ('profile', lambda d: d['profiles'].__setitem__((128, 0), (*d['profiles'][128, 0][:2], d['profiles'][128, 0][2] + F(1, 100), d['profiles'][128, 0][3])))):
        bad = copy.deepcopy(data); mutate(bad)
        try: review(bad)
        except ValueError: mutations.append(name)
        else: raise ValueError('Review accepted corrupted ' + name)
    result.update(InputSha256=hashlib.sha256(raw).hexdigest(),CorruptionsRejected=mutations,IndependentAgentReview=False,
                  Scope='Pair/batch kinetic-remainder limiter and scalar advection only; no full Euler/world/game acceptance')
    with Path(sys.argv[2]).open('x') as stream: json.dump(result, stream, indent=2)
    print(json.dumps(result))
if __name__ == '__main__': main()
