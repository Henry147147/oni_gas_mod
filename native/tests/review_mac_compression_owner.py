"""Independent saved operands for atomic pressure/transport/compression ownership."""
import copy
from decimal import Decimal as D, localcontext
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
BEFORE={'B'+s for s in ('PAIR','GRID','EVENT','SCELL','SMASS','SPASSIVE','SFACE','SOLID')}


def parse(raw):
    ordinary=[]; before=[]; compression=[]; owners={}; coupling={}; controls=None
    for line in raw.decode('ascii').splitlines():
        row=line.split();kind=row[0]
        if kind in COMPRESSION:
            compression.append(line[1:])
        elif kind in BEFORE:
            before.append(line[1:])
        elif kind=='OWNER':
            check(len(row)==25,'Owner record shape')
            base.unique(owners,int(row[1]),(number(row[2]),int(row[3]),number(row[4]),*map(int,row[5:12]),number(row[12]),*[comp.scaled(row,i) for i in range(13,23,2)],number(row[23]),number(row[24])))
        elif kind=='COUPLING':
            check(len(row)==16,'Coupling shape')
            base.unique(coupling,int(row[1]),(*map(number,row[2:10]),comp.scaled(row,10),comp.scaled(row,12),number(row[14]),number(row[15])))
        elif kind=='OCONTROL':
            check(controls is None and row[1:]==['1','1','1'],'Owner disabled/rollback/mode controls');controls=True
        else:
            ordinary.append(line)
            if kind in ('MATERIAL','PASS'):
                compression.append(line);before.append(line)
    check(controls and set(owners)=={0,1,2} and set(coupling)=={8,16,32},'Missing owner/coupling records')
    pressure=ref.parse(('\n'.join(ordinary)+'\n').encode('ascii'),required_passes=5,require_acoustic=False,required_cases=(0,1,2),required_pressure_cases=(0,1,2))
    initial=comp.parse(('\n'.join(before)+'\n').encode('ascii'),required_passes=5,required_grids=(0,1,2),analytic=False)
    damped=comp.parse(('\n'.join(compression)+'\n').encode('ascii'),required_passes=5,required_grids=(0,1,2),analytic=False)
    return pressure,initial,damped,owners,coupling


def review(data):
    pressure,initial,damped,owners,coupling=data
    for identifier,case in pressure[0].items():
        base.review_case(case);ref.review_pressure(identifier,pressure)
    comp.review(initial,analytic=False);comp.review(damped,analytic=False)
    cases,_,pinfo,pcells,pfaces,*_=pressure
    for gid,case in cases.items():
        record=owners[gid]
        t0,r0,t1,r1,steps,attempts,rejects,events,un_u,un_j,projected,heat,residual,bound,wx,wy,ds,sb=record
        grid=damped['grids'][gid];first=initial['grids'][gid];w,h=grid[:2]
        check(t0==0 and r0==1 and t1==pinfo[gid][0] and r1==2 and steps==attempts==1 and rejects==0,'Atomic owner clock/counts')
        check(first[:6]==grid[:6] and first[6]==grid[6]==pinfo[gid][0]/2,'Symmetric stage geometry/time differs')
        check((events,un_u,un_j)==tuple(a+b for a,b in zip(first[7:10],grid[7:10])) and events>0,'Owner compression counts')
        close(heat,first[10]+grid[10],first[10]+grid[10],'Owner full symmetric heat',8)
        check(projected==F(float(heat)),'Owner full heat projection differs')
        expected_bound=pinfo[gid][6]+first[12]+grid[12]
        close(bound,expected_bound,expected_bound,'Composed owner stage bound',8)
        terms=[];momentum=[[-wx],[-wy]];wall=[pinfo[gid][7],pinfo[gid][8]];entropy=D(0);entropy_scale=F(0)
        for cell in range(w*h):
            if case['walls'][cell]:
                continue
            check(damped['cells'][gid,0,cell][0]==case['cells'][1,cell],'Compression stage U source differs')
            check(initial['cells'][gid,1,cell][0]==pcells[gid,cell][0],'Pressure source is not first compression endpoint')
            for field,source in (('masses','masses'),('passives','passives')):
                old={k:v for (g,e,c,k),v in damped[field].items() if (g,e,c)==(gid,0,cell)}
                expected={k:v for (e,c,k),v in case[source].items() if (e,c)==(1,cell)}
                check(old==expected,'Compression stage constituent/passive source differs')
                prepared={k:v for (g,e,c,k),v in initial[field].items() if (g,e,c)==(gid,1,cell)}
                expected_initial={k:v for (e,c,k),v in case[source].items() if (e,c)==(0,cell)}
                check(prepared==expected_initial,'Pressure source constituent/passive differs')
            start=initial['cells'][gid,0,cell];end=damped['cells'][gid,1,cell]
            terms.extend((end[0],-start[0]))
            entropy_scale+=abs(start[0])/start[1]+abs(end[0])/end[1]
            for state,point,sign in ((initial,start,-1),(damped,end,1)):
                e=0 if sign<0 else 1
                mass={s:v for (g,endpoint,c,s),v in state['masses'].items() if (g,endpoint,c)==(gid,e,cell)}
                entropy+=sign*ref.equilibrium(mass,state['materials'],point[0],grid[2]*grid[2]*grid[3])[3]
        for (axis,f),face in case['faces'].items():
            m0,m1=face[3:5];j0=initial['faces'][gid,axis,0,f][2];j1=damped['faces'][gid,axis,1,f][2]
            check(initial['faces'][gid,axis,1,f][0]==m0 and initial['faces'][gid,axis,1,f][2]==pfaces[gid,axis,f][0],'Pressure source face differs from first compression')
            check(damped['faces'][gid,axis,0,f][0]==m1 and damped['faces'][gid,axis,0,f][2]==face[2],'Compression stage face source differs')
            terms.extend((comp.kinetic(m1,j1),-comp.kinetic(m0,j0)))
            momentum[axis].extend((j1,-j0));wall[axis]+=face[10]
        wall[0]+=first[13]+grid[13];wall[1]+=first[14]+grid[14]
        for actual,expected,account in zip((wx,wy),wall,momentum):
            close(actual,expected,abs(actual)+abs(expected),'Owner pressure/transport/compression wall sum',8)
            close(sum(account,F(0)),F(0),sum(map(abs,account),F(0)),'Owner actual momentum plus wall impulse')
        actual,scale=sum(terms,F(0)),sum(map(abs,terms),F(0))
        check(abs(actual)<=bound,'Owner actual represented U+K')
        close(residual,actual,scale,'Owner reported energy residual',2)
        close(sb,128*base.EPS*entropy_scale,128*base.EPS*entropy_scale,'Owner endpoint entropy operand bound',512)
        close(ds,F(entropy),entropy_scale,'Owner actual endpoint entropy',128)
        check(entropy>=-ref.dec(sb) and ds>=-sb,'Owner entropy decreases')
        if gid==2:
            check(heat>0 and projected==0 and un_u>0,'Owner below-range heat flag missing')
    previous=None
    for n,values in sorted(coupling.items()):
        duration,il,ir,force,coefficient,c0,symmetric,post,q,work,err,post_err=values
        check((duration,il,ir,force,c0)==(F(1,2),2,4,4,5) and coefficient==F(float('.4')),'Manufactured coupling inputs changed')
        a=1/il+1/ir;nu=ref.dec(a*coefficient);s=ref.dec(force/coefficient).sqrt()
        z=(ref.dec(c0)-s)/(ref.dec(c0)+s)*(-2*nu*s*ref.dec(duration)).exp();exact=s*(1+z)/(1-z)
        expected=abs(ref.dec(symmetric)-exact)/ref.dec(c0);expected_post=abs(ref.dec(post)-exact)/ref.dec(c0)
        close(err,F(expected),(abs(symmetric)+abs(F(exact)))/c0, 'Independent forced-coupling error')
        close(post_err,F(expected_post),(abs(post)+abs(F(exact)))/c0,'Independent first-order coupling error')
        dt=duration/n;reference_outputs=[];reference_heat=reference_work=F(0)
        for mode in (0,1):
            c=c0
            for _ in range(n):
                if mode==0:
                    next_c=c/(1+coefficient*a*c*dt/2);reference_heat+=(c*c-next_c*next_c)/(2*a);c=next_c
                increment=a*force*dt
                if mode==0:reference_work+=force*dt*(c+increment/2)
                c+=increment;next_c=c/(1+coefficient*a*c*dt/(2 if mode==0 else 1))
                if mode==0:reference_heat+=(c*c-next_c*next_c)/(2*a)
                c=next_c
            reference_outputs.append(c)
        for actual,wanted in zip((symmetric,post),reference_outputs):
            close(actual,wanted,abs(c0)+abs(a*force*duration),'Exact subflow composition replay',8*n)
        close(q,reference_heat,reference_heat,'Exact symmetric subflow heat',16*n)
        close(work,reference_work,reference_work,'Exact pressure-kick work',16*n)
        check(expected>0 and (previous is None or expected<D('.3')*previous),'Second-order coupling refinement')
        previous=expected
        terms=[symmetric*symmetric/(2*a),-c0*c0/(2*a),q,-work]
        close(sum(terms,F(0)),F(0),sum(map(abs,terms),F(0)),'Coupled kinetic/heat/force work')
        if n==32:check(expected<D('1e-4') and expected<D('.1')*expected_post,'Symmetric coupling fine accuracy')


def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=524288,'Owner saved pipe bound')
    with localcontext() as ctx:
        ctx.prec=80;data=parse(raw);review(data);checks=base.CHECKS;rejected=0
        for kind in range(3):
            altered=copy.deepcopy(data)
            if kind<2:
                r=list(altered[3][0]);r[11 if kind==0 else 14]+=F(1,16);altered[3][0]=tuple(r)
            else:
                key=next(k for k in altered[1]['masses'] if k[1]==0);altered[1]['masses'][key]*=F(17,16)
            try:
                review(altered)
            except ValueError:
                rejected+=1
        check(rejected==3,'Owner corruption escaped')
    print(json.dumps(dict(Passed=True,SavedSha256=hashlib.sha256(raw).hexdigest(),SavedChecks=checks,CorruptionsRejected=rejected,
        NativeProcessLaunched=False,IndependentAgentReview=False,Scope='Three atomic symmetric compression/pressure/transport owners and manufactured temporal refinement; no strong-flow, production, game or performance acceptance')))


if __name__=='__main__':
    main()
