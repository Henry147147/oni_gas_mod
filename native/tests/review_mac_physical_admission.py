"""Exact saved inventories/accounts and 80-digit EOS; no native calls."""
from pathlib import Path
from fractions import Fraction as F
from decimal import Decimal as D,localcontext
import hashlib,json,sys
SOURCE=Path(sys.argv[3]) if len(sys.argv)>3 else Path(__file__).resolve().parent
sys.path.insert(0,str(SOURCE));import review_mac_closed_world as closed
base=closed.base;check=base.check;near=base.close;number=base.number;EPS=base.EPS
def full(r,i):return base.scaled_energy(r[i],r[i+1])
def parse(raw):
 check(0<len(raw)<=2097152,'Saved stream cap');d=dict(grid=None,domain=None,unchanged=None,materials={},sweeps={},flux={},cells={},masses={},passives={},faces={},results={},complete=None,passes=0)
 for line in raw.decode('ascii').splitlines():
  r=line.split();check(bool(r),'Empty record');tag=r[0]
  if tag=='BGRID':check(len(r)==6 and d['grid'] is None,'Grid shape');d['grid']=tuple(map(number,r[1:]))
  elif tag=='BDOMAIN':check(len(r)==4 and d['domain'] is None,'Domain shape');d['domain']=tuple(map(number,r[1:]))
  elif tag=='BUNCHANGED':check(len(r)==2 and d['unchanged'] is None,'Immutability shape');d['unchanged']=int(r[1])
  elif tag=='BMATERIAL':check(len(r)==8,'Material shape');base.unique(d['materials'],int(r[1]),(int(r[2]),*map(number,r[3:])))
  elif tag in ('BSWEEP','BFLUX'):
   check(len(r)==4,'Flux shape');base.unique(d['sweeps' if tag=='BSWEEP' else 'flux'],tuple(map(int,r[1:3])),number(r[3]))
  elif tag=='BCELL':check(len(r)==5,'Cell shape');base.unique(d['cells'],tuple(map(int,r[1:3])),tuple(map(number,r[3:])))
  elif tag in ('BMASS','BPASSIVE'):
   check(len(r)==5,'Inventory shape');base.unique(d['masses' if tag=='BMASS' else 'passives'],tuple(map(int,r[1:4])),number(r[4]))
  elif tag=='BFACE':check(len(r)==7,'Face shape');base.unique(d['faces'],tuple(map(int,r[1:4])),(number(r[4]),full(r,5)))
  elif tag=='BRESULT':check(len(r)==12,'Result shape');base.unique(d['results'],int(r[1]),(number(r[2]),int(r[3]),*[full(r,i) for i in range(4,12,2)]))
  elif tag=='BEND':check(len(r)==2 and d['complete'] is None,'Completion shape');d['complete']=int(r[1])
  elif line=='PASS intermediate EOS admission preserves actual state accounts':d['passes']+=1
  else:raise ValueError('Unknown saved record: '+tag)
 check(d['grid']==(8,4,1,1,1) and d['domain']==(300,1000000,32) and d['unchanged']==1 and d['complete']==4 and d['passes']==1,'Fixed control identity/completion')
 check(set(d['cells'])=={(e,i) for e in range(4) for i in range(32)},'Cell coverage')
 check(set(d['faces'])=={(e,a,i) for e in range(4) for a in (0,1) for i in range(32)},'Face coverage')
 check(set(d['sweeps'])==set(d['flux'])=={(a,i) for a in (0,1) for i in range(32)} and set(d['results'])=={1,2,3},'Flux/result coverage')
 check(len(d['materials'])==4,'Material coverage');return d
def review(d):
 theta,backtracks,*_=d['results'][1];check(0<theta<1 and backtracks>0,'No intermediate actual admission')
 check(d['results'][2][:2]==d['results'][3][:2]==(1,0),'Donor/unrestricted control changed')
 mass={};entropy={};temperatures={};kinetic={};allowance={}
 for field in ('masses','passives'):
  check(all(0<=e<4 and 0<=i<32 and identity>=0 and value>0 for (e,i,identity),value in d[field].items()),'Invalid positive inventory')
  for e in range(4):
   for i in range(32):check(sum(1 for key in d[field] if key[:2]==(e,i))==2,'Positive bulk/trace/passive row missing')
  for e in (2,3):
   check({(i,s):v for (end,i,s),v in d[field].items() if end==e}=={(i,s):v for (end,i,s),v in d[field].items() if end==1},'Admission changed material/passive transport')
 for e in range(4):
  entropy[e]=D(0);temperatures[e]=[];kinetic[e]=F(0);allowance[e]=D(0)
  for i in range(32):
   amounts={s:v for (end,j,s),v in d['masses'].items() if (end,j)==(e,i)};mass[e,i]=sum(amounts.values())
   check(min(amounts.values())<F(1,10**25),'Tiny trace witness absent')
   volume,u=d['cells'][e,i];check(volume>0 and (e==0 or volume==1),'Physical endpoint volume')
   p,t,_,s=closed.equilibrium(amounts,d['materials'],u,volume);check(p>0 and t<=1000000,'Actual EOS upper/pressure domain')
   if e!=3:check(D(300)-t<=closed.dec(32*EPS)*max(abs(t),D(300)),'Original explicit EOS lower-bound arithmetic allowance')
   else:check(t>=1,'Broad-domain control EOS')
   entropy[e]+=s;temperatures[e].append(t);allowance[e]+=closed.dec(128*EPS*abs(u))/t
  for a in (0,1):
   for i in range(32):
    x,y=i%8,i//8;left=(x+7)%8+8*y if a==0 else x+8*((y+3)%4);right=i
    inertia,j=d['faces'][e,a,i];expected=(mass[e,left]+mass[e,right])/2
    near(inertia,expected,abs(inertia)+expected,'Original half-cell inertia');check(inertia>0,'Positive face inertia')
    kinetic[e]+=j*j/(2*inertia)
 for i in range(32):
  x,y=i%8,i//8;v=d['cells'][0,i][0]
  ql=d['sweeps'][0,i];qr=d['sweeps'][0,(x+1)%8+8*y];qb=d['sweeps'][1,i];qt=d['sweeps'][1,x+8*((y+1)%4)]
  near(v,1+qr-ql+qt-qb,abs(v)+1+sum(map(abs,(ql,qr,qb,qt))),'Original source swept-volume closure')
 transfers=[]
 for (a,i),flux in d['flux'].items():
  x,y=i%8,i//8;left=(x+7)%8+8*y if a==0 else x+8*((y+3)%4);right=i;donor,target=(left,right) if flux>0 else (right,left)
  expected=d['sweeps'][a,i]*mass[0,donor]/d['cells'][0,donor][0]
  near(flux,expected,abs(flux)+abs(expected),'Original donor volume/mass transfer')
  if flux:transfers.append((donor,target,abs(flux)))
 for field in ('masses','passives'):
  identities={s for e,i,s in d[field]}
  for identity in identities:
   expected=[d[field].get((0,i,identity),F(0)) for i in range(32)]
   for donor,target,amount in transfers:
    packet=d[field].get((0,donor,identity),F(0))*amount/mass[0,donor];expected[donor]-=packet;expected[target]+=packet
   old=sum(d[field].get((0,i,identity),F(0)) for i in range(32))
   for e in (1,2,3):
    new=sum(d[field].get((e,i,identity),F(0)) for i in range(32));near(new,old,abs(new)+abs(old),'Original constituent/passive conservation')
    for i in range(32):near(d[field].get((e,i,identity),F(0)),expected[i],abs(d[field].get((e,i,identity),F(0)))+abs(expected[i]),'Actual donor inventory transport')
 for e in (1,2,3):
  terms=[d['cells'][e,i][1] for i in range(32)]+[-d['cells'][0,i][1] for i in range(32)]
  # Use the actual individual face K operands for the original 128-eps bound.
  for a in (0,1):
   momentum=[]
   for i in range(32):
    for end,sign in ((e,1),(0,-1)):
     inertia,j=d['faces'][end,a,i];terms.append(sign*j*j/(2*inertia));momentum.append(sign*j)
   near(sum(momentum),0,sum(map(abs,momentum)),'Original periodic component momentum')
  residual=sum(terms);bound=128*EPS*sum(map(abs,terms));check(abs(residual)<=bound,'Original stored U+face-K bound')
  saved=d['results'][e];near(saved[2],residual,sum(map(abs,terms)),'Actual energy residual');near(saved[3],bound,abs(bound),'Unchanged energy bound')
  change=entropy[e]-entropy[0];allowed=allowance[e]+allowance[0];check(change>=-allowed,'Original actual-state entropy inequality')
  if e!=2:
   check(abs(closed.dec(saved[4])-change)<=allowed,'Actual entropy report');near(saved[5],F(allowed),abs(saved[5]),'Original entropy allowance')
 check(min(temperatures[3])<D('299.99') and min(temperatures[2])>=D(300)-closed.dec(32*EPS*300),'Unsafe full correction and safe donor witnesses')
 return dict(Passed=True,IntermediateFraction=float(theta),RejectedCandidates=backtracks,MinimumAcceptedTemperature=str(min(temperatures[1])),MinimumDonorTemperature=str(min(temperatures[2])),MinimumUnrestrictedTemperature=str(min(temperatures[3])),SavedChecks=base.CHECKS,NativeProcessLaunched=False,Scope='Actual prescribed 2D deformed EOS admission at the original 32-epsilon temperature boundary, with exact canonical inventories and original energy/momentum/entropy bounds. No pressure step, strong gate or game acceptance.')
def main():
 raw=Path(sys.argv[1]).read_bytes()
 with localcontext() as context:context.prec=80;result=review(parse(raw))
 rejected=[]
 for name,tag,at in [('intermediate-fraction','BRESULT',2),('actual-energy','BCELL',4),('trace-mass','BMASS',4)]:
  lines=raw.decode('ascii').splitlines();index=next(i for i,line in enumerate(lines) if line.startswith(tag+' 1 ') and (tag!='BMASS' or abs(float(line.split()[4]))<1e-20));r=lines[index].split();r[at]='0' if tag=='BRESULT' else str(float(r[at])*1.01);lines[index]=' '.join(r)
  try:
   with localcontext() as context:context.prec=80;review(parse(('\n'.join(lines)+'\n').encode('ascii')))
  except (ValueError,KeyError,AssertionError):rejected.append(name)
  else:raise ValueError('Corrupted actual boundary control accepted: '+name)
 result.update(CorruptionsRejected=rejected,InputSha256=hashlib.sha256(raw).hexdigest(),AnalysisSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
 with Path(sys.argv[2]).open('x') as f:json.dump(result,f,indent=2);f.write('\n')
 print(json.dumps(result))
if __name__=='__main__':main()
