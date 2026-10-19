"""Saved-only coupled owner review: actual U/J/geometry, EOS and every trace."""
from pathlib import Path
from fractions import Fraction as F
from decimal import Decimal as D,localcontext
import copy,hashlib,importlib.util,json,sys

spec=importlib.util.spec_from_file_location('cw',Path(__file__).with_name('review_mac_compressive_work.py'))
cw=importlib.util.module_from_spec(spec);spec.loader.exec_module(cw)
base=cw.base;check,close,number,unique=base.check,base.close,base.number,base.unique
EPS=base.EPS;dec=cw.dec

def parse(raw):
    records={k:{} for k in ('cases','walls','inventory','masses','passive','faces','work')};checkpoints=set();clock=None;rejected=None
    shared=[]
    for line in raw.decode('ascii').splitlines():
        r=line.split();check(bool(r),'Empty owner record');kind=r[0]
        if kind.startswith('CW') or kind=='PASS':shared.append(line);continue
        if kind=='COCASE':
            check(len(r)==17,'Owner case shape');unique(records['cases'],int(r[1]),(*map(int,r[2:6]),*map(number,r[6:])))
        elif kind in ('COWALL','COWORK'):
            check(len(r)==4,'Cell mapping shape');unique(records['walls' if kind=='COWALL' else 'work'],tuple(map(int,r[1:3])),int(r[3]))
        elif kind=='COINV':
            check(len(r)==9,'Owner inventory shape');unique(records['inventory'],tuple(map(int,r[1:4])),tuple(map(number,r[4:])))
        elif kind in ('COMASS','COPASSIVE'):
            check(len(r)==6,'Owner constituent shape');unique(records['masses' if kind=='COMASS' else 'passive'],tuple(map(int,r[1:5])),number(r[5]))
        elif kind=='COFACE':
            check(len(r)==11,'Owner face shape');unique(records['faces'],tuple(map(int,r[1:4])),tuple(map(number,r[4:])))
        elif kind=='COCHECKPOINT':
            check(len(r)==4 and tuple(map(int,r[1:])) not in checkpoints,'Checkpoint shape/duplicate');checkpoints.add(tuple(map(int,r[1:])))
        elif kind=='COCLOCK':
            check(len(r)==4 and clock is None,'Clock shape');clock=tuple(map(int,r[1:]))
        elif kind=='COREJECTED':
            check(len(r)==2 and rejected is None,'Rejection shape');rejected=int(r[1])
        else:raise ValueError('Unknown owner record '+kind)
    check(set(records['cases'])==set(range(4)) and rejected==3,'Missing scenes or atomicity controls')
    check(clock is not None and clock[0]==3 and clock[1]>0 and clock[2]>=0,'Missing actual owner heat/clock comparison')
    check(checkpoints=={(wet,material,heat) for wet in range(2) for material in range(3) for heat in range(4)},'Missing exact continuation setting')
    records['cw']=cw.parse(('\n'.join(shared)+'\n').encode('ascii'),case_ids=tuple(range(29)),profiles=(0,),groups=7,rejections=None,large_offset_control=False)
    return records

def review(d):
    cw.review(d['cw'],fixture_controls=False)
    definitions={i:q for (profile,i),q in d['cw']['definitions'].items() if profile==0};materials={i:q[:6] for i,q in definitions.items()}
    expected_work={};expected_inv=set();expected_faces=set();expected_walls=set()
    for c,row in d['cases'].items():
        w,h,px,py,dt,spacing,depth,gravity,wx,wy,body,tx,ty,pbound,ebound=row
        check(w>1 and h>1 and px in (0,1) and py in (0,1) and min(dt,spacing,depth)>0 and gravity>=0,'Invalid owner geometry/time')
        n=w*h;volume=spacing*spacing*depth;area=spacing*depth;wall={i:d['walls'][c,i] for i in range(n)}
        check(all(value in (0,1) for value in wall.values()),'Invalid wall mask');expected_walls.update((c,i) for i in range(n))
        mass={};energy={};entropies={};readings={}
        for stage in range(3):
            for i in range(n):
                expected_inv.add((c,stage,i));v,u,p,t,s=d['inventory'][c,stage,i]
                constituents={species:m for (j,e,cell,species),m in d['masses'].items() if (j,e,cell)==(c,stage,i)}
                check(all(m>0 and species in materials for species,m in constituents.items()),'Invalid constituent')
                mass[stage,i]=sum(constituents.values(),F(0));energy[stage,i]=u;entropies[stage,i]=s
                if wall[i]:check(not constituents and u==p==t==s==0,'Wall gained a fluid inventory');continue
                ep,et,es,_=cw.thermal.eos(constituents,materials,u,v)
                for actual,expected,name in ((p,ep,'p'),(t,et,'T'),(s,es,'S')):cw.reading(actual,expected,'Owner actual '+name)
                check(v>0 and p>0 and t>0,'Owner left occupied EOS');readings[stage,i]=(v,u,p,t,s)
                check(stage==1 or v==volume,'Eulerian cell volume changed')
                if stage==1:
                    ident=d['work'][c,i];expected_work[c,i]=ident
                    check(d['cw']['end'][ident]==(v,u,p,t,s),'Pressure inventory is not its completed actual constitutive endpoint')
                    check(d['cw']['initial'][ident]==readings[0,i][1:],'Pressure response has a different source')
                    check(d['cw']['cases'][ident][1:5]==(volume,d['cw']['cases'][ident][2],spacing,dt),'Pressure source geometry/time differs')
            for family in ('masses','passive'):
                identities={key[3] for key in d[family] if key[0]==c}
                for species in identities:
                    before=sum((x for (j,e,i,k),x in d[family].items() if (j,e,k)==(c,0,species)),F(0))
                    after=sum((x for (j,e,i,k),x in d[family].items() if (j,e,k)==(c,stage,species)),F(0))
                    close(after,before,abs(before)+abs(after),'Every constituent/passive relative conservation')
                    for i in range(n):
                        value=d[family].get((c,stage,i,species),F(0));check(value>=0,'Negative remapped constituent/passive')
                        if stage==1:check(value==d[family].get((c,0,i,species),F(0)),'Pressure step changed a trace or passive')
        def neighbour(i,axis,forward):
            coordinate=i%w if axis==0 else i//w;extent=w if axis==0 else h;periodic=px if axis==0 else py
            if not periodic and (coordinate+1==extent if forward else coordinate==0):return i
            at=(coordinate+(1 if forward else extent-1))%extent;target=at+(i//w)*w if axis==0 else i%w+at*w
            return i if wall[target] else target
        curvature={}
        for i in range(n):
            if wall[i]:continue
            for axis in range(2):
                left=readings[0,neighbour(i,axis,False)][2];centre=readings[0,i][2];right=readings[0,neighbour(i,axis,True)][2]
                curvature[i,axis]=abs(left-2*centre+right)/(left+2*centre+right)
        for i in range(n):
            if wall[i]:continue
            v,u,p,t,s=readings[0,i];constituents={species:m for (j,e,cell,species),m in d['masses'].items() if (j,e,cell)==(c,0,i)}
            liquid,elastic,capacity,offset,moles=cw.thermal.ref.eos_values(constituents,materials,dec(p))
            gas_volume=moles*dec(cw.R)*dec(t)/dec(p)
            compliance=gas_volume*capacity/(capacity+moles*dec(cw.R))/dec(p)
            for species,m in constituents.items():
                phase,molar,cv,off,rho,bulk=materials[species]
                if phase==1:compliance+=dec(m)/dec(rho)/dec(bulk)/(1+dec(p)/dec(bulk))**2
            speed=dec(v)/(dec(mass[0,i])*compliance).sqrt()/2
            sensor=max(curvature[neighbour(i,axis,forward),axis] for axis in range(2) for forward in (False,True))
            sensor=max(sensor,curvature[i,0],curvature[i,1]);actual=d['cw']['cases'][d['work'][c,i]][5]
            cw.reading(actual,speed*dec(sensor),'Frozen isotropic incoming pressure-sensed coefficient')
        sweeps={i:F(0) for i in range(n)};wall_impulse=[F(0),F(0)];body_actual=F(0);pressure_kinetic=F(0);total_kinetic=F(0);body_work=F(0)
        pressure_scale=F(0);momentum=[[],[]]
        for axis in range(2):
            width=w if axis or px else w+1;height=h if not axis or py else h+1
            for f in range(width*height):
                expected_faces.add((c,axis,f));m0,m1,j0,jp,j1,q,reaction=d['faces'][c,axis,f];x=f%width;y=f//width
                l=((x+w-1)%w+y*w if axis==0 else x+((y+h-1)%h)*w) if (x>0 or px if axis==0 else y>0 or py) else None
                r=(x+y*w if axis==0 else x+y*w) if (x<w if axis==0 else y<h) else None
                l=None if l is None or wall[l] else l;r=None if r is None or wall[r] else r
                old=(mass[0,l] if l is not None else 0)+(mass[0,r] if r is not None else 0)
                new=(mass[2,l] if l is not None else 0)+(mass[2,r] if r is not None else 0)
                close(m0,old/2,old,'Incoming face inertia');close(m1,new/2,new,'Remapped face inertia')
                if m0==0:check(j0==jp==j1==q==reaction==0,'Empty face has mechanics');continue
                pl=d['cw']['full'][d['work'][c,l]][3] if l is not None else F(0)
                pr=d['cw']['full'][d['work'][c,r]][3] if r is not None else F(0)
                body_force=-gravity*m0 if axis==1 else F(0);impulse=dt*(area*(pl-pr)+body_force)
                if axis==1:body_actual+=dt*body_force
                if l is None or r is None:
                    check(j0==jp==j1==q==0,'Fixed face moved');wall_impulse[axis]-=impulse
                else:
                    close(jp-j0,impulse,abs(jp)+abs(j0)+abs(impulse),'Actual canonical pressure impulse')
                    close(q,dt*area*(j0+jp)/(2*m0),abs(q),'Actual midpoint sweep');sweeps[l]+=q;sweeps[r]-=q
                k0=j0*j0/(2*m0);kp=jp*jp/(2*m0);k1=j1*j1/(2*m1) if m1 else F(0)
                work=dt*body_force*(j0+jp)/(2*m0);pressure_kinetic+=kp-k0;total_kinetic+=k1-k0;body_work+=work
                pressure_scale+=abs(kp)+abs(k0)+abs(work);momentum[axis].extend((j1,-j0,-reaction))
        for actual,expected in ((wx,wall_impulse[0]),(wy,wall_impulse[1]),(body,body_actual)):
            close(actual,expected,abs(actual)+abs(expected),'Wall/body impulse')
        for axis in range(2):
            operands=momentum[axis]+[-(wx if axis==0 else wy),-(body if axis==1 else F(0))]
            check(abs(sum(operands,F(0)))<=128*EPS*sum(map(abs,operands)),'Actual owner total momentum/wall/body account')
        pu=F(0);tu=F(0);potential=F(0)
        for i in range(n):
            if wall[i]:continue
            identifier=d['work'][c,i];dv=d['cw']['cases'][identifier][2];v1=readings[1,i][0]
            close(dv,sweeps[i],abs(v1)+abs(volume)+abs(sweeps[i]),'Material volume change differs from actual face sweeps')
            close(v1-volume,sweeps[i],abs(v1)+abs(volume)+abs(sweeps[i]),'Actual material volume closure')
            pu+=energy[1,i]-energy[0,i];tu+=energy[2,i]-energy[0,i];pressure_scale+=abs(energy[1,i])+abs(energy[0,i])
            potential+=gravity*(F(i//w)+F(1,2))*spacing*(mass[2,i]-mass[0,i])
        check(abs(pu+pressure_kinetic-body_work)<=pbound,'Actual pressure U plus primary face K minus body work')
        close(pbound,128*EPS*pressure_scale,pbound,'Original pressure operand bound',8)
        check(ebound>=pbound and abs(tu+total_kinetic+potential)<=ebound,'Actual owning composed energy account')
        if c==3:check(all(d['cw']['full'][d['work'][c,i]][2]==0 for i in range(n)),'Uniform translation generated heat')
    check(set(d['walls'])==expected_walls and set(d['inventory'])==expected_inv and set(d['faces'])==expected_faces and d['work']==expected_work,'Orphan or missing owner records')

def main():
    if len(sys.argv)!=3:raise SystemExit('Usage: review_mac_compressive_owner.py <saved.raw> <fresh.json>')
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=2097152,'Owner raw cap exceeded')
    with localcontext() as ctx:
        ctx.prec=80;data=parse(raw);review(data);checks=base.CHECKS;rejected=0
        for kind in ('heat','trace','sweep','energy'):
            changed=copy.deepcopy(data)
            if kind=='heat':
                key=next(k for k,v in changed['cw']['full'].items() if v[2]>0);values=list(changed['cw']['full'][key]);values[2]*=2;changed['cw']['full'][key]=tuple(values)
            elif kind=='trace':
                key=next(k for k,v in changed['masses'].items() if k[1]==2 and v<F(1,10**20));changed['masses'][key]*=2
            elif kind=='sweep':
                key=next(k for k,v in changed['faces'].items() if v[5]!=0);values=list(changed['faces'][key]);values[5]+=F(1,100);changed['faces'][key]=tuple(values)
            else:
                key=next(k for k in changed['inventory'] if k[1]==2 and changed['inventory'][k][1]!=0);values=list(changed['inventory'][key]);values[1]+=100;changed['inventory'][key]=tuple(values)
            check(changed!=data,'Corruption witness changed nothing')
            try:review(changed)
            except ValueError:rejected+=1
            else:raise ValueError('Corrupted owner '+kind+' admitted')
    result=dict(Passed=True,SavedChecks=checks,Scenes=4,Responses=29,NativeGroups=7,ExactCheckpointContinuations=24,CorruptionsRejected=rejected,
        SavedSha256=hashlib.sha256(raw).hexdigest(),NativeProcessLaunched=False,IndependentAgentReview=False,StrongFlowGatePassed=False,FirstPlayableColonyPassed=False)
    with Path(sys.argv[2]).open('x') as out:json.dump(result,out,indent=2);out.write('\n')
    print(json.dumps(result))
if __name__=='__main__':main()
