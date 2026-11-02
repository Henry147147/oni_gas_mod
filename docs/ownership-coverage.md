# Ownership coverage and cluster staging

Milestone A of the [completion plan](completion-plan.md) is **in progress**; the
release gate is closed. The production mechanism is
[native SimDLL hooks](native-backend-integration.md). Hook acceptance must audit
readers/writers and trace actual execution; projected scalar fluids are allowed,
and observer attachment cannot satisfy ownership gates. The staging and filter
code described below comes from the earlier vacuum-shadow work and is not an
enabled production importer.

## Playtest checklist

The release scope of the [playtest](playtest-plan.md) is the base game plus
Spaced Out! (completion plan amendments; the first release covers both).
`tools/playtest_checklist.py apply` marks every entry in or out from two
runtime registries: `runtime-identities-linux-base.json` (the base game, captured
in game with no DLC) and `runtime-identities.json` (the Windows capture with
`EXPANSION1_ID` loaded). It assigns each in-scope entry to one gameplay system
by its real components, copies the system's status from the script's table of
implemented systems with their sources and in-game check, and lists any further
system the entry needs (`playtest_requires`). `apply --scope base` reproduces the
earlier base-game-only checklist. `status` prints implementation coverage plus
the separate [private-playtest acceptance blockers](private-playtest-release.md);
`scenarios` prints the in-game scenarios each system still needs. `gate` fails
until both are complete. Broad system checks do not certify every recipe, the
final mixed-pipe path or a releasable package. Entries without fluid interaction
(`native`, `spaced-out-native`) need no adapter.

Only three recorded dispositions keep an entry out of the release scope, and
none of them is a claim of implementation (`playtest_disposition`, validated by
`coverage_inventory.py validate`; their status stays `unimplemented`):

- **`refused-pack`** (12 entries, `playtest_pack` names the pack): content of
  the Frosty, Bionic, Prehistoric and Aquatic packs, which are not installed
  here and which the mod refuses. A colony with such a pack active stays on
  ONI's own simulation and shows a notice (`GameplayPacks.ActiveUnsupported`,
  checked at colony start, owner restore and save load; the pure decisions are
  covered by `tools/DistributionTests` and the checklist tests assert both
  guards). Entries: the Bionic gameplay system, the call sites of
  `BionicMassOxygenAbsorbChore`, `GunkMonitor`, `Electrobank` and
  `SelfChargingElectrobank` (Bionic); `IceKettle`, `SpaceTreeSeededComet` and
  `GeothermalVent` (Frosty); `BaseSquidConfig` and `BreathingGeyser` (Aquatic).
  The last three were counted under base systems before; their prefab configs
  require `DLC2_ID`/`DLC5_ID` and appear in neither registry.
- **`kept-vanilla`** (the light system): decision of 2026-11-02, the release
  keeps ONI's own light model. In ONI gases and liquids never absorb light, so
  vanilla behaviour is unchanged and there is no physics to implement.
- **`disabled-element`** (16 materials: Aerogel, Brick, CarbonFibre, Cement,
  CementMix, Electrum, Helium, LiquidHelium, MoltenSyngas, PhosphateNodules,
  Propane, SandCement, Slabs, SolidSyngas, Syngas, Yellowcake): their element
  data sets `isDisabled`, so they never spawn in a loaded world, and the owner's
  catalogue skips disabled elements by design (`ColonyOwner.BuildCatalogue`).

### Spaced Out! systems

The owner takes `SimMessages` wrappers (`AddRemoveSubstance`, `EmitMass`,
`ReplaceAndDisplaceElement`, ...) and `Storage.AddElement`/`AddGasChunk`/
`AddLiquid` for every caller, so Spaced Out! callers reach the same adapters as
base-game ones; each system below still needs its own in-game evidence before it
counts. Messages the owner does not handle (the radiation messages) pass to ONI's
native simulation, which reads the shadow of the owner's cells. A system is
implemented when all its scenarios are in `PASSED_SCENARIOS`; scenarios marked
(passing) are recorded results in [DLC compatibility](dlc-compatibility.md)
(WindowsPlayer under Proton). Entries by system are in the inventory.

| System | Spaced Out! content and its fluid contact | In-game scenarios |
| --- | --- | --- |
| `spaced-out-native` | Cluster-map entities, artifacts and props, food, seeds, eggs, pills, research and logic buildings, HEP logistics (ports, storage, redirectors, batteries), duplicant teleporters, solid-only launch-pad and rocket conduits, previews, element chunks. No fluid contact beyond cell reads and native heat | `spaced-out-worlds` (passing) |
| `spaced-out-materials` | Fallout (gas), NuclearWaste and MoltenUranium (liquids): enabled game fluids joining the owner's catalogue | `spaced-out-worlds` (passing) |
| `rocket-modules` | `RocketModuleCluster`, `ReorderableBuilding`, `Clustercraft`, habitats (`PassengerRocketModule`, exterior/interior doors, `NavTeleporter`), `RocketWallTile`, `DebrisPayload`: sealed cabins as owned worlds, retirement (landed and airborne) and slot reuse | `spaced-out-rocket`, `-input-flush`, `-rocket-retire`, `-rocket-flight`, `-rocket-abandon` (all passing) |
| `rocket-ports` | `RocketConduitSender`/`Receiver`/`StorageAccess` and the rocket-interior gas and liquid input/output ports | `spaced-out-rocket-ports` (passing) |
| `rocket-cargo` | `CargoBayCluster`, special/artifact/orbital cargo bays, `LaunchPad` and the modular launch-pad gas and liquid ports and unloaders | `spaced-out-cargo-gas`, `-cargo-liquid` (passing) |
| `warp-ports` | `WarpConduitSender`/`Receiver`: pipes between asteroids | `spaced-out-warp-ports` (passing) |
| `rocket-engines` | `RocketEngineCluster` (`DoBurn` emits exhaust with `EmitMass`), fuel and oxidizer tanks fed by `ConduitConsumer`, the HEP engine | `spaced-out-rocket-flight` (passing), `spaced-out-engine-exhaust` (to write: every exhaust-emitting cluster engine at launch, tank fill by pipe) |
| `rocket-landers` | `PodLander` and `CargoLander` (`LandingUpdate` emits mass on landing): the landing pod and the pioneer and scout landers | `spaced-out-landers` (to write) |
| `poi-harvest` | `HarvestablePOIClusterGridEntity`/`Configurator`, `StarmapHexCellInventory` and the harvest nosecone: gas and liquid POIs (helium, chlorine, gas giant, ocean) reach cargo bays through `Storage.AddElement` | `spaced-out-poi-harvest` (to write) |
| `rail-gun` | `RailGun` (gas and liquid input ports), `RailGunPayload`, `RailGunPayloadOpener` (gas and liquid output ports) | `spaced-out-rail-gun` (to write) |
| `radiation` | `RadiationEmitter` (radiation light, and Spaced Out! gives the shine bug, wheezewort, bees and engines emitters too), `LogicRadiationSensor`, the six radiation native messages and the four radiation `SimMessages` wrappers; the radiation grid is ONI's native transport over the shadow cells | `spaced-out-radiation` (to write) |
| `hep-particles` | `HighEnergyParticle` (`EmitRemainingPayload` emits Fallout with germs at impact), the spawners and their recipes | `spaced-out-hep` (to write) |
| `reactor` | `Reactor`: water coolant by pipe, `TransferCoolant`, coolant dumped at the vent cell, meltdown emits NuclearWaste with germs, waste storage | `spaced-out-reactor` (to write) |
| `uranium-centrifuge` | `UraniumCentrifuge`: molten uranium out through the liquid dispenser | `spaced-out-centrifuge` (to write) |
| `sludge-press` | `SludgePress` and its recipes: mud and toxic mud into dirt or toxic sand and (dirty) water through the liquid dispenser | `spaced-out-sludge-press` (to write) |
| `sublimation-station` | `SublimationStation`: toxic sand to polluted oxygen by an `ElementConverter` | `spaced-out-sublimation-station` (to write) |
| `spaced-out-critters` | Beetas and hive (`ElementConsumer` of carbon dioxide, radiation, `BeeForageStates` mining), staterpillars and their gas and liquid connectors (`GasAndLiquidConsumerMonitor`, `ConduitDispenser`) | `spaced-out-critters` (to write) |
| `spaced-out-plants` | `CritterTrapPlant` (vents hydrogen), the filter plant (`ElementConsumer`, irrigation): Spaced Out! plants that consume or vent fluids | `spaced-out-plants` (to write) |

Spaced Out! prefabs that reuse base components keep their base system:
the wall toilet, decontamination shower, enclosed telescope and lead suit locker
(`conduits`), lead suit and marker (`suits-masks`), the comets and molten-metal
geysers (`meteors`, `geysers`), the divergent critters (`critters`), and
under-construction prefabs (`terrain-construction`). Base-game prefabs that
Spaced Out! gives a `RadiationEmitter` also need `radiation`; the umbrella
systems `rockets`, `generators`, `plants`, `critters`, `refineries` and
`cluster-import` wait for the Spaced Out! systems named in their descriptions.

## Executable inventory

`coverage-inventory.json` contains 3,130 entries: 212 material identities,
62 native messages, 16 native imports, 31 native-hook responsibilities, 47
snapshot/event/texture channels, 215 observed call sites, 39 gameplay systems,
2,197 prefab registrations, 36 prefab converters and 275 recipe registrations
(the Spaced Out! capture plus the base-game entries it lacks).
Every entry names its intended owner, adapter and acceptance requirement.
Adapter names with `unimplemented` status are planned responsibilities, not
existing code. The playtest checklist above marks an entry `implemented` once its
system has passed its in-game checks; the per-configuration acceptance evidence
below is separate and still empty.

The two [publication bindings](native-domain-publication.md) are included among
the native-hook responsibilities. They leave the observer's 29 public hook
indices unchanged and still require full colony acceptance evidence.

The inventory is a conservative list of known work, not a completeness proof:

- Material identities came from one private Windows capture. The checked-in
  data contains identifiers and phase classifications, not game property tables
  or native payloads; numerical recipe/converter data stays outside Git.
- Static call-site labels do not distinguish overloads, resolve indirect
  dispatch or establish DLC applicability. A repeated generic adapter name does
  not prove that each originating action has an atomic transaction.
- All 32 gameplay DLC masks are explicit on both operating systems. Each
  configuration needs a passing runtime registry capture and per-entry in-game
  evidence. Absence requires a registry capture; native interfaces and observed
  call sites cannot be waived as absent. Unit tests never count as in-game
  acceptance. Cosmetic compatibility and numerical/performance/long-run
  requirements are separate release evidence.
- Native-hook entries additionally require explicit authoritative fluid
  ownership and complete reader/writer coverage; forwarding observation cannot
  pass them. Windows has no standalone `Step` symbol, so that obligation needs
  proof of the enclosing phase rather than an absence waiver.

```sh
python3 tools/coverage_inventory.py validate
python3 tools/coverage_inventory.py status
python3 tools/coverage_inventory.py gate
python3 tools/coverage_inventory.py gate --release
```

`validate` checks inventory integrity, source drift, omissions and evidence
shape and exits zero for a consistent *incomplete* inventory. `gate` exits one
until acceptance is complete; malformed or stale inputs exit two. The normal
build runs validation and regression tests, not the deliberately failing
release gate. A ledger of artifact hashes is an audit index, not independent
proof that the underlying in-game experiments passed.

Runtime identities are in `runtime-identities.json`. To extract a new source
without copying numerical game data:

```sh
python3 tools/runtime_inventory.py --capture /private/game-contract-local/runtime-coverage.json \
  --output /tmp/runtime-identities-candidate.json
```

Review and replace `docs/runtime-identities.json` before regenerating the main
inventory. Duplicate names are distinct registrations with separate occurrence
identities (the observed registry has 18 repeated prefab names and 24 repeated
recipe names); deduplicating them would hide work. Occurrence identities are
discovery labels, not permanent save or material identifiers.

To regenerate from an inspected private material capture, write a new candidate
and review it before replacing the checked-in inventory. Generation refuses to
overwrite existing evidence:

```sh
python3 tools/coverage_inventory.py generate \
  --materials /private/game-contract-local/contract.json \
  --inventory /tmp/coverage-candidate.json
```

## Implemented staging boundary

`NativeColonyImport.StageNewColony` consumes owned copies of length-prefixed
native v15 world blobs and an independently supplied expected cluster layout.
Before returning any result it validates count, identities, dimensions,
placement, nonoverlap, outer length, every inner blob and every fluid
material/phase binding. Unknown bindings, guard fluids and fluid backwalls
reject the import. Source buffers never change, even when the final world
fails. Returned worlds retain separate captured fluid records, sanitized native
payloads and correct outer prefixes behind read-only spans.

This is a **staging API**, not a native rollback protocol, and no live loader
hook uses it. A production loader must still validate material property
support, stage custom inventories, filter definitions before native Load,
handle partial native-initialization failures, publish merged owned buffers and
reject incompatible saves. A binding string alone does not establish a material
model.

`NativeElementTable.SuppressFluidProduction` validates the inspected 164-byte
element rows against all expected indices, hashes and phases and rejects
invalid transition references and byproducts. It copies the table, preserving
opaque names and tail bytes. It disables a whole temperature-transition branch
if its target **or byproduct** is fluid, and disables both sublimation and
conversion when either product is fluid; solid-only branches are unchanged and
suppressed branches are reported. The caller must verify the game binary before
using this version-specific interface. Definition filtering does not replace
transitions, their energy accounting, or native/managed emitters, chunks,
buildings, backwalls, interactions and recipes; those remain ownership gates.

## Verification

- Managed/native staging and filter scenarios pass on Linux and Windows
  (reordered and corrupted worlds, overlap, framing, unknown mappings, owned
  buffers, byproduct-only transitions, conversion remnants, malformed rows).
- Python regression checks cover omitted interfaces/materials, source drift,
  missing ownership/tests, DLC matrix corruption, insufficient evidence,
  runtime identities, duplicate labels and exclusion of numerical properties.
- The isolated [native harness](../tools/NativeShadowProbe/README.md) passes its
  import, solid-heat and filtered/unfiltered ice controls with this code on
  Windows and Linux, plus the expected sanitize failure.
- The Windows main-menu probe captured the runtime registry (2,143 prefabs with
  2,125 distinct labels, 34 converters, 275 recipes with 251 distinct labels).
  Loaded content reported only `EXPANSION1_ID`; that is not a colony's DLC mask
  and counts as no gameplay-matrix acceptance.

No disposable-colony ownership, multiphase solver, gameplay conversion or
release gate is certified by these checks.
