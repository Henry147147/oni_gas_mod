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
    # The matching PDB's LF_PROCEDURE records confirm bool returns for both
    # overloads (types 0xD8F7 and 0xC495). Windows has no procedure or inline
    # site for either; see docs/windows-gas-ownership.md. No RVA is invented.
    ("gas-interaction", None, "_Z16DoGasInteractionR7SimDataR9SimEventsi", "bool(data*,events*,int)"),
    ("gas-interaction-pair", None, "_Z16DoGasInteractionR7SimDataR9SimEventsii", "bool(data*,events*,int,int)"),
    # The three indices are cell, ncell and nncell (matching PDB module 32).
    ("gas-displacement", "?DoGasPressureDisplacement@@YAMGHHHAEAVSimData@@@Z", "_Z25DoGasPressureDisplacementtiiiR7SimData", "float(uint16,int,int,int,data*)"),
    ("liquid-displacement", "?DoLiquidPressureDisplacement@@YAMGHHHAEAVSimData@@@Z", "_Z28DoLiquidPressureDisplacementtiiiR7SimData", "float(uint16,int,int,int,data*)"),
    # GCC emits a separate implementation for the secondary-base virtual call.
    # Its this pointer is already adjusted by the caller; forward it unchanged.
    ("emitter-dispatch", None, "_ZThn128_N14ElementEmitter6UpdateEfR7SimDataRK6Region", "void(adjusted-this*,float,data*,const-region*)"),
    ("chunk-heat", "ExchangeHeatEnergyWithWorld", "<inspected-GCC-private-chunk-contact>",
        {"windows-x64":"float(float,const-data*,const-handle*,cell-accessor*,float,element-chunk*)",
         "linux-x64":"float(cell-accessor*,element-chunk*,float,float)"}),
)

# These private procedure locations are supported only in the independently
# inspected files. Windows PDB 0xCFFD retains six arguments; GCC specializes
# away the unused SimData/Handle arguments and reorders the remaining ones.
CHUNK_HEAT_LOCATIONS={
    "windows-x64":("031960f120ac1d59fd8e43f6e1a7369111c629b90c24d52186b691dfe2d8a727",258448),
    "linux-x64":("85111cd0b1fc14c79c785581d6c055280b9d48ba705776f3d14eb0a7b239c12d",398624),
}
CHUNK_HEAT_LAYOUT=dict(accessor_bytes=16,cell_index_offset=8,chunk_bytes=44,temperature_offset=4,
    heat_capacity_offset=8,conductivity_offset=12,transfer_scale_offset=16)

# Separate read-only publication bindings leave the observer ABI and its public
# hook indices unchanged. No private GameData/STL layout is read by the mod.
PUBLICATION_SPECS = (
    ('copy-to-game', '?CopySimDataToGame@SimBase@@QEAAXAEAVSimData@@AEAVGameData@@AEBV3@H@Z',
     '_ZN7SimBase17CopySimDataToGameER7SimDataR8GameDataRKS2_i', 'void(this*,data*,game*,const-game*,int)'),
    ('prepare-game-data-update', '?PrepareGameDataUpdate@@YAPEAXAEBVGameData@@@Z',
     '_Z21PrepareGameDataUpdateRK8GameData', 'pointer(const-game*)'),
)

def normalized_identity(data: bytes) -> str:
    """SHA-256 of a native image with only the linker's build stamps zeroed.

    PE: COFF and export/resource directory timestamps, the optional-header
    checksum, and every debug directory entry's timestamp and data (CodeView
    GUID, age and PDB path). ELF: the GNU build-id note's descriptor. Every
    code and data byte is still covered, so an image with this identity has
    the inspected RVAs, layouts and fingerprints. Mirrored exactly by
    native/src/oni_image_identity.hpp (docs/game-update-resilience.md).
    """
    image = bytearray(data)

    def require(condition: bool, reason: str) -> None:
        if not condition:
            raise ValueError("Cannot normalise native image: " + reason)

    def u16(at: int) -> int:
        require(0 <= at and at + 2 <= len(image), "truncated field"); return struct.unpack_from("<H", image, at)[0]

    def u32(at: int) -> int:
        require(0 <= at and at + 4 <= len(image), "truncated field"); return struct.unpack_from("<I", image, at)[0]

    def u64(at: int) -> int:
        require(0 <= at and at + 8 <= len(image), "truncated field"); return struct.unpack_from("<Q", image, at)[0]

    def zero(at: int, count: int) -> None:
        require(0 <= at and count >= 0 and at + count <= len(image), "range outside the image"); image[at:at + count] = bytes(count)

    if image[:2] == b"MZ":
        pe = u32(60)
        require(image[pe:pe + 4] == b"PE\0\0", "missing PE signature")
        count, optional_size = u16(pe + 6), u16(pe + 20)
        optional = pe + 24
        magic = u16(optional)
        require(magic in (0x10B, 0x20B), "unknown optional header")
        directories, directory_count = optional + (96 if magic == 0x10B else 112), u32(optional + (92 if magic == 0x10B else 108))
        sections = []
        for index in range(count):
            at = optional + optional_size + 40 * index
            sections.append((u32(at + 12), max(u32(at + 8), u32(at + 16)), u32(at + 20), u32(at + 16)))

        def offset(rva: int, size: int) -> int:
            for address, extent, raw, raw_size in sections:
                if address <= rva and rva + size <= address + min(extent, raw_size):
                    return raw + rva - address
            raise ValueError("Cannot normalise native image: directory outside file-backed sections")

        zero(pe + 8, 4)
        zero(optional + 64, 4)
        for index in (0, 2):  # export and resource directory timestamps
            if index < directory_count and u32(directories + 8 * index):
                zero(offset(u32(directories + 8 * index), 8) + 4, 4)
        if 6 < directory_count and u32(directories + 48):
            start, size = offset(u32(directories + 48), u32(directories + 52)), u32(directories + 52)
            require(size % 28 == 0, "debug directory size")
            for entry in range(start, start + size, 28):
                zero(entry + 4, 4)
                length, raw = u32(entry + 16), u32(entry + 24)
                if raw:
                    zero(raw, length)
    elif image[:6] == b"\x7fELF\x02\x01":
        header, header_size, header_count = u64(32), u16(54), u16(56)
        require(header_size == 56, "program header size")
        for index in range(header_count):
            at = header + index * header_size
            if u32(at) != 4:  # PT_NOTE
                continue
            note, end = u64(at + 8), u64(at + 8) + u64(at + 32)
            require(end <= len(image), "note outside the image")
            while note + 12 <= end:
                name_size, descriptor_size, kind = u32(note), u32(note + 4), u32(note + 8)
                name = note + 12
                descriptor = name + ((name_size + 3) & ~3)
                require(descriptor + descriptor_size <= end, "truncated note")
                if kind == 3 and bytes(image[name:name + name_size]) == b"GNU\0":
                    zero(descriptor, descriptor_size)
                note = descriptor + ((descriptor_size + 3) & ~3)
    else:
        raise ValueError("Cannot normalise native image: neither PE nor ELF64")
    return sha(bytes(image))


def signature_for(signature,platform):
    return signature if isinstance(signature,str) else signature[platform]


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


def linux_load_segments(data: bytes) -> list[dict]:
    if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 62:
        raise ValueError("Expected little-endian Linux x86-64 ELF")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    phsize, count = struct.unpack_from("<HH", data, 54)
    if phsize != 56 or phoff + count * phsize > len(data):
        raise ValueError("Invalid ELF program headers")
    segments = []
    for index in range(count):
        offset = phoff + index * phsize
        kind, flags, raw, rva, _, files, memory, alignment = struct.unpack_from("<IIQQQQQQ", data, offset)
        if kind == 1:
            if files > memory or raw + files > len(data) or rva + memory >= 1 << 64:
                raise ValueError("Invalid ELF load segment")
            segments.append(dict(rva=rva, file_offset=raw, file_bytes=files,
                                 memory_bytes=memory, flags=flags, alignment=alignment))
    return segments


def linux_trampoline_storage(data: bytes) -> dict:
    """Reserve only zero file padding after an RX segment's allocated bytes.

    The page is already privately mapped by the loader. Segment contents,
    relocations, zero-initialized variables and other load segments are excluded.
    Runtime checks repeat the boundaries against the loaded program headers.
    """
    segments = linux_load_segments(data)
    page = 4096
    candidates = []
    for segment in segments:
        start, memory = segment["rva"], segment["memory_bytes"]
        end = start + memory
        first, last = (end + 15) & ~15, (end + page - 1) & ~(page - 1)
        if (segment["flags"] != 5 or segment["file_bytes"] != memory or
            segment["alignment"] != page or start % page != segment["file_offset"] % page or
            not first < last or first // page != (last - 1) // page):
            continue
        if any(other is not segment and other["rva"] < last and
               other["rva"] + other["memory_bytes"] > first for other in segments):
            continue
        raw = segment["file_offset"] + first - start
        padding = data[raw:raw + last - first]
        if len(padding) != last - first or any(padding):
            continue
        candidates.append(dict(rva=first, bytes=len(padding), page_bytes=page,
                               load_segment_rva=start, load_segment_file_offset=segment["file_offset"],
                               load_segment_memory_bytes=memory, fill_byte=0, sha256=sha(padding)))
    if not candidates:
        raise ValueError("No verified zero RX tail padding for Linux trampolines")
    return max(candidates, key=lambda item: item["bytes"])


def linux_symbols(data: bytes, image: Path) -> tuple[dict, list, dict]:
    segments = linux_load_segments(data)
    sections = [(s["rva"], s["file_offset"], s["file_bytes"], 0x20000000 if s["flags"] & 1 else 0)
                for s in segments]
    text = subprocess.check_output(["nm", "-D", "--defined-only", str(image)], text=True)
    symbols = {name: int(address, 16) for address, name in re.findall(r"^([0-9a-f]+)\s+\w\s+(\S+)$", text, re.M)}
    return symbols, sections, {"trampoline_storage": linux_trampoline_storage(data)}


def inspect(game: Path, platform: str, utility: str) -> dict:
    relative = "OxygenNotIncluded_Data/Plugins/" + ("x86_64/SimDLL.dll" if platform == "windows-x64" else "libSimDLL.so")
    image = game / relative
    data = image.read_bytes()
    symbols, sections, evidence = (windows_symbols(data, image.with_suffix(".pdb"), utility)
                                    if platform == "windows-x64" else linux_symbols(data, image))
    hooks = []
    for index, (role, win, linux, signature) in enumerate(SPECS):
        signature=signature_for(signature,platform)
        symbol = win if platform == "windows-x64" else linux
        if symbol is None:
            continue  # Windows Step is inlined; never invent an address.
        if role=="chunk-heat":
            identity,rva=CHUNK_HEAT_LOCATIONS[platform]
            if sha(data)!=identity:raise ValueError("Private chunk heat contact requires the exact inspected native image")
            if platform=="windows-x64":
                private=subprocess.check_output([utility,"dump","-symbols",str(image.with_suffix('.pdb'))],text=True)
                found=re.findall(r'S_[LG]PROC32[^\n]*`ExchangeHeatEnergyWithWorld`\s*\n[^\n]*addr = (\d+):(\d+)',private)
                if len(found)!=1 or sections[int(found[0][0])-1][0]+int(found[0][1])!=rva:
                    raise ValueError("Private PDB chunk heat procedure differs from its inspected location")
            else:
                call=0x61f27;start=file_offset(sections,call,5,executable=True);instruction=data[start:start+5]
                if instruction[0]!=0xe8 or call+5+struct.unpack('<i',instruction[1:])[0]!=rva:
                    raise ValueError("Specialized GCC chunk contact call differs from its inspected target")
        else:rva = symbols[symbol]
        offset = file_offset(sections, rva, 32, executable=True)
        hooks.append({"index": index, "role": role, "symbol": symbol, "rva": rva,
                      "fingerprint_bytes": 32, "fingerprint_sha256": sha(data[offset:offset + 32]),
                      "signature": signature,**({"native_layout":CHUNK_HEAT_LAYOUT} if role=="chunk-heat" else {})})
    publication_bindings = []
    for index, (role, win, linux, signature) in enumerate(PUBLICATION_SPECS):
        symbol = win if platform == 'windows-x64' else linux
        rva = symbols[symbol]; offset = file_offset(sections, rva, 32, executable=True)
        publication_bindings.append(dict(index=index, role=role, symbol=symbol, rva=rva,
            fingerprint_bytes=32, fingerprint_sha256=sha(data[offset:offset + 32]), signature=signature))
    globals_ = []
    for name in ("gSim", "gSimData"):
        symbol = (f"?{name}@@3V?$unique_ptr@V{name[1:]}@@U?$default_delete@V{name[1:]}@@@std@@@std@@A"
                  if platform == "windows-x64" else name)
        rva = symbols[symbol]
        # Lifecycle pointers may reside in zero-initialized, non-file-backed BSS.
        globals_.append({"name": name, "rva": rva})
    storage = evidence.pop("trampoline_storage", None)
    return {"platform": platform, "native_relative_path": relative, "native_identity": normalized_identity(data),
            "native_sha256": sha(data), "assembly_sha256": sha((game / "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll").read_bytes()),
            "native_bytes": len(data), "symbols": evidence, "globals": globals_, "hooks": hooks,
            "publication_bindings": publication_bindings,
            **({"trampoline_storage": storage} if storage else {})}


PLATFORMS = ("windows-x64", "linux-x64")


def validate(value: dict) -> None:
    """Profiles accumulate: one or more verified SimDLL images per platform,
    Windows first, each keyed by its normalised identity."""
    if value.get("schema_version") != 1 or value.get("oni_build") != 744825:
        raise ValueError("Unsupported profile schema/build")
    platforms = value.get("profiles", [])
    order = [p.get("platform") for p in platforms]
    if not platforms or order != sorted(order, key=PLATFORMS.index) or set(order) != set(PLATFORMS):
        raise ValueError("Both independent platform profiles are required, Windows first")
    identities = [p.get("native_identity") for p in platforms]
    if any(not isinstance(i, str) or not re.fullmatch("[0-9a-f]{64}", i) for i in identities) or len(set(identities)) != len(identities):
        raise ValueError("Every profile needs a distinct normalised native identity")
    for profile in platforms:
        if type(profile.get("oni_build", value["oni_build"])) is not int or profile.get("oni_build", value["oni_build"]) < value["oni_build"]:
            raise ValueError("Invalid profile game build")
        publication = profile.get('publication_bindings', [])
        if not isinstance(publication, list):
            raise ValueError('Invalid publication binding list')
        if publication:
            if len(publication) != 2 or [h.get('index') for h in publication] != [0, 1]:
                raise ValueError('Missing/duplicate publication binding')
            for hook, (role, win, linux, signature) in zip(publication, PUBLICATION_SPECS):
                if (hook.get('role'), hook.get('symbol'), hook.get('signature')) != (role, win if profile['platform'] == 'windows-x64' else linux, signature):
                    raise ValueError('Publication binding identity/signature mismatch')
                if not 0 < hook['rva'] < profile['native_bytes'] or hook.get('fingerprint_bytes') != 32 or not re.fullmatch('[0-9a-f]{64}', hook.get('fingerprint_sha256', '')):
                    raise ValueError('Invalid publication binding fingerprint')
            locations = [h['rva'] for h in [*profile['hooks'], *publication]]
            if len(set(locations)) != len(locations):
                raise ValueError('Publication binding aliases another hook')
        expected = {i for i, spec in enumerate(SPECS) if profile["platform"] != "windows-x64" or spec[1] is not None}
        if len(profile["hooks"]) != len(expected) or {h["index"] for h in profile["hooks"]} != expected:
            raise ValueError("Missing or duplicate required hook")
        for hook in profile["hooks"]:
            role, win, linux, signature = SPECS[hook["index"]]
            signature=signature_for(signature,profile['platform'])
            if (hook["role"], hook["symbol"], hook["signature"]) != (role, win if profile["platform"] == "windows-x64" else linux, signature):
                raise ValueError("Hook identity/signature mismatch")
            if role=='chunk-heat' and (hook.get('native_layout')!=CHUNK_HEAT_LAYOUT or
                    (profile['native_sha256'],hook['rva'])!=CHUNK_HEAT_LOCATIONS[profile['platform']]):
                raise ValueError("Private chunk contact layout/location differs from its inspected image")
            if not 0 < hook["rva"] < profile["native_bytes"] or hook["fingerprint_bytes"] != 32:
                raise ValueError("Invalid hook location/fingerprint size")
        for digest in (profile["native_sha256"], profile["assembly_sha256"], *(h["fingerprint_sha256"] for h in profile["hooks"])):
            if not re.fullmatch("[0-9a-f]{64}", digest):
                raise ValueError("Invalid SHA256 fingerprint")
        if [g["name"] for g in profile["globals"]] != ["gSim", "gSimData"] or any(g["rva"] <= 0 for g in profile["globals"]):
            raise ValueError("Native lifecycle globals missing")
        if profile["platform"] == "linux-x64":
            storage = profile.get("trampoline_storage", {})
            keys = ("rva", "bytes", "page_bytes", "load_segment_rva", "load_segment_file_offset",
                    "load_segment_memory_bytes", "fill_byte")
            if any(type(storage.get(key)) is not int for key in keys):
                raise ValueError("Missing or invalid Linux trampoline storage")
            if any(not 0 <= storage[key] < 1 << 63 for key in keys):
                raise ValueError("Out-of-range Linux trampoline storage")
            first, size, page = (storage[key] for key in ("rva", "bytes", "page_bytes"))
            segment, memory = storage["load_segment_rva"], storage["load_segment_memory_bytes"]
            end, raw = segment + memory, storage["load_segment_file_offset"]
            # The pinned x64 dependency needs a 16-byte header and 72 bytes per
            # entry. Its actual compiled capacity is checked again at attach.
            if (page != 4096 or segment <= 0 or memory <= 0 or raw < 0 or raw % page != segment % page or
                first != ((end + 15) & ~15) or first + size != ((end + page - 1) & ~(page - 1)) or
                first // page != (first + size - 1) // page or size < 16 + 72 * (len(profile["hooks"]) + len(publication)) or
                raw + first + size - segment > profile["native_bytes"] or storage["fill_byte"] != 0 or
                storage.get("sha256") != sha(bytes(size)) or
                any(first < h["rva"] + 32 and h["rva"] < first + size for h in [*profile["hooks"], *publication]) or
                any(first < g["rva"] + 8 and g["rva"] < first + size for g in profile["globals"])):
                raise ValueError("Invalid Linux trampoline storage bounds or fingerprint")


def header(value: dict) -> str:
    """Every verified profile of the build's platform; the backend selects
    the one whose normalised identity matches the game's SimDLL."""
    validate(value)
    lines = ["// Generated from native-hook-profiles.json; contains no game code.", "#pragma once"]
    for platform in PLATFORMS:
        profiles = [p for p in value["profiles"] if p["platform"] == platform]
        lines += ["#if defined(_WIN32)" if platform == "windows-x64" else "#else",
                  f'constexpr char native_relative_path[] = "{profiles[0]["native_relative_path"]}";']
        entries = []
        for number, profile in enumerate(profiles):
            name = f"profile_{number}"
            lines.append(f"constexpr HookProfile {name}_hooks[] = {{")
            for hook in profile["hooks"]:
                lines.append(f'    {{{hook["index"]}U, {hook["rva"]}U, "{hook["fingerprint_sha256"]}"}},')
            lines.append("};")
            publication = profile.get("publication_bindings", [])
            lines.append(f"constexpr std::array<HookProfile, {len(publication)}> {name}_bindings{{{{")
            for hook in publication:
                lines.append(f'    {{{hook["index"]}U, {hook["rva"]}U, "{hook["fingerprint_sha256"]}"}},')
            lines.append("}};")
            entry = (f'    {{{profile.get("oni_build", value["oni_build"])}U, "{profile["native_identity"]}", '
                     f'{profile["globals"][0]["rva"]}U, {profile["globals"][1]["rva"]}U, {name}_hooks, {name}_bindings')
            if platform == "linux-x64":
                storage = profile["trampoline_storage"]
                fields = ("rva", "bytes", "page_bytes", "load_segment_rva", "load_segment_file_offset", "load_segment_memory_bytes")
                entry += ", ma::oni::TrampolineStorage{" + ", ".join(f"{storage[key]}U" for key in fields) + f'}}, "{storage["sha256"]}"'
            entries.append(entry + "},")
        lines += ["constexpr NativeProfile native_profiles[] = {", *entries, "};"]
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
