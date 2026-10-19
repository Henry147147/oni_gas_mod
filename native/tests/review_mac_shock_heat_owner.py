"""Saved-only review of actual default heat/pressure/compression owner stages."""
import copy
from decimal import Decimal as D,localcontext
from fractions import Fraction as F
import hashlib,importlib.util,json
from pathlib import Path
import sys

def module(name,file):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file))
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result

heat=module('owner_heat_saved','review_mac_shock_heat.py')
compression=module('owner_compression_saved','review_mac_compression.py')
acoustic=module('owner_acoustic_saved','review_mac_acoustic_compression.py')
matched=module('owner_matched_saved','review_mac_matched_heat.py')
closed=heat.ref;base=heat.base
check,close,number,unique,dec=base.check,base.close,base.number,base.unique,closed.dec
EPS=base.EPS

def full(r,i):return base.scaled_energy(r[i],r[i+1])

def parse(raw,required_law=1,required_heat=1):
    d={k:{} for k in ('frames','cells','masses','passives','faces','walls','paths','cases','reports','physical','kappa','laws','linear','speeds','pressures','sensors')}
    d['required_law']=required_law;d['required_heat']=required_heat;d['heat_settings']={}
    ordinary=[];heat_lines=[];compression_lines=[];materials=[];passes=[];control=None
    heat_kinds={'HGRID','HPAIR','HPMASS','HEQCERT','HCAP','HEVENT','SCELL','SMASS','SPASSIVE','SFACE','SOLID'}
    for line in raw.decode('ascii').splitlines():
        r=line.split();check(bool(r),'Empty owner heat record');kind=r[0]
        if kind=='MATERIAL':materials.append(line)
        elif kind=='PASS':passes.append(line)
        elif kind in heat_kinds or kind in matched.KINDS:heat_lines.append(line)
        elif kind=='KALINEAR':
            check(len(r)==8,'Owned acoustic pair shape');unique(d['linear'],int(r[1]),tuple(full(r,i) for i in (2,4,6)))
        elif kind=='KASPEED':
            check(len(r)==5,'Owned acoustic speed shape');unique(d['speeds'],tuple(map(int,r[1:3])),full(r,3))
        elif kind=='KPSTATE':
            check(len(r)==4,'Owned sensor pressure shape');unique(d['pressures'],tuple(map(int,r[1:3])),number(r[3]))
        elif kind=='KPSENSOR':
            check(len(r)==6,'Owned pressure sensor shape');unique(d['sensors'],tuple(map(int,r[1:4])),full(r,4))
        elif kind.startswith('K'):compression_lines.append(line[1:])
        elif kind=='SHHEAT':
            check(len(r)==3,'Owned heat setting shape');unique(d['heat_settings'],int(r[1]),int(r[2]))
        elif kind=='SHLAW':
            check(len(r)==3,'Owned compression law shape');unique(d['laws'],int(r[1]),int(r[2]))
        elif kind=='SHFRAME':
            check(len(r)==10,'Frame shape');unique(d['frames'],tuple(map(int,r[1:3])),(*map(int,r[3:5]),*map(number,r[5:7]),*map(int,r[7:9]),r[9]))
        elif kind=='SHCELL':
            check(len(r)==8,'Frame cell shape');unique(d['cells'],tuple(map(int,r[1:4])),tuple(map(number,r[4:])))
        elif kind in ('SHMASS','SHPASSIVE'):
            check(len(r)==6,'Frame inventory shape');unique(d['masses' if kind=='SHMASS' else 'passives'],tuple(map(int,r[1:5])),number(r[5]))
        elif kind=='SHFACE':
            check(len(r)==8,'Frame face shape');unique(d['faces'],tuple(map(int,r[1:5])),(number(r[5]),full(r,6)))
        elif kind=='SHWALL':
            check(len(r)==5 and r[4] in ('0','1'),'Frame wall shape');unique(d['walls'],tuple(map(int,r[1:4])),int(r[4]))
        elif kind=='SHPATH':
            check(len(r)==8 and r[4] in ('H','C','P','L'),'Stage path shape');unique(d['paths'],tuple(map(int,r[1:4])),(r[4],*map(int,r[5:])))
        elif kind=='SHCASE':
            check(len(r)==9,'Owner case shape');unique(d['cases'],int(r[1]),(number(r[2]),*map(int,r[3:])))
        elif kind=='SHREPORT':
            check(len(r)==32,'Owner report shape');unique(d['reports'],int(r[1]),(number(r[2]),*map(int,r[3:18]),*[full(r,i) for i in range(18,30,2)],number(r[30]),number(r[31])))
        elif kind=='SHL':
            check(len(r)==13,'Physical conduction report shape');unique(d['physical'],int(r[1]),(number(r[2]),*map(int,r[3:6]),*[full(r,i) for i in range(6,12,2)],number(r[12])))
        elif kind=='SHKAPPA':
            check(len(r)==3,'Conductivity shape');unique(d['kappa'],int(r[1]),number(r[2]))
        elif kind=='SHCONTROL':
            check(len(r)==7 and control is None,'Atomic control shape');control=tuple(map(number,r[1:]))
        else:ordinary.append(line)
    check(len(passes)==len(set(passes))==6 and control==(F(2),F(1),F(0),F(1),F(2),F(4)),'Missing owner groups/atomic controls')
    check(set(d['cases'])==set(d['reports'])==set(range(6)),'Missing owner cases')
    hp=tuple(int(line.split()[1]) for line in heat_lines if line.startswith('HGRID '))
    cp=tuple(int(line.split()[1]) for line in compression_lines if line.startswith('GRID '))
    pp=tuple(int(line.split()[1]) for line in ordinary if line.startswith('PCASE '))
    heat_raw,d['matched'],rejections=matched.split(('\n'.join(materials+heat_lines)+'\n').encode());check(rejections is None,'Unexpected owner matching rejections')
    d['heat']=heat.parse(heat_raw,0,hp,False)
    d['compression']=compression.parse(('\n'.join(materials+compression_lines)+'\n').encode(),0,cp,False)
    d['pressure']=closed.parse(('\n'.join(materials+ordinary)+'\n').encode(),0,False,pp,pp)
    return d

def view(d,g,f):
    header=d['frames'][g,f]
    def field(name):return {k[2:]:v for k,v in d[name].items() if k[:2]==(g,f)}
    return {k:field(k) for k in ('cells','masses','passives','faces','walls')}|{'header':header[:6]}

def frame_check(d,g,f):
    v=view(d,g,f);w,h,spacing,depth,px,py=v['header'];volume=spacing*spacing*depth
    check(w>0 and h>0 and spacing>0 and depth>0 and px in (0,1) and py in (0,1),'Frame geometry')
    walls={i:v['walls'][i,] for i in range(w*h)};fluid={i for i in walls if not walls[i]}
    check(set(v['cells'])=={(i,) for i in fluid},'Frame cell coverage')
    for i in fluid:
        m={s:a for (c,s),a in v['masses'].items() if c==i};ps={s:a for (c,s),a in v['passives'].items() if c==i}
        check(m and min(m.values())>0 and len(ps)==2 and min(ps.values())>0,'Frame lost positive constituent/passive')
        u,p,t,s=v['cells'][i,];e=heat.eos(m,d['heat']['materials'],u,volume)
        heat.near(p,e[0],'Independent owner-frame pressure');heat.near(t,e[1],'Independent owner-frame temperature')
        check(abs(dec(s)-e[2])<=D('5e-10')*(abs(e[2])+e[3]),'Independent owner-frame entropy')
    for axis,nf in ((0,(w+(not px))*h),(1,w*(h+(not py)))):
        for i in range(nf):
            inertia,j=v['faces'][axis,i]
            x,y=(i%(w+(not px)),i//(w+(not px))) if axis==0 else (i%w,i//w)
            l=((x-1)%w+y*w if x>0 or px else None) if axis==0 else (x+((y-1)%h)*w if y>0 or py else None)
            r=(x+y*w if x<w else None) if axis==0 else (x+y*w if y<h else None)
            present=[c for c in (l,r) if c is not None and not walls[c]]
            im=sum((sum(a for (c,_),a in v['masses'].items() if c==cell)/2 for cell in present),F(0))
            close(inertia,im,im,'Owner-frame inertia from actual masses',8)
            check(len(present)==2 or j==0,'Owner-frame fixed wall momentum')
    return v

def component_view(d,kind,identifier,end):
    if kind in ('H','C'):
        c=d['heat'] if kind=='H' else d['compression'];w,h,spacing,depth,px,py,*_=c['grids'][identifier]
        cells={(i,):(v[0],v[2],v[1],v[3]) for (g,e,i),v in c['cells'].items() if (g,e)==(identifier,end)}
        fields={name:{(i,s):v for (g,e,i,s),v in c[name].items() if (g,e)==(identifier,end)} for name in ('masses','passives')}
        faces={(a,i):(v[0],v[2]) for (g,a,e,i),v in c['faces'].items() if (g,e)==(identifier,end)}
        walls={(i,):v for (g,i),v in c['walls'].items() if g==identifier}
        return fields|dict(cells=cells,faces=faces,walls=walls,header=(w,h,spacing,depth,px,py))
    c=d['pressure'][0][identifier];row=d['pressure'][2][identifier]
    # Pressure case cells contain U; pressure endpoint EOS is checked separately.
    return dict(masses={(i,s):v for (e,i,s),v in c['masses'].items() if e==end},
        passives={(i,s):v for (e,i,s),v in c['passives'].items() if e==end},
        cells={(i,):(d['pressure'][3][identifier,i][0] if end==0 else v) for (e,i),v in c['cells'].items() if e==end and not c['walls'][i]},
        faces={(a,i):(v[3] if end==0 else v[4],d['pressure'][4][identifier,a,i][0] if end==0 else v[2]) for (a,i),v in c['faces'].items()},
        walls={(i,):v for i,v in c['walls'].items()},header=(c['w'],c['h'],row[1],row[2],c['px'],c['py']))

def same_component(frame,component,kind):
    for field in ('header','masses','passives','faces','walls'):check(frame[field]==component[field],'Actual stage/frame '+field+' differs')
    if kind=='P':
        check({i:r[0] for i,r in frame['cells'].items()}==component['cells'],'Actual pressure/transport U differs')
    else:check(frame['cells']==component['cells'],'Actual thermal/compression EOS frame differs')

def physical_check(d,identifier,before,after):
    dt,contacts,iterations,un,q,res,bound,ds=d['physical'][identifier]
    check(dt>0 and contacts>0 and iterations>=0 and un>=0,'Physical-stage counters')
    for field in ('header','masses','passives','faces','walls'):check(before[field]==after[field],'Physical conduction changes conserved nonthermal fields')
    w,h,spacing,depth,px,py=before['header'];volume=spacing*spacing*depth
    temps0={i:r[2] for (i,),r in before['cells'].items()};temps1={i:r[2] for (i,),r in after['cells'].items()}
    capacity={};conductivity={}
    for i in temps0:
        m={s:v for (cell,s),v in before['masses'].items() if cell==i}
        check(all(d['heat']['materials'][s][0]==0 for s in m),'Combined physical control is not a constant-capacity gas')
        capacity[i]=sum((v*d['heat']['materials'][s][2] for s,v in m.items()),F(0))
        moles={s:v/d['heat']['materials'][s][1] for s,v in m.items()};conductivity[i]=sum((n*d['kappa'][s] for s,n in moles.items()),F(0))/sum(moles.values(),F(0))
    net={i:F(0) for i in temps0};terms={i:[capacity[i]*temps1[i],-capacity[i]*temps0[i]] for i in temps0};count=0
    for y in range(h):
        for x in range(w):
            a=x+y*w
            for b in ((x+1)%w+y*w if x+1<w or px else None,x+((y+1)%h)*w if y+1<h or py else None):
                if b is None or a not in temps0 or b not in temps0:continue
                ka,kb=conductivity[a],conductivity[b]
                conductance=2*ka*kb/(ka+kb)*depth if ka and kb else F(0)
                if not conductance:continue
                count+=1;flux=dt*conductance*(temps1[b]-temps1[a]);net[a]+=flux;net[b]-=flux
                terms[a].extend((-dt*conductance*temps1[b],dt*conductance*temps1[a]));terms[b].extend((-dt*conductance*temps1[a],dt*conductance*temps1[b]))
    check(count==contacts,'Physical conductive geometry/contact count')
    changes=[];u_terms=[]
    for i in temps0:
        close(sum(terms[i],F(0)),F(0),sum(map(abs,terms[i]),F(0)),'Independent backward-Euler thermal equation')
        old=before['cells'][i,][0];new=after['cells'][i,][0];changes.append(new-old);u_terms.extend((new,-old))
        close(new-old,net[i],abs(old)+abs(new)+abs(net[i]),'Physical signed stored-U transfer')
    close(res,-sum(u_terms,F(0)),sum(map(abs,u_terms),F(0)),'Physical actual-U residual')
    close(bound,128*EPS*sum(map(abs,u_terms),F(0)),bound,'Physical actual-U bound',16);check(abs(res)<=bound,'Physical stage energy conservation')
    # Its redistribution report measures net body changes before storage rounding.
    close(q,sum(map(abs,changes),F(0))/2,sum(map(abs,u_terms),F(0)),'Physical redistribution from actual U')
    return q,bound

def review(d):
    check(d['required_law'] in (1,2) and d['laws']=={g:d['required_law'] for g in range(7)},'Owned compression setting differs from declared method')
    if d['required_heat'] in (2,3):
        check(d['heat_settings']=={g:(0 if g==5 else d['required_heat']) for g in range(7)},'Owned matched-heat setting differs')
        matched.review_component(d['heat'],d['matched'],expected_law=d['required_heat']-1)
    else:
        check(d['required_heat']==1 and not d['heat_settings'] and not any(d['matched'].values()),'Legacy heat contains matching operands')
        heat.review(d['heat'],False)
    if d['required_law']==2:
        acoustic.review_component(d['compression'],{k:d[k] for k in ('linear','speeds','pressures','sensors')},pressure_sensed=True)
    else:
        check(not any(d[k] for k in ('linear','speeds','pressures','sensors')),'Quadratic reference contains acoustic operands')
        compression.review(d['compression'],False)
    for i,c in d['pressure'][0].items():base.review_case(c);closed.review_pressure(i,d['pressure'])
    views={key:frame_check(d,*key) for key in d['frames']}
    for g,case in d['cases'].items():
        dt,steps,mode,physical,manual_id,owner_id,held_id=case
        check(dt>0 and steps==(2 if g==3 else 1) and mode==int(g!=5) and physical==int(g==2),'Declared owner settings')
        check(views[g,manual_id]==views[g,owner_id] and views[g,0]==views[g,held_id],'Actual owner/manual/immutable source mismatch')
        expected_order=(['L'] if physical else [])+(['H'] if mode else [])+['C','P','C']+(['H'] if mode else [])+(['L'] if physical else [])
        cursor=0;stats=[0]*11;heat_sum=F(0);energy_bounds=F(0);physical_sum=F(0);wall=[F(0),F(0)]
        for step in range(steps):
            for order,kind in enumerate(expected_order):
                actual,identifier,a,b=d['paths'][g,step,order]
                check(actual==kind and a==cursor and b==cursor+1,'Completed stage order or state chain differs');cursor=b
                before,after=views[g,a],views[g,b]
                if kind=='L':
                    q,budget=physical_check(d,identifier,before,after);physical_sum+=q;energy_bounds+=budget
                    row=d['physical'][identifier];stats[7]+=1;stats[8]+=row[1];stats[9]+=row[2];stats[10]+=row[3]
                else:
                    for end,frame in enumerate((before,after)):same_component(frame,component_view(d,kind,identifier,end),kind)
                    if kind=='H':
                        row=d['heat']['grids'][identifier];stats[0]+=1;stats[1]+=row[7];stats[4]+=row[8];heat_sum+=row[9];energy_bounds+=row[11]
                        keys=[event[5] for (gid,_),event in d['heat']['events'].items() if gid==identifier]
                        stats[2]+=sum(d['heat']['pairs'][k][2] for k in keys);stats[3]+=sum(d['heat']['certs'][k][0] for k in keys)
                        if g==4 and step==order==0:
                            carried=any(v[0]!=F(float(v[0])) for v in after['cells'].values())
                            projected=copy.deepcopy(after)
                            projected['cells']={key:(F(float(v[0])),*v[1:]) for key,v in projected['cells'].items()}
                            check(0<row[9]<F(2)**-1074 and (row[8]==0 and projected==before if carried else row[8]>0 and before==after),'Below-range source heat lost/changed stored fields')
                    elif kind=='C':
                        row=d['compression']['grids'][identifier];stats[6]+=row[7];energy_bounds+=row[12];wall[0]+=row[13];wall[1]+=row[14]
                    else:
                        row=d['pressure'][2][identifier];energy_bounds+=row[6];wall[0]+=row[7];wall[1]+=row[8]
                        for (axis,_),face in d['pressure'][0][identifier]['faces'].items():wall[axis]+=face[10]
                        stats[5]+=sum(d['pressure'][0][identifier]['mreports'][a][0] for a in (0,1))
        check(cursor==manual_id and set(k for k in d['paths'] if k[0]==g)=={(g,s,i) for s in range(steps) for i in range(len(expected_order))},'Missing/extra owner stages')
        time,rev,attempts,substeps,rejected,*r=d['reports'][g]
        check(time==steps*dt and rev==2 and attempts==substeps==steps and rejected==0,'Committed owner clock/revision')
        check(r[:11]==stats,'Accepted owner stage/event/root counts differ')
        reported_heat,res,bound,reported_physical,wx,wy,ds,sb=r[11:]
        close(reported_heat,heat_sum,heat_sum,'Committed full heat transfer',8);close(reported_physical,physical_sum,physical_sum,'Committed physical heat',8)
        close(bound,energy_bounds,energy_bounds,'Committed composed stage bound',16)
        first,last=views[g,0],views[g,owner_id];energy=[];impulse=[[],[]]
        for endpoint,v in enumerate((first,last)):
            sign=1 if endpoint else -1
            energy.extend(sign*row[0] for row in v['cells'].values())
            for (axis,_),(mass,j) in v['faces'].items():energy.append(sign*j*j/(2*mass) if mass else F(0));impulse[axis].append(sign*j)
        close(res,sum(energy,F(0)),sum(map(abs,energy),F(0)),'Actual owner U+K residual');check(abs(res)<=bound and bound>0,'Actual owner U+K conservation')
        for axis,wvalue in enumerate((wx,wy)):
            close(sum(impulse[axis],F(0)),wvalue,sum(map(abs,impulse[axis]),F(0))+abs(wvalue),'Actual owner momentum/wall account')
            close(wvalue,wall[axis],sum(map(abs,impulse[axis]),F(0))+abs(wvalue),'Owner wall reaction from actual stages')
        for field in ('masses','passives'):
            for species in {s for _,s in first[field]}:
                a=sum(v for (_,s),v in first[field].items() if s==species);b=sum(v for (_,s),v in last[field].items() if s==species)
                close(a,b,abs(a)+abs(b),'Owner complete inventory/trace account')
        entropy=[sum((row[3] for row in v['cells'].values()),F(0)) for v in (first,last)]
        close(ds,entropy[1]-entropy[0],sum(abs(row[3]) for v in (first,last) for row in v['cells'].values()),'Actual endpoint entropy report')
        budget=128*EPS*sum((abs(row[0])/row[2] for v in (first,last) for row in v['cells'].values()),F(0))
        close(sb,budget,budget,'Actual endpoint stored-U entropy allowance',8);check(ds>=-sb,'Owner entropy acceptance')
    check(views[6,0]==views[6,1] and views[6,2]==views[6,3],'Failed interval changed source or deterministic recovery')

def main():
    raw=Path(sys.argv[1]).read_bytes();check(len(raw)<=4194304,'Owner heat saved pipe cap')
    required_heat=3 if '--series-heat' in sys.argv[2:] else 2 if '--matched-heat' in sys.argv[2:] else 1
    required_law=2 if required_heat>=2 or '--acoustic-compression' in sys.argv[2:] else 1
    mutations=('clock','chain','trace','heat')+(('tangent','law') if required_law==2 else ())+(('capacity','heat-law') if required_heat>=2 else ())
    with localcontext() as context:
        context.prec=80;d=parse(raw,required_law,required_heat);review(d)
        checks=base.CHECKS+compression.base.CHECKS+acoustic.base.CHECKS+acoustic.heat.base.CHECKS+matched.base.CHECKS+matched.acoustic.base.CHECKS
        for mutation in mutations:
            bad=copy.deepcopy(d)
            if mutation=='clock':
                row=list(bad['reports'][0]);row[0]+=1;bad['reports'][0]=tuple(row)
            elif mutation=='chain':
                row=list(bad['paths'][0,0,0]);row[3]+=1;bad['paths'][0,0,0]=tuple(row)
            elif mutation=='trace':
                endpoint=bad['cases'][0][5];key=next(k for k,v in bad['masses'].items() if k[:2]==(0,endpoint) and v<F(1,10**20));bad['masses'][key]*=2
            elif mutation=='heat':
                key=next(iter(bad['heat']['pairs']));row=list(bad['heat']['pairs'][key]);row[18]*=F(5,4);bad['heat']['pairs'][key]=tuple(row)
            elif mutation=='tangent':
                key=next(k for k,v in bad['speeds'].items() if v>0);bad['speeds'][key]*=F(5,4)
            elif mutation=='law':bad['laws'][0]=1
            elif mutation=='capacity':
                key=next(k for k,v in bad['matched']['cp'].items() if v>0);bad['matched']['cp'][key]*=F(5,4)
            else:bad['heat_settings'][0]=1
            caught=False
            try:review(bad)
            except (ValueError,KeyError):caught=True
            check(caught,'Altered owning heat record accepted')
    result=dict(Passed=True,Groups=6,CompressionLaw=required_law,HeatLaw=required_heat,SavedChecks=checks,CorruptionsRejected=list(mutations),SavedSha256=hashlib.sha256(raw).hexdigest(),NativeProcessLaunched=False,IndependentAgentReview=False,
        Scope='Owning occupied heat/compression/pressure controls; no strong-flow, production or game acceptance')
    if len(sys.argv)>2:
        with Path(sys.argv[2]).open('x') as stream:json.dump(result,stream,indent=2)
    print(json.dumps(result))

if __name__=='__main__':main()
