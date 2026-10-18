"""Independent saved-operand review of the one-dimensional nodal conversion."""
import copy
from decimal import Decimal, localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec=importlib.util.spec_from_file_location('nodal_closed_saved',Path(__file__).with_name('review_mac_closed_world.py'))
closed=importlib.util.module_from_spec(spec);spec.loader.exec_module(closed)
base=closed.base
check,close,number,unique=base.check,base.close,base.number,base.unique
EPS=base.EPS


def full(row,start,end):
    return tuple(base.scaled_energy(row[i],row[i+1]) for i in range(start,end,2))


def parse(raw):
    cases,materials,sweeps,cells,faces,events,works,fluxes={}, {}, {}, {}, {}, {}, {}, {}
    passes=0
    for line in raw.decode('ascii').splitlines():
        row=line.split();check(bool(row),'Empty nodal record');kind=row[0]
        if kind=='NCASE':
            check(len(row)==12,'Nodal case shape')
            unique(cases,int(row[1]),(*map(int,row[2:6]),number(row[6]),int(row[7]),*full(row,8,12)))
        elif kind=='MATERIAL':
            check(len(row)==8,'Nodal material shape');unique(materials,int(row[1]),(int(row[2]),*map(number,row[3:])))
        elif kind=='NSWEEP':
            check(len(row)==4,'Nodal sweep shape');unique(sweeps,tuple(map(int,row[1:3])),number(row[3]))
        elif kind=='NCELL':
            check(len(row)==10,'Nodal cell shape');unique(cells,tuple(map(int,row[1:4])),tuple(map(number,row[4:])))
        elif kind=='NFACE':
            check(len(row)==28,'Nodal face shape');unique(faces,tuple(map(int,row[1:4])),(*map(number,row[4:6]),*full(row,6,28)))
        elif kind=='NCONV':
            check(len(row)==14,'Nodal conversion shape');unique(events,tuple(map(int,row[1:4])),(*map(int,row[4:6]),*full(row,6,14)))
        elif kind=='NWORK':
            check(len(row)==7,'Nodal material work shape');unique(works,tuple(map(int,row[1:3])),full(row,3,7))
        elif kind=='NPRIMAL':
            check(len(row)==6,'Nodal primal flux shape');unique(fluxes,tuple(map(int,row[1:4])),full(row,4,6)[0])
        else:
            check(kind=='PASS','Unknown nodal record: '+kind);passes+=1
    check(passes==3 and len(materials)==4 and len(cases)==16,'Nodal test/material coverage')
    expected={(axis,curve,direction,exponent) for axis in (0,1) for curve in (0,1) for direction in (-1,1) for exponent in (0,-1200)}
    check({value[:4] for value in cases.values()}==expected,'Nodal axis/deformation/sign/range coverage')
    check(set(sweeps)=={(i,f) for i in cases for f in range(11)},'Nodal sweep coverage')
    check(set(cells)=={(i,e,c) for i in cases for e in (0,1) for c in range(20)},'Nodal cell coverage')
    check(set(works)=={(i,c) for i in cases for c in range(20)},'Nodal material work coverage')
    check(set(fluxes)==set(faces),'Nodal primal flux coverage')
    return cases,materials,sweeps,cells,faces,events,works,fluxes


def review(data):
    cases,materials,sweeps,cells,faces,events,works,fluxes=data
    nitrogen=next(i for i,m in materials.items() if F(28,1000)<m[1]<F(29,1000))
    helium=next(i for i,m in materials.items() if m[0]==0 and m[1]<F(5,1000))
    expected_faces=set();expected_events=set()
    for identity,values in cases.items():
        axis,curve,direction,exponent,theta,backtracks,residual,bound=values
        check(theta==1 and backtracks==0,'Admissible nodal control was restricted')
        q=lambda f:F(0) if f in (0,10) else direction*(F(f-1,64)+curve*F((f-1)**2,512))
        scale=F(2)**exponent
        for f in range(11):check(sweeps[identity,f]==q(f),'Changed nodal scientific sweep')
        width,height=(10,2) if axis==0 else (2,10)
        corrections={};work={}
        for component in (0,1):
            nx=width+(component==0 and axis==0)
            ny=height+(component==1 and axis==1)
            expected_faces.update((identity,component,f) for f in range(nx*ny))
        for other in range(2):
            face=lambda f:(f+11*other) if axis==0 else other+2*f
            cell=lambda x:x+10*other if axis==0 else other+2*x
            for x in range(1,9):
                dv=q(x+1)-q(x)
                if not dv:continue
                key=(identity,axis,cell(x));expected_events.add(key)
                check(key in events,'Missing nodal conversion')
                left,right,saved_dv,coefficient,request,impulse=events[key]
                check((left,right)==(face(x),face(x+1)),'Nodal conversion crossed the wrong cell')
                volume=1+dv
                close(saved_dv,dv,abs(dv),'Nodal deformation lost')
                expected_coefficient=dv/volume*(1+1/volume)/8
                close(coefficient,expected_coefficient,abs(coefficient)+abs(expected_coefficient),'Nodal geometric coefficient')
                lm,rm=faces[identity,axis,left],faces[identity,axis,right]
                change=rm[2]/rm[0]-lm[2]/lm[0]
                expected=coefficient*cells[identity,0,cell(x)][2]*change
                close(request,expected,abs(request)+abs(expected),'Conservative nodal pair impulse')
                check(impulse==request,'Changed nodal correction fraction')
                corrections[left]=corrections.get(left,F(0))+impulse
                corrections[right]=corrections.get(right,F(0))-impulse
            for f in range(11):
                row=faces[identity,axis,face(f)];i0,i1,original,converted,final,source_work=row[:6]
                expected_i0=F(1,2)*(F(0) if f==0 else 1+q(f)-q(f-1))+F(1,2)*(F(0) if f==10 else 1+q(f+1)-q(f))
                check(i0==expected_i0,'Changed nodal inertia')
                expected_original=F(0) if f in (0,10) else scale*i0*(F(1,4)+(F(f)+q(f))/8)
                close(original,expected_original,abs(original)+abs(expected_original),'Point-source canonical momentum')
                delta=corrections.get(face(f),F(0))
                close(converted,original+delta,abs(original)+abs(delta)+abs(converted),'Nodal pair gathering lost momentum')
                expected_work=-(original/i0*delta+delta*delta/(2*i0))
                close(source_work,expected_work,abs(source_work)+abs(original/i0*delta)+delta*delta/(2*i0),'Source conversion lost compatible work')
                if f in (4,5,6):
                    expected_final=scale*i1*(F(1,4)+F(f,8))
                    close(final,expected_final,abs(final)+abs(expected_final),'Actual curved point-velocity oracle')
                    if exponent==-1200:check(0<abs(final)<F(1,2**1075),'Below-range canonical field was narrowed')
        energy=[];momentum=[[],[]];carrier=[];source_kinetic=[]
        source_work_terms=[[] for _ in range(20)];target_work_terms=[[] for _ in range(20)]
        def adjacent(component,face):
            if component==0:
                nx=width+(axis==0);x,y=face%nx,face//nx
                return ((x-1)%width+y*width if axis==1 or x>0 else None,x+y*width if x<width else None)
            x,y=face%width,face//width
            return (x+((y-1)%height)*width if axis==0 or y>0 else None,x+y*width if y<height else None)
        for key,row in faces.items():
            if key[0]!=identity:continue
            component=key[1]
            i0,i1,original,converted,final,source_work,old_k,new_k,transported,mixing,wall,remap,reaction=row
            k0=original*original/(2*i0);k1=final*final/(2*i1)
            close(old_k,k0,abs(old_k)+abs(k0),'Original canonical kinetic energy')
            close(new_k,k1,abs(new_k)+abs(k1),'Final canonical kinetic energy')
            conversion=[converted*converted/(2*i0),-k0,source_work]
            close(sum(conversion,F(0)),F(0),sum(map(abs,conversion),F(0)),'Actual source J/K conversion account')
            terms=[transported,-new_k,-mixing,-wall,-remap]
            close(sum(terms,F(0)),F(0),sum(map(abs,terms),F(0)),'Full-range transported K and signed work account')
            check(transported>=0 and mixing>=0 and wall>=0,'Negative carried kinetic or variance/wall heat')
            momentum[component].extend((final,-original,-reaction))
            energy.extend((k1,-k0));carrier.append(transported);source_kinetic.append(old_k)
            for c in adjacent(component,key[2]):
                if c is not None:source_work_terms[c].append(source_work*cells[identity,0,c][2]/(2*i0))
        for terms in momentum:close(sum(terms,F(0)),F(0),sum(map(abs,terms),F(0)),'Global canonical momentum and wall reaction')
        source_work_values=[]
        for c,terms in enumerate(source_work_terms):
            actual=works[identity,c][0];source_work_values.append(actual)
            close(actual,sum(terms,F(0)),abs(actual)+sum(map(abs,terms),F(0)),'Nodal source work gathering into material')
        remaining=[cells[identity,0,c][2] for c in range(20)]
        for key,flux in fluxes.items():
            if key[0]!=identity or flux==0:continue
            left,right=adjacent(key[1],key[2]);check(left is not None and right is not None,'Nodal mass crossed a wall')
            donor,target=(left,right) if flux>0 else (right,left);amount=abs(flux)
            remaining[donor]-=amount
            target_work_terms[target].append(source_work_values[donor]*amount/cells[identity,0,donor][2])
        for c in range(20):
            check(remaining[c]>=0,'Nodal material work has negative retained mass')
            target_work_terms[c].append(source_work_values[c]*remaining[c]/cells[identity,0,c][2])
            actual=works[identity,c][1];terms=target_work_terms[c]
            close(actual,sum(terms,F(0)),abs(actual)+sum(map(abs,terms),F(0)),'Nodal work did not follow the actual material mass flux')
        target_work_values=[works[identity,c][1] for c in range(20)]
        close(sum(target_work_values,F(0)),sum(source_work_values,F(0)),sum(map(abs,target_work_values+source_work_values),F(0)),'Nodal material work conservation')
        terms=carrier+source_work_values+[-x for x in source_kinetic]
        close(sum(terms,F(0)),F(0),sum(map(abs,terms),F(0)),'Original kinetic energy and conversion-work carrier conservation')
        entropy=[Decimal(0),Decimal(0)];entropy_budget=Decimal(0)
        for endpoint in (0,1):
            for c in range(20):
                volume,u,n,h,p,t=cells[identity,endpoint,c]
                check(min(n,h,p,t)>0,'Nodal conversion lost inventory or a trace')
                if endpoint==0:
                    coordinate=c%10 if axis==0 else c//2
                    check(volume==1+q(coordinate+1)-q(coordinate) and n==volume,'Changed source geometry/inventory')
                else:check(volume==1,'Changed target geometry')
                eos=closed.equilibrium({nitrogen:n,helium:h},materials,u,volume)
                entropy[endpoint]+=eos[3]
                entropy_budget+=closed.dec(128*EPS*abs(u))/eos[1]
                energy.append(u if endpoint else -u)
        check(entropy[1]-entropy[0]>=-entropy_budget,'Independent completed-state entropy admission')
        for field in (2,3,4,5):
            terms=[(1 if e else -1)*cells[identity,e,c][field] for e in (0,1) for c in range(20)]
            close(sum(terms,F(0)),F(0),sum(map(abs,terms),F(0)),'Separate constituent/passive inventory conservation')
        exact=sum(energy,F(0));magnitude=sum(map(abs,energy),F(0))
        check(abs(exact)<=bound,'Actual original-U/K endpoint conservation')
        close(residual,exact,magnitude,'Saved total energy residual',2)
        close(bound,128*EPS*magnitude,128*EPS*magnitude,'Saved original-operand energy bound',1024)
    check(set(faces)==expected_faces and set(events)==expected_events,'Extra/missing nodal face or pair record')


def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=2097152,'Nodal saved pipe exceeds 2 MiB')
    with localcontext() as context:
        context.prec=100;data=parse(raw);review(data);checks=base.CHECKS
        for corruption in ('impulse','source-work','momentum','trace','remap-work','material-work'):
            bad=copy.deepcopy(data)
            if corruption=='impulse':
                key=next(k for k,v in bad[5].items() if v[-1]);row=list(bad[5][key]);row[-1]=F(0);bad[5][key]=tuple(row)
            elif corruption=='trace':
                key=(0,1,4);row=list(bad[3][key]);row[3]/=2;bad[3][key]=tuple(row)
            elif corruption=='material-work':
                key=next(k for k,v in bad[6].items() if k[0]%2==1 and v[1]!=0)
                row=list(bad[6][key]);row[1]=F(0);bad[6][key]=tuple(row)
            else:
                index={'source-work':5,'momentum':4,'remap-work':11}[corruption]
                key=next(k for k,v in bad[4].items() if k[0]%2==1 and v[index]!=0)
                row=list(bad[4][key]);row[index]=F(0);bad[4][key]=tuple(row)
            rejected=False
            try:review(bad)
            except ValueError:rejected=True
            check(rejected,'Altered nodal evidence accepted: '+corruption)
    print(json.dumps(dict(Passed=True,Cases=16,SavedChecks=checks,CorruptionsRejected=6,
        SavedSha256=hashlib.sha256(raw).hexdigest(),NativeProcessLaunched=False,IndependentAgentReview=False,
        Scope='One-dimensional nodal momentum/kinetic conversion and original curved point-velocity oracle; full-range inventories, work and EOS entropy. No two-dimensional, strong-flow or production acceptance.')))


if __name__=='__main__':main()
