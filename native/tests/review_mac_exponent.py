"""Exact integer and symbolic scaled-number review. Never builds 2**huge_exponent."""
import copy
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import sys

sys.set_int_max_str_digits(30000)
CHECKS=0
EPS=F(1,2**52)
def check(ok,why):
    global CHECKS
    CHECKS+=1
    if not ok:raise ValueError(why)
def integer(token):
    check(len(token)<=20001,'Exponent decimal input cap');value=int(token)
    check(str(value)==token and abs(value).bit_length()<=65536,'Noncanonical/oversize exponent integer')
    return value
def number(token):
    value=float(token);check(math.isfinite(value),'Nonfinite significand');return F(value)
def scalar(m,e):
    value,power=number(m),integer(e)
    check((value==0 and power==0) or F(1,2)<=abs(value)<1,'Nonnormal scaled value')
    return value,power
def unique(mapping,key,value):
    check(key not in mapping,'Duplicate saved record');mapping[key]=value
def pow2(power):
    check(abs(power)<=4096,'Reviewer attempted an unbounded power allocation')
    return F(2)**power
def normalized(value,power):
    check(value!=0,'Zero product reference');shift=abs(value).numerator.bit_length()-abs(value).denominator.bit_length()
    mantissa=value/pow2(shift)
    if abs(mantissa)>=1:mantissa/=2;shift+=1
    if abs(mantissa)<F(1,2):mantissa*=2;shift-=1
    check(F(1,2)<=abs(mantissa)<1,'Reference normalization')
    return mantissa,power+shift
def close_scaled(actual,expected,factor=8):
    m,e=actual;reference,power=expected;shift=e-power
    check(abs(shift)<=4096,'Scaled result exponent differs')
    check(abs(m*pow2(shift)-reference)<=factor*EPS*abs(reference),'Scaled result exceeds actual operand rounding bound')

def parse(raw):
    data=dict(ints={},ops={},ownership=None,former=None,kin={},var={},share={},anchors=None,sums={},accounts={},cascade={},limit=None,passes=[])
    for line in raw.decode('ascii').splitlines():
        row=line.split();check(bool(row),'Empty saved record');kind=row[0]
        if kind=='INTEGER':
            check(len(row)==4,'Integer shape');unique(data['ints'],int(row[1]),(integer(row[2]),int(row[3])))
        elif kind=='IOP':
            check(len(row)==8,'Integer operation shape');unique(data['ops'],int(row[1]),tuple(map(integer,row[2:])))
        elif kind=='OWNERSHIP':
            check(len(row)==5 and data['ownership'] is None,'Ownership shape');data['ownership']=tuple(map(integer,row[1:]))
        elif kind=='FORMER':
            check(len(row)==5 and data['former'] is None,'Former rejection shape');data['former']=(scalar(row[1],row[2]),number(row[3]),int(row[4]))
        elif kind=='SKIN':
            check(len(row)==7,'Kinetic shape');unique(data['kin'],int(row[1]),(number(row[2]),scalar(*row[3:5]),scalar(*row[5:7])))
        elif kind=='SVAR':
            check(len(row)==8,'Variance shape');unique(data['var'],int(row[1]),tuple(scalar(*row[i:i+2]) for i in (2,4,6)))
        elif kind=='SSHARE':
            check(len(row)==10,'Share shape');unique(data['share'],int(row[1]),tuple(scalar(*row[i:i+2]) for i in (2,4,6,8)))
        elif kind=='ANCHORS':
            check(len(row)==3 and data['anchors'] is None,'Anchor shape');data['anchors']=tuple(map(integer,row[1:]))
        elif kind in ('XSUM','XACCOUNT'):
            account=kind=='XACCOUNT';check(len(row)==(10 if account else 7),'Expansion shape')
            n,k,sign=map(int,row[2:5]);value=scalar(*row[5:7])
            record=dict(n=n,k=k,sign=sign,value=value,terms={},components={})
            if account:record.update(bound=scalar(*row[7:9]),accepted=int(row[9]))
            unique(data['accounts' if account else 'sums'],int(row[1]),record)
        elif kind in ('XTERM','XCOMP','XATERM','XACOMP'):
            check(len(row)==5,'Expansion operand shape');target=data['accounts' if kind.startswith('XA') else 'sums']
            check(int(row[1]) in target,'Orphan expansion operand');record=target[int(row[1])]
            unique(record['terms' if kind.endswith('TERM') else 'components'],int(row[2]),scalar(*row[3:5]))
        elif kind=='CASCADE':
            check(len(row)==4,'Cascade shape');unique(data['cascade'],int(row[1]),scalar(*row[2:4]))
        elif kind=='LIMIT':
            check(len(row)==6 and data['limit'] is None,'Limit shape');data['limit']=tuple(map(integer,row[1:]))
        elif kind=='PASS':data['passes'].append(line)
        else:raise ValueError('Unknown saved record: '+kind)
    check(len(data['passes'])==6 and len(set(data['passes']))==6,'Native group coverage')
    return data

def buckets(values,anchors):
    result={}
    for m,e in values:
        if m==0:continue
        matches=[a for a in anchors if abs(e-a)<=1024];check(len(matches)==1,'Unbounded or ambiguous expansion cluster')
        a=matches[0];result[a]=result.get(a,F(0))+m*pow2(e-a)
    return {a:v for a,v in result.items() if v}
def sign_of(values):
    if not values:return 0
    # Clusters differ by 2**255 exponent units. Their finite coefficients are
    # bounded below/above by 2**-1024/2**1024, so the leading cluster dominates.
    check(all(F(1,2**1024)<=abs(v)<=F(2)**1024 for v in values.values()),'Cluster dominance bound')
    return 1 if values[max(values)]>0 else -1
def estimated(value,expected):
    if not expected:check(value==(F(0),0),'Zero expansion estimate differs');return
    anchor=max(expected);close_scaled(value,(expected[anchor],anchor),4)

def review(data):
    curated=[0,1,-1,2147483647,2147483648,-2147483648,-3049816383,9223372036854775807,-9223372036854775808,
        9223372036854775808,-9223372036854775809,18446744073709551615,-18446744073709551616,
        10000000000000000000000000000000000000000001,-9999999999999999999999999999999999999999999]
    seed=0x693a17dcb46e0215
    for i in range(32):
        seed=(seed*6364136223846793005+1442695040888963407)%(2**64)
        value=(seed>>1)<<(32+(i%4)*320);curated.append(-value if i%2 else value)
    check(set(data['ints'])==set(range(47)),'Integer source coverage')
    for i,expected in enumerate(curated):
        value,large=data['ints'][i];check(value==expected and large==int(not -(2**63)<=value<2**63),'Integer value/storage differs')
    check(set(data['ops'])==set(range(321)),'Integer arithmetic coverage')
    pairs=[(curated[i],curated[j]) for i in range(15) for j in range(15)]
    for i in range(15,47):pairs.extend(((curated[i],curated[(i+3)%47]),(curated[i],-curated[i]+5),(curated[i],-127)))
    for identifier,(a,b,total,difference,order,doubled) in data['ops'].items():
        check((a,b)==pairs[identifier],'Integer operation input coverage differs')
        check(total==a+b and difference==a-b and order==((a>b)-(a<b)) and doubled==2*a,'Exact signed exponent operation differs')
    anchor=-(1<<255);anchors=(anchor,2*anchor)
    check(data['anchors']==anchors and abs(anchors[0]-anchors[1])>4096,'Expansion anchor/domain differs')
    check(data['ownership']==(3,512,anchor,1),'Shared ownership control differs')
    value,projected,rounded=data['former'];check(value==(F(float('.99960036254833473')),-3049816383) and projected==0 and rounded==1,'Former range rejection not retained')
    check(set(data['kin'])==set(data['var'])==set(data['share'])=={0},'Scaled product coverage')
    for inertia,(m,e),actual in data['kin'].values():
        check(inertia>0,'Nonpositive kinetic inertia');close_scaled(actual,normalized(m*m/(2*inertia),2*e))
    for (mass,em),(du,eu),actual in data['var'].values():close_scaled(actual,normalized(mass*du*du/2,em+2*eu))
    for (m,e),(part,ep),(total,et),actual in data['share'].values():close_scaled(actual,normalized(m*part/total,e+ep-et))
    check(data['share'][0][-1][1]==-8,'Cancelling huge share exponents did not recover finite range')
    check(set(data['sums'])==set(range(6)) and set(data['accounts'])==set(range(5)),'Expansion/account coverage')
    for group in ('sums','accounts'):
        for record in data[group].values():
            check(set(record['terms'])==set(range(record['n'])) and set(record['components'])==set(range(record['k'])),'Expansion operand coverage')
            expected=buckets(record['terms'].values(),anchors);actual=buckets(record['components'].values(),anchors)
            check(actual==expected and record['sign']==sign_of(expected),'Exact additive expansion lost a component or sign');estimated(record['value'],expected)
            if group=='accounts':
                budget=buckets(((abs(m),e-45) for m,e in record['terms'].values()),anchors)
                signed=sign_of(expected);comparison=dict(budget)
                for a,v in expected.items():comparison[a]=comparison.get(a,F(0))-signed*v
                comparison={a:v for a,v in comparison.items() if v}
                check(record['accepted']==int(sign_of(comparison)>=0),'Actual-operand conservation bound differs');estimated(record['bound'],budget)
    check(data['sums'][0]['value']==(F(5,8),2*anchor),'Separated tiny contribution vanished in cancellation')
    check([data['accounts'][i]['accepted'] for i in range(5)]==[1,1,0,1,0],'Required passing/failing account controls differ')
    check(set(data['cascade'])==set(range(257)),'Cascade coverage')
    check(data['cascade'][0]==(F(float('.72004705831217664')),-1941),'Cascade source differs')
    for i in range(1,257):
        m,e=data['cascade'][i-1];rounded,shift=math.frexp(float(m*m))
        check(data['cascade'][i]==(F(rounded),2*e+shift),'Cascaded product significand/exponent differs')
    bits,digits,value,invalid,fatal=data['limit'];check((bits,digits,invalid,fatal)==(65536,20000,11,5),'Resource/negative control declaration differs')
    check(value==1<<(bits-1),'Maximum allocated exponent differs')

def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=524288,'Saved exponent pipe cap')
    data=parse(raw);review(data);checks=CHECKS
    for corruption in ('integer','cascade','tail'):
        bad=copy.deepcopy(data)
        if corruption=='integer':row=list(bad['ops'][0]);row[2]+=1;bad['ops'][0]=tuple(row)
        elif corruption=='cascade':m,e=bad['cascade'][128];bad['cascade'][128]=(m,e+1)
        else:bad['sums'][0]['components'][0]=(F(0),0)
        rejected=False
        try:review(bad)
        except ValueError:rejected=True
        check(rejected,'Altered exponent evidence accepted: '+corruption)
    print(json.dumps(dict(Passed=True,SavedSha256=hashlib.sha256(raw).hexdigest(),SavedChecks=checks,CorruptionsRejected=3,
        NativeProcessLaunched=False,IndependentAgentReview=False,MaximumExponentMagnitudeBits=65536,
        Scope='Bounded signed exponents and symbolic scaled arithmetic; no strong-flow, production, game or performance acceptance')))

if __name__=='__main__':main()
