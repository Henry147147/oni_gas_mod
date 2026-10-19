"""Exact saved-state review of momentum contribution range. No native launch."""
import copy
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location('momentum_mac_saved', Path(__file__).with_name('review_mac_transport.py'))
base = importlib.util.module_from_spec(spec); spec.loader.exec_module(base)
check = base.check


def review(cases):
    for case in cases.values(): base.review_case(case)
    case = cases[0]; full_shares = []
    for (axis, left, right), flux in case['duals'].items():
        if axis == 0 and flux:
            donor = left if flux > 0 else right; row = case['faces'][axis, donor]
            full_shares.append(row[1] * abs(flux) / row[3])
    check(full_shares and all(0 < value < F(1, 2**1075) for value in full_shares), 'Original subrange momentum scale not exercised')
    for case in (cases[0], cases[1]):
        check(all(row[1] == row[2] for row in case['faces'].values()), 'Uniform stored momentum changed')
    unit = F(1, 2**1074)
    for (axis, i), row in cases[1]['faces'].items():
        if axis == 0: check(row[1] == row[2] == unit, 'Uniform minimum J was not retained')
    for (axis, i), row in cases[2]['faces'].items():
        if axis == 0:
            check(row[1] == (1 if i % 2 == 0 else -3) * unit, 'Signed source J differs')
            check(row[2] == (0 if i % 2 == 0 else -2) * unit, 'Signed subrange cancellation differs')
    report = cases[2]['wide_report']
    carried=any(u!=F(float(u)) for (e,_),u in cases[2]['cells'].items() if e==1)
    storage=(report[1]==0 and carried) if cases[2].get('carried_u',False) else report[1]>0
    check(report is not None and storage and report[4][2] > 0 and report[2] == 0, 'Subrange mixing/U rounding was hidden')
    case=cases[3];row=case['faces'][0,1]
    check(row[1]==0 and 0<row[2]<F(1,2**1075) and float(row[2])==0, 'New canonical J did not persist below binary64 range')


def main():
    raw = Path(sys.argv[1]).read_bytes(); check(len(raw) <= 524288, 'Momentum capture cap')
    cases, waves, samples, timestep = base.parse(raw, required_passes=4, required_cases=(0, 1, 2, 3))
    check(not waves and not samples and timestep is None, 'Unexpected momentum fixture records')
    review(cases); checks = base.CHECKS
    for corruption in ('dual-flux', 'stored-J', 'trace'):
        bad = copy.deepcopy(cases)
        if corruption == 'dual-flux':
            case = bad[0]; key = next(k for k, v in case['duals'].items() if v); case['duals'][key] *= F(101, 100)
        elif corruption == 'stored-J':
            case = bad[1]; key = (0, 0); row = list(case['faces'][key]); row[2] = 0; case['faces'][key] = tuple(row)
        else:
            case = bad[2]; key = next(k for k, v in case['masses'].items() if k[0] == 1 and v < F(1, 10**20)); case['masses'][key] /= 10
        rejected = False
        try: review(bad)
        except ValueError: rejected = True
        check(rejected, 'Altered momentum record accepted: ' + corruption)
    print(json.dumps(dict(Passed=True, SavedSha256=hashlib.sha256(raw).hexdigest(), SavedChecks=checks,
        CorruptionsRejected=3, NativeProcessLaunched=False, IndependentAgentReview=False,
        Scope='Scaled momentum contributions with unchanged binary64 canonical/wall storage; no strong-flow, game or performance acceptance')))


if __name__ == '__main__': main()
