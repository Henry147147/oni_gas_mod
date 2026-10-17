"""Actual stored-state heat entropy, independently reconstructed from saved U."""
import copy
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec=importlib.util.spec_from_file_location('heat',Path(__file__).with_name('review_mac_shock_heat.py'))
heat=importlib.util.module_from_spec(spec);spec.loader.exec_module(heat)
check,close=heat.check,heat.close
dec=heat.dec;EPS=heat.EPS

def parse(raw):
    rows=raw.decode('ascii').splitlines();cool=[r.split() for r in rows if r.startswith('HTCOOL ')]
    check(len(cool)==1 and len(cool[0])==15 and cool[0][1:4]==['0','0','0'],'Cooling control shape')
    data=heat.parse(('\n'.join(r for r in rows if not r.startswith('HTCOOL '))+'\n').encode('ascii'),required_passes=3,required_grids=(),controls=False)
    check(set(data['pairs'])==set(range(5)),'Complete captured heat pair controls')
    r=cool[0]
    data['cool']=(*map(heat.number,r[4:11]),heat.base.scaled_energy(r[11],r[12]),heat.base.scaled_energy(r[13],r[14]))
    return data

def entropy_terms(data,key):
    p=data['pairs'][key];terms=[]
    for side,old_u,new_u in ((0,p[4],p[6]),(1,p[5],p[7])):
        masses={i:m for (case,s,i),m in data['pmasses'].items() if (case,s)==(key,side)}
        check(all(data['materials'][i][0]==0 for i in masses),'Captured pure gas phases')
        capacity=sum((m*data['materials'][i][2] for i,m in masses.items()),F(0))
        offset=sum((m*data['materials'][i][3] for i,m in masses.items()),F(0))
        check(old_u-offset>0 and new_u-offset>0,'Actual caloric logarithm domain')
        terms.append(dec(capacity)*(dec(new_u-offset)/dec(old_u-offset)).ln())
    return terms

def relative_close(actual,terms,why):
    expected=sum(terms,D(0));scale=sum(map(abs,terms),D(0))
    check(abs(dec(actual)-expected)<=128*dec(EPS)*scale,why)

def review(data):
    expected=[(F(1.5955820824192574e-05),F(1.5955820824192582e-33),F(-.0033121700131082035)),
              (F(1.5955820813853493e-05),F(1.5955820813853496e-33),F(-.0033124391079650661))]
    volume=F(1.52587890625e-05);conductance=F(.64028600022444926)*F(2)**-37
    for key,p in data['pairs'].items():
        heat.review_pair(data,key)
        terms=entropy_terms(data,key);relative_close(p[-2],terms,'Relative actual stored-state entropy increment')
        check(p[:2]==(volume,volume),'Captured pair fixed volumes')
        for side in (0,1):
            source_side=1-side if key==1 else 0 if key==4 else side
            nitrogen,helium,energy=expected[source_side]
            check({i:m for (case,s,i),m in data['pmasses'].items() if (case,s)==(key,side)}=={1:nitrogen,7:helium},'Captured positive bulk/trace masses')
            check(p[4+side]==energy,'Captured original stored energy')
        if key in (0,1):
            check(p[14]==conductance and p[-2]>0 and sum(terms,D(0))>0,'Captured positive entropy/actual conductance')
        else:check(p[6:8]==p[4:6] and p[-2]==0,'Unchanged stored U has zero entropy increment')
    a,b=data['pairs'][0],data['pairs'][1]
    check(a[6:8]==(F(-.0033121700131083093),F(-.0033124391079649602)) and b[6:8]==a[6:8][::-1],'Exact captured endpoint regression/reflection')
    check((a[6]-a[4])+(a[7]-a[5])==0,'Exact actual pair energy conservation')
    check(data['pairs'][3][18]>0 and data['pairs'][3][18]<F(2)**-1074 and data['pairs'][3][3]==2,'Unresolved full-range heat retained')
    v,u0,u1,t0,t1,s0,s1,ds,bound=data['cool'];check(v==volume and u0==a[4] and u1==F(float(u0-F(.001))),'Genuine cooling actual source/endpoint')
    masses={i:m for (case,side,i),m in data['pmasses'].items() if (case,side)==(0,0)}
    capacity=sum((m*data['materials'][i][2] for i,m in masses.items()),F(0));offset=sum((m*data['materials'][i][3] for i,m in masses.items()),F(0))
    expected_ds=dec(capacity)*(dec(u1-offset)/dec(u0-offset)).ln();relative_close(ds,[expected_ds],'Genuine cooling relative entropy')
    close(bound,128*EPS*(abs(u0)/t0+abs(u1)/t1),bound,'Unchanged original cooling allowance',8)
    check(ds+bound<0 and expected_ds<0,'Genuine cooling must still fail original entropy sign')
    old=heat.eos(masses,data['materials'],u0,v);new=heat.eos(masses,data['materials'],u1,v)
    for actual,expected_value in ((t0,old[1]),(t1,new[1])):heat.near(actual,expected_value,'Independent cooling EOS')
    return dict(Pairs=5,PositiveCapturedTransfers=2,UnchangedStoredStates=3,GenuineCoolingRejected=True,PreservedPositiveHeliumTrace=True)

def run(raw):
    with localcontext() as context:
        context.prec=100;heat.base.CHECKS=0;data=parse(raw);summary=review(data);checks=heat.base.CHECKS;mutants=[]
        for kind in ('zero_trace','zero_entropy','endpoint_energy','doubled_allowance','hidden_cooling'):
            bad=copy.deepcopy(data)
            if kind=='zero_trace':bad['pmasses'][0,0,7]=F(0)
            elif kind=='hidden_cooling':bad['cool']=(*bad['cool'][:-2],F(0),bad['cool'][-1])
            else:
                row=list(bad['pairs'][0]);index={'zero_entropy':-2,'endpoint_energy':6,'doubled_allowance':-1}[kind]
                row[index]=F(0) if kind=='zero_entropy' else row[index]*2 if kind=='doubled_allowance' else row[index]+F(.001);bad['pairs'][0]=tuple(row)
            failure=None
            try:review(bad)
            except (ValueError,AssertionError) as error:failure=str(error)
            check(failure is not None,'Corrupted heat entropy record accepted');mutants.append(dict(Kind=kind,RejectedAt=failure))
        return dict(Passed=True,Summary=summary,SavedChecks=checks,Mutants=mutants,Scope='Saved fixed-inventory gas heat entropy regression; original128epsilon bounds and actual stored endpoints. No owning strong-flow or Unity certification.')

if __name__=='__main__':
    raw=Path(sys.argv[1]).read_bytes();result=run(raw);result['RawSha256']=hashlib.sha256(raw).hexdigest()
    with Path(sys.argv[2]).open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2);stream.write('\n')
    print(json.dumps(result))
