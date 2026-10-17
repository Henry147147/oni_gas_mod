"""Review saved swept-parcel data with exact rational first moments; launches nothing."""
from fractions import Fraction as F
import copy
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


def number(token):
    value = float(token)
    check(math.isfinite(value), 'Nonfinite saved operand')
    return F(value)


def close(actual, expected, operands=None, eps=256):
    values = [actual, expected] if operands is None else operands
    check(abs(actual - expected) <= eps * EPS * sum(map(abs, values)), 'Saved operand comparison')


def unique(table, key, value):
    check(key not in table, 'Repeated saved identity')
    table[key] = value


def parse(raw):
    check(len(raw) <= 4 * 1024 * 1024, 'Swept raw-data cap')
    cases = {i: dict(cells={}, sweeps={}, parcels={}, affine={}) for i in range(12)}
    checkpoints = {}
    passes = []
    for line in raw.decode('ascii').splitlines():
        row = line.split()
        check(bool(row), 'Empty swept record')
        kind = row[0]
        if kind == 'PASS':
            passes.append(line)
            continue
        if kind == 'SCHECKPOINT':
            check(len(row) == 8, 'Checkpoint record shape')
            unique(checkpoints, tuple(map(int, row[1:4])), tuple(map(number, row[4:])))
            continue
        check(kind in ('SGRID', 'SCELL', 'SSWEEP', 'SPARC', 'SAFFINE'), 'Unknown swept record')
        control = int(row[1])
        check(control in cases, 'Unknown swept control')
        case = cases[control]
        if kind == 'SGRID':
            check(len(row) == 8 and 'grid' not in case, 'Grid record shape or repeated grid')
            case['grid'] = (*map(int, row[2:4]), *map(number, row[4:6]), *map(int, row[6:]))
        elif kind == 'SCELL':
            check(len(row) == 18, 'Cell record shape')
            unique(case['cells'], int(row[2]), tuple(map(number, row[3:])))
        elif kind == 'SSWEEP':
            check(len(row) == 5, 'Sweep record shape')
            unique(case['sweeps'], tuple(map(int, row[2:4])), number(row[4]))
        elif kind == 'SPARC':
            check(len(row) == 13, 'Parcel record shape')
            unique(case['parcels'], tuple(map(int, row[2:4])), (*map(int, row[4:6]), *map(number, row[6:])))
        elif kind == 'SAFFINE':
            check(len(row) == 7, 'Affine record shape')
            unique(case['affine'], tuple(map(int, row[2:5])), tuple(map(number, row[5:])))
    check(len(passes) == len(set(passes)) == 4, 'Missing or repeated swept control summaries')
    return cases, checkpoints


def review(data):
    cases, checkpoints = data
    check(set(checkpoints) == {(wet, heat, compression) for wet in (0, 1) for heat in range(4) for compression in range(3)}, 'Missing checkpoint setting combination')
    for source_time, resumed_time, steps, rejected in checkpoints.values():
        close(source_time, F(.002)); close(resumed_time, F(.004))
        check(steps >= 1 and steps.denominator == 1 and rejected >= 0 and rejected.denominator == 1, 'Malformed checkpoint clock/attempt counts')
    for control, case in cases.items():
        w, h, spacing, depth, px, py = case['grid']
        check((w, h) == ((6, 2) if control < 4 else (2, 6) if control < 8 else (8, 8)), 'Changed affine grid')
        check((px, py) == ((0, 1) if control < 4 else (1, 0) if control < 8 else (0, 0)), 'Changed affine boundary')
        check((spacing, depth) == ((F(1, 4), F(2)) if control < 8 and control % 2 else (F(1), F(1))), 'Changed affine length scale')
        base = spacing * spacing * depth
        xw, yh = w + (not px), h + (not py)
        cells, sweeps, parcels = case['cells'], case['sweeps'], case['parcels']
        check(set(cells) == set(range(w*h)), 'Missing affine cell')
        check(set(sweeps) == {(0, f) for f in range(xw*h)} | {(1, f) for f in range(w*yh)}, 'Missing affine sweep')

        def xf(x, y):
            return x % xw + y * xw

        def yf(x, y):
            return x + y % yh * w

        def neighbours(axis, face):
            x, y = (face % xw, face // xw) if axis == 0 else (face % w, face // w)
            if (axis == 0 and not px and x in (0, w)) or (axis == 1 and not py and y in (0, h)):
                return None
            return (((x-1) % w + y*w, x+y*w) if axis == 0 else (x+(y-1) % h*w, x+y*w))

        def mean(cell, axis):
            x, y = cell % w, cell // w
            return ((sweeps[0, xf(x, y)] + sweeps[0, xf(x+1, y)]) if axis == 0 else
                    (sweeps[1, yf(x, y)] + sweeps[1, yf(x, y+1)])) / (2 * base)

        half = {}
        for (axis, face), q in sweeps.items():
            pair = neighbours(axis, face)
            if pair is None:
                check(q == 0, 'Exterior sweep')
                half[axis, face] = (F(0), F(0))
            else:
                normal = q / (2 * base)
                tangent = sum(mean(cell, 1-axis) for cell in pair) / 4
                half[axis, face] = (normal, tangent) if axis == 0 else (tangent, normal)

        centres = {}
        for i, row in cells.items():
            x, y = i % w, i // w
            volume, retained, theta, *amounts = row
            faces = [(0, xf(x, y), -1, (-F(1, 2), F(0))), (0, xf(x+1, y), 1, (F(1, 2), F(0))),
                     (1, yf(x, y), -1, (F(0), -F(1, 2))), (1, yf(x, y+1), 1, (F(0), F(1, 2)))]
            check(volume == base + sum(sign * sweeps[axis, face] for axis, face, sign, _ in faces), 'Exact swept volume closure')
            offset = tuple(sum(sign * sweeps[axis, face] * (position[k] + half[axis, face][k]) for axis, face, sign, position in faces) / volume for k in (0, 1))
            centres[i] = (x+F(1, 2)+offset[0], y+F(1, 2)+offset[1])
            check(0 <= theta <= 1 and retained > 0 and all(v > 0 for v in amounts), 'Nonpositive saved inventory/invalid limiter')
            for n, expected in ((0, volume), (1, volume/F(2**100)), (2, volume*2**20), (5, volume/F(2**200))):
                close(amounts[n], expected)
            cx, cy = centres[i]
            if control < 4:
                cy = F(0)
            elif control < 8:
                cx = F(0)
            elif x in (0, w-1) or y in (0, h-1):
                cx, cy = x+F(1, 2), y+F(1, 2)
            close(amounts[3], volume*(2+cx/8+cy/16))
            close(amounts[4], volume*(3-cx/32+cy/8))
        expected_parcels = {key for key, q in sweeps.items() if q}
        check(set(parcels) == expected_parcels, 'Missing/spurious swept parcel')
        for key, parcel in parcels.items():
            donor, target, volume, *amounts = parcel
            pair = neighbours(*key)
            check(pair is not None and (donor, target) == (pair if sweeps[key] > 0 else pair[::-1]), 'Incorrect parcel ownership')
            check(volume == abs(sweeps[key]) and all(v > 0 for v in amounts), 'Swept parcel volume or positive trace lost')
            for n, expected in ((0, volume), (1, volume/F(2**100)), (2, volume*2**20), (5, volume/F(2**200))):
                close(amounts[n], expected)
        for i, row in cells.items():
            outgoing = [parcel for parcel in parcels.values() if parcel[0] == i]
            volume, retained, _, *amounts = row
            close(retained + sum(p[2] for p in outgoing), volume, [volume, retained, *[p[2] for p in outgoing]], eps=128)
            for n in range(6):
                operands = [amounts[n], -amounts[n+6], *[-p[3+n] for p in outgoing]]
                close(sum(operands), F(0), operands, eps=128)
        check(len(case['affine']) == (1 if control < 8 else 2), 'Missing selected affine parcel')
        for (donor, axis, face), position in case['affine'].items():
            check(cells[donor][2] == 1 and (axis, face) in parcels and parcels[axis, face][0] == donor, 'Selected affine parcel limited or missing')
            x, y = (face % xw, face // xw) if axis == 0 else (face % w, face // w)
            exact = (x + (0 if axis == 0 else F(1, 2)) + half[axis, face][0],
                     y + (F(1, 2) if axis == 0 else 0) + half[axis, face][1])
            if control < 4:
                exact = exact[0], F(0)
            elif control < 8:
                exact = F(0), exact[1]
            check(position == exact, 'Affine face location disagrees with exact first moment')
            volume = parcels[axis, face][2]
            close(parcels[axis, face][6], volume*(2+exact[0]/8+exact[1]/16))
            close(parcels[axis, face][7], volume*(3-exact[0]/32+exact[1]/8))


def main():
    raw = Path(sys.argv[1]).read_bytes()
    data = parse(raw)
    review(data)
    checks = CHECKS
    for mutation in ('trace', 'affine', 'limiter', 'volume'):
        bad = copy.deepcopy(data)
        case = bad[0][2]
        donor, axis, face = next(iter(case['affine']))
        if mutation in ('trace', 'affine'):
            row = list(case['parcels'][axis, face]); row[4 if mutation == 'trace' else 6] /= 10; case['parcels'][axis, face] = tuple(row)
        elif mutation == 'limiter':
            row = list(case['cells'][donor]); row[2] /= 2; case['cells'][donor] = tuple(row)
        else:
            case['sweeps'][axis, face] += F(1, 1024)
        caught = False
        try:
            review(bad)
        except ValueError:
            caught = True
        check(caught, 'Altered swept evidence accepted')
    print(json.dumps(dict(Passed=True, SavedSha256=hashlib.sha256(raw).hexdigest(), SavedChecks=checks,
        CorruptionsRejected=4, NativeProcessLaunched=False, IndependentAgentReview=False,
        Scope='Twelve affine controls, exact saved balances and checkpoint record coverage; no shock, vacuum, game or performance acceptance')))


if __name__ == '__main__':
    main()
