#!/usr/bin/env python3
"""Capture one bounded MAC benchmark. This does not certify a playable colony."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import platform
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]

def digest(path:Path)->str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def write(path:Path,data:dict)->None:
    with path.open('x',encoding='utf-8',newline='\n') as stream:
        json.dump(data,stream,indent=2,allow_nan=False); stream.write('\n')

def validate_metrics(data:dict,width:int,height:int,samples:int,profile:str,scene:str,no_reference:bool)->None:
    if (data['schema']!=1 or data['cells']!=width*height or data['samples']!=samples or
            data['profile']!=profile or data['scene']!=scene or data['whole_colony_verified'] is not False or
            data['simulated_seconds_per_sample']!=.1): raise ValueError('Benchmark identity differs')
    def number(value):
        if isinstance(value,bool) or not isinstance(value,(float,int)) or not math.isfinite(value) or value<0:
            raise ValueError('Invalid nonnegative finite measurement')
        return value
    times=data['wall_ms']
    if not isinstance(times,list) or len(times)!=samples: raise ValueError('Sample count differs')
    ordered=sorted(number(value) for value in times)
    if (number(data['cold_ms'])!=times[0] or number(data['maximum_ms'])!=ordered[-1] or
            number(data['p50_ms'])!=ordered[math.ceil(.5*samples)-1] or
            number(data['p95_ms'])!=ordered[math.ceil(.95*samples)-1]): raise ValueError('Timing summary differs from raw samples')
    number(data['setup_ms'])
    for key in ('substeps','retries','pressure_iterations','exact_equilibrium_intervals'):
        value=data[key]
        if isinstance(value,bool) or not isinstance(value,int) or value<0: raise ValueError('Invalid stage count')
    if data['substeps']<samples or data['exact_equilibrium_intervals']>data['substeps']: raise ValueError('Stage counts differ')
    if (number(data['relative_mass_drift_including_trace'])>1e-8 or number(data['relative_energy_drift'])>1e-6):
        raise ValueError('Conservation gate failed')
    compare=profile!='reference' and not no_reference
    if data['reference_comparison_performed'] is not compare: raise ValueError('Reference scope differs')
    differences=('maximum_relative_density_difference','maximum_relative_pressure_difference','maximum_temperature_difference_k')
    if compare:
        if not isinstance(data['reference_checkpoint_equal'],bool): raise ValueError('Missing reference equality')
        for key in differences: number(data[key])
    elif data['reference_checkpoint_equal'] is not None or any(data[key] is not None for key in differences):
        raise ValueError('Unmeasured reference result is not null')
    if data['100k_25ms_this_case_this_host'] is not (width*height==100000 and ordered[-1]<=25):
        raise ValueError('Performance claim differs from measured scope')

def main()->int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--width',type=int,default=16)
    parser.add_argument('--height',type=int,default=16)
    parser.add_argument('--samples',type=int,default=5)
    parser.add_argument('--profile',choices=('reference','equilibrium','balanced'),default='equilibrium')
    parser.add_argument('--scene',choices=('uniform','wave'),default='wave')
    parser.add_argument('--timeout',type=float,default=120)
    parser.add_argument('--no-reference',action='store_true')
    args=parser.parse_args()
    if not (1<=args.width<=100000 and 1<=args.height<=100000 and args.width*args.height<=100000 and
            1<=args.samples<=101 and 1<=args.timeout<=3600): parser.error('Invalid dimensions, samples or timeout')
    executable=args.executable.resolve(strict=True); output=args.output.resolve()
    command=[str(executable),str(args.width),str(args.height),str(args.samples),args.profile,args.scene]
    if args.no_reference: command.append('--no-reference')
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    status=subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).splitlines()
    identity=digest(executable)
    source_files=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','native','CMakeLists.txt'],cwd=ROOT,text=True).splitlines()
    sources={name:digest(ROOT/name) for name in source_files if Path(name).suffix in ('.cpp','.c','.h','.hpp','.txt')}
    output.mkdir(parents=True,exist_ok=False)
    write(output/'invocation.json',{'argv':command,'timeout_seconds':args.timeout,'executable_sha256':identity,
        'source_revision':revision,'source_status':status,'source_sha256':sources,'platform':platform.platform(),
        'processor':platform.processor(),'scope':'closed MAC owner with constant-capacity test EOS; no game publication'})
    start=time.monotonic(); timed_out=False; code=None
    with (output/'stdout.raw').open('xb') as stdout,(output/'stderr.raw').open('xb') as stderr:
        try:
            code=subprocess.run(command,stdout=stdout,stderr=stderr,timeout=args.timeout,check=False).returncode
        except subprocess.TimeoutExpired: timed_out=True
    errors=[]; metrics=None
    if timed_out: errors.append('Native benchmark timed out')
    if code!=0: errors.append('Native benchmark did not exit successfully')
    unchanged=digest(executable)==identity and all(digest(ROOT/name)==value for name,value in sources.items())
    if not unchanged: errors.append('Executable or native source changed during capture')
    try:
        def invalid_constant(value): raise ValueError('Nonfinite JSON constant: '+value)
        metrics=json.loads((output/'stdout.raw').read_text(),parse_constant=invalid_constant)
        validate_metrics(metrics,args.width,args.height,args.samples,args.profile,args.scene,args.no_reference)
    except (ValueError,KeyError,TypeError) as error: errors.append('Invalid benchmark output: '+str(error))
    report={'passed':not errors,'errors':errors,'exit_code':code,'timed_out':timed_out,
        'elapsed_seconds':time.monotonic()-start,'executable_and_sources_unchanged':unchanged,
        'stdout_sha256':digest(output/'stdout.raw'),'stderr_sha256':digest(output/'stderr.raw'),
        'metrics':metrics,'first_playable_passed':False,'ryzen_5900x_target_certified':False}
    write(output/'result.json',report)
    print(json.dumps({'passed':report['passed'],'result':str(output/'result.json'),'metrics':metrics,'errors':errors}))
    return 0 if report['passed'] else 1

if __name__=='__main__': raise SystemExit(main())
