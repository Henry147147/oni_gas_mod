"""Saved-data-only checks of the separate compatible compression component."""
import copy
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location('closed_reference', Path(__file__).with_name('review_mac_closed_world.py'))
ref = importlib.util.module_from_spec(spec); spec.loader.exec_module(ref)
base = ref.base
check, close, number, unique = base.check, base.close, base.number, base.unique
EPS = base.EPS


def scaled(row, i):
    return base.scaled_energy(row[i], row[i + 1])


def round_scaled(value):
    if not value:
        return F(0)
    e = abs(value.numerator).bit_length() - value.denominator.bit_length()
    check(-16384 < e < 16384, 'Saved reference magnitude exceeds declared domain')
    power = F(2) ** e
    if abs(value) < power:
        e -= 1
    return F(float(value / (F(2) ** (e + 1)))) * F(2) ** (e + 1)


def parse(raw, required_passes=6, required_grids=(0, 1, 2, 3), analytic=True):
    data = {key: {} for key in ('pairs', 'grids', 'events', 'cells', 'masses', 'passives', 'faces', 'walls', 'materials')}
    passes = []; controls = {}
    for line in raw.decode('ascii').splitlines():
        r = line.split(); kind = r[0]
        if kind == 'PAIR':
            check(len(r) == 27 and r[4] in ('0', '1') and r[5] in ('0', '1'), 'Pair record shape')
            unique(data['pairs'], int(r[1]), (number(r[2]), number(r[3]), int(r[4]), int(r[5]), int(r[6]), *[scaled(r, i) for i in range(7, 27, 2)]))
        elif kind == 'GRID':
            check(len(r) == 24, 'Grid record shape')
            unique(data['grids'], int(r[1]), (int(r[2]), int(r[3]), number(r[4]), number(r[5]), int(r[6]), int(r[7]), number(r[8]), *map(int, r[9:12]), *[scaled(r, i) for i in range(12, 22, 2)], number(r[22]), number(r[23])))
        elif kind == 'EVENT':
            check(len(r) == 7, 'Event record shape')
            unique(data['events'], tuple(map(int, r[1:3])), tuple(map(int, r[3:])))
        elif kind == 'SCELL':
            check(len(r) == 8, 'Cell record shape'); unique(data['cells'], tuple(map(int, r[1:4])), tuple(map(number, r[4:])))
        elif kind in ('SMASS', 'SPASSIVE'):
            check(len(r) == 6, 'Constituent/passive shape'); unique(data['masses' if kind == 'SMASS' else 'passives'], tuple(map(int, r[1:5])), number(r[5]))
        elif kind == 'SFACE':
            check(len(r) == 9 and r[6] in ('0', '1'), 'Face record shape')
            unique(data['faces'], tuple(map(int, r[1:5])), (number(r[5]), int(r[6]), scaled(r, 7)))
        elif kind == 'SOLID':
            check(len(r) == 4 and r[3] in ('0', '1'), 'Solid shape'); unique(data['walls'], tuple(map(int, r[1:3])), int(r[3]))
        elif kind == 'MATERIAL':
            check(len(r) == 8, 'Material shape'); unique(data['materials'], int(r[1]), (int(r[2]), *map(number, r[3:])))
        elif kind in ('REJECTED', 'INACTIVE'):
            check(len(r) == 2, 'Control shape'); unique(controls, kind, int(r[1]))
        elif kind == 'PASS':
            passes.append(line)
        else:
            raise ValueError('Unknown record: ' + kind)
    check(len(passes) == len(set(passes)) == required_passes and controls == ({'REJECTED': 8, 'INACTIVE': 1} if analytic else {}), 'Missing native groups/controls')
    check(set(data['grids']) == set(required_grids) and len(data['materials']) == 4, 'Missing grids/materials')
    if analytic:
        check(set(range(12)).issubset(data['pairs']), 'Missing analytic pair controls')
    return data


def kinetic(inertia, momentum):
    check(inertia >= 0 and (inertia > 0 or momentum == 0), 'Invalid face kinetic state')
    return momentum * momentum / (2 * inertia) if inertia else F(0)


def resolved(before, after, request):
    return abs(after - before - request) <= 32 * EPS * abs(request)


def review_pair(p):
    il, ir, fl, fr, unresolved, jl, jr, b, c, k, impulse, heat, nl, nr, wall = p
    check(il > 0 and ir > 0 and b >= 0 and (not fl or jl == nl == 0) and (not fr or jr == nr == 0), 'Pair domain/fixed endpoints')
    ul, ur = jl / il, jr / ir
    close(c, ul - ur, abs(ul) + abs(ur), 'Pair closing velocity', 8)
    if c <= 0 or b == 0:
        check(k == impulse == heat == wall == unresolved == 0 and nl == jl and nr == jr, 'Inactive pair mutated')
        return
    a = (F(0) if fl else 1 / il) + (F(0) if fr else 1 / ir)
    check(a > 0 and k > 0 and impulse > 0 and heat > 0, 'Positive compression contribution disappeared')
    close(k, b * c * a, k, 'Pair relaxation number', 8)
    exact = b * c * c / (1 + b * c * a)
    close(impulse, exact, exact, 'Exact nonlinear pair impulse', 16)
    q = impulse * (c - a * impulse / 2)
    check(q > 0, 'Nonpositive compatible work')
    close(heat, q, q, 'Compatible pair heat', 16)
    close(nl, F(0) if fl else jl - impulse, abs(jl) + impulse, 'Left pair update', 2)
    close(nr, F(0) if fr else jr + impulse, abs(jr) + impulse, 'Right pair update', 2)
    check(wall == (impulse if fl else -impulse if fr else 0), 'Wall impulse convention')
    check(unresolved == sum((not fl and not resolved(jl, nl, -impulse), not fr and not resolved(jr, nr, impulse))), 'Unresolved impulse count differs')
    terms = [kinetic(il, nl), kinetic(ir, nr), -kinetic(il, jl), -kinetic(ir, jr), heat]
    close(sum(terms, F(0)), F(0), sum(map(abs, terms), F(0)), 'Actual pair kinetic/heat account')
    terms = [nl, nr, -jl, -jr, -wall]
    close(sum(terms, F(0)), F(0), sum(map(abs, terms), F(0)), 'Actual pair momentum/wall account')
    scale = abs(ul) + abs(ur)
    for velocity in (nl / il, nr / ir):
        check(min(ul, ur) - 128 * EPS * scale <= velocity <= max(ul, ur) + 128 * EPS * scale, 'Pair velocity outside initial interval')


def review(data, analytic=True):
    for p in data['pairs'].values():
        review_pair(p)
    if analytic:
        p0, p4, p5 = (data['pairs'][i] for i in (0, 4, 5))
        for i in (12, 13):
            close(p5[i], p0[i], abs(p0[i]), 'Pair semigroup', 128)
        close(p4[11] + p5[11], p0[11], p0[11], 'Pair semigroup heat', 128)
        check(data['pairs'][10][7:12] == data['pairs'][11][7:12], 'Galilean compression/work differs')
    referenced = set(range(12)) if analytic else set()
    for gid, grid in data['grids'].items():
        w, h, spacing, depth, px, py, dt, count, unresolved_u, unresolved_j, reported_heat, residual, bound, wx, wy, ds, sb = grid
        check(w > 1 and h > 1 and spacing > 0 and depth > 0 and dt > 0 and px in (0, 1) and py in (0, 1), 'Grid domain')
        volume = spacing * spacing * depth; xw = w + (not px); yh = h + (not py)
        walls = {i: data['walls'][gid, i] for i in range(w * h)}
        cells = {i: data['cells'][gid, 0, i][0] for i in walls if not walls[i]}
        current = {}; old = {}; masses = {}; flags = {}
        for axis, nf in ((0, xw * h), (1, w * yh)):
            for f in range(nf):
                m, flag, j = data['faces'][gid, axis, 0, f]
                current[axis, f] = old[axis, f] = j; masses[axis, f] = m; flags[axis, f] = flag
                x, y = (f % xw, f // xw) if axis == 0 else (f % w, f // w)
                neighbours = ((None if not px and x == 0 else (x - 1) % w + y * w, None if not px and x == w else x % w + y * w)
                    if axis == 0 else (None if not py and y == 0 else x + ((y - 1) % h) * w, None if not py and y == h else x + (y % h) * w))
                present = [i for i in neighbours if i is not None and not walls[i]]
                check(flag == (len(present) != 2) and (not flag or j == 0), 'Fixed geometry/momentum differs')
                expected = sum((sum(v for (g, e, c, _), v in data['masses'].items() if (g, e, c) == (gid, 0, i)) / 2 for i in present), F(0))
                close(m, expected, expected, 'Face inertia from unchanged cell masses', 8)
        index = 0; total_heat = F(0); total_wall = [F(0), F(0)]; un_u = un_j = 0
        for axis in (0, 1):
            for sweep in (0, 1):
                for cell in (range(w * h) if sweep == 0 else reversed(range(w * h))):
                    if walls[cell]:
                        continue
                    x, y = cell % w, cell // w
                    l, r = (x + y * xw, (x + 1) % xw + y * xw) if axis == 0 else (x + y * w, x + ((y + 1) % yh) * w)
                    jl, jr = current[axis, l], current[axis, r]; il, ir = masses[axis, l], masses[axis, r]
                    closing = round_scaled(round_scaled(jl / il) - round_scaled(jr / ir))
                    if closing <= 0:
                        continue
                    check((gid, index) in data['events'], 'Missing positive compression event')
                    a, c, s, key = data['events'][gid, index]; index += 1; referenced.add(key)
                    check((a, c, s) == (axis, cell, sweep), 'Event order/geometry differs')
                    p = data['pairs'][key]
                    check(p[:4] == (il, ir, flags[axis, l], flags[axis, r]) and p[5:7] == (jl, jr) and p[8] == closing, 'Event source differs')
                    cmass = sum(v for (g, e, c, _), v in data['masses'].items() if (g, e, c) == (gid, 0, cell))
                    coefficient = dt * cmass / (2 * spacing)
                    close(p[7], coefficient, coefficient, 'Integrated density/area/time coefficient', 8)
                    current[axis, l], current[axis, r] = p[12:14]
                    u0 = cells[cell]; cells[cell] = F(float(u0 + p[11])); un_u += not resolved(u0, cells[cell], p[11]); un_j += p[4]
                    total_heat += p[11]; total_wall[axis] += p[14]
        check(index == count and unresolved_u == un_u and unresolved_j == un_j, 'Event/unresolved counts differ')
        close(reported_heat, total_heat, total_heat, 'Reported full heat', 8)
        close(wx, total_wall[0], abs(total_wall[0]), 'Reported x wall impulse', 8)
        close(wy, total_wall[1], abs(total_wall[1]), 'Reported y wall impulse', 8)
        terms = []; entropy = D(0); entropy_scale = F(0)
        for cell in cells:
            a, b = (data['cells'][gid, e, cell] for e in (0, 1)); check(b[0] == cells[cell] and b[0] >= a[0], 'Represented cell heat differs/decreases U')
            saved_mass = {s: v for (g, e, c, s), v in data['masses'].items() if (g, e, c) == (gid, 0, cell)}
            for field in ('masses', 'passives'):
                old_values = {s: v for (g, e, c, s), v in data[field].items() if (g, e, c) == (gid, 0, cell)}
                new_values = {s: v for (g, e, c, s), v in data[field].items() if (g, e, c) == (gid, 1, cell)}
                check(old_values == new_values and all(v > 0 for v in old_values.values()), 'Constituent/passive changed')
            for point in (a, b):
                p, t, _, s = ref.equilibrium(saved_mass, data['materials'], point[0], volume)
                close(point[1], F(t), abs(F(t)), 'Independent EOS temperature', 4096)
                close(point[2], F(p), abs(F(p)), 'Independent EOS pressure', 4096)
                close(point[3], F(s), sum(abs(v) for v in point) + abs(F(s)), 'Independent EOS entropy', 4096)
            s0 = ref.equilibrium(saved_mass, data['materials'], a[0], volume)[3]
            s1 = ref.equilibrium(saved_mass, data['materials'], b[0], volume)[3]
            entropy += s1 - s0; entropy_scale += abs(a[0]) / a[1] + abs(b[0]) / b[1]
            terms.extend((b[0], -a[0]))
        for axis, nf in ((0, xw * h), (1, w * yh)):
            impulse_terms = [-(wx if axis == 0 else wy)]
            for f in range(nf):
                m, flag, j1 = data['faces'][gid, axis, 1, f]; j0 = old[axis, f]
                check(m == masses[axis, f] and flag == flags[axis, f] and j1 == current[axis, f], 'Final face differs from event replay')
                terms.extend((kinetic(m, j1), -kinetic(m, j0))); impulse_terms.extend((j1, -j0))
            close(sum(impulse_terms, F(0)), F(0), sum(map(abs, impulse_terms), F(0)), 'Global impulse/wall account')
        actual, scale = sum(terms, F(0)), sum(map(abs, terms), F(0))
        check(abs(actual) <= bound and bound > 0, 'Represented U+K bound')
        close(residual, actual, scale, 'Reported energy residual', 2)
        close(bound, 128 * EPS * scale, 128 * EPS * scale, 'Original actual-operand bound', 512)
        close(sb, 128 * EPS * entropy_scale, 128 * EPS * entropy_scale, 'Entropy roundoff bound', 512)
        check(entropy >= -ref.dec(sb) and ds >= -sb, 'EOS entropy decreased')
        close(ds, F(entropy), entropy_scale, 'Reported entropy change', 128)
        if analytic and gid == 3:
            check(reported_heat > 0 and float(reported_heat) == 0 and un_u > 0 and un_j > 0, 'Subrange heat/control missing')
    check(referenced == set(data['pairs']), 'Missing/orphan pair')
    check(set(data['events']) == {(gid, k) for gid, grid in data['grids'].items() for k in range(grid[7])}, 'Extra event')


def main():
    path = Path(sys.argv[1]); raw = path.read_bytes(); data = parse(raw)
    with localcontext() as ctx:
        ctx.prec = 80; review(data)
        saved_checks = base.CHECKS; rejected = 0
        for kind in range(3):
            bad = copy.deepcopy(data)
            if kind < 2:
                p = list(bad['pairs'][0]); p[10 if kind == 0 else 11] *= F(17, 16); bad['pairs'][0] = tuple(p)
            else:
                key = next(k for k in bad['masses'] if k[1] == 1); bad['masses'][key] *= F(17, 16)
            try:
                review(bad)
            except ValueError:
                rejected += 1
        check(rejected == 3, 'Corruption escaped saved-data review')
    print(json.dumps(dict(Passed=True, SavedSha256=hashlib.sha256(raw).hexdigest(), SavedChecks=saved_checks, CorruptionsRejected=rejected,
        NativeProcessLaunched=False, IndependentAgentReview=False, Scope='Separate compression-pair and occupied-grid component only; no closed-owner, strong-flow, production, game or performance acceptance')))


if __name__ == '__main__':
    main()
