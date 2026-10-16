"""Exact saved-data review of scaled primary parcels. Never starts native code."""
import copy
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
spec = importlib.util.spec_from_file_location('primary_mac_saved', Path(__file__).with_name('review_mac_transport.py'))
base = importlib.util.module_from_spec(spec); spec.loader.exec_module(base)

check, close, number, unique = base.check, base.close, base.number, base.unique


def parse(raw):
    common, materials, reports, eos = [], {}, {}, {}
    for line in raw.decode('ascii').splitlines():
        row = line.split(); check(bool(row), 'Empty primary parcel record')
        if row[0] == 'MATERIAL':
            check(len(row) == 5, 'Primary material shape')
            unique(materials, int(row[1]), tuple(map(number, row[2:])))
        elif row[0] == 'PRIMARY':
            check(len(row) == 3, 'Primary report shape'); unique(reports, int(row[1]), int(row[2]))
        elif row[0] == 'PEOS':
            check(len(row) == 6, 'Primary EOS shape'); unique(eos, tuple(map(int, row[1:4])), tuple(map(number, row[4:])))
        else:
            common.append(line)
    cases, waves, samples, timestep = base.parse(('\n'.join(common)+'\n').encode('ascii'), required_passes=5, required_cases=range(6))
    check(not waves and not samples and timestep is None and len(materials) == 4 and set(reports) == set(cases), 'Primary fixture coverage')
    return cases, materials, reports, eos


def review(data):
    cases, materials, reports, eos = data
    gas_constant = F(float('8.31446261815324'))
    expected_eos = set()
    for identifier, case in cases.items():
        base.prepare_scaled_parcels(case)
        base.review_case(case)
        check(reports[identifier] == len(case['scaled_parcels']) > 0, 'Scaled primary parcel count differs')
        for endpoint in (0, 1):
            for cell in range(case['w'] * case['h']):
                key = (identifier, endpoint, cell); expected_eos.add(key)
                mass = {s: v for (at, i, s), v in case['masses'].items() if (at, i) == (endpoint, cell)}
                check(set(mass).issubset(materials) and len(mass) >= 1, 'Missing primary material coefficients')
                capacity = sum((v * materials[s][1] for s, v in mass.items()), F(0))
                reference = sum((v * materials[s][2] for s, v in mass.items()), F(0))
                temperature = (case['cells'][endpoint, cell] - reference) / capacity
                volume = case['rplan'][0]
                pressure = gas_constant * temperature * sum((v / materials[s][0] for s, v in mass.items()), F(0)) / volume
                declared_p, declared_t = eos[key]
                close(temperature, F(300), F(300), 'Primary parcel changed uniform temperature')
                close(declared_t, temperature, abs(temperature), 'Primary temperature differs from actual U and constituent masses')
                close(declared_p, pressure, abs(pressure), 'Primary pressure differs from actual U and constituent masses')
        for field, scaled in (('pmasses', 'scaled_masses'), ('ppassives', 'scaled_passives')):
            check(set(case[field]) == set(case[scaled]), 'Scaled parcel field coverage differs')
    check(set(eos) == expected_eos, 'Missing/extra primary EOS readings')

    # Original rejection: every positive helium parcel is below half of the
    # smallest binary64 value. No narrowed zero enters either constituent sum.
    case = cases[0]
    helium = next(s for s, (molar, _, _) in materials.items() if F(3, 1000) < molar < F(5, 1000))
    for key, amount in case['pmasses'].items():
        if key[-1] == helium:
            check(F(0) < amount < F(1, 2**1075) and float(amount) == 0, 'Original helium parcel range differs')
    for identifier in (1, 2):
        values = [v for k, v in cases[identifier]['pmasses'].items() if k[-1] == helium]
        check(values and all(v > 0 and F(float(v)) != v and float(v) > 0 for v in values), 'Rounded subnormal control did not retain full values')
    check(all(F(0) < v < F(1, 2**1075) for k, v in cases[3]['ppassives'].items() if k[-1] == 0x80000000), 'Sparse passive range differs')

    case = cases[4]; quarter = F(1, 2**1076)
    check(len(case['parcels']) == 4 and all(case['pmasses'][*k, helium] == quarter for k in case['parcels']), 'New trace does not have four exact quarter-denormal contributions')
    check((0, 4, helium) not in case['masses'] and case['masses'][1, 4, helium] == 4 * quarter, 'New trace was rounded before accumulation')
    case=cases[5]
    check(case['wide_duals'] and all(0<abs(v)<F(1,2**1075) for v in case['wide_duals'].values() if v), 'Bulk flux range extension was bypassed')


def main():
    raw = Path(sys.argv[1]).read_bytes(); check(len(raw) <= 524288, 'Primary parcel capture cap')
    data = parse(raw); review(data); checks = base.CHECKS
    for corruption in ('full-constituent', 'reference-volume', 'new-target-trace'):
        bad = copy.deepcopy(data)
        if corruption == 'full-constituent':
            case = bad[0][0]; key = min(case['pmasses'], key=case['pmasses'].get); case['pmasses'][key] *= F(101, 100)
        elif corruption == 'reference-volume':
            case = bad[0][1]; key = next(iter(case['scaled_parcels'])); reference, energy = case['scaled_parcels'][key]
            case['scaled_parcels'][key] = (2 * reference, energy)
        else:
            case = bad[0][4]; key = next(k for k in case['masses'] if k[:2] == (1, 4) and case['masses'][k] < F(1, 2**1000))
            case['masses'][key] = 0
        rejected = False
        try:
            review(bad)
        except ValueError:
            rejected = True
        check(rejected, 'Altered primary parcel evidence accepted: ' + corruption)
    print(json.dumps(dict(Passed=True, SavedSha256=hashlib.sha256(raw).hexdigest(), SavedChecks=checks,
        CorruptionsRejected=3, NativeProcessLaunched=False, IndependentAgentReview=False,
        Scope='Scaled primary contributions with bounded binary64 cell storage; no strong-flow, free-surface, game or performance acceptance')))


if __name__ == '__main__':
    main()
