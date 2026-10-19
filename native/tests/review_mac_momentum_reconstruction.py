"""Exact saved-operand review of geometric MAC momentum/work integration."""
import copy
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec=importlib.util.spec_from_file_location('pair_reference',Path(__file__).with_name('review_mac_momentum_limiter.py'))
ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
EPS=ref.EPS;check=ref.check
def number(token):
    if '~' not in token:return ref.number(token)
    fields=token.split('~');check(len(fields)==3,'Primary-U token shape')
    high=ref.number(fields[0]);mantissa=ref.number(fields[1]);exponent=int(fields[2])
    check(-16384<exponent<16384 and F(1,2)<=abs(mantissa)<1,'Primary-U correction shape')
    low=mantissa*F(2)**exponent
    check(F(float(high+low))==high,'Noncanonical primary-U pair')
    return high+low
def encoded(row,index):
    m=number(row[index]);e=int(row[index+1])
    check(-16384<e<16384 and ((m==0 and e==0) or F(1,2)<=abs(m)<1),'Scaled field shape/range')
    return m*F(2)**e
CHECKS=0
QUADRATIC=False
_quadratic_reference=None
def close(a,b,operands,why):
    global CHECKS;CHECKS+=1
    check(abs(a-b)<=128*EPS*sum(map(abs,operands)),why)
def rounded(x):
    if not x:return F(0)
    e=abs(x.numerator).bit_length()-x.denominator.bit_length()
    if abs(x)<F(2)**e:e-=1
    return F(float(x/F(2)**(e+1)))*F(2)**(e+1)
def product(a,b):return rounded(a*b)
def unique(d,k,v):check(k not in d,'Duplicate saved record');d[k]=v
def parse(raw):
    d={k:{} for k in ('grids','cells','masses','passives','faces','duals','events','reports','profiles','waves')};passes=[];trace=None;rejected=None;qmode=None
    for line in raw.decode('ascii').splitlines():
        r=line.split();check(bool(r),'Empty record');kind=r[0]
        if kind=='QMODE':
            check(len(r)==2 and qmode is None and r[1]=='2','Quadratic mode shape/duplicate');qmode=2
        elif kind=='GRID':
            check(len(r)==13,'Grid shape');unique(d['grids'],int(r[1]),(int(r[2]),int(r[3]),number(r[4]),number(r[5]),int(r[6]),int(r[7]),int(r[8]),encoded(r,9),encoded(r,11)))
        elif kind=='CELL':
            check(len(r)==12,'Cell shape');unique(d['cells'],tuple(map(int,r[1:3])),(int(r[3]),*map(number,r[4:])))
        elif kind in ('MASS','PASSIVE'):
            check(len(r)==6,'Material record shape');unique(d['masses' if kind=='MASS' else 'passives'],tuple(map(int,r[1:5])),number(r[5]))
        elif kind=='FACE':
            check(len(r)==41,'Face shape');unique(d['faces'],tuple(map(int,r[1:4])),(number(r[4]),number(r[5]),int(r[6]),*[encoded(r,i) for i in range(7,41,2)]))
        elif kind=='DUAL':
            check(len(r)==8,'Dual shape');unique(d['duals'],tuple(map(int,r[1:4])),(int(r[4]),int(r[5]),encoded(r,6)))
        elif kind=='CORRECTION':
            check(len(r)==41,'Correction shape');unique(d['events'],tuple(map(int,r[1:4])),(*map(int,r[4:9]),*[encoded(r,i) for i in range(9,41,2)]))
        elif kind=='REPORT':
            check(len(r)==7,'Report shape');unique(d['reports'],tuple(map(int,r[1:3])),tuple(map(int,r[3:])))
        elif kind=='PROFILE':
            check(len(r)==6,'Profile shape');unique(d['profiles'],tuple(map(int,r[1:4])),tuple(map(number,r[4:])))
        elif kind=='WAVE':
            check(len(r)==10,'Wave shape');unique(d['waves'],tuple(map(int,r[1:4])),(number(r[4]),int(r[5]),encoded(r,6),encoded(r,8)))
        elif kind=='TRACE':check(len(r)==3 and trace is None,'Trace shape');trace=tuple(map(int,r[1:]))
        elif kind=='REJECTED':check(len(r)==2 and rejected is None,'Rejection shape');rejected=int(r[1])
        elif kind=='PASS':passes.append(line)
        else:raise ValueError('Unknown record '+kind)
    check(set(d['grids'])=={0,1,2,3} and len(set(passes))==len(passes)==6 and rejected==4 and trace is not None,'Incomplete native groups/cases')
    check(set(d['cells'])=={(c,i) for c,g in d['grids'].items() for i in range(g[0]*g[1])},'Orphan/missing cell')
    check(set(d['reports'])=={(c,a) for c in d['grids'] for a in (0,1)},'Orphan/missing component report')
    for field in ('masses','passives'):
        check(all(c in d['grids'] and t in (0,1) and 0<=i<d['grids'][c][0]*d['grids'][c][1] and s>=0 for c,t,i,s in d[field]),'Orphan material identity')
    for field in ('faces','duals','events'):
        check(all(c in d['grids'] and a in (0,1) for c,a,i in d[field]),'Orphan component record')
    d['trace']=trace;d['quadratic']=qmode==2;return d

def topology(g,component):
    w,h,_,_,px,py,*_=g;nx=w if component else w+(not px);ny=h+(not py) if component else h
    def cell(x,y):
        if (not px and not 0<=x<w) or (not py and not 0<=y<h):return None
        return (x%w)+(y%h)*w
    pairs=[]
    for y in range(ny):
        for x in range(nx):pairs.append((cell(x,y-1),cell(x,y)) if component else (cell(x-1,y),cell(x,y)))
    return nx,ny,pairs
def review_pair(il,ir,p,work_budget=None):
    global _quadratic_reference
    jl,jr,ql,qr,request,alpha,impulse,al,ar,hl,hr,work,dl,dr=p
    check(ql>=0 and qr>=0 and hl>=0 and hr>=0 and 0<=alpha<=1,'Pair heat/limiter domain')
    if impulse==0:check((al,ar,hl,hr,work,dl,dr)==(jl,jr,ql,qr,0,0,0),'Zero impulse changed a paired state')
    ul,ur=jl/il,jr/ir
    # Account for rounded endpoint velocity operands when the difference is small.
    err=16*EPS*abs(request)*(abs(ul)+abs(ur))
    a=(ul-ur)*request/2;costs=(request*request/(2*il)-a,request*request/(2*ir)-a)
    if QUADRATIC and request:
        if _quadratic_reference is None:
            spec=importlib.util.spec_from_file_location('quadratic_saved_reference',Path(__file__).with_name('review_mac_quadratic_limiter.py'))
            _quadratic_reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(_quadratic_reference)
        root=_quadratic_reference.root_limit
        coefficients=(request*request/(2*il),request*request/(2*ir))
        lower=min(root(q,a-err,b*(1+8*EPS)) for q,b in zip((ql,qr),coefficients))
        upper=min(root(q,a+err,b*(1-8*EPS)) for q,b in zip((ql,qr),coefficients))
    else:
        lower=min([F(1)]+[q/(c+err) for q,c in zip((ql,qr),costs) if c+err>0 and q<c+err])
        upper=min([F(1)]+[q/(c-err) for q,c in zip((ql,qr),costs) if c-err>0 and q<c-err])
    if work_budget is not None:
        tl,tr=work_budget;check(tl>=0 and tr>=0 and tl-work>=0 and tr+work>=0,'Actual transported kinetic work capacity')
        w0=(ul+ur)*request/2
        low_cost=(max(w0+err,F(0)),max(-w0+err,F(0)))
        high_cost=(max(w0-err,F(0)),max(-w0-err,F(0)))
        lower=min([lower]+[t/c for t,c in zip((tl,tr),low_cost) if c>0 and t<c])
        upper=min([upper]+[t/c for t,c in zip((tl,tr),high_cost) if c>0 and t<c])
    check(alpha>=lower*(1-128*EPS) and alpha<=upper*(1+128*EPS),'Independent heat/work limiter interval')
    close(impulse,request*alpha,(impulse,request*alpha),'Applied correction')
    close(al,jl-impulse,(al,jl,impulse),'Left corrected J');close(ar,jr+impulse,(ar,jr,impulse),'Right corrected J')
    close(work,(ul+ur)*impulse/2,(work,(abs(ul)+abs(ur))*impulse),'Paired work flux')
    for inertia,before,after,q,h,delta,sgn in ((il,jl,al,ql,hl,dl,1),(ir,jr,ar,qr,hr,dr,-1)):
        a=(ul-ur)*impulse/2;b=impulse*impulse/(2*inertia)
        close(delta,a-b,(delta,a,b,(abs(ul)+abs(ur))*impulse),'Pending remainder change')
        close(h,q+delta,(h,q,delta),'Remainder storage')
        ko,kn=ref.kinetic(inertia,before),ref.kinetic(inertia,after)
        close(kn-ko+h-q+sgn*work,0,(kn,ko,h,q,work),'Local K/remainder/work identity')

def review_components(d,case,m0,m1,cells,require_uniform=False):
    g=d['grids'][case];w,h,_,_,px,py,*_=g
    heat={i:F(0) for i in cells};energy_terms=[];energy_change=F(0)
    primal=[]
    for component in (0,1):
        nx,ny,pairs=topology(g,component);rows={i:d['faces'][case,component,i] for i in range(nx*ny)}
        active=lambda i:pairs[i][0] is not None and pairs[i][1] is not None and not cells[pairs[i][0]][0] and not cells[pairs[i][1]][0]
        for i,(left,right) in enumerate(pairs):
            i0,i1,is_fixed,*f=rows[i];check(is_fixed==int(not active(i)),'Fixed face topology')
            for mass,source in ((i0,m0),(i1,m1)):
                expected=sum(source[k]/2 for k in (left,right) if k is not None and not cells[k][0])
                close(mass,expected,(mass,expected),'Derived half-cell inertia')
            if not active(i):check(f[0]==f[1]==f[2]==0,'Wall/exterior flux or canonical J')
            else:primal.append((left,right,f[0]))
        duals={k:v for (c,a,k),v in d['duals'].items() if c==case and a==component}
        expected=[]
        face=lambda x,y:x%nx+(y%ny)*nx
        # Independently enumerate dual cell centers and transverse faces.
        for y in range(h):
            for x in range(w):
                if cells[x+y*w][0]:continue
                l,r=(face(x,y),face(x,y+1)) if component else (face(x,y),face(x+1,y))
                expected.append((l,r,rounded((rows[l][3]+rows[r][3])/2)))
        other={i:d['faces'][case,1-component,i][3] for i in range((w+(not px))*h if component else w*(h+(not py)))}
        if component==0:
            for y in range(0 if py else 1,h):
                for x in range(nx):
                    a=other[(x+w-1)%w+y*w] if x>0 or px else 0;b=other[x+y*w] if x<w else 0
                    expected.append((face(x,y-1),face(x,y),rounded((a+b)/2)))
        else:
            for x in range(0 if px else 1,w):
                for y in range(ny):
                    a=other[x+((y+h-1)%h)*(w+(not px))] if y>0 or py else 0;b=other[x+y*(w+(not px))] if y<h else 0
                    expected.append((face(x-1,y),face(x,y),rounded((a+b)/2)))
        check(set(duals)==set(range(len(expected))),'Dual flux count')
        for k,(l,r,f) in sorted(duals.items()):
            el,er,ef=expected[k];check((l,r)==(el,er),'Dual geometry');close(f,ef,(f,ef),'Dual compatible mass flux')
        oldv={i:rounded(row[4]/row[0]) if row[0] else 0 for i,row in rows.items()}
        def neighbor(i,axis,sgn):
            x,y=i%nx,i//nx;v=x if axis==0 else y;size=nx if axis==0 else ny;periodic=px if axis==0 else py
            if not periodic and not 0<=v+sgn<size:return None
            j=face(x+sgn,y) if axis==0 else face(x,y+sgn)
            return j if active(j) else None
        center={(i,a):F(0) for i in rows for a in (0,1)};out={i:F(0) for i in rows};incoming={i:[] for i in rows}
        for l,r,f in duals.values():
            a=int(l%nx==r%nx);center[l,a]+=f/2;center[r,a]+=f/2
            if f:donor,target=(l,r) if f>0 else (r,l);out[donor]+=abs(f);incoming[target].append((donor,abs(f)))
        slopes={};courants={};current_j={};current_q={};tk={};budgets={};works={i:[] for i in rows}
        for i,row in rows.items():
            i0,i1,wall,flux,j0,j1,jup,sx,sy,cx,cy,ko,kn,qup,qnew,tup,tnew,qwall,jwall,audit_bound=row
            keep=rounded(i0-out[i]);check(keep>=0,'Dual CFL');momentum=[j0*keep/i0] if i0 else []
            kinetic=[ko*keep/i0] if i0 else []
            for donor,amount in incoming[i]:momentum.append(rows[donor][4]*amount/rows[donor][0]);kinetic.append(rows[donor][11]*amount/rows[donor][0])
            jp=sum(momentum);close(jup,0 if wall else jp,(jup,*momentum),'Upwind canonical momentum')
            close(jwall,-jp if wall else 0,(jwall,*momentum),'Fixed wall impulse')
            close(tup,sum(kinetic),(tup,*kinetic),'Actual upwind kinetic flux')
            if i0:close(ko,ref.kinetic(i0,j0),(ko,ref.kinetic(i0,j0)),'Old kinetic')
            if i1:close(kn,ref.kinetic(i1,j1),(kn,ref.kinetic(i1,j1)),'New kinetic')
            provisional=rounded((-jwall if wall else jup)/i1) if i1 else 0
            variance=[keep*rounded(oldv[i]-provisional)**2/2]
            variance += [amount*rounded(oldv[donor]-provisional)**2/2 for donor,amount in incoming[i]]
            close(qup,sum(variance),(qup,*variance),'Positive upwind variance heat')
            close(qwall,ref.kinetic(i1,-jwall) if wall and i1 else 0,(qwall,ref.kinetic(i1,-jwall) if i1 else 0),'Wall kinetic heat')
            for a,slope,courant in ((0,sx,cx),(1,sy,cy)):
                left,right=neighbor(i,a,-1),neighbor(i,a,1);wanted=F(0)
                if active(i) and left is not None and right is not None:
                    dl,dr=rounded(oldv[i]-oldv[left]),rounded(oldv[right]-oldv[i])
                    if dl*dr>0:wanted=dl if abs(dl)<=abs(dr) else dr
                check(slope==wanted,'Geometric minmod slope')
                wanted_c=rounded(rounded(center[i,a])/i0) if active(i) else 0
                check(courant==wanted_c,'Centered dual Courant');slopes[i,a]=slope;courants[i,a]=courant
            current_j[i]=jup;current_q[i]=qup;tk[i]=tup
            kup=ref.kinetic(i1,jup) if i1 else 0
            budgets[i]=128*EPS*(abs(tup)+kup+qup+qwall)
            check(abs(tup-kup-qup-qwall)<=budgets[i],'Original upwind local energy account')
        requests={}
        for k,(l,r,f) in sorted(duals.items()):
            if not f or not active(l) or not active(r):continue
            a=int(l%nx==r%nx);donor,target=(l,r) if f>0 else (r,l)
            pred=rounded((slopes[donor,a] if f>0 else -slopes[donor,a])/2-product(courants[donor,0],slopes[donor,0])/2-product(courants[donor,1],slopes[donor,1])/2)
            diff=rounded(oldv[target]-oldv[donor]);bounded=pred if pred*diff>0 and abs(pred)<=abs(diff) else diff if pred*diff>0 else 0
            request=product(f,bounded)
            if request:requests[k]=(a,pred,bounded,request)
        events=sorted((k,v) for (c,a,k),v in d['events'].items() if c==case and a==component)
        check([k for k,_ in events]==list(range(len(events))) and [v[0] for _,v in events]==list(requests),'Missing/reordered geometric correction')
        limited=unresolved_j=unresolved_q=0
        for _,event in events:
            edge,a,backoffs,uj,uq,pred,bounded,*p=event;l,r,_=duals[edge]
            check((a,pred,bounded,p[4])==requests[edge],'Geometric predictor or bounded request')
            check(p[:4]==[current_j[l],current_j[r],current_q[l],current_q[r]],'Correction chain source state')
            check(0<=backoffs<=64 and 0<=uj<=2 and 0<=uq<=2,'Correction diagnostics');review_pair(rows[l][1],rows[r][1],p,work_budget=(tk[l],tk[r]))
            current_j[l],current_j[r]=p[7:9];current_q[l],current_q[r]=p[9:11];tk[l]-=p[11];tk[r]+=p[11]
            if p[6]:
                for cell,inertia,before,after,qb,qa,flow in ((l,rows[l][1],p[0],p[7],p[2],p[9],-p[11]),(r,rows[r][1],p[1],p[8],p[3],p[10],p[11])):
                    budgets[cell]+=128*EPS*(ref.kinetic(inertia,after)+ref.kinetic(inertia,before)+qa+qb+abs(flow));works[cell].append(flow)
            limited+=p[5]<1;unresolved_j+=uj;unresolved_q+=uq
        check(d['reports'][case,component]==(len(events),limited,unresolved_j,unresolved_q),'Correction report totals')
        if require_uniform:check(not events,'Uniform velocity activated correction')
        for i,row in rows.items():
            i0,i1,wall,flux,j0,j1,jup,sx,sy,cx,cy,ko,kn,qup,qnew,tup,tnew,qwall,jwall,audit_bound=row
            check(j1==current_j[i] and qnew==current_q[i] and qnew>=0,'Final corrected canonical J/remainder')
            close(tnew,tk[i],(tnew,tk[i]),'Accumulated paired work in transported kinetic energy')
            composed=budgets[i]
            if works[i]:
                storage_bound=128*EPS*(abs(tnew)+abs(tup)+sum(map(abs,works[i])))
                check(abs(tnew-tup-sum(works[i]))<=storage_bound,'Actual transported work storage account')
                composed+=storage_bound
            close(audit_bound,composed,(audit_bound,composed),'Composed actual stage operand bound')
            check(abs(tnew-kn-qnew-qwall)<=audit_bound,'Composed local K/heat/work account')
            energy_change+=kn-ko;energy_terms.extend((kn,ko))
            if i1:
                for cell in pairs[i]:
                    if cell is not None and not cells[cell][0]:heat[cell]+=(qnew+qwall)*m1[cell]/(2*i1)
        close(sum(row[5]-row[4]-row[18] for row in rows.values()),0,[x for row in rows.values() for x in (row[5],row[4],row[18])],'Component momentum plus wall impulse')
    return heat,energy_change,energy_terms,primal

def review(d):
    global QUADRATIC
    QUADRATIC=d['quadratic']
    global CHECKS;CHECKS=0
    for case,g in d['grids'].items():
        w,h,spacing,depth,px,py,unresolved,residual,bound=g
        check(spacing>0 and depth>0 and px in (0,1) and py in (0,1) and abs(residual)<=bound,'Grid domain/account')
        cells={i:d['cells'][case,i] for i in range(w*h)}
        m0={i:v[4] for i,v in cells.items()};m1={i:v[5] for i,v in cells.items()}
        for field in ('masses','passives'):
            src={(i,s):v for (c,t,i,s),v in d[field].items() if c==case and t==0}
            dst={(i,s):v for (c,t,i,s),v in d[field].items() if c==case and t==1}
            check(set(src)==set(dst) and all(v>0 for v in (*src.values(),*dst.values())),'Constituent/passive positivity or identity loss')
            for s in {k[1] for k in src}:
                a=sum(v for (i,k),v in src.items() if k==s);b=sum(v for (i,k),v in dst.items() if k==s)
                close(a,b,(a,b),'Global constituent/passive account')
        for i,v in cells.items():
            check(v[0] in (0,1) and (v[0] or (all(t>0 for t in v[6:9]) and m0[i]>0 and m1[i]>0)),'Cell occupancy/EOS temperature')
            if v[0]:check(all(x==0 for x in v[1:]),'Solid contains material U/mass')
            for t,mass in ((0,m0[i]),(1,m1[i])):
                values=[x for (c,e,k,s),x in d['masses'].items() if c==case and e==t and k==i]
                close(mass,sum(values),(mass,*values),'Cell mass is not constituent sum')
            if not v[0]:check((case,1,i,d['trace'][0]) in d['masses'] and (case,1,i,d['trace'][1]) in d['passives'],'Missing positive trace')
        heat,energy_change,energy_terms,primal=review_components(d,case,m0,m1,cells,require_uniform=(case==0))
        outgoing={i:F(0) for i in cells};incoming={i:[] for i in cells}
        for left,right,f in primal:
            if f:donor,target=(left,right) if f>0 else (right,left);outgoing[donor]+=abs(f);incoming[target].append((donor,abs(f)))
        definitely_unresolved=possibly_unresolved=0
        for i,v in cells.items():
            if v[0]:continue
            difference=abs(v[3]-v[2]-heat[i])
            if heat[i]:
                definitely_unresolved+=difference>192*EPS*heat[i]
                possibly_unresolved+=difference>64*EPS*heat[i]
            else:check(v[3]==v[2],'Zero pending heat changed material U')
            keep=m0[i]-outgoing[i];check(keep>=0,'Primal CFL')
            expected=v[1]*keep/m0[i]+sum(cells[k][1]*amount/m0[k] for k,amount in incoming[i])
            close(v[2],expected,(v[2],expected),'Advected material U before correction heat')
            close(v[3],v[2]+heat[i],(v[3],v[2],heat[i]),'Actual material heat deposition')
            for field in ('masses','passives'):
                for (c,t,k,s),old in d[field].items():
                    if (c,t,k)!=(case,0,i):continue
                    expected=old*keep/m0[i]+sum(d[field][case,0,k,s]*amount/m0[k] for k,amount in incoming[i])
                    actual=d[field][case,1,i,s];close(actual,expected,(actual,expected),'Individual trace/species/passive transport')
            energy_change+=v[3]-v[1];energy_terms.extend((v[3],v[1]))
        check(definitely_unresolved<=unresolved<=possibly_unresolved,'Material heat precision diagnostics')
        if case==3:
            carried=any(v[3]!=F(float(v[3])) for v in cells.values())
            check((unresolved==0 if carried else unresolved>0) and d['reports'][case,0][0]>0,'Missing full-range correction/storage control')
        close(energy_change,0,energy_terms,'Closed actual material U plus canonical K')
        close(bound,128*EPS*sum(map(abs,energy_terms)),(bound,128*EPS*sum(map(abs,energy_terms))),'Original endpoint energy operand bound')
        close(residual,energy_change,energy_terms,'Reported actual energy residual')
    expected_waves={(0,n,1) for n in (32,64,128)}|{(1,n,1) for n in (8,16,32)}|{(0,128,0),(1,32,0)}
    check(set(d['waves'])==expected_waves,'Missing wave grids/donor controls')
    errors={}
    for axis,grids,ratio,donor_ratio in ((0,(32,64,128),F(35,100),F(3,10)),(1,(8,16,32),F(45,100),F(4,10))):
        for n in grids:
            rows=[d['profiles'][axis,n,i] for i in range(n)];expected=sum(abs(b-a) for a,b in rows)/n
            actual,events,residual,bound=d['waves'][axis,n,1];check(events>0 and abs(residual)<=bound,'Wave correction/energy gate')
            close(actual,expected,(actual,expected),'Independent one-period wave norm');errors[axis,n]=expected
        check(errors[axis,grids[1]]<ratio*errors[axis,grids[0]] and errors[axis,grids[2]]<ratio*errors[axis,grids[1]] and
            errors[axis,grids[2]]<donor_ratio*d['waves'][axis,grids[2],0][0],'Unchanged normal/transverse refinement criteria')
    return dict(Passed=True,Checks=CHECKS,Groups=6,NormalRatios=[float(errors[0,64]/errors[0,32]),float(errors[0,128]/errors[0,64])],
        TransverseRatios=[float(errors[1,16]/errors[1,8]),float(errors[1,32]/errors[1,16])])

def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=1048576,'Saved integration pipe cap');data=parse(raw);result=review(data)
    mutations=[]
    for kind in ('geometry','work','trace'):
        bad=copy.deepcopy(data)
        if kind in ('geometry','work'):
            key=next(k for k in bad['events'] if k[0]==1);v=list(bad['events'][key]);v[5 if kind=='geometry' else 18]*=F(101,100);bad['events'][key]=tuple(v)
        else:
            key=next(k for k in bad['masses'] if k[0]==1 and k[1]==1 and k[3]==bad['trace'][0]);bad['masses'][key]*=F(101,100)
        try:review(bad)
        except (ValueError,KeyError):mutations.append(kind)
        else:raise ValueError('Accepted corrupted '+kind)
    result.update(InputSha256=hashlib.sha256(raw).hexdigest(),CorruptionsRejected=mutations,IndependentAgentReview=False,
        Scope='Geometric MAC transport only; no closed pressure owner, full Euler, game or performance acceptance')
    with Path(sys.argv[2]).open('x') as f:json.dump(result,f,indent=2)
    print(json.dumps(result))
if __name__=='__main__':main()
