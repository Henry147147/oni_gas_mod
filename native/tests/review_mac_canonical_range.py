"""Review full canonical state, pressure/transport and owner records; no native process."""
import copy
from decimal import localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec=importlib.util.spec_from_file_location('canonical_closed_saved',Path(__file__).with_name('review_mac_closed_world.py'))
closed=importlib.util.module_from_spec(spec);spec.loader.exec_module(closed)
base=closed.base;check,close=base.check,base.close


def parse(raw):
    common=[];owners={}
    for line in raw.decode('ascii').splitlines():
        row=line.split();check(bool(row),'Empty canonical record')
        if row[0]=='CANONICAL':
            check(len(row)==7,'Canonical owner shape')
            base.unique(owners,int(row[1]),(int(row[2]),int(row[3]),base.number(row[4]),int(row[5]),int(row[6])))
        else:common.append(line)
    data=closed.parse(('\n'.join(common)+'\n').encode('ascii'),required_passes=5,require_acoustic=False,required_cases=(0,1,2,3))
    check(set(owners)=={0,1},'Canonical owner coverage')
    return data,owners


def review(parsed):
    data,owners=parsed;cases=data[0]
    for case in cases.values():base.review_case(case)
    for identifier in (0,1):
        closed.review_pressure(identifier,data)
        case=cases[identifier];dt=data[2][identifier][0]
        check(owners[identifier][:4]==(1,0,dt,2),'Full canonical owner clock/revision/retry differs')
        check(owners[identifier][4]==(4 if identifier==0 else 0),'Root velocity projection count differs')
        moving=[(key,row) for key,row in case['faces'].items() if key[0]==0]
        check(moving and all(0<row[1]==row[2]<F(1,2**1075) for key,row in moving),'Canonical state was narrowed or uniform J changed')
        check(all(0<row[0]<F(1,2**1075) for key,row in moving),'Full bulk flux was lost')
        for key,row in moving:
            initial,sweep,helper=data[4][identifier,*key]
            check(initial==row[1] and helper>0 and sweep>0,'Full pressure quantities disagree')
            check((float(sweep)==0) if identifier==0 else (float(sweep)>0),'Volume sweep range control differs')
    case=cases[2];row=case['faces'][0,1]
    check(row[1]==0 and 0<row[2]<F(1,2**1075),'Original newly occupied canonical face is missing')
    case=cases[3];row=case['faces'][0,2]
    check(row[2]==0 and row[9]>0 and -F(1,2**1075)<row[10]<0,'Full wall reaction/kinetic heat was lost')


def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=524288,'Canonical saved pipe cap')
    with localcontext() as context:
        context.prec=80;parsed=parse(raw);review(parsed);checks=base.CHECKS
        for corruption in ('canonical-state','swept-volume','wall-reaction'):
            bad=copy.deepcopy(parsed);cases=bad[0][0]
            if corruption=='swept-volume':
                values=bad[0][4];key=(0,0,0);row=list(values[key]);row[1]*=F(101,100);values[key]=tuple(row)
            else:
                case=cases[0 if corruption=='canonical-state' else 3];key=(0,0 if corruption=='canonical-state' else 2)
                row=list(case['faces'][key]);row[2 if corruption=='canonical-state' else 10]*=F(101,100);case['faces'][key]=tuple(row)
            rejected=False
            try:review(bad)
            except ValueError:rejected=True
            check(rejected,'Altered full canonical evidence accepted: '+corruption)
    print(json.dumps(dict(Passed=True,SavedSha256=hashlib.sha256(raw).hexdigest(),SavedChecks=checks,CorruptionsRejected=3,
        NativeProcessLaunched=False,IndependentAgentReview=False,
        Scope='Range-extended zero-gravity canonical state, velocity, sweeps, bulk flux, wall reaction and two uniform closed-owner steps; no general gravity, strong-flow, game or performance acceptance')))


if __name__=='__main__':main()
