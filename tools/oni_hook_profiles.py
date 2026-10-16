#!/usr/bin/env python3
"""Inspect local licensed native symbols or emit the observer's pinned profiles.

Only identities, addresses and fingerprints are written, never game code.
Windows inspection requires the matching PDB and llvm-pdbutil; Linux uses nm.
Ordinary builds consume the checked-in JSON without licensed dependencies.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "docs/native-hook-profiles.json"
# Index is the public MAOniHook enum. No private STL object crosses our ABI.
SPECS = (
    ("initialize", "SIM_Initialize", "SIM_Initialize", "void(callback)"),
    ("shutdown", "SIM_Shutdown", "SIM_Shutdown", "void()"),
    ("message", "SIM_HandleMessage", "SIM_HandleMessage", "pointer(int,int,byte*)"),
    ("messages", "SIM_HandleMessages", "SIM_HandleMessages", "pointer(int,int,int,byte*)"),
    ("begin-save", "SIM_BeginSave", "SIM_BeginSave", "byte*(int*,int,int)"),
    ("end-save", "SIM_EndSave", "SIM_EndSave", "void()"),
    ("main", "?Main@Sim@@MEAAXXZ", "_ZN3Sim4MainEv", "void(this*)"),
    ("process-frame", "?ProcessFrame@SimFrameManager@@SAXAEAUSimFrameInfo@@AEAVSimData@@@Z", "_ZN15SimFrameManager12ProcessFrameER12SimFrameInfoR7SimData", "void(frame*,data*)"),
    ("next-frame", "?ProcessNextFrame@SimFrameManager@@QEAAMAEAVSimData@@@Z", "_ZN15SimFrameManager16ProcessNextFrameER7SimData", "float(this*,data*)"),
    ("update-data", "?UpdateData@SimBase@@QEAAXAEAVSimData@@@Z", "_ZN7SimBase10UpdateDataER7SimData", "void(this*,data*)"),
    ("pressure", "?UpdatePressure@@YAMAEAVSimData@@AEAUSimEvents@@PEBUCellSOA@@PEAU3@HGCMH@Z", "_Z14UpdatePressureR7SimDataR9SimEventsPK7CellSOAPS3_itafi", "float(data*,events*,const-cell*,cell*,int,uint16,int8,float,int)"),
    ("liquid", "?UpdateLiquid@@YAXAEAVSimData@@AEAUSimEvents@@H@Z", "_Z12UpdateLiquidR7SimDataR9SimEventsi", "void(data*,events*,int)"),
    ("temperature", "?UpdateTemperature@@YAXAEAVSimData@@AEAUSimEvents@@HH@Z", "_Z17UpdateTemperatureR7SimDataR9SimEventsii", "void(data*,events*,int,int)"),
    ("state-transition", "?DoStateTransition@@YA_NAEAVSimData@@AEAUSimEvents@@HAEBUElementTemperatureData@@@Z", "_Z17DoStateTransitionR7SimDataR9SimEventsiRK22ElementTemperatureData", "bool(data*,events*,int,const-definition*)"),
    ("load-transition", "?DoLoadTimeStateTransition@@YAXAEAVSimData@@H@Z", "_Z25DoLoadTimeStateTransitionR7SimDatai", "void(data*,int)"),
    ("sublimation", "?DoSublimation@@YA_NAEAVSimData@@AEAUSimEvents@@HAEBUElementPostProcessData@@@Z", "_Z13DoSublimationR7SimDataR9SimEventsiRK22ElementPostProcessData", "bool(data*,events*,int,const-definition*)"),
    ("post-process", "?PostProcessCell@@YAXAEAVSimData@@AEAUSimEvents@@H@Z", "_Z15PostProcessCellR7SimDataR9SimEventsi", "void(data*,events*,int)"),
    ("step", None, "_ZN7SimBase4StepER7SimDataR8GameDataRKS2_", "void(this*,data*,game-data*,const-game-data*)"),
    ("begin-frame", "?BeginFrameProcessing@SimFrameManager@@QEAAHXZ", "_ZN15SimFrameManager20BeginFrameProcessingEv", "int(this*)"),
    ("end-frame", "?EndFrameProcessing@SimFrameManager@@QEAAXXZ", "_ZN15SimFrameManager18EndFrameProcessingEv", "void(this*)"),
    ("consumer", "?Update@ElementConsumer@@UEAAXMAEAVSimData@@AEBURegion@@@Z", "_ZN15ElementConsumer6UpdateEfR7SimDataRK6Region", "void(this*,float,data*,const-region*)"),
    ("emitter", "?Update@ElementEmitter@@UEAAXMAEAVSimData@@AEBURegion@@@Z", "_ZN14ElementEmitter6UpdateEfR7SimDataRK6Region", "void(this*,float,data*,const-region*)"),
    ("building-heat", "?Update@BuildingHeatExchange@@UEAAXMAEAVSimData@@AEBURegion@@@Z", "_ZN20BuildingHeatExchange6UpdateEfR7SimDataRK6Region", "void(this*,float,data*,const-region*)"),
)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pe_sections(data: bytes) -> list[tuple[int, int, int, int]]:
    if data[:2] != b"MZ":
        raise ValueError("Expected PE image")
    pe = struct.unpack_from("<I", data, 60)[0]
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    if data[pe:pe + 4] != b"PE\0\0" or machine != 0x8664:
        raise ValueError("Expected Windows x64 image")
    result = []
    for index in range(count):
        offset = pe + 24 + optional + index * 40
        size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
        flags = struct.unpack_from("<I", data, offset + 36)[0]
        result.append((rva, raw_offset, min(size, raw_size), flags))
    return result


def file_offset(sections: list, rva: int, count: int, executable: bool = False) -> int:
    for start, offset, size, flags in sections:
        if start <= rva and rva + count <= start + size:
            if executable and not flags & 0x20000000:
                raise ValueError("Hook location is not executable")
            return offset + rva - start
    raise ValueError(f"RVA {rva:x} outside file-backed sections")


def windows_symbols(data: bytes, pdb: Path, utility: str) -> tuple[dict, list, dict]:
    sections = pe_sections(data)
    summary = subprocess.check_output([utility, "dump", "-summary", str(pdb)], text=True)
    guid = re.search(r"GUID: \{([^}]+)\}", summary)
    age = re.search(r"Age: (\d+)", summary)
    if not guid or not age:
        raise ValueError("PDB identity missing")
    # Match the PE CodeView record, not just the PDB's filename.
    rsds = b"RSDS" + uuid.UUID(guid[1]).bytes_le + struct.pack("<I", int(age[1]))
    if rsds not in data:
        raise ValueError("PDB does not match native image")
    text = subprocess.check_output([utility, "dump", "-publics", str(pdb)], text=True)
    symbols = {}
    for name, section, offset in re.findall(r"S_PUB32[^\n]*`([^`]+)`\s*\n\s*flags[^\n]*addr = (\d+):(\d+)", text):
        if 1 <= int(section) <= len(sections):
            symbols[name] = sections[int(section) - 1][0] + int(offset)
    return symbols, sections, {"pdb_guid": guid[1].lower(), "pdb_age": int(age[1])}


def linux_symbols(data: bytes, image: Path) -> tuple[dict, list, dict]:
    if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 62:
        raise ValueError("Expected little-endian Linux x86-64 ELF")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phsize, count = struct.unpack_from("<HH", data, 54)
    sections = []
    for index in range(count):
        offset = phoff + index * phsize
        kind, flags, raw, rva, _, files, _ = struct.unpack_from("<IIQQQQQ", data, offset)
        if kind == 1:
            sections.append((rva, raw, files, 0x20000000 if flags & 1 else 0))
    text = subprocess.check_output(["nm", "-D", "--defined-only", str(image)], text=True)
    symbols = {name: int(address, 16) for address, name in re.findall(r"^([0-9a-f]+)\s+\w\s+(\S+)$", text, re.M)}
    return symbols, sections, {}


def inspect(game: Path, platform: str, utility: str) -> dict:
    relative = "OxygenNotIncluded_Data/Plugins/" + ("x86_64/SimDLL.dll" if platform == "windows-x64" else "libSimDLL.so")
    image = game / relative
    data = image.read_bytes()
    symbols, sections, evidence = (windows_symbols(data, image.with_suffix(".pdb"), utility)
                                    if platform == "windows-x64" else linux_symbols(data, image))
    hooks = []
    for index, (role, win, linux, signature) in enumerate(SPECS):
        symbol = win if platform == "windows-x64" else linux
        if symbol is None:
            continue  # Windows Step is inlined; never invent an address.
        rva = symbols[symbol]
        offset = file_offset(sections, rva, 32, executable=True)
        hooks.append({"index": index, "role": role, "symbol": symbol, "rva": rva,
                      "fingerprint_bytes": 32, "fingerprint_sha256": sha(data[offset:offset + 32]),
                      "signature": signature})
    globals_ = []
    for name in ("gSim", "gSimData"):
        symbol = (f"?{name}@@3V?$unique_ptr@V{name[1:]}@@U?$default_delete@V{name[1:]}@@@std@@@std@@A"
                  if platform == "windows-x64" else name)
        rva = symbols[symbol]
        # Lifecycle pointers may reside in zero-initialized, non-file-backed BSS.
        globals_.append({"name": name, "rva": rva})
    return {"platform": platform, "native_relative_path": relative,
            "native_sha256": sha(data), "assembly_sha256": sha((game / "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll").read_bytes()),
            "native_bytes": len(data), "symbols": evidence, "globals": globals_, "hooks": hooks}


def validate(value: dict) -> None:
    if value.get("schema_version") != 1 or value.get("oni_build") != 744825:
        raise ValueError("Unsupported profile schema/build")
    platforms = value.get("profiles", [])
    if [p["platform"] for p in platforms] != ["windows-x64", "linux-x64"]:
        raise ValueError("Both independent platform profiles are required")
    for profile in platforms:
        expected = {i for i, spec in enumerate(SPECS) if profile["platform"] != "windows-x64" or spec[1] is not None}
        if len(profile["hooks"]) != len(expected) or {h["index"] for h in profile["hooks"]} != expected:
            raise ValueError("Missing or duplicate required hook")
        for hook in profile["hooks"]:
            role, win, linux, signature = SPECS[hook["index"]]
            if (hook["role"], hook["symbol"], hook["signature"]) != (role, win if profile["platform"] == "windows-x64" else linux, signature):
                raise ValueError("Hook identity/signature mismatch")
            if not 0 < hook["rva"] < profile["native_bytes"] or hook["fingerprint_bytes"] != 32:
                raise ValueError("Invalid hook location/fingerprint size")
        for digest in (profile["native_sha256"], profile["assembly_sha256"], *(h["fingerprint_sha256"] for h in profile["hooks"])):
            if not re.fullmatch("[0-9a-f]{64}", digest):
                raise ValueError("Invalid SHA256 fingerprint")
        if [g["name"] for g in profile["globals"]] != ["gSim", "gSimData"] or any(g["rva"] <= 0 for g in profile["globals"]):
            raise ValueError("Native lifecycle globals missing")


def header(value: dict) -> str:
    validate(value)
    lines = ["// Generated from native-hook-profiles.json; contains no game code.", "#pragma once"]
    for index, profile in enumerate(value["profiles"]):
        lines += ["#if defined(_WIN32)" if index == 0 else "#else",
                  f'constexpr char native_relative_path[] = "{profile["native_relative_path"]}";',
                  f'constexpr char expected_native_hash[] = "{profile["native_sha256"]}";',
                  f'constexpr char expected_assembly_hash[] = "{profile["assembly_sha256"]}";',
                  f'constexpr std::uintptr_t sim_rva = {profile["globals"][0]["rva"]}U;',
                  f'constexpr std::uintptr_t data_rva = {profile["globals"][1]["rva"]}U;',
                  "constexpr HookProfile profile_hooks[] = {"]
        for hook in profile["hooks"]:
            lines.append(f'    {{{hook["index"]}U, {hook["rva"]}U, "{hook["fingerprint_sha256"]}"}},')
        lines += ["};"]
    return "\n".join(lines + ["#endif", ""])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("inspect", "header", "validate"))
    parser.add_argument("--windows-game", type=Path)
    parser.add_argument("--linux-game", type=Path)
    parser.add_argument("--pdbutil", default="llvm-pdbutil")
    parser.add_argument("--profiles", type=Path, default=PROFILE)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.command == "inspect":
        if not args.windows_game or not args.linux_game or not args.output:
            parser.error("inspect requires both licensed game roots and --output")
        value = {"schema_version": 1, "oni_build": 744825, "steam_build_id": 24423041,
                 "scope": "Forwarding observer only; no fluid authority or complete writer coverage.",
                 "profiles": [inspect(args.windows_game, "windows-x64", args.pdbutil), inspect(args.linux_game, "linux-x64", args.pdbutil)]}
        validate(value)
        args.output.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    else:
        value = json.loads(args.profiles.read_text(encoding="utf-8"))
        validate(value)
        if args.command == "header":
            if not args.output:
                parser.error("header requires --output")
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(header(value), encoding="utf-8")
        else:
            print("Valid Windows/Linux forwarding-observer profiles; ownership remains unverified.")


if __name__ == "__main__":
    main()
