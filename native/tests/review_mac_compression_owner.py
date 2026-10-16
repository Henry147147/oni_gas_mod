"""Independent saved operands for atomic pressure/transport/compression ownership."""
import copy
from decimal import localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec=importlib.util.spec_from_file_location('compression',Path(__file__).with_name('review_mac_compression.py'))
comp=importlib.util.module_from_spec(spec);spec.loader.exec_module(comp)
ref=comp.ref;base=ref.base
check,close,number=base.check,base.close,base.number
COMPRESSION={'C'+s for s in ('PAIR','GRID','EVENT','SCELL','SMASS','SPASSIVE','SFACE','SOLID')}


def parse(raw):
    ordinary=[]; compression=[]; owners={}; controls=None
    for line in raw.decode('ascii').splitlines():
        row=line.split();kind=row[0]
        if kind in COMPRESSION:
            compression.append(line[1:])
        elif kind=='OWNER':
            check(len(row)==23,'Owner record shape')
            base.unique(owners,int(row[1]),(number(row[2]),int(row[3]),number(row[4]),*map(int,row[5:12]),number(row[12]),*[comp.scaled(row,i) for i in range(13,23,2)]))
        elif kind=='OCONTROL':
            check(controls is None and row[1:]==['1','1','1'],'Owner disabled/rollback/mode controls');controls=True
        else:
            ordinary.append(line)
            if kind in ('MATERIAL','PASS'):
                compression.append(line)
    check(controls and set(owners)=={0,1,2},'Missing owner records')
    pressure=ref.parse(('\n'.join(ordinary)+'\n').encode('ascii'),required_passes=4,require_acoustic=False,required_cases=(0,1,2),required_pressure_cases=(0,1,2))
    damped=comp.parse(('\n'.join(compression)+'\n').encode('ascii'),required_passes=4,required_grids=(0,1,2),analytic=False)
    return pressure,damped,owners


def review(data):
    pressure,damped,owners=data
    for identifier,case in pressure[0].items():
        base.review_case(case);ref.review_pressure(identifier,pressure)
    comp.review(damped,analytic=False)
    cases,_,pinfo,pcells,pfaces,*_=pressure
    for gid,case in cases.items():
        record=owners[gid]
        t0,r0,t1,r1,steps,attempts,rejects,events,un_u,un_j,projected,heat,residual,bound,wx,wy=record
        grid=damped['grids'][gid];w,h=grid[:2]
        check(t0==0 and r0==1 and t1==pinfo[gid][0] and r1==2 and steps==attempts==1 and rejects==0,'Atomic owner clock/counts')
        check((events,un_u,un_j)==grid[7:10] and events>0,'Owner compression counts')
        check(heat==grid[10] and projected==F(float(heat)),'Owner full heat/projection differs')
        expected_bound=pinfo[gid][6]+grid[12]
        close(bound,expected_bound,expected_bound,'Composed owner stage bound',8)
        terms=[];momentum=[[-wx],[-wy]];wall=[pinfo[gid][7],pinfo[gid][8]]
        for cell in range(w*h):
            if case['walls'][cell]:
                continue
            check(damped['cells'][gid,0,cell][0]==case['cells'][1,cell],'Compression stage U source differs')
            for field,source in (('masses','masses'),('passives','passives')):
                old={k:v for (g,e,c,k),v in damped[field].items() if (g,e,c)==(gid,0,cell)}
                expected={k:v for (e,c,k),v in case[source].items() if (e,c)==(1,cell)}
                check(old==expected,'Compression stage constituent/passive source differs')
            terms.extend((damped['cells'][gid,1,cell][0],-pcells[gid,cell][0]))
        for (axis,f),face in case['faces'].items():
            m0,m1=face[3:5];j0=pfaces[gid,axis,f][0];j1=damped['faces'][gid,axis,1,f][2]
            check(damped['faces'][gid,axis,0,f][0]==m1 and damped['faces'][gid,axis,0,f][2]==face[2],'Compression stage face source differs')
            terms.extend((comp.kinetic(m1,j1),-comp.kinetic(m0,j0)))
            momentum[axis].extend((j1,-j0));wall[axis]+=face[10]
        wall[0]+=grid[13];wall[1]+=grid[14]
        for actual,expected,account in zip((wx,wy),wall,momentum):
            close(actual,expected,abs(actual)+abs(expected),'Owner pressure/transport/compression wall sum',8)
            close(sum(account,F(0)),F(0),sum(map(abs,account),F(0)),'Owner actual momentum plus wall impulse')
        actual,scale=sum(terms,F(0)),sum(map(abs,terms),F(0))
        check(abs(actual)<=bound,'Owner actual represented U+K')
        close(residual,actual,scale,'Owner reported energy residual',2)
        if gid==2:
            check(heat>0 and projected==0 and un_u>0,'Owner below-range heat flag missing')


def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=524288,'Owner saved pipe bound')
    with localcontext() as ctx:
        ctx.prec=80;data=parse(raw);review(data);checks=base.CHECKS;rejected=0
        for kind in range(3):
            altered=copy.deepcopy(data)
            if kind<2:
                r=list(altered[2][0]);r[11 if kind==0 else 14]+=F(1,16);altered[2][0]=tuple(r)
            else:
                key=next(k for k in altered[1]['masses'] if k[1]==0);altered[1]['masses'][key]*=F(17,16)
            try:
                review(altered)
            except ValueError:
                rejected+=1
        check(rejected==3,'Owner corruption escaped')
    print(json.dumps(dict(Passed=True,SavedSha256=hashlib.sha256(raw).hexdigest(),SavedChecks=checks,CorruptionsRejected=rejected,
        NativeProcessLaunched=False,IndependentAgentReview=False,Scope='Three single-step pressure/transport/compression owners, full energy and wall accounts; no strong-flow, production, game or performance acceptance')))


if __name__=='__main__':
    main()
