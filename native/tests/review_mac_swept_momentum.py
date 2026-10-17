"""Saved-only actual-state checks; no native process or native EOS calls."""
from pathlib import Path
from fractions import Fraction as F
from decimal import Decimal as D, localcontext
import copy,hashlib,importlib.util,json,sys

SOURCE=Path(sys.argv[3]) if len(sys.argv)>3 else Path(__file__).resolve().parent
sys.path.insert(0,str(SOURCE))
import review_mac_closed_world as closed
base=closed.base;check=base.check;near=base.close;number=base.number;EPS=base.EPS
def encoded(row,i):return base.scaled_energy(row[i],row[i+1])
def parse(raw,bounded_energy=False):
 check(0<len(raw)<=16777216,'Saved stream size');common=[]
 d=dict(config={},sweeps={},nodes={},materials={},admit={},counts={},faces={},events={},modes={},baseline={},complete=None)
 for line in raw.decode('ascii').splitlines():
  r=line.split();check(bool(r),'Empty record');tag=r[0]
  if tag=='SWCASE':
   check(len(r) in (8,9),'Case shape');base.unique(d['config'],int(r[1]),(*map(int,r[2:6]),*map(number,r[6:8]),int(r[8]) if len(r)==9 else 2))
  elif tag=='SWSWEEP':
   check(len(r)==4,'Sweep shape');base.unique(d['sweeps'],tuple(map(int,r[1:3])),number(r[3]))
  elif tag=='SWNODE':
   check(len(r)==9,'Node shape');base.unique(d['nodes'],tuple(map(int,r[1:4])),(int(r[4]),encoded(r,5),encoded(r,7)))
  elif tag=='SWMATERIAL':
   check(len(r)==8,'Material shape');base.unique(d['materials'],int(r[1]),(int(r[2]),*map(number,r[3:])))
  elif tag=='SWADMIT':
   check(len(r)==10,'Admission shape');base.unique(d['admit'],int(r[1]),(number(r[2]),int(r[3]),*[encoded(r,i) for i in range(4,10,2)]))
  elif tag=='SWCOUNT':
   check(len(r)==4,'Count shape');base.unique(d['counts'],tuple(map(int,r[1:3])),int(r[3]))
  elif tag=='SWFACE':
   check(len(r)==20,'Swept face shape');base.unique(d['faces'],tuple(map(int,r[1:4])),(*map(number,r[4:6]),*[encoded(r,i) for i in range(6,20,2)]))
  elif tag=='SWEVENT':
   check(len(r)==32,'Event shape');base.unique(d['events'],tuple(map(int,r[1:4])),(*map(int,r[4:8]),*[encoded(r,i) for i in range(8,32,2)]))
  elif tag=='SWCOMPLETE':
   check(len(r)==2 and d['complete'] is None,'Completion shape');d['complete']=int(r[1])
  elif tag=='BWBASE':
   check(bounded_energy and len(r)==6,'Stagnation baseline shape');base.unique(d['baseline'],tuple(map(int,r[1:4])),encoded(r,4))
  elif tag=='MCONFIG':
   mode=4 if bounded_energy else 3
   check(len(r)==3 and r[2]==str(mode),'Physical mode missing');base.unique(d['modes'],int(r[1]),mode)
  else:common.append(line)
 check(d['complete'] in ((32,) if bounded_energy else (22,24)),'Fixture version coverage');count=d['complete']
 d['ordinary']=base.parse(('\n'.join(common)+'\n').encode('ascii'),required_passes=6 if bounded_energy else 4,required_cases=range(count))[0]
 check(set(d['config'])==set(d['admit'])==set(d['modes'])==set(range(count)),'Case coverage')
 check(set(d['counts'])=={(c,a) for c in range(count) for a in (0,1)},'Count coverage')
 check(set(d['nodes'])=={(c,o,f) for c in range(count) for o in range(d['config'][c][-1]) for f in (4,5,6)},'Selected node coverage')
 check(set(d['sweeps'])=={(c,f) for c in range(count) for f in range(11)} and len(d['materials'])==4,'Sweep/material coverage')
 check(set(d['baseline'])==({(c,o,f) for c in range(24,28) for o in range(2) for f in (4,5,6)} if bounded_energy else set()),'Stagnation baseline coverage')
 return d

def review(d,bounded_energy=False):
 nodes=0;negative=0;edge_limited=0;events=0;zero_carrier_corrections=0;baseline_defects=0
 for c,(axis,control,direction,exponent,a,b,transverse) in d['config'].items():
  case=d['ordinary'][c];topology=base.topology(case);base.expected_duals(case)
  check(axis in (0,1) and control in range(7 if bounded_energy else 5) and direction in (-1,1) and exponent in (0,-1200),'Fixed configuration')
  expected_config=(c//10,(c%10)//2,(-1,1)[c%2],0) if c<20 else (c-20,0,1,-1200) if c<22 else (c-22,0,1,0) if c<24 else ((c-24)//2,5,(-1,1)[c%2],0) if c<28 else ((c-28)//2,6,(-1,1)[c%2],0)
  check((axis,control,direction,exponent)==expected_config,'Case ordering')
  check(a==(direction*F(-5,8) if control>=5 else 1 if control==2 else F(-1,8) if control==4 else F(1,4)) and b==(direction*F(1,8) if control>=5 else 0 if control==2 else F(1,8)),'Affine coefficients')
  check(transverse==(1 if 22<=c<24 else 2),'Transverse dimension')
  check((case['w'],case['h'],case['px'],case['py'])==((10,transverse,0,int(transverse>1)) if axis==0 else (transverse,10,int(transverse>1),0)),'Grid identity')
  nc=case['w']*case['h']
  q={f:d['sweeps'][c,f] for f in range(11)}
  for f in range(11):
   prescribed=F(0) if f in (0,10) or control==3 else direction*(f-5)*F(2)**-80 if control==6 else direction*F(f-5,64-direction) if control==5 else direction*(F(1,8) if control==1 else F(f-1,64))
   near(q[f],prescribed,abs(q[f])+abs(prescribed),'Prescribed sweeps')
  if control==4 and direction==1:
   check(all(q[f]==F(8,65)*(a+b*(f+q[f])) for f in range(1,10)),'Actual kinematic q=dt*u control')
  if control>=5:
   dt=F(1,8) if control==5 else F(2)**-77
   for f in range(1,10):near(q[f],dt*(a+b*(f+q[f])),abs(q[f])+abs(dt*(a+b*(f+q[f]))),'Stagnation kinematic q=dt*u control')
  theta,backtracks,saved_entropy,saved_allowance,saved_work=d['admit'][c]
  check(theta==1 and backtracks==0,'Physically admissible affine state restricted')
  mass=[[sum(v for (e,j,s),v in case['masses'].items() if (e,j)==(end,i)) for i in range(nc)] for end in (0,1)]
  targets=base.review_reconstructed_primal(case,topology)
  for field in ('masses','passives'):
   identities={key[2] for key in case[field]}
   for identity in identities:
    old=sum(case[field].get((0,i,identity),F(0)) for i in range(nc));new=sum(case[field].get((1,i,identity),F(0)) for i in range(nc))
    near(new,old,abs(old)+abs(new),'Original global species/passive balance')
    for i in range(nc):near(case[field].get((1,i,identity),F(0)),targets[field][i].get(identity,F(0)),abs(case[field].get((1,i,identity),F(0)))+abs(targets[field][i].get(identity,F(0))),'Actual parcel species/passive')
  deposit=[F(0) for i in range(nc)];energy_terms=[];work_terms=[];entropy=D(0);allowance=D(0)
  for i in range(nc):
   coordinate=i%10 if axis==0 else i//transverse;volume=1+q[coordinate+1]-q[coordinate]
   if control>=5:near(case['rcells'][i][0],volume,abs(case['rcells'][i][0])+abs(volume),'Physical source volume')
   else:check(case['rcells'][i][0]==volume,'Physical source volume')
   volume=case['rcells'][i][0]
   for end in (0,1):
    m={s:v for (e,j,s),v in case['masses'].items() if (e,j)==(end,i)}
    check(len(m)==2 and all(v>0 for v in m.values()),'Positive bulk/trace gas lost')
    v=volume if end==0 else F(1)
    near(max(m.values()),v,v,'Bulk density');near(min(m.values()),v*F(1e-28),v*F(1e-28),'Tiny trace density')
    for identity,density in ((17,F(1)),(2**31,F(1e-30))):near(case['passives'][end,i,identity],v*density,v*density,'Positive passive density')
    u=case['cells'][end,i];p,t,_,s=closed.equilibrium(m,d['materials'],u,v)
    check(p>0 and 1<=t<=1000000,'Independent actual EOS domain')
    entropy+=s if end else -s;allowance+=closed.dec(128*EPS*abs(u))/t
    energy_terms.append(u if end else -u)
  check(entropy>=-allowance,'Original actual-state entropy inequality')
  check(abs(closed.dec(saved_entropy)-entropy)<=allowance,'Reported actual entropy')
  near(saved_allowance,F(allowance),abs(saved_allowance),'Original entropy allowance')
  correction_sources={}
  for component in (0,1):
   n=len(topology[component]);rows=[case['faces'][component,i] for i in range(n)]
   check({key for key in d['faces'] if key[:2]==(c,component)}=={(c,component,i) for i in range(n)},'Swept face coverage')
   weights=base.packets([r[3] for r in rows],[(l,r,f) for (part,l,r),f in case['duals'].items() if part==component])
   j=[];kt=[];signed=[F(0) for i in range(n)];momentum_terms=[];correction_operands=[F(0) for i in range(n)]
   for i,(r,donors) in enumerate(zip(rows,weights)):
    _,j0,j1,i0,i1,k0,k1,t,qm,qw,wall,dm=r
    check(min(i0,i1,k0,k1,t,qm,qw)>=0,'Negative positive energy account')
    for end,inertia in ((0,i0),(1,i1)):
     expected=sum(mass[end][cell] for cell in topology[component][i] if cell is not None)/2
     near(inertia,expected,abs(inertia)+expected,'Actual dual inertia')
    near(k0,j0*j0/(2*i0) if i0 else F(0),abs(k0),'Source face K');near(k1,j1*j1/(2*i1) if i1 else F(0),abs(k1),'Target face K')
    up=sum((amount*rows[donor][1]/rows[donor][3] for donor,amount in donors if amount),F(0))
    t0=sum((amount*rows[donor][5]/rows[donor][3] for donor,amount in donors if amount),F(0))
    fixed=None in topology[component][i];provisional=up/i1 if i1 else F(0)
    qp=sum((amount*(rows[donor][1]/rows[donor][3]-provisional)**2/2 for donor,amount in donors if amount),F(0))
    f=d['faces'][c,component,i]
    if not fixed:
     coordinate=(i%(11 if component==0 else 10)) if axis==0 else i//transverse
     if component==axis:
      vl=1+q[coordinate]-q[coordinate-1];vr=1+q[coordinate+1]-q[coordinate]
      source_volume=(vl+vr)/2;ql=(q[coordinate-1]+q[coordinate])/2;qr=(q[coordinate]+q[coordinate+1])/2
     else:
      source_volume=1+q[coordinate+1]-q[coordinate];ql=q[coordinate];qr=q[coordinate+1]
     centre=(qr*(F(1,2)+qr/2)+ql*(F(1,2)-ql/2))/source_volume
     near(f[axis],centre,abs(f[axis])+abs(centre),'Independent swept dual first moment')
     near(f[1-axis],0,abs(f[1-axis]),'Invariant transverse first moment')
     if component==axis and 3<=coordinate<=7:
      near(f[3+axis],b*F(2)**exponent,abs(f[3+axis])+abs(b*F(2)**exponent),'Physical affine source gradient')
    near(f[2],0 if fixed else up,abs(up)+abs(f[2]),'Actual upwind J')
    near(f[5],qp,abs(qp)+abs(t0),'Nonnegative upwind variance')
    near(f[6],t0,abs(t0),'Upwind transported K')
    near(qm,f[5],abs(qm)+abs(f[5]),'Signed work must not replace positive mixing heat')
    near(qw,up*up/(2*i1) if fixed and i1 else F(0),abs(t0)+qw,'Wall heat')
    near(wall,-up if fixed else F(0),abs(up)+abs(wall),'Wall impulse')
    j.append(f[2]);kt.append(f[6]);momentum_terms.extend((j1,-j0,-wall));energy_terms.extend((k1,-k0))
    transported=sum(amount for donor,amount in donors);near(transported,i1,transported+i1,'Dual mass transport');near(dm,transported-i1,transported+i1,'Dual mass residual')
   count=d['counts'][c,component];keys=sorted(k for k in d['events'] if k[:2]==(c,component));check(keys==[(c,component,i) for i in range(count)],'Event coverage')
   for key in keys:
    edge,transport_axis,left,right,pred,bounded,request,impulse,limit,jl,jr,al,ar,w,dl,dr=d['events'][key]
    check(0<=left<n and 0<=right<n and left!=right and transport_axis in (0,1),'Event topology')
    check(None not in topology[component][left] and None not in topology[component][right],'Correction at fixed wall')
    flux=case['duals'][component,left,right];check(flux!=0 and 0<=limit<=1,'Work limiter and flow')
    check(component==axis and transport_axis==axis,'Invariant component acquired a spurious correction')
    donor=left if flux>0 else right;target=right if flux>0 else left
    left_coordinate=left%11 if axis==0 else left//transverse
    qdual=(q[left_coordinate]+q[left_coordinate+1])/2
    source_face=d['faces'][c,component,donor]
    geometric_delta=source_face[3+axis]*((F(1,2) if flux>0 else -F(1,2))+qdual/2-source_face[axis])
    near(pred,geometric_delta,abs(pred)+abs(geometric_delta),'Independent swept slab centroid predictor')
    difference=rows[target][1]/rows[target][3]-rows[donor][1]/rows[donor][3]
    expected_bound=(min(abs(pred),abs(difference))*(1 if pred>0 else -1)) if pred*difference>0 else F(0)
    near(bounded,expected_bound,abs(bounded)+abs(expected_bound),'Original donor-target velocity bound')
    near(request,flux*bounded,abs(request)+abs(flux*bounded),'Geometric impulse request')
    near(impulse,theta*(1 if bounded_energy else limit)*request,abs(impulse)+abs(request),'Actual admitted impulse')
    near(jl,j[left],abs(jl)+abs(j[left]),'Sequential left before J');near(jr,j[right],abs(jr)+abs(j[right]),'Sequential right before J')
    near(al,jl-impulse,abs(al)+abs(jl)+abs(impulse),'Left stored J');near(ar,jr+impulse,abs(ar)+abs(jr)+abs(impulse),'Right stored J')
    ul=jl/rows[left][4];ur=jr/rows[right][4];linear=(ul-ur)*impulse/2
    desired=(ul+ur)*impulse/2;difference=desired-w if bounded_energy else F(0)
    near(w,desired*(limit if bounded_energy else 1),abs(w)+abs(desired),'Compatible carried energy')
    if bounded_energy:
     available=kt[left] if desired>0 else kt[right]
     expected_limit=min(F(1),max(F(0),available)/abs(desired)) if desired else F(1)
     near(limit,expected_limit,abs(limit)+abs(expected_limit),'Original nonnegative carried-energy budget')
     zero_carrier_corrections+=limit==0 and impulse!=0
    near(dl,linear-impulse*impulse/(2*rows[left][4])+difference,abs(dl)+abs(linear)+impulse*impulse/(2*rows[left][4])+abs(difference),'Signed left work energy')
    near(dr,linear-impulse*impulse/(2*rows[right][4])-difference,abs(dr)+abs(linear)+impulse*impulse/(2*rows[right][4])+abs(difference),'Signed right work energy')
    near((al*al-jl*jl)/(2*rows[left][4])+dl+w,0,(al*al+jl*jl)/(2*rows[left][4])+abs(dl)+abs(w),'Original left pair-work account')
    near((ar*ar-jr*jr)/(2*rows[right][4])+dr-w,0,(ar*ar+jr*jr)/(2*rows[right][4])+abs(dr)+abs(w),'Original right pair-work account')
    kt[left]-=w;kt[right]+=w;signed[left]+=dl;signed[right]+=dr;j[left]=al;j[right]=ar
    correction_operands[left]+=abs(impulse);correction_operands[right]+=abs(impulse)
    check(kt[left]>=-128*EPS*(abs(w)+abs(d['faces'][c,component,left][6])) and kt[right]>=-128*EPS*(abs(w)+abs(d['faces'][c,component,right][6])),'Original nonnegative transported work budget')
    edge_limited+=limit<1;events+=1
   for i,r in enumerate(rows):
    f=d['faces'][c,component,i];near(r[2],j[i],abs(r[2])+abs(j[i]),'Actual completed J')
    near(r[7],kt[i],abs(r[7])+abs(kt[i])+abs(f[6]),'Actual transported work')
    near(f[7],signed[i],abs(f[7])+sum(abs(v) for v in signed),'Separate signed work accumulation')
    balance=(r[7],-r[6],-r[8],-r[9],-f[7]);check(abs(sum(balance))<=f[8],'Composed local work/K/heat account')
    work_terms.append(f[7]);negative+=r[8]+f[7]<0
    total=sum(mass[1][cell] for cell in topology[component][i] if cell is not None)
    for cell in topology[component][i]:
     if cell is not None and total:deposit[cell]+=(r[8]+r[9]+f[7])*mass[1][cell]/total
   near(sum(momentum_terms),0,sum(map(abs,momentum_terms)),'Original J and wall balance')
   correction_sources[component]=correction_operands
  for i in range(nc):
   advected=sum(targets['energy'][i]);near(case['mcells'][i],advected,abs(case['mcells'][i])+sum(map(abs,targets['energy'][i])),'Advected U')
   near(case['cells'][1,i],advected+deposit[i],abs(case['cells'][1,i])+abs(advected)+abs(deposit[i]),'Actual signed-work material deposition')
  near(sum(energy_terms),0,sum(map(abs,energy_terms)),'Original stored U+face-K balance')
  near(case['residual'],sum(energy_terms),sum(map(abs,energy_terms)),'Actual stored energy residual')
  near(case['bound'],128*EPS*sum(map(abs,energy_terms)),abs(case['bound']),'Original energy bound')
  near(saved_work,sum(work_terms),sum(map(abs,work_terms)),'Separate signed work summary')
  scale=F(2)**exponent
  for other in range(transverse):
   for face_number in (4,5,6):
    face,actual,expected=d['nodes'][c,other,face_number]
    check(face==(face_number+11*other if axis==0 else other+transverse*face_number),'Node index')
    near(expected,scale*case['faces'][axis,face][4]*(a+b*face_number),abs(expected),'Analytical fixed-grid affine value')
    check(actual==case['faces'][axis,face][2],'Selected actual stored result')
    scale_account=abs(actual)+abs(expected)
    if control>=5 and expected==0:scale_account+=abs(d['faces'][c,axis,face][2])+correction_sources[axis][face]
    near(actual,expected,scale_account,'Actual affine criterion');nodes+=1
    if control==5:
     baseline=d['baseline'][c,other,face_number]
     baseline_defects+=abs(baseline-expected)>128*EPS*(abs(baseline)+abs(expected))
 check(nodes==(186 if bounded_energy else 138 if d['complete']==24 else 132) and negative>0 and edge_limited>0,'Mandatory affine, signed-energy and work-limiter witnesses')
 if bounded_energy:check(zero_carrier_corrections>0 and baseline_defects>=8,'Missing nonzero correction at zero carrier or original stagnation defect')
 return dict(Passed=True,ActualAffineNodes=nodes,NegativeCombinedRemainders=negative,WorkLimitedEdges=edge_limited,CompatibleEvents=events,ZeroCarrierCorrections=zero_carrier_corrections,OriginalStagnationDefects=baseline_defects,TinyStationaryCases=4 if bounded_energy else 0,SavedChecks=base.CHECKS,Scope='Actual transport fixtures, including both sweep signs, q=dt*u, below-binary64 canonical J and both true one-dimensional axes. Bounded-energy mode adds interior stagnation and tiny-transfer controls. No strong-flow or production acceptance.')

def main():
 raw=Path(sys.argv[1]).read_bytes()
 with localcontext() as context:context.prec=80;d=parse(raw);result=review(d)
 rejected=[]
 for name,tag,at in [('actual-node','SWNODE',5),('paired-work','SWEVENT',26),('trace-inventory','MASS',5)]:
  lines=raw.decode('ascii').splitlines()
  index=next(i for i,line in enumerate(lines) if line.startswith(tag+' ') and (tag!='MASS' or 'e-28' in line or 'e-29' in line))
  r=lines[index].split();r[at]=str(float(r[at])*1.01 if float(r[at]) else .01);lines[index]=' '.join(r)
  try:
   with localcontext() as context:context.prec=80;review(parse(('\n'.join(lines)+'\n').encode('ascii')))
  except (ValueError,KeyError,AssertionError):rejected.append(name)
  else:raise ValueError('Corrupted saved stream accepted: '+name)
 result.update(InputSha256=hashlib.sha256(raw).hexdigest(),AnalysisSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),NativeProcessLaunched=False,CorruptionsRejected=rejected)
 with Path(sys.argv[2]).open('x') as out:json.dump(result,out,indent=2);out.write('\n')
 print(json.dumps(result))
if __name__=='__main__':main()
