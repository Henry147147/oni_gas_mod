# Fluid ownership boundary in ONI 744825

**Architecture amendment, 2026-10-16:** direct hooks are the selected production
approach; see [native backend integration](native-backend-integration.md).
The vacuum-shadow candidate and minimum gate below describe earlier experiments
and remain historical evidence. Native private ABI, phase order and reader/writer
coverage need independent Windows/Linux proof. Projected native fluid values
are permitted; zero-native-fluid scans do not establish hook ownership.

The Windows x64 interface permits investigating a hybrid in which the custom solver owns all fluids and the original native simulator owns solids, solid chunks, buildings and backwalls. Inspection has not exposed an operation that makes this architecture impossible. It also has not proved that all native fluid production can be disabled safely. A native vacuum shadow is the strongest candidate; a native solid proxy changes collision, falling solids, heat capacity and digging, so it is unsuitable as a general fluid representation.

Facts below come from the acquired licensed build and its matching shipped `SimDLL.pdb`. They are original interface descriptions, not reproduced game implementation. The companion `interop-contract.json` contains 62 message IDs, 84 unmanaged layouts, exact Windows pointer widths, PDB comparisons and static caller facts. Build 744825 is the inspected reference, not a promise that Steam's latest build remains unchanged.

## Evidence and native isolation

Game root: `/mnt/data/share/oni/OxygenNotIncluded`. Managed assembly: `OxygenNotIncluded_Data/Managed/Assembly-CSharp.dll` (SHA256 `a58e04d0ffdf89b86fb28b71ad900625b3b539db30d67f8c6269f73a9f5ae599`). Native library: `OxygenNotIncluded_Data/Plugins/x86_64/SimDLL.dll`, matching PDB beside it. Steam BuildID 24423041; Unity 6000.3.5f2; Harmony 2.4.2.0.

`SIM_Initialize(callback)`, `SIM_Shutdown()`, `SIM_HandleMessage(id,length,bytes)`, `SIM_HandleMessages(id,length,count,bytes)`, and `SIM_BeginSave`/`SIM_EndSave` have no world-instance parameter. The PDB names global `gSim` and `gSimData`. There is no exported second-world or fluids-only stepping API. Headless initialization is still the same native singleton. Its frame includes pressure/liquid/temperature, state transitions, sublimation, disease and component services; internal native phase order has not been established. Freezing the whole native frame would also stop services intended to remain vanilla.

The separate native `ConduitTemperatureManager` has its own initialize/add/set/remove/update/clear exports, but exchanges building temperature data from the main snapshot. It is not a separate replacement world simulator.

An actual Unity main-menu probe now proves scoped managed interception, absolute native DLL preload, native mixture ABI roundtrip, and the expected layouts: Cell 16 bytes; frame 28; snapshot 496 with 77 fields. Its report explicitly leaves authoritative ownership false. Evidence is in `/mnt/data/share/oni/mod-development/probe-evidence`; no disposable colony ownership test has passed yet.

## Transition definition control

`SimMessages.CreateSimElementsTable` sends `Elements_CreateTable` (1108437482): int32 element count, 164-byte `Sim.Element` rows, then one UTF8 name per element prefixed with int32 byte length. It can be intercepted to transform only the serialized native rows while retaining managed ElementLoader definitions and stable indices. The current method passes backing-buffer capacity as message length, which can exceed serialized content; parse using counts, not trailing capacity.

The low two state bits distinguish vacuum 0, gas 1, liquid 2 and solid 3. Other state flags are unbreakable 4, unstable 8, temperature-insulated 16. Low/high transition indices are at offsets 4/6, ushort no-target sentinel 65535. Low/high transition byproduct hashes are at 68/76 and conversion fractions at 72/80; sublimation/conversion indices at 84/86; sublimation rate/efficiency/probability/offgas fields at 124/128/132/136. These are native-owned definitions after serialization, so changing only managed element fields later is insufficient.

For a hybrid, filter every native transition or byproduct that would create gas/liquid, and native sublimation/conversion destinations. Inspect `CreateElementInteractions` (-930289787) as well: its Pack4 header contains count plus pointer to 32-byte rows; each row names both reactants and result plus mass/probability multipliers. Do not disable all reactions blindly: solids and gameplay chemistry still require an owner. Complete native source coverage and behavior of no-target sentinel for each branch remain to be tested. Disabling a definition does not implement latent heat or custom phase conversion; the custom phase solver must supply those operations.

Backwall transitions are partly mediated by `BackwallManager.UpdateFromSim`: it consumes native transition notices, mutates backwall data and emits products through `AddRemoveSubstance`. That emission can be routed, but suppressing the native transition definition can also suppress the notice. A custom schedule must replace it in that case. Chunk melts, building melts, spawned liquid particles, disease/radiation components and native ore byproducts need independent audit.

## Actual lifecycle and publication

`SaveLoader.LoadFromWorldGen` initializes the singleton, creates the element table, allocates the cluster grid, defines world offsets, creates disease definitions, clears unoccupied cells, loads each opaque worldgen native blob, then calls `Sim.Start` before prefab spawning. The ordinary save loader follows a similar initialize/table/allocate/load/start path. `WorldGenSimUtil.DoSettleSim` has a separate headless initialize/table/InitializeFromCells/start lifecycle used before colony creation. It also publishes raw native Grid pointers. Scope ownership activation explicitly; rewriting world-generation physics is a separate concern from migrating initial state into a new colony.

`AllocateCells` writes 10 bytes: int32 width and height, one-byte radiation-enabled bool and headless bool. Its stream capacity 16 is not payload length. `SimDataInitializeFromCells` writes width/height/seed, two one-byte bools, then all 16-byte Cell rows, DiseaseCell rows and SimBackwall rows. `Sim.Cell.Write` deliberately serializes properties/strength as zero; it is not a general serializer for preserving constructed-cell flags. Opaque native save blobs cannot be rewritten by assuming they contain these public cell rows.

`Sim.Start` returns a native snapshot and assigns Grid pointer fields before `Grid.InitializeCells`. Every `Game.StepTheSim` calls `PrepareGameData` (1078620451) with the visibility-byte array and publishes new pointers. BackwallManager, property textures, cached Grid.Element and build/nav/visibility masks are distinct consumers; replacing mass alone does not replace the domain.

`Game.UnsafeSim200ms` sends `NewGameFrame` (-775326397) once, with one 28-byte record per active region. All records belong to one step, so do not advance custom time once per region. Region bounds, sunlight and cosmic radiation belong to each record. `PrepareGameData` then supplies the frame result. Native buffer lifetime and synchronization guarantees are not documented: keep native snapshots separate, copy to owned buffers, and publish a merged projection with explicit lifetimes. Never put custom fluid mass into native snapshot backing arrays merely to make rendering see it.

The raw snapshot exposes native element indices, mass, temperature, cell flags, disease, radiation, backwall state, accumulated flow and texture pointers. Its event arrays include solid/liquid/substance changes, callbacks, dig/ore/FX/falling liquids, instability/damage, building temperature/melt/overheat, chunks, backwall transitions, mass/disease/radiation results and component registration. Preserve native nonfluid records and merge custom fluid records with the same lifetime and callback semantics. `World.UpdateCellInfo`, scene partitioning and nav grids consume further queued changes after StepTheSim; fluid-solid phase changes must update them too.

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

MassConsumedCallback entries release their callback handles when delivered. MassEmittedCallback and disease consumption use retained versioned callback entries. ComponentStateChangedMessage releases the registration callback and supplies the native handle. Native ElementConsumer.AddMass is dispatched from removedMassEntries by handle. Custom component handles must not collide with native handles, and unregister/disable/storage-full paths must remain balanced. EmittedMass and building/chunk result pointers are also retained in `Game.simData`; merging only immediate callback loops is insufficient.

Conduit thermal processing and managed gas/liquid/solid flow run after the native snapshot, using a fixed 0.2-second step. Native conduit update receives building-temperature pointers and returns temperature, frozen-handle and melted-handle arrays. Those phase changes can return contents to world cells. Do not retain its single-element thermal model for mixture packets without proving capacity, enthalpy and phase behavior.

## Static call and writer audit

Run the original metadata-only audit with:

```sh
dotnet run --project tools/InteropAudit -- /absolute/path/Assembly-CSharp.dll > /tmp/oni-native-audit.json
```

The inspected assembly has 68,387 method bodies, 16 native imports, 70 direct native call sites, and no `calli`. Outside Sim/SimMessages, `DebugHandler.OnKeyDown` and `SubworldZoneRenderData.InitSimZones` call SIM_HandleMessage directly. SaveLoader and WorldGenSimUtil call SIM_Initialize directly; a debug lambda calls SIM_DebugCrash. These are explicit holes in a caller interception policy confined to Sim and SimMessages.

Raw physics pointer assignments are in Sim.Start, Game.StepTheSim, shutdown and worldgen publication. The only raw-pointer-read/indirect-store candidate is Grid.InitializeCells; inspection identifies its stores as build masks, not native cell mass/temperature. The scan does not prove absence of aliased pointer writes, other managed assemblies, native writes, reflection, generated code or installed mod writers. Unresolved generic metadata and delegate/reflection dispatch are reported by the tool. Assembly-CSharp-firstpass has no SimDLL imports and no detected raw Grid pointer-store candidates.

## Observed isolated native results and new-colony import

Root's actual v4 external-process tests now establish native fluid flow and solid heat, filtered solid-to-fluid transition suppression, and a rebuild that captured102kg of fluids, recreated the singleton with vacuum in their cells, preserved every nonfluid CellData record, and then ran12 native frames with zero fluid cells and continued solid heat transfer. The unfiltered400K ice control melted; the filtered control stayed ice. This is a tested synthetic native domain, not a Unity colony takeover.

The alternative ModifyCell(vacuum,Replace) followed by repeated PrepareGameData **failed** to sanitize without a frame: two fluid cells and102kg remained after250ms, with zero processed frames. Do not use that approach for first-frame activation. Native frames are asynchronous: submit once and poll snapshots until the expected numFramesProcessed count, rather than reissuing NewGameFrame while waiting.

An actual v5 native save proves this build's compact version15 format. Header29 bytes: magic `SIMSAVE\0` at0; int32 version15 at8; padded width/height12/16; int32 placement X/Y20/24; saved-options byte28. For N=paddedWidth*paddedHeight, arrays are all N cell records (stride16: element **hash**, temperature, mass, radiation), then N disease records (stride8: disease **hash**, count), then N backwall records (stride12: element **hash**, mass, temperature). Total29+36N bytes. These are separate arrays, not interleaved36-byte rows and not the public Cell/DiseaseCell initialization layouts.

The public64×64 test emitted dimensions66×66 and156845 bytes. A public(x,y) record sits at padded index(x+1)+(y+1)*66. Independent comparison verified all4096 element-hash/T/mass fields bitwise as float32 against Start's snapshot; this specimen's radiation/disease/backwall mass/temperature are zero. Evidence: `/mnt/data/share/oni/mod-development/shadow-evidence-v5/baseline.native.bin` and `.native.cells.json`. Only the facts and file hash are recorded here, not derived game data.

AllocateCells takes public cluster dimensions and adds2 internally; allocate64×64 to load that66×66 file. Native Load places all padded rows at savedX+column+(savedY+row)*(clusterPublicWidth+2); it does not subtract2 from the file's dimensions or add another border. Guard cells and world placement bounds must remain intact. The managed Sim.Save wrapper adds an outer int32 payload length; SIM_BeginSave's raw bytes do not contain that wrapper.

Filtering new-colony worldgen blobs before native Load is now the preferred candidate backed by an actual isolated native load test. Extract each interior fluid record into custom state and replace its native record with vacuum hash/zero mass/temperature/radiation; clear its disease hash/count. Keep header/options, guards, native solid and backwall bytes unchanged. Activate the filtered element table before Load because Load performs load-time transitions. Run validation and reject unknown versions, inconsistent lengths, invalid hashes, fluid guards or overlapping/out-of-bounds placement. Managed parser tests now cover nonzero species/disease/radiation preservation, placement, corrupt/unsupported payloads, and refusal of fluids in guard/backwall records. The earlier nine managed/interop tests passed on Windows using the then-current39-test native DLL. The current suite passes12/12 on both platforms against frozen native49, including three added strict-settings JSON tests. Actual native loading with nonzero disease/radiation/backwalls and multiple world placements still needs integration tests.

The v7 `load-shadow` process test exported a real native save, split102kg (2kg oxygen at20,20 and100kg water at30,20), supplied filtered tables, AllocateCells64×64 and one public world offset, then loaded the transformed native bytes. Start returned zero fluids before any frame. All12 completed frames retained zero fluids while solid temperatures moved from300/600K to303.06064/596.9394K; no native diagnostics occurred. Evidence: `/mnt/data/share/oni/mod-development/shadow-evidence-v7/load-shadow.json`.

Nonfluid preservation compares against an **unmodified native Load control**. Ordinary native Load normalized metadata on4096 cells relative to InitializeFromCells; comparing filtered loading directly with initialization would falsely blame normal load behavior on migration. The transformed and unmodified loads matched all nonfluid CellData fields exactly. This proves a narrow synthetic fluid import/native shadow boundary, including gas and liquid extraction and native solid heat. It does not prove active Unity colony scheduling, projection, native component coexistence, comprehensive transition suppression, mixed pipes or persistence.

The current v15 writer does not serialize flags/insulation/strength, private disease timers/error, random/tick/direction state, flow/radiation occlusion, visibility, world offset/zones or native registries/events. Those are therefore not extra hidden worldgen services lost specifically by filtering this file. Ordinary native loading already supplies defaults/reconstruction. Existing DefineWorldOffsets and later SubworldZoneRenderData zone generation remain in the original colony lifecycle. Snapshot rebuilding loses additional already-initialized state and needs replay, especially radiation (InitializeFromCells has no radiation array), world metadata, options and registrations. SceneInitializer also instantiates preloadPrefabs before world loading; being before PostLoadPrefabs does not prove that all existing component registries are empty. The blob approach avoids a second reset.

## Minimum disposable-colony ownership gate

1. Create a distinct new test colony through the real game lifecycle. Keep ownership disabled for existing colonies and headless world generation. Scope import to SaveLoader.LoadFromWorldGen and stage custom state from every worldgen blob before native Load; freeze first real NewGameFrame until all imports and merged projection are ready.
2. Validate the exact supported native v15 format and padded placement, preserve all nonfluid/backwall/header data, replace native fluid records with vacuum and clear fluid disease records. Supply the filtered native element/interaction tables before Load, retain original world-offset and zone setup, and require first Start's raw snapshot to contain zero native gas/liquid. The isolated rebuilt-owner route is a proven fallback for synthetic cells but needs additional lifecycle state reconstruction in Unity. Do not revive the failed no-frame ModifyCell sanitation route.
3. Publish deep owned merged arrays and compatible render textures; preserve native solid/building callbacks. Start with a sealed solid enclosure, native building and two custom species. Step native solids and custom fluids once per 0.2-second frame. Prove temperature and native solid/building event progression while fluid stays custom.
4. Test species consumption and bulk pump/storage transactions, emitter blocking, fluid-solid/building heat equal-and-opposite energy, phase conversion, and unloading. Retain all native nonfluid data needed by the game. Scan raw native state before and after every frame and reject on any fluid cell; also reject unexpected fluid emission/ore/particle events.
5. Test a solid reaching a formerly native fluid-transition threshold, sublimation and a backwall/chunk/building melt. Demonstrate custom material/energy transfer, expected game callbacks and zero native fluid generation. A post-frame scan alone cannot prove that no fluid existed transiently during a native frame; declaration filtering and branch coverage are required alongside assertions.
6. Serialize native solids/services plus a versioned custom fluid inventory and migration marker. Reload only the disposable new test colony and check species/energy/handles/projection roundtrip. A vanilla load of only the native vacuum shadow would silently lose fluids, so unsupported loaders must reject the custom save.

This gate proves a narrow ownership prototype, not the requested complete mod. It omits exhaustive conversion/metabolism rebalance, all machinery/creatures/crops, all DLC paths, physical fluid pressure forces on native solids, hydrostatic support/falling interactions, bubbles, radiation, complex conduit packets, world resizing and other mods. Vacuum shadow changes native fluid support and pressure-damage behavior; custom coupling must restore it where gameplay needs it. No public release readiness follows from passing this gate.

## Isolated native process proof

SimDLL's inspected import table contains only Windows system libraries (dbghelp, KERNEL32, ADVAPI32, VERSION, WS2_32, ole32 and OLEAUT32), with no UnityPlayer dependency. An external Windows harness can investigate the singleton without sharing an active game's global state. The external-process proof has now run; its actual scope and results are recorded below.

Export the game's derived native element bytes from a `MainMenu.OnSpawn` postfix, checking ElementLoader's list is populated, substances/gradients are ready and indices match list position. `new Sim.Element(element, list).Write(writer)` reads managed definitions/colors and writes bytes; it does not initialize native simulation. Append the exact int32-length UTF8 names after the rows. Do not call CreateSimElementsTable unguarded: it sends the native table message. A scoped interceptor that captures and suppresses that message is another option. Db.Initialize completion alone does not establish ElementLoader's readiness. Keep exported game data outside this repository and release packages.

The actual headless world-generation precedent uses initialize, element table, disease table, InitializeFromCells(headless), Start, frame and PrepareGameData. It supplies no AllocateCells, world offsets or interaction table in that path. That is the smallest observed complete sequence for an external harness; omission of disease definitions has not been proved safe. Export the real disease-table message under capture-only scope, or test a zero-disease header (int32 zero, int32 element count) in the disposable process before relying on it. Disease-free cells use index255 and count0.

InitializeFromCells uses a 14-byte header (width int32, height int32, seed uint32, radiation bool byte, headless bool byte), followed by **16-byte Cell, 12-byte DiseaseCell and 12-byte SimBackwall** records for every cell. DiseaseCell includes private fields: index0, reserved byte1, padding2/3, count int32 at4 and reserved float at8. SimBackwall includes private padding bytes2/3. These must be present even though they are absent from a public-field-only reflection listing. The checked JSON includes private fields.

Start is message -931446686 with length0 and null data, returning GameDataUpdate*. Sanitation uses ModifyCell (-1252920804), 28-byte Pack4: Replace enum1, Vacuum's exported ushort index, mass0, temperature0, disease index255/count0, callback -1 and addSubType1 (no vertical solid displacement). ReplaceAndDisplace enum2 is not appropriate for sanitation.

Match the shipped managed imports' default Winapi convention on Windows x64. GAME_MessageHandler is a managed int-return delegate taking int ID and pointer, with no explicit convention override. Diagnostic ID0 has two pointer fields (callstack, dump filename),16 bytes; ID1 has callstack/message/file pointers plus int32 line,28 bytes. The game copies ANSI strings and returns0 for known IDs, -1 unknown. Keep the delegate alive, do not free callback pointers, and never let an exception escape to native. PDB's global native callback symbol encodes a pointer-return type rather than the managed int declaration; flag this ABI discrepancy rather than extending the Windows behavior to other platforms. Ordinary component/mass events arrive through snapshots, not this diagnostic delegate.

First test actual initialized solid/vacuum cells, Start's snapshot, frame progression and shutdown. Then test ModifyCell→PrepareGameData without a frame, unfiltered versus filtered transition thresholds, and native raw-state scans. The harness must call the same exported functions and real layouts; managed stub success is not evidence. Native failure callbacks need a live pinned delegate, and a crash is confined to this separate test process. An external harness still cannot prove Unity callback, prefab, renderer and save integration.

## Linux native import and ABI evidence

The licensed Linux depot has now been acquired at `/mnt/data/share/oni/OxygenNotIncluded-linux`. Actual build744825 native `OxygenNotIncluded_Data/Plugins/libSimDLL.so` has SHA256 `85111cd0b1fc14c79c785581d6c055280b9d48ba705776f3d14eb0a7b239c12d`; Assembly-CSharp has SHA256 `6db652990ad1038e86b3493ed43004e12bfd61e62a4be60c787a9a9b9ac52ee1`. Sim/SimMessages decompiled declarations match the Windows dumps byte-for-byte. The same metadata audit finds68387 bodies,16imports,70native call sites, one raw-pointer/store candidate and no calli.

The isolated Linux native suite passes baseline, rebuild-shadow, load-shadow, unfiltered transition and filtered transition. Sanitation without a frame is again the expected negative control: two fluid cells102kg, zero completed frames. Linux's raw initial native save is byte-identical to Windows (SHA256 `9df8859e47112d2bcf64543a6f5bf82e0947f15bde46fde2b2f29059fe734da3`), supporting the inspected padded v15 format and native import boundary on both platforms. Evidence is under `/mnt/data/share/oni/mod-development/shadow-evidence-linux-v2`.

These tests supplied the native element/disease table captured from the Windows main menu. They did not launch Linux Unity or prove its own DLC-dependent element generation, mod loader, Steam initialization, callback dispatch and rendering. Linux Unity main-menu validation remains a separate setup gate. No full-colony ownership claim follows from identical declarations or synthetic native load controls.

Windows MSVC now passes56/56 standalone scenarios with zero compiler warnings and all30 transferred source hashes verified. Linux Release and ASan/UBSan also pass56/56. The seven new material-pressure/volume-work scenarios test a reversible moving-volume substep coupling nonlinear gas/liquid EOS, pressure work, kinetic energy and entropy; Eulerian remap and complete world replacement remain unimplemented. Evidence is `windows-build-native56/build-evidence.json` and `probe-evidence/native56-linux/build-evidence.json` under the shared mod-development directory. The12/12 managed tests and successful Windows/Linux Unity json49 packages stay frozen against native49; neither managed tests nor Unity were rerun for native56. Earlier acoustic and fixed-flux transport components remain limited primitives, not a complete pressure-driven world solver.

## Transactional import hook order and unresolved source coverage

The narrow all-world staging point is `Cluster.LoadClusterSim(List<Klei.SimSaveFileStructure> loadedWorlds, FastReader reader)` postfix, inside a scope entered by `SaveLoader.LoadFromWorldGen()`. It returns the decoded list before any native initialization in that method. It catches deserialization errors internally, so a returning call can still have an incomplete list: validate world count, every outer payload length, per-record width/height/X/Y against Cluster.worlds, padded guards, hash mappings, whole-cluster placement/nonoverlap and every required custom species/phase before changing anything.

`Klei.SimSaveFileStructure.Sim` is **length-prefixed** native bytes. NativeWorldBlob.Split takes the raw payload, not that outer prefix. Stage transformed clones and captured inventory for all worlds; preserve original arrays and defer replacing all item.Sim fields until every validation succeeds. If using a Sim.Load prefix instead, changing the reader after consuming its prefix risks desynchronizing it; replacing the staged per-world buffer avoids depending on reader rewinding. Importing one world successfully before discovering a later invalid world is not an atomic migration.

The original sequence after staging is initialize → native element table → public cluster AllocateCells → world offsets → disease table → ClearUnoccupiedCells → Sim.Load for each world → Start → PostLoadPrefabs → NewSaveGamePrefab → OnWorldGenComplete.Signal. Filter the serialized native table during the scoped CreateSimElementsTable call, retain managed definitions and stable indices, and verify the filtered table before the first Load because Load already performs transitions. On Start, require every expected world imported, publish the merged projection before Grid/cache/prefab readers see vacuum shadows, and verify raw native state before committing custom inventories/marking saves. A native-load failure must abort the custom ledger and clean up the partial new-colony native state; a scope finalizer must prevent leaked filtering into other lifecycle paths.

The current direct-call audit distinguishes `Sim.Load(IReader)` (only caller SaveLoader.LoadFromWorldGen) from `Sim.LoadWorld(IReader)` (ordinary SaveLoader.Load). Native table creation also occurs in SaveLoader.OnSpawn, SaveLoader.Load and WorldGenSimUtil.DoSettleSim. Scope those independently; a new-colony filter must not accidentally alter normal saves or headless generation. Existing safeguards must explicitly reject an unsupported colony rather than let the ordinary loader silently read only the vacuum shadow. No direct managed call to CreateElementInteractions was found in this assembly. This does not establish native default interaction behavior or exclude reflection/other mods; do not describe an ordinary managed interaction-table initialization call as observed.

The call-site facts in the JSON identify49 AddRemoveSubstance sites plus direct EmitMass, ModifyMass, ReplaceElement and component-registry sites. They do not establish that all those sites always emit fluids. Important remaining owners are:

- **Chunks:** SimTemperatureTransfer.SimRegister registers any eligible positive-mass non-insulated PrimaryElement, without an IsSolid condition. Fluid bottles/items can therefore enter the native chunk thermal registry. DoOreMeltTransition uses original managed element targets/byproducts and can emit fluid before destroying the source object. Native row filtering and a zero-fluid world-cell scan do not by themselves preserve item phase/latent heat or replace native chunk registration.
- **Backwalls/buildings:** native notices are consumed by BackwallManager.UpdateFromSim and StructureTemperatureComponents.DoMelt; managed ElementLoader still contains the real transitions. Building melt emits material then destroys the building. A native row threshold filter may prevent its notice, so the custom phase owner must generate a replacement event and atomic heat/material transfer. Independent native building/chunk/backwall controls are still missing.
- **Native emitters:** Add/ModifyElementEmitter is reached by ElementEmitter, BuildingElementEmitter and GeothermalVent. Their native pressure blocking sees the vacuum shadow; leaving them active can generate native fluid and incorrect blocked/unblocked callbacks. Consumers likewise need virtual handles, selective/bulk inventory transactions and correct registration/removal callbacks.
- **Converters/storage:** ElementConverter.ConvertMass subtracts input item mass and disease and calls onConvertMass before generating outputs. Stored output uses AddToPrimaryElement/AddGasChunk/AddLiquid, bypassing world-cell messages. Capturing AddRemoveSubstance at the end cannot roll back consumed storage or side effects when custom output capacity rejects a transaction. Own recipe calculation, reservation, callbacks and item/pipe inventories at their actual transaction boundary.
- **Conduits/particles:** native conduit thermal results trigger freeze/melt events; packet dumping, conduit consumers and fluid vents re-enter world cells. FallingWater.AddToSim and BubbleManager are separate fluid inventories/emission sources. Their identity, energy, disease and failed placement need conservation ledgers, not a display-only projection.
- **Gameplay sources:** the observed call sites include CO2Manager/EnergyGenerator, geysers/sublimation, breathing/metabolism/excreta, creatures/plants, meteor/rocket exhaust, bottles/dumpers and new Aquatic bubble/heater paths. Templates/sandbox, terrain updates, doors and occupiers also replace/displace cells. Native solids moving or appearing in a custom-fluid cell need coupled displacement/support/pressure behavior; vacuum-native displacement cannot solve it.

The proven synthetic import/shadow path supplies none of those adapters. It also does not advance a complete liquid mixture model, provide merged render/query buffers, or establish full conservation across machines and metabolism. Future raw native scans are assertions alongside exhaustive source/registry tests; they cannot prove absence of transient fluid during a native frame. Static caller inventories omit indirect dispatch and other assemblies/mods. All ownership and release gates must remain false until the required services are implemented and exercised in a disposable real colony.

## Nitrogen registration and mod element loading

No nitrogen entry occurs in this build's shipped `StreamingAssets/elements/{gas,liquid,solid,special}.yaml`. Registration must therefore add a new game element identity for nitrogen display/storage/queries, even though the custom species registry can already carry its physical properties.

The actual mod loader recognizes an `elements` directory at the mod content root as LayerableFiles. `KMod.Directory` supplies an AliasDirectory mapped onto Application.streamingAssetsPath, and KMod.Mod.Load inserts that file source into Klei.FileSystem. `ElementLoader.CollectElementsFromYAML` enumerates `elements/*.yaml` through that virtual filesystem. Current KYaml uses VYaml but correctly reads the supplied FileHandle's source bytes. Thus an original mod YAML under `<mod>/elements/<unique-file>.yaml` is the inspected supported loading path; copying files into the game installation is unnecessary. This is a reverse-engineered shipped loader contract, not a separate official element authoring API.

ElementLoader.Load hashes each elementId with SDBMLower, registers tag/name/description, copies the YAML properties and manifests a Substance from the DLC-specific substance table. The relevant table key for base content is the actual empty string DlcManager.VANILLA_ID. FinaliseElementsTable sorts elements by phase/hash and assigns indices: resolve indices after that stage, do not assume a fixed appended index. A registration needs a valid gas Substance/gradient/material and localization; the native Sim.Element constructor reads Gradient before its later null check, so a missing substance is a startup failure. Add liquid/solid nitrogen identities and transitions if the full phase model exposes condensation/freezing; assigning pure-fluid defaults does not replace the custom mixture thermodynamics.

The implementation now adds three namespaced nitrogen identities through this
path; see [native nitrogen](nitrogen.md) for property scope and Windows game
evidence. A valid gas substance may legitimately have null material/animation
references and rely on the normal native fallback. Preserve those references
when cloning; the live prefab and renderer paths establish validity. Both
native `GameTagExtensions` hash-to-tag helpers need scoped custom identities,
because an unknown `SimHashes` value otherwise stringifies as an integer.
YAML also supplies explicit description keys: the installed default getter uses
current-culture `ToUpper()`, which produces missing keys for these IDs under
Turkish casing.

A [maintained mod author's registration patches](https://github.com/Sgt-Imalas/Sgt_Imalas-Oni-Mods/blob/master/RonivansLegacy_ChemicalProcessing/Patches/ElementLoader_Patches.cs) register substances before ElementLoader.Load and adjust finalized elements afterward. Its [ModElements definitions](https://github.com/Sgt-Imalas/Sgt_Imalas-Oni-Mods/blob/master/RonivansLegacy_ChemicalProcessing/Content/ModDb/ModElements.cs) already add three nitrogen phases. This is community-authored implementation precedent; do not copy its code or numerical values as physical authority. The historical [VYaml mod-loading report](https://kleiforums.com/klei-bug-tracker/oni_beta/elementloader-yaml-loader-change-breaks-modded-element-loading-r52663/) concerns an earlier beta; the inspected current FileHandle loader uses its mod file source, so that report alone does not establish a present failure.

## Public source context

[Klei's Unity/Harmony transition announcement](https://kleiforums.com/game-updates/oni-alpha/719533-r2701/) explains the newer managed runtime target. [Klei's mod metadata guidance](https://kleiforums.com/forums/topic/158363-setting-up-mod_infoyaml-and-archived_versions/) covers APIVersion and minimumSupportedBuild. [Harmony's native-method limitations](https://harmony.pardeike.net/v2/articles/patching-edgecases.html) explain why intercepting managed callers is useful for native externs. The [744825 release page](https://kleiforums.com/game-updates/oni-alpha/744825-r2776/) identifies the inspected public build. Native internal contracts above are reverse-engineered facts from the licensed local binary, not an official supported Klei API.

## Actual Linux Unity setup attempt

The original Linux Unity6000.3.5f2 executable was tested with the frozen44 probe package in a bubblewrap-isolated environment with task-owned XDG configuration, caches, logs and a writable thin game root. Existing host settings/saves and licensed game data were bound read-only. No colony or existing save was loaded, and no Steam authentication was attempted.

Both -batchmode/-nographics attempts (read-only and writable executable roots) crashed inside native Unity with SIGSEGV after Mono assembly loading. A locally extracted Xvfb plus existing Mesa llvmpipe allowed normal graphical initialization under -batchmode. The game reported missing local Steam but continued into loading the two probe assemblies. Report serialization then failed at DataContractSerializerSection.UnsafeGetSection because the shipped Mono configuration directory has no machine.config/DataContract section. The thin-root config matches the actual depot; this is not a missing copy in staging. No integration JSON was produced, so runtime scope/interop success is not claimed from that run. The corrected json49 package was subsequently tested successfully, as recorded below.

The owned game/display exited; no persistent host configuration was changed. Immutable attempt evidence/logs are under `/mnt/data/share/oni/mod-development/probe-evidence/linux-unity-attempt-v1`. The native-library import suite remains separately successful. Linux Unity mod loading and scoped probe completion are distinct from reaching a fully authenticated game menu or proving colony ownership.

## Linux Unity scoped probe completion

The corrected immutable `MixedAtmospheresProbe-linux-json49` package now passes
inside actual Linux Unity6000.3.5f2, using the same isolated Xvfb/Mesa setup.
Runtime reports LinuxPlayer, eight-byte pointers, Cell16/frame28/snapshot496
with77fields, and55 patched native-message methods. Managed frame, snapshot,
direct native message, scoped save, native mixture roundtrip and the actual
settings serialization roundtrip all pass. The serializer compatibility fix
uses the shipped game JSON library for reports and a strict standalone codec
for settings; no game DLL is bundled in the mod package.

No integration JSON was inferred from the earlier failed run: the successful
report and Player log are in
`/mnt/data/share/oni/mod-development/probe-evidence/linux-unity-json49`.
The package manifest stayed unchanged. The game exited naturally0 after the
successful mod probe and before MainMenu.OnSpawn, with missing local Steam
reported. No Linux-native element/disease table capture was generated, no auth
was attempted and no colony/settings from the host were opened or changed.
The independent Linux native suite's table provenance therefore remains the
Windows main-menu export. Windows also passes the refreshed12-test managed
suite against the unchanged native49 DLL. Full colony ownership remains false.
