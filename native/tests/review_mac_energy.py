"""Exact saved-operand checks for derived energy range and additive expansions."""
import copy
from fractions import Fraction as F
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

spec=importlib.util.spec_from_file_location('mac_saved',Path(__file__).with_name('review_mac_transport.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
check,number,unique=base.check,base.number,base.unique
EPS=base.EPS
def encoded(m,e):
    m=number(m) if isinstance(m,str) else m;e=int(e)
    check((m==0 and e==0) or F(1,2)<=abs(m)<1,'Energy representation is not normalized')
    return m*F(2)**e

def parse(raw):
    tables={k:{} for k in ('ROUND','KIN','VAR','SHARE','SUM','TERM','COMP','ACCOUNT','ATERM','PROJECT')};passes=0;rejections=None;environment=None
    lengths=dict(ROUND=7,KIN=6,VAR=6,SHARE=8,SUM=7,TERM=5,COMP=5,ACCOUNT=8,ATERM=5,PROJECT=6)
    for line in raw.decode('ascii').splitlines():
        r=line.split();check(bool(r),'Empty energy record');kind=r[0]
        if kind=='PASS':passes+=1;continue
        if kind=='REJECTIONS':check(len(r)==2 and rejections is None,'Negative-control shape/duplicate');rejections=int(r[1]);continue
        if kind=='ENVIRONMENT':check(len(r)==3 and environment is None,'Environment shape/duplicate');environment=tuple(map(int,r[1:]));continue
        check(kind in tables and len(r)==lengths[kind],'Energy record kind/shape')
        key=tuple(map(int,r[1:3])) if kind in ('TERM','COMP','ATERM') else int(r[1])
        unique(tables[kind],key,tuple(r[3:] if isinstance(key,tuple) else r[2:]))
    check(passes==7 and rejections==10 and environment==(5,1),'Incomplete native energy groups')
    return tables

def review(t):
    counts=dict(ROUND=11,KIN=5,VAR=4,SHARE=5,SUM=34,ACCOUNT=6,PROJECT=7)
    check(all(set(t[k])==set(range(v)) for k,v in counts.items()),'Missing/extra energy cases')
    for source,m,e,projected,rounded in t['ROUND'].values():
        original=number(source);check(encoded(m,e)==original and number(projected)==original and int(rounded)==0,'Finite binary64 roundtrip mismatch')
    for kind in ('KIN','VAR'):
        for a,b,m,e in t[kind].values():
            a,b=number(a),number(b);expected=b*b/(2*a) if kind=='KIN' else a*b*b/2;actual=encoded(m,e)
            check(expected>0 and actual>0,'Positive derived energy disappeared')
            check(abs(actual-expected)<=8*EPS*expected,'Derived kinetic/variance arithmetic error')
    for m,e,part,total,rm,re in t['SHARE'].values():
        expected=encoded(m,e)*number(part)/number(total);actual=encoded(rm,re)
        check(actual!=0 and (actual>0)==(expected>0),'Nonzero signed derived parcel disappeared')
        check(abs(actual-expected)<=4*EPS*abs(expected),'Wide derived parcel ratio error')
    expected_terms=set();expected_components=set()
    for identifier,(count,components,sign,m,e) in t['SUM'].items():
        count,components,sign=map(int,(count,components,sign));check(count>0 and 0<=components<=count,'Expansion component count')
        inputs=[encoded(*t['TERM'][identifier,i]) for i in range(count)];parts=[encoded(*t['COMP'][identifier,i]) for i in range(components)]
        expected_terms.update((identifier,i) for i in range(count));expected_components.update((identifier,i) for i in range(components))
        total=sum(inputs,F(0));check(sum(parts,F(0))==total,'Expansion lost a finite additive operand')
        check(sign==((total>0)-(total<0)),'Exact expansion sign mismatch')
        check(all(v!=0 for v in parts),'Expansion contains a redundant zero')
        check(abs(encoded(m,e)-total)<=4*EPS*sum(map(abs,parts),F(0)),'Expansion estimate differs from its exact components')
    check(set(t['TERM'])==expected_terms and set(t['COMP'])==expected_components,'Missing/extra expansion operands/components')
    expected_account_terms=set()
    for identifier,(count,within,rm,re,bm,be) in t['ACCOUNT'].items():
        count=int(count);values=[encoded(*t['ATERM'][identifier,i]) for i in range(count)];expected_account_terms.update((identifier,i) for i in range(count))
        total=sum(values,F(0));bound=128*EPS*sum(map(abs,values),F(0))
        check(bool(int(within))==(abs(total)<=bound),'Energy account changed its actual-operand criterion')
        check(abs(encoded(rm,re)-total)<=4*EPS*abs(total),'Reported energy-account residual lost cancellation result')
        check(abs(encoded(bm,be)-bound)<=4*EPS*bound,'Reported energy-account bound mismatch')
    check(set(t['ATERM'])==expected_account_terms,'Missing/extra energy account operands')
    for m,e,projected,rounded in t['PROJECT'].values():
        original=encoded(m,e);actual=number(projected);expected=F(float(original))
        check(actual==expected and bool(int(rounded))==(actual!=original),'Unresolved energy projection is missing or incorrect')

def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=524288,'Energy saved pipe cap');data=parse(raw);review(data);checks=base.CHECKS
    for corruption in ('parcel','component','projection'):
        bad=copy.deepcopy(data)
        if corruption=='parcel':
            v=list(bad['SHARE'][0]);v[4]=number(v[4])*F(101,100);bad['SHARE'][0]=tuple(v)
        elif corruption=='component':del bad['COMP'][0,0]
        else:
            v=list(bad['PROJECT'][0]);v[3]='0';bad['PROJECT'][0]=tuple(v)
        caught=False
        try:review(bad)
        except (ValueError,KeyError):caught=True
        check(caught,'Altered derived-energy record accepted')
    print(json.dumps(dict(Passed=True,SavedSha256=hashlib.sha256(raw).hexdigest(),SavedChecks=checks,CorruptionsRejected=3,
        NativeProcessLaunched=False,IndependentAgentReview=False,Scope='Derived-energy representation only; not yet connected to MAC transport or game')))

if __name__=='__main__':main()
