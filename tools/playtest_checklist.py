#!/usr/bin/env python3
"""Playtest checklist over the coverage inventory (docs/playtest-plan.md).

`apply` marks every inventory entry in or out of the playtest, assigns each
in-scope entry to a gameplay system, and copies that system's status from
SYSTEMS below. The default `release` scope is the base game (the Linux registry
docs/runtime-identities-linux-base.json) plus Spaced Out! (the Windows registry
docs/runtime-identities.json, captured with EXPANSION1_ID loaded); `--scope
base` reproduces the earlier base-game-only checklist. Entries stay out only by
a recorded disposition (refused pack, ONI's light model kept, disabled element;
see OUT_DISPOSITIONS). `status` prints the checklist and `scenarios` the
in-game scenarios each system still needs. Identities only; no game data.
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
SPACED_OUT = ROOT / "docs/runtime-identities.json"
PRIVATE_STATUS = ROOT / "docs/private-playtest-status.json"
PRIVATE_REQUIREMENTS = frozenset((
    "mixed-pipes-and-ports", "heat-and-all-germ-identities", "conserved-gameplay-transactions",
    "exact-in-flight-saves", "whole-asteroid-reliability", "selected-profile-accuracy",
    "ordinary-player-build", "ten-cycle-acceptance-and-package",
    "configuration-and-presets", "windows-player-acceptance", "spaced-out-cluster-playtest"))

# Systems out of the base-game scope, with the reason recorded on their entries
# (`--scope base` only; the release scope implements radiation and the DLC
# classes below).
BASE_OUT_SYSTEMS = {
    "dlc-only": "only DLC content reaches this call site",
    "bionics": "Bionic Booster Pack content; packs follow the playtest",
    "radiation": "Spaced Out! radiation; not in the base game",
    "light": "the playtest keeps ONI's light model; fluid light absorption is a later physics item",
}
# Why an entry stays out of the release scope. Every out entry names exactly
# one of these (`playtest_disposition`) next to its reason. None of them is a
# claim of implementation: a refused pack leaves its colonies on ONI's own
# simulation (docs/dlc-compatibility.md), ONI's light model is kept by
# decision, and a disabled element never exists in a loaded game.
OUT_DISPOSITIONS = ("refused-pack", "kept-vanilla", "disabled-element")
REFUSED_PACKS = {
    "Frosty": "The Frosty Planet Pack (DLC2_ID)", "Bionic": "The Bionic Booster Pack (DLC3_ID)",
    "Prehistoric": "The Prehistoric Planet Pack (DLC4_ID)", "Aquatic": "The Aquatic Planet Pack (DLC5_ID)"}
REFUSAL = "refused pack: a colony with it active stays on ONI's own simulation and shows a notice"
LIGHT_REASON = ("kept vanilla by decision (2026-11-02): the release keeps ONI's own light model; ONI gases and liquids never "
                "absorb light, so vanilla behaviour is unchanged and there is no physics to implement")
DISABLED_REASON = ("disabled element (isDisabled in the game's element data): it cannot spawn in any loaded world, and the owner's "
                   "catalogue skips disabled elements by design (ColonyOwner.BuildCatalogue)")
# Elements whose game data sets isDisabled (identity list read once from the
# Windows copy's elements/*.yaml; re-derive when the game build changes).
DISABLED_ELEMENTS = frozenset((
    "Aerogel", "Brick", "CarbonFibre", "Cement", "CementMix", "Electrum", "Helium", "LiquidHelium", "MoltenSyngas",
    "PhosphateNodules", "Propane", "SandCement", "Slabs", "SolidSyngas", "Syngas", "Yellowcake"))
# Fluids that exist only with Spaced Out! (dlcId EXPANSION1_ID in the element data).
SPACED_OUT_FLUIDS = frozenset(("Fallout", "MoltenUranium", "NuclearWaste"))

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
    "element-converters": (MACHINES, ("deoxidizer", "life-support", "skimmer", "terrarium", "deodorizer")),
    "refineries": (MACHINES, ("polymer-press", "rust-deoxidizer")),
    "electrolysis": (MACHINES, ("electrolysis", "electrolyzer-blocked")),
    "generators": (MACHINES, ("gas-generator", "coal-generator")),
    "emitters-consumers": (ACTORS, ("life-support", "wheezewort", "oxyfern")),
    "sublimation": (SHADOW, ("sublimation",)),
    "duplicants": (ACTORS, ("breathing", "hazards", "germ-exposure")),
    "storage": (OWNER, ("bottle-emptier",)),
    "suits-masks": (ACTORS, ("mask-station", "suit")),
    "disease": ([*OWNER, *SHADOW[1:], "native/src/oni_world_biology.cpp", "managed/MixedAtmospheres.Oni/ColonyWorldGerms.cs"],
                ("germs", "germ-oracle", "germ-exposure")),
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
# Spaced Out! systems (release scope). Each lists the adapter sources and the
# in-game scenarios (managed/MixedAtmospheres.Oni/ColonyScenarios*.cs, recorded
# in docs/dlc-compatibility.md) that show it working; it flips to implemented
# when every one is in PASSED_SCENARIOS. Names without a scenario in the
# managed project yet are the scenarios still to be written (SPACED_OUT_TODO).
MANAGED = "managed/MixedAtmospheres.Oni/"
CLUSTER = [MANAGED + "ColonyClusterWorlds.cs", MANAGED + "ColonyOwner.cs", "native/src/oni_colony.cpp"]
RETIREMENT = [MANAGED + "ColonyClusterRetirement.cs", MANAGED + "ColonyClusterRetirementInventory.cs", MANAGED + "ColonyClusterDebris.cs",
              "native/src/oni_colony.cpp"]
PORTS = [MANAGED + "ColonyConduitsPorts.cs", MANAGED + "ColonyConduitsMachinePorts.cs"]
BOUNDARY = [MANAGED + "NativeMessageBoundary.cs", "native/src/oni_backend.cpp", "native/src/oni_colony.cpp"]
SPACED_OUT_SYSTEMS: dict[str, tuple[list[str], tuple[str, ...]]] = {
    # Entries with no fluid interaction of their own: cluster map entities, artifacts, props, previews, seeds, food, research,
    # logic, HEP logistics and solid-only hardware. They need the cluster's worlds owned and published.
    "spaced-out-native": (CLUSTER, ("spaced-out-worlds", "spaced-out-vacuum-pocket")),
    # Fallout, NuclearWaste and MoltenUranium join the owner's catalogue like every enabled game gas and liquid.
    "spaced-out-materials": ([*CLUSTER, "native/src/oni_catalogue.cpp"], ("spaced-out-worlds",)),
    "rocket-modules": ([*CLUSTER, *RETIREMENT], ("spaced-out-rocket", "spaced-out-input-flush", "spaced-out-rocket-retire",
                                                 "spaced-out-rocket-flight", "spaced-out-rocket-abandon")),
    "rocket-ports": (PORTS, ("spaced-out-rocket-ports",)),
    "rocket-cargo": ([*PORTS, MANAGED + "ColonyOwnedStorage.cs"], ("spaced-out-cargo-gas", "spaced-out-cargo-liquid")),
    "warp-ports": (PORTS, ("spaced-out-warp-ports",)),
    "rocket-engines": (BOUNDARY, ("spaced-out-rocket-flight", "spaced-out-engine-exhaust")),
    "rocket-landers": (BOUNDARY, ("spaced-out-landers",)),
    "poi-harvest": ([MANAGED + "ColonyOwnedStorage.cs", MANAGED + "ColonyConduitsPumps.cs"], ("spaced-out-poi-harvest",)),
    "rail-gun": (PORTS, ("spaced-out-rail-gun",)),
    "radiation": ([*BOUNDARY, "native/src/oni_world_biology.cpp"], ("spaced-out-radiation",)),
    "hep-particles": (BOUNDARY, ("spaced-out-hep",)),
    "reactor": ([*MACHINES, *BOUNDARY], ("spaced-out-reactor", "spaced-out-reactor-meltdown")),
    "uranium-centrifuge": (MACHINES, ("spaced-out-centrifuge",)),
    "sludge-press": (MACHINES, ("spaced-out-sludge-press",)),
    "sublimation-station": (MACHINES, ("spaced-out-sublimation-station",)),
    "spaced-out-critters": (ACTORS, ("spaced-out-critters",)),
    "spaced-out-plants": (ACTORS, ("spaced-out-plants",)),
}
# Passing in the Linux harness (Terra, seed 147147) on the current owner. `phase` fails under
# the profile-10 default (2026-11-02) and stays out until it passes again.
PASSED_SCENARIOS: set[str] = {
    "terrain", "door", "airflow", "falling", "heat", "freeze-melt", "deoxidizer", "sublimation", "breathing",
    "electrolysis", "vent-overpressure", "space-heater", "bottle-emptier", "germs", "puft", "plant", "wheezewort",
    "life-support", "oxyfern", "debris-heat", "building-melt", "rocket-exhaust", "meteor", "sensor", "pacu",
    "liquid-pump", "pipes", "mask-station", "polymer-press", "gas-generator", "geyser", "filter",
    "skimmer", "terrarium", "deodorizer", "rust-deoxidizer", "coal-generator", "electrolyzer-blocked", "hazards", "suit",
    "germ-exposure", "germ-oracle",
    # Spaced Out!: WindowsPlayer under Proton on the recorded revisions, source plus cold/menu loads
    # (docs/dlc-compatibility.md; docs/spaced-out-private-playtest.md; docs/release-candidate-2026-10-30.md).
    "spaced-out-worlds", "spaced-out-rocket", "spaced-out-rocket-ports", "spaced-out-input-flush", "spaced-out-rocket-retire",
    "spaced-out-warp-ports", "spaced-out-rocket-flight", "spaced-out-cargo-gas", "spaced-out-cargo-liquid", "spaced-out-rocket-abandon",
    # Spaced Out! fluid systems, private Steam install, 2026-11-02 (docs/dlc-compatibility.md#spaced-out-fluid-systems).
    "spaced-out-radiation", "spaced-out-hep", "spaced-out-reactor", "spaced-out-reactor-meltdown", "spaced-out-landers",
    "spaced-out-poi-harvest", "spaced-out-comets-geysers", "spaced-out-engine-exhaust", "spaced-out-rail-gun",
    "spaced-out-sublimation-station", "spaced-out-intakes", "spaced-out-centrifuge", "spaced-out-sludge-press", "spaced-out-dehydrator",
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
SCENARIO_SYSTEMS.update(SPACED_OUT_SYSTEMS)


SYSTEM_NOTES = {
    "spaced-out-native": "; these entries need no fluid adapter: they read the published cells of the owned worlds",
    "spaced-out-materials": "; the owner's catalogue is built from every enabled game gas and liquid at start (docs/material-catalogue.md)",
}


def scenario_systems(passed: set[str]) -> dict[str, dict]:
    """The scenario-checked systems whose scenarios all pass: sources and check, by system."""
    done = {}
    for system, (files, scenarios) in SCENARIO_SYSTEMS.items():
        if set(scenarios) <= passed:
            record = "docs/dlc-compatibility.md" if system in SPACED_OUT_SYSTEMS else "docs/colony-owner.md#scenarios"
            done[system] = {"implementation": files, "check": "in-game scenarios " + ", ".join(f"`{name}`" for name in scenarios) +
                            f" ({record}) pass with the owner's ledgers closed" + SYSTEM_NOTES.get(system, "")}
    return done


SYSTEMS.update(scenario_systems(PASSED_SCENARIOS))
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
# Managed call sites by declaring class whose content needs a DLC. With the base
# scope they stay out as one group; the release scope names each class below.
DLC_CLASSES = {"BionicMassOxygenAbsorbChore", "ClustercraftInteriorDoorConfig", "GunkMonitor", "HighEnergyParticle", "PodLander",
               "Reactor", "RocketEngineCluster", "SelfChargingElectrobank", "Electrobank", "CargoLander", "IceKettle",
               "BeeForageStates", "SpaceTreeSeededComet"}
# Release scope: the Spaced Out! classes (EXPANSION1_ID in the prefab configs; the same SimMessages wrappers the owner already
# takes for every caller, each needing its own in-game evidence) and the classes that only a refused pack reaches (the
# prefab configs require DLC2_ID, DLC3_ID or DLC5_ID, and none is in either runtime registry).
SPACED_OUT_CLASSES = {
    "reactor": {"Reactor"},
    "rocket-modules": {"ClustercraftInteriorDoorConfig"},
    "rocket-landers": {"PodLander", "CargoLander"},
    "rocket-engines": {"RocketEngineCluster"},
    "hep-particles": {"HighEnergyParticle"},
    "spaced-out-critters": {"BeeForageStates"},
    "spaced-out-plants": {"CritterTrapPlant"},
}
REFUSED_CLASSES = {
    "Bionic": {"BionicMassOxygenAbsorbChore", "GunkMonitor", "Electrobank", "SelfChargingElectrobank"},
    "Frosty": {"IceKettle", "SpaceTreeSeededComet", "GeothermalVent"},
    "Aquatic": {"BaseSquidConfig", "BreathingGeyser"},
}
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


# Release scope: umbrella gameplay systems whose description reaches Spaced Out! parts also wait for those systems.
UMBRELLA_REQUIRES = {
    "rockets": ("rocket-modules", "rocket-ports", "rocket-cargo", "warp-ports", "rocket-engines", "rocket-landers", "poi-harvest", "rail-gun"),
    "generators": ("reactor",),
    "plants": ("spaced-out-plants",),
    "critters": ("spaced-out-critters",),
    "refineries": ("sludge-press", "uranium-centrifuge", "sublimation-station"),
    "cluster-import": ("spaced-out-native", "spaced-out-materials"),
}
# Spaced Out! prefab components that decide a system, first match wins (release scope, prefabs the base game lacks).
HEP_BUILDINGS = {"HighEnergyParticle", "HighEnergyParticleSpawner", "ManualHighEnergyParticleSpawner", "DevHEPSpawner"}
ROCKET_MODULE_COMPONENTS = {"RocketModuleCluster", "ReorderableBuilding", "Clustercraft", "PassengerRocketModule", "ClustercraftExteriorDoor",
                            "ClustercraftInteriorDoor", "NavTeleporter", "LaunchableRocketCluster", "CraftModuleInterface"}
RECIPE_FABRICATOR_SYSTEMS = {"SludgePress": "sludge-press", "UraniumCentrifuge": "uranium-centrifuge",
                             "ManualHighEnergyParticleSpawner": "hep-particles"}
SOLID_CONDUITS = {"SolidConduitConsumer", "SolidConduitDispenser"}


def spaced_out_prefab_system(name: str, components: set[str]) -> str | None:
    """The system of a Spaced Out! prefab by its real components, or None when only the base rules apply."""
    c = components
    if "BuildingUnderConstruction" in c: return "terrain-construction"
    if c & {"BuildingPreview", "EntityPreview"}: return "spaced-out-native"
    if name == "SublimationStation": return "sublimation-station"
    if name == "SludgePress": return "sludge-press"
    if "UraniumCentrifuge" in c: return "uranium-centrifuge"
    if "Reactor" in c: return "reactor"
    if c & HEP_BUILDINGS: return "hep-particles"
    if c & {"Bee", "Staterpillar", "StaterpillarGenerator"} or name.startswith(("BeeHive", "StaterpillarGas", "StaterpillarLiquid")):
        return "spaced-out-critters"
    if c & {"RailGun", "RailGunPayloadOpener"} or name == "RailGunPayload": return "rail-gun"
    if c & {"WarpConduitSender", "WarpConduitReceiver"}: return "warp-ports"
    if c & {"HarvestablePOIClusterGridEntity", "HarvestablePOIConfigurator", "StarmapHexCellInventory"} or name == "NoseconeHarvest":
        return "poi-harvest"
    if c & {"PodLander", "CargoLander"} or name in ("LandingPod", "PioneerLander", "ScoutLander"): return "rocket-landers"
    if "PassengerRocketModule" in c: return "rocket-modules"  # habitats; their cabin ports are required too (requirements)
    if c & {"RocketConduitSender", "RocketConduitReceiver", "RocketConduitStorageAccess"}:
        return "spaced-out-native" if c & SOLID_CONDUITS else "rocket-ports"
    if name.startswith("ModularLaunchpadPort"):
        return "rocket-cargo" if name.startswith(("ModularLaunchpadPortGas", "ModularLaunchpadPortLiquid")) else "spaced-out-native"
    if c & {"CargoBayCluster", "SpecialCargoBayClusterReceptacle", "ArtifactModule", "LaunchPad"} or name == "OrbitalCargoModule":
        return "rocket-cargo"
    if c & {"RocketEngineCluster", "FuelTank", "OxidizerTank"}: return "rocket-engines"
    if c & {"RadiationLight", "LogicRadiationSensor", "RadiationEmitter"}: return "radiation"
    if "SubstanceChunk" in c: return "chunks"  # a dropped fluid chunk (Fallout, molten uranium, liquid waste): the chunk path
    if c & {"CritterTrapPlant"} or {"ElementConsumer", "PressureVulnerable"} <= c: return "spaced-out-plants"
    if name in ("DebrisPayload", "RocketWallTile") or c & ROCKET_MODULE_COMPONENTS: return "rocket-modules"
    return None


def prefab_system(name: str, components: set[str], from_base: bool, release: bool) -> str:
    if release and not from_base:
        system = spaced_out_prefab_system(name, components)
        if system: return system
    for names, system in COMPONENT_SYSTEMS:
        if components & set(names): return system
    return "spaced-out-native" if release and not from_base else "native"


def refused(pack: str) -> str:
    return f"out:refused-pack:{pack}"


def system_of(entry: dict, base: dict, materials: dict, release: dict | None = None) -> str:
    """The playtest system of an entry. `release` (id -> Spaced Out! registry entry) selects the release scope;
    a result starting `out:` is a disposition, not a system."""
    kind, fact = entry["kind"], entry["fact"]
    full = {**(release or {}), **base}
    if kind == "gameplay-system":
        name = entry["id"].split("/", 1)[1]
        if release is None: return name
        return {"bionics": refused("Bionic"), "light": "out:kept-vanilla"}.get(name, name)
    if kind == "material":
        name = fact["name"]
        if release is not None:
            if name in DISABLED_ELEMENTS: return "out:disabled-element"
            if name in SPACED_OUT_FLUIDS: return "spaced-out-materials"
        phase = materials.get(name)
        return "material-catalogue" if phase in FLUID_PHASES or phase == "vacuum" else "cell-transitions"
    if kind == "prefab-converter":
        if release is None or entry["id"] in base: return "element-converters"
        identity = full[entry["id"]]["fact"]["prefab_identity"]
        owner = full[identity]["fact"]
        system = prefab_system(owner["prefab"], {n.rsplit(".", 1)[-1] for n in owner["components"]}, identity in base, True)
        return "element-converters" if system in ("spaced-out-native", "native", "terrain-construction") else system
    if kind == "recipe":
        recipe = full[entry["id"]]["fact"]
        if release is not None:
            for fabricator in recipe.get("fabricators", []):
                if fabricator in RECIPE_FABRICATOR_SYSTEMS and entry["id"] not in base: return RECIPE_FABRICATOR_SYSTEMS[fabricator]
        tags = set(recipe["input_tags"]) | set(recipe["output_tags"])
        if any(materials.get(tag) in FLUID_PHASES for tag in tags): return "refineries"
        return "spaced-out-native" if release is not None and entry["id"] not in base else "native"
    if kind == "prefab":
        identity = full[entry["id"]]["fact"]
        in_base = entry["id"] in base
        names = {n.rsplit(".", 1)[-1] for n in (base[entry["id"]] if in_base else full[entry["id"]])["fact"]["components"]}
        return prefab_system(identity["prefab"], names, in_base, release is not None)
    if kind == "snapshot-channel": return "frame-publication"
    if kind == "managed-callsite":
        owner = fact["method"].split("+", 1)[0].split(".", 1)[0]
        if release is not None:
            for pack, classes in REFUSED_CLASSES.items():
                if owner in classes: return refused(pack)
            for system, classes in SPACED_OUT_CLASSES.items():
                if owner in classes: return system
        elif owner in DLC_CLASSES: return "dlc-only"
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


def requirements(entry: dict, system: str, base: dict, release: dict | None) -> list[str]:
    """Further systems the entry also needs, in release scope: the Spaced Out! parts of an umbrella system, and radiation for a
    prefab that Spaced Out! gives a radiation emitter (the base registry's copy has none)."""
    if release is None: return []
    extra = list(UMBRELLA_REQUIRES.get(system, ())) if entry["kind"] == "gameplay-system" else []
    if entry["kind"] == "prefab":
        components = {n.rsplit(".", 1)[-1] for n in (release.get(entry["id"]) or base[entry["id"]])["fact"]["components"]}
        if "RadiationEmitter" in components: extra.append("radiation")
        if "RocketConduitSender" in components and not components & SOLID_CONDUITS: extra.append("rocket-ports")
    return [name for name in extra if name != system]


def apply(inventory: dict, base_identities: dict, spaced_out_identities: dict | None = None) -> None:
    """Mark every entry. With `spaced_out_identities` this is the release scope (base game plus Spaced Out!)."""
    release = None if spaced_out_identities is None else {item["id"]: item for item in spaced_out_identities["entries"]}
    base = {item["id"]: item for item in base_identities["entries"]}
    materials = {item["name"]: item["phase"] for item in base_identities.get("materials", [])}
    if not materials: raise ValueError("The base-game identities lack the loaded material list")
    if release is not None:
        materials.update({entry["fact"]["name"]: entry["fact"]["phase"] for entry in inventory["entries"] if entry["kind"] == "material"})
    present = {entry["id"] for entry in inventory["entries"]}
    adapters = {"prefab": ("PrefabFluidAdapters", "callsite-transaction"), "prefab-converter": ("ElementConverterAdapter", "conversion"),
                "recipe": ("RecipeAdapters", "conversion")}
    for item in base_identities["entries"]:
        if item["id"] in present: continue
        adapter, acceptance = adapters[item["kind"]]
        inventory["entries"].append({"id": item["id"], "kind": item["kind"], "owner": "custom-fluid/native-nonfluid", "adapter": adapter,
                                     "acceptance": acceptance, "status": "unimplemented", "fact": {"runtime_identity": item["id"]}})
    inventory["entries"].sort(key=lambda item: item["id"])
    inventory["playtest_scope"] = "base" if release is None else "release"
    for entry in inventory["entries"]:
        for key in ("playtest", "playtest_system", "playtest_reason", "playtest_disposition", "playtest_pack", "playtest_requires",
                    "implementation", "playtest_check"): entry.pop(key, None)
        kind = entry["kind"]
        reason = ""
        if release is None:
            if kind in ("prefab", "prefab-converter", "recipe") and entry["id"] not in base:
                reason = "not in the base-game registry"
            elif kind == "material" and entry["fact"]["name"] not in materials:
                reason = "not a loaded base-game material"
        elif kind in ("prefab", "prefab-converter", "recipe") and entry["id"] not in base and entry["id"] not in release:
            raise ValueError(f"{entry['id']} is in neither runtime registry")
        system = "" if reason else system_of(entry, base, materials, release)
        if system.startswith("out:"):
            _, disposition, *pack = system.split(":")
            entry["playtest"] = "out"; entry["playtest_disposition"] = disposition; entry["status"] = "unimplemented"
            if disposition == "refused-pack":
                entry["playtest_pack"] = pack[0]
                entry["playtest_reason"] = f"{REFUSAL} ({REFUSED_PACKS[pack[0]]}; not installed here)"
            else:
                entry["playtest_reason"] = LIGHT_REASON if disposition == "kept-vanilla" else DISABLED_REASON
            continue
        if release is None and system in BASE_OUT_SYSTEMS: reason = BASE_OUT_SYSTEMS[system]
        if reason:
            entry["playtest"] = "out"; entry["playtest_reason"] = reason; entry["status"] = "unimplemented"
            continue
        entry["playtest"] = "in"; entry["playtest_system"] = system
        needs = requirements(entry, system, base, release)
        if needs: entry["playtest_requires"] = needs
        every = [system, *needs]
        entry["status"] = "implemented" if all(name in SYSTEMS for name in every) else "unimplemented"
        if entry["status"] == "implemented":
            entry["implementation"] = list(dict.fromkeys(path for name in every for path in SYSTEMS[name]["implementation"]))
            entry["playtest_check"] = SYSTEMS[system]["check"] + ("; also " + ", ".join(needs) if needs else "")


def scenario_requirements() -> dict[str, dict]:
    """Per scenario-checked system: its scenarios, those passing, and those without a case in the managed project yet."""
    source = "\n".join(path.read_text(encoding="utf-8") for path in (ROOT / "managed/MixedAtmospheres.Oni").glob("ColonyScenarios*.cs"))
    return {system: {"scenarios": list(names), "passing": [name for name in names if name in PASSED_SCENARIOS],
                     "missing": [name for name in names if name not in PASSED_SCENARIOS],
                     "written": all(f'case "{name}"' in source for name in names if name not in PASSED_SCENARIOS)}
            for system, (_files, names) in SCENARIO_SYSTEMS.items()}


def status(inventory: dict) -> dict:
    scope = Counter(entry.get("playtest", "unmarked") for entry in inventory["entries"])
    systems: dict[str, Counter] = defaultdict(Counter)
    for entry in inventory["entries"]:
        if entry.get("playtest") != "in": continue
        systems[entry["playtest_system"]][entry["status"]] += 1
        # A system an entry also needs is counted open for it when the entry is open and that system is not done.
        for name in entry.get("playtest_requires", []):
            systems[name]["implemented" if entry["status"] == "implemented" or name in SYSTEMS else "unimplemented"] += 1
    for entry in inventory["entries"]:
        # Open only for the system that is not done: an entry waiting on a required system leaves its own system closed.
        if entry.get("playtest") == "in" and entry.get("playtest_requires") and entry["status"] != "implemented" and entry["playtest_system"] in SYSTEMS:
            systems[entry["playtest_system"]]["unimplemented"] -= 1; systems[entry["playtest_system"]]["implemented"] += 1
    open_systems = sorted(name for name, counts in systems.items() if counts["unimplemented"] > 0)
    out = Counter(entry.get("playtest_disposition", "base-scope") for entry in inventory["entries"] if entry.get("playtest") == "out")
    return {"scope": inventory.get("playtest_scope", "base"), "in": scope["in"], "out": scope["out"], "unmarked": scope["unmarked"],
            "implemented": sum(entry["status"] == "implemented" for entry in inventory["entries"] if entry.get("playtest") == "in"),
            "out_by_disposition": dict(sorted(out.items())),
            "systems": {name: {key: value for key, value in counts.items() if value} for name, counts in sorted(systems.items())},
            "open_systems": open_systems}


def readiness(inventory: dict, acceptance: dict) -> dict:
    """Implementation coverage alone cannot certify a playable package."""
    if acceptance.get("scope") != "linux-windows-spaced-out-private-playtest":
        raise ValueError("Incorrect private-playtest acceptance scope")
    rows = acceptance.get("requirements", [])
    ids = [row["id"] for row in rows]
    if len(ids) != len(set(ids)) or set(ids) != PRIVATE_REQUIREMENTS:
        raise ValueError("Private-playtest requirements are missing, duplicated or unknown")
    for row in rows:
        if row.get("status") not in ("open", "passed", "deferred"):
            raise ValueError("Unknown private-playtest acceptance status")
        # Only a recorded user decision defers an item past the first playtest.
        documents = [row.get("evidence", "")] + ([row.get("decision", "")] if row["status"] == "deferred" else [])
        for document in documents:
            if not document.startswith("docs/") or ".." in Path(document).parts or not (ROOT / document).is_file():
                raise ValueError("Private-playtest requirement needs repository evidence")
    report = status(inventory)
    blockers = [row["id"] for row in rows if row["status"] == "open"]
    report.update(private_playtest_ready=not (blockers or report["open_systems"] or report["unmarked"]),
                  open_acceptances=blockers,
                  deferred_acceptances=[row["id"] for row in rows if row["status"] == "deferred"],
                  acceptance_record="docs/private-playtest-release.md")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("apply", "status", "gate", "scenarios"))
    parser.add_argument("--scope", choices=("release", "base"), default="release",
                        help="apply: release is the base game plus Spaced Out! (default); base is the base game alone")
    args = parser.parse_args()
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    if args.action == "scenarios":
        print(json.dumps({name: item for name, item in scenario_requirements().items() if item["missing"]}, indent=1))
        return 0
    if args.action == "apply":
        apply(inventory, json.loads(BASE.read_text(encoding="utf-8")),
              json.loads(SPACED_OUT.read_text(encoding="utf-8")) if args.scope == "release" else None)
        inventory["source_hashes"]["runtime_identities_linux_base"] = hashlib.sha256(BASE.read_bytes()).hexdigest()
        INVENTORY.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
    report = readiness(inventory, json.loads(PRIVATE_STATUS.read_text(encoding="utf-8")))
    print(json.dumps(report, indent=1))
    return 1 if args.action == "gate" and not report["private_playtest_ready"] else 0


if __name__ == "__main__":
    sys.exit(main())
