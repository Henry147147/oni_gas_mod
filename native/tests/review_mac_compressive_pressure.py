"""Saved-only nodal compressive pressure/work/volume and constitutive review."""
from decimal import localcontext
from fractions import Fraction as F
from pathlib import Path
import copy,hashlib,importlib.util,json,sys

spec=importlib.util.spec_from_file_location('constitutive',Path(__file__).with_name('review_mac_compressive_work.py'))
law=importlib.util.module_from_spec(spec);spec.loader.exec_module(law)
base=law.base;check,close,number,unique=base.check,base.close,base.number,base.unique;EPS=base.EPS

def parse(raw):
    common=[];scenes={};nodes={};mapping={};full={}
    for line in raw.decode('ascii').splitlines():
        r=line.split();check(bool(r),'Empty coupled work record');kind=r[0]
        if kind.startswith('CW') or kind=='PASS':common.append(line)
        elif kind=='CPCASE':
            check(len(r)==11 and r[4] in ('0','1'),'Coupled case shape')
            unique(scenes,int(r[1]),(*map(int,r[2:5]),number(r[5]),*map(int,r[6:9]),*map(number,r[9:11])))
        elif kind=='CPNODE':
            check(len(r)==11 and r[9] in ('0','1'),'Coupled node shape')
            unique(nodes,tuple(map(int,r[1:3])),(*map(int,r[3:5]),*map(number,r[5:9]),int(r[9]),number(r[10])))
        elif kind=='CPMAP':
            check(len(r)==5,'Constitutive mapping shape');unique(mapping,tuple(map(int,r[1:3])),(int(r[3]),number(r[4])))
        elif kind=='CPFULL':
            check(len(r)==14,'Full coupled balance shape');unique(full,int(r[1]),tuple(base.scaled_energy(r[i],r[i+1]) for i in range(2,14,2)))
        else:raise ValueError('Unknown coupled pressure record')
    check(set(scenes)==set(full)==set(range(6)),'Missing coupled cases or accounts')
    check(set(nodes)=={(s,i) for s,scene in scenes.items() for i in range(scene[1])},'Coupled node coverage differs')
    check(set(mapping)=={(s,i) for s,scene in scenes.items() for i in range(scene[0])},'Coupled material coverage differs')
    check(len(mapping)==15 and {m[0] for m in mapping.values()}==set(range(15)),'Constitutive responses are duplicated or orphaned')
    constitutive=law.parse(('\n'.join(common)+'\n').encode('ascii'),case_ids=tuple(range(15)),profiles=(0,),groups=6,rejections=5,large_offset_control=False)
    return constitutive,scenes,nodes,mapping,full

def review(data):
    d,scenes,nodes,mapping,full=data;law.review(d,fixture_controls=False)
    for s,scene in scenes.items():
        n,nf,periodic,dt,iterations,linear_iterations,active,wall_report,body_report=scene
        check(n==(3 if s==4 else 4 if s==5 else 2) and nf==(n if periodic else n+1) and periodic==int(s==5),'Declared coupled topology differs')
        check(dt==F(float(.0001)) and min(iterations,linear_iterations,active)>=0,'Coupled time or iteration fields differ')
        ids=[mapping[s,i][0] for i in range(n)];errors={i:d['cases'][c][2] for i,c in enumerate(ids)}
        mass={i:sum((v[0] for (c,_),v in d['masses'].items() if c==ids[i]),F(0)) for i in range(n)}
        work={i:F(float(d['full'][c][3])) for i,c in enumerate(ids)}
        energy=[];momentum=[];body=[];walls=[];volumes={i:[] for i in range(n)}
        entropy=[];entropy_allowance=[]
        for i,c in enumerate(ids):
            profile,v,dv,h,local_dt,coefficient,_=d['cases'][c]
            check(profile==0 and v==h==1 and local_dt==dt and coefficient==(10 if s==3 else 50),'Frozen material coefficient or geometry differs')
            energy.extend((d['end'][c][1],-d['initial'][c][0]));entropy.append(d['full'][c][8]);entropy_allowance.append(d['full'][c][9])
        for f in range(nf):
            l,r,area,inertia,u0,acceleration,fixed,u1=nodes[s,f]
            expected_l=(f+n-1)%n if f or periodic else -1;expected_r=f if f<n else -1
            check((l,r)==(expected_l,expected_r) and area==1 and inertia>0 and fixed==int(not periodic and f in (0,n)),'Node topology or physical metric differs')
            expected_inertia=sum((mass[i]/2 for i in (l,r) if i>=0),F(0))
            close(inertia,expected_inertia,abs(inertia)+abs(expected_inertia),'Node half-cell inertia differs')
            check(acceleration==(F(float(-9.81)) if s==4 and not fixed else 0),'Body acceleration differs')
            force=area*(work.get(l,F(0))-work.get(r,F(0)))+inertia*acceleration
            expected=F(0) if fixed else u0+dt*force/inertia
            close(u1,expected,abs(u0)+abs(dt*force/inertia)+abs(u1),'Nodal velocity is not driven by the complete pressure stress')
            check(not fixed or u0==u1==0,'Fixed node moved')
            flow=dt*area*(u0+u1)/2
            if l>=0:errors[l]-=flow;volumes[l].append(-flow)
            if r>=0:errors[r]+=flow;volumes[r].append(flow)
            energy.extend((inertia*u1*u1/2,-inertia*u0*u0/2,-dt*inertia*acceleration*(u0+u1)/2))
            body.append(dt*inertia*acceleration*(u0+u1)/2)
            momentum.extend((inertia*u1,-inertia*u0,-dt*inertia*acceleration))
            if fixed:
                walls.append(-dt*force);momentum.append(dt*force)
        for i,c in enumerate(ids):
            reported=mapping[s,i][1];v1=d['end'][c][0];dv=d['cases'][c][2]
            close(reported,errors[i],abs(dv)+sum(map(abs,volumes[i]),F(0)),'Reported root volume residual differs')
            check(abs(reported)<=16*EPS*v1 and abs(errors[i])<=32*EPS*v1,'Actual midpoint geometry exceeds its original cell-volume budget')
        er,eb,mr,mb,ds,sb=full[s];es=sum(map(abs,energy),F(0));ms=sum(map(abs,momentum),F(0))
        close(er,sum(energy,F(0)),es,'Reported coupled actual U+K/body residual differs')
        close(eb,128*EPS*es,eb,'Coupled energy operand bound differs',8)
        check(abs(sum(energy,F(0)))<=eb and abs(er)<=eb,'Actual material U plus nodal K/body work fails')
        close(mr,sum(momentum,F(0)),ms,'Reported coupled actual momentum residual differs')
        close(mb,128*EPS*ms,mb,'Coupled momentum operand bound differs',8)
        check(abs(sum(momentum,F(0)))<=mb and abs(mr)<=mb,'Actual nodal momentum/wall/body account fails')
        close(wall_report,sum(walls,F(0)),sum(map(abs,walls),F(0)),'Wall reaction omits compressive stress')
        close(body_report,sum(body,F(0)),sum(map(abs,body),F(0)),'Body-work report differs')
        close(ds,sum(entropy,F(0)),sum(map(abs,entropy),F(0)),'Coupled actual entropy report differs')
        close(sb,sum(entropy_allowance,F(0)),sum(entropy_allowance,F(0)),'Coupled stored-U entropy allowance differs',8)
        check(ds>=-sb,'Coupled actual entropy decreases')
        check(active==sum(d['full'][c][2]>0 for c in ids),'Active dissipative response count differs')
        if s==5:
            check(iterations==linear_iterations==active==0 and all(nodes[s,f][-1]==nodes[s,f][4]==3 for f in range(nf)),
                  'Uniform translation has false force or work')
            check(all(d['cases'][c][2]==0 and d['initial'][c][0]==d['end'][c][1] for c in ids),'Uniform translation changes material energy or volume')
        else:check(iterations>0 and active>0,'Coupled nonlinear stress was not exercised')
    for i in range(2):
        a=mapping[0,i][0];b=mapping[1,1-i][0]
        close(d['end'][a][1],d['end'][b][1],abs(d['end'][a][1])+abs(d['end'][b][1]),'Reflected coupled material differs')
    close(nodes[0,1][-1],-nodes[1,1][-1],abs(nodes[0,1][-1])+abs(nodes[1,1][-1]),'Reflected coupled node differs')

def main():
    if len(sys.argv)!=3:raise SystemExit('Usage: review_mac_compressive_pressure.py <saved.raw> <fresh-report.json>')
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=1048576,'Coupled pressure raw cap exceeded')
    with localcontext() as context:
        context.prec=80;data=parse(raw);review(data);saved=base.CHECKS;rejected=0
        for kind in ('node','geometry','heat','trace'):
            d=copy.deepcopy(data)
            if kind=='node':v=list(d[2][0,1]);v[-1]+=F(1,10);d[2][0,1]=tuple(v)
            elif kind=='geometry':c,r=d[3][0,0];d[3][0,0]=(c,r+1)
            elif kind=='heat':
                key=next(c for c,v in d[0]['full'].items() if v[2]>0)
                v=list(d[0]['full'][key]);v[2]*=2;d[0]['full'][key]=tuple(v)
            else:
                key=next(k for k,v in d[0]['masses'].items() if k[0]==0 and v[0]<F(1,10**20));before,after=d[0]['masses'][key];d[0]['masses'][key]=(before,after*2)
            check(d!=data,'Corruption control did not change its saved operand')
            try:review(d)
            except ValueError:rejected+=1
            else:raise ValueError('Corrupted coupled '+kind+' admitted')
    result=dict(Passed=True,SavedChecks=saved,Scenes=6,Responses=15,CorruptionsRejected=rejected,
                SavedSha256=hashlib.sha256(raw).hexdigest(),NativeProcessLaunched=False,IndependentAgentReview=False,
                Scope='Coupled local nodal pressure and material work; no Eulerian owning interval or game acceptance')
    with Path(sys.argv[2]).open('x') as output:json.dump(result,output,indent=2);output.write('\n')
    print(json.dumps(result))

if __name__=='__main__':main()
