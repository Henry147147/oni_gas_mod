"""Exact saved parcel balances, independent EOS and oblique sine comparison."""
import copy
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec=importlib.util.spec_from_file_location('closed_saved',Path(__file__).with_name('review_mac_closed_world.py'))
closed=importlib.util.module_from_spec(spec);spec.loader.exec_module(closed)
base=closed.base;check,close,number,unique,dec=base.check,base.close,base.number,base.unique,closed.dec


def parse(raw):
    ordinary=[];materials={};waves={};samples={}
    for line in raw.decode('ascii').splitlines():
        row=line.split();check(bool(row),'Empty reconstruction record')
        if row[0]=='MATERIAL':
            check(len(row)==8 and int(row[2]) in (0,1),'Reconstruction material shape')
            unique(materials,int(row[1]),(int(row[2]),*map(number,row[3:])))
        elif row[0]=='RWAVE':
            check(len(row)==4,'Reconstructed wave shape');unique(waves,int(row[1]),(int(row[2]),number(row[3])))
        elif row[0]=='RSAMPLE':
            check(len(row)==4,'Reconstructed sample shape');unique(samples,tuple(map(int,row[1:3])),number(row[3]))
        else:ordinary.append(line)
    cases,*_=base.parse(('\n'.join(ordinary)+'\n').encode('ascii'),required_passes=5)
    check(len(materials)==4 and set(waves)=={16,32,64} and set(samples)=={(n,x) for n in waves for x in range(n)},'Missing reconstructed model/wave records')
    return cases,materials,waves,samples


def temperature(masses,materials,energy,volume,minimum=1):
    if any(materials[s][0]==0 for s in masses):
        return closed.equilibrium(masses,materials,energy,volume)[:2]
    check(len(masses)==1,'Cold reference expects one explicit elastic material')
    species,amount=next(iter(masses.items()));phase,molar,cv,offset,rho,bulk=materials[species]
    check(phase==1 and rho>0 and bulk>0,'Cold material identity')
    p=dec(bulk)*(dec(amount)/(dec(rho)*dec(volume))-1)
    check(p>0,'Reconstructed cold liquid lost occupied compression')
    q=p/dec(bulk);elastic=dec(amount)/dec(rho)*dec(bulk)*((1+q).ln()-q/(1+q))
    t=(dec(energy)-dec(amount)*dec(offset)-elastic)/(dec(amount)*dec(cv))
    allowance=dec(256*base.EPS*(abs(energy)+abs(amount*offset)))/dec(amount*cv)
    check(t>=minimum-allowance,'Independent reconstructed cold-liquid temperature')
    return p,t


def review(data):
    cases,materials,waves,samples=data
    for identifier,case in cases.items():
        check(case['rplan'] is not None,'Reconstructed control used donor-order path')
        base.review_case(case)
        nc=case['w']*case['h'];nominal=case['rplan'][0]
        minimum=300 if identifier==1 else 1
        if identifier==1:check(case['rplan'][1]>0,'Cold state did not exercise EOS backtracking')
        for endpoint in (0,1):
            for i in range(nc):
                masses={s:m for (e,c,s),m in case['masses'].items() if (e,c)==(endpoint,i)}
                p,t=temperature(masses,materials,case['cells'][endpoint,i],nominal,minimum)
                if identifier==0:
                    check(abs(p-100000)<=D('2e-6') and abs(t-300)<=D('6e-9'),'Independent mixed contact pressure/temperature')
        for (axis,face),(donor,target,volume,energy) in case['parcels'].items():
            masses={s:m for (a,f,s),m in case['pmasses'].items() if (a,f)==(axis,face)}
            temperature(masses,materials,energy,volume,minimum)
        for i,(source_volume,volume,energy,theta) in case['rcells'].items():
            masses={s:m for (c,s),m in case['rmasses'].items() if c==i}
            temperature(masses,materials,energy,volume,minimum)
    for n,(steps,error) in waves.items():
        check(0<steps<=int((D('.125')/(D('.2')/n/D('.75'))).to_integral_value(rounding='ROUND_CEILING'))+2,'Reconstructed step bound')
        pi=base.PI;h=D(1)/n;sinc=base.sincos(pi*h)[0]/(pi*h);expected_error=D(0)
        for x in range(n):
            actual=samples[n,x];exact=1+D('.2')*sinc*sinc*base.sincos(2*pi*((D(x)+1)*h-D('.09375')))[0]
            check(F(4,5)-F(1,10**12)<=actual<=F(6,5)+F(1,10**12),'Reconstructed tag range')
            expected_error+=abs(dec(actual)-exact)/n
        check(abs(dec(error)-expected_error)<D('2e-11'),'Oblique complete-grid/row error agreement')
    for a,b in ((16,32),(32,64)):check(0<waves[b][1]<F(45,100)*waves[a][1],'Oblique reconstruction .45 refinement bound')


def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=262144,'Reconstruction raw pipe cap')
    with localcontext() as ctx:
        ctx.prec=80;data=parse(raw);review(data);checks=base.CHECKS
        for control in ('trace-parcel','common-limiter','wave-profile'):
            bad=copy.deepcopy(data)
            if control=='trace-parcel':
                case=bad[0][0];key=next(k for k,v in case['pmasses'].items() if v<F(1,10**20));case['pmasses'][key]/=10
            elif control=='common-limiter':
                case=bad[0][1];i=next(i for i,v in case['rcells'].items() if v[3]>0 and any(p[0]==i for p in case['parcels'].values()))
                v=list(case['rcells'][i]);v[3]/=2;case['rcells'][i]=tuple(v)
            else:bad[3][16,0]+=F(1,100)
            caught=False
            try:review(bad)
            except ValueError:caught=True
            check(caught,'Altered reconstructed result accepted')
    print(json.dumps(dict(Passed=True,SavedSha256=hashlib.sha256(raw).hexdigest(),SavedChecks=checks,CorruptionsRejected=3,
        NativeProcessLaunched=False,IndependentAgentReview=False,Scope='Reconstructed occupied MAC parcels, EOS and scalar accuracy; no production/game acceptance')))


if __name__=='__main__':main()
