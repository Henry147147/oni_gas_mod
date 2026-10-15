# Owned reservoir rejection-only load boundary

These tests cover a dormant original header inspector, immutable load-attempt
observations and manually installed central rejection hooks for pinned ONI
build744825. They load licensed managed declarations for reflection/IL checks;
they never initialize Unity or invoke native SimDLL.

The product files are `OwnedReservoirLoadHeaderV1.cs`,
`OwnedReservoirLoadAttempt.cs` and `OwnedReservoirLoadBoundary.cs`. There are no
HarmonyPatch discovery attributes. `base.OnLoad(harmony)`/PatchAll does not
install these hooks. Only an explicit integration call to
`OwnedReservoirLoadBoundary.Install(harmony)` can install them. Mod.cs has not
been changed by this slice. Installation checks exact Assembly-CSharp,
firstpass, UnityEngine.CoreModule and Harmony SHA256 values before patching27
exact managed overloads, and rolls back only its own patch methods if
installation fails. The Unity and Harmony pins authenticate the scene callback
and normal-path callback emission used by the cleanup witnesses.

## Deliberate input behavior

* Null/empty selected path means NoSelection. New-colony/worldgen flow is inactive
  with respect to this boundary. It creates no attempt and calls no header reader.
* Existing valid unmarked native headerVersion1 inputs pass through byte for byte.
  Other positive native build numbers are not reclassified as owned. The parser
  does not claim the remaining body is valid or repair ordinary native failures.
* Every recognized owned header rejects, including a fully valid declaration.
  There is no successful restore, owner adoption, token setter, native save
  producer, Storage permit, file promotion or Core publication in this slice.
* Once an actual nonempty path is selected, malformed, truncated, unsupported
  legacy/header versions, invalid UTF8/JSON, read errors and filesystem errors
  fail closed rather than falling into cached world generation. InvalidHeader
  and ReadError do **not** claim that an owned manifest exists.

The exact top-level JSON namespace is `mixedAtmospheresOwnedReservoirs`. Escaped
JSON spelling resolves to the same key. Case variants reject. Duplicate keys
anywhere, malformed strings, invalid surrogates, comments, trailing commas,
nonfinite JSON numbers and truncated data reject instead of downgrading to
Unmarked. The same name inside an ordinary value/nested object does not assert
top-level ownership. Assigned component markers lacking this header are **not
protected by this slice**; full component-table inspection remains deferred.

The declaration requires exactly the15 fields described by the implementation:
formatVersion1, scope `two-gas-reservoirs-idle`, oniBuild744825, canonical native
colony identity matching ordinary GameInfo, ownerCount2, checkpointFormatVersion1,
checkpointSha256, envelopeFormatVersion1/envelopeSha256, coreFormatVersion3,
materialModelVersion1, catalogVersion/catalogFingerprint and both pinned managed
assembly hashes. Those fields are declarations only; no body/envelope/Core
checkpoint acceptance is implemented here.

Header inspection reads at most16+1MiB bytes. JSON depth is bounded at32 and
value tokens at65,536. Pure Inspect takes complete header bytes, not a whole.sav.
InspectFile reads only that header using a read-only FileStream with FileShare.Read;
the remaining directory/compressed body is untouched. Relative filesystem paths
are normalized to an absolute path; missing/denied/locked files and directories
are ReadError. URIs, device namespace paths, null characters, whitespace-only
paths and paths longer than32,768 characters reject as ReadError. There is no
cloud download, alternate path resolver, directory creation, save write, delete,
rename, retry, personal Resume or fallback lookup. Ordinary network filesystem
latency follows FileStream/OS behavior; no I/O deadline is claimed.

## Actual selected bytes and fatal lifetime

A path preflight is not authenticity. Every nonempty public Load(string)
invocation acquires an opaque call identity before inspection can request
frontend or synchronously reenter teardown. This includes an early owned-header,
invalid-input or read-error rejection. A separate ordinary-selected observation
retains the inspected unmarked header when preflight passes.
SaveGame.GetHeader checks a bounded copy from the actual native IReader.RawBytes
and Position before native header parsing, and requires byte equality. Changed
ordinary or owned header bytes reject. Metadata scans outside that scope, and
for other filenames, remain untouched. A repeated same-selection header callback
is unsupported and rejects. An absent actual-header observation or escaping
selected-load exception also latches failure. Both inner Load(IReader) and outer
Load(string) false results latch a failure before checking an existing fatal
attempt; the outer native loader discards the inner bool. A missing selection
does not acquire this behavior. This does not promise safety from
another mod mutating native reader arrays after a validated hook returns.

Within a selected load, Sim.LoadWorld/Load validate the actual int32 section
framing without advancing the reader. Exact MACOLONY magic rejects before native
dispatch even under an ordinary unmarked header. Ordinary payloads remain native;
this is no full envelope/native-format decoder. Without a selected load, these
checks stay inactive, including new-world generation.

`OwnedReservoirLoadAttempt` observations are immutable, with128 bounded events;
the tracker retains at most16 completed attempts. No callbacks run under its lock.
Fatal has no Dispose/reset/success bypass. A frontend request is marked before its
external callback and issued once. Inner false results, swallowed exceptions,
native shutdown or stale teardown handles cannot clear it. The completion kind
is fixed at the first failure. An existing exact Game requires its actual
Game.DestroyInstances body. A failure before Game creation qualifies for
PreGameLoaderScene only after capturing the exact live backend SaveLoader,
scene handle and existing animation updater. It requires both that loader's
inherited KMonoBehaviour.OnDestroy and that scene's
SceneManager.Internal_SceneUnloaded original bodies. There is no fallback from
failed Game cleanup to a later no-Game observation. Missing or unauthenticated
owner context remains fatal with completion kind None.

All three bodies have an entry callback, callbacks before each normal return,
and a normal-path postfix certificate requiring Harmony's __runOriginal flag.
The pinned Harmony emitter invokes postfixes after the normal original/skip
join; its exception handler invokes only finalizers. A throw inserted after the
pre-return marker cannot acquire the certificate even if another finalizer
suppresses it. The finalizer also requires a null exception and the exact
per-invocation state. This certifies that the composed original reached this
postfix; it does not certify later foreign postfix/finalizer completion. Missing,
skipped, faulted or out-of-order witnesses keep fatal latched. A one-use token
belongs only to the first actual matching cleanup invocation. Nested invocations
get their own body witness but cannot complete the outer token. Closed states
also tolerate Harmony reentering finalizers after a finalizer exception.

Both the exact public Load(string) call and any active original OnSpawn must
unwind before completion. PreGame additionally requires the two successful
witnesses to join before clearing a stale managed SaveLoader singleton
reference. The boundary checks the pinned getter/setter/DestroyInstance IL and
uses a compare-and-exchange against the exact private backing field; it never
calls the reset helper. Null is observed explicitly; a foreign successor is
preserved and fails completion. Game/App getters run before a fresh exact
attempt/capture/owner/thread and readiness check immediately before CAS. A
changed attempt, newly acquired hold or invalidation prevents the write. The
reference clear is not native shutdown or a fabricated Game teardown.
Unexpected teardown inside an active ordinary selected load latches failure
without requesting another frontend transition from the entering teardown
prefix; once teardown has begun, the tracker rejects further frontend requests.
The exact failed public call also re-clears the active save pointer in its
postfix and finalizer before releasing its hold. This prevents native autosave
name rewriting after an ignored inner false result from reintroducing a selected
path. A foreign loader/call or successor attempt cannot qualify for this clear;
clear failures are recorded and fatal remains latched.

While fatal, exact native world-load/start, saved-root creation, worldgen fallback,
Game spawn/update and native/game save entry points are fenced. Save fences throw
before the original body, rather than returning a fabricated saved path. The
helper never calls Sim.Shutdown or guesses native initialization from Grid data.
Before requesting frontend, it retires managed cell views only for the exact
current fatal attempt, activation thread and non-null managed Game reference
still equal by ReferenceEquals to Game.Instance. Installation verifies that the
pinned Game.SetIsLoading method consists only of assigning its private managed
isLoading bool. After the once-only frontend latch, that method runs before
setting Grid.CellCount to0. No array, width, height, native initialization flag
or loaded-success flag is changed; no shutdown call is added. This follows the
managed preparation used by native ForceStopGame without duplicating native
shutdown. In pinned744825, AmbienceManager's cell sampling has no Game loading
predicate, so the loading bool alone does not close the stale native-array view.
Actual StopBE/DestroyInstances still belong to the game's lifecycle.

Missing current Game records a skip without reading or writing Grid; frontend
may be requested only for the authenticated PreGameLoaderScene capture with the
exact current loader reference. A non-null foreign Game records a skip and prevents the
scene request, so neither its Grid nor its scene is changed. A stale attempt or
one whose teardown has begun cannot retire a successor's view or request its
scene transition. The immutable LastNativeViewRetirement observation records
the exact attempt/owner/thread references and prior/current counts/dimensions;
native-view-retired is separate from actual Game.DestroyInstances completion.
Grid.CellCount0 proves only view retirement, not native shutdown, teardown or a
stable frontend. Diagnostics for skipped retirement contain no Grid readings.
The original field-only IL does not exclude foreign Harmony callbacks. The
boundary therefore rechecks activation thread, exact current attempt and
ReferenceEquals(Game.Instance, expectedOwner) immediately after SetIsLoading,
before any further Grid access. Both retirement decisions sample the current
attempt after the possibly patched Game.Instance getter has returned. A changed decision stops both the Grid write and
frontend request; a skip is recorded only if the same attempt remains current.
That post-call diagnostic can retain prior readings captured before the call,
but contains no current Grid readings. A successor observation is never replaced.
SaveManager.Load cannot report success if fatal was raised during a saved-root
callback. The exact Game.Load(Deserializer) entry and exit throw while fatal,
including fatal raised by its Game.OnLoad callback, before the caller continues
Camera, WorldGrid and native offset restoration. Earlier native/object effects
are not rolled back by these rejection checks.
The v32 Windows run proved the native positive control and direct-load rejection
through actual Game.DestroyInstances and stable frontend. Its early-OnSpawn
case failed: no Game existed to complete teardown, and a zero-sized animation
grid caused a native DivideByZeroException. Those are preserved failed-run
observations, not a passing full rejection gate. The new PreGame protocol still
requires actual Unity acceptance.

While that exact PreGame attempt is pending, the DoGridProcessing prefix returns
false only for the captured existing KBatchedAnimUpdater, the still-matching
singleton, and an actual non-null rank-two controllerGrid with zero total
length. It revalidates the attempt/capture after the singleton getter. Null,
nonempty, foreign, ordinary and wrong-thread decisions preserve native grid
processing. No updater is created, Clear/InitializeGrid is not called, and no
controller grid, cell array or dimension is repaired. An unexpected late
Game.OnPrefabInit is blocked and permanently invalidates PreGame completion.
Neither no Game nor an empty animation grid attests native initialization or
shutdown; the caught native load-failure path may already have done both.

An explicit Priority.First transpiler additionally fences an already-entered
SaveLoader.OnSpawn continuation. It matches the unique direct Load(string) call
immediately followed by the exact loadedFromSave setter, then returns early when
Installed is true and the stored native result is false. The predicate does not
depend on CurrentAttempt, so synchronous teardown completing inside that call
cannot re-enable the old failed-load shutdown/frontend/worldgen continuation.
It does not fabricate success or rewrite flags. The earlier native assignment
of true and ordinary no-selection/worldgen paths remain unchanged. The pinned
body must have no exception regions; ambiguous calls, shifted setters, branches
into that setter or duplicate guards reject installation. Existing labels stay
on their original instructions. Manual installation and rollback both include
this exact transpiler; separate private counters run later and must observe the
composed early-return site rather than assume the original return count.

## Integration API for the private driver

* `OwnedReservoirLoadHeaderV1.Inspect/InspectFile` returns immutable
  `OwnedReservoirLoadHeaderInspection` with Kind, SelectedPath, FileBytes,
  ReservedNamespaceObserved, Error, Declaration, HeaderBytes/HeaderSha256 and
  CopyHeaderBytes. UnsupportedEnvelope/InvalidNativeSection observations capture
  only bounded section-prefix bytes; their byte/hash labels do not attest a full
  header or envelope checksum.
* `OwnedReservoirLoadBoundary.Install`, Installed, RejectionOnly, IsFatal,
  CurrentAttempt, LastAttempt and CompletedAttempts expose status. GuardContracts
  exposes exact target/prefix/postfix/finalizer methods for review.
* `LastNativeViewRetirement` returns an immutable
  `OwnedReservoirNativeViewRetirement` with AttemptIdentity/AttemptId,
  ActivationThreadId/ObservedThreadId, AttemptMatches, ExpectedOwner/ObservedOwner,
  OwnerMatches, Retired, nullable PriorCellCount/CellCount,
  PriorWidth/PriorHeight/WidthInCells/HeightInCells and Reason. Compare the Game
  references with ReferenceEquals; project those comparisons into JSON rather
  than serializing Unity objects. The single retained diagnostic is not a lease
  or a promise that the owner is alive; always match its exact attempt identity.
* `LastPreGameSceneCapture` exposes immutable exact loader/updater references,
  AttemptIdentity/AttemptId, SceneHandle/Name/Path/BuildIndex and the initial
  controller-grid reference/shape. `LastEmptyAnimationGridSuppression` exposes
  the attempt, captured/observed updater, actual grid/shape, thread IDs and
  monotonic Count. `EmptyAnimationGridSuppressionCount` has no reset. Project
  reference comparisons into private JSON; do not serialize Unity objects.
* Attempt fields include Id, Sequence, Origin, Input, Fatal, FrontendRequested,
  CompletionKind, TeardownStarted/Completed/Failed, SelectedCallPending,
  OnSpawnCallPending, LoaderDestroyCompleted, SceneUnloadCompleted,
  PreGameInvalidated, OwnerReferenceCleared, DroppedObservations and immutable
  Observations(Boundary/Detail/RecordedUtc). Internal properties need explicit
  projection into private JSON evidence; they are not a production UI or writer.

Run isolated outputs without touching the game's frozen package:

```powershell
& C:/Users/henry/.dotnet/sdk10/dotnet.exe build tools/OwnedReservoirLoadTests/OwnedReservoirLoadTests.csproj --artifacts-path local/reservoir-load-build/artifacts -p:OniGameDir=D:/SteamLibrary/steamapps/common/OxygenNotIncluded
& C:/Users/henry/.dotnet/sdk10/dotnet.exe local/reservoir-load-build/artifacts/bin/OwnedReservoirLoadTests/debug/OwnedReservoirLoadTests.dll D:/SteamLibrary/steamapps/common/OxygenNotIncluded 'local/reservoir-load-build/malformed-header-fixture-v1/MA Rejected Owned Header.sav'
```

The optional second argument is the preserved task-owned malformed native save;
the test inspects only its header and checks that its exact SHA256 stays unchanged.
No licensed save bytes are committed. For product-only netstandard2.1 compilation
add `-p:LoadContractOnly=true` and use a separate artifacts directory.

Current host checks are not Unity frontend-abort proof. Root owns the actual
malformed copied-save launch, native-call/object/worldgen/save counters, unchanged
input assertions and frontend observation. Full KSerialization/KSAV parsing,
pre-instantiation quarantine, all-owner restore/activation, ordinary owned saves,
crash recovery and Linux Unity remain unimplemented/unverified.

The current72 host checks cover pure result/lifetime decisions and inspect the
real patch IL, including early call acquisition, frontend decision order,
SaveManager/Game.Load continuation fences, exact-pointer clearing and matching
prefix/finalizer token signatures. A few real patch methods run only against
uninitialized managed Game/SaveLoader reference shells; no Unity construction,
lifecycle, overloaded Unity equality, native calls or first-failure frontend
callback is executed. The already-fatal result check uses the pure once-only
frontend flag to prevent callback replay. No Harmony patches are installed on
the host: the shipped Harmony patching path uses BinaryFormatter, unavailable on
.NET10. No workaround or production testing bypass is added. Actual first-false
frontend, direct/autosave path clearing and synchronous Unity teardown behavior
remain Mono game acceptance requirements.

The v6 additions exercise both witness join orders, all public-call holds,
terminal/stale/nested/skipped cleanup, a swallowed failure after the pre-return
marker, skip-original postfix rejection, duplicate and one-use certificates,
exact loader CAS and callback reentry decisions. The real three witness bodies
and transformed return labels are inspected without executing their native
effects. One default Scene value uses only its inspected managed handle getter
and conversions; no scene name, validity or unload API is invoked on the host.
CAS tests mutate local managed reference slots, not the game's static singleton.
No actual animation updater or Grid static initializer runs. Exact getter/field
contracts and the new Unity/Harmony assembly pins are independently checked.

The six view-retirement checks additionally exercise pure thread/attempt/owner
decisions, old/successor/teardown exclusion, exact field-only licensed IL,
once-latch/preparation/frontend ordering, the sole allowed Grid.CellCount store
and immutable observations. They never execute the retirement method,
Game.SetIsLoading, Grid's type initializer, App.LoadScene or native cleanup.
Their IL checks handle Debug shared-return branches as well as Release returns.
The separate Windows v32 run verified existing-Game retirement/teardown. The
later Windows v33 run verifies that path plus the typed pre-Game protocol below;
these host checks themselves do not execute either native lifecycle.
The reentry check models a callback replacing the Game, beginning/completing
teardown or admitting a successor, and separately permits a new immutable
observation of the same attempt identity. IL checks require the post-call
thread/attempt/owner decision before the sole Grid write; no Harmony callback is
installed or executed on the host.

The four additional continuation checks build/inspect transformed IL without
executing it or installing Harmony. They verify preserved original instructions
and labels, the one extra return after the selected result setter, strict
unsupported-composition rejection, the false predicate after completed teardown,
and installation/rollback wiring. The separate private
OwnedReservoirLoadValidationCounters helper is not linked into this host project;
its actual Mono installation, body/callsite calibration and composed control flow
remain part of root's private game acceptance.

Linux host/contract runs use the separately pinned licensed Linux declarations
(`Assembly-CSharp`6db65299… and firstpass117fcc69…). The product Install/header
compatibility currently requires the exact Windows hashes above and is not
enabled by those Linux checks. No Linux boundary installation or native/game
invocation is performed or implied.

Actual Windows v33 passes ordinary unmarked native reservoir save/reload and a
separate positive animation interval, followed by all three rejection epochs:
direct selected schema-99 on an existing Game, early schema-99 in backend OnSpawn
before Game creation, and ordinary-header/invalid-zlib caught native load false.
The latter two observe exact loader and backend scene body witnesses and normal
postfix certificates, all invocation holds released, dead owner reference
cleared and the narrow empty-grid guard exercised three times each. Each epoch
returns to a stable frontend; no Unity error or engine crash marker appears
through the later native menu quit. All frozen input files remain unchanged,
13 original settings/world-generation files restore, and all 283 personal save
files/membership match their prelaunch inventory. The private report, source
manifest, counters and actual Unity screenshots are retained under
`local/colony-runs/load-rejection-windows-v33`. This is Windows rejection-only
acceptance; owned restore, ordinary owned saves and first playable remain false.
