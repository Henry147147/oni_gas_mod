"""Saved operands only: exact rational balances and independent decimal roots."""
import copy
from decimal import Decimal, localcontext
from fractions import Fraction as F
import hashlib
import json
from pathlib import Path
import sys

EPS = F(1, 2**52)
CHECKS = 0


def check(ok, why):
    global CHECKS
    CHECKS += 1
    if not ok:
        raise ValueError(why)


def number(text):
    x = float(text)
    check(x == x and abs(x) != float('inf'), 'Nonfinite significand')
    return F(x)


def scaled(row, i, scale=0):
    m, e = number(row[i]), int(row[i + 1])
    check((m == 0 and e == 0) or F(1, 2) <= abs(m) < 1, 'Noncanonical range value')
    if not m:
        return F(0)
    e -= scale
    check(-32768 <= e <= 32768, 'Normalized review capacity')
    return m * F(2)**e


def bounded(residual, operands, why):
    check(abs(residual) <= 128 * EPS * sum(map(abs, operands)), why)


def parse(raw):
    d = dict(pairs={}, budgets={}, halves={}, roots=[], passes=[], continuation=None, rejected=None)
    # J, heat, request, theta, impulse, after J, heat, work and heat changes.
    dimensions = (1, 1, 2, 2, 1, 0, 1, 1, 1, 2, 2, 2, 2, 2)
    for line in raw.decode('ascii').splitlines():
        r = line.split()
        check(bool(r), 'Empty record')
        if r[0] == 'QPAIR':
            check(len(r) == 36, 'Quadratic pair shape')
            key, scale = int(r[1]), int(r[2])
            check(key not in d['pairs'], 'Duplicate pair')
            d['pairs'][key] = (scale, number(r[3]), number(r[4]), *map(int, r[5:8]),
                *[scaled(r, 8 + 2*i, dimension*scale) for i, dimension in enumerate(dimensions)])
        elif r[0] == 'QBUDGET':
            key = int(r[1]), int(r[2])
            check(key not in d['budgets'] and key[0] in d['pairs'] and key[1] in (0, 1), 'Budget identity')
            count = int(r[3])
            check(0 <= count <= 4096 and len(r) == 4 + 2*count, 'Budget shape')
            d['budgets'][key] = [scaled(r, 4 + 2*i, 2*d['pairs'][key[0]][0]) for i in range(count)]
        elif r[0] == 'QHALF':
            check(len(r) == 5 and int(r[1]) not in d['halves'], 'Half shape/identity')
            d['halves'][int(r[1])] = tuple(map(int, r[2:]))
        elif r[0] == 'QSQRT':
            check(len(r) == 6, 'Square root shape')
            d['roots'].append((int(r[1]), number(r[2]), int(r[3]), number(r[4]), int(r[5])))
        elif r[0] == 'QCONTINUATION':
            check(len(r) == 2 and d['continuation'] is None, 'Continuation shape')
            d['continuation'] = int(r[1])
        elif r[0] == 'QREJECTED':
            check(len(r) == 2 and d['rejected'] is None, 'Rejection shape')
            d['rejected'] = int(r[1])
        elif r[0] == 'PASS':
            d['passes'].append(line)
        else:
            raise ValueError('Unknown record ' + r[0])
    check(set(d['pairs']) == set(range(275)) and set(d['halves']) == set(range(15)) and len(d['roots']) == 60,
          'Incomplete roots or pairs')
    check(len(set(d['passes'])) == len(d['passes']) == 8 and d['continuation'] == 72 and d['rejected'] == 6,
          'Incomplete groups/continuations/rejections')
    check(set(d['budgets']) == {(i, s) for i in (8, 9, 10) for s in (0, 1)}, 'Missing/orphan work budgets')
    return d


def decimal_fraction(x):
    return Decimal(x.numerator) / Decimal(x.denominator)


def root_limit(q, a, b):
    # Independent high precision root of B theta^2 - A theta - Q = 0.
    # The native implementation uses normalized range arithmetic, not Decimal.
    if q + a - b >= 0:
        return F(1)
    if not q:
        return max(F(0), a/b)
    with localcontext() as ctx:
        ctx.prec = 110
        dq, da, db = map(decimal_fraction, (q, a, b))
        radical = (da*da + 4*db*dq).sqrt()
        theta = (2*dq/(radical-da)) if a <= 0 else ((da+radical)/(2*db))
        return F(theta)


def review(d):
    for e, half, odd in d['halves'].values():
        check(half == e//2 and odd == e % 2, 'Independent signed integer half/parity')
    seen = set()
    for key, m, e, root, re in d['roots']:
        check((key, m) not in seen and key in d['halves'] and d['halves'][key][0] == e, 'Root source identity')
        seen.add((key, m))
        check(F(1, 2) <= m < 1 and F(1, 2) <= root < 1 and -2 <= 2*re-e <= 2, 'Normalized root/exponent')
        square = root*root*F(2)**(2*re-e)
        check(abs(square-m) <= 4*EPS*m, 'Independent square root relative error')
    for key, row in d['pairs'].items():
        scale, il, ir, backoffs, unresolved_j, unresolved_q, jl, jr, ql, qr, request, alpha, impulse, al, ar, hl, hr, work, dl, dr = row
        check(il > 0 and ir > 0 and min(ql, qr, hl, hr) >= 0 and 0 <= alpha <= 1, 'Pair physical domain')
        check(0 <= backoffs <= 64 and 0 <= unresolved_j <= 2 and 0 <= unresolved_q <= 2, 'Pair diagnostics')
        ul, ur = jl/il, jr/ir
        a = (ul-ur)*request/2
        if request:
            cap = min(root_limit(ql, a, request*request/(2*il)), root_limit(qr, a, request*request/(2*ir)))
        else:
            cap = F(1)
        if (key, 0) in d['budgets']:
            left, right = (sum(d['budgets'][key, s]) for s in (0, 1))
            check(left >= 0 and right >= 0 and left-work >= 0 and right+work >= 0, 'Exact additive transported work capacity')
            requested_work = (ul+ur)*request/2
            if requested_work:
                cap = min(cap, (left if requested_work > 0 else right)/abs(requested_work))
        # At most 64 representable backoffs plus coefficient/root rounding;
        # conservation retains its separate original 128-epsilon bounds.
        bounded(alpha-cap, (alpha, cap), 'Independent maximal quadratic heat/work admission')
        bounded(impulse-request*alpha, (impulse, request*alpha), 'Applied quadratic correction')
        bounded(al-jl+impulse, (al, jl, impulse), 'Corrected left momentum')
        bounded(ar-jr-impulse, (ar, jr, impulse), 'Corrected right momentum')
        bounded(work-(ul+ur)*impulse/2, (work, (abs(ul)+abs(ur))*impulse), 'Compatible transferred work')
        for inertia, before, after, q, h, delta, sign in ((il, jl, al, ql, hl, dl, 1), (ir, jr, ar, qr, hr, dr, -1)):
            linear, quadratic = (ul-ur)*impulse/2, impulse*impulse/(2*inertia)
            bounded(delta-linear+quadratic, (delta, linear, quadratic, (abs(ul)+abs(ur))*impulse), 'Actual quadratic heat change')
            bounded(h-q-delta, (h, q, delta), 'Stored remainder heat')
            before_k, after_k = before*before/(2*inertia), after*after/(2*inertia)
            bounded(after_k-before_k+h-q+sign*work, (after_k, before_k, h, q, work), 'Local kinetic heat work identity')
        bounded(al+ar-jl-jr, (al, ar, jl, jr), 'Pair total momentum')
    check(d['pairs'][0][11] == F(1, 2) and d['pairs'][0][15:17] == (0, 0), 'Admissible correction at zero initial heat')
    check(d['pairs'][1][11] == d['pairs'][2][11] == F(1, 2) and d['pairs'][5][11] == 1 and d['pairs'][6][11] == 0,
          'Known negative/zero/endpoint/blocked roots')
    check(d['pairs'][8][11] < 1 and d['pairs'][9][11] < 1 and d['pairs'][8][3] > 0 and d['pairs'][9][3] > 0,
          'Near-empty additive work remainder backoffs')
    check(0 < d['pairs'][11][11] < F(2)**-1074 and d['pairs'][12][0] < -(2**63) and d['pairs'][12][12] > 0 and d['pairs'][13][4] == 2,
          'Full-range root/impulse/unresolved momentum controls')
    check(d['pairs'][3][13] == d['pairs'][4][14] and d['pairs'][3][14] == d['pairs'][4][13] and
          d['pairs'][3][15] == d['pairs'][4][16] and d['pairs'][3][17] == -d['pairs'][4][17], 'Pair reflection')
    # Rebuild the five sequential batch edges and total energy independently.
    j, q = list(map(F, (1, 0, -.5, 2))), list(map(F, (.1, .05, .2, .01)))
    inertia = list(map(F, (1, 2, .5, 1)))
    initial_j = sum(j)
    initial_energy = sum(x*x/(2*m)+h for x, m, h in zip(j, inertia, q))
    for key, (left, right) in enumerate(((0, 1), (1, 2), (2, 3), (3, 0), (0, 2)), 270):
        p = d['pairs'][key]
        check(p[1:3] == (inertia[left], inertia[right]) and p[6:10] == (j[left], j[right], q[left], q[right]), 'Sequential batch operands')
        j[left], j[right], q[left], q[right] = p[13:17]
    bounded(sum(j)-initial_j, (*j, initial_j), 'Batch momentum balance')
    final_energy = sum(x*x/(2*m)+h for x, m, h in zip(j, inertia, q))
    bounded(final_energy-initial_energy, (initial_energy, final_energy), 'Batch kinetic heat balance')
    return dict(Passed=True, Checks=CHECKS, Groups=8, Pairs=275, SquareRoots=60, ExponentHalves=15,
                InternalContinuationCases=72, NativeProcessLaunched=False, IndependentAgentReview=False,
                Scope='Independent pair/root/batch algebra; owner continuation self-checks only; no strong-flow, world, game or performance acceptance')


def main():
    if len(sys.argv) != 3:
        raise ValueError('Usage: review_mac_quadratic_limiter.py native.stdout.raw fresh-review.json')
    raw = Path(sys.argv[1]).read_bytes()
    d = parse(raw)
    result = review(d)
    mutations = []
    for name, mutate in (
        ('limiter', lambda x: x['pairs'].__setitem__(0, (*x['pairs'][0][:11], F(1), *x['pairs'][0][12:]))),
        ('heat', lambda x: x['pairs'].__setitem__(1, (*x['pairs'][1][:16], F(-1), *x['pairs'][1][17:]))),
        ('root-exponent', lambda x: x['roots'].__setitem__(0, (*x['roots'][0][:4], x['roots'][0][4]+1))),
        ('work-expansion', lambda x: x['budgets'].__setitem__((8, 0), [F(7, 8)])),
    ):
        bad = copy.deepcopy(d)
        mutate(bad)
        try:
            review(bad)
        except ValueError:
            mutations.append(name)
        else:
            raise ValueError('Review accepted corrupted ' + name)
    result.update(InputSha256=hashlib.sha256(raw).hexdigest(), CorruptionsRejected=mutations)
    with Path(sys.argv[2]).open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
