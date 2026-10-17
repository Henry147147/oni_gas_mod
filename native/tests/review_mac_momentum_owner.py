"""Saved-only review of reconstructed owning MAC steps and atomic intervals."""
import copy
from decimal import Decimal as D,localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec=importlib.util.spec_from_file_location('momentum_gravity_saved',Path(__file__).with_name('review_mac_gravity.py'))
gravity=importlib.util.module_from_spec(spec);spec.loader.exec_module(gravity)
closed=gravity.closed;base=closed.base
check,close,number,unique,dec=base.check,base.close,base.number,base.unique,closed.dec
spec=importlib.util.spec_from_file_location('owning_work_pair_saved',Path(__file__).with_name('review_mac_momentum_reconstruction.py'))
pair_reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(pair_reference)

def full(r,i):
    m=number(r[i]);e=int(r[i+1])
    check((m==0 and e==0) or F(1,2)<=abs(m)<1,'Owner full scalar shape')
    return base.scaled_energy(r[i],r[i+1])

def parse(raw):
    d=dict(states={},masses={},passives={},faces={},gravity={},gfaces={},gcells={},interval=None,rollback=None,trace=None,
        workpairs={},workterms={},work_invalid=None)
    common=[]
    for line in raw.decode('ascii').splitlines():
        r=line.split();check(bool(r),'Empty owner record');kind=r[0]
        if kind=='OSTATE':
            check(len(r)==4,'Owner cell shape');unique(d['states'],tuple(map(int,r[1:3])),number(r[3]))
        elif kind in ('OMASS','OPASSIVE'):
            check(len(r)==5,'Owner inventory shape');v=number(r[4]);check(v>0,'Owner positive inventory')
            unique(d['masses' if kind=='OMASS' else 'passives'],tuple(map(int,r[1:4])),v)
        elif kind=='OFACE':
            check(len(r)==7,'Owner face shape');unique(d['faces'],tuple(map(int,r[1:4])),(number(r[4]),full(r,5)))
        elif kind=='OINTERVAL':
            check(len(r)==18 and d['interval'] is None,'Owner interval shape/duplicate')
            d['interval']=(*map(number,r[1:4]),*map(int,r[4:12]),full(r,12),full(r,14),number(r[16]),number(r[17]))
        elif kind=='OROLLBACK':
            check(len(r)==5 and d['rollback'] is None,'Rollback shape/duplicate');d['rollback']=(int(r[1]),int(r[2]),number(r[3]),int(r[4]))
        elif kind=='OTRACE':
            check(len(r)==3 and d['trace'] is None,'Owner trace shape/duplicate');d['trace']=tuple(map(int,r[1:]))
        elif kind=='OWORK':
            check(len(r)==35,'Work capacity pair shape');unique(d['workpairs'],int(r[1]),(number(r[2]),number(r[3]),*map(int,r[4:7]),*[full(r,i) for i in range(7,35,2)]))
        elif kind=='OWORKTERM':
            check(len(r)==6,'Actual work expansion shape');unique(d['workterms'],tuple(map(int,r[1:4])),full(r,4))
        elif kind=='OWORK_INVALID':
            check(len(r)==2 and d['work_invalid'] is None,'Work invalid control shape');d['work_invalid']=int(r[1])
        elif kind=='GCASE':
            check(len(r)==11,'Owner gravity shape');unique(d['gravity'],int(r[1]),tuple(map(number,r[2:])))
        elif kind in ('GFACE','GUCELL'):
            check(len(r)==6,'Owner gravity operand shape');unique(d['gfaces' if kind=='GFACE' else 'gcells'],tuple(map(int,r[1:3])),tuple(map(number,r[3:])))
        else:common.append(line)
    d['ordinary']=closed.parse(('\n'.join(common)+'\n').encode('ascii'),required_passes=7,require_acoustic=False,
        required_cases=tuple(range(6)),required_pressure_cases=tuple(range(6)))
    check(d['interval'] is not None and d['rollback']==(2,1,F(0),1) and d['trace'] is not None and set(d['gravity'])=={1},'Missing owner groups')
    return d

def review(d):
    pair_reference.QUADRATIC=d['ordinary'][0][0]['momentum_mode']==2
    check(set(d['workpairs'])==set(range(4)) and d['work_invalid']==1,'Missing actual work capacity controls')
    check(all(k in d['workpairs'] and side in (0,1) and i>=0 for k,side,i in d['workterms']),'Orphan work expansion')
    for k,row in d['workpairs'].items():
        il,ir,backoffs,unresolved_j,unresolved_q,*p=row;budget=[]
        for side in (0,1):
            terms={i:v for (key,s,i),v in d['workterms'].items() if key==k and s==side}
            check(set(terms)==set(range(len(terms))),'Work expansion component coverage');budget.append(sum(terms.values(),F(0)))
        before=pair_reference.CHECKS;pair_reference.review_pair(il,ir,p,work_budget=tuple(budget));base.CHECKS+=pair_reference.CHECKS-before
        check(il==ir==1 and p[:4]==[F(1),F(1),F(1,2),F(1,2)] and p[4]==(-1 if k==1 else 1),'Declared work capacity operands')
        check(0<=backoffs<=64 and 0<=unresolved_j<=2 and 0<=unresolved_q<=2,'Work capacity precision diagnostics')
        if k<2:
            check(backoffs>0 and p[5]<1 and budget==([1-F(2)**-54,F(1)] if k==0 else [F(1),1-F(2)**-54]),'Rounded-up capacity control')
        elif k==2:check(budget==[F(3,4)*F(2)**-1200,F(1)] and 0<p[5]<F(2)**-1074 and p[6]>0 and unresolved_j>0,'Full-range actual work capacity')
        else:check(budget==[F(0),F(1)] and p[5]==p[6]==p[11]==0,'Zero actual work capacity')
    check(d['workpairs'][0][10]==d['workpairs'][1][10],'Reflected work limiter differs')
    cases,materials,pcases,pcells,pfaces,_,_=d['ordinary'];trace,passive=d['trace']
    check(trace in materials and passive>=0,'Owner trace identity')
    for identifier,c in cases.items():
        check(c['momentum_mode']==(0 if identifier==4 else cases[0]['momentum_mode']) and cases[0]['momentum_mode'] in (1,2),
            'Declared corrected/donor owner mode')
        for i,wall in c['walls'].items():
            if not wall:
                for e in (0,1):check(c['masses'].get((e,i,trace),0)>0 and c['passives'].get((e,i,passive),0)>0,'Owner lost positive constituent/passive trace')
    for identifier in (0,1):check(all(cases[identifier]['mreports'][a][0]>0 for a in (0,1)),'Missing owning normal/transverse correction')
    tiny=cases[5]
    check(tiny['mreports'][0][0]>0 and tiny['mreports'][0][2]>0 and tiny['wide_report'] is not None and tiny['wide_report'][1]>0,
        'Full-range owning correction or unresolved material heat missing')
    check(any(0<abs(v[13])<F(2)**-1074 for v in tiny['mevents'].values()),'No below-binary64 applied owning correction')
    dt,duration,time,revision,attempts,substeps,rejected,events,limited,uj,uq,residual,bound,entropy,entropy_bound=d['interval']
    check(dt>0 and duration==2*dt and time==duration and revision==2 and attempts==substeps==2 and rejected==0,'Atomic owning clock/revision/attempts')
    expected=tuple(sum(cases[k]['mreports'][a][i] for k in (2,3) for a in (0,1)) for i in range(4))
    check((events,limited,uj,uq)==expected and events>0,'Committed correction diagnostics')
    check(all(pcases[k][0]==dt for k in (2,3)),'Accepted owning substep times')
    # Material and pressure operands stay identical when only momentum mode changes.
    for k in ('masses','passives'):
        check(cases[0][k]==cases[4][k],'Donor/default changed actual material transfers')
    for key,row in cases[0]['faces'].items():
        other=cases[4]['faces'][key]
        check(row[0]==other[0] and row[1]==other[1] and row[3:5]==other[3:5] and row[9:11]==other[9:11],
            'Donor/default changed pressure momentum, primal mass, inertia or wall reaction')
    check(any(row[2]!=cases[4]['faces'][key][2] for key,row in cases[0]['faces'].items()),'Donor/default did not change actual momentum')
    for i in cases[0]['walls']:
        check(pcells[0,i][:5]==pcells[4,i][:5] and cases[0]['cells'][0,i]==cases[4]['cells'][0,i],'Donor/default changed pressure work')
    for identifier in cases:
        if identifier==1:
            gravity.review_case(1,(d['ordinary'],d['gravity'],d['gfaces'],d['gcells'],{},{}))
        else:
            base.review_case(cases[identifier]);closed.review_pressure(identifier,d['ordinary'])
    nc=cases[2]['w']*cases[2]['h'];pairs=base.topology(cases[2])
    check(set(d['states'])=={(e,i) for e in (0,1) for i in range(nc)},'Actual owner state coverage')
    check(set(d['faces'])=={(e,a,i) for e in (0,1) for a in (0,1) for i in range(len(pairs[a]))},'Actual owner face coverage')
    for field in ('masses','passives'):
        expected_keys={(e,i,s) for e,k in ((0,2),(1,3)) for endpoint,i,s in cases[k][field] if endpoint==e}
        check(set(d[field])==expected_keys,'Actual owner inventory coverage')
        for (e,i,s),v in d[field].items():check(v==cases[2 if e==0 else 3][field][e,i,s],'Actual owner inventory differs from primitive replay')
        for i,s in {(i,s) for _,i,s in d[field]}:
            check(cases[2][field].get((1,i,s),0)==cases[3][field].get((0,i,s),0),'Material continuity across accepted substeps')
        for s in {s for _,_,s in d[field]}:
            before=sum(v for (e,_,k),v in d[field].items() if e==0 and k==s)
            after=sum(v for (e,_,k),v in d[field].items() if e==1 and k==s)
            close(after,before,abs(after)+abs(before),'Actual owner constituent/passive balance')
    energy_terms=[];entropy_expected=D(0);entropy_scale=D(0);entropy_allowance=D(0)
    volume=pcases[2][1]**2*pcases[2][2]
    for i in range(nc):
        check(d['states'][0,i]==pcells[2,i][0] and d['states'][1,i]==cases[3]['cells'][1,i] and
            cases[2]['cells'][1,i]==pcells[3,i][0],'Actual owner/accepted primitive U continuity')
        for e in (0,1):
            u=d['states'][e,i];energy_terms.append(u if e else -u)
            m={s:v for (endpoint,k,s),v in d['masses'].items() if endpoint==e and k==i}
            eos=closed.equilibrium(m,materials,u,volume)
            entropy_expected+=eos[3] if e else -eos[3];entropy_scale+=abs(eos[3]);entropy_allowance+=dec(128*base.EPS*abs(u))/eos[1]
            check(m.get(trace,0)>0 and d['passives'].get((e,i,passive),0)>0,'Actual owner positive traces')
    for a in (0,1):
        momentum=[]
        for i,adjacent in enumerate(pairs[a]):
            source_j=pfaces[2,a,i][0];between=cases[2]['faces'][a,i][2]
            check(between==pfaces[3,a,i][0],'Canonical momentum continuity across owning substeps')
            check(d['faces'][0,a,i][1]==source_j and d['faces'][1,a,i][1]==cases[3]['faces'][a,i][2],'Actual owner canonical J differs from replay')
            for e in (0,1):
                inertia,j=d['faces'][e,a,i]
                mass=sum(v/2 for (endpoint,k,_),v in d['masses'].items() if endpoint==e and k in adjacent)
                close(inertia,mass,inertia+mass,'Actual owner half-cell inertia')
                kinetic=j*j/(2*inertia) if inertia else 0
                energy_terms.append(kinetic if e else -kinetic);momentum.append(j if e else -j)
        close(sum(momentum,F(0)),0,sum(map(abs,momentum)),'Actual periodic owner momentum')
    actual=sum(energy_terms,F(0));scale=sum(map(abs,energy_terms),F(0))
    check(abs(actual)<=bound,'Actual owning U+canonical K exceeds composed bound')
    close(residual,actual,scale,'Actual owning endpoint residual',2)
    stage_bound=pcases[2][6]+pcases[3][6];close(bound,stage_bound,bound+stage_bound,'Accumulated accepted stage bound')
    check(abs(dec(entropy)-entropy_expected)<=dec(entropy_bound)+1024*dec(base.EPS)*entropy_scale,'Actual owning endpoint entropy')
    check(abs(dec(entropy_bound)-entropy_allowance)<=1024*dec(base.EPS)*entropy_allowance,'Original owning endpoint entropy bound')

def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=2097152,'Owner full-record pipe cap')
    with localcontext() as context:
        context.prec=80;data=parse(raw);review(data);checks=base.CHECKS
        mutations=[]
        for kind in ('geometry','work','trace','clock','capacity'):
            bad=copy.deepcopy(data);cases=bad['ordinary'][0]
            if kind in ('geometry','work'):
                key=next(iter(cases[0]['mevents']));row=list(cases[0]['mevents'][key]);row[5 if kind=='geometry' else 18]*=F(101,100);cases[0]['mevents'][key]=tuple(row)
            elif kind=='trace':
                trace=bad['trace'][0];key=next(k for k in cases[0]['masses'] if k[0]==1 and k[2]==trace);cases[0]['masses'][key]*=F(101,100)
            elif kind=='clock':
                row=list(bad['interval']);row[2]+=row[0];bad['interval']=tuple(row)
            else:
                for key in list(bad['workterms']):
                    if key[:2]==(0,0):del bad['workterms'][key]
                bad['workterms'][0,0,0]=bad['workpairs'][0][16]/2
            try:review(bad)
            except (ValueError,KeyError):mutations.append(kind)
            else:raise ValueError('Accepted corrupted owning '+kind)
    out=dict(Passed=True,Groups=7,SavedChecks=checks,CorruptionsRejected=mutations,SavedSha256=hashlib.sha256(raw).hexdigest(),
        NativeProcessLaunched=False,IndependentAgentReview=False,Scope='Reconstructed occupied MAC owner only; original strong-flow gate and real-game acceptance separate')
    with Path(sys.argv[2]).open('x') as f:json.dump(out,f,indent=2)
    print(json.dumps(out))
if __name__=='__main__':main()
