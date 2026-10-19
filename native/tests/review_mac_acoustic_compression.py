"""Saved-only acoustic/quadratic compression review; no native execution."""
import copy
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

def load(name, filename):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(filename))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
comp=load('acoustic_compression_reference','review_mac_compression.py')
heat=load('acoustic_eos_reference','review_mac_shock_heat.py')
base=comp.base;check,close,number,unique=comp.check,comp.close,comp.number,comp.unique
dec=comp.ref.dec;EPS=comp.EPS
CONTROLS=set(range(7))|set(range(10,15))|set(range(20,24))

def parse(raw):
    ordinary=[];extra={k:{} for k in ('linear','speeds','tangents')};rejected=None
    for line in raw.decode('ascii').splitlines():
        r=line.split();check(bool(r),'Empty acoustic record')
        if r[0]=='ALINEAR':
            check(len(r)==8,'Linear pair shape');unique(extra['linear'],int(r[1]),tuple(comp.scaled(r,i) for i in (2,4,6)))
        elif r[0]=='ASPEED':
            check(len(r)==5,'Frozen speed shape');unique(extra['speeds'],tuple(map(int,r[1:3])),comp.scaled(r,3))
        elif r[0]=='ATANGENT':
            check(len(r)==6,'Tangent shape');unique(extra['tangents'],tuple(map(int,r[1:3])),tuple(map(number,r[3:])))
        elif r[0]=='AREJECTED':
            check(len(r)==2 and rejected is None,'Acoustic rejection shape');rejected=int(r[1])
        else:ordinary.append(line)
    data=comp.parse(('\n'.join(ordinary)+'\n').encode('ascii'),required_passes=7,required_grids=range(7),analytic=False)
    check(rejected==10 and set(extra['linear'])==set(data['pairs']),'Missing acoustic rejections/pair metadata')
    check({i for i in data['pairs'] if i<1000}==CONTROLS,'Missing analytic acoustic controls')
    check(set(extra['tangents'])=={(0,0),(2,0),(3,0)},'Missing independent tangent controls')
    return data,extra

def pair(p,linear):
    il,ir,fl,fr,unresolved,jl,jr,b,v,k,impulse,q,nl,nr,wall=p
    c,rate,phi=linear
    check(c>=0,'Negative acoustic speed')
    if c==0:
        check(rate==0 and phi==1,'Quadratic reference has an acoustic relaxation')
        comp.review_pair(p);return
    check(il>0 and ir>0 and b>=0 and (not fl or jl==nl==0) and (not fr or jr==nr==0),'Acoustic domain/wall condition')
    ul,ur=jl/il,jr/ir
    close(v,ul-ur,abs(ul)+abs(ur),'Acoustic closing speed',8)
    if v<=0 or b==0:
        check(k==rate==impulse==q==wall==unresolved==0 and phi==1 and nl==jl and nr==jr,'Inactive acoustic pair changed')
        return
    a=(F(0) if fl else 1/il)+(F(0) if fr else 1/ir)
    check(a>0 and k>0 and rate>0 and phi>0 and impulse>0 and q>0,'Positive acoustic contribution disappeared')
    close(k,b*v*a,k,'Quadratic relaxation operand',8)
    close(rate,b*c*a,rate,'Linear relaxation operand',8)
    lam=dec(b*c*a)
    # Independent high-precision ODE solution. Its small argument series
    # avoids subtracting rounded exp(-lambda) from one. For lambda>200 the
    # omitted exponential is below 1.4e-87, beneath this 80-digit calculation.
    ph=(1-lam/2+lam*lam/6-lam**3/24) if lam<D('1e-20') else 1/lam if lam>200 else (1-(-lam).exp())/lam
    expected=dec(b*v*(c+v))*ph/(1+dec(b*a*v)*ph)
    close(phi,F(ph),F(ph),'Independent acoustic exponential factor',16)
    close(impulse,F(expected),F(expected),'Exact acoustic/quadratic impulse',16)
    work=impulse*(v-a*impulse/2)
    check(work>0,'Nonpositive acoustic work');close(q,work,work,'Compatible acoustic work',16)
    close(nl,F(0) if fl else jl-impulse,abs(jl)+impulse,'Acoustic left primary update',2)
    close(nr,F(0) if fr else jr+impulse,abs(jr)+impulse,'Acoustic right primary update',2)
    check(wall==(impulse if fl else -impulse if fr else 0),'Acoustic wall sign')
    check(unresolved==sum((not fl and not comp.resolved(jl,nl,-impulse),not fr and not comp.resolved(jr,nr,impulse))),'Acoustic unresolved flags')
    terms=[comp.kinetic(il,nl),comp.kinetic(ir,nr),-comp.kinetic(il,jl),-comp.kinetic(ir,jr),q]
    close(sum(terms,F(0)),F(0),sum(map(abs,terms),F(0)),'Actual acoustic pair K/heat')
    terms=[nl,nr,-jl,-jr,-wall]
    close(sum(terms,F(0)),F(0),sum(map(abs,terms),F(0)),'Actual acoustic momentum/wall')
    for velocity in (nl/il,nr/ir):
        check(min(ul,ur)-128*EPS*(abs(ul)+abs(ur))<=velocity<=max(ul,ur)+128*EPS*(abs(ul)+abs(ur)),'Acoustic endpoint crossed its initial interval')

def equilibrium(masses,materials,u,v):
    p,t,s,_=heat.eos(masses,materials,u,v)
    return p,t,D(0),s

def review_pressure_sensors(data,extra):
    expected_pressures=set();expected_sensors=set()
    for gid,g in data['grids'].items():
        w,h,_,_,px,py=g[:6];walls={i:data['walls'][gid,i] for i in range(w*h)}
        p={i:extra['pressures'][gid,i] for i in walls}
        for i in walls:
            expected_pressures.add((gid,i))
            check(p[i]==(0 if walls[i] else data['cells'][gid,0,i][2]),'Sensor pressure differs from actual source EOS')
        def neighbour(i,axis,forward):
            c,extent,periodic=(i%w,w,px) if axis==0 else (i//w,h,py)
            if not periodic and (c==extent-1 if forward else c==0):return i
            n=(c+(1 if forward else -1))%extent;j=n+(i//w)*w if axis==0 else i%w+n*w
            return i if walls[j] else j
        for axis in (0,1):
            raw={}
            for i in walls:
                if walls[i]:raw[i]=F(0);continue
                a,b=p[neighbour(i,axis,False)],p[neighbour(i,axis,True)];den=a+2*p[i]+b
                raw[i]=abs(a-2*p[i]+b)/den if den else F(0)
            for i in walls:
                expected_sensors.add((gid,axis,i));actual=extra['sensors'][gid,axis,i]
                expected=F(0) if walls[i] else max(raw[i],raw[neighbour(i,axis,False)],raw[neighbour(i,axis,True)])
                check(0<=actual<=1,'Pressure sensor outside dimensionless interval')
                close(actual,expected,expected,'Independent pressure curvature and neighbour maximum',16)
    check(set(extra['pressures'])==expected_pressures and set(extra['sensors'])==expected_sensors,'Missing/orphan pressure sensor field')

def review_component(data,extra,pressure_sensed=False):
    check(set(extra['linear'])==set(data['pairs']),'Missing/orphan acoustic pair metadata')
    if pressure_sensed:review_pressure_sensors(data,extra)
    else:check(not extra.get('pressures') and not extra.get('sensors'),'Unsensed component contains pressure sensor fields')
    for key,p in data['pairs'].items():pair(p,extra['linear'][key])
    # Every pair was independently checked above. Reconstruct its grid path.
    comp.review(data,analytic=False,pair_reviewer=lambda p:None,equilibrium=equilibrium)
    expected_speeds=set()
    for gid,g in data['grids'].items():
        w,h,spacing,depth=g[:4];volume=spacing*spacing*depth
        for cell in range(w*h):
            expected_speeds.add((gid,cell));speed=extra['speeds'][gid,cell]
            if data['walls'][gid,cell]:check(speed==0,'Solid acoustic speed must be zero');continue
            masses={s:m for (i,e,c,s),m in data['masses'].items() if (i,e,c)==(gid,0,cell)}
            p,t,_,_=heat.eos(masses,data['materials'],data['cells'][gid,0,cell][0],volume)
            _,_,cv,_,moles=comp.ref.eos_values(masses,data['materials'],p)
            compliance=D(0);vg=moles*dec(comp.ref.R)*t/p if moles else D(0)
            for i,m in masses.items():
                phase,_,_,_,rho,bulk=data['materials'][i]
                if phase:compliance+=dec(m)/dec(rho)/dec(bulk)/(1+p/dec(bulk))**2
            if moles:compliance+=cv/(cv+moles*dec(comp.ref.R))*vg/p
            mass=sum(masses.values(),F(0));expected=dec(volume)/(dec(mass)*compliance).sqrt()/2
            check(compliance>0 and speed>0,'Nonpositive occupied acoustic stiffness')
            check(abs(dec(speed)-expected)<=D('5e-10')*expected,'Frozen acoustic speed differs from independent mixed EOS')
            if (gid,cell) in extra.get('tangents',{}):
                delta,lo,hi=extra['tangents'][gid,cell]
                check(delta==number('1e-5') and lo>hi>0,'Invalid finite-difference tangent operands')
                measured=(dec(lo-hi)*dec(volume)**2/(2*dec(delta)*dec(mass))).sqrt()/2
                check(abs(dec(speed)-measured)<D('1e-5')*dec(speed),'Independent tangent finite difference failed')
        for (gkey,_),event in data['events'].items():
            if gkey!=gid:continue
            axis,cell,_,key=event
            expected=extra['speeds'][gid,cell]*(extra['sensors'][gid,axis,cell] if pressure_sensed else 1)
            close(extra['linear'][key][0],expected,expected,'Event did not use its frozen source-cell acoustic coefficient',8)
    check(set(extra['speeds'])==expected_speeds,'Missing/orphan frozen acoustic speed')

def review(data,extra):
    for key,p in data['pairs'].items():
        if key<1000:pair(p,extra['linear'][key])
    first,half,twice=(data['pairs'][i] for i in (0,4,5))
    for i in (12,13):close(first[i],twice[i],abs(first[i]),'Acoustic exact semigroup',128)
    close(half[11]+twice[11],first[11],first[11],'Acoustic semigroup heat',128)
    check(data['pairs'][22][7:12]==data['pairs'][23][7:12],'Acoustic Galilean work differs')
    check(data['pairs'][10][10]>0 and float(data['pairs'][10][10])==0 and data['pairs'][10][11]>0 and float(data['pairs'][10][11])==0,'Missing subrange acoustic impulse/heat')
    check(data['pairs'][11][13]>0 and float(data['pairs'][11][13])==0 and data['pairs'][11][4]==1,'Missing new subrange canonical target/unresolved flag')
    grid_data=copy.deepcopy(data);grid_data['pairs']={key:p for key,p in data['pairs'].items() if key>=1000}
    grid_extra=extra|{'linear':{key:v for key,v in extra['linear'].items() if key>=1000}}
    review_component(grid_data,grid_extra)
    tiny=data['grids'][5]
    check(tiny[10]>0 and float(tiny[10])==0 and comp.retained_cell_heat(data,5,tiny[8]),'Missing grid subrange heat/unresolved storage')
    check(data['grids'][6][7]==0,'Stationary composition contact has compression events')

def main():
    raw=Path(sys.argv[1]).read_bytes();data,extra=parse(raw)
    with localcontext() as context:
        context.prec=80;review(data,extra);checks=base.CHECKS+heat.base.CHECKS;rejected=0
        for corruption in range(4):
            bad,other=copy.deepcopy(data),copy.deepcopy(extra)
            if corruption==0:
                row=list(bad['pairs'][0]);row[10]*=F(17,16);bad['pairs'][0]=tuple(row)
            if corruption==1:
                row=list(bad['pairs'][0]);row[11]*=F(17,16);bad['pairs'][0]=tuple(row)
            if corruption==2:
                key=min((k for k in bad['masses'] if k[1]==1 and bad['materials'][k[3]][0]==0),key=lambda k:bad['masses'][k]);bad['masses'][key]*=F(17,16)
            if corruption==3:other['speeds'][0,0]*=F(17,16)
            try:review(bad,other)
            except ValueError:rejected+=1
        check(rejected==4,'Altered acoustic impulse/heat/trace/tangent escaped review')
    result=dict(Passed=True,SavedSha256=hashlib.sha256(raw).hexdigest(),SavedChecks=checks,CorruptionsRejected=rejected,
        NativeProcessLaunched=False,IndependentAgentReview=False,Scope='Separate acoustic/quadratic compression component only; no owning, strong-flow, game or performance acceptance from this review.')
    if len(sys.argv)>2:
        with Path(sys.argv[2]).open('x') as output:json.dump(result,output,indent=2)
    print(json.dumps(result))

if __name__=='__main__':main()
