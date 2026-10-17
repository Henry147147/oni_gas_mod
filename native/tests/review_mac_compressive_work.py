"""Saved-only compressive work review; exact accounts and independent 80-digit EOS."""
from decimal import Decimal as D,localcontext
from fractions import Fraction as F
import copy,hashlib,importlib.util,json,sys
from pathlib import Path

spec=importlib.util.spec_from_file_location('thermal',Path(__file__).with_name('review_mac_shock_heat.py'))
thermal=importlib.util.module_from_spec(spec);spec.loader.exec_module(thermal)
base=thermal.base;check,close,number,unique=base.check,base.close,base.number,base.unique
dec=thermal.dec;EPS=base.EPS;R=thermal.ref.R

def parse(raw,case_ids=tuple(range(16)),profiles=(0,1),groups=7,rejections=10):
    d={k:{} for k in ('definitions','cases','initial','end','iso','masses','full')};passes=[];rejected=None
    for line in raw.decode('ascii').splitlines():
        r=line.split();check(bool(r),'Empty compressive record');kind=r[0]
        if kind=='CWDEF':
            check(len(r)==11 and r[3] in ('0','1'),'Definition shape');unique(d['definitions'],tuple(map(int,r[1:3])),(int(r[3]),*map(number,r[4:])))
        elif kind=='CWCASE':
            check(len(r)==10 and r[9] in ('0','1'),'Compressive case shape')
            unique(d['cases'],int(r[1]),(int(r[2]),*map(number,r[3:7]),base.scaled_energy(r[7],r[8]),int(r[9])))
        elif kind in ('CWINIT','CWEND'):
            check(len(r)==(6 if kind=='CWINIT' else 7),'Endpoint shape')
            unique(d['initial' if kind=='CWINIT' else 'end'],int(r[1]),tuple(map(number,r[2:])))
        elif kind=='CWISO':
            check(len(r)==6 and r[4] in ('0','1') and r[5] in ('0','1'),'Isentropic shape')
            unique(d['iso'],int(r[1]),(*map(number,r[2:4]),*map(int,r[4:6])))
        elif kind=='CWMASS':
            check(len(r)==5,'Constituent shape');unique(d['masses'],tuple(map(int,r[1:3])),tuple(map(number,r[3:])))
        elif kind=='CWFULL':
            check(len(r)==22,'Full-range response shape');unique(d['full'],int(r[1]),tuple(base.scaled_energy(r[i],r[i+1]) for i in range(2,22,2)))
        elif kind=='CWREJECTED':
            check(len(r)==2 and rejected is None,'Rejection shape');rejected=int(r[1])
        else:
            check(kind=='PASS','Unknown compressive work record');passes.append(line)
    check(len(set(passes))==len(passes)==groups and rejected==rejections,'Missing compressive work groups or rejection controls')
    check(all(set(d[k])==set(case_ids) for k in ('cases','initial','end','iso','full')),'Missing or extra complete compressive response')
    check(set(k[0] for k in d['definitions'])==set(profiles) and len(d['definitions'])==4*len(profiles),'Missing immutable material profiles')
    check(all(c in d['cases'] for c,_ in d['masses']),'Orphan constituent')
    return d

def reading(actual,expected,why):
    check(abs(dec(actual)-expected)<=D('5e-10')*abs(expected),why)

def gas_work(masses,materials,u,v,dv):
    _,_,capacity,offset,moles=thermal.ref.eos_values(masses,materials,D(0))
    t0=(dec(u)-offset)/capacity;p0=moles*dec(R)*t0/dec(v);k=moles*dec(R)/capacity;x=dec(dv)/dec(v)
    if abs(x)<D('.001'):
        coefficient=D(1);power=D(1);value=D(1);derivative=D(0)
        for n in range(1,17):
            coefficient*=-(k+n)/(n+1);derivative+=n*coefficient*power;power*=x;value+=coefficient*power
        work=p0*value;dp=p0/dec(v)*derivative
    else:
        work=-capacity*t0*((1+x)**(-k)-1)/dec(dv)
        endpoint=p0*(1+x)**(-(k+1));dp=(endpoint-work)/dec(dv)
    return -work*dec(dv),work,dp

def liquid_work(masses,materials,u,v,dv):
    check(len(masses)==1,'Pure-liquid response needs the declared single liquid')
    i,m=next(iter(masses.items()));_,_,_,_,rho,bulk=materials[i]
    nominal=dec(m)/dec(rho);b=dec(bulk);volume=dec(v);x=dec(dv)/volume;p0=b*(nominal/volume-1)
    if abs(x)<D('.001'):
        value=D(0);derivative=D(0)
        for n in range(2,21):
            coefficient=D(1 if n%2==0 else -1)/n
            value+=coefficient*x**(n-1);derivative+=(n-1)*coefficient*x**(n-2)
        work=p0-b*nominal/volume*value;dp=-b*nominal/volume**2*derivative
    else:
        du=-b*nominal*(1+x).ln()+b*dec(dv);work=-du/dec(dv)
        endpoint=b*(nominal/(volume+dec(dv))-1);dp=(endpoint-work)/dec(dv)
    return -work*dec(dv),work,dp

def mixed_work(masses,materials,u,v,dv):
    p0,t0,vg0,_=thermal.ref.equilibrium(masses,materials,u,v)
    _,_,capacity,offset,moles=thermal.ref.eos_values(masses,materials,p0)
    alpha=capacity/(capacity+moles*dec(R));target=dec(v+dv)
    def state(p):
        liquid,elastic,*_=thermal.ref.eos_values(masses,materials,p)
        vg=vg0*(p0/p)**alpha;t=t0*(p/p0)**(1-alpha)
        return liquid+vg,offset+capacity*t+elastic
    lo=D(0);hi=p0
    for _ in range(100):
        if state(hi)[0]<=target:break
        hi*=2
    else:raise ValueError('Independent mixed compression bracket exhausted')
    for _ in range(300):
        mid=(lo+hi)/2
        if state(mid)[0]>target:lo=mid
        else:hi=mid
    p1=(lo+hi)/2;du=state(p1)[1]-dec(u)
    check(dv!=0,'Mixed zero-step tangent has no fixture')
    work=-du/dec(dv);return du,work,(p1-work)/dec(dv)

def review(d,fixture_controls=True):
    subrange=0;negative=0
    for c,row in d['cases'].items():
        profile,v,dv,h,dt,linear,unresolved=row
        check((not fixture_controls or profile==(1 if c==6 else 0)) and min(v,h,dt)>0 and linear>=0,'Invalid source or transport coefficient')
        definitions={i:q for (p,i),q in d['definitions'].items() if p==profile}
        materials={i:q[:6] for i,q in definitions.items()}
        masses={i:m[0] for (j,i),m in d['masses'].items() if j==c}
        check(masses and all(m[0]==m[1] and m[0]>0 for (j,_),m in d['masses'].items() if j==c),'A constituent or trace changed')
        check(all(i in materials for i in masses),'Constituent lacks a declared material')
        u0,p0,t0,s0=d['initial'][c];v1,u1,p1,t1,s1=d['end'][c];isodu,isop,rv,ru=d['iso'][c]
        check(v1==F(float(v+dv)) and v1>0,'Actual endpoint volume differs')
        for endpoint,(u,p,t,s,volume) in enumerate(((u0,p0,t0,s0,v),(u1,p1,t1,s1,v1))):
            ep,et,es,_=thermal.eos(masses,materials,u,volume)
            for actual,expected,name in ((p,ep,'pressure'),(t,et,'temperature'),(s,es,'entropy')):
                reading(actual,expected,'Actual stored endpoint '+name)
            check(all(dec(definitions[i][6])<=et<=dec(definitions[i][7]) for i in masses),'Actual EOS outside declared temperature domain')
            check(min(ep,et)>0,'Nonpositive actual occupied endpoint')
            if not endpoint and u<0:negative+=1
        q,dq,heat,work,tangent,requested,res,bound,ds,sb=d['full'][c]
        mass=sum(masses.values(),F(0));speed=h/dt;rho=mass/v;s=max(-dv/v,F(0))
        a=rho*linear*speed;b=rho*speed*speed
        expected_q=a*s+b*s*s
        expected_dq=-(a+2*b*s)/v if dv<=0 else F(0)
        close(q,expected_q,abs(expected_q),'Stress law differs')
        close(dq,expected_dq,abs(expected_dq),'Stress tangent differs')
        close(heat,expected_q*max(-dv,F(0)),abs(expected_q*dv),'Stress heat is not its actual compressive work')
        check(heat>=0 and q>=0 and (dv<0 or heat==q==0),'Expansion heated or compression lost its sign')
        if heat and float(heat)==0:subrange+=1
        phases={materials[i][0] for i in masses}
        exact_du,exact_work,exact_derivative=(gas_work if phases=={0} else liquid_work if phases=={1} else mixed_work)(masses,materials,u0,v,dv)
        reading(isodu,exact_du,'Independent isentropic balanced energy')
        reading(isop,exact_work,'Independent isentropic work pressure')
        reading(tangent,exact_derivative+dec(expected_dq),'Complete work tangent')
        close(work,isop+q,abs(isop)+abs(q),'Pressure force omits stress')
        close(requested,isodu+heat,abs(isodu)+abs(heat),'Requested U increment differs')
        check(u1==F(float(u0+isodu+heat)),'Canonical U does not contain the admitted pressure and stress work')
        actual_residual=u1-u0+work*dv
        close(res,actual_residual,abs(u1)+abs(u0)+abs(work*dv),'Reported stored U/work residual')
        close(bound,128*EPS*(abs(u1)+abs(u0)+abs(work*dv)),bound,'Original work operand bound differs',8)
        check(abs(actual_residual)<=bound and abs(res)<=bound and bound>0,'Actual stored work exceeds original bound')
        close(ds,s1-s0,abs(s1)+abs(s0),'Actual entropy difference')
        allowance=128*EPS*(abs(u0)/t0+abs(u1)/t1)
        close(sb,allowance,allowance,'Original stored-U entropy allowance differs',8)
        check(ds>=-sb,'Actual stored entropy reduced beyond original allowance')
        expected_unresolved=abs(u1-u0-requested)>128*EPS*abs(requested)
        check(unresolved==int(expected_unresolved),'Unresolved canonical U flag differs')
        if fixture_controls and c==15:check(unresolved==1 and rv==ru==0 and u1==u0 and v1==v,'Subrange canonical representability was hidden')
    if fixture_controls:check(subrange==1 and negative>=1 and d['initial'][6][0]<0,'Missing subrange heat or negative-reference control')

def main():
    if len(sys.argv)!=3:raise SystemExit('Usage: review_mac_compressive_work.py <saved.raw> <fresh-report.json>')
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=1048576,'Compressive raw pipe cap exceeded')
    with localcontext() as ctx:
        ctx.prec=80;data=parse(raw);review(data);saved_checks=base.CHECKS;rejected=0
        for kind in ('heat','trace','energy'):
            corrupt=copy.deepcopy(data)
            if kind=='heat':
                values=list(corrupt['full'][0]);values[2]*=2;corrupt['full'][0]=tuple(values)
            elif kind=='trace':
                key=next(k for k,m in corrupt['masses'].items() if k[0]==0 and m[0]<F(1,10**20))
                before,after=corrupt['masses'][key];corrupt['masses'][key]=(before,after*2)
            else:
                values=list(corrupt['end'][0]);values[1]+=100;corrupt['end'][0]=tuple(values)
            try:review(corrupt)
            except ValueError:rejected+=1
            else:raise ValueError('Corrupted compressive '+kind+' admitted')
    result=dict(Passed=True,SavedChecks=saved_checks,Cases=16,NativeGroups=7,CorruptionsRejected=rejected,
                SavedSha256=hashlib.sha256(raw).hexdigest(),NativeProcessLaunched=False,IndependentAgentReview=False,
                Scope='Local constitutive pressure-work response; no owning timestep, strong-flow or game acceptance')
    with Path(sys.argv[2]).open('x') as output:json.dump(result,output,indent=2);output.write('\n')
    print(json.dumps(result))

if __name__=='__main__':main()
