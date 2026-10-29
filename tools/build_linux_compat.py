#!/usr/bin/env python3
"""Build/test x86-64 native libraries against the Ubuntu 22.04 glibc baseline.

The container receives a frozen native-source snapshot and a fresh output
directory. Licensed game files, user profiles and working-tree writes are not
part of this build. The existing package builder consumes output/native/.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
MAXIMUM_GLIBC=(2,34)
BASE_IMAGE="ubuntu@sha256:5ec03bb3441e8b0bf3b4f9cd4629a1ae763010dc3035bb8da3ae6cf026486401"

def glibc_versions_within_limit(values):
    return isinstance(values,list) and bool(values) and all(
        isinstance(value,str) and re.fullmatch(r'[0-9]+(?:\.[0-9]+)+',value) and
        tuple(map(int,value.split('.')))<=MAXIMUM_GLIBC for value in values)

def run(command):
    print("+ "+" ".join(map(str,command)),flush=True)
    subprocess.run(list(map(str,command)),check=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build-linux-ubuntu2204')
    parser.add_argument('--jobs',type=int,default=4)
    parser.add_argument('--image',default='oni-gas-mod-native-ubuntu2204:v1')
    parser.add_argument('--skip-image-build',action='store_true')
    args=parser.parse_args()
    if not 1<=args.jobs<=64:parser.error('--jobs must be between 1 and 64')
    output=args.output.resolve()
    if output.exists():parser.error('Use a fresh output directory; source snapshots and earlier evidence are retained')
    output.mkdir(parents=True)
    source=output/'source';source.mkdir()
    entries=subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=ROOT).decode().split('\0')
    # tools/review_gas_diffusion.py is the mac_gas_diffusion_maxwell_stefan CTest oracle.
    exact={'CMakeLists.txt','docs/native-hook-profiles.json','tools/oni_hook_profiles.py','tools/patch_funchook.py','tools/patch_funchook_unix.py','tools/patch_distorm.py',
        'tools/review_gas_diffusion.py','tools/check_colony_benchmark.py'}
    # A tracked file deleted in the working tree is not part of the snapshot
    # being built; it is listed in the manifest rather than silently copied.
    deleted=set(subprocess.check_output(['git','ls-files','-z','--deleted'],cwd=ROOT).decode().split('\0'))-{''}
    frozen={};skipped=[]
    for name in sorted(set(entries)):
        if name not in exact and not name.startswith(('native/','cmake/')):continue
        original=ROOT/name
        if name in deleted and not original.exists() and not original.is_symlink():skipped.append(name);continue
        if not original.is_file() or original.is_symlink():raise ValueError('Expected an ordinary native source: '+name)
        target=source/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(original,target)
        frozen[name]=hashlib.sha256(target.read_bytes()).hexdigest()
    if not exact<=frozen.keys():raise ValueError('Native source snapshot is incomplete')
    manifest={'kind':'linux-native-compatibility-build','base_image':BASE_IMAGE,'glibc_maximum':'2.34',
        'source_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'source_status':subprocess.check_output(['git','status','--short'],cwd=ROOT,text=True),
        'started_utc':datetime.now(timezone.utc).isoformat(),'source_files':frozen,'deleted_in_working_tree':skipped,'passed':False}
    report=output/'compatibility-build.json';report.write_text(json.dumps(manifest,indent=2)+'\n')
    if not args.skip_image_build:
        run(['docker','build','--platform','linux/amd64','--network','host','--tag',args.image,ROOT/'tools/linux_native_build'])
    image_id=subprocess.check_output(['docker','image','inspect',args.image,'--format','{{.Id}}'],text=True).strip()
    manifest['image_id']=image_id
    base=['docker','run','--rm','--platform','linux/amd64','--network','host','--user',f'{os.getuid()}:{os.getgid()}',
        '--mount',f'type=bind,src={source},dst=/source,readonly',
        '--mount',f'type=bind,src={output},dst=/build','--workdir','/build',image_id]
    run([*base,'cmake','-S','/source','-B','/build/native','-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
        '-DMA_BUILD_ONI_NATIVE_BACKEND=ON','-DMA_BUILD_STAGGERED_EXPERIMENTS=OFF','-DMA_SANITIZERS=OFF',
        '-DCMAKE_BUILD_RPATH_USE_ORIGIN=ON'])
    run([*base,'cmake','--build','/build/native','--parallel',str(args.jobs)])
    run([*base,'ctest','--test-dir','/build/native','--parallel',str(args.jobs),'--output-on-failure'])
    required={}
    for name in ['libmixed_atmospheres.so','libmixed_atmospheres_oni.so']:
        library=output/'native'/name
        versions=subprocess.check_output(['readelf','--version-info',library],text=True)
        values=sorted(set(re.findall(r'\bGLIBC_([0-9.]+)\b',versions)),key=lambda value:tuple(map(int,value.split('.'))))
        if not glibc_versions_within_limit(values) or 'GLIBC_PRIVATE' in versions or 'GLIBC_ABI_DT_RELR' in versions:
            raise ValueError(f'{name} exceeds the declared glibc 2.34 baseline: {values}')
        required[name]={'required_glibc':values,'sha256':hashlib.sha256(library.read_bytes()).hexdigest()}
    manifest.update(passed=True,libraries=required,finished_utc=datetime.now(timezone.utc).isoformat(),
        actual_game_test=False,release_ready=False)
    report.write_text(json.dumps(manifest,indent=2)+'\n')
    print(report,flush=True)

if __name__=='__main__':main()
