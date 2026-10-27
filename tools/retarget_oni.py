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
   and RIP-relative targets are symbolised. The hooked functions are also the
   witnesses of the private layouts the backend reads (SimData, CellSOA,
   SimFrameInfo vectors, chunk registry), so a changed body needs review;
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
RELATIVE = {"windows-x64": "OxygenNotIncluded_Data/Plugins/x86_64/SimDLL.dll", "linux-x64": "OxygenNotIncluded_Data/Plugins/libSimDLL.so"}
WINDOW = 512  # bytes compared when a function's extent is unknown


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


def normalized_body(image: Path, data: bytes, platform: str, rva: int, extents: dict[int, int]) -> list[str]:
    """The function's instructions with addresses, branch targets and
    RIP-relative displacements replaced by position-independent forms.
    Immediates and memory offsets (struct layouts) are kept."""
    end = extents.get(rva, rva + WINDOW)
    base = image_base(data, platform)
    output = subprocess.check_output(
        ["objdump", "-d", "-w", "-M", "intel", "--no-show-raw-insn",
         f"--start-address={base + rva:#x}", f"--stop-address={base + end:#x}", str(image)], text=True)
    lines = []
    for line in output.splitlines():
        match = re.match(r"^\s*([0-9a-f]+):\s+(.*)$", line)
        if not match:
            continue
        text = re.sub(r"\s+#.*$", "", match[2]).strip()
        text = re.sub(r"<[^>]*>", "", text)
        text = re.sub(r"rip\s*[+-]\s*0x[0-9a-f]+", "rip+REL", text)

        def target(found: re.Match) -> str:
            value = int(found[0], 16) - base
            return f"L{value - rva:+#x}" if rva <= value < end else "EXT"
        if re.match(r"^(call|j\w+|loop\w*)\s", text):
            text = re.sub(r"\b(?:0x)?[0-9a-f]{4,}\b", target, text)
        lines.append(" ".join(text.split()))
    return lines


def native_report(platform: str, new_root: Path, reference_root: Path | None, value: dict, pdbutil: str) -> dict:
    image = new_root / RELATIVE[platform]
    data = image.read_bytes()
    identity = profiles.normalized_identity(data)
    known = [p for p in value["profiles"] if p["platform"] == platform]
    match = next((p for p in known if p["native_identity"] == identity), None)
    report = {"image": str(image), "normalized_identity": identity, "sha256": sha(data),
              "verified_profile": match is not None, "hooks": [], "needs_review": [], "candidate": None}
    if match is not None:
        report["status"] = "unchanged: a verified profile already covers this image"
        return report
    reference = known[-1]
    if reference_root is None or not (reference_root / RELATIVE[platform]).is_file():
        report["status"] = "needs review: no reference installation to compare function bodies"
        report["needs_review"].append("Provide the reference installation (--reference-" + platform.split("-")[0] + ")")
        return report
    reference_image = reference_root / RELATIVE[platform]
    reference_data = reference_image.read_bytes()
    if profiles.normalized_identity(reference_data) != reference["native_identity"]:
        raise ValueError(f"The {platform} reference installation is not the image of the latest verified profile")
    # Symbols of the new image: the PDB (Windows) or dynamic symbols (Linux).
    symbols, sections, _ = (profiles.windows_symbols(data, image.with_suffix(".pdb"), pdbutil) if platform == "windows-x64"
                            else profiles.linux_symbols(data, image))
    new_extents, old_extents = function_extents(data, platform, image), function_extents(reference_data, platform, reference_image)
    candidate = copy.deepcopy(reference)
    candidate.update({"native_identity": identity, "native_sha256": sha(data), "native_bytes": len(data)})
    if (new_root / "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll").is_file():
        candidate["assembly_sha256"] = sha((new_root / "OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll").read_bytes())
    for group in ("hooks", "publication_bindings"):
        for hook, entry in zip(reference.get(group, []), candidate.get(group, [])):
            symbol = hook["symbol"]
            # Private locations (not exported) keep their reference RVA; a
            # moved or changed body there needs the location re-derived.
            private = symbol.startswith("<") or hook["role"] == "chunk-heat"
            rva = hook["rva"] if private else symbols.get(symbol)
            item = {"group": group, "role": hook["role"], "symbol": symbol, "reference_rva": hook["rva"], "rva": rva}
            if rva is None:
                item["result"] = "symbol missing"
                report["needs_review"].append(f"{hook['role']}: symbol {symbol} is missing")
            else:
                offset = profiles.file_offset(sections, rva, 32, executable=True)
                item["fingerprint_sha256"] = sha(data[offset:offset + 32])
                old = normalized_body(reference_image, reference_data, platform, hook["rva"], old_extents)
                new = normalized_body(image, data, platform, rva, new_extents)
                item["equivalent_body"] = old == new and bool(old)
                item["result"] = "carried" if item["equivalent_body"] else "body changed"
                if not item["equivalent_body"]:
                    report["needs_review"].append(f"{hook['role']} ({symbol}): function body changed"
                                                  + (" (private location: re-derive it in oni_hook_profiles.py)" if private else ""))
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
    game_contract.generate_single(managed, platform, new_root, contract, raw=True)
    shipped = managed / game_contract.FILE_NAME
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
        report["native"][platform] = native_report(platform, root.resolve(), reference, value, args.pdbutil)
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
    for platform, managed in report["managed"].items():
        count = len(managed["differences"] or [])
        lines.append(f"- {platform} managed: {count} contract differences")
    for platform, elements in report["elements"].items():
        lines.append(f"- {platform} element definitions: " + (", ".join(elements.get("changed", [])) or "unchanged" if elements["compared"] else "not compared"))
    lines += ["", "## Needs review or code changes", ""] + ([f"- {n}" for n in needs] or ["- Nothing: the build is already supported."])
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
