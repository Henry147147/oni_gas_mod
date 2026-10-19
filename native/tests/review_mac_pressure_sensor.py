"""Exact saved pressure sensors and compatible compression; no native run."""
import copy
from decimal import localcontext
from fractions import Fraction as F
import hashlib,importlib.util,json
from pathlib import Path
import sys
spec=importlib.util.spec_from_file_location('pressure_acoustic_review',Path(__file__).with_name('review_mac_acoustic_compression.py'))
ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
base=ref.base;check,close,number,unique=ref.check,ref.close,ref.number,ref.unique

def parse(raw):
    ordinary=[];extra={k:{} for k in ('linear','speeds','pressures','sensors','triples')};rejected=None
    for line in raw.decode('ascii').splitlines():
        r=line.split();check(bool(r),'Empty pressure sensor record')
        if r[0]=='ALINEAR':
            check(len(r)==8,'Pressure-sensed pair shape');unique(extra['linear'],int(r[1]),tuple(ref.comp.scaled(r,i) for i in (2,4,6)))
        elif r[0]=='ASPEED':
            check(len(r)==5,'Pressure-sensed acoustic speed shape');unique(extra['speeds'],tuple(map(int,r[1:3])),ref.comp.scaled(r,3))
        elif r[0]=='PSTATE':
            check(len(r)==4,'Frozen pressure shape');unique(extra['pressures'],tuple(map(int,r[1:3])),number(r[3]))
        elif r[0]=='PSENSOR':
            check(len(r)==6,'Pressure sensor field shape');unique(extra['sensors'],tuple(map(int,r[1:4])),ref.comp.scaled(r,4))
        elif r[0]=='PTRIPLE':
            check(len(r)==7,'Pressure triple shape');unique(extra['triples'],int(r[1]),(*map(number,r[2:5]),ref.comp.scaled(r,5)))
        elif r[0]=='PREJECTED':
            check(len(r)==2 and rejected is None,'Pressure rejection shape');rejected=int(r[1])
        else:ordinary.append(line)
    data=ref.comp.parse(('\n'.join(ordinary)+'\n').encode(),required_passes=3,required_grids=range(5),analytic=False)
    check(rejected==8 and set(extra['triples'])==set(range(8)),'Missing pressure controls')
    return data,extra

def review(data,extra):
    for left,centre,right,actual in extra['triples'].values():
        check(min(left,centre,right)>=0,'Invalid pressure triple')
        den=left+2*centre+right;expected=abs(left-2*centre+right)/den if den else F(0)
        check(0<=actual<=1,'Scalar pressure sensor outside interval')
        close(actual,expected,expected,'Independent exact pressure triple',16)
    check(extra['triples'][5][3]>0 and float(extra['triples'][5][3])==0,'Lost subrange positive pressure sensor')
    ref.review_component(data,extra,pressure_sensed=True)
    for gid in (0,1):
        check(any(v>F(1,10) for (g,_,_),v in extra['sensors'].items() if g==gid),'Shock grid has no active pressure sensor')
        check(any(extra['linear'][e[3]][0]>0 for (g,_),e in data['events'].items() if g==gid),'Shock grid has no acoustic impulse')
    check(all(v==0 for (g,_,_),v in extra['sensors'].items() if g==3),'Uniform pressure activated sensed linear damping')
    tiny=data['grids'][4];check(tiny[10]>0 and float(tiny[10])==0 and ref.comp.retained_cell_heat(data,4,tiny[8]),'Sensed grid lost below-range heat/unresolved flags')
    initial=data['faces'][4,0,0,1][2];final=data['faces'][4,0,1,1][2]
    check(initial==0 and final!=0 and float(final)==0,'Sensed grid did not retain new below-range canonical momentum')

def main():
    raw=Path(sys.argv[1]).read_bytes();data,extra=parse(raw)
    with localcontext() as context:
        context.prec=80;review(data,extra);checks=base.CHECKS+ref.heat.base.CHECKS;rejected=0
        for mutation in range(4):
            bad,other=copy.deepcopy(data),copy.deepcopy(extra)
            if mutation==0:
                row=list(other['triples'][2]);row[3]*=F(17,16);other['triples'][2]=tuple(row)
            elif mutation==1:
                key=next(k for k,v in other['sensors'].items() if v>0);other['sensors'][key]*=F(17,16)
            elif mutation==2:
                key=min((k for k in bad['masses'] if k[1]==1 and bad['materials'][k[3]][0]==0),key=lambda k:bad['masses'][k]);bad['masses'][key]*=F(17,16)
            else:
                key=next(k for k,v in other['linear'].items() if v[0]>0);row=list(other['linear'][key]);row[0]*=F(17,16);other['linear'][key]=tuple(row)
            try:review(bad,other)
            except ValueError:rejected+=1
        check(rejected==4,'Altered scalar/grid sensor, trace or coefficient escaped review')
    result=dict(Passed=True,SavedChecks=checks,CorruptionsRejected=rejected,SavedSha256=hashlib.sha256(raw).hexdigest(),NativeProcessLaunched=False,IndependentAgentReview=False,
        Scope='Separate pressure-sensed compression component; no owning, original acoustic/strong-flow or game acceptance from this review.')
    if len(sys.argv)>2:
        with Path(sys.argv[2]).open('x') as output:json.dump(result,output,indent=2)
    print(json.dumps(result))

if __name__=='__main__':main()
