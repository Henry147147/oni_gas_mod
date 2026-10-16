"""Read saved gravity operands only; no native process or native EOS calls."""
import copy
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location('closed_saved',Path(__file__).with_name('review_mac_closed_world.py'))
closed = importlib.util.module_from_spec(spec); spec.loader.exec_module(closed)
base=closed.base
check,close,number,unique,dec=base.check,base.close,base.number,base.unique,closed.dec
EPS=base.EPS


def parse(raw):
    common=[]; gravity={}; faces={}; cells={}; hydro={}; waves={}; samples={}
    extra={'GCASE','GFACE','GUCELL','HYDRO','GMODE','GSAMPLE'}
    for line in raw.decode('ascii').splitlines():
        r=line.split(); check(bool(r),'Empty record')
        if r[0] not in extra:
            common.append(line); continue
        if r[0]=='GCASE':
            check(len(r)==11,'Gravity case shape'); unique(gravity,int(r[1]),tuple(map(number,r[2:])))
        elif r[0] in ('GFACE','GUCELL'):
            check(len(r)==6,'Gravity face/cell shape'); unique(faces if r[0]=='GFACE' else cells,tuple(map(int,r[1:3])),tuple(map(number,r[3:])))
        elif r[0]=='HYDRO':
            check(len(r)==5,'Hydro shape'); unique(hydro,int(r[1]),tuple(map(number,r[2:])))
        elif r[0]=='GMODE':
            check(len(r)==15,'Gravity mode shape'); unique(waves,int(r[1]),tuple(map(number,r[2:])))
        else:
            check(len(r)==16,'Gravity sample shape'); unique(samples,tuple(map(int,r[1:3])),tuple(map(number,r[3:])))
    ordinary=closed.parse(('\n'.join(common)+'\n').encode('ascii'),required_passes=4,require_acoustic=False)
    check(set(gravity)=={0,1} and set(hydro)=={0,1,2} and set(waves)=={32,64,128},'Missing gravity cases')
    check(set(samples)=={(n,y) for n in waves for y in range(n)},'Missing/extra gravity profiles')
    for speed,res,bound in hydro.values(): check(0<=speed<F(1,10**7) and abs(res)<=bound,'Hydrostatic rest/energy')
    return ordinary,gravity,faces,cells,waves,samples


def review_case(identifier,data):
    ordinary,gravity,gfaces,gcells,_,_=data
    cases,materials,pcases,pcells,pfaces,_,_=ordinary; case=cases[identifier]
    base.review_case(case); pairs=base.topology(case)
    g,gr,gb,pr,pb,body_impulse,change,saved_entropy,entropy_bound=gravity[identifier]
    dt,spacing,depth,pressure_res,pressure_bound,total_res,total_bound,wx,wy=pcases[identifier]
    volume,area=spacing*spacing*depth,spacing*depth; nc=len(case['walls'])
    check(g>0 and set(k[1] for k in gfaces if k[0]==identifier)==set(range(len(pairs[1]))),'Gravity face coverage')
    check(set(k[1] for k in gcells if k[0]==identifier)==set(range(nc)),'Gravity cell coverage')
    masses=[{s:m for (end,c,s),m in case['masses'].items() if end==e and c==i} for e in (0,1) for i in range(nc)]
    oldm=[sum(m.values(),F(0)) for m in masses[:nc]]; newm=[sum(m.values(),F(0)) for m in masses[nc:]]
    correction=[[] for _ in range(nc)]; sweeps=[F(0) for _ in range(nc)]
    pressure_terms=[]; total_terms=[]; potential_terms=[]; gravity_terms=[]; declared_changes=[]; body_terms=[]
    for axis in (0,1):
        momentum=[]; reactions=[]
        for f,adjacent in enumerate(pairs[axis]):
            row=case['faces'][axis,f]; flux,jp,jnew,i0,i1=row[:5]; jold,q,helper=pfaces[identifier,axis,f]
            pl=F(0) if adjacent[0] is None else pcells[identifier,adjacent[0]][3]
            pt=F(0) if adjacent[1] is None else pcells[identifier,adjacent[1]][3]
            bi=F(0) if axis==0 else -dt*i0*g; impulse=dt*area*(pl-pt)+bi
            fixed=None in adjacent
            close(jp,F(0) if fixed else jold+impulse,abs(jold)+abs(impulse)+abs(jp),'Gravity/pressure canonical impulse',256)
            if fixed:
                check(jold==jp==q==0,'Fixed gravity normal moved'); reactions.append(-impulse)
            else:
                expect_q=dt*area*(jold+jp)/(2*i0); close(q,expect_q,abs(q)+abs(expect_q),'Gravity midpoint sweep')
                sweeps[adjacent[0]]+=q; sweeps[adjacent[1]]-=q
                if q:
                    donor=adjacent[0] if q>0 else adjacent[1]; expect_f=q*oldm[donor]/pcells[identifier,donor][1]
                    close(flux,expect_f,abs(flux)+abs(expect_f),'Gravity material donor density')
                else: check(flux==0,'Mass flux without gravity sweep')
            momentum.extend((jp,-jold,-bi,impulse if fixed else F(0)))
            kold=jold*jold/(2*i0) if i0 else F(0)
            pressure_terms.extend((row[5],-kold)); total_terms.extend((row[6],-kold))
            if axis==1:
                work,pe,corr=gfaces[identifier,f]; body_terms.append(bi)
                expect_work=bi*(jold+jp)/(2*i0) if i0 else F(0)
                close(work,expect_work,abs(work)+abs(expect_work),'Represented gravity body work')
                close(pe,flux*g*spacing,abs(pe)+abs(flux*g*spacing),'Actual-mass potential flux')
                close(corr,-work-pe,abs(work)+abs(pe)+abs(corr),'Signed internal gravity work')
                pressure_terms.append(-work); potential_terms.append(-pe); gravity_terms.extend((work,pe))
                mass=sum((newm[c] for c in adjacent if c is not None),F(0))
                for c in adjacent:
                    if c is not None and mass: correction[c].append(corr*newm[c]/mass)
        close(sum(momentum,F(0)),F(0),sum(map(abs,momentum),F(0)),'Gravity momentum plus body/wall',256)
        close(wx if axis==0 else wy,sum(reactions,F(0)),sum(map(abs,reactions),F(0)),'Gravity pressure wall impulse')
    close(body_impulse,sum(body_terms,F(0)),sum(map(abs,body_terms),F(0)),'Reported body impulse')
    entropy0=entropy1=D(0); entropy_scale=D(0)
    for i in range(nc):
        pre,final,declared=gcells[identifier,i]
        check(pre==case['cells'][1,i],'Transport/gravity source U disagree')
        close(declared,sum(correction[i],F(0)),sum(map(abs,correction[i]),F(0)),'Cell gravity work shares')
        close(final,pre+declared,abs(final)+abs(pre)+abs(declared),'Stored gravity U addition')
        gravity_terms.extend((final,-pre)); declared_changes.append(declared)
        phi=g*(F(i//case['w'])+F(1,2))*spacing
        potential_terms.extend((newm[i]*phi,-oldm[i]*phi)); total_terms.extend((newm[i]*phi,-oldm[i]*phi))
        if case['walls'][i]: continue
        u0,vlag,dv,work,pend,pfinal,tfinal,s0,s1,geo=pcells[identifier,i]
        ulag=case['cells'][0,i]
        close(vlag,volume+dv,vlag+volume+abs(dv),'Gravity pressure volume addition',32)
        close(vlag-volume-sweeps[i],F(0),vlag+volume+abs(sweeps[i]),'Gravity fixed-grid volume closure')
        close(geo,vlag-volume-sweeps[i],vlag+volume+abs(sweeps[i]),'Gravity geometric residual')
        close(ulag,u0-work*dv,abs(ulag)+abs(u0)+abs(work*dv),'Gravity isentropic stored U')
        expected_work=closed.isentropic_work(masses[i],materials,u0,volume,dv)
        check(abs(dec(work)-expected_work)<=D('5e-10')*abs(expected_work),'Independent gravity pressure work')
        e0=closed.equilibrium(masses[i],materials,u0,volume)
        elag=closed.equilibrium(masses[i],materials,ulag,vlag)
        e1=closed.equilibrium(masses[nc+i],materials,final,volume)
        for actual,expected in ((pend,elag[0]),(pfinal,e1[0]),(tfinal,e1[1])):
            check(abs(dec(actual)-expected)<=D('5e-10')*abs(expected),'Independent gravity EOS')
        entropy0+=e0[3]; entropy1+=e1[3]; entropy_scale+=abs(e0[3])+abs(e1[3])
        pressure_terms.extend((ulag,-u0)); total_terms.extend((final,-u0))
    for actual,terms,why in ((pressure_res,pressure_terms,'pressure'),(gr,gravity_terms,'gravity internal'),(pr,potential_terms,'potential')):
        residual=sum(terms,F(0)); scale=sum(map(abs,terms),F(0))
        close(residual,F(0),scale,'Actual '+why+' balance')
        close(actual,residual,scale,'Saved '+why+' residual',2)
    for actual,terms in ((pressure_bound,pressure_terms),(gb,gravity_terms),(pb,potential_terms)):
        expect=128*EPS*sum(map(abs,terms),F(0)); close(actual,expect,expect,'Saved local energy bound',1024)
    close(change,sum(declared_changes,F(0)),sum(map(abs,declared_changes),F(0)),'Total signed gravity adjustment')
    expect_bound=pressure_bound+case['bound']+gb+pb; close(total_bound,expect_bound,expect_bound,'Composed gravity bound')
    residual=sum(total_terms,F(0)); check(abs(residual)<=total_bound,'Actual gravity total U+K+PE')
    close(total_res,residual,sum(map(abs,total_terms),F(0)),'Reported gravity total residual',2)
    actual_entropy=entropy1-entropy0
    check(abs(dec(saved_entropy)-actual_entropy)<=dec(entropy_bound)+dec(1024*EPS)*entropy_scale,'Independent gravity entropy change')
    check(actual_entropy>=-dec(entropy_bound),'Gravity operand case loses material entropy')


def review_modes(data):
    ordinary,_,_,_,waves,samples=data; materials=ordinary[1]
    nitrogen=next(i for i,m in materials.items() if m[0]==0 and F(28,1000)<m[1]<F(29,1000))
    helium=next(i for i,m in materials.items() if m[0]==0 and m[1]<F(5,1000))
    rspec=dec(closed.R/materials[nitrogen][1]); gamma=1+rspec/dec(materials[nitrogen][2]); sound=(gamma*rspec*300).sqrt()
    pi=base.PI; omega=sound*(pi*pi+D('.25')).sqrt()
    def integral(y,sine):
        s,c=base.sincos(pi*y)
        return (-D('.5')*s-pi*c if sine else -D('.5')*c+pi*s)*(-y/2).exp()/(D('.25')+pi*pi)
    for n,v in waves.items():
        g,amp,c,duration,dt,steps,rejected,perr,uerr,entropy,rounding,res,bound=v
        check(steps.denominator==rejected.denominator==1 and n//4<=steps<=n//4+2 and rejected==0,'Stratified nominal step/count path')
        check(abs(res)<=bound and bound>0,'Stratified energy bound')
        check(abs(dec(c)-sound)<D('1e-12')*sound and abs(dec(duration)-pi/(2*omega))<D('1e-12')*dec(duration),'Independent stratified frequency/time')
        close(g,closed.R/materials[nitrogen][1]*300,g,'Stratified scale height')
        errors=[D(0),D(0)]; s0=s1=D(0); mass=F(0); total_energy=[]
        for y in range(n):
            mn0,mhe0,u0,mn1,mhe1,u1,j0,j1,p0,p1,t1,_,_=samples[n,y]
            check(min(mn0,mhe0,mn1,mhe1)>0,'Stratified bulk/trace positivity')
            volume=F(1,n*n)
            e0=closed.equilibrium({nitrogen:mn0,helium:mhe0},materials,u0,volume)
            e1=closed.equilibrium({nitrogen:mn1,helium:mhe1},materials,u1,volume)
            for actual,expect in ((p0,e0[0]),(p1,e1[0]),(t1,e1[1])):
                check(abs(dec(actual)-expect)<=D('5e-10')*abs(expect),'Independent stratified EOS reading')
            s0+=e0[3]; s1+=e1[3]; mass+=mn0+mhe0
            left,right=D(y)/n,D(y+1)/n
            expected=dec(amp)/omega*n*((dec(g)-sound*sound/2)*(integral(right,True)-integral(left,True))-
                sound*sound*pi*(integral(right,False)-integral(left,False)))
            errors[0]+=abs((dec(p1-p0)-expected)/(sound*dec(amp)))/n
            mi0=(mn0+mhe0)/2; mi1=(mn1+mhe1)/2
            if y:
                a=samples[n,y-1]; mi0+=(a[0]+a[1])/2; mi1+=(a[3]+a[4])/2
            errors[1]+=abs(dec(j1/mi1))/dec(amp)/n
            phi=g*F(2*y+1,2*n)
            total_energy.extend((u1,-u0,j1*j1/(2*mi1),-j0*j0/(2*mi0),(mn1+mhe1)*phi,-(mn0+mhe0)*phi))
        for actual,expected in zip((perr,uerr),errors): check(abs(dec(actual)-expected)<D('2e-7'),'Stratified row/report continuous mode errors')
        independent_entropy=(s1-s0)/dec(mass)
        check(abs(dec(entropy)-independent_entropy)<=dec(rounding)+D('1e-10'),'Stratified independent specific entropy')
        check(abs(2*sum(total_energy,F(0)))<=bound,'Stratified saved U+Y-K+PE conservation')
    for a,b in ((32,64),(64,128)):
        check(all(0<waves[b][j]<F(7,10)*waves[a][j] for j in (7,8)),'Joint stratified p/u refinement')
        check(abs(waves[b][9])<F(4,5)*abs(waves[a][9]),'Stratified specific entropy convergence')
    check(waves[128][7]<F(1,50) and waves[128][8]<F(1,50) and abs(waves[128][9])<F(1,10**6),'Finest stratified absolute limits')


def review(data):
    for i in (0,1): review_case(i,data)
    review_modes(data)


def main():
    raw=Path(sys.argv[1]).read_bytes(); check(len(raw)<=131072,'Gravity saved pipe cap')
    with localcontext() as context:
        context.prec=80; data=parse(raw); review(data); checks=base.CHECKS
        for control in ('mass-potential','stored-gravity-work','stratified-profile'):
            bad=copy.deepcopy(data)
            if control=='mass-potential':
                key=next(k for k,v in bad[2].items() if v[1]); v=list(bad[2][key]); v[1]*=F(101,100); bad[2][key]=tuple(v)
            elif control=='stored-gravity-work':
                key=(1,0); pre,final,decl=bad[3][key]; bad[3][key]=(pre,final+1,decl)
            else:
                v=list(bad[5][32,0]); v[9]+=10; bad[5][32,0]=tuple(v)
            caught=False
            try: review(bad)
            except ValueError: caught=True
            check(caught,'Altered gravity result was accepted')
    print(json.dumps(dict(Passed=True,SavedSha256=hashlib.sha256(raw).hexdigest(),SavedChecks=checks,CorruptionsRejected=3,
        NativeProcessLaunched=False,IndependentAgentReview=False,Scope='Occupied MAC gravity conservation and smooth stratified controls; no production/game acceptance')))


if __name__=='__main__': main()
