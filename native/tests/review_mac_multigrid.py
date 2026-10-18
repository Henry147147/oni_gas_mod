"""Saved-only multigrid topology, transfer and V-cycle oracle."""
import copy
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import hashlib
import json
import math
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
    check(math.isfinite(x), 'Nonfinite saved value')
    return F(x)


def dec(x):
    return D(x.numerator) / D(x.denominator)


def parse(raw):
    data = {k: {} for k in ('cases', 'cells', 'faces', 'levels', 'nodes', 'edges', 'parent', 'p', 'r', 'vectors', 'refresh')}
    shapes = {'MGCELL': ('cells', 4, 5), 'MGFACE': ('faces', 4, 5), 'MGLEVEL': ('levels', 3, 6),
              'MGNODE': ('nodes', 4, 8), 'MGEDGE': ('edges', 4, 8), 'MGPARENT': ('parent', 4, 5),
              'MGP': ('p', 5, 6), 'MGR': ('r', 5, 6)}
    for line in raw.decode('ascii').splitlines():
        row = line.split(); check(bool(row), 'Empty saved record'); kind = row[0]
        if kind == 'MGCASE':
            check(len(row) == 8, 'Case shape'); table, key, value = 'cases', int(row[1]), tuple(map(int, row[2:]))
        elif kind in shapes:
            table, split, length = shapes[kind]; check(len(row) == length, 'Saved row shape')
            # Cells have a boolean wall field before their coefficient.
            if kind == 'MGCELL': key, value = tuple(map(int, row[1:3])), (int(row[3]), number(row[4]))
            elif kind == 'MGLEVEL': key, value = tuple(map(int, row[1:3])), tuple(map(int, row[3:]))
            elif kind == 'MGNODE': key, value = tuple(map(int, row[1:4])), (*map(int, row[4:6]), *map(number, row[6:]))
            elif kind == 'MGEDGE': key, value = tuple(map(int, row[1:4])), (*map(int, row[4:7]), number(row[7]))
            elif kind == 'MGPARENT': key, value = tuple(map(int, row[1:4])), int(row[4])
            else: key, value = tuple(map(int, row[1:split])), number(row[split])
        elif kind == 'MGV':
            check(len(row) == 5, 'Vector shape'); table, key, value = 'vectors', (int(row[1]), row[2], int(row[3])), number(row[4])
        elif kind == 'MGREFRESH':
            check(len(row) == 3, 'Refresh shape'); table, key, value = 'refresh', int(row[1]), int(row[2])
        elif kind == 'PASS':
            check('pass' not in data, 'Duplicate completion'); data['pass'] = True; continue
        else: raise ValueError('Unknown record: ' + kind)
        check(key not in data[table], 'Duplicate saved record'); data[table][key] = value
    return data


def connected_components(n, edges):
    adjacent = [[] for _ in range(n)]
    for a, b, _, _ in edges: adjacent[a].append(b); adjacent[b].append(a)
    labels = [-1] * n
    for i in range(n):
        if labels[i] >= 0: continue
        labels[i] = i; queue = [i]
        for j in queue:
            for k in adjacent[j]:
                if labels[k] < 0: labels[k] = i; queue.append(k)
    return labels, adjacent


def multiply(h, edges, x):
    y = [h[i] * x[i] for i in range(len(h))]
    for a, b, _, g in edges:
        flux = g * (x[a] - x[b]); y[a] += flux; y[b] -= flux
    return y


def solve(h, edges, rhs):
    n = len(h); a = [[D(0) for _ in range(n)] for _ in range(n)]; b = list(rhs)
    for i in range(n): a[i][i] = h[i]
    for i, j, _, g in edges: a[i][i] += g; a[j][j] += g; a[i][j] -= g; a[j][i] -= g
    for j in range(n):
        check(a[j][j] > 0, 'Coarse exact operator lost positive pivot')
        for i in range(j + 1, n):
            factor = a[i][j] / a[j][j]
            for k in range(j + 1, n): a[i][k] -= factor * a[j][k]
            b[i] -= factor * b[j]
    x = [D(0)] * n
    for i in reversed(range(n)): x[i] = (b[i] - sum((a[i][j] * x[j] for j in range(i + 1, n)), D(0))) / a[i][i]
    return x


def vcycle(levels, index, rhs):
    h, edges, p, diagonal = levels[index]; x = [D(0)] * len(h)
    if index + 1 == len(levels):
        return solve(h, edges, rhs) if edges else [rhs[i] / h[i] for i in range(len(h))]
    def smooth(weights):
        nonlocal x
        for omega in weights:
            ax = multiply(h, edges, x)
            x = [x[i] + omega * (rhs[i] - ax[i]) / diagonal[i] for i in range(len(h))]
    smooth((D('.5'), D('1.5'))); ax = multiply(h, edges, x); coarse_rhs = [D(0)] * len(levels[index + 1][0])
    for i, row in enumerate(p):
        for j, weight in row: coarse_rhs[j] += weight * (rhs[i] - ax[i])
    correction = vcycle(levels, index + 1, coarse_rhs)
    for i, row in enumerate(p): x[i] += sum((weight * correction[j] for j, weight in row), D(0))
    smooth((D('1.5'), D('.5'))); return x


def review(data):
    check(set(data['cases']) == set(range(5)) and data.get('pass'), 'Incomplete manufactured cases')
    check(data['refresh'] == dict.fromkeys(range(5), 1), 'Missing coefficient replacement/rejection control')
    for table in ('cells', 'faces', 'levels', 'nodes', 'edges', 'parent', 'p', 'r', 'vectors'):
        check(all(key[0] in data['cases'] for key in data[table]), 'Unexpected case in ' + table)
    expected_levels = {(case, level) for case, metadata in data['cases'].items() for level in range(metadata[4])}
    check(set(data['levels']) == expected_levels, 'Unexpected/missing hierarchy level')
    for table in ('nodes', 'edges'):
        check(all(key[:2] in expected_levels for key in data[table]), 'Unexpected level in ' + table)
    for table in ('parent', 'p', 'r'):
        check(all(key[:2] in expected_levels and key[1] + 1 < data['cases'][key[0]][4] for key in data[table]), 'Unexpected transfer level')
    check(all(key[1] in (0, 1) for key in data['faces']), 'Unexpected face axis')
    check(all(key[1] in ('u', 'v', 'bu', 'bv', 'rhs', 'truth', 'solution') for key in data['vectors']), 'Unexpected vector')
    for case, (w, height, px, py, count, iterations) in data['cases'].items():
        check(w > 0 and height > 0 and px in (0, 1) and py in (0, 1) and 0 < iterations < 64, 'Invalid case metadata')
        n = w * height
        check({k[1] for k in data['cells'] if k[0] == case} == set(range(n)), 'Incomplete physical cells')
        fluid = [i for i in range(n) if data['cells'][case, i][0] == 0]
        local = {i: j for j, i in enumerate(fluid)}
        for i in range(n):
            wall, h = data['cells'][case, i]; check(wall in (0, 1) and (h == 0 if wall else h > 0), 'Invalid wall/compliance')
        physical_edges = []
        for axis in (0, 1):
            fw, fh = (w + (not px), height) if axis == 0 else (w, height + (not py))
            check({k[2] for k in data['faces'] if k[:2] == (case, axis)} == set(range(fw * fh)), 'Incomplete physical faces')
            for f in range(fw * fh):
                x, y = f % fw, f // fw; a = b = None; g = data['faces'][case, axis, f]
                if axis == 0:
                    if x or px: a = (x + w - 1) % w + y * w
                    if x < w: b = x + y * w
                else:
                    if y or py: a = x + ((y + height - 1) % height) * w
                    if y < height: b = x + y * w
                if a not in local or b not in local: check(g == 0, 'Blocked face has conductance')
                else: check(g > 0, 'Open face lost conductance'); physical_edges.append((local[a], local[b], axis, g))
        oracle = []; previous = None
        for l in range(count):
            stride, size, edge_count = data['levels'][case, l]; check(stride == 2**l, 'Wrong geometric stride')
            check({k[2] for k in data['nodes'] if k[:2] == (case, l)} == set(range(size)), 'Incomplete level nodes')
            check({k[2] for k in data['edges'] if k[:2] == (case, l)} == set(range(edge_count)), 'Incomplete level edges')
            nodes = [data['nodes'][case, l, i] for i in range(size)]; edges = [data['edges'][case, l, e] for e in range(edge_count)]
            coefficients = [row[2] for row in nodes]; diagonal = list(coefficients)
            for a, b, axis, g in edges:
                check(0 <= a < size and 0 <= b < size and a != b and axis in (0, 1) and g > 0, 'Invalid level edge')
                diagonal[a] += g; diagonal[b] += g
            for i, row in enumerate(nodes): check(coefficients[i] > 0 and abs(row[3] - diagonal[i]) <= 32 * EPS * diagonal[i], 'Wrong positive operator diagonal')
            if l == 0:
                check([(row[0], row[1]) for row in nodes] == [(i % w, i // w) for i in fluid], 'Fine physical-cell mapping differs')
                check(coefficients == [data['cells'][case, i][1] for i in fluid] and edges == physical_edges, 'Fine operator changed a physical face or mass term')
            else:
                old_nodes, old_edges, parents = previous
                sums = [F(0)] * size; groups = [[] for _ in range(size)]
                for i, p in enumerate(parents):
                    check(0 <= p < size and nodes[p][:2] == (old_nodes[i][0] // 2, old_nodes[i][1] // 2), 'Coarse aggregate crossed a geometric bin')
                    sums[p] += old_nodes[i][2]; groups[p].append(i)
                labels, adjacent = connected_components(len(old_nodes), old_edges)
                for p, group in enumerate(groups):
                    check(group and abs(coefficients[p] - sums[p]) <= 32 * EPS * sums[p], 'Coarse compliance sum differs')
                    reached = {group[0]}; queue = [group[0]]
                    for i in queue:
                        for j in adjacent[i]:
                            if parents[j] == p and j not in reached: reached.add(j); queue.append(j)
                    check(reached == set(group), 'Aggregate merged disconnected pores')
                expected = {}
                old_stride = stride // 2
                for a, b, axis, g in old_edges:
                    pa, pb = parents[a], parents[b]
                    if nodes[pa][:2] == nodes[pb][:2]: check(pa == pb, 'Connected cells in one bin were split')
                    if pa == pb: continue
                    extent = w if axis == 0 else height
                    width = lambda row, step: min(step, extent - row[axis] * step)
                    scale = F(width(old_nodes[a], old_stride) + width(old_nodes[b], old_stride), width(nodes[pa], stride) + width(nodes[pb], stride))
                    key = (min(pa, pb), max(pa, pb), axis); expected[key] = expected.get(key, F(0)) + g * scale
                check({edge[:3] for edge in edges} == set(expected), 'Coarse graph added/lost a connection')
                for a, b, axis, g in edges: check(abs(g - expected[a, b, axis]) <= 32 * EPS * expected[a, b, axis], 'Coarse geometric conductance differs')
            p = [[] for _ in range(size)]
            if l + 1 < count:
                check({k[2] for k in data['parent'] if k[:2] == (case, l)} == set(range(size)), 'Incomplete parent mapping')
                parents = [data['parent'][case, l, i] for i in range(size)]
                labels, _ = connected_components(size, edges)
                coarse_labels = {}
                for i, parent in enumerate(parents):
                    check(parent not in coarse_labels or coarse_labels[parent] == labels[i], 'Parent spans disconnected components'); coarse_labels[parent] = labels[i]
                for key, weight in data['p'].items():
                    if key[:2] != (case, l): continue
                    _, _, i, j = key; check(0 <= i < size and j in coarse_labels and weight > 0, 'Invalid prolongation')
                    check(labels[i] == coarse_labels[j], 'Interpolation coupled disconnected components')
                    check(data['r'].get((case, l, j, i)) == weight, 'Restriction is not the actual transpose')
                    p[i].append((j, dec(weight)))
                check({(k[2], k[3]) for k in data['r'] if k[:2] == (case, l)} == {(k[3], k[2]) for k in data['p'] if k[:2] == (case, l)}, 'Extra restriction terms')
                for row in p: check(row and abs(sum((weight for _, weight in row), D(0)) - 1) <= dec(8 * EPS), 'Interpolation does not preserve constants')
                previous = nodes, edges, parents
            else: check(size <= 24 or not edges, 'Missing bounded exact coarse solve')
            oracle.append(([dec(v) for v in coefficients], [(a, b, axis, dec(g)) for a, b, axis, g in edges], p, [dec(v) for v in diagonal]))
        vectors = {}
        for name in ('u', 'v', 'bu', 'bv', 'rhs', 'truth', 'solution'):
            check({k[2] for k in data['vectors'] if k[:2] == (case, name)} == set(range(n)), 'Incomplete saved vector')
            values = [data['vectors'][case, name, i] for i in range(n)]
            check(all(values[i] == 0 for i in range(n) if i not in local), 'Vector has a wall unknown')
            vectors[name] = [values[i] for i in fluid]
        for name, result in (('u', 'bu'), ('v', 'bv')):
            exact = vcycle(oracle, 0, list(map(dec, vectors[name]))); scale = max(map(abs, exact))
            check(scale > 0 and max(abs(exact[i] - dec(vectors[result][i])) for i in range(len(fluid))) <= D('1e-10') * scale, 'Saved V-cycle differs from independent 100-digit operator')
            check(sum((a * b for a, b in zip(vectors[name], vectors[result])), F(0)) > 0, 'Preconditioner lost positivity')
        symmetric = [a * b - c * d for a, b, c, d in zip(vectors['u'], vectors['bv'], vectors['bu'], vectors['v'])]
        scale = sum((abs(a * b) + abs(c * d) for a, b, c, d in zip(vectors['u'], vectors['bv'], vectors['bu'], vectors['v'])), F(0))
        check(abs(sum(symmetric, F(0))) <= 4096 * EPS * scale, 'Preconditioner is not numerically symmetric')
        applied = multiply([data['cells'][case, i][1] for i in fluid], physical_edges, vectors['solution'])
        residual = max(abs(a - b) for a, b in zip(applied, vectors['rhs']))
        check(residual <= F(2, 10**11) * max(map(abs, vectors['rhs'])), 'Manufactured solve failed its true residual')
        check(max(abs(a - b) for a, b in zip(vectors['solution'], vectors['truth'])) <= F(1, 10**9), 'Manufactured solution is wrong')


def main():
    raw = Path(sys.argv[1]).read_bytes(); check(len(raw) < 2 * 1024 * 1024, 'Saved pipe cap')
    with localcontext() as context:
        context.prec = 100; data = parse(raw); review(data); saved = CHECKS
        for kind in ('transfer', 'coarse', 'face', 'cycle', 'solution', 'extra_case', 'extra_level', 'extra_vector'):
            bad = copy.deepcopy(data)
            if kind == 'transfer': key = next(iter(bad['p'])); bad['p'][key] *= 2
            elif kind == 'coarse':
                key = next(k for k in bad['nodes'] if k[1] == 1); row = list(bad['nodes'][key]); row[2] *= 2; bad['nodes'][key] = tuple(row)
            elif kind == 'face': key = next(iter(bad['edges'])); row = list(bad['edges'][key]); row[3] *= 2; bad['edges'][key] = tuple(row)
            elif kind == 'extra_case': bad['cells'][99, 0] = (0, F(1))
            elif kind == 'extra_level': bad['parent'][0, 99, 0] = 0
            elif kind == 'extra_vector': bad['vectors'][0, 'unread', 0] = F(1)
            else: bad['vectors'][0, 'bu' if kind == 'cycle' else 'solution', 0] += 1
            rejected = False
            try: review(bad)
            except ValueError: rejected = True
            check(rejected, 'Corrupted multigrid evidence admitted: ' + kind)
    print(json.dumps(dict(Passed=True, SavedChecks=saved, CorruptionsRejected=8, NativeProcessLaunched=False,
        SavedSha256=hashlib.sha256(raw).hexdigest(), Scope='Geometric pressure preconditioner; physical timestep and colony gates are separate')))


if __name__ == '__main__':
    main()
