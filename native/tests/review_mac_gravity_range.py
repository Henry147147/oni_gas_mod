"""Review saved full-range gravity operands with exact fractions; launches no solver."""
import copy
from decimal import localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location('gravity_saved', Path(__file__).with_name('review_mac_gravity.py'))
gravity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gravity)
closed, base = gravity.closed, gravity.base
check, close, number, unique = base.check, base.close, base.number, base.unique


def scaled(row, start, end):
    return tuple(base.scaled_energy(row[i], row[i + 1]) for i in range(start, end, 2))


def parse(raw):
    common, cases, faces, cells, owners, metadata, long_run = [], {}, {}, {}, {}, {}, None
    for line in raw.decode('ascii').splitlines():
        row = line.split()
        check(bool(row), 'Empty gravity-range record')
        kind = row[0]
        if kind == 'GRAVITYRANGE':
            check(len(row) == 24, 'Gravity-range case shape')
            identifier = int(row[1])
            g = number(row[2])
            unique(metadata, identifier, tuple(map(int, row[3:6])))
            residual, bound, pe_residual, pe_bound, body, change, wx, wy = scaled(row, 6, 22)
            unique(cases, identifier, (g, residual, bound, pe_residual, pe_bound, body, change,
                                     number(row[22]), number(row[23])))
            metadata[identifier] += (wx, wy)
        elif kind == 'GRAVITYFACE':
            check(len(row) == 9, 'Gravity-range face shape')
            unique(faces, tuple(map(int, row[1:3])), scaled(row, 3, 9))
        elif kind == 'GRAVITYCELL':
            check(len(row) == 7, 'Gravity-range cell shape')
            unique(cells, tuple(map(int, row[1:3])), (number(row[3]), number(row[4]), *scaled(row, 5, 7)))
        elif kind == 'GRAVITYOWNER':
            check(len(row) == 12, 'Gravity-range owner shape')
            unique(owners, int(row[1]), (int(row[2]), int(row[3]), number(row[4]), int(row[5]),
                                        *scaled(row, 6, 10), int(row[10]), int(row[11])))
        elif kind == 'GRAVITYLONG':
            check(len(row) == 12 and long_run is None, 'Long gravity-range shape/duplicate')
            long_run = (int(row[1]), number(row[2]), int(row[3]), *scaled(row, 4, 12))
        else:
            common.append(line)
    ordinary = closed.parse(('\n'.join(common) + '\n').encode('ascii'), required_passes=5,
                            require_acoustic=False, required_cases=(0, 1, 2), required_pressure_cases=(0, 1, 2))
    check(set(cases) == set(owners) == set(metadata) == {0, 1, 2}, 'Gravity-range case/owner coverage')
    for identifier in cases:
        row = ordinary[2][identifier]
        wx, wy = metadata[identifier][-2:]
        check(row[-2:] == (F(float(wx)), F(float(wy))), 'Full pressure-wall projection differs')
        ordinary[2][identifier] = row[:-2] + (wx, wy)
    check(long_run is not None, 'Missing long gravity-range continuation')
    return (ordinary, cases, faces, cells, {}, {}), metadata, owners, long_run


def review(parsed):
    data, metadata, owners, long_run = parsed
    ordinary, cases, faces, cells, _, _ = data
    for identifier in (0, 1, 2):
        gravity.review_case(identifier, data)
        layered, unresolved, projections, _, _ = metadata[identifier]
        check(layered == (identifier == 1), 'Gravity-range layered control differs')
        case = ordinary[0][identifier]
        dt = ordinary[2][identifier][0]
        steps, retries, time, revision, body, change, owner_unresolved, owner_projections = owners[identifier]
        check((steps, retries, time, revision) == (1, 0, dt, 2), 'Gravity-range nominal owner path')
        close(body, cases[identifier][5], abs(body), 'Owner narrowed body impulse')
        # Incoming compression/heat stages leave these uniform-pressure
        # fixtures' primary U and effectively stationary J unchanged.
        close(change, cases[identifier][6], abs(change) + abs(cases[identifier][6]), 'Owner narrowed signed gravity work')
        check(owner_unresolved == unresolved and
              owner_projections == projections + (F(float(body)) != body) + (F(float(change)) != change),
              'Owner gravity rounding diagnostics differ')
        if identifier != 0:
            check(0 > body > -F(1, 2**1075) and projections > 0 and owner_projections > 0,
                  'Below-range body impulse was lost or fixture did not reach it')
            moving = [(key, value) for key, value in faces.items() if key[0] == identifier and value[0] != 0]
            check(moving and all(0 < value[0] < F(1, 2**1075) and
                                -F(1, 2**1075) < value[1] < 0 for _, value in moving),
                  'Below-range body work or potential flux was lost')
            check(all(case['faces'][1, key[1]][0] < 0 for key, _ in moving), 'Actual full mass flux disappeared')
        else:
            check(any(0 < abs(v[0]) < F(1, 2**1075) for key, v in ordinary[4].items()
                      if key[0] == identifier and key[1] == 1), 'Original full vertical momentum missing')
        actual_unresolved = 0
        projected_values = list(cases[identifier][1:7])
        for (case_id, _), values in faces.items():
            if case_id == identifier:
                projected_values.extend(values)
        for (case_id, _), (before, after, requested) in cells.items():
            if case_id != identifier:
                continue
            check(abs(before) >= F(1, 2**1022) and abs(after) >= F(1, 2**1022),
                  'Gravity fixture primary-U projection is not normal')
            projected_values.append(requested)
            if requested == 0:
                continue
            if abs(after - before - requested) > 32 * base.EPS * abs(requested):
                actual_unresolved += 1
        check(unresolved == actual_unresolved, 'Unresolved primary-U changes were hidden')
        energy_writes=sum(after!=F(float(after)) for (case_id,_),(_,after,_) in cells.items() if case_id==identifier) if case.get('carried_u',False) else 0
        check(projections == energy_writes + sum(F(float(value)) != value for value in projected_values),
              'Gravity projection count differs from its full saved operands')
        if identifier == 1:
            check(unresolved > 0 and change != 0, 'Layered work fixture did not retain unresolved signed work')
        if identifier == 2:
            check(any(0 < abs(v[1]) < F(1, 2**1075) for key, v in ordinary[4].items()
                      if key[0] == identifier and key[1] == 1), 'Below-range vertical sweep missing')
    steps, time, revision, actual, expected, residual, bound = long_run
    check(steps == 10000 and time == steps * ordinary[2][1][0] and revision == steps + 1,
          'Long gravity-range clock/revision differs')
    inertia = ordinary[0][1]['faces'][1, 2][3]
    close(expected, -inertia * cases[1][0] * time, abs(expected), 'Long gravity-range reference impulse')
    close(actual, expected, abs(actual) + abs(expected), 'Long gravity-range accumulated impulse')
    check(0 < abs(actual) < F(1, 2**1075) and abs(residual) <= bound, 'Long gravity-range state or energy')
    # Two equal columns have one moving vertical face each. Primary U/masses
    # remain unchanged; their resolved kinetic energy is the full residual.
    close(residual, actual * actual / inertia, abs(residual), 'Long gravity-range kinetic residual')


def main():
    raw = Path(sys.argv[1]).read_bytes()
    check(len(raw) <= base.saved_pipe_cap(raw, 524288), 'Gravity-range saved pipe cap')
    with localcontext() as context:
        context.prec = 100
        parsed = parse(raw)
        review(parsed)
        checks = base.CHECKS
        for corruption in ('body-work', 'potential-flux', 'cell-work', 'body-impulse', 'hidden-rounding', 'long-energy'):
            bad = copy.deepcopy(parsed)
            data, metadata, owners, _ = bad
            if corruption in ('body-work', 'potential-flux'):
                key = next(key for key, value in data[2].items() if key[0] == 1 and value[0] != 0)
                row = list(data[2][key]); row[0 if corruption == 'body-work' else 1] = F(0)
                data[2][key] = tuple(row)
            elif corruption == 'cell-work':
                key = next(key for key, value in data[3].items() if key[0] == 1 and value[2] != 0)
                row = list(data[3][key]); row[2] = F(0); data[3][key] = tuple(row)
            elif corruption == 'body-impulse':
                row = list(owners[2]); row[4] = F(0); owners[2] = tuple(row)
            elif corruption == 'hidden-rounding':
                row = list(metadata[1]); row[1] = 0; metadata[1] = tuple(row)
            else:
                row = list(bad[3]); row[5] = F(0); bad = (*bad[:3], tuple(row))
            rejected = False
            try:
                review(bad)
            except ValueError:
                rejected = True
            check(rejected, 'Altered gravity-range evidence accepted: ' + corruption)
    print(json.dumps(dict(Passed=True, SavedSha256=hashlib.sha256(raw).hexdigest(), SavedChecks=checks,
                          CorruptionsRejected=6, NativeProcessLaunched=False, IndependentAgentReview=False,
                          Scope='Full-range vertical gravity impulse, body work, actual-mass potential flux and signed U work; '
                                'primary-U rounding is reported, not persisted; no strong-flow, vacuum or game acceptance')))


if __name__ == '__main__':
    main()
