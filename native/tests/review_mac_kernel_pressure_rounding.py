#!/usr/bin/env python3
"""Review pressure-addition rounding with exact binary64 fractions; no solver launch."""
from __future__ import annotations
import copy
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

EPS=F(1,2**52)
TINY=F(1,2**1074)
checks=0

def check(ok,message):
    global checks
    checks+=1
    if not ok:raise ValueError(message)

def frac(value):return F.from_float(value)

def prescribed_inputs():
    tiny=float(TINY);normal=float(F(1,2**1022))
    for a in (0.,tiny,3*tiny,normal,2*normal,1.,math.nextafter(1.,2.),2.,2.**450):
        for b in (0.,tiny,-tiny,2.**-107,2.**-54,2.**-53,2.**396):
            yield a,b;yield -a,-b;yield a,-a
    state=0x3ab9a538814f6d19
    def next_word():
        nonlocal state
        state^=(state<<13)&((1<<64)-1);state^=state>>7;state^=(state<<17)&((1<<64)-1)
        return state
    for i in range(1024):
        bits,exponent,shift=next_word(),next_word(),next_word()
        mantissa=struct.unpack('<d',struct.pack('<Q',(bits&0xfffffffffffff)|0x3fe0000000000000))[0]
        power=int(exponent%1451)-1000
        a=float(frac(mantissa)*F(2)**power);b=float(frac(a)*F(2)**(-int(shift%100)))
        yield -a if i%2 else a,b if i%3 else -b

def parse(raw):
    result=dict(additions=[],axes={},cases={},contract=None,setup=None,merged=None,controls=None,passed=False)
    for line in raw.decode('ascii').splitlines():
        p=line.split();check(bool(p),'Empty record')
        kind=p.pop(0)
        if kind=='WCONTRACT':check(result['contract'] is None and len(p)==1,'Duplicate/bad contract');result['contract']=int(p[0])
        elif kind=='WSETUP':check(result['setup'] is None and len(p)==10,'Duplicate/bad setup');result['setup']=tuple(map(float,p))
        elif kind=='WADD':
            check(len(p)==10 and int(p[0])==len(result['additions']),'Addition framing/order')
            result['additions'].append([float(p[1]),float(p[2]),float(p[3]),int(p[4]),*map(float,p[5:])])
        elif kind=='WMERGE':check(result['merged'] is None and len(p)==7,'Duplicate/bad merge');result['merged']=[int(p[0]),int(p[1]),*map(float,p[2:])]
        elif kind=='WAXIS':
            check(len(p)==5,'Axis framing');key=(int(p[0]),p[1]);check(key not in result['axes'],'Duplicate axis')
            result['axes'][key]=[int(p[2]),float(p[3]),float(p[4])]
        elif kind=='WCASE':
            check(len(p)==7 and int(p[0]) not in result['cases'],'Case framing/order')
            result['cases'][int(p[0])]=tuple(map(int,p[1:]))
        elif kind=='WCONTROLS':check(result['controls'] is None,'Duplicate controls');result['controls']=tuple(map(int,p))
        elif kind=='WPASS':check(not p and not result['passed'],'Duplicate/bad completion');result['passed']=True
        else:raise ValueError('Unknown record: '+kind)
    return result

def review(data):
    check(data['contract']==128,'Original interval epsilon count changed')
    check(data['setup']==(193,173,.002,.01,1,.5,100000,20,10,300),'Scientific pressure scene changed')
    check(data['controls']==(1,1,1,1) and data['passed'],'Missing failure/overflow controls or completion')
    inputs=list(prescribed_inputs());check(len(data['additions'])==len(inputs),'Scientific addition count changed')
    count=0;error=F(0);half=F(0)
    for row,prescribed in zip(data['additions'],inputs):
        before,increment,stored,n,eh,el,bh,bl,charged=row
        check((before,increment)==prescribed,'Scientific addition operands changed')
        check(all(math.isfinite(v) for v in (before,increment,stored,eh,el,bh,bl,charged)),'Nonfinite addition observation')
        exact=frac(before)+frac(increment);difference=abs(exact-frac(stored))
        check(stored==float(exact),'Stored result differs from nearest-even binary64')
        check(n==int(difference!=0),'Inexact addition count differs')
        check(frac(eh)+frac(el)==difference,'Exact local addition error differs')
        if n:
            exponent=(struct.unpack('<Q',struct.pack('<d',stored))[0]>>52)&0x7ff
            expected=F(2)**(exponent-1076)
        else:expected=F(0)
        check(frac(bh)+frac(bl)==expected and difference<=expected,'Half-ulp charge differs')
        allowance=frac(charged)
        check(expected<=allowance<=expected*(1+64*EPS*EPS*(n+1)+8*EPS)+TINY,'Local outward charge is not bounded')
        count+=n;error+=difference;half+=expected
    merged=data['merged'];check(merged is not None and merged[:2]==[len(inputs),count] and count>500,'Merged count differs')
    _,_,eh,el,bh,bl,charged=merged
    check(abs(frac(eh)+frac(el)-error)<=64*EPS*EPS*(count+1)*error+2*TINY,'Merged DD error estimate differs')
    check(abs(frac(bh)+frac(bl)-half)<=64*EPS*EPS*(count+1)*half+2*TINY,'Merged DD half-ulp estimate differs')
    check(half<=frac(charged)<=half*(1+64*EPS*EPS*(count+1)+8*EPS)+TINY,'Merged outward allowance differs')
    check(set(data['cases'])=={0,1,2},'Missing pressure integration method')
    check(set(data['axes'])=={(method,axis) for method in range(3) for axis in ('momentum_x','momentum_y')},'Missing pressure momentum axis')
    for method,case in data['cases'].items():
        cells,threads,total,*controls=case
        check((cells,threads)==(33389,12) and controls==[1,1,1],'Missing true parallelism, rollback or exact continuation')
        check(total==sum(data['axes'][method,axis][0] for axis in ('momentum_x','momentum_y')) and total>0,'Axis/owner counts differ')
        for axis in ('momentum_x','momentum_y'):
            n,charged,bound=data['axes'][method,axis]
            check(n>0 and math.isfinite(charged) and math.isfinite(bound) and 0<charged<=bound,'Pressure charge exceeds the original audit budget')

def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<4*1024*1024,'Saved stream exceeds cap')
    data=parse(raw);review(data);saved=checks
    mutations=('contract','setup','operands','stored','count','error','half','allowance','merge_count','merge_error','merge_half','merge_allowance','axis_count','axis_allowance','axis_bound','threads','rollback','completion')
    selected=next(i for i,row in enumerate(data['additions']) if row[3])
    for name in mutations:
        bad=copy.deepcopy(data);row=bad['additions'][selected]
        if name=='contract':bad['contract']=129
        elif name=='setup':bad['setup']=(192,*bad['setup'][1:])
        elif name=='operands':row[1]=math.nextafter(row[1],math.inf)
        elif name=='stored':row[2]=math.nextafter(row[2],math.inf)
        elif name=='count':row[3]+=1
        elif name=='error':row[4]*=2
        elif name=='half':row[6]*=.5
        elif name=='allowance':row[8]=0
        elif name=='merge_count':bad['merged'][1]+=1
        elif name=='merge_error':bad['merged'][2]*=1.01
        elif name=='merge_half':bad['merged'][4]*=.5
        elif name=='merge_allowance':bad['merged'][6]=0
        elif name=='axis_count':bad['axes'][0,'momentum_x'][0]+=1
        elif name=='axis_allowance':bad['axes'][0,'momentum_x'][1]=2*bad['axes'][0,'momentum_x'][2]
        elif name=='axis_bound':bad['axes'][0,'momentum_x'][2]=0
        elif name=='threads':bad['cases'][0]=(33389,1,*bad['cases'][0][2:])
        elif name=='rollback':bad['cases'][0]=(*bad['cases'][0][:3],0,1,1)
        else:bad['passed']=False
        rejected=False
        try:review(bad)
        except ValueError:rejected=True
        check(rejected,'Altered capture admitted: '+name)
    print(json.dumps(dict(Passed=True,SavedChecks=saved,CorruptionsRejected=len(mutations),SavedSha256=hashlib.sha256(raw).hexdigest(),NativeProcessLaunched=False,
        Scope='Pressure momentum storage rounding only; no complete remaining-ledger, local-subcycling or real-time acceptance')))

if __name__=='__main__':main()
