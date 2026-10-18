"""Saved full-range coupled pressure review; exact flux/work accounts, no native process."""
import copy
from decimal import Decimal as D,localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec=importlib.util.spec_from_file_location('coupled_range_closed',Path(__file__).with_name('review_mac_closed_world.py'))
closed=importlib.util.module_from_spec(spec);spec.loader.exec_module(closed)
base=closed.base
check,close,unique=base.check,base.close,base.unique

def parse(raw):
    common=[];records={};cells={};active=None
    for line in raw.decode('ascii').splitlines():
        row=line.split();check(bool(row),'Empty coupled range record')
        if row[0]=='CRANGE':
            check(len(row)==13,'Coupled range shape')
            unique(records,int(row[1]),(tuple(map(int,row[2:9])),base.scaled_energy(row[9],row[10]),base.scaled_energy(row[11],row[12])))
        elif row[0]=='CRANGECELL':
            check(len(row)==8,'Coupled range cell shape')
            unique(cells,tuple(map(int,row[1:3])),(base.number(row[3]),base.scaled_energy(row[4],row[5]),base.scaled_energy(row[6],row[7])))
        elif row[0]=='CRANGEACTIVE':
            check(len(row)==5 and active is None,'Active coupled range control shape/duplicate');active=tuple(map(int,row[1:]))
        else:common.append(line)
    data=closed.parse(('\n'.join(common)+'\n').encode('ascii'),required_passes=4,require_acoustic=False,
        required_cases=tuple(range(8)),required_pressure_cases=tuple(range(8)))
    check(set(records)==set(range(8)) and set(cells)=={(i,c) for i in range(8) for c in range(4)},'Coupled range coverage')
    check(active is not None and active[0]==1 and active[1]>0 and active[2:]==(1,0),'Active coupled stress path coverage differs')
    return data,records,cells

def review(parsed):
    data,records,cells=parsed
    for identifier,case in data[0].items():
        base.review_case(case);closed.review_pressure(identifier,data)
        flags,residual,bound=records[identifier];axis,divergent,rounded,unresolved,owner_unresolved,mode,responses=flags
        check(axis in (0,1) and divergent==(identifier in (3,4,5)) and mode==3 and responses==4,'Coupled mode/response coverage differs')
        check(rounded==(4 if identifier<6 else 0),'Incoming full velocity projection count differs')
        pairs=base.topology(case);delta=[F(0) for _ in range(4)]
        for direction in (0,1):
            for face,(left,right) in enumerate(pairs[direction]):
                q=data[4][identifier,direction,face][1]
                check(left is not None and right is not None,'Range controls must retain periodic open geometry')
                delta[left]+=q;delta[right]-=q
                if identifier!=7 and direction==axis:check(case['faces'][direction,face][0]!=0,'Full bulk flux was lost')
        terms=[];unresolved_expected=0
        for cell in range(4):
            pressure,actual_volume,actual_work=cells[identifier,cell]
            close(actual_volume,delta[cell],abs(actual_volume)+abs(delta[cell]),'Actual full volume was narrowed')
            work=-pressure*delta[cell]
            close(actual_work,work,abs(actual_work)+abs(work),'Actual full compressive work was narrowed')
            original,_,root_delta,root_pressure,*_=data[3][identifier,cell]
            check(root_delta==0 and pressure==root_pressure,'Range control did not retain its declared zero pressure-root parameter')
            after=case['cells'][0,cell];error=after-original-work
            scale=abs(after)+abs(original)+abs(work)
            check(abs(error)<=128*base.EPS*scale,'Actual local full pressure-work account exceeds its operand bound')
            if work and abs(after-original-actual_work)>abs(actual_work)*F(1,2**47):unresolved_expected+=1
            terms.extend((after,-original,-work))
        check(unresolved==owner_unresolved==unresolved_expected==(4 if divergent else 0),'Unresolved full work was concealed')
        actual=sum(terms,F(0));scale=sum(map(abs,terms),F(0))
        close(residual,actual,scale,'Full coupled work residual differs')
        close(bound,128*base.EPS*scale,128*base.EPS*scale,'Full coupled work bound differs',512)
        check(abs(actual)<=bound,'Full coupled work violates its actual operand bound')

def liquid_oracle_controls(materials):
    species=next(s for s,v in materials.items() if v[0]==1)
    _,_,cv,offset,rho,bulk=materials[species];mass=rho*F(11,10);masses={species:mass}
    m,r,k=map(closed.dec,(mass,rho,bulk));p0=k/10
    def elastic(p):return m/r*k*((1+p/k).ln()-p/(k+p))
    energy=F(float(closed.dec(mass*offset+mass*cv*300)+elastic(p0)))
    pressure,_,gas,_=closed.equilibrium(masses,materials,energy,F(1))
    check(abs(pressure-p0)<=D('1e-45')*p0 and gas==0,'Independent compressed-liquid EOS differs from analytic cold curve')
    for delta in (F(1,10000),F(-1,10000),F(1,10**8),F(-1,10**8)):
        p1=k*(m/(r*closed.dec(1+delta))-1)
        expected=-(elastic(p1)-elastic(p0))/closed.dec(delta)
        actual=closed.isentropic_work(masses,materials,energy,F(1),delta)
        check(abs(actual-expected)<=D('1e-45')*abs(expected),'Independent liquid work differs from analytic elastic energy difference')
    try:closed.equilibrium(masses,materials,energy,F(2))
    except ValueError:pass
    else:raise ValueError('Independent occupied liquid oracle admitted internal void')

def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=1048576,'Coupled range pipe cap')
    with localcontext() as context:
        context.prec=100;data=parse(raw);review(data);liquid_oracle_controls(data[0][1]);checks=base.CHECKS;rejected=[]
        for name in ('incoming-J','midpoint-sweep','full-work','full-volume','unresolved-count','work-bound','positive-trace'):
            altered=copy.deepcopy(data);physical,records,cells=altered
            if name in ('incoming-J','midpoint-sweep'):
                key=(0,0,0);row=list(physical[4][key]);row[0 if name=='incoming-J' else 1]=F(0);physical[4][key]=tuple(row)
            elif name in ('full-work','full-volume'):
                key=next(k for k,v in cells.items() if k[0]==3 and v[2]);row=list(cells[key]);row[2 if name=='full-work' else 1]*=2;cells[key]=tuple(row)
            elif name in ('unresolved-count','work-bound'):
                flags,residual,bound=records[3]
                if name=='unresolved-count':flags=list(flags);flags[3]=flags[4]=0;flags=tuple(flags)
                else:bound=F(0)
                records[3]=(flags,residual,bound)
            else:
                case=physical[0][0];key=next(k for k,v in case['masses'].items() if k[0]==1 and 0<v<F(1,10**20));case['masses'][key]/=2
            try:review(altered)
            except (ValueError,KeyError,AssertionError):rejected.append(name)
            else:raise ValueError('Corrupted coupled range record admitted: '+name)
    print(json.dumps(dict(Passed=True,Cases=8,SavedChecks=checks,CorruptionsRejected=rejected,SavedSha256=hashlib.sha256(raw).hexdigest(),
        NativeProcessLaunched=False,IndependentAgentReview=False,Scope='Coupled-pressure full incoming canonical motion and midpoint sweeps, actual per-cell work and stored-U projection diagnostics; no strong-flow, full primary remainders or production acceptance.')))

if __name__=='__main__':main()
