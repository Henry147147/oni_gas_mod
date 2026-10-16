"""Saved-only thermal relaxation review; exact accounts and 80-digit EOS."""
import copy
import functools
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec=importlib.util.spec_from_file_location('heat_eos',Path(__file__).with_name('review_mac_closed_world.py'))
ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
base=ref.base;check,close,number,unique=base.check,base.close,base.number,base.unique
EPS=base.EPS;dec=ref.dec


def parse(raw,required_passes=8,required_grids=tuple(range(7)),controls=True):
    data={k:{} for k in ('materials','pairs','certs','pmasses','grids','events','caps','cells','masses','passives','faces','walls','network')}
    passes=[];rejected=None;brackets=set()
    for line in raw.decode('ascii').splitlines():
        r=line.split();check(bool(r),'Empty heat record');kind=r[0]
        if kind=='MATERIAL':
            check(len(r)==8,'Material shape');unique(data['materials'],int(r[1]),(int(r[2]),*map(number,r[3:])))
        elif kind=='HPAIR':
            check(len(r)==32,'Heat pair shape')
            values=(*map(number,r[2:4]),*map(int,r[4:6]),*map(number,r[6:16]),*[base.scaled_energy(r[i],r[i+1]) for i in range(16,30,2)],*map(number,r[30:32]))
            unique(data['pairs'],int(r[1]),values)
        elif kind=='HPMASS':
            check(len(r)==5,'Pair mass shape');unique(data['pmasses'],tuple(map(int,r[1:4])),number(r[4]))
        elif kind=='HEQCERT':
            check(len(r)==9,'Thermal-root certificate shape')
            unique(data['certs'],int(r[1]),(int(r[2]),number(r[3]),number(r[4]),base.scaled_energy(r[5],r[6]),base.scaled_energy(r[7],r[8])))
        elif kind=='HBRACKET_START':
            check(len(r)==2 and int(r[1]) not in brackets,'Bracket fixture marker');brackets.add(int(r[1]))
        elif kind=='HGRID':
            check(len(r)==19,'Heat grid shape')
            unique(data['grids'],int(r[1]),(*map(int,r[2:4]),*map(number,r[4:6]),*map(int,r[6:8]),number(r[8]),*map(int,r[9:11]),*[base.scaled_energy(r[i],r[i+1]) for i in range(11,17,2)],*map(number,r[17:19])))
        elif kind=='HEVENT':
            check(len(r)==11,'Heat event shape');unique(data['events'],tuple(map(int,r[1:3])),(*map(int,r[3:9]),base.scaled_energy(r[9],r[10])))
        elif kind=='HCAP':
            check(len(r)==4,'Frozen capacity shape');unique(data['caps'],tuple(map(int,r[1:3])),number(r[3]))
        elif kind=='SOLID':
            check(len(r)==4 and r[3] in ('0','1'),'Wall shape');unique(data['walls'],tuple(map(int,r[1:3])),int(r[3]))
        elif kind=='SCELL':
            check(len(r)==8,'State shape');unique(data['cells'],tuple(map(int,r[1:4])),tuple(map(number,r[4:])))
        elif kind in ('SMASS','SPASSIVE'):
            check(len(r)==6,'Inventory shape');unique(data['masses' if kind=='SMASS' else 'passives'],tuple(map(int,r[1:5])),number(r[5]))
        elif kind=='SFACE':
            check(len(r)==9 and r[6] in ('0','1'),'Face shape');unique(data['faces'],tuple(map(int,r[1:5])),(number(r[5]),int(r[6]),base.scaled_energy(r[7],r[8])))
        elif kind=='HNETWORK':
            check(len(r)==7,'Network shape');unique(data['network'],int(r[1]),tuple(map(number,r[2:])))
        elif kind=='HREJECTED':
            check(len(r)==2 and rejected is None,'Rejection shape');rejected=int(r[1])
        else:
            check(kind=='PASS','Unknown heat record');passes.append(line)
    check(len(set(passes))==len(passes)==required_passes and rejected==(8 if controls else None),'Missing heat groups/rejections')
    check(set(data['grids'])==set(required_grids) and set(data['network'])==({1,2,4,8,16} if controls else set()),'Missing grid/network controls')
    check(len(data['materials'])==4 and brackets==({11,12,13} if controls else set()),'Missing materials/brackets')
    if controls:check(set(range(14)).issubset(data['pairs']),'Missing analytic pairs')
    check(data['certs'].keys()==data['pairs'].keys(),'Missing root certificates')
    return data


def eos(masses,materials,u,v):
    return cached_eos(tuple(sorted(masses.items())),tuple(sorted(materials.items())),u,v)


@functools.lru_cache(maxsize=None)
def cached_eos(mass_items,material_items,u,v):
    masses=dict(mass_items);materials=dict(material_items)
    if all(materials[i][0]==1 for i in masses):
        check(len(masses)==1,'Pure-liquid oracle expects the explicit water surrogate')
        i,m=next(iter(masses.items()));_,_,_,_,rho,bulk=materials[i]
        p=dec(bulk)*(dec(m)/dec(rho)/dec(v)-1);check(p>0,'Unoccupied pure-liquid branch')
        _,elastic,capacity,offset,_=ref.eos_values(masses,materials,p)
        t=(dec(u)-offset-elastic)/capacity;s=capacity*(t/dec(F(298.15))).ln()
        return p,t,s,capacity
    p,t,vg,s=ref.equilibrium(masses,materials,u,v)
    _,_,capacity,_,_=ref.eos_values(masses,materials,p)
    compliance=D(0)
    for i,m in masses.items():
        phase,_,_,_,rho,bulk=materials[i]
        if phase:compliance+=dec(m)/dec(rho)/dec(bulk)/(1+p/dec(bulk))**2
    if compliance:capacity+=p*compliance*vg/(t*(compliance+vg/p))
    return p,t,s,capacity


def near(actual,expected,why,relative=D('5e-10')):
    check(abs(dec(actual)-expected)<=relative*abs(expected),why)


def rounded_scaled(value):
    if not value:return F(0)
    exponent=abs(value.numerator).bit_length()-value.denominator.bit_length()
    check(-16384<exponent<16384,'Saved velocity exponent exceeds review domain')
    if abs(value)<F(2)**exponent:exponent-=1
    return F(float(value/(F(2)**(exponent+1))))*F(2)**(exponent+1)


def entropy_account(before,after,us0,us1,ts0,ts1,ds,sb):
    terms=[*after,*[-v for v in before]];close(ds,sum(terms,F(0)),sum(map(abs,terms),F(0)),'Stored-state entropy report')
    budget=128*EPS*sum((abs(u)/t for u,t in zip((*us0,*us1),(*ts0,*ts1))),F(0))
    close(sb,budget,budget,'Stored-U thermal entropy allowance',8)
    check(ds>=-sb,'Actual entropy reduced beyond stored-U allowance')


def review_pair(data,key):
    p=data['pairs'][key];vl,vr,it,un,*_=p
    ul,ur,nl,nr,tl,tr,ntl,ntr,cl,cr,g,x,f,eq,q,res,bound,ds,sb=p[4:]
    check(vl>0 and vr>0 and it>=0 and un in (0,1,2) and cl>0 and cr>0 and g>=0,'Invalid pair domain')
    masses=[{i:m for (k,s,i),m in data['pmasses'].items() if k==key and s==side} for side in (0,1)]
    check(all(m and min(m.values())>0 for m in masses),'Missing/zero pair constituents')
    old=[eos(m,data['materials'],u,v) for m,u,v in zip(masses,(ul,ur),(vl,vr))]
    new=[eos(m,data['materials'],u,v) for m,u,v in zip(masses,(nl,nr),(vl,vr))]
    rej,eqtl,eqtr,eqres,eqbound=data['certs'][key]
    check(0<=rej<=it and eqbound>=0,'Thermal-root trial metadata')
    check(eqres==eqtl-eqtr,'Actual root temperature residual')
    for actual,expected in zip((tl,tr,ntl,ntr,cl,cr),(old[0][1],old[1][1],new[0][1],new[1][1],old[0][3],old[1][3])):
        near(actual,expected,'Independent pair EOS/capacity')
    if g==0 or tl==tr:
        check(x==f==eq==q==res==bound==ds==sb==un==it==0 and nl==ul and nr==ur,'Inactive thermal pair changed')
        check(rej==eqtl==eqtr==eqres==eqbound==0,'Inactive root certificate changed')
        return
    close(x,g*(1/cl+1/cr),g*(1/cl+1/cr),'Thermal relaxation number',8)
    exact=dec(x) if x<F(2)**-53 else D(1)-(-dec(x)).exp() if x<64 else D(1)
    near(f,exact,'Exponential relaxation fraction',16*dec(EPS));check(0<f<=1,'Nonpositive/full-range thermal fraction')
    constant=all(len({data['materials'][i][0] for i in m})==1 for m in masses)
    if constant:
        check(it==0,'Constant capacity unnecessarily invokes a nonlinear root')
        close(eq,(tl-tr)*cl*cr/(cl+cr),abs(eq),'Constant-capacity equilibrium partition',16)
        check(rej==eqtl==eqtr==eqres==eqbound==0,'Analytic partition claims a root certificate')
    else:
        check(0<it<=128,'Mixed equilibrium root count')
        eq_u=[F(float(u+sign*eq)) for u,sign in ((ul,-1),(ur,1))]
        temps=[eos(m,data['materials'],u,v)[1] for m,u,v in zip(masses,eq_u,(vl,vr))]
        for t,e in zip((eqtl,eqtr),temps):near(t,e,'Independent thermal-root EOS')
        intrinsic=[sum((amount*data['materials'][i][2] for i,amount in m.items()),F(0)) for m in masses]
        expected_bound=128*EPS*(abs(eqtl)+abs(eqtr)+sum(((abs(u)+abs(n)+abs(eq))/c for u,n,c in zip((ul,ur),eq_u,intrinsic)),F(0)))
        close(eqbound,expected_bound,expected_bound,'Thermal-root actual storage bound',16)
        check(abs(eqres)<=eqbound,'Root exceeds certified primary storage precision')
        if eqres*(1 if tl>tr else -1)<0:
            check(abs(eqres)<=128*EPS*(abs(eqtl)+abs(eqtr)),'Storage certificate permits root temperature crossing')
    close(q,eq*f,abs(q),'Full heat transfer',8);check((q>0)==(tl>tr),'Heat flows from cold to hot')
    for after,before,request in ((nl,ul,-q),(nr,ur,q)):
        close(after-before,request,abs(after)+abs(before)+abs(request),'Actual signed U update')
    expected_un=sum(abs(after-before-request)>32*EPS*abs(request) for after,before,request in ((nl,ul,-q),(nr,ur,q)))
    check(un==expected_un,'Unresolved U update count')
    terms=(nl,nr,-ul,-ur);close(res,sum(terms,F(0)),sum(map(abs,terms),F(0)),'Actual pair U residual')
    close(bound,128*EPS*sum(map(abs,terms),F(0)),bound,'Actual pair U bound',8);check(abs(res)<=bound,'Pair U conservation')
    lo,hi=min(tl,tr),max(tl,tr)
    for t in (ntl,ntr):check(lo-128*EPS*(abs(t)+abs(lo))<=t<=hi+128*EPS*(abs(t)+abs(hi)),'Pair thermal maximum principle')
    check((ntl-ntr)*(1 if tl>tr else -1)>=-128*EPS*(abs(ntl)+abs(ntr)),'Thermal relaxation crosses equilibrium')
    # Native saved entropy is reconstructed independently above; compare its
    # actual change with the same EOS, allowing the independent closure error.
    expected_ds=new[0][2]+new[1][2]-old[0][2]-old[1][2]
    check(abs(dec(ds)-expected_ds)<=D('5e-10')*sum(abs(v[2])+v[3] for v in (*old,*new)),'Independent pair entropy change')
    budget=128*EPS*(abs(ul)/tl+abs(ur)/tr+abs(nl)/ntl+abs(nr)/ntr)
    close(sb,budget,budget,'Pair stored-U entropy allowance',8);check(ds>=-sb,'Pair entropy gate')


def review_grid(data,gid,controls=True):
    w,h,spacing,depth,px,py,dt,count,un,total,res,bound,ds,sb=data['grids'][gid]
    check(w>0 and h>0 and spacing>0 and depth>0 and dt>0 and px in (0,1) and py in (0,1),'Grid domain')
    vol=spacing*spacing*depth;xw=w+(not px);yh=h+(not py)
    walls={i:data['walls'][gid,i] for i in range(w*h)};fluid=[i for i in walls if not walls[i]]
    masses={};current={};entropies=[[],[]];us=[[],[]];ts=[[],[]]
    for i in fluid:
        for end in (0,1):
            m={s:v for (g,e,c,s),v in data['masses'].items() if (g,e,c)==(gid,end,i)}
            check(m and min(m.values())>0,'Missing/zero grid constituent');masses[end,i]=m
            if end:check(m==masses[0,i],'Heat changes bulk/trace inventory')
            psv={s:v for (g,e,c,s),v in data['passives'].items() if (g,e,c)==(gid,end,i)}
            check(len(psv)==2 and min(psv.values())>0,'Missing passive trace')
            if end:check(psv=={s:v for (g,e,c,s),v in data['passives'].items() if (g,e,c)==(gid,0,i)},'Heat changes passive inventory')
            u,t,p,s=data['cells'][gid,end,i];ind=eos(m,data['materials'],u,vol)
            for a,b in ((p,ind[0]),(t,ind[1])):near(a,b,'Independent grid EOS')
            check(abs(dec(s)-ind[2])<=D('5e-10')*(abs(ind[2])+ind[3]),'Independent grid entropy')
            entropies[end].append(s);us[end].append(u);ts[end].append(t)
            if not end:current[i]=u;near(data['caps'][gid,i],ind[3],'Frozen equilibrium capacity')
    pairs={};velocities={}
    for axis,nf in ((0,xw*h),(1,w*yh)):
        for f in range(nf):
            a=data['faces'][gid,axis,0,f];check(a==data['faces'][gid,axis,1,f],'Heat changes normal J/inertia/fixed face')
            mass,fixed,j=a;x,y=(f%xw,f//xw) if axis==0 else (f%w,f//w)
            l=((x-1)%w+y*w if x>0 or px else None) if axis==0 else (x+((y-1)%h)*w if y>0 or py else None)
            r=(x+y*w if x<w else None) if axis==0 else (x+y*w if y<h else None)
            l=None if l is None or walls[l] else l;r=None if r is None or walls[r] else r
            check(fixed==(l is None or r is None) and (not fixed or j==0),'Face wall geometry')
            im=sum((sum(masses[0,c].values(),F(0))/2 for c in (l,r) if c is not None),F(0))
            close(mass,im,im,'Face inertia from unchanged masses',8)
            velocities[axis,f]=rounded_scaled(j/mass) if mass else F(0)
            pairs[axis,f]=(l,r)
    links=[]
    for (axis,f),(l,r) in pairs.items():
        if l is None or r is None or l==r:continue
        jumps=[]
        for i in (l,r):
            x,y=i%w,i//w;a=x+y*xw if axis==0 else x+y*w
            b=((x+1)%xw)+y*xw if axis==0 else x+((y+1)%yh)*w
            jumps.append(max(F(0),rounded_scaled(velocities[axis,a]-velocities[axis,b])))
        closing=max(jumps)
        if closing>0:links.append((axis,f,l,r,closing))
    expected=[(0,v) for v in links]+[(1,v) for v in reversed(links)]
    check(count==len(expected) and set(k for k in data['events'] if k[0]==gid)=={(gid,i) for i in range(count)},'Missing/extra activated heat faces')
    transfers=F(0);unresolved=0
    for i,(sweep,link) in enumerate(expected):
        axis,f,l,r,closing=link;ea,ef,el,er,es,key,ec=data['events'][gid,i]
        check((ea,ef,el,er,es)==(axis,f,l,r,sweep),'Heat topology/symmetric order')
        close(ec,closing,abs(closing),'Compression switch',8)
        cl=data['caps'][gid,l];cr=data['caps'][gid,r];g=dt/2/spacing*closing*2*cl*cr/(cl+cr)
        p=data['pairs'][key];close(p[14],g,g,'Frozen face conductance',16)
        check((p[4],p[5])==(current[l],current[r]),'Pair does not consume actual evolving U')
        for side,c in enumerate((l,r)):
            check({s:v for (k,a,s),v in data['pmasses'].items() if (k,a)==(key,side)}==masses[0,c],'Pair source mass differs')
        current[l],current[r]=p[6:8];transfers+=abs(p[18]);unresolved+=p[3]
    check(all(current[i]==data['cells'][gid,1,i][0] for i in fluid),'Completed U differs from event chain')
    close(total,transfers,transfers,'Reported absolute heat',8);check(unresolved==un,'Grid unresolved count')
    terms=[*us[1],*[-v for v in us[0]]];close(res,sum(terms,F(0)),sum(map(abs,terms),F(0)),'Grid actual U residual')
    close(bound,128*EPS*sum(map(abs,terms),F(0)),bound,'Grid actual U bound',8);check(abs(res)<=bound,'Global actual U conservation')
    entropy_account(entropies[0],entropies[1],us[0],us[1],ts[0],ts[1],ds,sb)
    if controls and gid in (2,3):check(count==0 and total==0 and us[0]==us[1],'Rest/shear contact changes')
    if controls and gid==4:check(count>0 and total==0 and us[0]==us[1],'Uniform temperature changes')
    if controls and gid==5:check(total>0 and un>0 and us[0]==us[1],'Full-range grid heat not retained/flagged')
    if controls and gid==6:
        check(count==0 and total==0 and us[0]==us[1],'Stationary pressure contact changes')
        for i in fluid:check(abs(data['cells'][gid,0,i][2]-100000)<F(float('5e-10'))*100000,'Contact is not at its declared pressure')


def review(data,controls=True):
    for key in data['pairs']:review_pair(data,key)
    for gid in data['grids']:review_grid(data,gid,controls)
    if not controls:return
    previous=None
    for n,(time,*values) in sorted(data['network'].items()):
        check(time==F(.25),'Network duration changed');temps=values[:3];error=values[3]
        exact=[D(400)+D(v)*D('-1.5').exp() for v in (-100,200,-100)]
        computed=max(abs(dec(t)-e) for t,e in zip(temps,exact))
        check(abs(dec(error)-computed)<D('1e-9'),'Independent heat-network error')
        check(error>0 and (previous is None or error<F(.3)*previous),'Heat-network temporal refinement')
        previous=error
    check(previous<F(.05),'Fine heat-network absolute error')


def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=1048576,'Heat saved pipe cap')
    with localcontext() as context:
        context.prec=80;data=parse(raw);review(data);checks=base.CHECKS
        for mutation in ('heat','trace','geometry','root'):
            bad=copy.deepcopy(data)
            if mutation=='heat':
                row=list(bad['pairs'][0]);row[18]*=F(5,4);bad['pairs'][0]=tuple(row)
            elif mutation=='trace':
                key=next(k for k,v in bad['masses'].items() if v<F(1,10**20));bad['masses'][key]*=2
            elif mutation=='geometry':
                row=list(bad['events'][0,0]);row[2]=row[3];bad['events'][0,0]=tuple(row)
            else:
                row=list(bad['certs'][11]);row[1]+=1;bad['certs'][11]=tuple(row)
            caught=False
            try:review(bad)
            except (ValueError,KeyError):caught=True
            check(caught,'Altered heat record accepted')
    result=dict(Passed=True,Groups=8,SavedChecks=checks,CorruptionsRejected=['heat','trace','geometry','root'],SavedSha256=hashlib.sha256(raw).hexdigest(),NativeProcessLaunched=False,IndependentAgentReview=False,
        Scope='Separate occupied shock-heat component; not owning-step, shock refinement, production or game acceptance')
    if len(sys.argv)>2:
        with Path(sys.argv[2]).open('x') as stream:json.dump(result,stream,indent=2)
    print(json.dumps(result))


if __name__=='__main__':main()
