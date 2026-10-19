#!/usr/bin/env python3
"""Audit saved gas/liquid heat operands with Decimal arithmetic, without native code."""
from __future__ import annotations
import argparse
import copy
from decimal import Decimal, localcontext
import hashlib
import json
from pathlib import Path


def number(text):
    if '~' in text:
        fields=text.split('~')
        if len(fields)!=3:raise ValueError('Primary-U token shape')
        high=number(fields[0]);mantissa=number(fields[1]);exponent=int(fields[2])
        if not (-16384<exponent<16384 and Decimal('.5')<=abs(mantissa)<1):raise ValueError('Primary-U correction shape')
        low=mantissa*Decimal(2)**exponent
        if Decimal.from_float(float(high+low))!=high:raise ValueError('Noncanonical primary-U pair')
        return high+low
    value=Decimal.from_float(float(text))
    if not value.is_finite():raise ValueError('Nonfinite saved operand')
    return value


def require(value,message):
    if not value:raise ValueError(message)


def parse(path):
    materials={};cases={};cells={};masses={};terminal=False
    def add(table,key,value):
        require(key not in table,'Duplicate saved heat record');table[key]=value
    for line in path.read_text().splitlines():
        fields=line.split();require(fields and not terminal,'Data after native terminal');kind=fields[0]
        if kind=='TMATERIAL':
            require(len(fields)==6,'Invalid material record');add(materials,fields[1],tuple(number(v) for v in fields[2:]))
        elif kind=='THERMAL':
            require(len(fields)==5,'Invalid contact record');add(cases,int(fields[1]),tuple(number(v) for v in fields[2:]))
        elif kind=='TCELL':
            require(len(fields)==11,'Invalid state record');add(cells,tuple(map(int,fields[1:4])),tuple(number(v) for v in fields[4:]))
        elif kind=='TMASS':
            require(len(fields)==6,'Invalid mass record');add(masses,(*map(int,fields[1:4]),fields[4]),number(fields[5]))
        elif kind=='PASS':
            require(line=='PASS nonlinear gas/liquid conduction, stiff maximum principle, independent time refinement, immutable rejection, insulation, traces and complete continuation','Unexpected native terminal');terminal=True
        else:raise ValueError('Unknown heat record '+kind)
    require(terminal and set(materials)=={'nitrogen','oxygen','helium','water'} and set(cases)==set(range(5)),'Incomplete heat cases')
    require(len(cells)==20 and len(masses)==80,'Incomplete saved heat states')
    return dict(materials=materials,cases=cases,cells=cells,masses=masses)


def review(data):
    checks=0;epsilon=Decimal(2)**-52;bulk=Decimal(2200000000);density=Decimal(1000);gas_constant=number('8.31446261815324')
    def check(value,message):
        nonlocal checks
        checks+=1;require(value,message)
    def account(terms,message):check(abs(sum(terms))<=128*epsilon*sum(abs(v) for v in terms),message)
    def relative(a,b,allowance,message):check(abs(a-b)<=max(abs(a),abs(b))*allowance,message)
    materials=data['materials'];states=data['cells'];masses=data['masses'];exact={}
    def reconstruct_state(u,amounts,cv,moles):
        offset=sum(mass*materials[name][1] for name,mass in amounts.items())
        def at(p):
            ratio=1+p/bulk;elastic=amounts['water']*bulk/density*(ratio.ln()+1/ratio-1)
            temperature=(u-offset-elastic)/cv;liquid=amounts['water']/density/ratio
            return p*(liquid-1)+moles*gas_constant*temperature,temperature,liquid
        low=Decimal(0);high=bulk
        check(at(low)[0]>0 and at(high)[0]<0,'Independent EOS root is not bracketed')
        for _ in range(300):
            middle=(low+high)/2
            if middle==low or middle==high:break
            if at(middle)[0]>0:low=middle
            else:high=middle
        p=(low+high)/2;_,temperature,liquid=at(p)
        check(1<=temperature<=1000000,'Independent equilibrium left the material model')
        return temperature,moles*gas_constant*temperature/p,p
    for case,(duration,conductance,reported_entropy) in data['cases'].items():
        check(duration>0 and conductance>0 and reported_entropy>0,'Invalid heat interval or progress')
        initial_conductivity=[];entropy=Decimal(0);entropy_scale=Decimal(0)
        for stage in (0,1):
            for cell in (0,1):
                u,t,p,vg,vl,capacity,equilibrium=states[case,stage,cell]
                amounts={name:masses[case,stage,cell,name] for name in materials}
                check(all(value>0 for value in amounts.values()),'A phase or positive helium trace disappeared')
                check(1<=t<=1000000 and p>0 and 1+p/bulk<=2,'Outside the declared EOS model')
                ratio=1+p/bulk;elastic=bulk/density*(ratio.ln()+1/ratio-1)
                moles=sum(amounts[name]/materials[name][2] for name in materials if name!='water')
                relative(vg,moles*gas_constant*t/p,Decimal('2e-10'),'Gas partial volume disagrees with EOS')
                relative(vl,amounts['water']/(density*ratio),Decimal('2e-10'),'Liquid volume ignores compression')
                relative(vg+vl,Decimal(1),Decimal('2e-10'),'Occupied volume does not close')
                terms=[u]
                for name,mass in amounts.items():terms.extend((-mass*materials[name][0]*t,-mass*materials[name][1]))
                terms.append(-amounts['water']*elastic);account(terms,'Stored internal energy disagrees with caloric/elastic EOS')
                cv=sum(mass*materials[name][0] for name,mass in amounts.items())
                relative(capacity,cv,128*epsilon,'Intrinsic heat capacity changed')
                exact[case,stage,cell]=reconstruct_state(u,amounts,cv,moles)
                relative(t,exact[case,stage,cell][0],Decimal('2e-10'),'Returned temperature disagrees with conserved-state equilibrium')
                relative(p,exact[case,stage,cell][2],Decimal('2e-10'),'Returned pressure disagrees with conserved-state equilibrium')
                compliance=vl/(bulk+p)
                effective=cv+p*compliance*vg/(t*(compliance+vg/p))
                relative(equilibrium,effective,Decimal('2e-10'),'Equilibrium heat capacity ignores elastic coupling')
                if stage==0:
                    gas_weight=sum(amounts[name]/materials[name][2]/moles*materials[name][3] for name in materials if name!='water')
                    initial_conductivity.append((vg*gas_weight+vl*materials['water'][3])/(vg+vl))
                else:
                    previous=states[case,0,cell]
                    before_exact=exact[case,0,cell];after_exact=exact[case,1,cell]
                    entropy+=cv*(after_exact[0]/before_exact[0]).ln()+moles*gas_constant*(after_exact[1]/before_exact[1]).ln()
                    entropy_scale+=abs(previous[0])/previous[1]+abs(u)/t+cv
                    for name,mass in amounts.items():check(mass==masses[case,0,cell,name],'Conduction changed a constituent or trace')
        expected_conductance=1/(Decimal('.5')/initial_conductivity[0]+Decimal('.5')/initial_conductivity[1])
        relative(conductance,expected_conductance,128*epsilon,'Face conductance does not match the initial phase-volume mixture')
        cold0,hot0=states[case,0,0],states[case,0,1];cold1,hot1=states[case,1,0],states[case,1,1]
        q=cold1[0]-cold0[0];flux=duration*conductance*(hot1[1]-cold1[1])
        bound=(128*epsilon*(abs(cold0[0])+abs(cold1[0])+abs(q)+abs(flux))+
            128*epsilon*duration*conductance*(cold1[1]+hot1[1]))
        check(q>0 and abs(q-flux)<=bound,'Nonlinear backward-Euler heat flux does not close')
        account([cold0[0],hot0[0],-cold1[0],-hot1[0]],'Closed pair energy changed')
        check(cold0[1]<=cold1[1]<=hot0[1] and cold0[1]<=hot1[1]<=hot0[1] and
            cold1[1]<=hot1[1]+128*epsilon*hot0[1],'Heat solve violates its maximum principle')
        check(entropy>0 and abs(reported_entropy-entropy)<=128*epsilon*entropy_scale,'Reported entropy omits gas-volume or caloric changes')
        if case==4:
            check(q==hot0[0]-hot1[0],'Weak entropy witness is not exactly closed')
            tc0,th0=exact[case,0,0][0],exact[case,0,1][0]
            tc1,th1=exact[case,1,0][0],exact[case,1,1][0]
            lower=q*(th1-tc1)/tc1/th1;upper=q*(th0-tc0)/tc0/th0
            allowance=q*128*epsilon*(1/tc0+1/th0)
            check(lower-allowance<=reported_entropy<=upper+allowance,'Weak entropy lost its heat-scale signal')
    return checks


def corruptions(data):
    variants=[]
    def cell_variant(name,index,multiplier):
        changed=copy.deepcopy(data);row=list(changed['cells'][0,1,0]);row[index]*=multiplier
        changed['cells'][0,1,0]=tuple(row);variants.append((name,changed))
    cell_variant('stored-U',0,Decimal('1.001'));cell_variant('temperature',1,Decimal('1.001'))
    cell_variant('liquid-volume',4,Decimal(2));cell_variant('equilibrium-heat-capacity',6,Decimal(2))
    changed=copy.deepcopy(data);changed['masses'][0,1,0,'helium']*=2;variants.append(('trace-mass',changed))
    for name,index in [('conductance',1),('entropy',2)]:
        changed=copy.deepcopy(data);row=list(changed['cases'][0]);row[index]*=2;changed['cases'][0]=tuple(row);variants.append((name,changed))
    changed=copy.deepcopy(data);row=list(changed['cases'][4]);row[2]*=Decimal('1.001');changed['cases'][4]=tuple(row)
    variants.append(('weak-entropy-signal',changed))
    for name,changed in variants:
        require(changed!=data,'Mutation did not alter a saved operand')
        try:review(changed)
        except ValueError:continue
        raise ValueError('Saved reviewer accepted corrupted '+name)
    return [name for name,_ in variants]


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('native_stdout',type=Path);args=parser.parse_args()
    with localcontext() as context:
        context.prec=90;data=parse(args.native_stdout);checks=review(data);negative=corruptions(data)
    print(json.dumps(dict(kind='saved-only-nonlinear-mixture-heat-review',passed=True,cases=5,independent_checks=checks,
        corruptions_rejected=negative,operand_relative_allowance='128*binary64-epsilon',volume_relative_allowance='2e-10',
        absolute_mass_floor=0,raw_sha256=hashlib.sha256(args.native_stdout.read_bytes()).hexdigest(),
        game_integration_verified=False,whole_colony_fluid_authority=False),indent=2))


if __name__=='__main__':
    try:main()
    except (ValueError,KeyError,IndexError,ArithmeticError,OSError) as error:raise SystemExit('mixture-heat review failed: '+str(error))
