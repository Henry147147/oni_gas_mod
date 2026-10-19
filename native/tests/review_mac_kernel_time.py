"""Review saved shared-face time integrals with exact binary64 fractions."""
import copy
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

CHECKS = 0
EPS = F(2) ** -52
INPUTS = ((.125, .7, -.2, 2.3, .8, .1, 0), (-.125, -.7, .2, 2.3, .8, .1, 3),
          (.125, 0, .2, 2.3, .8, .1, 0), (.125, -2.5, 0, 2.3, .8, .1, 0),
          (2.**-700, 0, 0, 2.**420, 1, .002, 0),
          (2.**-700, 0, 0, 2.**420, 2.**500, .002, 0),
          (0, 2.**1000, 0, 1, 2.**1000, 2.**-1000, 20),
          (0, 2.**1000, 0, 1, 2.**1000, 2.**-1074, 20),
          (0, 2.**-1074, 0, 1, 2.**500, 2.**500, 0))
PAIRS = ((0, 0), (0, 2), (3, 0), (2, 3))


def check(ok, why):
    global CHECKS
    CHECKS += 1
    if not ok:
        raise ValueError(why)


def number(s):
    v = float(s); check(math.isfinite(v), 'Nonfinite saved value'); return F(v)


def close(actual, expected, operands, why):
    check(abs(actual - expected) <= 128 * EPS * sum(map(abs, operands)), why)


def parse(raw):
    data = dict(version=None, root3=None, faces={}, values={}, pieces={}, pairs={}, thread=None, invalid=None, passes=[])
    for line in raw.decode('ascii').splitlines():
        row = line.split(); check(bool(row), 'Empty record'); tag = row[0]
        if tag == 'TVERSION':
            check(len(row) == 3 and data['version'] is None, 'Version shape'); data['version'] = int(row[1]); data['root3'] = number(row[2])
        elif tag == 'TFACE':
            i = int(row[1]); check(len(row) == 10 and i not in data['faces'], 'Face shape/identity')
            data['faces'][i] = (*map(number, row[2:8]), int(row[8]), int(row[9]))
        elif tag in ('TJ', 'TQ', 'TB', 'TMOMENT'):
            key = (tag, int(row[1]), int(row[2])); check(len(row) == 5 and key not in data['values'], 'Scaled shape/identity')
            exponent = int(row[4]); check(-4096 <= exponent <= 4096, 'Saved exponent cap')
            data['values'][key] = number(row[3]) * F(2) ** exponent
        elif tag == 'TPIECE':
            key = (int(row[1]), int(row[4])); check(len(row) == 25 and key not in data['pieces'], 'Piece shape/identity')
            data['pieces'][key] = (int(row[2]), int(row[3]), *map(number, row[5:14]), int(row[14]), int(row[15]), *map(number, row[16:]))
        elif tag == 'TPAIR':
            i = int(row[1]); check(len(row) == 4 and i not in data['pairs'], 'Pair shape/identity'); data['pairs'][i] = (int(row[2]), number(row[3]))
        elif tag == 'TTHREAD':
            check(len(row) == 4 and data['thread'] is None, 'Thread shape'); data['thread'] = tuple(map(int, row[1:]))
        elif tag == 'TINVALID':
            check(len(row) == 2 and data['invalid'] is None, 'Admission shape'); data['invalid'] = int(row[1])
        elif tag == 'PASS':
            data['passes'].append(line)
        else:
            check(False, 'Unknown saved record')
    return data


def integrals(j, f0, f1, inertia, area, seconds, root3):
    a = j * seconds * area / inertia
    b = f0 * seconds**2 * area / (2 * inertia)
    c = -root3 * f1 * seconds**2 * area / (6 * inertia)
    q = a + b + c; first = root3 * f0 * seconds**2 * area / (6 * inertia)
    return j + seconds * f0, q, first, (a, b, c)


def review(d):
    check(d['version'] == 1 and d['root3'] == F(math.sqrt(3)), 'Version/basis constant')
    r3 = d['root3']; check(set(d['faces']) == set(range(9)), 'Face coverage')
    keys = {('TMOMENT', 0, 0)}
    for i, inputs in enumerate(INPUTS):
        row = d['faces'][i]; check(row[:7] == tuple(map(F, inputs)), 'Scientific face inputs changed')
        check(row[7] == (1 if i >= 4 else 0), 'Normal/flagged route changed')
        j, f0, f1, inertia, area, dt, level = row[:7]; h = dt / F(2)**level
        expected_j, q, b, operands = integrals(j, f0, f1, inertia, area, h, r3)
        values = []
        for tag in ('TJ', 'TQ', 'TB'):
            key = (tag, i, 0); keys.add(key); check(key in d['values'], 'Missing scaled endpoint'); values.append(d['values'][key])
        J, Q, B = values
        close(J, expected_j, (j, h*f0), 'Momentum primitive')
        close(Q, q, operands, 'Shared zeroth volume moment')
        close(B, b, (b,), 'Shared first volume moment')
        old = j*j/(2*inertia); new = J*J/(2*inertia); w0 = f0*Q/area; w1 = f1*B/area
        close(new-old, w0+w1, (old, new, w0, w1), 'Original kinetic/work account')
    check(set(d['values']) == keys and d['values'][('TMOMENT', 0, 0)] == F(2)**-1080, 'Flagged basis product/extra scalar')
    check(set(d['pairs']) == set(range(4)), 'Local time pair coverage'); piece_keys = set()
    for i, (left, right) in enumerate(PAIRS):
        count = 2**max(left, right); previous = F(.125); check(d['pairs'][i][0] == count, 'Pair count')
        for s in range(count):
            key = (i, s); piece_keys.add(key); check(key in d['pieces'], 'Missing physical piece')
            row = d['pieces'][key]
            l, r, j, lp, ly, rp, ry, inertia, area, body, dt, li, ri, lm, lr, rm, rr, J, Q, B, LB, RB = row
            check((l, r) == (left, right) and j == previous and (inertia, area, body, dt) == tuple(map(F, (2, .75, .03125, .1))), 'Piece inputs/continuity')
            for level, index, midpoint, relative in ((left, li, lm, lr), (right, ri, rm, rr)):
                stride = 2**(max(left, right)-level)
                check(index == s//stride and relative == F(1, stride), 'Local clock mapping')
                expected_mid = r3 * (F(2*s% (2*stride) + 1, stride) - 1)
                close(midpoint, expected_mid, (r3,), 'Local P1 midpoint')
            for actual, expected in ((lp,F(.5)+F(.03)*li), (ly,F(.125)-F(.01)*li), (rp,F(.25)+F(.02)*ri), (ry,F(-.0625)+F(.015)*ri)):
                close(actual, expected, (expected,), 'Prescribed pressure trace changed')
            f0 = area*((lp+lm*ly)-(rp+rm*ry))+body; f1 = area*(lr*ly-rr*ry)
            ej, eq, eb, operands = integrals(j, f0, f1, inertia, area, dt/count, r3)
            close(J, ej, (j,(dt/count)*f0), 'Piece momentum')
            close(Q, eq, operands, 'Piece zeroth integral'); close(B, eb, (eb,), 'Piece first integral')
            close(LB, lm*Q+lr*B, (lm*Q,lr*B), 'Left cell moment'); close(RB, rm*Q+rr*B, (rm*Q,rr*B), 'Right cell moment')
            old=j*j/(2*inertia);new=J*J/(2*inertia);terms=(lp*Q,ly*LB,-rp*Q,-ry*RB,body*Q/area)
            close(new-old,sum(terms), (old,new,*terms), 'Different local bases lost kinetic work')
            previous=J
        check(d['pairs'][i][1] == previous, 'Pair endpoint')
    check(set(d['pieces']) == piece_keys and d['thread'] == (32768,12,1) and d['invalid'] == 7, 'Coverage/thread/admission controls')
    check(d['passes'] == ['PASS all dyadic level pairs and flagged local-basis products', 'PASS shared quadratic face paths and different local time bases'], 'Native completion controls')


def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<1024*1024,'Pipe cap');data=parse(raw);review(data);saved=CHECKS
    mutations=('force','slope','inertia','area','level','momentum','volume','first','basis','cell_moment','missing','threads','constant','tiny')
    for mutation in mutations:
        bad=copy.deepcopy(data)
        if mutation in ('force','slope','inertia','area','level'):
            row=list(bad['faces'][0]);row[{'force':1,'slope':2,'inertia':3,'area':4,'level':6}[mutation]]*=2;bad['faces'][0]=tuple(row)
            if mutation=='level': row[6]=1;bad['faces'][0]=tuple(row)
        elif mutation in ('momentum','volume','first'):
            tag={'momentum':'TJ','volume':'TQ','first':'TB'}[mutation];bad['values'][(tag,0,0)]*=2
        elif mutation in ('basis','cell_moment'):
            row=list(bad['pieces'][(1,0)]);row[13 if mutation=='basis' else 20]*=2;bad['pieces'][(1,0)]=tuple(row)
        elif mutation=='missing':del bad['pieces'][(1,0)]
        elif mutation=='threads':bad['thread']=(32768,1,1)
        elif mutation=='constant':bad['root3']*=2
        else:bad['values'][('TMOMENT',0,0)]=0
        rejected=False
        try:review(bad)
        except ValueError:rejected=True
        check(rejected,'Altered evidence accepted: '+mutation)
    print(json.dumps(dict(Passed=True,SavedChecks=saved,CorruptionsRejected=len(mutations),SavedSha256=hashlib.sha256(raw).hexdigest(),NativeProcessLaunched=False,Scope='Shared time-face integration only; no full MAC subcycling or release acceptance')))


if __name__=='__main__':main()
