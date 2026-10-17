#!/usr/bin/env python3
"""Review saved occupied-mixture point transfers without loading native code."""
from __future__ import annotations
import argparse
import copy
from decimal import Decimal, localcontext
import hashlib
import json
from pathlib import Path

def require(value,message):
    if not value:raise ValueError(message)

def number(text):
    value=Decimal.from_float(float(text))
    if not value.is_finite():raise ValueError('Nonfinite saved operand')
    return value

def wide(mantissa,exponent):
    m=number(mantissa);e=int(exponent)
    if not ((m==0 and e==0) or Decimal('.5')<=abs(m)<1) or not -10000<=e<=10000:
        raise ValueError('Noncanonical saved momentum/energy')
    return m*Decimal(2)**e

def parse(path):
    species={};cases={};worlds={};cells={};faces={};parcels={};terminal=False;rejections=0
    def unique(table,key,value):
        if key in table:raise ValueError('Duplicate saved record')
        table[key]=value
    for line in path.read_text().splitlines():
        fields=line.split();kind=fields[0]
        if terminal:raise ValueError('Data after terminal native result')
        if kind=='SPECIES':
            require(len(fields)==6,'Invalid species row');unique(species,int(fields[1]),(fields[2],*[number(v) for v in fields[3:]]))
        elif kind=='CASE':
            require(len(fields)==7,'Invalid request row');unique(cases,int(fields[1]),(int(fields[2]),int(fields[3]),int(fields[4]),number(fields[5]),number(fields[6])))
        elif kind=='WORLD':
            require(len(fields)==8,'Invalid world row');unique(worlds,(int(fields[1]),int(fields[2])),(int(fields[3]),int(fields[4]),number(fields[5]),number(fields[6]),int(fields[7])))
        elif kind=='CELL':
            require(len(fields)==10+len(species),'Invalid cell row');unique(cells,tuple(map(int,fields[1:4])),tuple(number(v) for v in fields[4:]))
        elif kind=='FACE':
            require(len(fields)==8,'Invalid face row');unique(faces,tuple(map(int,fields[1:5])),(number(fields[5]),wide(fields[6],fields[7])))
        elif kind=='PARCEL':
            require(len(fields)==13,'Invalid parcel row');unique(parcels,int(fields[1]),tuple([number(v) for v in fields[2:5]]+[wide(fields[i],fields[i+1]) for i in (5,7,9,11)]))
        elif kind=='REJECTIONS':
            require(fields==['REJECTIONS','8'],'Invalid native rejection count');rejections+=1
        elif kind=='PASS':
            require(line=='PASS six occupied mixed-phase point transfers and eight atomic rejections','Missing native terminal');terminal=True
        else:raise ValueError('Unknown native record '+kind)
    require(terminal and rejections==1 and set(cases)==set(parcels)==set(range(6)),'Incomplete native cases')
    require(set(species)==set(range(len(species))) and len(worlds)==12 and len(cells)==48 and len(faces)==144,'Incomplete native states')
    return dict(species=species,cases=cases,worlds=worlds,cells=cells,faces=faces,parcels=parcels)

def review(data):
    checks=0;epsilon=Decimal(2)**-52
    def check(value,message):
        nonlocal checks
        checks+=1
        if not value:raise ValueError(message)
    def account(terms,message):
        check(abs(sum(terms)) <= sum(abs(v) for v in terms)*128*epsilon,message)
    def relative(a,b,tolerance,message):
        check(abs(a-b)<=max(abs(a),abs(b))*tolerance,message)
    species=data['species'];water=next(k for k,v in species.items() if v[0]=='water')
    supported={k for k,v in species.items() if v[0] in ('nitrogen','oxygen','helium','water')}
    gas_constant=number('8.31446261815324');bulk=Decimal(2200000000);density=Decimal(1000)
    for case,(target,selected,inject,mass,temperature) in data['cases'].items():
        states=[]
        for stage in (0,1):
            width,height,gravity,time,revision=data['worlds'][case,stage]
            check((width,height)==(2,2) and gravity==number('9.81'),'Changed fixture geometry or gravity')
            energy=[];momentum=[[],[]];total_masses=[]
            for cell in range(width*height):
                u,t,p,vg,vl,entropy,*masses=data['cells'][case,stage,cell]
                check(all(m>=0 for m in masses) and all(m==0 or k in supported for k,m in enumerate(masses)),'Invalid/unsupported constituent')
                check(1<=t<=1000000 and p>0 and 1+p/bulk<=2,'Outside declared EOS domain')
                ratio=1+p/bulk;elastic=bulk/density*(ratio.ln()+1/ratio-1)
                expected_gas=sum(m/species[k][3]*gas_constant*t/p for k,m in enumerate(masses) if k in supported and k!=water)
                expected_liquid=masses[water]/(density*ratio)
                relative(vg,expected_gas,Decimal('2e-10'),'Gas partial-volume mismatch')
                relative(vl,expected_liquid,Decimal('2e-10'),'Liquid elastic-volume mismatch')
                relative(vg+vl,Decimal(1),Decimal('2e-10'),'Occupied volume does not close')
                terms=[u]
                for k,m in enumerate(masses):terms.extend((-m*species[k][1]*t,-m*species[k][2]))
                terms.append(-masses[water]*elastic);account(terms,'Stored U disagrees with independent caloric/elastic EOS')
                total=sum(masses);total_masses.append(total);energy.extend((u,total*gravity*(cell//width+Decimal('.5'))))
            for axis in (0,1):
                for face in range(6):
                    inertia,p=data['faces'][case,stage,axis,face]
                    x=face%(width+1) if axis==0 else face%width
                    y=face//(width+1) if axis==0 else face//width
                    neighbors=[]
                    if axis==0:
                        if x>0:neighbors.append(x-1+y*width)
                        if x<width:neighbors.append(x+y*width)
                    else:
                        if y>0:neighbors.append(x+(y-1)*width)
                        if y<height:neighbors.append(x+y*width)
                    account([inertia,*[-total_masses[i]/2 for i in neighbors]],'Face inertia disagrees with its complete cell masses')
                    check(inertia>0,'Nonpositive fixture inertia');energy.append(p*p/(2*inertia));momentum[axis].append(p)
            states.append((energy,momentum,total_masses,time,revision))
        before,after=states
        check(after[3]==before[3] and after[4]==before[4]+1,'Transfer changed physical time or revision incorrectly')
        pmass,pt,pu,pk,pg,px,py=data['parcels'][case]
        check(pmass==mass and inject in (0,1),'Changed request mass/direction')
        source=data['cells'][case,0,target]
        check(pt==(temperature if inject else source[1]),'Parcel temperature disagrees with request or source')
        for cell in range(4):
            old=data['cells'][case,0,cell][6:];new=data['cells'][case,1,cell][6:]
            for k,(a,b) in enumerate(zip(old,new,strict=True)):
                if cell==target and k==selected:account([a,mass if inject else -mass,-b],'Selected constituent mass balance failed')
                else:check(a==b,'Unselected constituent or trace changed')
        ratio=1+source[2]/bulk;elastic=bulk/density*(ratio.ln()+1/ratio-1) if selected==water else Decimal(0)
        account([pu,-mass*species[selected][1]*pt,-mass*species[selected][2],-mass*elastic],'Incorrect caloric/elastic parcel U')
        account([pg,-mass*number('9.81')*(target//2+Decimal('.5'))],'Incorrect carried gravitational potential')
        if inject:
            check(pk==px==py==0 and before[1]==after[1],'At-rest injection created momentum or carried kinetic energy')
        else:
            expected_k=[];expected_p=[[],[]]
            x,y=target%2,target//2
            for axis,indices in ((0,(x+y*3,x+1+y*3)),(1,(x+y*2,x+(y+1)*2))):
                for face in indices:
                    inertia,p=data['faces'][case,0,axis,face];share=mass/(2*inertia)
                    expected_p[axis].append(p*share);expected_k.append(p*p/(2*inertia)*share)
            account([pk,*[-v for v in expected_k]],'Extraction lost carried face kinetic energy')
            for axis,carried in enumerate((px,py)):account([carried,*[-v for v in expected_p[axis]]],'Extraction lost carried momentum')
        sign=Decimal(1) if inject else Decimal(-1)
        account([*before[0],sign*pu,sign*pg,-pk,*[-v for v in after[0]]],'Complete stored U+K+G balance failed')
        for axis,carried in enumerate((px,py)):account([*before[1][axis],-carried,*[-v for v in after[1][axis]]],'Complete face momentum balance failed')
        check(data['cells'][case,1,target][3]>0 and data['cells'][case,1,target][4]>0,'No gas/liquid coexistence in accepted result')
    return checks

def corruptions(data):
    changed=copy.deepcopy(data);values=list(changed['parcels'][1]);values[2]*=2;changed['parcels'][1]=tuple(values)
    variants=[('parcel-U',changed)]
    changed=copy.deepcopy(data);helium=next(k for k,v in changed['species'].items() if v[0]=='helium')
    values=list(changed['cells'][0,1,0]);require(values[6+helium]>0,'Trace corruption needs a positive witness')
    values[6+helium]*=2;changed['cells'][0,1,0]=tuple(values);variants.append(('trace-mass',changed))
    changed=copy.deepcopy(data);inertia,p=changed['faces'][0,1,0,1];require(p!=0,'Momentum corruption needs a nonzero witness')
    changed['faces'][0,1,0,1]=(inertia,p*2);variants.append(('face-momentum',changed))
    changed=copy.deepcopy(data);values=list(changed['cells'][0,1,0]);require(values[4]>0,'Liquid-volume corruption needs a positive witness')
    values[4]*=2;changed['cells'][0,1,0]=tuple(values);variants.append(('liquid-volume',changed))
    for name,altered in variants:
        require(altered!=data,'Corruption did not change a saved operand')
        try:review(altered)
        except ValueError:continue
        raise ValueError('Reviewer admitted corrupted '+name)
    return [name for name,_ in variants]

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('native_stdout',type=Path);args=parser.parse_args()
    with localcontext() as context:
        context.prec=90;data=parse(args.native_stdout);checks=review(data);negative=corruptions(data)
    print(json.dumps(dict(kind='saved-only-occupied-mixture-transfer-review',passed=True,cases=6,native_rejections=8,
        independent_checks=checks,corruptions_rejected=negative,operand_relative_allowance='128*binary64-epsilon',
        volume_relative_allowance='2e-10',absolute_mass_floor=0,
        raw_sha256=hashlib.sha256(args.native_stdout.read_bytes()).hexdigest(),
        game_integration_verified=False,whole_colony_fluid_authority=False),indent=2))

if __name__=='__main__':
    try:main()
    except (ValueError,KeyError,IndexError,ArithmeticError,OSError) as error:
        raise SystemExit('mixed-transfer review failed: '+str(error))
