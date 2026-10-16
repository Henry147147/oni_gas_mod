"""Saved-state Euler Riemann review: Fraction accounts and 80-digit exact waves.

No native executable or native EOS is loaded. --partial reviews only available
complete profiles after a failed native gate and cannot certify the full gate.
"""
import copy
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import functools
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec=importlib.util.spec_from_file_location('mac_accounts',Path(__file__).with_name('review_mac_transport.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
check,close,number,unique=base.check,base.close,base.number,base.unique
EPS=base.EPS;R=F(float('8.31446261815324'))
def dec(v): return D(v.numerator)/D(v.denominator)

def parse(raw):
    materials={};problems={};states={};faces={};modes={};ids=None;passes=0
    for line in raw.decode('ascii').splitlines():
        row=line.split();check(bool(row),'Empty Riemann record');kind=row[0]
        if kind=='MATERIAL':
            check(len(row)==8,'Material shape');unique(materials,int(row[1]),tuple(map(number,row[2:])))
        elif kind=='RIDS':
            check(len(row)==3 and ids is None,'Identity shape/duplicate');ids=tuple(map(int,row[1:]))
        elif kind=='RPROBLEM':
            check(len(row)==10,'Problem shape');unique(problems,int(row[1]),tuple(map(number,row[2:])))
        elif kind=='RSTATE':
            check(len(row)==13,'State shape');unique(states,tuple(map(int,row[1:5])),tuple(map(number,row[5:])))
        elif kind=='RFACE':
            check(len(row)==7,'Face shape');unique(faces,tuple(map(int,row[1:5])),tuple(map(number,row[5:])))
        elif kind=='RMODE':
            check(len(row)==19,'Mode shape');unique(modes,tuple(map(int,row[1:3])),tuple(map(number,row[3:])))
        else:check(kind=='PASS','Unknown Riemann record');passes+=1
    check(ids is not None and len(set(ids))==2 and all(i in materials for i in ids),'Missing constituent identities')
    return materials,ids,problems,states,faces,modes,passes

class Exact:
    def __init__(self,left,right,gamma):
        self.left,self.right,self.g=left,right,gamma;g=gamma
        cl=(g*left[2]/left[0]).sqrt();cr=(g*right[2]/right[0]).sqrt();self.cl,self.cr=cl,cr
        check(right[1]-left[1]<2*(cl+cr)/(g-1),'Oracle would create vacuum')
        def curve(s,c,p):
            rho,_,p0=s
            return (p-p0)*(2/((g+1)*rho)/(p+(g-1)/(g+1)*p0)).sqrt() if p>p0 else 2*c/(g-1)*((p/p0)**((g-1)/(2*g))-1)
        def residual(p):return curve(left,cl,p)+curve(right,cr,p)+right[1]-left[1]
        lo=D(0);hi=max(left[2],right[2])
        for _ in range(128):
            if residual(hi)>=0:break
            hi*=2
        else:raise ValueError('Independent Riemann bracket exhausted')
        for _ in range(300):
            mid=(lo+hi)/2
            if residual(mid)>0:hi=mid
            else:lo=mid
        self.p=(lo+hi)/2;self.u=(left[1]+right[1]+curve(right,cr,self.p)-curve(left,cl,self.p))/2
        beta=(g-1)/(g+1)
        self.dl=left[0]*((self.p/left[2]+beta)/(beta*self.p/left[2]+1) if self.p>left[2] else (self.p/left[2])**(1/g))
        self.dr=right[0]*((self.p/right[2]+beta)/(beta*self.p/right[2]+1) if self.p>right[2] else (self.p/right[2])**(1/g))
        self.lh=left[1]-cl*(((g+1)/(2*g)*self.p/left[2]+(g-1)/(2*g)).sqrt() if self.p>left[2] else 1)
        self.lt=self.lh if self.p>left[2] else self.u-(g*self.p/self.dl).sqrt()
        self.rh=right[1]+cr*(((g+1)/(2*g)*self.p/right[2]+(g-1)/(2*g)).sqrt() if self.p>right[2] else 1)
        self.rt=self.rh if self.p>right[2] else self.u+(g*self.p/self.dr).sqrt()
    def at(self,xi):
        g=self.g
        if xi<=self.u:
            if xi<=self.lh:return self.left,None
            if xi>=self.lt:return (self.dl,self.u,self.p),None
            s,c,sign=self.left,self.cl,-1
        else:
            if xi>=self.rh:return self.right,None
            if xi<=self.rt:return (self.dr,self.u,self.p),None
            s,c,sign=self.right,self.cr,1
        rho,u,p=s;A=2/(g+1)*(1-sign*(g-1)*u/(2*c));B=sign*(g-1)/((g+1)*c);f=A+B*xi
        velocity=2/(g+1)*(sign*-c+(g-1)*u/2+xi)
        return (rho*f**(2/(g-1)),velocity,p*f**(2*g/(g-1))),(s,A,B)
    @functools.lru_cache(maxsize=None)
    def average(self,a,b,time):
        points=sorted({a,b,*[D('.5')+time*s for s in (self.lh,self.lt,self.u,self.rt,self.rh) if a<D('.5')+time*s<b]})
        values=[D(0),D(0)]
        for l,r in zip(points,points[1:]):
            xi=((l+r)/2-D('.5'))/time;state,fan=self.at(xi)
            if fan is None:
                values[0]+=(r-l)*state[0];values[1]+=(r-l)*state[2];continue
            s,A,B=fan;ql=A+B*(l-D('.5'))/time;qr=A+B*(r-D('.5'))/time
            for j,k,scale in ((0,2/(self.g-1),s[0]),(1,2*self.g/(self.g-1),s[2])):
                values[j]+=scale*time*(qr**(k+1)-ql**(k+1))/(B*(k+1))
        return tuple(v/(b-a) for v in values)

def review(data,complete=True):
    materials,ids,problems,states,faces,modes,passes=data;ni,he=ids
    if complete:check(set(modes)=={(c,n) for c in range(3) for n in (64,128,256)} and passes==3,'Incomplete Riemann gate')
    check(bool(modes),'No completed Riemann profile')
    check(set(states)=={(c,n,e,i) for c,n in modes for e in (0,1) for i in range(2*n)},'Missing/extra saved cells')
    check(set(faces)=={(c,n,a,f) for c,n in modes for a in (0,1) for f in range(2*(n+1) if a==0 else 2*n)},'Missing/extra saved faces')
    _,mn,cvn,offn,_,_=map(dec,materials[ni]);_,mh,cvh,offh,_,_=map(dec,materials[he])
    rgas=dec(R)*(1/mn+D('1e-28')/mh)/(1+D('1e-28'));cv=(cvn+D('1e-28')*cvh)/(1+D('1e-28'));gamma=1+rgas/cv
    results={};oracles={}
    for control,p in problems.items():
        check(control in (0,1,2),'Unknown scientific problem')
        v=tuple(map(dec,p));oracles[control]=Exact(v[:3],v[3:6],gamma)
        check((v[0],v[2],v[3],v[5],v[6],v[7])==(D(1),D(100000),D('.125') if control==0 else D(1),D(10000) if control==0 else D(100000),D(0) if control==0 else D('.25'),D(1) if control==0 else D('.75')),'Scientific problem inputs changed')
        speed=(D('1.5') if control==1 else D(4) if control==2 else D(0))*(gamma*100000).sqrt()
        check(abs(v[1]-(speed if control!=2 else -speed))<D('1e-12')*(gamma*100000).sqrt() and v[4]==-v[1],'Scientific velocity inputs changed')
    for (control,n),mode in modes.items():
        h,time,dt,steps,rejected,eres,ebound,entropy,entropy_bound,erho,ep,eu,centre_rho,centre_p,wx,wy=mode
        check(h==F(float(1/n)) and min(time,dt)>0 and steps.denominator==rejected.denominator==1,'Grid/time/count metadata')
        exact=oracles[control];lo,hi=map(dec,problems[control][6:]);sound=exact.cl;v=h*h
        expected_time=(D('.2') if control==0 else D('.06') if control==1 else D('.03'))/D(100000).sqrt()
        divisor=D(2) if control==0 else D('2.5') if control==1 else D(5)
        check(abs(dec(time)-expected_time)<D('1e-14')*expected_time and abs(dec(dt)-D('.25')*dec(h)/(divisor*sound))<D('1e-14')*dec(dt),'Scientific duration/timestep changed')
        mass={(e,i):states[control,n,e,i][0]+states[control,n,e,i][1] for e in (0,1) for i in range(2*n)}
        energy_terms=[];sums=[D(0),D(0)];allow=D(0);errors=[D(0)]*3;central=[D(0),D(0)]
        for column in (0,1,6,7):
            terms=[states[control,n,e,i][column]*(1 if e else -1) for e in (0,1) for i in range(2*n)]
            close(sum(terms,F(0)),F(0),sum(map(abs,terms),F(0)),'Global constituent/passive conservation')
        for e in (0,1):
            for i in range(2*n):
                nitrogen,helium,u,p,t,s,tag,trace=states[control,n,e,i];check(min(nitrogen,helium,tag,trace)>0,'Positive bulk/trace lost')
                capacity=dec(nitrogen)*cvn+dec(helium)*cvh;temp=(dec(u)-dec(nitrogen)*offn-dec(helium)*offh)/capacity
                moles=dec(nitrogen)/mn+dec(helium)/mh;pressure=moles*dec(R)*temp/dec(v)
                check(temp>0 and pressure>0,'Nonpositive independent EOS')
                independent_s=capacity*(temp/dec(F(float(298.15)))).ln()
                for amount,molar in ((nitrogen,mn),(helium,mh)):
                    m=dec(amount)/molar;independent_s+=m*dec(R)*(dec(v)/m).ln()
                for actual,expected in ((p,pressure),(t,temp)):
                    check(abs(dec(actual)-expected)<=D('5e-10')*abs(expected),'Independent ideal-mixture EOS reading')
                if not e:
                    initial=exact.left if i%n<n//2 else exact.right
                    check(abs(dec(mass[e,i]/v)-initial[0])<D('1e-12')*initial[0] and abs(pressure-initial[2])<D('1e-12')*initial[2],'Source Riemann density/pressure mismatch')
                    close(helium,nitrogen*F(float('1e-28')),abs(helium)+abs(nitrogen*F(float('1e-28'))),'Source helium trace ratio')
                check(abs(dec(s)-independent_s)<=D('5e-10')*max(abs(independent_s),capacity),'Independent entropy reading')
                sums[e]+=independent_s;allow+=128*dec(EPS)*abs(dec(u))/temp;energy_terms.append(u*(1 if e else -1))
                if e:
                    x=i%n;a=dec(h)*x;b=a+dec(h);rho_exact,p_exact=exact.average(a,b,dec(time))
                    if a>=lo and b<=hi:
                        errors[0]+=dec(h)/2*abs(dec(mass[e,i]/v)-rho_exact)/(hi-lo)
                        errors[1]+=dec(h)/2*abs(pressure-p_exact)/(D(100000)*(hi-lo))
                    if x in (n//2-1,n//2):
                        central[0]=max(central[0],abs(dec(mass[e,i]/v)-rho_exact)/rho_exact);central[1]=max(central[1],abs(pressure-p_exact)/p_exact)
        impulses=[[],[]]
        for axis in (0,1):
            for f in range(2*(n+1) if axis==0 else 2*n):
                j=faces[control,n,axis,f]
                if axis==0:
                    y,x=divmod(f,n+1);adj=([x-1+y*n] if x else [])+([x+y*n] if x<n else [])
                    if x in (0,n):check(j==(F(0),F(0)),'Rigid wall normal momentum')
                else:
                    y,x=divmod(f,n);adj=[x+((y+1)%2)*n,x+y*n]
                inertia=[sum((mass[e,i] for i in adj),F(0))/2 for e in (0,1)]
                initial_u=F(0) if axis or x in (0,n) or x==n//2 else problems[control][1] if x<n//2 else problems[control][4]
                close(j[0],inertia[0]*initial_u,abs(j[0])+abs(inertia[0]*initial_u),'Source face momentum')
                for e in (0,1):energy_terms.append(j[e]*j[e]/(2*inertia[e])*(1 if e else -1))
                impulses[axis].extend((j[1],-j[0]))
                if axis==0 and x<n and lo<=dec(h)*x<hi:
                    expected,_=exact.at((dec(h)*x-D('.5'))/dec(time));errors[2]+=dec(h)/2*abs(dec(j[1]/inertia[1])-expected[1])/(sound*(hi-lo))
        momentum_ok=True;momentum_witness=[]
        for terms,wall in zip(impulses,(wx,wy)):
            residual=sum(terms,F(0))-wall;bound=128*EPS*(sum(map(abs,terms),F(0))+abs(wall));ok=abs(residual)<=bound
            momentum_ok=momentum_ok and ok
            momentum_witness.append(dict(Residual=str(dec(residual)),EndpointOperandBound=str(dec(bound)),Passed=ok))
            if complete:check(ok,'Momentum change minus wall-on-fluid impulse')
        check(abs(sum(energy_terms,F(0)))<=ebound and abs(eres)<=ebound and ebound>0,'Actual U+face-K bound')
        close(eres,sum(energy_terms,F(0)),sum(map(abs,energy_terms),F(0)),'Reported represented energy residual')
        check(abs(dec(entropy)-(sums[1]-sums[0]))<=allow+D('1e-12'),'Reported entropy change')
        check(abs(dec(entropy_bound)-allow)<=D('1e-10')*allow,'Reported stored-U entropy allowance')
        for reported,expected in zip((erho,ep,eu),errors):check(abs(dec(reported)-expected)<D('2e-7'),'Independent primitive L1 error')
        for reported,expected in zip((centre_rho,centre_p),central):check(abs(dec(reported)-expected)<D('2e-7')*max(D(1),expected),'Independent central error')
        scientific=(momentum_ok and rejected==0 and steps<=int(dec(time/dt).to_integral_value(rounding='ROUND_CEILING'))+2 and sums[1]-sums[0]>=-allow)
        if n==256:scientific=scientific and max(errors)<D('.06') and (control!=2 or max(central)<D('.5'))
        if complete:check(scientific,'Strong-flow timestep/entropy/absolute gate')
        results[control,n]=dict(Errors=list(map(str,errors)),Central=list(map(str,central)),Momentum=momentum_witness,ProfilePhysicalCriteriaPassed=bool(scientific))
    for c in range(3):
        for a,b in ((64,128),(128,256)):
            if (c,a) in results and (c,b) in results:
                good=all(D(y)<D('.85')*D(x) for x,y in zip(results[c,a]['Errors'],results[c,b]['Errors']))
                results[c,b]['JointRefinementPassed']=good
                if complete:check(good,'Independent joint rho/p/u refinement')
    return results

def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=2097152,'Riemann saved pipe cap');partial='--partial' in sys.argv[2:]
    with localcontext() as context:
        context.prec=80;data=parse(raw);results=review(data,complete=not partial);checks=base.CHECKS
        if not partial:
            for corruption in ('trace','wall','profile'):
                bad=copy.deepcopy(data)
                if corruption=='trace':
                    k=(0,64,1,0);v=list(bad[3][k]);v[1]*=F(101,100);bad[3][k]=tuple(v)
                elif corruption=='wall':
                    k=(0,64,0,0);old,new=bad[4][k];bad[4][k]=(old,new+1)
                else:
                    k=(0,64);v=list(bad[5][k]);v[10]/=2;bad[5][k]=tuple(v)
                caught=False
                try:review(bad)
                except ValueError:caught=True
                check(caught,'Altered Riemann profile accepted')
    print(json.dumps(dict(Passed=not partial,PartialDataReview=partial,SavedSha256=hashlib.sha256(raw).hexdigest(),SavedChecks=checks,
        CorruptionsRejected=0 if partial else 3,NativeProcessLaunched=False,IndependentAgentReview=False,
        Profiles={str(k):v for k,v in results.items()},Scope='Closed occupied ideal-gas Riemann controls; no vacuum interface, production or game acceptance')))

if __name__=='__main__':main()
