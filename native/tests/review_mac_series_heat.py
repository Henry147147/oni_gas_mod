"""Saved-only half-cell resistance review; no native process is launched."""
import copy
from decimal import localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location('series_matched_reference', Path(__file__).with_name('review_mac_matched_heat.py'))
matched = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matched)
base, heat, acoustic = matched.base, matched.heat, matched.acoustic
check, close, number = base.check, base.close, base.number


def review(data, extra):
    matched.review_component(data, extra, expected_law=2)
    for gid in (*range(3, 8), 13):
        events, total = data['grids'][gid][7], data['grids'][gid][9]
        check(total == 0 and (events > 0) == (gid == 6), 'Inactive series half-cell/control changed')
    for gid in (9, 14):
        check(data['grids'][gid][7] == 2, 'Series two-body axes must retain one symmetric face')
    subrange = data['grids'][8]
    check(subrange[9] > 0 and float(subrange[9]) == 0 and subrange[8] > 0, 'Missing series subrange heat/unresolved storage')
    check(set(extra['weak']) == {10, 11, 12}, 'Missing series weak-wave controls')
    previous = None
    for gid, amplitude in ((10, number('1e-3')), (11, number('1e-5')), (12, number('1e-7'))):
        a, value = extra['weak'][gid]
        check(a == amplitude, 'Series weak-wave input changed')
        expected = data['grids'][gid][9] / (a * a)
        close(value, expected, expected, 'Series amplitude-normalized heat', 8)
        check(value > 0 and (previous is None or abs(value - previous) < number('1e-5') * previous), 'Series weak heat has a first-order amplitude term')
        previous = value


def main():
    raw = Path(sys.argv[1]).read_bytes()
    check(len(raw) <= 2097152, 'Series saved pipe cap')
    with localcontext() as context:
        context.prec = 80
        data, extra = matched.parse(raw, required_grids=range(15))
        review(data, extra)
        checks = base.CHECKS + acoustic.base.CHECKS
        corruptions = ('heat', 'trace', 'capacity', 'tangent', 'sensor', 'geometry', 'diffusion', 'activation', 'law')
        for corruption in corruptions:
            bad, other = copy.deepcopy(data), copy.deepcopy(extra)
            if corruption == 'heat':
                key = min(k for k, row in bad['pairs'].items() if row[18])
                row = list(bad['pairs'][key]); row[18] *= F(17, 16); bad['pairs'][key] = tuple(row)
            elif corruption == 'trace':
                key = min((k for k in bad['masses'] if k[1] == 1 and bad['materials'][k[3]][0] == 0), key=lambda k: bad['masses'][k])
                bad['masses'][key] *= 2
            elif corruption in ('capacity', 'tangent'):
                other['cp' if corruption == 'capacity' else 'speeds'][0, 0] *= F(17, 16)
            elif corruption == 'sensor':
                other['sensors'][0, 0, 0] += F(1, 8)
            elif corruption == 'geometry':
                key = min(bad['events']); row = list(bad['events'][key]); row[2] = row[3]; bad['events'][key] = tuple(row)
            elif corruption == 'diffusion':
                other['diffusion'][0, 0] *= F(17, 16)
            elif corruption == 'activation':
                # Make two adjacent cells compress, consistently in both
                # endpoints, while leaving the reported empty event set.
                for end in (0, 1):
                    row = list(bad['faces'][13, 0, end, 2]); row[2] = row[0] / 2
                    bad['faces'][13, 0, end, 2] = tuple(row)
            else:
                other['laws'][0] = 1
            caught = False
            try:
                review(bad, other)
            except (ValueError, KeyError):
                caught = True
            check(caught, 'Altered series heat record accepted')
    result = dict(Passed=True, Groups=8, SavedChecks=checks, CorruptionsRejected=list(corruptions),
                  SavedSha256=hashlib.sha256(raw).hexdigest(), NativeProcessLaunched=False, IndependentAgentReview=False,
                  Scope='Separate occupied half-cell series heat component; no owner, strong-flow, production or game acceptance')
    if len(sys.argv) > 2:
        with Path(sys.argv[2]).open('x') as stream:
            json.dump(result, stream, indent=2)
    print(json.dumps(result))


if __name__ == '__main__':
    main()
