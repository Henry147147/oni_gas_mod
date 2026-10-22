#!/usr/bin/env python3
"""Base-game playtest checklist over the coverage inventory (docs/playtest-plan.md).

`apply` marks every inventory entry in or out of the playtest from the
base-game Linux registry (docs/runtime-identities-linux-base.json), assigns
each in-scope entry to a gameplay system, and copies that system's status from
SYSTEMS below. `status` prints the checklist. Identities only; no game data.
"""
from __future__ import annotations

import argparse
import hashlib
from collections import Counter, defaultdict
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
INVENTORY = ROOT / "docs/coverage-inventory.json"
BASE = ROOT / "docs/runtime-identities-linux-base.json"

# Systems out of this playtest, with the reason recorded on their entries.
OUT_SYSTEMS = {
    "dlc-only": "only DLC content reaches this call site",
    "bionics": "Bionic Booster Pack content; packs follow the playtest",
    "radiation": "Spaced Out! radiation; not in the base game",
    "light": "the playtest keeps ONI's light model; fluid light absorption is a later physics item",
}

# Implemented systems: the adapter sources and the in-game check that shows
# the system working with closed ledgers. A system missing here is open.
SYSTEMS: dict[str, dict] = {
    "material-catalogue": {
        "implementation": ["native/src/oni_catalogue.cpp", "managed/MixedAtmospheres.Oni/ColonyOwner.cs"],
        "check": "colony-owner run: the owner started with every loaded gas and liquid as a species (docs/material-catalogue.md)"},
    "frame-publication": {
        "implementation": ["native/src/oni_colony.cpp", "managed/MixedAtmospheres.Oni/ColonyOwner.cs"],
        "check": "colony-owner run: owner frames published, mass/energy ledgers closed (docs/colony-owner.md#verification)"},
    "cluster-import": {
        "implementation": ["native/src/oni_colony_import.cpp", "managed/MixedAtmospheres.Oni/ColonyOwner.cs"],
        "check": "colony-owner run: Terra imported whole and settled (docs/colony-owner.md#import)"},
    "native": {
        "implementation": ["managed/MixedAtmospheres.Oni/ColonyOwner.cs", "native/src/oni_colony.cpp"],
        "check": "colony-owner run: no fluid adapter is needed; these read the published cells and exchange heat through the native shadow"},
    "solid-heat": {
        "implementation": ["native/src/oni_colony.cpp", "native/src/oni_backend.cpp"],
        "check": "colony-owner run: native heat contacts booked to the owner, ledgers closed (docs/colony-owner.md#native-bodies)"},
}
# Systems checked by in-game scenarios (docs/colony-owner.md#scenarios): the
# adapter sources and the scenarios that show each one working. A system is
# implemented once all of its scenarios are in PASSED_SCENARIOS (Linux harness).
OWNER = ["managed/MixedAtmospheres.Oni/ColonyOwner.cs", "native/src/oni_colony.cpp"]
ACTORS = ["managed/MixedAtmospheres.Oni/ColonyActors.cs", "native/src/oni_colony.cpp"]
SHADOW = ["native/src/oni_colony.cpp", "native/src/oni_backend.cpp"]
MACHINES = ["managed/MixedAtmospheres.Oni/ColonyMachines.cs", *ACTORS]
SCENARIO_SYSTEMS: dict[str, tuple[list[str], tuple[str, ...]]] = {
    "terrain-construction": (OWNER, ("terrain", "door", "airflow")),
    "heating-cooling": ([*SHADOW, *ACTORS[:1]], ("heat", "space-heater", "wheezewort")),
    "cell-transitions": (OWNER, ("phase", "freeze-melt")),
    "pumps": (ACTORS, ("pipes", "liquid-pump")),
    "conduits": (MACHINES, ("pipes", "liquid-pump", "vent-overpressure")),
    "fans-vents": (OWNER, ("pipes", "vent-overpressure")),
    "element-converters": (MACHINES, ("deoxidizer", "life-support")),
    "refineries": (MACHINES, ("polymer-press",)),
    "electrolysis": (MACHINES, ("electrolysis",)),
    "generators": (MACHINES, ("gas-generator",)),
    "emitters-consumers": (ACTORS, ("life-support", "wheezewort", "oxyfern")),
    "sublimation": (SHADOW, ("sublimation",)),
    "duplicants": (ACTORS, ("breathing",)),
    "storage": (OWNER, ("bottle-emptier",)),
    "suits-masks": (ACTORS, ("mask-station",)),
    "disease": ([*OWNER, *SHADOW[1:]], ("germs",)),
    "critters": (ACTORS, ("puft",)),
    "aquatic-ecosystems": (OWNER, ("pacu",)),
    "liquid-access": (OWNER, ("pacu",)),
    "plants": (ACTORS, ("plant", "oxyfern")),
    "pressure-damage": (OWNER, ("plant",)),
    "geysers": (ACTORS, ("geyser",)),
    "rockets": (OWNER, ("rocket-exhaust",)),
    "meteors": (OWNER, ("meteor",)),
    "sensors": (["managed/MixedAtmospheres.Oni/ColonySensors.cs"], ("sensor",)),
    "filters-separators": (MACHINES, ("filter",)),
    "chunks": (SHADOW, ("debris-heat",)),
    "building-melting": (OWNER, ("building-melt",)),
    "particles": (OWNER, ("falling", "bottle-emptier")),
}
# Passing in the Linux harness (Terra, seed 147147) on the current owner.
PASSED_SCENARIOS: set[str] = {
    "terrain", "door", "airflow", "falling", "heat", "phase", "freeze-melt", "deoxidizer", "sublimation", "breathing",
    "electrolysis", "vent-overpressure", "space-heater", "bottle-emptier", "germs", "puft", "plant", "wheezewort",
    "life-support", "oxyfern", "debris-heat", "building-melt", "rocket-exhaust", "meteor", "sensor", "pacu",
    "liquid-pump", "pipes", "mask-station", "polymer-press", "gas-generator", "geyser", "filter",
}
# Systems checked by whole-colony runs rather than one scenario.
RUN_SYSTEMS: dict[str, dict] = {
    "native-hook-coverage": {
        "implementation": ["native/src/oni_backend.cpp", "native/src/oni_colony.cpp"],
        "check": "every Linux colony-owner run: the backend hooks ONI's native frame, suppresses its fluid stages for owned cells and books "
                 "every native change to an owned cell through the shadow, with the ledgers closed (docs/colony-owner.md); Windows follows the port"},
    "element-interactions": {
        "implementation": ["native/src/oni_backend.cpp", "native/src/oni_colony.cpp"],
        "check": "the base game registers no native element interactions (no managed CreateElementInteractions call, docs/interop-contract.md); "
                 "the native gas-interaction stages are suppressed for owned cells and any native change to one is booked by the shadow"},
}
RUN_EVIDENCE: dict[str, dict] = {
    "backwalls": {
        "implementation": ["managed/MixedAtmospheres.Oni/ColonyOwner.cs"],
        "check": "the owner counts backwall cells at start: none on any base-game starting asteroid (the import runs); a backwall's "
                 "transition would emit through AddRemoveSubstance, which the owner takes"},
    "colony-save": {
        "implementation": ["managed/MixedAtmospheres.Oni/ColonyOwnerPersistence.cs", "native/src/oni_colony.cpp"],
        "check": "the prepared base saves through ONI's ordinary save, loads in a fresh process with its owner, overwrites in place, "
                 "and ONI's own loader refuses it (docs/colony-owner.md#saves)"},
}
PASSED_RUNS: set[str] = {"colony-save", "backwalls"}
SYSTEMS.update(RUN_SYSTEMS)
SYSTEMS.update({name: entry for name, entry in RUN_EVIDENCE.items() if name in PASSED_RUNS})
for _system, (_files, _scenarios) in SCENARIO_SYSTEMS.items():
    if set(_scenarios) <= PASSED_SCENARIOS:
        SYSTEMS[_system] = {"implementation": _files, "check": "in-game scenarios " + ", ".join(f"`{name}`" for name in _scenarios) +
                            " (docs/colony-owner.md#scenarios) pass with the owner's ledgers closed"}
FLUID_PHASES = ("gas", "liquid")

# Prefab component -> system, first match wins. Prefabs matching nothing
# touch fluids only through cell reads and native heat (system "native").
COMPONENT_SYSTEMS = [
    (("RocketEngine", "RocketEngineCluster", "LaunchableRocket", "RocketModule", "CommandModule", "FuelTank",
      "OxidizerTank", "CargoBay", "ConduitRocketTank"), "rockets"),
    (("Geyser", "GeyserConfigurator"), "geysers"),
    (("Electrolyzer",), "electrolysis"),
    (("Comet",), "meteors"),
    (("Pump", "LiquidPumpingStation", "MopTool"), "pumps"),
    (("Vent", "Fan"), "fans-vents"),
    (("ElementFilter", "AirFilter"), "filters-separators"),
    (("LogicElementSensor", "ConduitElementSensor", "LogicPressureSensor", "ConduitTemperatureSensor",
      "ConduitDiseaseSensor", "LogicTemperatureSensor", "ConduitThresholdSensor"), "sensors"),
    (("ConduitBridge", "ValveBase", "Valve", "OperationalValve", "ConduitConsumer", "ConduitDispenser",
      "Conduit", "ConduitOverflow", "ConduitPreferentialFlow"), "conduits"),
    (("Generator", "EnergyGenerator", "SteamTurbine"), "generators"),
    (("AirConditioner", "SpaceHeater", "LiquidHeater", "IceCooledFan", "ColdBreather", "MinimumOperatingTemperature"), "heating-cooling"),
    (("SuitTank", "SuitLocker", "SuitMarker", "JetSuitTank"), "suits-masks"),
    (("OxygenBreather", "MinionIdentity"), "duplicants"),
    (("CreatureBrain", "CreatureCalorieMonitor", "FishOvercrowdingMonitor"), "critters"),
    (("Crop", "ReceptacleMonitor", "PressureVulnerable", "PlantElementAbsorber", "Growing"), "plants"),
    (("Sublimates",), "sublimation"),
    (("BottleEmptier", "GasBottler", "Bottler", "StorageLocker"), "storage"),
    (("ElementConverter", "ElementConsumer", "PassiveElementConsumer", "ElementEmitter", "BuildingElementEmitter"), "element-converters"),
    (("Door", "SimCellOccupier", "Constructable", "Diggable"), "terrain-construction"),
]
# Native messages, imports and hooks by name.
NAMED_SYSTEMS = {
    "frame-publication": ("PrepareGameData", "SimFrameManager_NewGameFrame", "SetVisibleCells", "SetDebugProperties", "ToggleProfiler",
                          "Sim.SIM_HandleMessage", "Sim.SIM_HandleMessages", "pressure", "liquid", "gas-interaction", "gas-interaction-pair",
                          "gas-displacement", "liquid-displacement", "main", "message", "messages", "process-frame", "next-frame",
                          "update-data", "step", "begin-frame", "end-frame", "copy-to-game", "prepare-game-data-update"),
    "cluster-import": ("AllocateCells", "ClearUnoccupiedCells", "DefineWorldOffsets", "Elements_CreateInteractions", "Elements_CreateTable",
                       "SimData_FreeCells", "SimData_InitializeFromCells", "SimData_ResizeAndInitializeVacuumCells", "Start", "SetWorldZones",
                       "ModifyCellWorldZone", "Sim.SIM_Initialize", "Sim.SIM_Shutdown", "initialize", "shutdown", "Sim.SIM_DebugCrash",
                       "Sim.SYSINFO_Acquire", "Sim.SYSINFO_Release"),
    "terrain-construction": ("Dig", "ModifyCell", "ChangeCellProperties", "SetInsulationValue", "SetStrengthValue"),
    "solid-heat": ("ModifyCellEnergy", "AddBuildingHeatExchange", "AddBuildingToBuildingHeatExchange",
                   "AddInContactBuildingToBuildingToBuildingHeatExchange", "ModifyBuildingEnergy", "ModifyBuildingHeatExchange",
                   "ModifyChunkTemperatureAdjuster", "ModifyElementChunkEnergy", "RemoveBuildingHeatExchange",
                   "RemoveBuildingInContactFromBuildingToBuildingHeatExchange", "RemoveBuildingToBuildingHeatExchange",
                   "building-heat", "chunk-heat", "temperature"),
    "chunks": ("AddElementChunk", "RemoveElementChunk", "MoveElementChunk", "SetElementChunkData"),
    "emitters-consumers": ("AddElementConsumer", "AddElementEmitter", "ModifyElementEmitter", "RemoveElementConsumer", "RemoveElementEmitter",
                           "SetElementConsumerData", "MassConsumption", "MassEmission", "consumer", "emitter", "emitter-dispatch"),
    "disease": ("AddDiseaseConsumer", "AddDiseaseEmitter", "CellDiseaseModification", "ConsumeDisease", "Disease_CreateTable",
                "ModifyDiseaseConsumer", "ModifyDiseaseEmitter", "RemoveDiseaseConsumer", "RemoveDiseaseEmitter"),
    "colony-save": ("Load", "SetSavedOptions", "begin-save", "end-save", "Sim.SIM_BeginSave", "Sim.SIM_EndSave"),
    "conduits": tuple("ConduitTemperatureManager.ConduitTemperatureManager_" + name
                      for name in ("Add", "Clear", "Initialize", "Remove", "Set", "Shutdown", "Update")),
    "cell-transitions": ("state-transition", "load-transition", "post-process"),
    "sublimation": ("sublimation",),
    "backwalls": ("ModifyBackwallData",),
    "radiation": ("AddRadiationEmitter", "CellRadiationModification", "ModifyRadiationEmitter", "RadiationParamsModification",
                  "RadiationSickness", "RemoveRadiationEmitter"),
}
# SimMessages wrappers by method.
SIM_MESSAGE_SYSTEMS = {
    **{name: "solid-heat" for name in ("AddBuildingHeatExchange", "AddBuildingToBuildingHeatExchange", "ModifyBuildingEnergy",
       "ModifyBuildingHeatExchange", "ModifyElementChunkEnergy", "ModifyElementChunkTemperatureAdjuster", "ModifyEnergy",
       "RegisterBuildingToBuildingHeatExchange", "RemoveBuildingHeatExchange", "RemoveBuildingInContactFromBuildingToBuildingHeatExchange",
       "RemoveBuildingToBuildingHeatExchange")},
    **{name: "disease" for name in ("AddDiseaseEmitter", "ConsumeDisease", "CreateDiseaseTable", "ModifyDiseaseEmitter",
       "ModifyDiseaseOnCell", "RemoveDiseaseEmitter")},
    **{name: "chunks" for name in ("AddElementChunk", "MoveElementChunk", "RemoveElementChunk", "SetElementChunkData")},
    **{name: "emitters-consumers" for name in ("AddElementConsumer", "AddElementEmitter", "AddRemoveSubstance", "ConsumeMass", "EmitMass",
       "ModifyElementEmitter", "ModifyMass", "RemoveElementConsumer", "RemoveElementEmitter", "SetElementConsumerData")},
    **{name: "radiation" for name in ("AddRadiationEmitter", "ModifyRadiationEmitter", "ModifyRadiationOnCell", "ModifyRadiationParams",
       "RemoveRadiationEmitter")},
    **{name: "terrain-construction" for name in ("ClearCellProperties", "Dig", "ModifyCell", "ReplaceAndDisplaceElement", "ReplaceElement",
       "SetCellProperties", "SetInsulation", "SetStrength")},
    **{name: "cluster-import" for name in ("CreateElementInteractions", "CreateSimElementsTable", "DefineWorldOffsets", "ModifyCellWorldZone")},
    "NewGameFrame": "frame-publication", "SetDebugProperties": "frame-publication", "SetSavedOptionValue": "colony-save",
    "SetBackwallData": "backwalls",
}
# Managed call sites by declaring class. DLC-only classes are out.
DLC_CLASSES = {"BionicMassOxygenAbsorbChore", "ClustercraftInteriorDoorConfig", "GunkMonitor", "HighEnergyParticle", "PodLander",
               "Reactor", "RocketEngineCluster", "SelfChargingElectrobank", "Electrobank", "CargoLander", "IceKettle",
               "BeeForageStates", "SpaceTreeSeededComet"}
CLASS_SYSTEMS = {
    "duplicants": {"CO2Manager", "GasBreatherFromWorldProvider", "VomitChore", "FartChore", "Flatulence", "PeeChore", "UglyCryChore", "Stinky"},
    "disease": {"DecompositionMonitor", "SlimeSickness"},
    "geysers": {"GeothermalVent", "OilWellCap", "BreathingGeyser"},
    "heating-cooling": {"ColdBreather", "LiquidHeaterBubbleEmitter", "LiquidCooledFan"},
    "meteors": {"Comet", "LargeComet"},
    "terrain-construction": {"Door", "EntombedItemManager", "SandboxFloodTool", "SimCellOccupier", "UnstableGroundManager", "WorldDamage",
                             "SandboxBrushTool", "SandboxSprinkleTool", "TemplateLoader", "VerticalWindTunnel"},
    "sublimation": {"Sublimates"},
    "conduits": {"BottleEmptier", "ConduitConsumer", "ConduitFlow", "ConduitTemperatureManager", "EntityConduitConsumer",
                 "LiquidPumpingStation", "Moppable"},
    "critters": {"BaseSquidConfig", "CreatureCalorieMonitor", "CritterTrapPlant", "ElementDropperMonitor", "GasAndLiquidConsumerMonitor",
                 "SurfaceAirConsumerStates", "OilEater"},
    "element-converters": {"BuildingElementEmitter", "ElementConverter", "ElementEmitter", "PlantElementEmitter", "EntityElementExchanger"},
    "generators": {"EnergyGenerator", "SteamTurbine", "Turbine"},
    "refineries": {"Polymerizer"},
    "rockets": {"AutoRocketUtility", "LaunchPad", "RocketEngine", "Exhaust"},
    "particles": {"FallingWater", "BubbleManager"},
    "storage": {"Dumpable"},
    "chunks": {"SubstanceChunk"},
    "solid-heat": {"SimTemperatureTransfer", "StructureTemperatureComponents", "SandboxHeatTool"},
    "colony-save": {"SaveLoader"},
    "backwalls": {"BackwallManager"},
    "cluster-import": {"DebugHandler", "DebugTool", "GameUtil", "MakeBaseSolid", "Scenario", "Sim", "SimMessages", "SubworldZoneRenderData",
                       "TerrainCell", "WorldGenSimUtil"},
}


def system_of(entry: dict, base: dict, materials: dict) -> str:
    kind, fact = entry["kind"], entry["fact"]
    if kind == "gameplay-system": return entry["id"].split("/", 1)[1]
    if kind == "material":
        phase = materials.get(fact["name"])
        return "material-catalogue" if phase in FLUID_PHASES or phase == "vacuum" else "cell-transitions"
    if kind == "prefab-converter": return "element-converters"
    if kind == "recipe":
        tags = set(base[entry["id"]]["fact"]["input_tags"]) | set(base[entry["id"]]["fact"]["output_tags"])
        return "refineries" if any(materials.get(tag) in FLUID_PHASES for tag in tags) else "native"
    if kind == "prefab":
        components = {name.rsplit(".", 1)[-1] for name in base[entry["id"]]["fact"]["components"]}
        for names, system in COMPONENT_SYSTEMS:
            if components & set(names): return system
        return "native"
    if kind == "snapshot-channel": return "frame-publication"
    if kind == "managed-callsite":
        owner = fact["method"].split("+", 1)[0].split(".", 1)[0]
        if owner in DLC_CLASSES: return "dlc-only"
        method = fact["method"].rsplit(".", 1)[-1]
        if owner == "SimMessages": return SIM_MESSAGE_SYSTEMS[method]
        if owner == "Sim": return "colony-save" if method in ("Load", "LoadWorld", "Save") else "frame-publication"
        for system, owners in CLASS_SYSTEMS.items():
            if owner in owners: return system
        raise ValueError(f"No playtest system for call sites of {owner}")
    name = entry["id"].split("/", 1)[1] if kind in ("native-import", "native-hook") else fact.get("name") or entry["id"].split("/", 1)[1]
    for system, names in NAMED_SYSTEMS.items():
        if name in names: return system
    raise ValueError(f"No playtest system for {entry['id']}")


def apply(inventory: dict, base_identities: dict) -> None:
    base = {item["id"]: item for item in base_identities["entries"]}
    materials = {item["name"]: item["phase"] for item in base_identities.get("materials", [])}
    if not materials: raise ValueError("The base-game identities lack the loaded material list")
    present = {entry["id"] for entry in inventory["entries"]}
    adapters = {"prefab": ("PrefabFluidAdapters", "callsite-transaction"), "prefab-converter": ("ElementConverterAdapter", "conversion"),
                "recipe": ("RecipeAdapters", "conversion")}
    for item in base_identities["entries"]:
        if item["id"] in present: continue
        adapter, acceptance = adapters[item["kind"]]
        inventory["entries"].append({"id": item["id"], "kind": item["kind"], "owner": "custom-fluid/native-nonfluid", "adapter": adapter,
                                     "acceptance": acceptance, "status": "unimplemented", "fact": {"runtime_identity": item["id"]}})
    inventory["entries"].sort(key=lambda item: item["id"])
    for entry in inventory["entries"]:
        for key in ("playtest", "playtest_system", "playtest_reason", "implementation", "playtest_check"): entry.pop(key, None)
        kind = entry["kind"]
        reason = ""
        if kind in ("prefab", "prefab-converter", "recipe") and entry["id"] not in base:
            reason = "not in the base-game registry"
        elif kind == "material" and entry["fact"]["name"] not in materials:
            reason = "not a loaded base-game material"
        system = "" if reason else system_of(entry, base, materials)
        if system in OUT_SYSTEMS: reason = OUT_SYSTEMS[system]
        if reason:
            entry["playtest"] = "out"; entry["playtest_reason"] = reason; entry["status"] = "unimplemented"
            continue
        entry["playtest"] = "in"; entry["playtest_system"] = system
        done = SYSTEMS.get(system)
        entry["status"] = "implemented" if done else "unimplemented"
        if done: entry["implementation"] = done["implementation"]; entry["playtest_check"] = done["check"]


def status(inventory: dict) -> dict:
    scope = Counter(entry.get("playtest", "unmarked") for entry in inventory["entries"])
    systems: dict[str, Counter] = defaultdict(Counter)
    for entry in inventory["entries"]:
        if entry.get("playtest") == "in": systems[entry["playtest_system"]][entry["status"]] += 1
    open_systems = sorted(name for name, counts in systems.items() if counts["unimplemented"])
    return {"in": scope["in"], "out": scope["out"], "unmarked": scope["unmarked"],
            "implemented": sum(counts["implemented"] for counts in systems.values()),
            "systems": {name: dict(counts) for name, counts in sorted(systems.items())}, "open_systems": open_systems}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("apply", "status"))
    args = parser.parse_args()
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    if args.action == "apply":
        apply(inventory, json.loads(BASE.read_text(encoding="utf-8")))
        inventory["source_hashes"]["runtime_identities_linux_base"] = hashlib.sha256(BASE.read_bytes()).hexdigest()
        INVENTORY.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(status(inventory), indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
