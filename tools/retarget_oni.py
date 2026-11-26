#!/usr/bin/env python3
"""Inspect a newer ONI installation and report what retargeting it needs.

docs/game-update-resilience.md#retargeting-a-new-build. Given the new Windows
and/or Linux installation, this compares it with the reference installation
the current profiles and contract were written against, and writes a private
report under artifacts/retarget/:

1. build and identity: the game build, each SimDLL's normalised identity, and
   whether it already matches a verified profile;
2. native: each hook and binding located again by symbol, its fingerprint,
   and whether its function body is equivalent to the reference once branch
   and RIP-relative targets are symbolised (objdump's names; on Windows, whose
   PE has no symbol table, the matching PDB's). The hooked functions are also
   the witnesses of the private layouts the backend reads (SimData, CellSOA,
   SimFrameInfo vectors, chunk registry), so a changed body needs review. On
   Windows the report also lists every section contribution whose PDB data
   CRC changed, which names any changed callee or table;
3. managed: the current mod assembly's game contract checked against the new
   assemblies (every changed, missing or new dependency), with raw values for
   the changed entries when the reference installation is available;
4. data: element definition files whose change alters the gas/liquid
   catalogue, which mixture saves are bound to.

A candidate profile is written when the image is new; it is "carried" only
when every hooked function is equivalent. Nothing under docs/ is changed
unless --write-profiles is given and the candidate is carried. Licensed files
and full reports stay out of the repository.
"""
from __future__ import annotations

import argparse
import bisect
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_contract  # noqa: E402
import oni_hook_profiles as profiles  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def function_extents(data: bytes, platform: str, image: Path) -> dict[int, int]:
    """Start RVA -> end RVA of every function the image describes."""
    extents: dict[int, int] = {}
    if platform == "windows-x64":
        pe = struct.unpack_from("<I", data, 60)[0]
        optional = pe + 24
        directory = optional + 112 + 3 * 8  # exception directory (.pdata)
        rva, size = struct.unpack_from("<II", data, directory)
        sections = profiles.pe_sections(data)
        start = profiles.file_offset(sections, rva, size)
        for entry in range(start, start + size, 12):
            begin, end, _ = struct.unpack_from("<III", data, entry)
            if begin and end > begin:
                extents.setdefault(begin, end)
    else:
        text = subprocess.check_output(["nm", "-D", "-S", "--defined-only", str(image)], text=True)
        for address, size, _, name in re.findall(r"^([0-9a-f]+) ([0-9a-f]+) (\w) (\S+)$", text, re.M):
            if int(size, 16):
                extents.setdefault(int(address, 16), int(address, 16) + int(size, 16))
    return extents


def image_base(data: bytes, platform: str) -> int:
    if platform != "windows-x64":
        return 0
    pe = struct.unpack_from("<I", data, 60)[0]
    return struct.unpack_from("<Q", data, pe + 24 + 24)[0]


def pe_section_extents(data: bytes) -> list[tuple[int, int]]:
    """(start, end) RVA of every PE section by its virtual size (BSS included)."""
    pe = struct.unpack_from("<I", data, 60)[0]
    count, optional = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
    extents = []
    for index in range(count):
        size, rva = struct.unpack_from("<II", data, pe + 24 + optional + 40 * index + 8)
        extents.append((rva, rva + size))
    return extents


class PdbSymbolizer:
    """RVA -> stable name in a Windows image, from its matching PDB.

    The PE carries no COFF symbol table, and objdump (GNU binutils 2.46) prints
    no <symbol> for its branch or RIP-relative targets, so without this every
    external target of every hooked function was unresolved. An exact location
    takes its public (mangled) name first, then a module procedure or data
    name; identical-COMDAT-folded functions share an address and the smallest
    name is used. An address inside a procedure is that procedure plus the
    offset; any other address is the nearest preceding named location in the
    same section plus the offset. An address with no such location is None.
    """

    def __init__(self, publics: dict[str, int], records: list[tuple[str, str, int, int]],
                 sections: list[tuple[int, int]]):
        named = sorted([(rva, 0, name) for name, rva in publics.items()] +
                       [(rva, 1 if kind.endswith("PROC32") else 2, name) for kind, name, rva, _ in records])
        self.exact: dict[int, str] = {}
        for rva, _, name in named:
            self.exact.setdefault(rva, name)
        self.starts = sorted(self.exact)
        procedures = sorted({(rva, rva + size) for kind, _, rva, size in records if kind.endswith("PROC32") and size})
        self.procedure_starts = [start for start, _ in procedures]
        self.procedures = procedures
        self.sections = sorted(sections)

    def __call__(self, rva: int) -> str | None:
        if rva in self.exact:
            return self.exact[rva]
        index = bisect.bisect_right(self.procedure_starts, rva) - 1
        if index >= 0 and rva < self.procedures[index][1]:
            start = self.procedures[index][0]
            return f"{self.exact[start]}+{rva - start:#x}"
        section = next(((start, end) for start, end in self.sections if start <= rva < end), None)
        index = bisect.bisect_right(self.starts, rva) - 1
        if section is None or index < 0 or self.starts[index] < section[0]:
            return None
        start = self.starts[index]
        return f"{self.exact[start]}+{rva - start:#x}"


def pdb_symbolizer(image: Path, data: bytes, pdbutil: str) -> PdbSymbolizer | None:
    """The symbolizer of a Windows image whose matching PDB sits beside it."""
    pdb = image.with_suffix(".pdb")
    if not pdb.is_file():
        return None
    publics, sections, _ = profiles.windows_symbols(data, pdb, pdbutil)  # refuses a PDB of another image
    return PdbSymbolizer(publics, profiles.windows_module_symbols(pdb, sections, pdbutil), pe_section_extents(data))


def pdb_contributions(image: Path, data: bytes, pdbutil: str) -> dict[int, tuple[int, int, str]]:
    """RVA -> (size, data CRC, object file) of every section contribution in
    the image's matching PDB. The linker records a CRC of each object's bytes,
    so this names exactly which pieces of the image changed."""
    pdb = image.with_suffix(".pdb")
    sections = profiles.pe_sections(data)
    profiles.windows_symbols(data, pdb, pdbutil)  # refuses a PDB of another image
    modules = {int(number): re.split(r"[\\/]", path)[-1] for number, path in re.findall(
        r"^\s*Mod (\d+) \| `([^`]*)`", subprocess.check_output([pdbutil, "dump", "-modules", str(pdb)], text=True), re.M)}
    result = {}
    for module, section, offset, size, crc in re.findall(
            r"SC\[[^\]]*\]\s*\| mod = (\d+), (\d+):(\d+), size = (\d+), data crc = (\d+)",
            subprocess.check_output([pdbutil, "dump", "-section-contribs", str(pdb)], text=True)):
        if 1 <= int(section) <= len(sections):
            result[sections[int(section) - 1][0] + int(offset)] = (int(size), int(crc), modules.get(int(module), module))
    return result


def changed_contributions(old: dict, new: dict, symbolize) -> list[dict]:
    """Every contribution whose size, CRC or object differs, or that exists in
    one image only. Informational: a hooked body is compared separately; a
    changed callee or table it reads is found here."""
    changes = []
    for rva in sorted(set(old) | set(new)):
        if old.get(rva) != new.get(rva):
            size, _, module = new.get(rva) or old[rva]
            changes.append({"rva": rva, "size": size, "object": module, "symbol": symbolize(rva) if symbolize else None,
                            "change": "added" if rva not in old else "removed" if rva not in new else "changed"})
    return changes


def normalized_body(image: Path, data: bytes, platform: str, rva: int, extents: dict[int, int],
                    symbolize=None, indirect_operands: bool = False) -> list[str]:
    """Compare internal labels relatively; retain external destination identity.

    An external code/data reference must have both an image-relative address
    and a symbol: objdump's, or, when `symbolize` is given (a Windows image,
    whose PE has no COFF symbols), the matching PDB's. Relocated external
    destinations conservatively need review. Never erase a destination or
    equate unresolved indirect calls: an indirect branch refuses the
    comparison, and only `indirect_operands` (a reviewer's explicit
    acceptance) keeps its operand verbatim instead. Branch and RIP-relative
    targets inside the function compare by their offset. Immediates and
    memory offsets (struct layouts) are kept.
    """
    if rva not in extents or extents[rva] <= rva:
        raise ValueError(f"Unresolved function extent at {image.name}+{rva:#x}")
    end = extents[rva]
    base = image_base(data, platform)
    output = subprocess.check_output(
        ["objdump", "-d", "-w", "-M", "intel", "--no-show-raw-insn",
         f"--start-address={base + rva:#x}", f"--stop-address={base + end:#x}", str(image)], text=True)

    def destination(value: int, listed: str | None, kind: str, location: str) -> str:
        if rva <= value < end:
            return f"L{value - rva:+#x}"
        name = symbolize(value) if symbolize is not None else listed
        if not name:
            raise ValueError(f"Unresolved {kind} target at {location}")
        return f"RVA[{value:#x} <{name}>]"

    lines = []
    for line in output.splitlines():
        match = re.match(r"^\s*([0-9a-f]+):\s+(.*)$", line)
        if not match:
            continue
        text = match[2].strip()
        location = f"{image.name}+{int(match[1], 16) - base:#x}"
        if re.search(r"\brip\b", text):
            target = re.search(r"\s+#\s*(?:0x)?([0-9a-f]+)(?:\s+<([^>]+)>)?\s*$", text)
            if target is None:
                raise ValueError(f"Unresolved RIP-relative target at {location}")
            reference = destination(int(target[1], 16) - base, target[2], "RIP-relative", location)
            text = re.sub(r"\s+#.*$", "", text)
            text, replacements = re.subn(r"\[rip(?:\s*[+-]\s*(?:0x)?[0-9a-f]+)?\]", f"[{reference}]", text)
            if replacements != 1:
                raise ValueError(f"Unresolved RIP-relative operand at {location}")
        else:
            branch = re.match(r"^((?:(?:bnd|notrack)\s+)?(?:callq?|j\w+|loop\w*))\s+(.+)$", text)
            if branch:
                target = re.fullmatch(r"(?:0x)?([0-9a-f]+)(?:\s+<([^>]+)>)?", branch[2])
                if target is None and not indirect_operands:
                    raise ValueError(f"Unresolved indirect branch target at {location}")
                text = (f"{branch[1]} INDIRECT[{branch[2]}]" if target is None else
                        f"{branch[1]} {destination(int(target[1], 16) - base, target[2], 'external branch', location)}")
        lines.append(" ".join(text.split()))
    return lines


def native_report(platform: str, new_root: Path, reference_root: Path | None, value: dict, pdbutil: str,
                  accept_indirect: bool = False) -> dict:
    image = profiles.sim_image(new_root, platform)
    data = image.read_bytes()
    identity = profiles.normalized_identity(data)
    known = [p for p in value["profiles"] if p["platform"] == platform]
    match = next((p for p in known if p["native_identity"] == identity), None)
    report = {"image": str(image), "normalized_identity": identity, "sha256": sha(data),
              "verified_profile": match is not None, "hooks": [], "needs_review": [], "candidate": None,
              "accepted_indirect": []}
    if match is not None:
        report["status"] = "unchanged: a verified profile already covers this image"
        return report
    carried = profiles.carried_profile(known, data)
    if carried is not None:
        # docs/game-update-resilience.md#tolerated-newer-builds: the installed
        # package already accepts this image; a new profile is still written
        # when the build is registered.
        report["carried_from"] = carried.get("oni_build", value["oni_build"])
        report["revision_masked_identity"] = carried["revision_masked_identity"]
        report["note"] = (f"carried: equal to the verified image of build {report['carried_from']} apart from the revision constant "
                          f"at {carried['revision_site']['rva']:#x} and the Authenticode signature")
    if reference_root is None or not profiles.sim_image(reference_root, platform).is_file():
        report["status"] = "needs review: no reference installation to compare function bodies"
        report["needs_review"].append("Provide the reference installation (--reference-" + platform.split("-")[0] + ")")
        return report
    reference_image = profiles.sim_image(reference_root, platform)
    reference_data = reference_image.read_bytes()
    reference_identity = profiles.normalized_identity(reference_data)
    reference = next((p for p in known if p["native_identity"] == reference_identity), None)
    if reference is None:
        raise ValueError(f"The {platform} reference installation is not the image of a verified profile")
    # Symbols of the new image: the PDB (Windows) or dynamic symbols (Linux).
    symbols, sections, evidence = (profiles.windows_symbols(data, image.with_suffix(".pdb"), pdbutil) if platform == "windows-x64"
                                   else profiles.linux_symbols(data, image))
    new_symbolize = old_symbolize = None
    private_procedures: list = []
    if platform == "windows-x64":
        # objdump names no target in a PE image: both sides use their PDBs.
        private_procedures = profiles.windows_module_symbols(image.with_suffix(".pdb"), sections, pdbutil)
        new_symbolize = PdbSymbolizer(symbols, private_procedures, pe_section_extents(data))
        old_symbolize = pdb_symbolizer(reference_image, reference_data, pdbutil)
        if old_symbolize is not None:
            report["changed_contributions"] = changed_contributions(
                pdb_contributions(reference_image, reference_data, pdbutil), pdb_contributions(image, data, pdbutil), new_symbolize)
    new_extents, old_extents = function_extents(data, platform, image), function_extents(reference_data, platform, reference_image)
    candidate = copy.deepcopy(reference)
    candidate.update({"native_identity": identity, "native_sha256": sha(data), "native_bytes": len(data)})
    if platform == "windows-x64":
        candidate["symbols"] = evidence  # the new PDB's GUID and age
        for key in ("revision_site", "revision_masked_identity"):
            candidate.pop(key, None)
        candidate.update(profiles.revision_fields(data, symbols))
    if (new_root / "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll").is_file():
        candidate["assembly_sha256"] = sha((new_root / "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll").read_bytes())
    for group in ("hooks", "publication_bindings"):
        for hook, entry in zip(reference.get(group, []), candidate.get(group, [])):
            symbol = hook["symbol"]
            # Private locations (not exported). Windows keeps the procedure's
            # PDB record, so it is found again by name; Linux keeps the
            # reference RVA. Either way the body must be equivalent and the
            # image's location registered in CHUNK_HEAT_LOCATIONS.
            private = symbol.startswith("<") or hook["role"] == "chunk-heat"
            if private and platform == "windows-x64":
                found = sorted({r for kind, name, r, _ in private_procedures if kind.endswith("PROC32") and name == symbol})
                rva = found[0] if len(found) == 1 else None
            else:
                rva = hook["rva"] if private else symbols.get(symbol)
            item = {"group": group, "role": hook["role"], "symbol": symbol, "reference_rva": hook["rva"], "rva": rva}
            if rva is None:
                item["result"] = "symbol missing"
                report["needs_review"].append(f"{hook['role']}: symbol {symbol} is missing")
            else:
                offset = profiles.file_offset(sections, rva, 32, executable=True)
                item["fingerprint_sha256"] = sha(data[offset:offset + 32])
                try:
                    old = normalized_body(reference_image, reference_data, platform, hook["rva"], old_extents, old_symbolize)
                    new = normalized_body(image, data, platform, rva, new_extents, new_symbolize)
                    item["equivalent_body"] = old == new and bool(old)
                    item["result"] = "carried" if item["equivalent_body"] else "body changed"
                except ValueError as error:
                    item["equivalent_body"] = False
                    item["result"] = str(error)
                    if "indirect branch" in str(error):
                        # Say whether the indirect calls are all that stands
                        # in the way; equate them only on explicit acceptance.
                        try:
                            old = normalized_body(reference_image, reference_data, platform, hook["rva"], old_extents,
                                                  old_symbolize, indirect_operands=True)
                            new = normalized_body(image, data, platform, rva, new_extents, new_symbolize, indirect_operands=True)
                        except ValueError:
                            old = new = []
                        if old == new and old:
                            item["indirect_branches"] = sum("INDIRECT[" in text for text in new)
                            if accept_indirect:
                                item["equivalent_body"] = True
                                item["result"] = f"carried: {item['indirect_branches']} identical indirect branches accepted after review"
                                report["accepted_indirect"].append(hook["role"])
                            else:
                                item["result"] += (f"; otherwise equivalent, with {item['indirect_branches']} identical indirect "
                                                   "branch operands (review them, then rerun with --accept-indirect)")
                if not item["equivalent_body"]:
                    report["needs_review"].append(f"{hook['role']} ({symbol}): {item['result']}"
                                                  + (" (private location: re-derive it in oni_hook_profiles.py)" if private else ""))
                elif hook["role"] == "chunk-heat" and profiles.CHUNK_HEAT_LOCATIONS[platform].get(sha(data)) != rva:
                    report["needs_review"].append(
                        f"chunk-heat ({symbol}): equivalent at {image.name}+{rva:#x}, but this image (SHA-256 {sha(data)}) "
                        "is not in CHUNK_HEAT_LOCATIONS; check the private procedure and its type, then add it in oni_hook_profiles.py")
                entry["rva"], entry["fingerprint_sha256"] = rva, item["fingerprint_sha256"]
            report["hooks"].append(item)
    for item, (name, rva) in zip(candidate["globals"], ((g["name"], symbols.get(
            f"?{g['name']}@@3V?$unique_ptr@V{g['name'][1:]}@@U?$default_delete@V{g['name'][1:]}@@@std@@@std@@A" if platform == "windows-x64" else g["name"]))
            for g in reference["globals"])):
        if rva is None:
            report["needs_review"].append(f"global {name} is missing")
        else:
            item["rva"] = rva
    if platform == "linux-x64":
        try:
            candidate["trampoline_storage"] = profiles.linux_trampoline_storage(data)
        except ValueError as error:
            report["needs_review"].append("Linux trampoline storage: " + str(error))
    candidate["oni_build"] = None
    report["candidate"] = candidate
    report["status"] = "carried: every hooked function is equivalent" if not report["needs_review"] else "needs review"
    return report


def element_report(new_root: Path, reference_root: Path | None) -> dict:
    def files(root: Path) -> dict[str, str]:
        assets = root / "OxygenNotIncluded_Data/StreamingAssets"
        return {p.relative_to(assets).as_posix(): sha(p.read_bytes()) for p in sorted(assets.rglob("*.yaml")) if "elements" in p.parts}
    new = files(new_root)
    if reference_root is None:
        return {"compared": False, "files": sorted(new)}
    old = files(reference_root)
    changed = sorted(k for k in set(new) | set(old) if new.get(k) != old.get(k))
    return {"compared": True, "changed": changed,
            "note": "Gas or liquid definition changes alter the material catalogue; mixture saves are refused until a save migration exists." if changed else ""}


def managed_report(platform: str, new_root: Path, reference_root: Path | None, managed: Path, folder: Path) -> dict:
    contract = folder / f"contract-{platform}.json"
    shipped = managed / game_contract.FILE_NAME
    try:
        game_contract.generate_single(managed, platform, new_root, contract, raw=True)
    except ValueError as error:
        # The mod cannot even enumerate its patch targets in the new build (a
        # target found by name is gone). Report that, and what the shipped
        # contract says, instead of stopping before the report is written.
        differences = game_contract.differences(managed, platform, new_root, shipped) \
            if shipped.is_file() and platform in json.loads(shipped.read_text())["Platforms"] else None
        return {"differences": differences, "raw_changes": [],
                "generation_failed": str(error).strip().splitlines()[-40:]}
    differences = game_contract.differences(managed, platform, new_root, shipped) \
        if shipped.is_file() and platform in json.loads(shipped.read_text())["Platforms"] else None
    report = {"differences": differences, "raw_changes": []}
    if reference_root is not None:
        reference = folder / f"reference-contract-{platform}.json"
        game_contract.generate_single(managed, platform, reference_root, reference, raw=True)
        old = {e["Key"]: e for e in json.loads(reference.read_text())["Platforms"][platform]["Entries"]}
        new = {e["Key"]: e for e in json.loads(contract.read_text())["Platforms"][platform]["Entries"]}
        for key in sorted(set(old) | set(new)):
            if old.get(key, {}).get("Value") != new.get(key, {}).get("Value"):
                report["raw_changes"].append({"key": key, "kind": (old.get(key) or new.get(key))["Kind"],
                                              "reference": (old.get(key) or {}).get("Value"), "new": (new.get(key) or {}).get("Value")})
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--windows-game", type=Path, help="New Windows installation root")
    parser.add_argument("--linux-game", type=Path, help="New Linux installation root")
    parser.add_argument("--reference-windows", type=Path, default=game_contract.DEFAULT_GAMES["windows-x64"])
    parser.add_argument("--reference-linux", type=Path, default=game_contract.DEFAULT_GAMES["linux-x64"])
    parser.add_argument("--managed-dir", type=Path, help="Built mod (with its game-contract.json for the contract check)")
    parser.add_argument("--output", type=Path, help="Report folder (default artifacts/retarget/<time>)")
    parser.add_argument("--pdbutil", default="llvm-pdbutil")
    parser.add_argument("--write-profiles", action="store_true", help="Append carried candidates to docs/native-hook-profiles.json")
    parser.add_argument("--build", type=int, help="The new game build number (required with --write-profiles)")
    parser.add_argument("--accept-indirect", action="store_true",
                        help="After reviewing them, accept hooked bodies that are equivalent except for indirect "
                             "branches whose operands are identical (recorded in the report)")
    args = parser.parse_args()
    games = {p: r for p, r in (("windows-x64", args.windows_game), ("linux-x64", args.linux_game)) if r}
    if not games:
        parser.error("Give at least one new installation")
    folder = (args.output or ROOT / "artifacts/retarget" / __import__("time").strftime("%Y%m%dT%H%M%S")).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    value = json.loads(profiles.PROFILE.read_text())
    references = {"windows-x64": args.reference_windows, "linux-x64": args.reference_linux}
    report = {"native": {}, "managed": {}, "elements": {}}
    for platform, root in games.items():
        reference = references[platform] if references[platform] and references[platform].resolve() != root.resolve() else None
        report["native"][platform] = native_report(platform, root.resolve(), reference, value, args.pdbutil, args.accept_indirect)
        report["elements"][platform] = element_report(root.resolve(), reference)
        if args.managed_dir:
            report["managed"][platform] = managed_report(platform, root.resolve(), reference, args.managed_dir.resolve(), folder)
    needs = [f"{p}: {item}" for p, r in report["native"].items() for item in r["needs_review"]]
    for platform, managed in report["managed"].items():
        needs += [f"{platform} managed: {d}" for d in (managed["differences"] or [])]
    for platform, elements in report["elements"].items():
        needs += [f"{platform} elements changed: {f}" for f in elements.get("changed", [])]
    report["needs_review"] = needs
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    lines = ["# Retarget report", ""]
    for platform, native in report["native"].items():
        lines.append(f"- {platform} native: {native['status']} (identity {native['normalized_identity'][:16]})")
        if native.get("note"):
            lines.append(f"- {platform} native: {native['note']} (revision-masked identity {native['revision_masked_identity'][:16]})")
        if native.get("accepted_indirect"):
            lines.append(f"- {platform} native: indirect branches accepted after review in " + ", ".join(native["accepted_indirect"]))
    for platform, managed in report["managed"].items():
        # A package of one platform carries that platform's contract only.
        checked = (f"{len(managed['differences'])} contract differences" if managed["differences"] is not None
                   else "not checked, the package's contract does not cover this platform")
        lines.append(f"- {platform} managed: {checked}; {len(managed['raw_changes'])} entries differ from the reference build")
    for platform, elements in report["elements"].items():
        lines.append(f"- {platform} element definitions: " + (", ".join(elements.get("changed", [])) or "unchanged" if elements["compared"] else "not compared"))
    nothing = ("- Nothing: the carried candidate profiles can be written (--write-profiles --build N)."
               if any(r["candidate"] is not None for r in report["native"].values()) else "- Nothing: the build is already supported.")
    lines += ["", "## Needs review or code changes", ""] + ([f"- {n}" for n in needs] or [nothing])
    for platform, native in report["native"].items():
        if "changed_contributions" in native:
            # Informational: callees and data the hooked bodies use live here.
            lines += ["", f"## {platform} changed section contributions (PDB data CRC)", ""]
            lines += [f"- {c['change']} {c['object']} +{c['rva']:#x} ({c['size']} bytes): {c['symbol']}"
                      for c in native["changed_contributions"]] or ["- None."]
    (folder / "report.md").write_text("\n".join(lines) + "\n")
    if args.write_profiles:
        carried = [r["candidate"] for r in report["native"].values() if r["candidate"] is not None and not r["needs_review"]]
        if carried and not needs:
            for candidate in carried:
                if not args.build:
                    parser.error("--write-profiles needs --build")
                candidate["oni_build"] = args.build
                value["profiles"].append(candidate)
            value["profiles"].sort(key=lambda p: profiles.PLATFORMS.index(p["platform"]))
            profiles.validate(value)
            profiles.PROFILE.write_text(json.dumps(value, indent=2) + "\n")
    print((folder / "report.md").read_text())
    return 0 if not needs else 1


if __name__ == "__main__":
    sys.exit(main())
