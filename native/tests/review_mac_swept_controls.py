"""Saved-data review of the original five controls with the swept selector.

This entry point requires a uniformly translated periodic mesh. The general
moved-volume affine controls have their own exact first-moment reviewer.
The existing transport/EOS/wave review remains in use; only the expected
material-gradient law is replaced explicitly below. It launches no native code.
"""
from fractions import Fraction as F
import review_mac_reconstruction as reconstruction
base = reconstruction.base
check, close = base.check, base.close


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
    check(case['px'] and case['py'] and not any(case['walls'].values()), 'Uniform swept reviewer requires a periodic fluid rectangle')
    check(all(row[0] == nominal for row in case['rcells'].values()), 'Uniform swept reviewer requires fixed material volumes')
    check(all(len({q.get((axis, f), F(0)) for f in range(len(faces[axis]))}) == 1 for axis in (0, 1)), 'Uniform swept reviewer requires constant face sweeps')
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
                # On this regular translated mesh, inverse-distance least
                # squares has diagonal normal matrix diag(4,4). Each diagonal
                # neighbour has weight 1/2; these are exact rational slopes.
                diagonals = [neighbor(neighbor(i, 0, dx), 1, dy) for dy in (-1, 1) for dx in (-1, 1)]
                dl, dr, ul, ur = [old(n, identity) for n in diagonals]
                sx = (values[1] - values[0]) / 4 + (dr + ur - dl - ul) / 8
                sy = (values[3] - values[2]) / 4 + (ul + ur - dl - dr) / 8
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


if __name__ == '__main__':
    base.review_reconstructed_primal = review_reconstructed_primal
    reconstruction.main()
