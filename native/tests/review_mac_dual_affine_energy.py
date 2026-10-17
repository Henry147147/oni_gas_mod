"""Exact saved-data counterfactual, not a native solver acceptance run."""
from pathlib import Path
from fractions import Fraction as F
from decimal import Decimal as D,localcontext
import importlib.util,hashlib,json,sys
BASE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('affine_data',BASE/'review_mac_dual_affine.py');ref=importlib.util.module_from_spec(spec);spec.loader.exec_module(ref)
base=ref.base;closed=ref.closed;EPS=base.EPS

def analyze(d,c):
 case=d['ordinary'][c];axis,control,mode,a,b=d['config'][c];assert mode==2
 topology=base.topology(case);mass=[sum(v for (e,j,_),v in case['masses'].items() if (e,j)==(1,i)) for i in range(20)]
 fields=[]
 for component in (0,1):
  rows={i:case['faces'][component,i] for i in range(len(topology[component]))}
  j={i:case['mfaces'][component,i][0] for i in rows};q={i:case['mfaces'][component,i][5] for i in rows};t={i:case['mfaces'][component,i][6] for i in rows}
  for (part,index),event in sorted(case['mevents'].items()):
   if part!=component:continue
   left,right,_=case['mduals'][component,event[0]];request=event[11]
   ul,ur=j[left]/rows[left][4],j[right]/rows[right][4]
   work=(ul+ur)*request/2;linear=(ul-ur)*request/2
   q[left]+=linear-request*request/(2*rows[left][4]);q[right]+=linear-request*request/(2*rows[right][4])
   t[left]-=work;t[right]+=work;j[left]-=request;j[right]+=request
  fields.append((rows,j,q,t))
 # Admit the exact all-request candidate then round canonical J/U once.
 deposit=[F(0) for i in range(20)];local_ok=True
 for component,(rows,j,q,t) in enumerate(fields):
  for i,row in rows.items():
   kt=j[i]*j[i]/(2*row[4]);terms=(t[i],kt,q[i],row[9]);local_ok &= abs(t[i]-kt-q[i]-row[9])<=128*EPS*sum(map(abs,terms))
   cells=[k for k in topology[component][i] if k is not None];total=sum(mass[k] for k in cells)
   for k in cells:deposit[k]+=(q[i]+row[9])*mass[k]/total
 u=[F(float(case['mcells'][i]+deposit[i])) for i in range(20)]
 stored_j=[{i:F(float(v)) for i,v in f[1].items()} for f in fields]
 source_energy_terms=[];target_energy_terms=[];momentum=[]
 for i in range(20):source_energy_terms.append(case['cells'][0,i]);target_energy_terms.append(u[i])
 for component,(rows,j,q,t) in enumerate(fields):
  terms=[]
  for i,row in rows.items():
   source_energy_terms.append(row[1]*row[1]/(2*row[3]));target_energy_terms.append(stored_j[component][i]*stored_j[component][i]/(2*row[4]));terms.extend((stored_j[component][i],-row[1],-row[10]))
  momentum.append(abs(sum(terms))<=128*EPS*sum(map(abs,terms)))
 residual=sum(target_energy_terms)-sum(source_energy_terms);bound=128*EPS*sum(map(abs,source_energy_terms+target_energy_terms))
 entropy=D(0);allowance=D(0);minimum_temperature=None;maximum_temperature=None;valid=True
 for endpoint in (0,1):
  for i in range(20):
   masses={s:v for (e,k,s),v in case['masses'].items() if (e,k)==(endpoint,i)}
   volume=case['rcells'][i][0] if endpoint==0 else F(1);energy=case['cells'][0,i] if endpoint==0 else u[i]
   p,temp,_,s=closed.equilibrium(masses,d['materials'],energy,volume)
   if endpoint==1:
    minimum_temperature=temp if minimum_temperature is None else min(minimum_temperature,temp);maximum_temperature=temp if maximum_temperature is None else max(maximum_temperature,temp);valid &= p>0 and 1<=temp<=1000000
   entropy+=(s if endpoint else -s);allowance+=closed.dec(128*EPS*abs(energy))/temp
 affine=[]
 for other in range(2):
  for f in (4,5,6):
   node=d['nodes'][c,other,f];face=node[0];actual=node[6];candidate=node[7];expected=node[8];new=stored_j[axis][face]
   assert abs(new-candidate)<=128*EPS*(abs(new)+abs(candidate))
   tol=128*EPS*(abs(expected)+abs(new));affine.append(dict(Face=f,Other=other,ActualPassed=abs(actual-expected)<=tol,AllRequestPassed=abs(new-expected)<=tol,AllRequestError=float(new-expected)))
 minimum_q=min(q for f in fields for q in f[2].values());minimum_t=min(t for f in fields for t in f[3].values())
 return dict(Axis=axis,Control=control,NegativeRemainders=sum(v<0 for f in fields for v in f[2].values()),MinimumRemainder=str(closed.dec(minimum_q)),MinimumTransportedKinetic=str(closed.dec(minimum_t)),LocalEnergyAccounts=bool(local_ok),EnergyResidual=str(closed.dec(residual)),OriginalEnergyBound=str(closed.dec(bound)),EnergyWithinOriginalBound=bool(abs(residual)<=bound),MomentumWithinOriginalBounds=all(momentum),EntropyChange=str(entropy),OriginalEntropyAllowance=str(allowance),EntropyWithinOriginalBound=bool(entropy>=-allowance),MinimumTemperature=str(minimum_temperature),MaximumTemperature=str(maximum_temperature),EOSDomain=bool(valid),TransportedKineticNonnegative=bool(minimum_t>=0),SelectedNodes=affine,CounterfactualPhysicalCriteria=bool(local_ok and abs(residual)<=bound and all(momentum) and entropy>=-allowance and valid and minimum_t>=0))

def main():
 raw=Path(sys.argv[1]).read_bytes()
 with localcontext() as context:
  context.prec=80;d=ref.parse(raw);source_review=ref.review(d);assert source_review['DataReviewPassed'];out={str(c):analyze(d,c) for c,config in d['config'].items() if config[2]==2}
 result=dict(DataOnlyCounterfactual=True,SavedSourceReviewed=True,NativeProcessLaunched=False,IndependentAgentReview=False,InputSha256=hashlib.sha256(raw).hexdigest(),AnalysisSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),Cases=out,Scope='Hypothetical exact all-request correction with final canonical rounding. This demonstrates or rejects physical feasibility for these fixtures; it does not implement or validate a replacement operator.')
 with Path(sys.argv[2]).open('x') as f:json.dump(result,f,indent=2);f.write('\n')
 print(json.dumps({k:dict(PhysicalCriteria=v['CounterfactualPhysicalCriteria'],NegativeRemainders=v['NegativeRemainders'],MinT=v['MinimumTemperature'],Entropy=v['EntropyChange'],AffineAfterAllRequests=all(x['AllRequestPassed'] for x in v['SelectedNodes'])) for k,v in out.items()}))
if __name__=='__main__':main()
