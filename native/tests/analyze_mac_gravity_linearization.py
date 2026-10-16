"""Linearize the saved column around its hydrostatic state; no native calls.

This independent tridiagonal calculation diagnoses competing spatial/temporal
errors. It does not rerun or accept the failed production gravity controls.
"""
import hashlib,json,sys
from pathlib import Path
path=Path(sys.argv[1]);raw=path.read_bytes();lines=raw.decode('ascii').splitlines()
samples={}; modes={}; materials={}
for line in lines:
    r=line.split()
    if r[0]=='GSAMPLE': samples[int(r[1]),int(r[2])]=list(map(float,r[3:]))
    if r[0]=='GMODE': modes[int(r[1])]=list(map(float,r[2:]))
    if r[0]=='MATERIAL': materials[int(r[1])]=list(map(float,r[2:]))
if set(modes)!={32,64} or set(samples)!={(n,y) for n in modes for y in range(n)}:
    raise ValueError('Expected complete saved N32/N64 first-source profiles')
ni=next(v for v in materials.values() if .028<v[1]<.029)
gamma=1+8.31446261815324/ni[1]/ni[2]

def solve(diag,off,rhs):
    d=diag[:];b=rhs[:]
    for i in range(1,len(d)):
        f=off[i-1]/d[i-1];d[i]-=f*off[i-1];b[i]-=f*b[i-1]
    x=[0.]*len(d);x[-1]=b[-1]/d[-1]
    for i in range(len(d)-2,-1,-1):x[i]=(b[i]-off[i]*x[i+1])/d[i]
    return x

def linear(n,divisor):
    g,amp,c,duration,dt,*_=modes[n];dt/=divisor; steps=n//4*divisor; h=1/n;area=h;vol=h*h
    rho=[(samples[n,i][0]+samples[n,i][1])/vol for i in range(n)]
    p=[samples[n,i][8] for i in range(n)]; m=[r*vol for r in rho]
    inertia=[m[0]/2]+[(m[i-1]+m[i])/2 for i in range(1,n)]+[m[-1]/2]
    u=[samples[n,i][6]/inertia[i] for i in range(n)]+[0.]
    pressure=[0.]*n;mass=[0.]*n
    compliance=[2*vol/(gamma*v) for v in p]
    for _ in range(steps):
        di=compliance[:];off=[0.]*(n-1);rhs=[compliance[i]*pressure[i]-dt*area*(u[i+1]-u[i]) for i in range(n)]
        for f in range(1,n):
            weight=.5*dt*dt*area*area/inertia[f];di[f-1]+=weight;di[f]+=weight;off[f-1]-=weight
            delta_inertia=(mass[f-1]+mass[f])/2
            source=.5*dt*dt*area*g*delta_inertia/inertia[f];rhs[f-1]+=source;rhs[f]-=source
        w=solve(di,off,rhs)
        unew=[0.]+[u[f]+dt*(area*(w[f-1]-w[f])-g*(mass[f-1]+mass[f])/2)/inertia[f] for f in range(1,n)]+[0.]
        mean=[(a+b)/2 for a,b in zip(u,unew)]
        pn=[pressure[i]-(gamma-1)*p[i]*dt*area*(mean[i+1]-mean[i])/vol for i in range(n)]
        mn=mass[:]
        for f in range(1,n):
            donor=f-1 if mean[f]>=0 else f
            flux=dt*area*rho[donor]*mean[f];mn[f-1]-=flux;mn[f]+=flux
            advected_pressure=dt*area*p[donor]*mean[f]/vol;pn[f-1]-=advected_pressure;pn[f]+=advected_pressure
            work=dt*g*area*h*((rho[f-1]+rho[f])/2-rho[donor])*mean[f]
            pn[f-1]+=(gamma-1)*work*m[f-1]/(m[f-1]+m[f])/vol
            pn[f]+=(gamma-1)*work*m[f]/(m[f-1]+m[f])/vol
        u,pressure,mass=unew,pn,mn
    actual=[]
    for y in range(n):
        actual.append(samples[n,y][7]/((samples[n,y][3]+samples[n,y][4])/2+((samples[n,y-1][3]+samples[n,y-1][4])/2 if y else 0)))
    return dict(Subdivision=divisor,LinearVelocityL1=sum(abs(v) for v in u[:-1])/n/amp,
        SignedLinearVelocityMean=sum(u[:-1])/n/amp,
        MaxDifferenceFromSavedVelocityOverAmplitude=max(abs(a-b)/amp for a,b in zip(actual,u)),
        MaxPressureDifferenceFromSavedOverAmplitude=max(abs((samples[n,i][9]-p[i])-pressure[i])/(c*amp) for i in range(n)))
rows=[]
for n in (32,64):
    rows.append(dict(N=n,SavedVelocityL1=modes[n][8],Predictions=[linear(n,d) for d in (1,2,4,8)]))
result=dict(DataOnly=True,NativeProcessLaunched=False,ProductionAcceptance=False,SourceSha256=hashlib.sha256(raw).hexdigest(),
    Model='Independent linearization: material secant-pressure midpoint plus upwind transport and signed potential-work adjustment',
    Records=rows,Interpretation='Compare errors as the diagnostic time discretization is resolved; no native timestep/input/criterion is changed')
print(json.dumps(result))
