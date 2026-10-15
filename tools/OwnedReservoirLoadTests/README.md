# Owned reservoir rejection-only load boundary

These tests cover a dormant original header inspector, immutable load-attempt
observations and manually installed central rejection hooks for pinned ONI
build744825. They load licensed managed declarations for reflection/IL checks;
they never initialize Unity or invoke native SimDLL.

The product files are `OwnedReservoirLoadHeaderV1.cs`,
`OwnedReservoirLoadAttempt.cs` and `OwnedReservoirLoadBoundary.cs`. There are no
HarmonyPatch discovery attributes. `base.OnLoad(harmony)`/PatchAll does not
install these hooks. Only an explicit later root integration call to
`OwnedReservoirLoadBoundary.Install(harmony)` can install them. Mod.cs has not
been changed by this slice. Installation checks both exact licensed assembly
SHA256 values before patching23 exact managed overloads, and rolls back only its
own patch methods if installation fails.

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
native shutdown or stale teardown handles cannot clear it. Actual matching
Game.DestroyInstances must complete successfully, and any active original
selected call must unwind, before a successor may begin. A failed teardown keeps
the latch permanently for that attempt. Only the outer invocation that actually
began teardown receives its opaque one-use completion token. Nested teardown
gets no token; success or failure consumes the outer token, so a stale later
success cannot convert failed cleanup into completion. Attempt/call/token
identities prevent stale or foreign release.
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
It requests frontend and leaves actual native cleanup to the game's lifecycle.
SaveManager.Load cannot report success if fatal was raised during a saved-root
callback. The exact Game.Load(Deserializer) entry and exit throw while fatal,
including fatal raised by its Game.OnLoad callback, before the caller continues
Camera, WorldGrid and native offset restoration. Earlier native/object effects
are not rolled back by these rejection checks.
This separation still needs actual early-OnSpawn/direct-load Unity acceptance.

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
* Attempt fields include Id, Sequence, Origin, Input, Fatal, FrontendRequested,
  TeardownStarted/Completed/Failed, SelectedCallPending, DroppedObservations and immutable
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

The current50 host checks cover pure result/lifetime decisions and inspect the
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
