"""Saved-only exact-fraction review of prescribed dual remap controls."""
from pathlib import Path
from fractions import Fraction as F
from decimal import localcontext
import json,hashlib,sys,copy
BASE=Path(__file__).resolve().parent
sys.path.insert(0,str(BASE))
import review_mac_closed_world as closed
base=closed.base;check=base.check;number=base.number;EPS=base.EPS

def encoded(r,i):return base.scaled_energy(r[i],r[i+1])
def parse(raw):
 d=dict(config={},sweeps={},nodes={},materials={},unchanged={},complete=None);common=[]
 for line in raw.decode('ascii').splitlines():
  r=line.split();check(bool(r),'Empty record');tag=r[0]
  if tag=='DCASE':
   check(len(r)==7,'Diagnostic case shape');key=int(r[1]);check(key not in d['config'],'Duplicate case')
   d['config'][key]=(*map(int,r[2:5]),*map(number,r[5:]))
  elif tag=='DSWEEP':
   check(len(r)==4,'Sweep shape');key=tuple(map(int,r[1:3]));check(key not in d['sweeps'],'Duplicate sweep');d['sweeps'][key]=number(r[3])
  elif tag=='DNODE':
   check(len(r)==17,'Node shape');key=tuple(map(int,r[1:4]));check(key not in d['nodes'],'Duplicate node');d['nodes'][key]=(int(r[4]),*map(number,r[5:10]),encoded(r,10),encoded(r,12),encoded(r,14),number(r[16]))
  elif tag=='DMATERIAL':
   check(len(r)==8,'Material shape');key=int(r[1]);check(key not in d['materials'],'Duplicate material');d['materials'][key]=(int(r[2]),*map(number,r[3:]))
  elif tag=='DUNCHANGED':
   check(len(r)==3,'Immutability shape');key=int(r[1]);check(key not in d['unchanged'],'Duplicate immutability');d['unchanged'][key]=int(r[2])
  elif tag=='DCOMPLETE':
   check(len(r)==2 and d['complete'] is None,'Completion shape');d['complete']=int(r[1])
  else:common.append(line)
 d['ordinary']=base.parse(('\n'.join(common)+'\n').encode('ascii'),required_passes=0,required_cases=range(24))[0]
 check(set(d['config'])==set(range(24)) and d['complete']==24 and set(d['unchanged'])==set(range(24)) and all(d['unchanged'].values()),'Incomplete diagnostic cases')
 check(set(d['sweeps'])=={(c,f) for c in range(24) for f in range(11)},'Incomplete sweeps')
 check(set(d['nodes'])=={(c,o,f) for c in range(24) for o in range(2) for f in (4,5,6)},'Incomplete selected nodes')
 check(len(d['materials'])==4,'Missing reference materials');return d

def near(a,b,operands,why):check(abs(a-b)<=128*EPS*sum(map(abs,operands)),why)
def review(d):
 results={};hypothesis=[]
 for c,(axis,control,mode,a,b) in d['config'].items():
  check(axis in (0,1) and control in range(4) and mode in range(3) and c==(axis*4+control)*3+mode,'Case identity')
  check(a==(1 if control==2 else F(1,4)) and b==(0 if control==2 else F(1,8)),'Fixed affine coefficients')
  q={f:d['sweeps'][c,f] for f in range(11)}
  check(all(q[f]==(0 if f in (0,10) or control==3 else F(1,8) if control==1 else F(f-1,64)) for f in range(11)),'Fixed prescribed sweeps')
  case=d['ordinary'][c];check(case['momentum_mode']==mode and (case['w'],case['h'],case['px'],case['py'])==((10,2,0,1) if axis==0 else (2,10,1,0)),'Declared geometry/mode')
  base.review_case(case)
  # Independently validate physical source volumes, inventories and EOS.
  for i in range(20):
   coordinate=i%10 if axis==0 else i//2;volume=1+q[coordinate+1]-q[coordinate]
   check(case['rcells'][i][0]==volume and volume>0,'Moved material volume')
   for endpoint in (0,1):
    masses={s:v for (e,j,s),v in case['masses'].items() if (e,j)==(endpoint,i)}
    check(len(masses)==2 and all(v>0 for v in masses.values()),'Bulk/trace source lost')
    bulk=max(masses.values());trace=min(masses.values());v=volume if endpoint==0 else F(1)
    near(bulk,v,(bulk,v),'Constant bulk mass density')
    near(trace,v*F(1e-28),(trace,v*F(1e-28)),'Constant positive trace density')
    for identity,density in ((17,F(1)),(2**31,F(1e-30))):
     quantity=case['passives'][endpoint,i,identity];near(quantity,v*density,(quantity,v*density),'Constant positive passive density')
    p,t,_,_=closed.equilibrium(masses,d['materials'],case['cells'][endpoint,i],volume if endpoint==0 else F(1))
    check(p>0 and 1<=t<=1000000,'Independent ideal-mixture EOS domain')
    check(all(v>0 for (e,j,_),v in case['passives'].items() if (e,j)==(endpoint,i)),'Positive passive quantity')
  rows=[]
  for other in range(2):
   for f in (4,5,6):
    face,xl,xc,xr,i0,i1,actual,candidate,expected,min_limiter=d['nodes'][c,other,f]
    check(face==(f+other*11 if axis==0 else other+f*2),'Selected face index')
    check((xl,xc,xr)==(f-1+q[f-1],f+q[f],f+1+q[f+1]),'Actual moved nodal coordinates')
    check(xc-xl==xr-xc and (xl+2*xc+xr)/4==xc,'Nodal and dual-average positions coincide locally')
    source=case['faces'][axis,face]
    check((i0,i1,actual)==(source[3],source[4],source[2]),'Native face source/result identity')
    near(source[1],i0*(a+b*xc),(source[1],i0*(a+b*xc)),'Manufactured physical source velocity')
    near(expected,i1*(a+b*f),(expected,i1*(a+b*f)),'Fixed-grid affine expectation')
    jup=source[2] if mode==0 else case['mfaces'][axis,face][0]
    terms=[jup];limiters=[]
    for (component,k),event in case['mevents'].items():
     if component!=axis:continue
     left,right,_=case['mduals'][axis,event[0]]
     request,limiter=event[11],event[12]
     if left==face:terms.append(-request);limiters.append(limiter)
     if right==face:terms.append(request);limiters.append(limiter)
    near(candidate,sum(terms,F(0)),(*terms,candidate),'Unlimited candidate derived from saved requests')
    check(min_limiter==min([F(1)]+limiters),'Independent admission limiter summary')
    tol=128*EPS*(abs(candidate)+abs(expected));error=candidate-expected
    deformation=q[f+1]-q[f];predicted=-i1*b*deformation*deformation/4 if mode else None
    derived_prediction=(predicted is not None and abs(error-predicted)<=128*EPS*(sum(map(abs,terms))+abs(expected)+abs(predicted)))
    if control==0 and mode:
     hypothesis.append(bool(abs(error)>tol and derived_prediction))
    elif control in (1,2,3):check(abs(error)<=tol,'Identity/translation unlimited affine control')
    rows.append(dict(Face=f,Other=other,ActualError=float(actual-expected),UnlimitedError=float(error),AffineBound=float(tol),MinimumLimiter=float(min_limiter),UnlimitedAffinePassed=bool(abs(error)<=tol),AnalyticBias=None if predicted is None else float(predicted),AnalyticBiasMatched=bool(derived_prediction)))
  results[str((axis,control,mode))]=rows
 return dict(DataReviewPassed=True,HypothesisConfirmed=all(hypothesis),HypothesisWitnesses=len(hypothesis),Checks=base.CHECKS,Profiles=results,NativeProcessLaunched=False,IndependentAgentReview=False,Scope='Prescribed occupied remap affine diagnostic only; no changed operator or strong-flow/game acceptance')

def main():
 raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=2097152,'Raw pipe bound')
 with localcontext() as context:
  context.prec=80;d=parse(raw);out=review(d);mutations=[]
  for name,mutate in (
   ('sweep',lambda x:x['sweeps'].__setitem__((1,5),x['sweeps'][1,5]+F(1,128))),
   ('candidate',lambda x:x['nodes'].__setitem__((1,0,5),(*x['nodes'][1,0,5][:7],x['nodes'][1,0,5][7]+F(1,100),*x['nodes'][1,0,5][8:]))),
   ('trace',lambda x:x['ordinary'][1]['masses'].__setitem__((1,0,min(k[2] for k,v in x['ordinary'][1]['masses'].items() if k[:2]==(1,0) and v<F(1,10**20))),F(1,10**20))),
  ):
   bad=copy.deepcopy(d);mutate(bad)
   try:review(bad)
   except ValueError:mutations.append(name)
   else:raise ValueError('Review accepted corrupted '+name)
 out.update(InputSha256=hashlib.sha256(raw).hexdigest(),CorruptionsRejected=mutations)
 with Path(sys.argv[2]).open('x') as f:json.dump(out,f,indent=2);f.write('\n')
 print(json.dumps(dict(DataReviewPassed=True,HypothesisConfirmed=out['HypothesisConfirmed'],Witnesses=out['HypothesisWitnesses'],Checks=out['Checks'],CorruptionsRejected=mutations)))
if __name__=='__main__':main()
