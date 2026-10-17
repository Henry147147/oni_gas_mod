#!/usr/bin/env python3
"""Run fresh-process save/load checks for the implemented bounded ONI scenes.

Use a frozen runner checkout and package. This cannot certify the unimplemented
overhaul, a playable colony or whole-colony fluid ownership. Optional Proton
execution tests WindowsPlayer on Linux; it does not certify the Windows OS.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
SCENES={"import":"native-import-domain","water-import":"native-liquid-import-domain","gas":"native-domain","water":"native-liquid-domain","water-transfer":"native-liquid-transfer-domain","heat":"native-heat-domain","water-heat":"native-liquid-heat-domain",
    "transfer":"native-transfer-domain","carbon":"native-carbon-domain","conduction":"native-conduction-domain",
    "gravity":"native-gravity-domain","water-gravity":"native-liquid-gravity-domain","actor":"native-actor-domain","water-actor":"native-liquid-actor-domain","checkpoint":"native-checkpoint-domain",
    "pair":"native-paired-checkpoint-domain","save":"native-save-domain"}

def digest(path:Path)->str:
    with path.open("rb") as stream:
        value=hashlib.sha256()
        for chunk in iter(lambda:stream.read(1<<20),b""):value.update(chunk)
    return value.hexdigest()

def fingerprint(runner:Path,package:Path,*,windows:bool=False)->dict:
    names=("tools/oni_headless.py","tools/oni_native_domain_evidence.py")
    if windows:names+=("tools/oni_proton_headless.py","tools/oni_native_matrix.py")
    source={name:digest(runner/name) for name in names}
    files={str(p.relative_to(package)):digest(p) for p in sorted(package.rglob("*")) if p.is_file()}
    if not files:raise ValueError("The package is empty")
    return {"runner_files":source,"package_files":files}

def invoke(command:list[str],log:Path,timeout:int)->int:
    with log.open("x",encoding="utf-8") as stream:
        return subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout).returncode

def run_matrix(runner:Path,package:Path,output:Path,scenes:list[str],frames:int=120,timeout:int=1800,
               *,proton:Path|None=None,game_dir:Path|None=None)->dict:
    runner,package,output=runner.resolve(),package.resolve(),output.resolve()
    if not scenes or len(set(scenes))!=len(scenes) or any(name not in SCENES for name in scenes):
        raise ValueError("Select distinct implemented scenes")
    if not 1<=frames<=10000 or (any(name in scenes for name in ("actor","water-actor")) and frames<80) or ("save" in scenes and frames<16):
        raise ValueError("Expected 1..10000 frames, at least 80 for actors and 16 for source-save controls")
    if not 30<=timeout<=7200:raise ValueError("Expected a 30..7200 second per-game timeout")
    windows=proton is not None
    def snapshot():return fingerprint(runner,package,windows=windows)
    frozen=snapshot()
    output.mkdir(mode=0o700,parents=True,exist_ok=False)
    report={"kind":"actual-unity-bounded-native-feature-matrix","passed":False,"runner":str(runner),
        "package":str(package),"inputs":frozen,"scenes":scenes,"frames":frames,"cases":[],
        "first_playable_colony_passed":False,"whole_colony_fluid_authority":False,"windows_unity_verified":False,
        "host_platform":"linux","target_platform":"WindowsPlayer" if windows else "LinuxPlayer","actual_windows_os":False,
        "proton":str(proton.resolve()) if proton else None}
    destination=output/"matrix.json"
    def write():destination.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    def case(name:str,mode:str,extra:list[str])->dict:
        if snapshot()!=frozen:raise ValueError("Frozen runner or package changed before "+name)
        run=output/name
        executable=[str(runner/"tools/oni_proton_headless.py"),"--proton",str(proton.resolve())] if windows else [str(runner/"tools/oni_headless.py"),"run"]
        command=[sys.executable,*executable,"--package",str(package),"--mode",mode,
            "--frames",str(frames),"--timeout",str(timeout),"--output",str(run),*extra]
        if game_dir:command.extend(["--game-dir",str(game_dir.resolve())])
        print("START",name,flush=True)
        code=invoke(command,output/(name+".log"),timeout+(360 if windows else 120))
        result=run/"result.json"
        if not result.is_file():
            report["cases"].append({"name":name,"mode":mode,"exit_code":code,"status":"missing-result","result":str(result)})
            write();raise ValueError("Game case produced no result: "+name)
        data=json.loads(result.read_text(encoding="utf-8"))
        if not isinstance(data,dict):raise ValueError("Malformed game result: "+name)
        checkpoint=(data.get("native_domain") or {}).get("ColonyCheckpoint") or {}
        row={"name":name,"mode":mode,"exit_code":code,"status":data.get("status"),
            "result":str(result),"result_sha256":digest(result),"checkpoint":checkpoint,
            "host_saved_file":data.get("host_saved_file"),
            "native_domain":(data.get("native_domain") or {}).get("NativeDomain")}
        report["cases"].append(row);write()
        if code!=0 or data.get("status")!="passed-"+mode:raise ValueError("First rejected game case: "+name)
        if windows and (data.get("target_platform")!="WindowsPlayer" or data.get("host_platform")!="linux"
                        or data.get("actual_windows_os") is not False or data.get("real_unity_player") is not True):
            raise ValueError("WindowsPlayer evidence is missing or misidentifies the host: "+name)
        print("PASS",name,flush=True)
        return data
    write()
    try:
        for scene in scenes:
            mode=SCENES[scene]
            source_result=case(scene+"-save",mode,[] if scene=="save" else ["--save-colony"])
            checkpoint=(source_result.get("native_domain") or {}).get("ColonyCheckpoint") or {}
            saved_file=source_result.get("host_saved_file") if windows else checkpoint.get("SavedFile")
            if not isinstance(saved_file,str):raise ValueError("Source game has no saved file receipt")
            source=Path(saved_file)
            if not source.is_absolute() or not source.resolve().is_relative_to(output/(scene+"-save")):
                raise ValueError("Source save is outside its disposable game run")
            source_hash=digest(source)
            if checkpoint.get("Saved") is not True or checkpoint.get("SavedFileSha256")!=source_hash:
                raise ValueError("Source save does not match its completed write receipt")
            if windows and source_result.get("host_saved_file_sha256")!=source_hash:
                raise ValueError("Windows host save does not match its exported file receipt")
            case(scene+"-load","native-load-domain",["--load-save",str(source),"--resave-before-resume"])
            if digest(source)!=source_hash:raise ValueError("Source save changed during the fresh-process reload")
        if snapshot()!=frozen:raise ValueError("Frozen inputs changed during the matrix")
        report["passed"]=True
        report["windows_unity_verified"]=windows
    except (Exception,KeyboardInterrupt) as problem:
        report["failure"]=str(problem) or type(problem).__name__;write();raise
    write()
    return report

def main()->int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner-source",type=Path,required=True,help="Frozen checkout containing tools/oni_headless.py")
    parser.add_argument("--package",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True,help="Fresh private evidence directory")
    parser.add_argument("--scenes",nargs="+",choices=SCENES,default=list(SCENES))
    parser.add_argument("--frames",type=int,default=120)
    parser.add_argument("--timeout",type=int,default=1800)
    parser.add_argument("--proton",type=Path,help="Test the Windows Unity player under this installed Proton tool on Linux")
    parser.add_argument("--game-dir",type=Path,help="Licensed game installation for the selected player target")
    args=parser.parse_args()
    try:
        report=run_matrix(args.runner_source,args.package,args.output,args.scenes,args.frames,args.timeout,
                          proton=args.proton,game_dir=args.game_dir)
    except (OSError,ValueError,subprocess.SubprocessError) as problem:
        print("oni_native_matrix:",problem,file=sys.stderr);return 2
    print("Passed",len(report["cases"]),"bounded Unity save/load cases:",args.output/"matrix.json")
    return 0

if __name__=="__main__":raise SystemExit(main())
