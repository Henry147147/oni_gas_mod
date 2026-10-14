#!/usr/bin/env python3
"""Extract reviewable identities from a private in-game discovery report.

Numerical properties, rates, temperatures and native payloads are not copied.
This is discovery only; no gameplay test or completeness claim is generated.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def identities(capture: dict, source_hash: str, contract: dict) -> dict:
    platforms = {"WindowsPlayer": ("windows-x64", contract["scope"]["assemblySha256"]),
                 "LinuxPlayer": ("linux-x64", contract["linuxContractEvidence"]["assemblySha256"])}
    if capture.get("SchemaVersion") != 1 or capture.get("Kind") != "runtime-registry-discovery":
        raise ValueError("Unsupported runtime discovery schema")
    if capture.get("OniBuild") != str(contract["scope"]["oniBuild"]) or capture.get("Platform") not in platforms:
        raise ValueError("Unsupported runtime discovery build/platform")
    platform, assembly_hash = platforms[capture["Platform"]]
    if capture.get("AssemblySha256") != assembly_hash:
        raise ValueError("Discovery came from a different game assembly")
    if not capture.get("PrefabRegistryInitialized") or not capture.get("RecipeRegistryInitialized"):
        raise ValueError("Discovery ran before the runtime registries were initialized")
    if capture.get("CoverageComplete") is not False or capture.get("AuthoritativeColonySimulationReady") is not False:
        raise ValueError("A discovery report cannot certify coverage or ownership")
    entries = []
    seen = set()

    def add(identity: str, kind: str, fact: dict):
        if identity in seen: raise ValueError(f"Duplicate runtime identity: {identity}")
        seen.add(identity); entries.append({"id": identity, "kind": kind, "fact": fact})

    def label(value):
        if not isinstance(value, str) or not value: raise ValueError("Empty or invalid runtime identifier")
        return value

    prefab_occurrences, recipe_occurrences = Counter(), Counter()
    for prefab in capture["Prefabs"]:
        name = label(prefab["Id"])
        occurrence = prefab_occurrences[name]; prefab_occurrences[name] += 1
        prefab_id = f"prefab/{name}/{occurrence}"
        if type(prefab["MissingComponents"]) is not int or prefab["MissingComponents"] < 0:
            raise ValueError("Invalid missing-component count")
        add(prefab_id, "prefab", {"prefab": name, "registration_occurrence": occurrence,
                                        "components": [label(item) for item in prefab["Components"]],
                                        "missing_components": prefab["MissingComponents"]})
        for converter in prefab["Converters"]:
            ordinal = converter["ComponentOrdinal"]
            if type(ordinal) is not int or ordinal < 0: raise ValueError("Invalid converter ordinal")
            add(f"converter/{name}/{occurrence}/{ordinal}", "prefab-converter", {
                "prefab_identity": prefab_id, "component_ordinal": ordinal,
                "input_tags": [label(item["Tag"]) for item in converter["Inputs"]],
                "output_elements": [label(item["Element"]) for item in converter["Outputs"]]
            })
    for recipe in capture["Recipes"]:
        name = label(recipe["Id"])
        occurrence = recipe_occurrences[name]; recipe_occurrences[name] += 1
        short_id = hashlib.sha256(name.encode()).hexdigest()[:24]
        def materials(elements):
            return sorted({label(item) for element in elements
                           for item in ([element["Material"]] + element["PossibleMaterials"]) if item})
        add(f"recipe/{short_id}/{occurrence}", "recipe", {"recipe": name, "registration_occurrence": occurrence,
            "fabricators": sorted(label(item) for item in recipe["Fabricators"]),
            "input_tags": materials(recipe["Inputs"]), "output_tags": materials(recipe["Outputs"])})
    if not capture["Prefabs"] or not capture["Recipes"]:
        raise ValueError("Initialized discovery report contains an empty registry")
    return {"schema_version": 1, "oni_build": contract["scope"]["oniBuild"], "platform": platform,
            "assembly_sha256": assembly_hash, "private_capture_sha256": source_hash,
            "loaded_dlc_ids": sorted(label(item) for item in capture["LoadedDlcIds"]),
            "save_dlc_configuration_known": False, "coverage_complete": False,
            "unresolved": capture["Unresolved"] + ["Duplicate prefab/recipe labels are distinct registrations indexed by occurrence, not collapsed; their dispatch remains to be reviewed."],
            "entries": sorted(entries, key=lambda item: item["id"])}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists(): parser.error("Output must be new; preserve previous discovery evidence")
    try:
        source = args.capture.read_bytes()
        result = identities(json.loads(source), hashlib.sha256(source).hexdigest(),
                            json.loads((ROOT / "docs/interop-contract.json").read_text(encoding="utf-8")))
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"entries": len(result["entries"]), "coverage_complete": False}))
        return 0
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(f"Runtime discovery rejected: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
