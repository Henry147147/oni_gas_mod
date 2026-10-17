#!/usr/bin/env python3
"""Review captured native input framing and bindings without loading native code.

This does not independently solve fluid physics or predict future game inputs.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

MAXIMUM_BYTES=1<<20

def require(valid: bool, message: str) -> None:
    if not valid:raise ValueError(message)

def decode(data: bytes) -> dict:
    require(232<=len(data)<=MAXIMUM_BYTES and data[:8]==b'MAFRIN01','Invalid journal framing')
    body=data[:-64]
    require(hashlib.sha256(body).hexdigest().encode()==data[-64:],'Journal checksum differs')
    version,flags=struct.unpack_from('<II',body,8)
    session,source,completed,time,seconds=struct.unpack_from('<QQQdd',body,16)
    require(version==1 and flags&~3==0 and session>0 and source<2**64-1 and completed==source+1,'Invalid journal version, flags or epoch')
    require(math.isfinite(time) and time>=0 and math.isfinite(seconds) and 0<seconds<=1,'Invalid journal clock')
    geometry=struct.unpack_from('<10I',body,56)
    size,abi,mode,pitch,x,y,width,height,scenario,reserved=geometry
    require((size,abi,mode,reserved)==(40,2,1,0) and 2<=width<=16 and 2<=height<=16 and
        x>0 and y>0 and x+width<pitch and scenario<=6,'Invalid journal geometry/model')
    count,reserved=struct.unpack_from('<II',body,96)
    require(count<=2048 and reserved==0,'Invalid batch count or reserved word')
    digest=body[104:168].decode('ascii')
    require(len(digest)==64 and all(c in '0123456789abcdef' for c in digest),'Invalid source digest')
    offset=168;batches=[];heat=transfers=0
    for _ in range(count):
        require(offset+8<=len(body),'Truncated batch header')
        kind,commands=struct.unpack_from('<II',body,offset);offset+=8
        require(kind in (1,2) and 1<=commands<=256,'Invalid batch kind or count')
        stride=48 if kind==1 else 64
        require(offset+commands*stride<=len(body),'Truncated commands')
        rows=[]
        for _ in range(commands):
            header=struct.unpack_from('<IIQQ',body,offset)
            require(header==(stride,1 if kind==1 else 2,session,source),'Command source epoch or ABI differs')
            if kind==1:
                cell,origin,joules,maximum=struct.unpack_from('<IIdd',body,offset+24)
                require(math.isfinite(joules) and math.isfinite(maximum) and maximum>0,'Nonfinite or invalid heat command')
                rows.append(dict(cell=cell,source=origin,joules=joules,maximum_temperature=maximum));heat+=1
            else:
                cell,species,action,options,callback,reserved,mass,temperature=struct.unpack_from('<IIIIiIdd',body,offset+24)
                require(species<5 and action in (1,2) and reserved==0 and options&~0xff01==0 and
                    math.isfinite(mass) and mass>=0 and math.isfinite(temperature),'Invalid transfer command')
                radius=(options>>8)&255
                require(not radius or action==2 and options&1,'Invalid radial consumption flags')
                if radius or action==1 and options&1:
                    try:projected=struct.unpack('<f',struct.pack('<f',mass))[0]
                    except (OverflowError,struct.error):raise ValueError('Transfer exceeds callback range') from None
                    require(projected==mass,'Transfer is not an exact callback mass')
                rows.append(dict(cell=cell,species=species,kind=action,flags=options,callback=callback,mass=mass,temperature=temperature));transfers+=1
            require(x<=cell%pitch<x+width and y<=cell//pitch<y+height,'Command is outside its declared owner')
            offset+=stride
        require(heat<=4096 and transfers<=4096,'Command counts exceed live reservation bounds')
        batches.append(dict(kind=kind,commands=rows))
    require(offset==len(body),'Trailing journal data')
    return dict(flags=flags,session=session,source_frame=source,completed_frame=completed,source_time=time,seconds=seconds,
        geometry=geometry,source_sha256=digest,batches=batches,heat_commands=heat,transfer_commands=transfers,
        heat_batches=sum(b['kind']==1 for b in batches),transfer_batches=sum(b['kind']==2 for b in batches))

def review(report: dict, directory: Path) -> dict:
    require(report.get('Status')=='passed','A complete passed Unity report is required')
    colony=report['ColonyCheckpoint'];physical=report['NativeDomain'];value=colony['FrameInputReplay'];rows=value['Journals']
    require(value['AbiVersion']==1 and value['PredictsFutureGameCommands'] is False and value['ActorReconfigurationReplayImplemented'] is False,'Unsupported replay scope')
    require(value['ActorRefreshFramesCompared']==0 and value['ReplayedFrames']==len(rows)==colony['ExactContinuationFrames'] and len(rows)>0,'Every frame must use supported input replay')
    directory=directory.resolve();session=physical['Session'];time=colony['SourceTime'];heat=transfers=0
    first=directory/f'native-frame-inputs-source-{session}.bin'
    require(hashlib.sha256(first.read_bytes()).hexdigest()==colony['RestoredInitialSha256']==rows[0]['SourceSha256'],'Initial captured source differs')
    for i,row in enumerate(rows):
        name=row['File'];require(isinstance(name,str) and Path(name).name==name and name==f'native-frame-inputs-{session}-{i+1}.bin','Invalid journal file identity')
        data=(directory/name).read_bytes();decoded=decode(data)
        require(len(data)==row['Bytes'] and hashlib.sha256(data).hexdigest()==row['Sha256'],'Captured bytes differ from their receipt')
        require(decoded['flags']==row['Flags']==0 and row['ReplaySupported'] is True and
            decoded['session']==row['Session']==session and decoded['source_frame']==row['SourceFrame']==i and
            decoded['completed_frame']==row['CompletedFrame']==i+1 and decoded['source_sha256']==row['SourceSha256'] and
            decoded['source_time']==row['SourceTime']==time and decoded['seconds']==row['Seconds']==physical['LastTimestepSeconds'],
            'Captured journal differs from its frame/source/clock receipt')
        for source_key,target_key in (('heat_batches','HeatBatches'),('transfer_batches','TransferBatches'),('heat_commands','HeatCommands'),('transfer_commands','TransferCommands')):
            require(decoded[source_key]==row[target_key],'Recorded batch/command count differs')
        heat+=decoded['heat_batches'];transfers+=decoded['transfer_batches'];time+=decoded['seconds']
    require(heat==value['HeatBatches'] and transfers==value['TransferBatches'] and time==colony['ReplayedTime']==physical['TimeSeconds'],'Completed replay totals or clock differ')
    return dict(passed=True,frames=len(rows),heat_batches=heat,transfer_batches=transfers,
        native_code_loaded=False,independent_physics_solution=False,predicts_future_game_commands=False)

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',required=True,type=Path);parser.add_argument('--directory',type=Path)
    args=parser.parse_args()
    try:result=review(json.loads(args.report.read_text()),args.directory or args.report.parent)
    except (OSError,KeyError,ValueError,TypeError,UnicodeError,struct.error) as error:result=dict(passed=False,error=str(error))
    print(json.dumps(result));return 0 if result['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
