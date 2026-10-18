# Owned reservoir test hosts

The `tools/OwnedReservoir*` projects test the experimental owned gas-reservoir
bridge and the building blocks for saving and loading it: idle checkpoints,
owner markers, load rejection, saved-file inspection and file promotion. The
bridge, its driver sequence and its in-game results are in
[the reservoir bridge](../docs/owned-reservoir-bridge.md). No host enables
integrated saves, owned restore, writer permission or native save permission,
and every report keeps those flags false.

| Project | Tests | Licensed game | Run by `tools/build.py` |
| --- | --- | --- | --- |
| [`OwnedReservoirTests`](#ownedreservoirtests-bridge-protocol-and-idle-lease) | Bridge and actor protocol, idle save lease | Yes | With `--game-dir` |
| [`OwnedReservoirCheckpointTests`](#ownedreservoircheckpointtests-idle-checkpoint-codec) | `MARESVR1` idle checkpoint codec | No | Yes |
| [`OwnedReservoirMarkerTests`](#ownedreservoirmarkertests-owner-markers-and-footprints) | Inert owner marker, copied footprint, saved-ID tracker | Yes | With `--game-dir` |
| [`OwnedReservoirLoadTests`](#ownedreservoirloadtests-load-rejection-boundary) | Rejection-only selected-load boundary | Yes | No |
| [`OwnedReservoirLoadCounterTests`](#ownedreservoirloadcountertests-load-counter-preflight) | `SIM_Initialize` callers, load-counter contracts | Windows files | No |
| [`OwnedReservoirSavedInspectionTests`](#ownedreservoirsavedinspectiontests-directory-body-prefix-and-ksav-inspection) | Saved directory, body prefix, KSAV sections | No | Yes |
| [`OwnedReservoirInflationTests`](#ownedreservoirinflationtests-bounded-zlib-fragment-inspection) | Bounded zlib/DEFLATE fragment | No | Yes |
| [`OwnedReservoirLayoutTests`](#ownedreservoirlayouttests-complete-source-layout-inspection) | Whole-file layout, save write receipt | No | Yes |
| [`OwnedReservoirFilePromotionTests`](#ownedreservoirfilepromotiontests-bounded-complete-file-promotion) | Bounded complete-file promotion | No | Yes |
| [`OwnedReservoirRecoverableFileTests`](#ownedreservoirrecoverablefiletests-recoverable-complete-file-promotion) | Recoverable promotion experiment | No | No |
| [`OwnedReservoirGameMonoValidation`](#ownedreservoirgamemonovalidation-mono-file-protocol-validation) | File protocol inside ONI's Mono | Runs in the game | No |

## Common setup

- The hosts target .NET 10. On the Windows machine, use
  `C:\Users\henry\.dotnet\sdk10\dotnet.exe`; see
  [the environment notes](ONI_ENVIRONMENT.md#machines-and-toolchains).
- A host that needs the game takes its root twice. The build reads
  `-p:OniGameDir` (falling back to `ONI_GAME_DIR`), and the program needs the
  root as its first argument to resolve game assemblies at run time. Assemblies
  are referenced in place and never copied.
- Build a `netstandard2.1` contract-only variant with its own
  `--artifacts-path`, for example under the ignored `local/`, so it does not
  share `obj/` with the .NET 10 host.
- Keep generated test directories, private fixtures, captured saves and reports
  in ignored local directories.
- No host installs Harmony patches. Shipped Harmony serializes its patch table
  with BinaryFormatter, which .NET 10 cannot run. Hook installation, Unity
  lifecycle and native side effects are verified only in disposable game runs.

## OwnedReservoirTests: bridge protocol and idle lease

```sh
dotnet run --project tools/OwnedReservoirTests -c Release -p:OniGameDir="$ONI_GAME_DIR" -- "$ONI_GAME_DIR"
```

This host compiles the production bridge, actor operations, idle save lease,
frame coordinator, message router, batch commands and owned snapshot against
the licensed declarations. Synthetic native memory is deep-copied by the
production snapshot code, and controlled managed hosts drive the actor
coordinator. It prints `PASS` or `FAIL` per check and exits 0 only if all pass.
All 51 checks pass against both the Windows and the Linux declarations.

The checks cover:

- conserved mixed transfers, including fractional disease;
- immutable prepared views and receipts, and independent acknowledgements;
- callback faults, stale and foreign leases, and source retry;
- rejection of over-capacity and late commands;
- forced cleanup during staging and callbacks, evacuation and unbinding;
- sticky world and unfinished-consumption obligations, and never-bound recovery;
- completed-frame queries, dormant guards, and pinned method and prefix
  signatures.

The source guard runs the real `Pickupable.storage` property. All five licensed
transfer overloads reject an owned endpoint at either end on entry.

The idle save eligibility lease pins the exact acknowledged Core publication and
both actor/native owner identities. It keeps empty unbound allocations. It
rejects nonempty world cells, unbound mass or metadata, pending callbacks or
work, and recovery obligations. While it is held, Core and actor writes fail,
and so do bind and unbind. Native callbacks and Unity getters run outside Core
locks. Detachment and session end abort the lease before coordinator disposal.

Native eligibility repeats the captured component and prefab checks: roles,
spawn, isolated gas ports, empty native packets, construction, capacity, world
and anchor. `Enabled` flags are diagnostics only, and native instance ID 0 is
valid. `SaveCoreCheckpoint` returns only the pinned Core bytes. `SaveLoader.Save`,
native `Storage` serialization, delayed saves and save-first quit stay blocked.

`PresentationTestBoundary.cs` replaces only presentation registration. Unity
lifecycle, item transforms, storage events, meters, native descriptions and
native-empty instance binding are tested only in the game.

## OwnedReservoirCheckpointTests: idle checkpoint codec

```sh
dotnet run --project tools/OwnedReservoirCheckpointTests -c Release
```

This pure-managed host links `OwnedReservoirCheckpoint.cs` and Core only. It
needs no game or native library, installs no hooks and prints one `PASS` line
per test; any failure exits nonzero. The publication lease
(`ColonyFluidState.ReservePublication`) is described in
[owned containers](../docs/container-inventories.md#idle-checkpoints-and-publication-leases)
and tested in `MixedAtmospheres.Tests`.

`OwnedReservoirCheckpointV1` keeps the exact canonical Core v3 bytes for exactly
two declared native owners:

- Every empty unbound container is kept. Every nonempty container, including
  trace mass and massless metadata, needs exactly one owner. World-cell mass and
  metadata reject.
- These are validated: full handles, native colony identity, the pinned build,
  the two managed contract hashes, catalogue identity, volume and gas admission,
  native instance IDs (0 is valid), owner tokens and construction values.
- Construction mass is an exact promoted native float. Capacity is the
  independent 1000 kg gameplay limit, never an inferred volume or pressure.
- `IdleState` is the caller's declaration, not proof of live eligibility.

**Binary layout.** Values are BinaryWriter little-endian primitives, GUIDs are
`Guid.ToByteArray`, and strings have an int32 UTF-8 byte length (not the 7-bit
prefix). The fields, in order:

1. `MARESVR1`, an int32 version and the int64 total length.
2. The build, two SHA256 contract identities and the native colony GUID.
3. The catalogue version and its SHA256 fingerprint.
4. The idle protocol version, zero flags and counts.
5. An int64 length and the canonical Core v3 bytes.
6. An int32 owner count and two owner records sorted by saved instance ID.
7. A SHA256 of all preceding bytes.

The maximum size is 640 MiB. Unknown versions, trailing bytes, bad lengths,
corruption, identity conflicts and nonidle data fail closed. The codec cannot
see prefab instances, packets, ports, pending callbacks or save coroutines, and
encoding it is not ONI persistence.

## OwnedReservoirMarkerTests: owner markers and footprints

```sh
dotnet run --project tools/OwnedReservoirMarkerTests -c Release -p:OniGameDir="$ONI_GAME_DIR" -- "$ONI_GAME_DIR"
dotnet build tools/OwnedReservoirMarkerTests -c Release -p:OniGameDir="$ONI_GAME_DIR" \
  -p:MarkerContractOnly=true --artifacts-path local/marker-contract
```

This host runs the production owner marker, footprint validation and checkpoint
sources against the licensed install. It never starts Unity, installs Harmony,
saves, loads, changes native objects or activates ownership. The second command
builds only the product files for `netstandard2.1`. All 31 checks pass on
Windows and Linux.

**Marker.** The marker has no assignment API and no lifecycle adoption. Its
seven private primitive fields are read strictly. Only complete empty defaults
are unassigned; partial, unknown-version or malformed data is invalid. The tests
run the licensed native serializer, directory roundtrip and type lookup. A local
template mapping must leave an unrelated global load mapping intact, so the game
fixture never clears or reinitializes the game's serialization manager.

**Validation.** Geometry-only pair validation accepts default markers and
reports `ManifestValidated=false`. Manifest validation requires two exact
assigned records with matching native colony, owner, token and full allocation.
The exact completed Core allocations must match; equivalent restored ones cannot
substitute. It also requires zero world inventories, coverage of every nonempty
allocation and unique IDs, tokens and handles. This validates data only. It is
not a save permit or a lease.

**Capture.** Capture reads all 15 cells and the definition, occupancy and
building caches. It checks native building-layer ownership, both utility cells
with their connections and pipes, and every orientation against the pinned
native offsets. Saved IDs must resolve to the exact owners, without creating or
repairing a tracker. Capture repairs nothing and derives no world fluid. Its
copied observations carry no permission, so a live caller must hold and
revalidate the lease. Overlapping footprints, row wrap, overflow, stale caches,
changed identity or capacity and cross-colony handles reject. Prefab spawn,
`Grid` occupancy and the in-game serializer are tested in the game.

## OwnedReservoirLoadTests: load rejection boundary

```sh
dotnet run --project tools/OwnedReservoirLoadTests -c Release -p:OniGameDir="$ONI_GAME_DIR" -- \
  "$ONI_GAME_DIR" [/private/path/malformed.sav]
dotnet build tools/OwnedReservoirLoadTests -c Release -p:OniGameDir="$ONI_GAME_DIR" \
  -p:LoadContractOnly=true --artifacts-path local/load-contract
```

This host covers `OwnedReservoirLoadHeaderV1.cs`, `OwnedReservoirLoadAttempt.cs`
and `OwnedReservoirLoadBoundary.cs`. Together they form a dormant header
inspector, immutable attempt records and manually installed rejection hooks for
build 744825. The host uses the declarations only for reflection and IL checks;
it never starts Unity or calls SimDLL. The optional private malformed save is
checked by header only, and its SHA256 must not change. Otherwise the host prints
a `NOTE` and uses its own fixtures. All 72 checks pass on Windows and on Linux,
where the host uses the pinned Linux declarations.

**Activation.** There are no `HarmonyPatch` attributes, so only an explicit
`OwnedReservoirLoadBoundary.Install(harmony)` installs it. The mod makes that
call only in the `load-rejection` validation mode, through
`OwnedReservoirLoadVisualValidation`. Install checks the
exact SHA256 of `Assembly-CSharp`, firstpass, `UnityEngine.CoreModule` and
Harmony, then patches 27 exact overloads. On failure it rolls back only its own
patches. The pins are the Windows hashes, so Linux installation is disabled.

**Inputs.** A null or empty selected path is `NoSelection`, and new-colony
generation is untouched. A valid unmarked native header (version 1) passes
through byte for byte. Every recognized owned header rejects, even a valid one,
because no restore path exists. Once a path is selected, malformed, truncated or
unsupported data, invalid UTF-8 or JSON, and read errors fail closed instead of
falling back to cached world generation.

The reserved top-level JSON key is `mixedAtmospheresOwnedReservoirs`, and an
escaped spelling counts as the same key. Case variants, duplicate keys, bad
strings or surrogates, comments, trailing commas, nonfinite numbers and
truncation reject instead of reading as unmarked. The name nested inside a value
is not a declaration. Markers without this header are not protected yet.

A declaration has exactly 15 fields:

- `formatVersion` 1, scope `two-gas-reservoirs-idle` and `oniBuild` 744825;
- a colony identity matching `GameInfo`, and `ownerCount` 2;
- checkpoint and envelope format 1, each with its SHA256;
- Core format 3 and material model 1;
- the catalogue version and fingerprint, and both managed assembly hashes.

Reads stop at 16 bytes plus 1 MiB, JSON depth 32 and 65,536 value tokens.
`InspectFile` reads only the header, through a read-only `FileStream` with
`FileShare.Read`. Missing, denied or locked files, directories, URIs, device
paths, NUL characters and paths over 32,768 characters report `ReadError`. There
are no writes, retries, cloud downloads or fallback lookups.

**Selected load.**

- Every nonempty public `Load(string)` call takes an opaque identity before
  inspection.
- `SaveGame.GetHeader` compares a bounded copy of the native reader's bytes at
  its position with the preflight bytes. A change or a repeated callback rejects.
- A false result from the inner `Load(IReader)` or the outer `Load(string)`
  latches failure; so do a missing header observation and an escaping exception.
- `Sim.LoadWorld` and `Load` validate the section framing without advancing the
  reader, and exact `MACOLONY` magic rejects before native dispatch.

**Fatal attempts.** An attempt records at most 128 events, and the tracker keeps
16 completed attempts. A fatal attempt cannot be reset. The frontend request is
issued once. The completion kind is fixed at the first failure:

- **Existing `Game`.** Completion needs that `Game`'s actual
  `Game.DestroyInstances` body.
- **Before any `Game` (`PreGameLoaderScene`).** Completion needs the captured
  `SaveLoader`'s inherited `KMonoBehaviour.OnDestroy` and the matching
  `SceneManager.Internal_SceneUnloaded` original bodies. Then the boundary
  clears the dead `SaveLoader` singleton by compare-exchange on its exact private
  field, and a foreign successor fails completion.

Each witness requires Harmony's `__runOriginal` normal-path postfix and a
finalizer with a null exception. A one-use token belongs to the first matching
cleanup, and the public call and any active `OnSpawn` must unwind first.

While fatal, these rules hold:

- Native world load and start, saved-root creation, world-generation fallback,
  `Game` spawn and update, and all save entry points are fenced; save fences
  throw.
- `SaveManager.Load` cannot report success, and `Game.Load(Deserializer)`
  throws.
- The failed call clears the active save pointer again, so an autosave cannot
  restore the path.
- Earlier effects are not rolled back, and `Sim.Shutdown` is never called.

**View retirement and guards.** Before requesting the frontend, the boundary
calls `Game.SetIsLoading` for the exact current attempt, thread and
`Game.Instance`. Installation verifies that this is a single assignment to a
private bool. The boundary then rechecks and sets `Grid.CellCount` to 0. The
bool alone is not enough, because `AmbienceManager` samples cells without
checking it. A missing or foreign `Game` is skipped.

During a pending pre-`Game` attempt, the `DoGridProcessing` prefix skips only the
captured animation updater's zero-sized grid; without that, early `OnSpawn`
rejection hit a native divide-by-zero. A late `Game.OnPrefabInit` blocks and
invalidates completion. A `Priority.First` transpiler adds one return to
`SaveLoader.OnSpawn`, directly after the `loadedFromSave` setter that follows
the `Load(string)` call. It returns when the boundary is installed and the
stored native result is false, so the old shutdown, frontend and worldgen
continuation cannot resume. Ambiguous bodies reject installation.

The validation driver reads `IsFatal`, the attempts and the immutable `Last*`
observations. It compares Unity references with `ReferenceEquals` and never
serializes Unity objects.

**Status.** On the host, a few patch methods run against uninitialized `Game`
and `SaveLoader` shells; no Unity lifecycle or native call runs. Actual Windows
ONI passes rejection-only acceptance; see
[the reservoir bridge](../docs/owned-reservoir-bridge.md#selected-load-rejection-boundary).
An ordinary reservoir save and reload pass. Then three rejections each return to
a stable frontend: schema 99 on an existing `Game`, schema 99 in `OnSpawn`
before `Game` exists, and an invalid-zlib native load failure. Full KSAV parsing
at load, quarantine, owned restore, owned saves, crash recovery and Linux Unity
remain open.

## OwnedReservoirLoadCounterTests: load counter preflight

```sh
dotnet build managed/MixedAtmospheres.Oni -c Release -p:OniGameDir="$ONI_GAME_DIR" \
  --artifacts-path local/load-counters/product
dotnet run --project tools/OwnedReservoirLoadCounterTests -c Release -- "$ONI_GAME_DIR" \
  local/load-counters/product/bin/MixedAtmospheres.Oni/release/MixedAtmospheres.Oni.dll
```

This host scans the original IL of every managed body in Windows build 744825.
It invokes no game method, Harmony, Unity or SimDLL. It pins Windows
`Assembly-CSharp` (`a58e04d0ffdf89b86fb28b71ad900625b3b539db30d67f8c6269f73a9f5ae599`)
and `UnityEngine.CoreModule`
(`a9613c2b0f1258ac7f7d9323181f816f5849beb24114e4a7665037fd11ddd488`), so
`$ONI_GAME_DIR` must hold Windows game files. On Linux, use
`/mnt/data/share/oni/OxygenNotIncluded`.

With only the root, the host lists the `SIM_Initialize` callers and exits. There
must be exactly four one-call callers: three `SaveLoader` bodies and the
nine-parameter `ProcGenGame.WorldGenSimUtil.DoSettleSim`. With the real product
DLL as the second argument, it also checks these contracts:

- The closure validator accepts the full list and rejects missing, duplicate and
  the old three-caller lists.
- `InspectContracts` validates 45 methods, 12 native call sites, 15 native-map
  keys and 4 managed antecedent keys.
- Each call site occurs once and returns normally, and iterators resolve
  through their state-machine attributes.
- `Game.OnPrefabInit`, `KMonoBehaviour.OnDestroy`,
  `SceneManager.Internal_SceneUnloaded` and
  `KBatchedAnimUpdater.DoGridProcessing` are included.
- Repeated inspection leaves the counter registries empty and `Installed`
  false.

Any failed check throws. The private Mono game run installs the counters and
calibrates dispatch, and on Windows it verifies the complete maps with zero
observation failures.

## OwnedReservoirSavedInspectionTests: directory, body-prefix and KSAV inspection

```sh
dotnet run --project tools/OwnedReservoirSavedInspectionTests -c Release
dotnet build tools/OwnedReservoirSavedInspectionTests -c Release \
  -p:InspectionTargetFramework=netstandard2.1 --artifacts-path local/ksav-contract
```

This host links three pure-managed product files:
`OwnedReservoirSavedTemplates.cs`, `OwnedReservoirBodyPrefixInspection.cs` and
`OwnedReservoirKsavInspection.cs`. It has no game, Unity or Core dependency,
resolves no saved types, runs no callbacks or native readers and writes nothing.
The default run uses only independently written synthetic byte streams. All 63
groups and the `netstandard2.1` build pass on Windows and Linux. The parsers are
summarized in
[saved-file inspection](../docs/owned-reservoir-bridge.md#saved-file-inspection).

**Contract.** The inspectors copy their input and read bounded little-endian
values. Every top-level root and component is framed before known layouts are
decoded, and recognized `Storage` packets are walked. Marker, `KPrefabID`, `Tag`
and inherited `Storage` fields are read in saved order. Unknown bytes and the
tail stay untouched. A structural failure throws a typed exception with no
partial report. Marker conflicts are reported without choosing a winner. Reports
keep every authority flag false: whole-file preflight, descendant coverage,
native grid and Sim validity, native load, owned restore, writer and quarantine.

- `SavedDirectoryInspector.InspectPrefix(image, offset, limits)` derives the
  directory end only from the template, member and descriptor counts. Its byte
  budget and count minima are checked before records are allocated. Truncation
  is `Malformed`; crossing a budget is `LimitExceeded`. `Inspect(image, offset,
  exactLength, limits)` requires exact closure.
- The body-prefix inspector walks the inflated body from byte zero. It accepts
  only the exact `Klei.SaveFileRoot`, `Game+Settings` and `KMod.Label` layouts,
  and every collection claim must equal the walked consumption. Null
  `requiredMods` is exactly `(length=0,count=-1)`, and any non-null value is
  unsupported. Non-null `active_mods` needs the exact `Label` template.
- Changed type flags, null streamed collections and the legacy streamed `Sim`
  key reject.
- The inline int32 Sim length and its opaque bytes give the only candidate KSAV
  offset. No layer searches for magic or type strings.

**Limits.** Callers may only tighten the defaults: a 1 GiB image, a 64 MiB
directory, 65,536 templates, members and groups, and 262,144 recognized roots.
Components are capped at 4,096 per root and 1,048,576 in total, `Storage`
nesting at 64, and descriptors at 32 levels and 262,144 nodes. `Tag` elements
are capped at 1,048,576. Names may be 16 KiB (member names 4 KiB) and value
strings 1 MiB. The prefix caps metadata at 64 MiB, streamed entries and `Label`
rows at 4,096, and streamed values at 64 MiB. UTF-8 values are capped at 1 MiB
each (16 MiB in total) and the Sim blob at 512 MiB. These bounds do not
guarantee every ONI or mod save.

**Capture modes.** Three optional modes calibrate the parsers against pinned
private Windows saves and observer captures, which stay outside the repository:

- `--ordinary-fixture <sav> --directory-end <n>`
- `--actual-capture <observer dir> --ordinary-save <sav>`
- `--derived-capture <observer dir> --ordinary-save <sav>`

The derived mode compares its derived offsets with the native observations only
after deriving them. Every mode must preserve each component payload, the native
packet, the prefix and the tail byte for byte, and none decodes `PrimaryElement`
values. The actual and derived modes pass on Windows and Linux. In actual ONI
Mono after a native save, menu and reload, the count-derived chain matches the
observed native boundaries and packet identities. The layouts were researched
against Windows build 744825.

## OwnedReservoirInflationTests: bounded zlib fragment inspection

```sh
dotnet run --project tools/OwnedReservoirInflationTests -c Release [-- /path/to/Fixtures]
dotnet build tools/OwnedReservoirInflationTests -c Release \
  -p:InflationTargetFramework=netstandard2.1 --artifacts-path local/inflation-contract
```

This host tests the zlib fragment adapter (`OwnedReservoirSavedInflation.cs`)
and the strict DEFLATE walker (`OwnedReservoirSavedDeflateStructure.cs`), with no
game or vendor codec. Fixtures are copied beside the host and checked against
the manifest's length and SHA256. A run passes only with exactly **64**
groups, 49 fixture records (21 valid, 28 malformed) and 15 support groups, and
then prints a JSON summary. They pass on Windows and Linux, and the `netstandard2.1` build
has no warnings.

The caller supplies an immutable image and an exact compressed range. Before any
decoder exists, the walker proves final-block closure, reference history and
the exact trailer position. `DeflateStream` then decodes the validated range.
Output bytes and `Read` calls are bounded. The decoder must reach EOF and agree
with the structural count, and Adler-32 must match. Decoder position is never
framing proof. Defaults are listed in
[the reservoir bridge](../docs/owned-reservoir-bridge.md#bounded-compressed-fragment-inspection).

The corpus covers stored, fixed and dynamic blocks, repeats, overlapping and
cross-block references, malformed trees and trailing streams. Two earlier
designs remain controls: an invalid distance before any output must reject, and
a valid repeat-18 stream across both alphabets must pass. This proves bounded
inspection of one fragment only. In actual Windows ONI Mono after a native save
and reload, 57 private decoder and bounds groups pass and the adapter recovers
an independently observed save body.

## OwnedReservoirLayoutTests: complete-source layout inspection

```sh
dotnet run --project tools/OwnedReservoirLayoutTests -c Release [-- /path/to/fresh-receipt.json]
dotnet build tools/OwnedReservoirLayoutTests -c Release -p:LayoutTargetFramework=netstandard2.1 \
  --artifacts-path local/layout-contract
```

This host runs **82** synthetic groups against eight linked product helpers:

- `OwnedReservoirLoadHeaderV1` and `OwnedReservoirSavedLayout`;
- `ColonySaveReadback`;
- `OwnedReservoirSavedTemplates`, `OwnedReservoirSavedInflation` and
  `OwnedReservoirSavedDeflateStructure`;
- `OwnedReservoirBodyPrefixInspection` and `OwnedReservoirKsavInspection`.

An independent writer builds the fixtures in memory, with hand-written stored
DEFLATE blocks. No compressor, other inspector or real save supplies expected
values. The run stops at the first failure. On success it prints 82 ordered
`PASS` lines and JSON, and an optional receipt path is created without
overwriting. The `netstandard2.1` library contains only the helpers. Linux and
Windows .NET under Wine last passed the 81-group revision.

`SavedColonyLayoutInspector.Inspect` starts at byte zero of one complete image.
In order, it derives the native header, the count-framed directory, the exact
zlib remainder, the body prefix and the KSAV section. The report shares one
source, directory and body identity. Only build 744825, header version 1 and the
compressed 7.38 profile are supported, and others reject with typed failures.
Later magic bytes never supply fallback boundaries.

`ColonySaveReadback.Verify` associates disk bytes with the exact serializer
body, its Sim section and one colony identity. It also works for uncompressed
saves. Stale files, changed bytes, swapped headers or pairs, incomplete writes
and trailing bytes produce no receipt. A receipt grants no load authority; see
[paused colony save/load](../native/MAC_CHECKPOINT.md#paused-unity-colony-saveload).

The groups are 39 layout, malformed, immutability and concurrency groups, 35
resource groups covering all 33 caller limits, and 8 receipt groups. Native Sim
bytes, custom components and the tail stay opaque. `NativeInvoked=false` means
no ONI Sim API was called.

## OwnedReservoirFilePromotionTests: bounded complete-file promotion

```sh
dotnet run --project tools/OwnedReservoirFilePromotionTests -c Release -- \
  /absolute/private/test-parent [/absolute/normal-helper.dll]
```

`OwnedReservoirFilePromotion.cs` is a pure-managed helper with no Core, native,
game or Unity dependency and no save hook. The host compiles its fault seam
under `MIXED_ATMOSPHERES_ATOMIC_FILE_TESTS`. Each run creates
`file-promotion-tests-<guid>` under the parent with a `result.json`; `build.py`
uses `<build dir>/file-promotion-checks`. An optional normal `netstandard2.1`
assembly, such as the `OwnedReservoirGameMonoValidation` output, must have no
fault API and no Core, game or Unity references. All 40 checks pass on Windows
NTFS and WSL Linux, with one platform skip each.

The contract is **bounded promotion only**. Two gates stay open: uninterrupted
target-name visibility, and recovery of the old target after promotion starts.
Do not read this as permission to enable integrated saves.

`Prepare(absoluteTarget, bytes)` copies a nonempty file of at most 1 GiB and
hashes it, with no I/O. The path must be fully qualified, at most 4,096
characters, with a name of at most 240. Relative, dot, control-character, UNC,
device, `/dev`, `/proc` and `/sys` paths reject.

`Promote(image)` validates the parent chain and target without creating
directories. Links, reparse points, devices and read-only targets reject. It
writes a fresh `.ma-save-{guid}.stage` (`CreateNew`, `FileShare.None`), calls
`Flush(true)`, and verifies the length, bytes, EOF and SHA256. Then it makes one
call: `File.Move` for an absent target or `File.Replace(stage, target, null,
false)` for an existing one. It never copies, truncates, deletes or overwrites
as a fallback, and it reads the target back before succeeding.

| Result | Meaning |
| --- | --- |
| `NotPromoted` | The promotion call never started. The target is unchanged, and only the helper's own stage is removed. |
| `PromotedAndVerified` | The call returned and the target's bytes and hash match. This does not prove crash durability or continuous name visibility. |
| `PromotionOutcomeUnknown` | Promotion started, then an error occurred. The target may be old, new or missing. Nothing is deleted or rolled back. |

Callers must reconcile unknown outcomes and never retry them automatically
because the target looks right. They must own the namespace and exclude other
writers. No backup is kept, and `Flush(true)` does not flush the directory or
survive a power loss.

The fault enum covers every stage, the moments around promotion, corruption,
verification and cleanup failures, and name collisions. On NTFS, readers saw a
missing name during `File.Replace`, so the strict visibility test failed. Later
probes saw missing-name and sharing errors but never mixed content. This matches
[ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).
.NET 10's `File.Move(..., true)` is no alternative: it verified only 2 of 5
attempts before access errors, and it is unavailable in `netstandard2.1`.

## OwnedReservoirRecoverableFileTests: recoverable complete-file promotion

```sh
dotnet run --project tools/OwnedReservoirRecoverableFileTests -c Release -- \
  /absolute/private/test-parent [/absolute/normal-production.dll]
```

This experiment links only file code. `OwnedReservoirRecoverableFilePromotion.cs`
and `OwnedReservoirLocalRename.cs` are excluded from the normal ONI build until
reviewed. Use a direct NTFS or ext-family directory; the default parent is the
temporary directory. Each run keeps its fixtures and `result.json` under
`recoverable-<guid>`. The host's fault seam exists only under
`MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS`. An optional normal assembly must have
no fault types, no test entry points, no `File.Delete` calls and no calls to the
old promoter. All 49 checks pass on Windows NTFS and Linux ext storage, with one
platform skip each.

`Prepare` copies a nonempty candidate of at most 1 GiB. `Promote(image)` is one
caller-controlled attempt with no callbacks or locks. A second call is a new
attempt, and an unknown outcome must never be retried automatically.

| Outcome | Meaning |
| --- | --- |
| `NewFileVerified` | The rename returned, and the target and intent passed readback. No target existed before. |
| `ReplacementVerified` | The rename returned, and the target, the byte-exact backup and the intent passed readback. |
| `NotAttempted` | No rename entry point ran. Created artifacts are kept. |
| `OutcomeUnknown` | A rename entry point ran, then an error occurred. A matching target cannot settle this. |

**Protocol.** Parents must be direct directories. UNC, device and virtual
roots, links, reparse points, device or read-only targets, malformed UTF-16 and
unsupported filesystems fail closed with no fallback. Scope is x64 Windows on
fixed local NTFS and x64 Linux on ext. On Linux, `statx` checks the type first,
so FIFOs cannot block. The helper holds the original with a handle that denies
writes and delete sharing. It writes and verifies a same-directory backup, the
new stage and a prepared intent, and closes them. It rechecks everything just
before the single rename and verifies everything again after it.

```text
.ma-recover-{operationGuid:N}.new
.ma-recover-{operationGuid:N}.old       # only when a previous file exists, including empty
.ma-recover-{operationGuid:N}.intent
```

Production code never deletes, retries, rolls back, heals or picks a latest
intent. It never writes the backup over the target. The prior image covers the
default byte stream only, and no result grants save permission.

**Prepared intent v1.** The format is little-endian and at most 16 KiB. Strings
are int32-length strict UTF-8 basenames, at most 240 UTF-16 characters.

| Field, in order | Encoding |
| --- | --- |
| Magic, version, length | ASCII `MAFSPRM1`, int32 `1`, int64 including the checksum |
| Operation, backend, reserved | `Guid.ToByteArray()`; byte `1` Windows or `2` Linux; three zero bytes |
| Basenames | Target, stage and backup; the backup is empty when absent |
| New file | int64 length (1 byte to 1 GiB) and SHA256 |
| Previous file | byte present flag, int64 length (0 to 1 GiB) and SHA256 |
| Checksum | SHA256 of all preceding bytes, then exact EOF |

An absent previous file has an empty backup name, zero length and a zero hash. A
present empty file has a GUID-derived name and SHA256(empty). Unknown versions,
noncanonical names, bad lengths, checksum errors, trailing bytes and presence
mismatches reject. The checksum detects corruption only; it is not
authentication.

`Inspect(target, intent)` classifies the target as `Missing`, `MatchesNew`,
`MatchesPrevious`, `MatchesBoth`, `Other` or `Unreadable`. It exposes the prior
image only after the backup matches and is captured again. It never renames.
The record always means *prepared*, never committed, and `CommitAttested` and
`NativeSavePermitted` are always false.

**Backends.** On Windows, a DELETE-only `CreateFileW` handle is renamed through
`SetFileInformationByHandle` class 22 (`FileRenameInfoEx`). The flags are
`REPLACE_IF_EXISTS | POSIX_SEMANTICS` (3), or 0 for a new target. The x64 layout
has the root at offset 8, length at 16 and name at 20, in a 24-byte header.
Readers that deny delete can cause error 32. On Linux, `renameat2` uses flags 0
to replace or `RENAME_NOREPLACE` (1) to create. A missing entry point is
`NotAttempted`, and any errno after entry is `OutcomeUnknown`. `statx` uses the
256-byte UAPI layout with the mode at offset 28. References:
[FILE_RENAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info),
[SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle),
[FileRenameInformationEx](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/4217551b-d2c0-42cb-9dc1-69a716cf6d0c),
[rename(2)](https://man7.org/linux/man-pages/man2/rename.2.html),
[statx(2)](https://man7.org/linux/man-pages/man2/statx.2.html).

The checks cover zero-byte and Unicode files, every fault stage, locked, linked
and FIFO inputs, no-replace collisions, held readers, 32 concurrent replacements
and hostile intents. They test correctness, not performance or durability.

## OwnedReservoirGameMonoValidation: Mono file-protocol validation

```sh
dotnet build tools/OwnedReservoirGameMonoValidation -c Release --artifacts-path local/game-mono-validation
```

This source-only project builds the normal file helpers into a separate
`netstandard2.1` assembly, `MixedAtmospheres.GameMonoFileProtocolValidation`.
It contains `OwnedReservoirFilePromotion`,
`OwnedReservoirRecoverableFilePromotion` and `OwnedReservoirLocalRename`, with
no fault symbols and no Core, Unity or game dependency. It is excluded from the
mod and grants no save permission.

To run it, pass the DLL to `tools/package_probe.py
--private-file-protocol-validator`, which records its SHA256. Then run
`tools/run_colony_validation.ps1 -Presentation -IdleReservoirLease
-PrivateFileProtocol -KeepOpen` with fresh package and evidence directories. The
driver verifies the hash, loads the bytes with `Assembly.Load` and archives the
JSON from:

```csharp
string MixedAtmospheres.Validation.GameMonoFileProtocolValidation.Run(string absoluteFreshParent);
```

The parent must be an absolute local NTFS directory with no other writers. The
nine checks cover the runtime and ABI, new, replacement, equal, zero-byte and
Unicode files, a share-delete reader, a deny-delete reader (error 32,
unresolved outcome, verified backup) and read-only intent inspection. There is
no cleanup, retry or rollback. Derived paths over 240 UTF-16 characters reject
before I/O, because Mono 6.13 failed every I/O case at 271–292 characters. A pass requires `Passed`, `MonoRuntimeDetected`, nine
checks and `ReportFileWriteVerified`. Windows ONI Mono 6.13 passes 9 of 9 with
paths of at most 239 characters. This proves neither full saves nor crash
durability, and it does not cover Linux.
