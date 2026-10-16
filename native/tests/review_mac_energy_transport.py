"""Saved-state checks for wide derived-energy transport and a tiny closed step."""
import copy
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec=importlib.util.spec_from_file_location('wide_mac_saved',Path(__file__).with_name('review_mac_transport.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
check,close,number,unique=base.check,base.close,base.number,base.unique
EPS=base.EPS;R=F(float('8.31446261815324'))
def dec(f):return D(f.numerator)/D(f.denominator)
def parse(raw):
    common=[];materials={};pressure={};cells={};faces={};interval=None
    for line in raw.decode('ascii').splitlines():
        row=line.split();check(bool(row),'Empty wide-transport record');kind=row[0]
        if kind=='MATERIAL':
            check(len(row)==8,'Material shape');unique(materials,int(row[1]),tuple(map(number,row[2:])))
        elif kind=='PCASE':
            check(len(row)==11,'Pressure summary shape');unique(pressure,int(row[1]),tuple(map(number,row[2:])))
        elif kind=='PCELL':
            check(len(row)==13,'Pressure cell shape');unique(cells,tuple(map(int,row[1:3])),tuple(map(number,row[3:])))
        elif kind=='PFACE':
            check(len(row)==7,'Pressure face shape');unique(faces,tuple(map(int,row[1:4])),tuple(map(number,row[4:])))
        elif kind=='EINTERVAL':
            check(len(row)==11 and row[1]=='2' and interval is None,'Wide interval shape/duplicate')
            interval=(int(row[2]),int(row[3]),number(row[4]),int(row[5]),int(row[6]),base.scaled_energy(row[7],row[8]),base.scaled_energy(row[9],row[10]))
        else:common.append(line)
    cases,_,_,_=base.parse(('\n'.join(common)+'\n').encode('ascii'),required_passes=4,required_cases=(0,1,2))
    check(len(materials)==4 and set(pressure)=={2} and interval is not None,'Missing material/pressure/owner records')
    return cases,materials,pressure,cells,faces,interval

def review(data):
    cases,materials,pressure,pcells,pfaces,interval=data
    def eos(masses,u,volume):
        capacity=offset=moles=D(0)
        for identity,amount in masses.items():
            phase,molar,cv,h,_,_=materials[identity];check(phase==0,'Wide test unexpectedly contains liquid')
            capacity+=dec(amount*cv);offset+=dec(amount*h);moles+=dec(amount/molar)
        temperature=(dec(u)-offset)/capacity;pressure=moles*dec(R)*temperature/dec(volume)
        entropy=capacity*(temperature/dec(F(float(298.15)))).ln()
        for identity,amount in masses.items():
            m=dec(amount/materials[identity][1]);entropy+=m*dec(R)*(dec(volume)/m).ln()
        check(temperature>0 and pressure>0,'Independent thin-gas EOS lost positive state')
        return pressure,temperature,entropy
    for identifier,case in cases.items():
        base.review_case(case);check(case['rplan'] is not None and case['wide_report'] is not None and case['widefaces'],'Missing retained wide-energy evidence')
        grid=base.topology(case);nc=len(case['walls']);count,unresolved,_,_,summary=case['wide_report']
        check(count>0,'Wide report concealed its projections')
        mass=[[sum((m for (end,c,_),m in case['masses'].items() if (end,c)==(e,i)),F(0)) for i in range(nc)] for e in (0,1)]
        heats=[F(0)]*nc;mix=[];wall=[]
        for axis in (0,1):
            for f,pair in enumerate(grid[axis]):
                row=case['faces'][axis,f];mix.append(row[8]);wall.append(row[9]);total=sum((mass[1][c] for c in pair if c is not None),F(0))
                for c in pair:
                    if c is not None:heats[c]+=(row[8]+row[9])*mass[1][c]/total
        for actual,terms in zip(summary[2:],(mix,wall)):
            close(actual,sum(terms,F(0)),abs(actual)+sum(map(abs,terms),F(0)),'Full derived heat summary')
        check(unresolved==sum(h>0 for h in heats),'Tiny unrepresented U updates were not counted')
        entropy=[D(0),D(0)];allow=D(0)
        for e in (0,1):
            for i in range(nc):
                values={s:m for (end,c,s),m in case['masses'].items() if (end,c)==(e,i)}
                p,t,s=eos(values,case['cells'][e,i],case['rplan'][0]);entropy[e]+=s;allow+=128*dec(EPS)*abs(dec(case['cells'][e,i]))/t
                check(all(m>0 for m in values.values()),'Positive bulk/trace inventory lost')
                if identifier in (0,1):check(abs(t-300)<D('1e-8'),'Tiny heat changed a resolved uniform temperature')
        check(entropy[1]-entropy[0]>=-allow,'Thin/tiny-energy state lost thermodynamic entropy beyond stored-U allowance')
        if identifier in (0,1):check(summary[2]>0 and F(float(summary[2]))==0,'Positive mixing heat was replaced with projected zero')
    case=cases[2];check(case['owner_energy'] is not None,'Closed owner has no wide energy record')
    count,energy=case['owner_energy'];row=pressure[2];dt,spacing,depth,pres,pbound,total,tbound,wx,wy=row
    check(dt==F(float('1e-104')) and spacing==depth==1,'Thin pressure reversal source/time changed')
    check(all(a==F(float(b)) for a,b in zip(row[3:7],energy)),'Owner projection differs from retained energy')
    check(count==case['wide_report'][0]+sum(a!=b for a,b in zip(row[3:7],energy)),'Closed owner projection counts differ')
    check(set(pcells)=={(2,0),(2,1)} and set(pfaces)=={(2,a,f) for a,rows in enumerate(base.topology(case)) for f in range(len(rows))},'Pressure operand coverage')
    terms=[];pressure_momentum=[[],[]];reactions=[[],[]]
    for i in range(2):
        old_u,volume,change,work,endpoint,final_p,final_t,s0,s1,geometry=pcells[2,i]
        check(volume==1 and change==geometry==0,'Pressure reversal has nonzero volume movement')
        old_mass={s:m for (end,c,s),m in case['masses'].items() if (end,c)==(0,i)}
        p,t,s=eos(old_mass,old_u,F(1));pf,tf,sf=eos(old_mass,case['cells'][1,i],F(1))
        for actual,expected in ((work,p),(endpoint,p),(final_p,pf),(final_t,tf)):
            check(abs(dec(actual)-expected)<=D('5e-10')*abs(expected),'Independent zero-volume EOS/work reading')
        for actual,expected in ((s0,s),(s1,sf)):
            check(abs(dec(actual)-expected)<=D('5e-10')*abs(expected),'Independent thin entropy reading')
        check(abs(t-300)<D('1e-8') and case['cells'][0,i]==case['cells'][1,i]==old_u,'Warm thin state/reference energy changed')
        terms.extend((case['cells'][0,i],-old_u))
    grid=base.topology(case)
    for axis in (0,1):
        for f,pair in enumerate(grid[axis]):
            initial,q,helper=pfaces[2,axis,f];report=case['faces'][axis,f];_,jp,_,i0,_,kp,*_=report
            pl=F(0) if pair[0] is None else pcells[2,pair[0]][3];pr=F(0) if pair[1] is None else pcells[2,pair[1]][3];impulse=dt*(pl-pr)
            fixed=None in pair;check(q==0,'Pressure reversal transported volume')
            close(jp,F(0) if fixed else initial+impulse,abs(jp)+abs(initial)+abs(impulse),'Tiny canonical pressure impulse')
            close(helper,jp/i0 if i0 else F(0),abs(helper)+(abs(jp/i0) if i0 else F(0)),'Tiny helper/canonical velocity')
            if not fixed:check(jp==-initial and kp>0 and F(float(kp))==0,'Tiny physical kinetic reversal was lost')
            else:reactions[axis].append(-impulse)
            pressure_momentum[axis].extend((jp,-initial,impulse if fixed else F(0)))
            terms.extend((kp,-initial*initial/(2*i0) if i0 else F(0)))
    for values,reaction,declared in zip(pressure_momentum,reactions,(wx,wy)):
        close(sum(values,F(0)),F(0),sum(map(abs,values),F(0)),'Tiny pressure impulse/wall account')
        close(declared,sum(reaction,F(0)),abs(declared)+sum(map(abs,reaction),F(0)),'Tiny wall impulse report')
    close(energy[0],sum(terms,F(0)),sum(map(abs,terms),F(0)),'Wide pressure energy residual')
    close(energy[1],128*EPS*sum(map(abs,terms),F(0)),energy[1],'Wide pressure energy operand bound',512)
    close(energy[3],energy[1]+case['bound'],energy[3]+energy[1]+case['bound'],'Wide composed stage allowance')
    check(interval[:4]==(1,0,dt,2) and interval[4]==count,'Tiny owner clock/revision/rounding metadata')
    check(interval[5:]==energy[2:],'One-step interval wide energy differs from its candidate')

def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=base.saved_pipe_cap(raw,524288),'Wide transport saved pipe cap')
    with localcontext() as context:
        context.prec=80;data=parse(raw);review(data);checks=base.CHECKS
        for corruption in ('primary-trace','wide-heat','unresolved-update'):
            bad=copy.deepcopy(data)
            if corruption=='primary-trace':
                values=bad[0][0]['masses'];key=next(k for k in values if k[0]==1 and values[k]<F(1,10**20));values[key]*=F(101,100)
            else:
                case=bad[0][1];v=list(case['wide_report'])
                if corruption=='wide-heat':e=list(v[4]);e[2]*=F(101,100);v[4]=tuple(e)
                else:v[1]=0
                case['wide_report']=tuple(v)
            caught=False
            try:review(bad)
            except ValueError:caught=True
            check(caught,'Altered wide transport evidence accepted')
    print(json.dumps(dict(Passed=True,SavedSha256=hashlib.sha256(raw).hexdigest(),SavedChecks=checks,CorruptionsRejected=3,
        NativeProcessLaunched=False,IndependentAgentReview=False,Scope='Wide derived energy in occupied transport/one thin closed step; no strong-flow or production/game acceptance')))

if __name__=='__main__':main()
