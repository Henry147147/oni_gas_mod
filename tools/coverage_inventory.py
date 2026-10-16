#!/usr/bin/env python3
"""Generate and check factual ownership coverage; never infer a passing game gate.

Only identities, interface declarations and original acceptance requirements are
committed. Private captured payloads, properties, saves and game code stay out.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
INVENTORY = ROOT / "docs/coverage-inventory.json"
CONTRACT = ROOT / "docs/interop-contract.json"
POLICY = ROOT / "docs/coverage-policy.json"
RUNTIME = ROOT / "docs/runtime-identities.json"
NATIVE_HOOKS = ROOT / "docs/native-hook-profiles.json"
PACKS = ("Spaced Out!", "Frosty", "Bionic", "Prehistoric", "Aquatic")
PLATFORMS = ("windows-x64", "linux-x64")
ABSENCE_ELIGIBLE = ("material", "gameplay-system", "prefab", "prefab-converter", "recipe")


def configurations() -> list[dict]:
    return [{"id": f"{platform}/dlc-{mask:02d}", "platform": platform,
             "dlc_mask": mask, "packs": [pack for bit, pack in enumerate(PACKS) if mask & (1 << bit)]}
            for platform in PLATFORMS for mask in range(32)]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def interface_entries(contract: dict, policy: dict) -> list[dict]:
    result: list[dict] = []

    def add(identity: str, kind: str, owner: str, adapter: str, acceptance: str, fact: dict) -> None:
        result.append({"id": identity, "kind": kind, "owner": owner, "adapter": adapter,
                       "acceptance": acceptance, "status": "unimplemented", "fact": fact})

    for name, value in sorted(contract["messageIds"].items()):
        add(f"message/{name}", "native-message", "hybrid-boundary", "NativeMessageRouter", "message-transaction",
            {"name": name, "value": value})
    for item in sorted(contract["staticAudit"]["nativeImports"], key=lambda item: item["method"]):
        add(f"import/{item['method']}", "native-import", "hybrid-boundary", "NativeServiceRouter", "native-service", item)
    for field in contract["layouts"]["Sim.GameDataUpdate"]["fields"]:
        if field["type"].endswith("*") or field["type"] == "IntPtr":
            add(f"snapshot/{field['name']}", "snapshot-channel", "owned-snapshot", "FrameSnapshotPublisher", "snapshot-lifetime",
                {key: field[key] for key in ("name", "type", "offset")})
    facts = (contract["staticAudit"]["nativeCalls"] + contract["importHookOrder"]["directCallerFacts"]
             + contract["importHookOrder"]["additionalFluidComponentFacts"])
    unique = {(item["method"], item["offset"], item["opcode"], item["target"]): item for item in facts}
    for key, item in sorted(unique.items()):
        # Labels in the historic audit lack overload tokens. Treat these as
        # observed candidates; runtime registry captures remain mandatory.
        identity = hashlib.sha256(json.dumps(key, separators=(",", ":")).encode()).hexdigest()[:20]
        add(f"callsite/{identity}", "managed-callsite", "custom-fluid", "GameplayTransactionAdapters", "callsite-transaction", item)
    for item in policy["systems"]:
        add(f"system/{item['id']}", "gameplay-system", item["owner"], item["adapter"], item["acceptance"],
            {"description": item["description"]})
    profiles = json.loads(NATIVE_HOOKS.read_text(encoding="utf-8"))
    from oni_hook_profiles import validate as validate_hooks
    validate_hooks(profiles)
    by_role = {}
    for profile in profiles["profiles"]:
        for hook in profile["hooks"]:
            by_role.setdefault(hook["role"], []).append({"platform": profile["platform"], **hook})
    for role, locations in sorted(by_role.items()):
        add(f"native-hook/{role}", "native-hook", "native-boundary", "NativeBackendHooks", "native-service",
            {"locations": locations,
             "requires_enclosing_phase_coverage": [p["platform"] for p in profiles["profiles"]
                                                  if not any(h["role"] == role for h in p["hooks"])],
             "scope": "Forwarding observer; replacement/reader-writer coverage remains required."})
    runtime = json.loads(RUNTIME.read_text(encoding="utf-8"))
    if runtime["schema_version"] != 1 or runtime["oni_build"] != contract["scope"]["oniBuild"] or runtime["coverage_complete"] is not False:
        raise ValueError("Invalid runtime discovery input")
    adapters = {"prefab": ("PrefabFluidAdapters", "callsite-transaction"), "prefab-converter": ("ElementConverterAdapter", "conversion"),
                "recipe": ("RecipeAdapters", "conversion")}
    for item in runtime["entries"]:
        adapter, acceptance = adapters[item["kind"]]
        add(item["id"], item["kind"], "custom-fluid/native-nonfluid", adapter, acceptance,
            {"runtime_identity": item["id"]})
    if len({item["id"] for item in result}) != len(result): raise ValueError("Duplicate discovered interface/runtime identity")
    return result


def generate(contract_path: Path, policy_path: Path, material_path: Path) -> dict:
    contract = json.loads(contract_path.read_text(encoding="utf-8"))
    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    captured = json.loads(material_path.read_text(encoding="utf-8"))
    if captured.get("OniBuild") != str(contract["scope"]["oniBuild"]):
        raise ValueError("Material identity capture and inspected game build differ")
    elements = captured["Elements"]
    if captured["ElementCount"] != len(elements):
        raise ValueError("Incomplete material identity capture")
    names, hashes, indices = set(), set(), set()
    materials = []
    for element in elements:
        name, native_hash, index, phase = (element[key] for key in ("Name", "Id", "Index", "Phase"))
        if (not isinstance(name, str) or not name or name in names or type(native_hash) is not int or native_hash in hashes
                or type(index) is not int or index in indices or phase not in ("solid", "liquid", "gas", "vacuum")):
            raise ValueError("Invalid or duplicate material identity")
        names.add(name); hashes.add(native_hash); indices.add(index)
        materials.append({"id": f"material/{name}", "kind": "material", "owner": "custom-fluid/native-nonfluid",
                          "adapter": "MaterialCatalog", "acceptance": "material-accounting", "status": "unimplemented",
                          "fact": {"name": name, "native_hash": native_hash, "phase": phase}})
    if indices != set(range(len(elements))):
        raise ValueError("Noncontiguous material identity indices")
    inventory = {
        "schema_version": 1, "oni_build": contract["scope"]["oniBuild"],
        "source_hashes": {"interop_contract": digest(contract_path), "coverage_policy": digest(policy_path),
                          "runtime_identities": digest(RUNTIME), "native_hook_profiles": digest(NATIVE_HOOKS),
                          "private_material_identities": digest(material_path)},
        "assembly_hashes": {"windows-x64": contract["scope"]["assemblySha256"],
                            "linux-x64": contract["linuxContractEvidence"]["assemblySha256"]},
        "scope": {
            "configurations": "Every entry must be verified in every configuration unless a runtime registry proves it absent.",
            "materials": "Identity inventory from one private Windows capture; properties and DLC applicability remain unverified.",
            "callsites": "Observed direct calls, not a complete dispatch graph; historic labels do not distinguish overloads.",
            "runtime": "Observed prefab, converter and recipe identities are indexed separately; no registry completeness proof or in-game ownership acceptance exists yet.",
            "cosmetics": "Separate compatibility evidence is required before release."
        },
        "configurations": configurations(),
        "entries": sorted(materials + interface_entries(contract, policy), key=lambda item: item["id"]),
        "registry_captures": [], "evidence": [], "release_evidence": []
    }
    inventory["material_identity_sha256"] = identity_digest([entry["fact"] for entry in materials])
    return inventory


def validate(inventory: dict, contract_path: Path = CONTRACT, policy_path: Path = POLICY) -> None:
    contract = json.loads(contract_path.read_text(encoding="utf-8")); policy = json.loads(policy_path.read_text(encoding="utf-8"))
    if inventory.get("schema_version") != 1 or inventory.get("oni_build") != contract["scope"]["oniBuild"]:
        raise ValueError("Unsupported inventory schema or game build")
    for key, path in (("interop_contract", contract_path), ("coverage_policy", policy_path), ("runtime_identities", RUNTIME),
                      ("native_hook_profiles", NATIVE_HOOKS)):
        if inventory["source_hashes"].get(key) != digest(path):
            raise ValueError(f"Stale inventory: {key} changed; regenerate and review")
    if inventory.get("assembly_hashes") != {"windows-x64": contract["scope"]["assemblySha256"],
                                            "linux-x64": contract["linuxContractEvidence"]["assemblySha256"]}:
        raise ValueError("Inventory assembly identities differ from the pinned contract")
    if inventory.get("configurations") != configurations():
        raise ValueError("All 32 DLC masks on both platforms must remain explicit")
    expected = {item["id"]: item for item in interface_entries(contract, policy)}
    observed = set()
    by_id = {}
    acceptances = policy["acceptance_tests"]
    materials = []
    for entry in inventory["entries"]:
        identity = entry["id"]
        if identity in observed: raise ValueError(f"Duplicate entry: {identity}")
        observed.add(identity)
        by_id[identity] = entry
        for key in ("owner", "adapter", "acceptance"):
            if not isinstance(entry.get(key), str) or not entry[key].strip():
                raise ValueError(f"Missing {key}: {identity}")
        if entry["acceptance"] not in acceptances: raise ValueError(f"Undefined acceptance test: {identity}")
        if entry.get("status") not in ("unimplemented", "implemented"):
            raise ValueError(f"Invalid implementation status: {identity}")
        if entry["status"] == "implemented":
            paths = entry.get("implementation", [])
            if not paths: raise ValueError(f"Implemented entry has no source references: {identity}")
            for path in paths:
                resolved = (ROOT / path).resolve()
                if not resolved.is_relative_to(ROOT) or not resolved.is_file():
                    raise ValueError(f"Invalid implementation source: {identity}")
        if identity in expected:
            for key in ("kind", "fact"):
                if entry[key] != expected[identity][key]: raise ValueError(f"Changed discovered interface: {identity}")
        elif identity.startswith("material/") and entry["kind"] == "material":
            fact = entry["fact"]
            if identity != "material/" + fact["name"] or fact["phase"] not in ("solid", "liquid", "gas", "vacuum"):
                raise ValueError(f"Invalid material identity: {identity}")
            materials.append(fact)
        else:
            raise ValueError(f"Entry is absent from discovery inputs: {identity}")
    missing = expected.keys() - observed
    if missing: raise ValueError(f"Missing discovered interfaces: {sorted(missing)}")
    if not materials or len({item["native_hash"] for item in materials}) != len(materials):
        raise ValueError("Missing or duplicate material identities")
    # Count and digest protect the identity list independently of the private
    # source, which deliberately is not a CI dependency.
    if inventory.get("material_identity_sha256") != identity_digest(materials):
        raise ValueError("Material identity list changed without an explicit regenerated capture")
    configs = {item["id"] for item in configurations()}
    for collection, keys in (("registry_captures", ("configuration",)), ("evidence", ("entry", "configuration")),
                             ("release_evidence", ("requirement", "configuration"))):
        seen = set()
        for item in inventory[collection]:
            key = tuple(item[field] for field in keys)
            if key in seen or item["configuration"] not in configs: raise ValueError(f"Duplicate or unknown evidence key: {key}")
            seen.add(key)
            if collection == "evidence" and item["entry"] not in observed: raise ValueError("Evidence refers to an unknown entry")
            if (collection == "evidence" and by_id[item["entry"]]["kind"] == "native-hook" and item.get("result") == "passed"
                    and (item.get("authoritative_fluid_owner") is not True or item.get("complete_reader_writer_coverage") is not True)):
                raise ValueError("Native-hook acceptance requires authoritative ownership and complete reader/writer coverage; forwarding observation is insufficient")
            if collection == "release_evidence" and item["requirement"] not in policy["release_requirements"]:
                raise ValueError("Unknown release requirement")
            sha = item.get("artifact_sha256", "")
            if (item.get("result") not in ("passed", "failed", "absent") or not isinstance(sha, str)
                    or len(sha) != 64 or any(char not in "0123456789abcdef" for char in sha)):
                raise ValueError("Evidence needs an explicit result and artifact hash")
            if item.get("kind") != "in-game" or not item.get("run_id"):
                raise ValueError("Standalone checks cannot substitute for an identified in-game run")
            platform = item["configuration"].split("/")[0]
            if item.get("assembly_sha256") != inventory["assembly_hashes"][platform]:
                raise ValueError("Evidence is for a different game binary")
            if collection == "registry_captures":
                if (not isinstance(item.get("entries"), list) or set(item["entries"]) - observed
                        or len(set(item["entries"])) != len(item["entries"])):
                    raise ValueError("Runtime registry has untriaged identities or lacks its entry list")
                if item["result"] == "passed" and item.get("unresolved") != []:
                    raise ValueError("A passing registry capture must explicitly have no unresolved identities")
            elif collection == "evidence":
                entry = by_id[item["entry"]]
                if item.get("acceptance") != entry["acceptance"]:
                    raise ValueError("Evidence does not match its acceptance test")
                if item["result"] == "absent" and entry["kind"] not in ABSENCE_ELIGIBLE:
                    raise ValueError("Declared native interfaces and observed call sites cannot be waived as absent")
            elif item["result"] == "absent":
                raise ValueError("Release requirements cannot be waived as absent")


def identity_digest(materials: list[dict]) -> str:
    data = json.dumps(sorted(materials, key=lambda item: item["name"]), sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(data.encode()).hexdigest()


def blockers(inventory: dict, policy: dict, release: bool = False) -> dict:
    captures = {item["configuration"]: item for item in inventory["registry_captures"] if item["result"] == "passed"}
    evidence = {(item["entry"], item["configuration"]): item for item in inventory["evidence"]}
    missing_registry, missing_acceptance, missing_adapters = [], 0, set()
    for config in inventory["configurations"]:
        cid = config["id"]; capture = captures.get(cid)
        if capture is None: missing_registry.append(cid)
        for entry in inventory["entries"]:
            proof = evidence.get((entry["id"], cid))
            # "Absent" requires a complete runtime capture which excludes the
            # identity. It is never inferred from a static DLC label.
            if (proof and proof["result"] == "absent" and capture and entry["id"] not in capture["entries"]
                    and entry["kind"] in ABSENCE_ELIGIBLE):
                continue
            if entry["status"] != "implemented": missing_adapters.add(entry["id"])
            if not proof or proof["result"] != "passed": missing_acceptance += 1
    release_missing = 0
    if release:
        passed = {(item["requirement"], item["configuration"]) for item in inventory["release_evidence"] if item["result"] == "passed"}
        release_missing = sum((req, config["id"]) not in passed for req in policy["release_requirements"] for config in inventory["configurations"])
    return {"gate_passed": not (missing_registry or missing_acceptance or missing_adapters or release_missing),
            "entries": len(inventory["entries"]), "by_kind": dict(sorted(Counter(item["kind"] for item in inventory["entries"]).items())),
            "missing_runtime_registry_configurations": len(missing_registry), "unimplemented_entries": len(missing_adapters),
            "missing_in_game_acceptances": missing_acceptance, "missing_release_acceptances": release_missing}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("generate", "validate", "status", "gate"))
    parser.add_argument("--inventory", type=Path, default=INVENTORY)
    parser.add_argument("--materials", type=Path, help="Private contract.json containing identities, never copied wholesale")
    parser.add_argument("--release", action="store_true")
    args = parser.parse_args()
    try:
        if args.action == "generate":
            if args.materials is None: parser.error("generate requires --materials")
            if args.inventory.exists(): parser.error("Generate into a new file; review before replacing an inventory with evidence")
            inventory = generate(CONTRACT, POLICY, args.materials)
            validate(inventory)
            args.inventory.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
        inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
        validate(inventory)
        report = blockers(inventory, json.loads(POLICY.read_text(encoding="utf-8")), args.release)
        print(json.dumps(report, indent=2))
        return 1 if args.action == "gate" and not report["gate_passed"] else 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Coverage inventory rejected: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
