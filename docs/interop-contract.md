# Fluid ownership boundary in ONI 744825

This records the inspected native interface of licensed ONI build 744825 and
the rules it imposes on taking over fluids. The production design uses direct
native hooks; see [native backend integration](native-backend-integration.md).
`MacClosedWorld` (`native/src/mac_*.cpp`) is the production colony fluid
solver. ONI's own SimDLL fluid simulation is what the mod replaces and must not
be used as the solver.

The vacuum-shadow and external-process experiments below are historical
research. Their results and negative controls remain valid facts about the
native library. Projected native fluid values are permitted in the hook
design, and zero-native-fluid scans do not establish hook ownership. Native
private ABI, phase order and reader/writer coverage need their own Windows and
Linux proof.

Facts come from the licensed build and its matching shipped `SimDLL.pdb`. They
are original interface descriptions, not reproduced game implementation, and
not an official Klei API. Build 744825 is the inspected reference, not a promise
that later Steam builds are unchanged.

## Companion JSON

[`interop-contract.json`](interop-contract.json) holds the machine-readable
facts: build scope and the hashes of the inspected Windows and Linux
assemblies and native libraries, 62 message IDs, 84 unmanaged layouts with
exact Windows pointer widths and private fields, PDB comparisons, the static
call audit, raw Grid pointer assignments, the native v15 save layout, the
isolated-process results, the import hook order, the remaining fluid-source
categories and the Linux evidence.

- The JSON is an input. `tools/coverage_inventory.py` builds ownership entries
  from its message IDs, native imports, snapshot pointer fields and call-site
  facts, and `tools/runtime_inventory.py` checks its build number.
- `docs/coverage-inventory.json` pins the JSON's SHA-256 and both assembly
  hashes. `python3 tools/coverage_inventory.py validate`, which
  `tools/build.py` runs first, fails after any edit until the inventory is
  regenerated; see [ownership coverage](ownership-coverage.md).
- No checked-in script regenerates the JSON. Its facts come from
  `tools/InteropAudit`, `tools/inspect_oni.py --dump-api --native-exports` and
  the shipped PDB. Commit only identities, declarations and layouts; never
  game code, decompiled source or captured game data.

## Native singleton and ABI

`SIM_Initialize(callback)`, `SIM_Shutdown()`, `SIM_HandleMessage(id,length,bytes)`,
`SIM_HandleMessages(id,length,count,bytes)` and `SIM_BeginSave`/`SIM_EndSave`
take no world instance. The PDB names the globals `gSim` and `gSimData`. There
is no exported second-world or fluids-only stepping API, and headless
initialization uses the same singleton. One native frame covers pressure,
liquid, temperature, state transitions, sublimation, disease and component
services, so freezing the whole frame also stops services that should stay
vanilla.

The separate native `ConduitTemperatureManager` has its own
initialize/add/set/remove/update/clear exports but exchanges building
temperature data with the main snapshot. It is not a separate world simulator.

Unity probes on Windows and Linux confirm scoped managed interception, native
DLL preload, a native mixture ABI round trip and the layouts: `Cell` 16 bytes,
frame record 28 bytes, `GameDataUpdate` snapshot 496 bytes with 77 fields, and
eight-byte pointers on Linux.

`GAME_MessageHandler` is a managed int-return delegate taking an int ID and a
pointer, with the default Winapi convention on Windows x64. Diagnostic ID 0 has
two pointer fields (callstack, dump filename; 16 bytes). ID 1 has
callstack/message/file pointers plus an int32 line (28 bytes). The game copies
ANSI strings and returns 0 for known IDs and -1 otherwise. Keep the delegate
alive, never free callback pointers, and never let an exception escape to
native code. The PDB's native callback symbol encodes a pointer return rather
than the managed int; do not extend the Windows behavior to other platforms on
that basis. Component and mass events arrive through snapshots, not this
delegate.

## Element table and transitions

`SimMessages.CreateSimElementsTable` sends `Elements_CreateTable` (1108437482):
an int32 element count, 164-byte `Sim.Element` rows, then one UTF-8 name per
element prefixed by its int32 byte length. The method passes the backing-buffer
capacity as the message length, which can exceed the content; parse by counts.
Intercepting it can change only the serialized native rows while keeping
managed `ElementLoader` definitions and stable indices.

- The low two state bits are vacuum 0, gas 1, liquid 2, solid 3. Other flags:
  unbreakable 4, unstable 8, temperature-insulated 16.
- Low/high transition indices are at offsets 4/6, with ushort 65535 as the
  no-target sentinel. Low/high byproduct hashes are at 68/76 and conversion
  fractions at 72/80. Sublimation/conversion indices are at 84/86, and
  sublimation rate/efficiency/probability/offgas fields at 124/128/132/136.
- These become native-owned definitions once serialized, so changing managed
  element fields later is not enough.
- `CreateElementInteractions` (-930289787) has a Pack4 header of count plus a
  pointer to 32-byte rows; each row names both reactants and the result plus
  mass and probability multipliers. No direct managed call to it exists in this
  assembly. That does not establish native default behavior or exclude
  reflection or other mods.

Filtering a transition or byproduct that would create gas or liquid also
requires the custom phase owner to supply latent heat and conversion; disabling
a definition implements neither. Do not disable all reactions blindly, because
solids and gameplay chemistry still need an owner. In the isolated process an
unfiltered 400 K ice control melted and the filtered control stayed ice.
`NativeElementTable.SuppressFluidProduction` implements the validated row
filter; see [ownership coverage](ownership-coverage.md).

`BackwallManager.UpdateFromSim` consumes native transition notices, mutates
backwall data and emits products through `AddRemoveSubstance`. Suppressing the
native transition can also suppress the notice, so a custom schedule must
replace it. Chunk melts, building melts, spawned liquid particles,
disease/radiation components and ore byproducts need their own audit.

## Lifecycle and publication

- `SaveLoader.LoadFromWorldGen` initializes the singleton, creates the element
  table, allocates the cluster grid, defines world offsets, creates disease
  definitions, clears unoccupied cells, loads each opaque worldgen native blob
  and calls `Sim.Start` before prefab spawning. The ordinary loader follows a
  similar initialize/table/allocate/load/start path.
- `WorldGenSimUtil.DoSettleSim` has its own headless
  initialize/table/`InitializeFromCells`/start lifecycle before colony
  creation and publishes raw native Grid pointers. Scope ownership activation
  explicitly: world-generation physics is a separate concern from migrating
  initial state into a new colony.
- `Sim.Start` returns a native snapshot and assigns Grid pointer fields before
  `Grid.InitializeCells`. Every `Game.StepTheSim` calls `PrepareGameData`
  (1078620451) with the visibility-byte array and publishes new pointers.
  `BackwallManager`, property textures, cached `Grid.Element` and
  build/nav/visibility masks are separate consumers; replacing mass alone does
  not replace the domain.
- `Game.UnsafeSim200ms` sends `NewGameFrame` (-775326397) once, with one
  28-byte record per active region. All records belong to one step, so custom
  time advances once per frame, not once per region. Region bounds, sunlight
  and cosmic radiation belong to each record.
- Native frames are asynchronous. Submit a frame once and poll snapshots until
  the expected `numFramesProcessed`; do not reissue `NewGameFrame` while
  waiting.
- Native buffer lifetime and synchronization are undocumented. Keep native
  snapshots separate, copy into owned buffers and publish a merged projection
  with explicit lifetimes. Never put custom fluid mass into native snapshot
  arrays just so rendering sees it.

The raw snapshot exposes native element indices, mass, temperature, cell
flags, disease, radiation, backwall state, accumulated flow and texture
pointers. Its event arrays cover solid/liquid/substance changes, callbacks,
dig/ore/FX/falling liquids, instability and damage, building
temperature/melt/overheat, chunks, backwall transitions,
mass/disease/radiation results and component registration. Native nonfluid
records must be preserved, and custom fluid records merged with the same
lifetime and callback semantics. `World.UpdateCellInfo`, scene partitioning and
nav grids consume further queued changes after `StepTheSim`; fluid-solid phase
changes must update them too.

## Message and save formats

- `AllocateCells` writes 10 bytes: int32 width and height, then one-byte
  radiation-enabled and headless bools. Its stream capacity of 16 is not the
  payload length. It takes public cluster dimensions and adds 2 internally.
- `SimData_InitializeFromCells` has a 14-byte header (int32 width, int32
  height, uint32 seed, radiation bool byte, headless bool byte), then 16-byte
  `Cell`, 12-byte `DiseaseCell` and 12-byte `SimBackwall` records for every
  cell. `DiseaseCell` has private fields: index at 0, a reserved byte at 1,
  padding at 2/3, int32 count at 4 and a reserved float at 8. `SimBackwall`
  has private padding at 2/3. These must be present even though a public-field
  reflection listing omits them. Disease-free cells use index 255 and count 0.
- `Sim.Cell.Write` serializes properties and strength as zero, so it cannot
  preserve constructed-cell flags.
- `Start` is message -931446686 with length 0 and null data, returning
  `GameDataUpdate*`.
- `ModifyCell` (-1252920804) is a 28-byte Pack4 record. Sanitizing a cell to
  vacuum used Replace (enum 1), Vacuum's ushort index, mass 0, temperature 0,
  disease index 255/count 0, callback -1 and addSubType 1 (no vertical solid
  displacement). ReplaceAndDisplace (enum 2) is not suitable for this.

The compact native save, version 15, has the same layout on Windows and Linux:

- A 29-byte header: magic `SIMSAVE\0` at 0, int32 version 15 at 8, padded
  width/height at 12/16, int32 placement X/Y at 20/24 and a saved-options byte
  at 28.
- For N = padded width × padded height, three separate arrays follow: N cell
  records (stride 16: element **hash**, temperature, mass, radiation), N
  disease records (stride 8: disease **hash**, count) and N backwall records
  (stride 12: element **hash**, mass, temperature). Total length is 29 + 36N.
  These are neither interleaved rows nor the public initialization layouts.
- A public 64×64 world is saved as 66×66. Public (x,y) sits at padded index
  (x+1) + (y+1)·66. Allocate 64×64 to load that file. Native Load places padded
  rows at savedX + column + (savedY + row)·(clusterPublicWidth + 2) without
  adjusting the file's dimensions. Guard cells and world placement bounds must
  stay intact.
- The managed `Sim.Save` wrapper adds an outer int32 payload length;
  `SIM_BeginSave`'s raw bytes do not include it.
- Version 15 does not serialize cell properties, insulation or strength,
  private disease timers and error, random/tick/direction state,
  flow/visibility/radiation occlusion, world offsets and zones, or native
  registries and pending events. Ordinary loading rebuilds or defaults these.
  Rebuilding a live snapshot loses further initialized state, especially
  radiation (`InitializeFromCells` has no radiation array), world metadata,
  options and registrations. `SceneInitializer` instantiates `preloadPrefabs`
  before world loading, so component registries cannot be assumed empty there.

## Transactions and gameplay queries

| Contract | Required custom ownership |
| --- | --- |
| ModifyCell, MassEmission, AddRemoveSubstance, ReplaceElement, digging/displacement | Dispatch by phase/result; preserve cell flags and route all fluid mass/energy/disease. |
| MassConsumption | Select requested species, not dominant projection; reserve and debit once; return actual species mass/temperature/disease. |
| Add/Set/RemoveElementConsumer | Preserve callback registration and handles; Configuration supports Element, AllLiquid, AllGas. Bulk pumps remove a composition, species consumers remove only eligible material. |
| Add/Modify/RemoveElementEmitter | Own frequency, pressure blocking, blocked/unblocked callbacks, storage debit and multi-species output. |
| Building heat and ModifyCellEnergy | Retain native solid/building services; compute symmetric fluid-solid/building/backwall energy transfers. ModifyEnergy payload uses kilojoules. No duplicate native/custom exchange. |
| Breathability | GasBreatherFromWorldProvider uses dominant element tag and total cell mass; override with O2 species fraction, O2 partial pressure, total pressure and toxins. Bionic absorption/suit providers require separate coverage. |
| Pumps | Pump.IsPumpable reads dominant Element.IsState; use actual phase inventory and atomic storage capacity/rate allocation. |
| Pipes/storage | ConduitFlow contents are one element plus scalar mass/temperature/disease; a mixture needs a separate authoritative packet/inventory and serialization. Keep network topology only where its contracts remain valid. |
| Pressure/display/sensors | Grid.Pressure currently returns mass * 101.3, not the intended physical pressure. Dominant Element is a compatibility/render projection, never the authority for species or pressure. |
| Fluid biology and radiation/light | Vacuum native cells remove those interactions; custom fluids need disease growth/transfer, absorption and radiation/light effects as well as density/heat. |

- `MassConsumedCallback` entries release their callback handles when
  delivered. `MassEmittedCallback` and disease consumption use retained,
  versioned callback entries.
- `ComponentStateChangedMessage` releases the registration callback and
  supplies the native handle. Native `ElementConsumer.AddMass` is dispatched
  from `removedMassEntries` by handle.
- Custom component handles must not collide with native handles, and
  unregister, disable and storage-full paths must stay balanced.
- `EmittedMass` and building/chunk result pointers are also kept in
  `Game.simData`, so merging only the immediate callback loops is not enough.

Conduit thermal processing and managed gas/liquid/solid flow run after the
native snapshot with a fixed 0.2 s step. Native conduit update receives
building-temperature pointers and returns temperature, frozen-handle and
melted-handle arrays, and those phase changes can return contents to world
cells. Its single-element thermal model must not be kept for mixture packets
without proving capacity, enthalpy and phase behavior.

## Static call and writer audit

```sh
dotnet run --project tools/InteropAudit -- /absolute/path/Assembly-CSharp.dll > /tmp/oni-native-audit.json
```

The tool reads metadata only; see [its README](../tools/InteropAudit/README.md)
for `--calls-to` and its limitations. Both platforms' Assembly-CSharp give the
same result: 68,387 method bodies, 16 native imports, 70 direct native call
sites and no `calli`.

- Outside `Sim`/`SimMessages`, `DebugHandler.OnKeyDown` and
  `SubworldZoneRenderData.InitSimZones` call `SIM_HandleMessage` directly.
  `SaveLoader` and `WorldGenSimUtil` call `SIM_Initialize` directly, and a debug
  lambda calls `SIM_DebugCrash`. An interception policy confined to `Sim` and
  `SimMessages` misses these.
- Raw physics pointer assignments are in `Sim.Start`, `Game.StepTheSim`,
  shutdown and worldgen publication. The only raw-pointer read plus indirect
  store candidate is `Grid.InitializeCells`, whose stores are build masks.
  `Assembly-CSharp-firstpass` has no SimDLL imports and no raw Grid pointer
  store candidates.
- The scan cannot exclude aliased pointer writes, other assemblies, native
  writes, reflection, generated code or installed mods. Unresolved generic
  metadata and delegate/reflection dispatch are reported by the tool.

## Load hook points

- The narrow all-world staging point is a postfix on
  `Cluster.LoadClusterSim(List<Klei.SimSaveFileStructure> loadedWorlds, FastReader reader)`,
  inside a scope entered by `SaveLoader.LoadFromWorldGen()`. It returns the
  decoded list before native initialization. It catches deserialization errors
  internally, so a returning call can still have an incomplete list. Validate
  world count, every outer payload length, each record's width/height/X/Y
  against `Cluster.worlds`, padded guards, hash mappings, whole-cluster
  placement and nonoverlap, and every required species and phase before
  changing anything.
- `Klei.SimSaveFileStructure.Sim` is **length-prefixed** native bytes.
  Stage transformed clones for all worlds and replace the `Sim` fields only
  after every world validates; importing one world before finding a later
  invalid one is not atomic. A `Sim.Load` prefix would risk desynchronizing a
  reader whose prefix is already consumed.
- After staging the sequence is initialize, native element table, public
  cluster `AllocateCells`, world offsets, disease table, `ClearUnoccupiedCells`,
  `Sim.Load` per world, `Start`, `PostLoadPrefabs`, `NewSaveGamePrefab`,
  `OnWorldGenComplete.Signal`. Load already performs transitions, so any table
  filter must be active and verified before the first Load. A native-load
  failure must abort the custom ledger and clean up partial native state, and a
  scope finalizer must stop filtering from leaking into other lifecycle paths.
- `Sim.Load(IReader)` is called only by `SaveLoader.LoadFromWorldGen`;
  `Sim.LoadWorld(IReader)` serves ordinary `SaveLoader.Load`. Native table
  creation also happens in `SaveLoader.OnSpawn`, `SaveLoader.Load` and
  `WorldGenSimUtil.DoSettleSim`. Scope each independently so a new-colony path
  cannot alter normal saves or headless generation.
- A vanilla load of a save whose fluids live outside the native arrays would
  silently lose them, so unsupported loaders must reject such a save.

The managed staging API is `NativeColonyImport.StageNewColony`; see
[ownership coverage](ownership-coverage.md#implemented-staging-boundary).

## Remaining fluid sources

The JSON lists 49 `AddRemoveSubstance` sites plus direct `EmitMass`,
`ModifyMass`, `ReplaceElement` and component-registry sites; not every site
always emits fluid. Owners still needed:

- **Chunks.** `SimTemperatureTransfer.SimRegister` registers any eligible
  positive-mass, non-insulated `PrimaryElement` with no `IsSolid` check, so
  fluid bottles and items can enter the native chunk thermal registry.
  `DoOreMeltTransition` uses managed targets and byproducts and can emit fluid
  before destroying its source. Row filtering does not preserve item phase or
  latent heat.
- **Backwalls and buildings.** `BackwallManager.UpdateFromSim` and
  `StructureTemperatureComponents.DoMelt` consume native notices while managed
  `ElementLoader` keeps the real transitions. Building melt emits material,
  then destroys the building. If a row filter removes the notice, the phase
  owner must generate the event and an atomic heat and material transfer.
- **Native emitters.** `ElementEmitter`, `BuildingElementEmitter` and
  `GeothermalVent` reach Add/ModifyElementEmitter, whose pressure blocking
  reads native cells. Left active they can create native fluid and wrong
  blocked/unblocked callbacks. Consumers need virtual handles, selective and
  bulk transactions and correct registration callbacks.
- **Converters and storage.** `ElementConverter.ConvertMass` subtracts input
  mass and disease and calls `onConvertMass` before producing outputs. Stored
  output uses `AddToPrimaryElement`/`AddGasChunk`/`AddLiquid`, bypassing cell
  messages, so capturing `AddRemoveSubstance` afterwards cannot roll back
  consumed storage. Recipes, reservations, callbacks and item/pipe inventories
  must be owned at their real transaction boundary.
- **Conduits and particles.** Conduit thermal results trigger freeze and melt
  events; packet dumping, conduit consumers and vents re-enter world cells.
  `FallingWater.AddToSim` and `BubbleManager` are separate fluid inventories
  whose identity, energy, disease and failed placement need ledgers.
- **Gameplay sources.** Observed sites include `CO2Manager`, `EnergyGenerator`,
  geysers and sublimation, breathing, metabolism and excreta, creatures and
  plants, meteor and rocket exhaust, bottles and dumpers, and the Aquatic
  bubble and heater paths. Templates, sandbox, terrain updates, doors and
  occupiers replace or displace cells; solids entering a custom-fluid cell need
  coupled displacement, support and pressure behavior.

A post-frame native scan cannot prove that no fluid existed during a native
frame; declaration filtering and branch coverage are needed alongside it.
Static caller inventories omit indirect dispatch and other assemblies and mods.
Ownership and release gates stay false until these services run in a real
colony.

## Historical isolated-process experiments

SimDLL imports only Windows system libraries (dbghelp, KERNEL32, ADVAPI32,
VERSION, WS2_32, ole32, OLEAUT32), so an external process can drive the
singleton with the real exports and layouts, isolated from a running game. It
cannot prove Unity callback, prefab, renderer or save integration.

- **Table export.** Element rows came from a `MainMenu.OnSpawn` postfix after
  checking that `ElementLoader` is populated, substances and gradients are
  ready and indices match list positions. `new Sim.Element(element,
  list).Write(writer)` writes a row without initializing native simulation;
  calling `CreateSimElementsTable` unguarded would send the message.
  `Db.Initialize` completion alone does not mean `ElementLoader` is ready.
  Exported game data stays out of the repository and packages.
- **Minimal sequence.** Headless worldgen uses initialize, element table,
  disease table, `InitializeFromCells` (headless), `Start`, frame and
  `PrepareGameData`, without `AllocateCells`, world offsets or interactions.
- **Results, Windows and Linux libraries.** Native flow and solid heat ran, and
  filtered solid-to-fluid transitions were suppressed. A rebuild captured 102 kg
  of fluid, recreated the singleton with vacuum in those cells, kept every
  nonfluid `CellData` record and ran 12 frames with zero fluid and continuing
  solid heat. A filtered v15 blob (2 kg oxygen, 100 kg water) loaded with zero
  fluid through 12 frames while solids moved from 300/600 K to
  303.06064/596.9394 K. Linux used the tables captured on Windows, and its raw
  initial save was byte-identical.
- **Negative control.** `ModifyCell` to vacuum plus repeated `PrepareGameData`
  without a frame does not sanitize: two fluid cells and 102 kg remained after
  250 ms with zero processed frames.
- **Comparison pitfall.** Ordinary native Load normalizes metadata relative to
  `InitializeFromCells`, so compare a filtered load against an unmodified Load.

The disposable-colony gate written for this approach is superseded by the
[completion plan](completion-plan.md).

## Linux Unity notes

`-batchmode -nographics` crashes with SIGSEGV after Mono assembly loading;
Xvfb with Mesa llvmpipe works. The shipped Mono configuration lacks the
`machine.config` DataContract section, so reports use the game's JSON library
and settings a strict standalone codec; no game DLL is bundled. The Linux probe
reports LinuxPlayer, eight-byte pointers, the layouts above and 55 patched
native-message methods, and its frame, snapshot, native message, scoped save,
mixture ABI and settings round trips pass. Current setup is in
[the Linux harness](../tools/ONI_HEADLESS.md).

## Public sources

[Klei's Unity/Harmony transition announcement](https://kleiforums.com/game-updates/oni-alpha/719533-r2701/)
explains the managed runtime target.
[Klei's mod metadata guidance](https://kleiforums.com/forums/topic/158363-setting-up-mod_infoyaml-and-archived_versions/)
covers APIVersion and minimumSupportedBuild.
[Harmony's native-method limitations](https://harmony.pardeike.net/v2/articles/patching-edgecases.html)
explain why managed callers of native externs are intercepted instead. The
[744825 release page](https://kleiforums.com/game-updates/oni-alpha/744825-r2776/)
identifies the inspected build. Mod element loading is covered in
[native nitrogen](nitrogen.md#game-loader-contract).
