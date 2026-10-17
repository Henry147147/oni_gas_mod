#!/usr/bin/env python3
"""Independent saved geometry/reference checks; exit 1 retains a failed nodal gate."""
from fractions import Fraction as F
import hashlib
import json
from pathlib import Path
import sys

EPS=F(1,2**52)

def require(value,message):
    if not value:raise ValueError(message)

def scalar(value):
    return F(float(value))

def scaled(mantissa,exponent):
    power=int(exponent);require(-2048<=power<=2048,'Invalid diagnostic exponent')
    return scalar(mantissa)*F(2)**power

def review(text):
    cases={};sweeps={};nodes={};complete=None
    for line in text.splitlines():
        row=line.split();require(bool(row),'Empty diagnostic row');kind=row[0]
        if kind=='CGEOM':
            require(len(row)==12,'Incomplete case');identity=int(row[1]);require(identity not in cases,'Duplicate case')
            axis,curved,convention,direction=map(int,row[2:6]);a,b=map(scalar,row[6:8]);fraction=scalar(row[8])
            require(axis in (0,1) and curved in (0,1) and convention in (0,1) and direction in (-1,1),'Invalid case identity')
            require(a==F(1,4) and b==F(1,8) and 0<=fraction<=1 and int(row[9])>=0,'Changed control or invalid admission')
            require(0<=scalar(row[11]) and abs(scalar(row[10]))<=scalar(row[11]),'Native reported energy guard failed')
            cases[identity]=(axis,curved,convention,direction,a,b,fraction,int(row[9]))
        elif kind=='CSWEEP':
            require(len(row)==5,'Incomplete sweep');key=(int(row[1]),int(row[2]));require(key not in sweeps,'Duplicate sweep')
            sweeps[key]=tuple(map(scalar,row[3:5]))
        elif kind=='CNODE':
            require(len(row)==13,'Incomplete node');key=tuple(map(int,row[1:4]));require(key not in nodes,'Duplicate node')
            nodes[key]=row
        elif kind=='CCOMPLETE':
            require(len(row)==3 and complete is None,'Invalid completion marker');complete=tuple(map(int,row[1:]))
        else:raise ValueError('Unknown diagnostic row: '+kind)
    expected={(a,c,v,d) for a in (0,1) for c in (0,1) for v in (0,1) for d in (-1,1)}
    require(len(cases)==16 and {v[:4] for v in cases.values()}==expected,'Missing/duplicated controls')
    require(set(sweeps)=={(i,f) for i in cases for f in range(11)},'Missing/extra sweeps')
    require(set(nodes)=={(i,r,f) for i in cases for r in range(2) for f in (4,5,6)},'Missing/extra nodes')
    counts={'nodal-point':{'readings':0,'matched':0},'dual-volume-mean':{'readings':0,'matched':0}}
    aliases=[]
    for identity,v in cases.items():
        axis,curved,convention,direction,a,b,_,_=v
        q=lambda f:F(0) if f in (0,10) else direction*(F(f-1,64)+curved*F((f-1)**2,512))
        mean=lambda f:F(0) if f in (0,10) else (q(f-1)+2*q(f)+q(f+1))/4
        for f in range(11):require(sweeps[identity,f]==(q(f),mean(f)),'Changed sweep or analytic centroid')
        for other in range(2):
            for f in (4,5,6):
                row=nodes[identity,other,f];face=int(row[4]);old_i,new_i,centroid=map(scalar,row[5:8])
                require(face==(f+11*other if axis==0 else other+2*f),'Wrong native face mapping')
                require(old_i==1+(q(f+1)-q(f-1))/2 and new_i==1 and centroid==mean(f),'Incorrect stored geometry')
                actual=scaled(row[8],row[9]);reference=scaled(row[10],row[11]);require(reference==a+b*f,'Changed affine reference')
                bound=128*EPS*(abs(actual)+abs(reference));matched=abs(actual-reference)<=bound
                require(row[12] in ('0','1') and bool(int(row[12]))==matched,'Native match flag differs from exact operands')
                label='dual-volume-mean' if convention else 'nodal-point';counts[label]['readings']+=1;counts[label]['matched']+=matched
                if curved and not convention:
                    predicted=-b*(mean(f)-q(f))
                    aliases.append(dict(Case=identity,Other=other,Face=f,ActualError=str(actual-reference),CentroidAliasPrediction=str(predicted),MatchesAlias=abs(actual-reference-predicted)<=bound))
    failures=counts['nodal-point']['readings']-counts['nodal-point']['matched']
    require(complete==(16,failures),'Completion marker differs from actual nodal failures')
    return dict(evidence_valid=True,nodal_gate_passed=failures==0,convention_counts=counts,curved_aliases=aliases,
        strong_flow_gate_passed=False,first_playable_colony_passed=False,independent_agent_review=False,
        scope='Saved curved/affine geometry, both axes/signs, exact 128-epsilon momentum oracle and source-convention comparison. Reported energy guard only; no independent full energy/entropy, original strong-flow or game acceptance.')

def main():
    try:
        require(len(sys.argv)==2,'Usage: review_mac_curved_nodal.py <raw stdout>');path=Path(sys.argv[1]);result=review(path.read_text())
        result['raw_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();print(json.dumps(result))
        return 0 if result['nodal_gate_passed'] else 1
    except (ValueError,KeyError,OverflowError,IndexError,OSError) as error:
        print(json.dumps(dict(evidence_valid=False,error=str(error))));return 2

if __name__=='__main__':sys.exit(main())
