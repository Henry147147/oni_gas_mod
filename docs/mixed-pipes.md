# Mixed gas and liquid pipe contents

The playtest requires an actual composition in each gas/liquid conduit cell,
selective filtering, recipe-specific consumption, conserved heat and germs,
and exact save continuation. Ordinary new colonies enable mixed pipes and
pipe saves by default ([runtime](#runtime)). A loaded colony retains its saved
pipe mode.
Harness controls validate internal mixing/filtering/readers, precise world
receipts, pumps, valves/shutoffs, gas and liquid vents, machine ports,
teardown, phase damage, hot/cold body heat and all-ID pipe biology. The
ordinary mixed save writer uses MACPIP01 schema4 inside MACOLP03. Exact
component storage and native scheduling use the separate gated development
writer described below; complete colony continuation remains open.

## Runtime

`ColonyPipeRuntime.Start` starts every pipe module together: owned cells,
precise world ports, pumps, vents, conservative spills, machine ports, body
contact heat and all-identity biology. Contact heat and biology need pinned
native layouts; without them the carrier/projected paths remain and the
start notes say so. In production a contact that cannot be replayed exactly
(shared or missing body, changed inventory, carrier mismatch) is excluded
and counted rather than faulting; a biology cell outside the exact rule keeps
the native projected update. A loaded colony keeps the pipe mode its save
recorded: a save with a pipe section restores it, one without stays on packets.
A requested start that fails (a native/managed package mismatch) is logged as
an error, shown as a failure in the status panel, which always states the
pipe mode, and fails harness runs; the owner keeps running on packets.

ONI moves pipe packets on job workers, and the owned moves publish their
projections there too. A projection that starts or ends a conduit's germs
would add or remove the conduit's disease container in a component table that
ONI changes only on the main thread. During a network update that change
waits, latest per conduit, and the main thread applies it in conduit order at
the pipe frame end, after every network job and before ONI's own end-of-frame
publication. A save refuses a waiting change. The 30,000-frame `8bb6e66` run
faulted at cycle 8 when two workers changed that table at once.

Fixed `mixed-pipe-colony` control (new colony on profile 4 with
`MA_COLONY_MIXED_PIPES=1`, the prepared base's piped reservoirs and CO2
scrubbers, 400 frames) passes **10/10 at 401 frames** on `pipes-heat-cd5-v1`:
164 kg dispensed into owned pipes, 11 kg stored by the scrubbers, 5125
validated body contacts with none excluded, 402 biology ticks, no stuck
spill/job, every pipe account exact, owner M/E 8.5e-16/3.3e-15. ONI's
initial save was refused by the early save guard (no pipe save writer in
that run). LOW owner mean 236 ms, diagnostic.

### Activation status

New colonies start mixed pipes and save their pipe runtime by default from
2026-10-25 (user decision, [completion plan](completion-plan.md)); test runs
opt out with `MA_COLONY_MIXED_PIPES=0` or `MA_COLONY_PIPE_SAVES=0` (the
`mixed-pipe-save-guard` control needs the latter). Exact owned storage stays
explicit. The default `base` colony on profile 5 passes 4/4 and writes its
ordinary save. Earlier, default-on waited for the pipe save round trip (saves
worker: MACOLP03 section 1, cold and menu reload) and the gameplay
worker's pipe scenarios on mixed pipes. Passing in game on profile 4:
`mixed-pipe-machines`, `mixed-pipe-heat-machines`, `mixed-pipe-colony`;
earlier explicit controls cover pumps, valves, vents, filters, readers,
teardown, biology and body heat. `mixed-pipe-retire` and
`mixed-pipe-readers` pass together on core22: **3/3 and 4/4**, respectively,
at frame71. The saved native scheduling gate remains open.

## Machine ports

The native-item storage path below has a confirmed gameplay gap: a 75/25
reservoir can emit pure-species withdrawals because the dispenser selects one
item. `StorageBoundary` retains numerical differences for accounting but never
returns them to physical withdrawals; sub-float mass and fractional/minority
germs can therefore become unavailable. The historical 8/8 control checks total
output, not each withdrawal's proportions or trace recovery, and does not
establish complete mixture storage.

`ColonyOwnedStorageState` replaces this with an exact inventory at each stable
prefab-instance/Storage-component ordinal. Reservoir withdrawals transfer the
whole eligible-phase mixture proportionally; reagent-specific withdrawals
remain selective. Prepared moves reserve both endpoint inventories and exact
journals before mutation. Focused pure controls verify successive 75/25 draws,
physical 1e-240 kg/fractional/all-ID recovery after cold encoding, culture clocks,
growth/death audits and stale/inconsistent-operation refusal. Schema5 requires
both this storage section and the native scheduling section, preserving older
codec bytes.

Cumulative storage admission, withdrawal, heat and birth/death journals use
exact integer binary64 units, separate from the physical inventories. A finite
1e308-germ parcel can enter and leave repeatedly even when its cumulative
history exceeds binary64. The focused controls retain ten such cycles and
wide birth/death histories through exact cold reencoding; malformed integer
encodings refuse. This changes only the gated schema5 storage section, not
physical `PipeAmount` arithmetic or schemas1–4.

The first `ColonyOwnedStorage` adapter is an explicitly started reservoir
prototype. Native items serve UI and the existing registered thermal service;
represented work uses the registered binary32 capacity and published body
temperature. Pure readers do not credit heat; projection publication and
queued native temperature resets still require the coupled control below.
Unsupported native item/storage mutations refuse. It has no default/runtime
activation, and live saves explicitly refuse until storage biology and native
projection cold rebinding are complete. The `mixed-pipe-owned-storage` actual
reservoir control tests individual withdrawals, physical trace/passive return
and native thermal contact; this is a focused transfer control, not full
storage/save acceptance. The first actual game control passed 8/8 at sampled
frame31 on core23: each limited draw returned 1.5 kg water and 0.5 kg polluted
water; the 1e-240 kg trace and three fractional/tiny germ identities returned
physically; two native bodies exchanged -6025.932168995496 J with the store.
Store and pipe audits closed, with no storage-boundary quantities. This
measures thermal service, not coupled world/store heat closure. Package
`codex-resume-owned-storage-proto-v1` and artifact
`codex-resume-owned-storage-control-v1` are private focused evidence. Broad
machine recipes, loose bottles and teardown
still require exact adapters.

The closed `mixed-storage-heat` fixture includes the sealed world region's
U+K+G, exact stored U and every construction body's actual native registered
capacity and temperature. Actor cleanup occurs before the source endpoint;
both endpoints retain the strict actor census, zero-energy capacity probes,
native completion/publication checks and the original 128-epsilon operand
bound. The first two runs refused an unaccounted live Hatch before measuring
closure. Clean run `codex-resume-storage-heat-control-v3` passes the mass,
body-identity and heat-service checks but gains 5488.7846115393422 J against
a 4.9810592011375575e-6 J bound. The diagnostic repeat v4 gains
5947.8139015770321 J against the same bound formula. Its default-off managed
trace records exact credits and absolute native projection resets; actual
native chunk temperature/capacity before those writes remains the next causal
check. Thermal acceptance, biology and live storage continuation stay open;
no residual account, reduced thermal service or default activation is accepted.

The immutable native-observer repeat v5 measures a 4581.4294168977649 J
surplus. Its actual native chunk endpoints prove that queued absolute
temperature writes create 4584.554700762965 J across 254 frame/handle groups:
the native chunks have already cooled beyond the published temperatures used
to form their replacement values. Capacities and cells stay unchanged, every
after-state equals the last queued request, and no matching move, energy,
adjuster or removal message confounds those handles. Global registrations
are recorded separately. This establishes a projection-publication defect;
the smaller remaining difference was then unexplained and unaccepted. The
later measured 0.703 J residual is classified below by the user's decision.
The same bundle's `owned-storage-readers` repeat passes
5/5: all three species and fractional/tiny germ identities remain visible,
100 pure reads change no inventory, heat or account, and physical withdrawal
produces an explicit empty reading. Neither control enables live schema5 saves.

The 2026-10-26 receipt prototype observes actual chunk temperature/capacity
segments before `ProcessFrame` applies queued absolute data and after native
`UpdateData` completes contacts. It validates each declared replacement in
order and retains the work before a replacement under the **old** capacity.
Replacements themselves are projection edits, never heat credits. Managed
publication consumes these operand receipts instead of subtracting a requested
temperature from a later published temperature. All affected stores prepare
their exact credits before any commit or receipt acknowledgement. Repeated
reads do not consume work; foreign energy/adjuster/removal messages, stale
handles and queue exhaustion refuse explicitly.

The native boundary has pinned Linux and Windows registry/frame views; both
registry layouts and delayed cooling/multiple replacement/capacity-change
sequences have standalone controls. The coupled game control retains its
original strict operand-bound diagnostic; acceptance of the measured residual
is governed by the user decision below. Native contact arithmetic, solver arithmetic
and checkpoint formats are unchanged. Transfers concurrent with outstanding
thermal work, complete storage teardown and native cold rebinding remain open;
nonempty exact stores still refuse live checkpointing.

The first Linux game repeat with these receipts (`storage-heat-r1`, private
evidence) retains all fluid mass and the three construction-body identities.
Two registered stored-fluid bodies deliver 44 native heat receipts; stored
energy changes by **-10117.699527938385 J** over the measured hold. The closed
balance is now **-0.70290390179257045 J**, against the unchanged
**4.9810459202356767e-6 J** operand bound. This removes the former multi-kJ
missing-credit defect. **User decision 2026-10-26: the remaining 0.703 J is
non-consequential and no longer an acceptance blocker.** The strict numerical
diagnostic still exceeds its bound, and the cause remains unproved; this is
explicit acceptance of the observed residual, not evidence of exact closure
or an identified rounding mechanism. See the [accepted plan amendment](completion-plan.md).
Transfers with outstanding thermal work, teardown and nonempty cold rebinding
remain separate open requirements.

The separately activated `owned-storage-biology` control now passes 15/15 on
core23 (`codex-resume-storage-biology-control-v4`). Actual reservoirs use native
item insulation for a fixed 300 K biology comparison, independently of the
separate thermal control. One original native culture and one owned
1,000,000-germ water culture retain identical counts, float residue and cached
material/tag/threshold state through 33 real disease-manager callbacks and 457
comparisons. Logical storage tags match all 24 native tag-rule cases. A second
store evolves fractional, tiny and 1e300-germ identities with exact per-ID
growth/death audits; a 1e-240 kg-only store retains an evolved population near
9.99618e-241. All three evolved inventories withdraw byte for byte, and their
2674-byte model image reencodes exactly. This is not live game cold-rebind or
schema5 save acceptance. Earlier fixture runs retain their temperature-drift,
repeated-source and entombed-reservoir failures. Default storage biology remains
off until its remaining integration controls pass.

Every `ConduitConsumer`/`ConduitDispenser` on an owned cell keeps its native
gates, rates and storage (`ColonyConduitsMachinePorts.cs`). An input takes
rate*dt of the cell's whole mixture in proportion, capped so the accepted
part fits the consumer's space. Species with the consumer's capacity tag
(or any, or all with `Store`) enter storage as native items, one per species;
the rest is dumped exactly as an owned spill at the building cell (vanilla
`Dump`/`Destroy`), never deleted. A wrong part above the 0.1% trace fraction
triggers vanilla's wrong-element damage. Germs follow mass and merge per item
by vanilla's `SimUtil` rule. Outputs keep native item selection; the moved
germ count is applied to both sides from one value. Conditioners
(aquatuner/thermo regulator) credit the pipe with exactly what their item
lost; a duplicant emptying a pipe makes one chunk per species. The
conduction panel bridge gives the mixture exactly the negated body energy;
the morb rover maker's absorbed germs are a booked pipe germ exchange.
`ToStorage`/`FromStorage` are exact pipe-side accounts; `StorageBoundary`
books pipe-side minus observed native storage change per species, joule and
germ identity (float items, ONI merges, germ competition, conditioner heat).

Fixed `mixed-pipe-machines` control (electrolyzer, liquid reservoir, liquid
vent, a 265 K pipe and a hand-emptied pipe) passes **8/8 at 101 frames** on
`pipes-machines-v1`: 11 kg water and 4 kg polluted water stored exactly,
the electrolyzer's 1 kg polluted water dumped, 9 kg reservoir output reaching
the world through the vent, one frozen-pipe event (HP 10 -> 9), boundary
ledger 0 kg/0 J/0.93 germs, every pipe account exact. M/E 4.7e-16/4.7e-17,
no fault; LOW owner mean 375 ms (diagnostic).

Destroying a pump or vent releases its owned storage like vanilla
`DropAll`: gases enter the world as an owned spill at its cell, liquids
become one chunk per species there; late intakes follow when their job
completes. A trace
of the other phase that arrives with a pump extraction returns to the intake
cell instead of blocking the pump's buffer.

The retirement control stops the dispensers before connecting their outlets,
so native `RequireOutputs` admits real held storage. At destruction the gas
pump holds 1 kg and the liquid pump 20 kg: all gas reaches the world, all
liquid becomes chunks, no pump/spill/job remains and every pipe account
closes. The shared readers/retirement run has owner mass/energy errors
3.32e-16/8.57e-16 and an ordinary schema4 save at durable frame72.

Fixed `mixed-pipe-heat-machines` control (liquid conditioner, conduction
panel bridge, a 380 K liquid pipe and a 360 K steam/oxygen gas pipe) passes
**4/4 at 61 frames** on `pipes-heat-v4` (profile 4, pipe saves on): the
conditioner moves all 8 kg 14 K colder and the storage boundary books
exactly the mixture's 14 K heat (468047.99 J); the panel passes 5 kg and the
mixture loses exactly the body's 135721.9 J; boiling and condensing each take
one phase event (HP 10 -> 9) and empty; 106 validated contacts, none
excluded; every account exact; M/E 2.8e-16/5.9e-16, no fault. Two earlier
runs stopped on fixture setup errors (read a dispenser the conditioner lacks;
an output route that shared the input pipe made the conditioner recirculate
until the water froze); the criteria did not change.

### Readers

Element, germ and thermo sensors read the owned mixture (element true at or
above the 0.1% trace fraction). The pipe's status line on hover and in
details lists every non-trace species with its share, the mixture
temperature and, in the germ overlay, the germs; the details panel lists
the complete composition. The overlay colours a cell by its dominant
species.

### Vents

Gas and liquid vents bind their storage to owned buffers and dispatch the
exact mixture as one atomic job. A liquid vent deposits its mixture at rest
in its own cell and the solver's gravity moves it (main's decision
2026-10-24); vanilla's FallingWater drip animation is absent. Single-element
native particles from other sources are unchanged.

### Phase change

The native freeze/melt check sees only the projected dominant element, so it
is skipped for owned cells. After each conduit thermal update any constituent
above 10% of capacity (vanilla's packet rule) past its own `lowTemp`/`highTemp`
at the mixture temperature raises vanilla's frozen/boiling conduit event:
1 HP damage and the whole mixture leaves as an owned spill into the world,
where the solver handles its phase.

## Saved runtime

MACPIP01 schema 3 (assigned by main 2026-10-24) = schema 2 cells, accounts
and pending jobs, plus port buffers/world accounts, pump and vent states
(by KPrefabID), spills, machine-port ledgers, contact counters and
biology clocks. `ColonyPipeRuntime.Capture` requires quiescence (no open
transaction, no unsettled receipt group, validated native pending suffix),
reconciles every owned cell and returns the canonical image.
`Restore` runs after the owner restore and prefab spawn, before any owner
frame publishes a receipt; each owned cell's consolidated projection must
equal ONI's restored conduit contents bit for bit, otherwise loading fails.
Schema 4 adds the native culture context of every owned conduit, including
empty and germ-free pipes: captured allocation mass and element, cached growth
descriptor (all eight fields), projected disease/count and native residue.
The native allocator captures mass once; loading its current packet would
otherwise change the biological rule. Restored contexts bind once to new
temperature handles before disease updates; subsequent native cache refreshes
keep their original behavior. Temporarily unavailable biology rows retain
their owned clocks. Schemas 1–3 still decode with identical bytes, but runtime
restore refuses them explicitly because they lack this context.

`GameStarted` can precede conduit registration. Loaded contexts stay staged
until the first positive-time boundary; a synchronous topology flush precedes
binding, then every native allocation/cache/residue is checked against the
saved bits. Missing or extra prefab/native-row coverage refuses continuation.
Zero-time save publications do not advance the owned biology cadence.

The pure schema controls preserve occupied and empty contexts and legacy
schema bytes, and reject duplicate or incomplete context coverage. These are
codec controls, not in-game continuation acceptance.

Actual core23/profile5 source `codex-resume-pipe-biology-zero-source-v1`
saves at frame12 with two prefab-built, occupied pipes whose native allocation
mass is still zero. Fresh-process and menu reload each advance 15 frames on
the same immutable bundle: both restore two contexts, correct both cold
allocation masses/caches, and verify every saved native context bit after
topology settlement. Both restore the source pipe hash and saved profile;
the first cold session also writes another schema4 save. Every pipe account
is exact, owner M/E is 1.81e-16/3.86e-16, no owner fault occurs and the source
file stays unchanged. The earlier core22 readers/retirement source additionally
restores all four contexts, including its empty outlets, through cold/menu
reload. These accept native culture restoration, not full flow continuation.

ONI's conduit staging,
direction masks, one-second clock, network order and updater order still need
capture and restore. No user waiver of exact save continuation is recorded;
the earlier vanilla-load scheduling assumption does not satisfy that gate.

## Conserved inventory

`ColonyConduitInventory.cs` keeps ordered constituent masses, internal energy
and every disease identity. Exact floating-point expansions retain small
components when a larger quantity is added. Internal moves subtract and add
the same shared flux; no endpoint mass, heat or passive is clipped to close
an account. In the explicit controls, the native scalar packet is a
compatibility projection; owned composition remains authoritative.

The focused `tools/ColonyConduitTests` control checks two materials in one
cell, different heat capacities/formation energies, selective filtering with
blocked output capacity, 1,000 repeated transfers, heat, passives, evacuation,
retention of a 1e-240 kg component and byte-exact cold continuation. Native
scalar dispatch controls retain mass below float resolution, energy that a
float temperature cannot represent, and fractional/multiple germ identities.
The precise-receipt and fresh-pump controls below validate live storage and
boundary settlement. They do not establish full production port/save/thermal/
biological integration or complete end-to-end game conservation.

Initial model measurement at `56b35ea` (Release, high CPU lock, 2026-10-23): median
0.176 us/transfer and 752 allocated bytes with one species; 0.368 us and
1,584 bytes with two species. Each uses five repeats of 20,000 alternating
moves after 50,000 warm-up moves. This measures the pure inventory only;
allocation remains an optimization target after the live correctness checks.
Run with `dotnet run --project tools/ColonyConduitTests -c Release -- --benchmark`.

For the new runtime, `PipeMaterial.Id` and constituent IDs are native element
ordinals in the saved catalogue. Compatibility projections map them back to
`SimHashes`; a negative element hash must never be cast to or confused with an
ordinal. The existing catalogue fingerprint covers both `NativeIndex` and
`ElementHash`. Disease IDs inside inventories are native zero-based disease
indices; world/job passive identities are one-based. Job radius is at most
64, an injection reserves exactly its requested native constituent, and each
supplied passive is bounded by the exact per-ID durable reservation.

Schemas 1/2 were dormant groundwork: no live mixed-pipe save or writer has
been enabled. Their encoded numeric bytes remain unchanged. Any previously
activated image with an ambiguous identity convention must be refused, never
reinterpreted. The eventual live schema will tag the identity convention
alongside durable port buffers and physical scheduling state.

## Saved framing

**MACPIP01 schema7 reserved, 2026-10-27**, immediately after fetching
`2837106`: retain completed empty-store histories independently of live native
storage identities. Extend schema6 after its live-store records, before native
scheduling, with a canonical nonempty list of at most two archives ordered by
fluid phase. Each archive stores the phase, exact initial/admitted/withdrawn
mass-energy-culture journals, signed heat and per-identity birth/death journals.
It holds no inventory, native projections, culture clocks or prefab identity.
Only fully drained stores with completed native cleanup can join an archive;
aggregate journals must audit exactly. Pristine empty hosts need no archive.
Schema6 remains the writer until a meaningful completed history is retired.
The extension stays behind the exact-storage development control. No native
physical checkpoint layout or solver arithmetic changes with this reservation.

The development writer implements schema7 when at least one meaningful
completed history exists. Reservoir destruction first requires empty exact
inventory, no native items or projections, completed native retirement, and a
closed audit. Both the queued-destruction callback (which calls `DropAll`)
and final cleanup run this check. The history then joins its phase aggregate
before the live host is detached. A cold load restores these aggregates
without looking up demolished prefabs. Nonempty reservoir teardown remains
refused until its exact container adapter exists. Schema5/6 records retain
their existing bytes when no completed aggregate is present.

**MACPIP01 schema6 reserved, 2026-10-27**, immediately after fetching
`d252486`: extend schema5's native schedule with the state that native port
callbacks need after prefab spawn. Each updater gains a fixed control record
(kind, validated flags, consumer rate and last-consumed element); known
consumer/dispenser callbacks must carry their matching kind. Runtime
registration order and identities remain separately validated. This repairs
port activation being reset by `OnSpawn` before the first restored update.
The writer remains behind the existing exact-storage/scheduling control until
cold/menu continuation and storage lifecycle checks pass. No native physical
checkpoint layout or solver arithmetic changes with this reservation.

The development writer now implements this record as ten bytes per updater:
one-byte kind, one-byte flags, binary32 consumer rate and signed 32-bit
last-consumed element. Consumer flags retain on/consuming/consumed-last-tick/
satisfied state; dispenser flags retain on/blocked/empty state. Unused fields
must be canonical zero. Both flow schedules are validated before controls are
restored. Schema5 remains readable and reencodes without adding controls.

**MACPIP01 schema5** is reserved (2026-10-25), immediately after a fresh fetch
of `ea052ad`, for authoritative component-storage inventories and biology,
plus native conduit clocks, mass staging, routing and update order. These are
physical continuation state, rather than permission to discard storage
quantities into a boundary ledger or reset scheduling on load. Schema4 and
earlier codec bytes remain unchanged; the new writer stays gated until its
storage and continuation controls pass.

For a bounded development scheduling check,
`MA_COLONY_NATIVE_SCHEDULE_CONTROL=1` starts new colonies with the gated
schema6 writer. It activates the reservoir storage prototype and all-ID biology,
captures native clocks, staging, routing, networks and updater order, and binds
the saved scheduling state after the other pipe modules on restore. Binding
waits for the first positive-time barrier, after component `OnSpawn` and
conduit updater registration. `GameStarted` alone is too early: the actual
cold load exposed a tracked reservoir whose storage had not spawned yet.
Zero-time conduit updates remain held while those bindings are pending.
The loaded image selects this mode; changing the environment does not migrate
a save. Before binding, zero-time native frames settle prefab chunk-data
messages and registration callbacks, without changing the owner session or
completed frame. This prevents a queued startup write from being mistaken for
a later exact-storage mutation.

The restore path now accepts nonempty and drained exact reservoirs. Persistent
host identity, prefab, projection ordinal, phase, binary32 mass/temperature and
native disease projection must all match before any host is installed. The
saved exact inventory, energy, culture and cumulative accounts remain
authoritative. A nonempty native reservoir cannot be omitted by calling its
exact section empty. Ordinary new
colonies retain schema4 while this control is unaccepted, and schemas1–4 keep
their previous bytes.

Native storage heat ABI2 adds explicit zero-capacity retirement receipts.
Withdrawal first credits completed physical work; publication retires exhausted
native carriers after any same-callback reuse. The observer verifies the
declared final zero-capacity updates and actual handle removal, retaining all
preceding heat until acknowledgement. Unknown writes, positive-capacity
removals and saving with retirements in flight refuse. Native cleanup cannot
transfer germs into these empty display carriers. The fixed
`owned-storage-retirement` Linux control passes six checks after complete
withdrawal of its 8 kg, one-million-germ mixture: physical heat/biology occur,
all output constituents and cultures remain accounted, and both native bodies
are removed and acknowledged within the unchanged eight-frame observation.

These changes do not enable general storage mutation, demolition, bottle or
recipe ownership. The complete exact-storage writer remains development-only.

The same frozen `release-storage-probe-startup-v2` bundle passes the original
eight-check `mixed-pipe-owned-storage` source and both cold/menu loads of its
nonempty frame20 save. Its eight-frame complete comparison still fails: owner
and pipe/storage hashes first differ at frame21; particle hashes match. The
first committed owner inputs also differ there (3,867 source records versus
3,870 restored records, different multisets). This does not establish exact
active continuation or isolate the pipe difference's cause. An earlier
schema6 prototype matched the pipe prefix, but that observation does not
certify the later cleanup/startup implementation.

For private diagnosis, `MA_COLONY_CONTINUATION_DUMP=1` records the held
images. Inspect a saved pipe image without loading ONI:

```sh
dotnet run --project tools/ColonyConduitTests -c Release -- \
  --inspect-checkpoint /private/path/frame.pipes
```

The production codec validates it first. The report fingerprints cells,
accounts, boundary jobs, the remaining module runtime, exact storage and
native scheduling separately, with per-store energy, heat and culture state.
The first resumed frame in `release-storage-source-diff` / `load-diff` differs
only in the storage section: its energy/heat differ by about 0.255 J. The
native receipts show different binary32 contact temperatures and different
handle order after re-registration. This is a candidate ordering cause, not
yet a demonstrated repair or complete continuation proof.

The separate `owned-storage-active-outlet` regression freezes 8 kg at 75/25,
350 K and one million germs, 22 warm owner frames, an ordinary save with the
native dispenser enabled, then eight positive frames for scheduled discharge
and cleanup. Its initial run faults: concurrent native cooling reaches the
already emptied store. Exact-storage publication now waits for the native
frame posted by PrepareGameData before publishing callbacks. A standalone
zero-time publication with no queued NewGameFrame does not wait for a frame
that cannot start. Ordinary saves defer while Unity's end-of-frame destruction
still leaves native projection retirements in flight; the held continuation
probe likewise waits at the same owner frame before capturing.

Frozen bundle `release-active-outlet-probe-cleanup` passes all five source
checks and both cold/menu loads of its frame31 save. Each restored branch
discharges the complete 8 kg, retires and acknowledges both bodies, retains
no native items and closes the exact store/pipe accounts. The source has no
storage-boundary mass, energy or germ adjustments. Complete continuation still
differs at frame32 in both owner and pipe images. This is functional lifecycle
acceptance for the bounded fixture, not the full deterministic-release gate.
The same fixture also passes all five checks without held continuation frames
(`release-active-outlet-unheld-cleanup`).

The first synchronized `mixed-storage-transfer-heat` repeat could not observe
uncredited work at its former Unity-update sampling point within the fixed
20-frame readiness window. Its observation now runs at the storage publication
boundary before automatic credit, retaining the original warm-up, quantities,
readiness limit and every bound. `release-storage-transfer-regression-sampling`
passes all nine checks: -453.25212547322735 J is both pending and credited before
the first draw, whose 430832.35470100481 J exactly equals its quarter of the
cooled inventory's energy. The earlier unobservable run remains recorded as
failed/inconclusive, rather than being relabelled as a pass.

The separate `owned-storage-drained-save` path adds a second ordinary save
after scheduled discharge and complete native retirement, followed by eight
held frames of empty-store continuation. It checks the retained 6/2 kg
admission and withdrawal histories, negative heat, culture birth/death history
and retired culture clocks. Its engine-free negative control substitutes a
fresh empty host: that object audits, but fails the completed-history oracle.

This path exposed a false pending-work inference in the old pipe-body save
check. A body cooled during native processing and was warmed back to exactly
the earlier binary32 temperature by the already processed contact message.
Sixty-four zero-time publication frames could not change that valid equality.
The read-only conduit heat completion observer now matches the original
native messages by handle, energy and temperature bounds, then completes them
only after `ProcessFrame` returns. Other energy sources on a body remain
untouched. The queued energy identity uses the validated rounded carrier
endpoints; the contact helper's intermediate transfer can differ from that
wire value. Represented contact-work accounting is unchanged. Ordinary saves
now require this completion proof and flush any genuine pending work without
advancing the settled owner session/frame.

`release-conduit-completion-source-wire` passes all seven checks. Cold and
menu reloads in `release-conduit-completion-load-wire` both pass. All 24 held
storage sections (eight source, eight cold, eight menu) have the same hash,
retaining -12389.31653945474 J and the complete empty store's history. Complete
world/pipe continuation still first differs at frame41 from its frame40 save.
The native observer's Linux and Windows vectors, nonconsuming failure paths,
request ordering/multiplicity and original arithmetic environment have focused
controls; all 89 Linux and 89 Windows/Wine native tests pass. This closes the
bounded drained-reservoir history path, not general machine/bottle/demolition
ownership or full deterministic colony continuation.

WindowsPlayer under official Proton 10.0-4b also passes all seven source
checks and cold/menu loading (`release-conduit-completion-windows-source-v3`
and `windows-load`). Its 24 storage sections likewise have one identical
history hash, retaining -12388.80640763091 J. The runner's explicit
`--exact-storage-control` records a development override; this is bounded
WindowsPlayer evidence on Linux, not ordinary defaults or Windows OS evidence.
An earlier run correctly stripped inherited control variables and failed to
enable the gated writer; it remains a failed setup/control attempt.

The fresh Ubuntu 22.04 pair also passes the same source/cold/menu controls on
the Linux development host (`release-conduit-completion-compatible-source`
and `compatible-load`). All 24 storage sections match, retaining
-10120.250187057536 J. Both binaries meet glibc 2.34 and all 89 container CTests
pass. This is a compatibility build plus host game execution; no Ubuntu 22.04
Unity run or complete release acceptance is claimed.

The next localization found the drained-store pipe mismatch was exactly one
diagnostic byte: restore's additional zero-time contact callback incremented
`Contacts`. All other bytes before the checksum matched for eight consecutive
frames. The counter now counts timed contacts; zero-time callbacks continue
settling all represented and retained heat with unchanged arithmetic.
`release-contact-counter-source` / `load` now have byte-identical complete
pipe images for all eight frames in both cold and menu sessions. The owner
still first differs at frame40 from the frame39 save; its committed native
inputs differ there too (3,845 versus 3,853 records, different multisets).
Particles match. No image section or acceptance comparison is normalized or
omitted, and complete colony continuation remains unverified.

`owned-storage-retired-save` extends the fixed 8 kg warm-outlet fixture by
demolishing the drained reservoir after native cleanup, then making an ordinary
save. `release-retired-storage-source-v2` passes all eight Linux checks and
`release-retired-storage-load` passes cold and menu reloads. Every one of the
24 held storage sections is byte-identical: schema7, no live store, and one
audited liquid-history aggregate. Engine-free controls cover initial versus
admitted inventory, heat and culture history, wide exact aggregation, phase
ordering, malformed/truncated data and unchanged schema5/6 bytes. An injected
missing schema7 reader fails the new codec regression. The first game attempt
failed at queued destruction's earlier `DropAll`; it is retained as failed
evidence. This closes empty-host retirement only.

WindowsPlayer/Proton passes the same eight checks and cold/menu reloads
(`release-retired-storage-windows-source` / `windows-load`). Each platform's
eight complete pipe images also match source/cold/menu byte for byte. The
Linux whole-owner image still first differs at frame42 from the frame41 save;
full colony continuation and actual Windows OS acceptance remain open.

`MA_COLONY_CONTINUATION_FRAMES=N` still records a held completed owner image
after each positive frame. With `MA_COLONY_CONTINUATION_RUNTIME=1`, it also
settles native publication and receipts and records pipe/native-scheduling and
CO2-particle images at each boundary; native scheduling control is required.
`MA_COLONY_CONTINUATION_SOURCE=1` arms the uninterrupted branch immediately
after a successful ordinary `MA_COLONY_SAVE_CONTROL=save`, preserving that
source save's hash and baseline owner/pipe identities. Cold and menu loads
record the restored branch. Held prefixes now run during system scenarios too.

`tools/colony_continuation.py --source source/result.json --loaded load/result.json`
compares complete, healthy prefixes from the same save, game binaries, package,
host and player. It reports the first frame and section that differ, including
timestep operands. A restored image reencoding, short prefix, wrong baseline,
inactive pipe runtime, pending native bindings or open audit refuses comparison. A matching prefix is
bounded observation only: it does not prove identical external inputs or every
native/managed actor's continuation, and the tool explicitly keeps full colony
continuation unverified.

The private Linux `continuation-source-r5` / `continuation-load-r5` pair
passes source execution, cold restore and an in-process menu reload with the
same frozen package and source save. Both load sessions reencode the exact
saved owner and pipe images, bind all seven native culture contexts and pass
closed pipe audits. The ordinary save settles at owner frame 21; all three
branches complete the eight observed frames through 29 with the same binary32
0.2 s step operands. Both restored branches first differ from the uninterrupted
branch at frame 22 in the owner and pipe images. Cold and menu branches first
differ from each other at frame 23 in the owner image. Particle images stay
empty. Thus restoration succeeds but byte-exact active continuation fails;
these observations do not establish identical native/managed inputs or assign
the mismatch to one particular actor.

Owner wrapper13 now preserves callback-owned pending requests and section4
names their persistent actors for rebinding ([owner saves](colony-owner.md#saves)).
A missing, extra or differently sized actor section refuses before native load.
Durable pipe jobs remain saved separately. Exact active continuation still
needs identical future inputs as well as the recorded pipe state: current
source/load journals differ in breathing, pump and solid-heat inputs after
loading. Flushing future requests early at save would change the uninterrupted
trajectory.

**MACPIP01 schema4** was reserved (2026-10-24), immediately after a fresh fetch
of `c57532a`, for complete native pipe biology context: captured allocation
mass/element, cached growth rule, projected culture and residue, including
empty conduits. Schema3's codec bytes remain readable; missing context cannot
be reconstructed as exact runtime continuation. Native flow scheduling and
full cold/menu continuation remain separate requirements before activation.

Format IDs **MACOLP02** and **MACPIP01** were claimed immediately after a
fresh fetch/rebase of `b36ed23` on 2026-10-23. These are managed payloads;
they do not change a native MAC checkpoint version or its bytes.

Pipe images retain every species, joule and germ and owned buffer, culture,
port and job state. Native conduit staging, directions, the 1 s clock and
network/updater order remain required for exact continued evolution. A restore
refuses unless each owned cell's consolidated projection equals ONI's
restored `ConduitContents` bit for bit; this alone is not continuation proof.

**MACOLP03** was assigned by main after a fetch at `f8eb87a` (2026-10-24): config, owner image, then tagged sections in increasing tag order (1 = pipe runtime, currently MACPIP01 schema 4; 2 = MACO2P01 in-flight CO2 particles; 3 = colony settings record, [settings](settings.md#save-record); 4 = MACCBR01 actors of callback-owned requests in flight, [colony owner](colony-owner.md#saves)).
Unknown, duplicate or empty sections refuse; a save without sections still writes exact MACOLP01 bytes; MACOLP02 stays refused.

- **MACOLP01:** unchanged magic, int32 configuration length, 56-byte
  configuration, then the existing owner image. An absent pipe image keeps
  exactly these bytes and the existing packet behavior.
- **MACOLP02:** magic, int32 configuration length, 56-byte configuration,
  int32 owner-image length, owner image, int32 pipe-image length, pipe image.
  The existing `MACOLONY` save envelope hashes the entire payload.
- **MACPIP01, version 1:** magic, int32 version, six uint32 geometry fields
  (global width/height, world x/y/width/height), SHA256 catalogue identity,
  ordered pipe cells, initial/supplied/removed inventories, external heat and
  disease exchange accounts, followed by SHA256 over the complete preceding
  image. A pipe cell records its gas/liquid phase, global cell index, ordered
  masses, U and germs. Each conserved amount stores its exact ordered binary64
  atoms. Counts, lengths, phase, geometry, identities and canonical ordering
  are checked before publication; the image is limited to 256 MiB.

The runtime writer emits MACOLP01 without extension sections, otherwise
MACOLP03. Mixed-pipe saving requires `MA_COLONY_PIPE_SAVES=1`; MACOLP02 stays
refused. No old save migrates
because an environment variable or a new default changes. The codec controls
check exact legacy bytes, new-image re-encoding/continuation, incompatible
catalogue/geometry, malformed lengths, duplicate cells, corruption and the
outer envelope's coverage of the pipe payload.

**MACPIP01 schema 2** was claimed after fresh fetch/fast-forward
`2129f2b` on 2026-10-23. Schema 1 remains readable and re-encodes with its
original bytes. Schema 2 appends named unreleased-boundary-heat and exact
uint64-identity passive representation-remainder accounts,
four exact mechanical exchange accounts (K, gravitational energy, x/y
momentum), next local-job/native-token watermarks and ordered durable port
jobs. Mechanical accounts use sparse balanced signed base-2^32 digits with
arbitrary-precision decimal bin indices; huge exponents and small residuals
survive exact cancellation and checkpointing.

A job stores its local identity, native token, pipe cell/phase, actual world
request cell, native species, operation/radius, requested mass/temperature,
reserved inventory and every uint64-identity/binary64 passive. Token zero
means prepared and unsubmitted; a positive token means already queued in
the native checkpoint. Only injections (1), gas extractions (3) and liquid
extractions (4) are accepted. Jobs, watermarks, phases, geometry, reservations,
passive ordering and byte/count bounds are validated. Committed outcomes must
be credited before both save images are captured; receipt sequence numbers
are transient and are never restored as credit identities. This codec does
not yet authorize live output, saving or migration.

## Explicit internal-flow fixture

The `mixed-pipes` and `mixed-pipe-filter` harness scenarios activate internal
ownership; separate catalogue, boundary and pump controls are described below. It
reuses ONI's conduit graph, native branch scheduling, bridge/valve rate
decisions and callbacks while moving actual owned mixtures. External scalar
inputs/removals/dumps, cross-world scalar edges and saving are gated while
the boundary/job integrations are unfinished. Ordinary colonies and old saves
retain their existing packet behavior.

The first fixture failed its branching check because both branches ended
without consumers; each bridge's complete mixture remained in one output
cell. The corrected fixture keeps the same requirement of at least two
downstream mixed cells per phase, adds real consuming bridge endpoints and
supplies several packets. On the immutable `pipes-core-a643-v2` bundle
(native precise sidecar `529744c`), all five checks pass after 26 frames:
one-cell gas/liquid composition, bridge/native branching and exact pipe
mass/U/every-germ accounts. Gas reaches two mixed cells (2 kg), liquid three
(30 kg); supplied/held mass is 32 kg, removed mass and U residual are zero.
This fixture exchanges no heat and does not prove live conduction, phase
change, filters, machines, external ports or save continuation.

LOW-lock diagnostic timings with concurrent HIGH work allowed: owner
mean/p95/max 171.766/208.443/233.359 ms (26 samples); observed report-completion
spacing 328.146/287.490/3019.882 ms (25 contiguous samples, 10 ms polling,
including arena/build work). These are fixture diagnostics, not performance
acceptance. World mass/energy errors are 1.126e-16/5.552e-17, trace sink is
0 kg/0 J, no owner fault occurred, and process cleanup was verified. The
colony report does not expose a fallback count. Installed-game contract
controls retain every original native move instruction, reject unknown
layouts and verify cached access and legacy/boundary behavior.

The subsequent `pipes-heat-732-v1` bundle uses rebuilt native metadata/capability
2 and passes all eight checks after 26 frames. Gas mixing gives
303.3540374939621 K and liquid mixing 306 K, exactly matching the analytic
heat-capacity weighted temperatures. Native conduction exchanges
-737.16118018637644 J and native disease 1 changes by -14,591 germs; the
32 kg inventory, U and every germ account still close exactly. World
mass/energy errors are 6.678e-17/4.995e-17, trace sink 0 kg/0 J, no owner
fault, verified process cleanup. Diagnostic owner mean/p95/max is
210.356/260.087/272.580 ms; observed spacing 383.958/400.380/3178.467 ms,
with the same polling, startup/build inclusion and concurrent-HIGH caveats.
Fallback counts remain unavailable. Valves, machine recipes, vents,
destruction, all-disease evolution and complete save continuation remain
separate acceptance work.

The `mixed-pipe-filter` scenario adds actual powered gas/liquid filters,
element sensors and a germ sensor. The first run passed one-cell composition,
unblocked filtering, all sensor cases and inspection, but failed four
partial/blocked-output checks: the native room gate required empty outputs.
The correction preserves its existing power/status/history state machine
while supplying actual selected/nonselected output capacity. The criteria
remain unchanged. Immutable `pipes-filter-732-v4` passes all 17 checks after
33 frames: only the selected species reaches its filtered output; each
output fills independently; blocked remainder stays in the source; minority,
trace and exact-threshold element sensing agree with the world threshold;
the germ sensor sums all identities; inspection shows full composition.

The final gas source retains 0.125 kg oxygen and 0.25 kg carbon dioxide;
the liquid source retains 1 kg water and 4 kg polluted water. Supplied/held
mass is 61.117187362106051 kg, removed mass and exact U residual are zero;
every mass, heat and germ account closes. World mass/energy errors are
1.021e-16/4.839e-17, sink 0 kg/0 J, no owner fault, cleanup verified.
Diagnostic owner mean/p95/max is 225.884/284.354/498.647 ms (33 samples);
observed spacing 373.319/516.006/3271.280 ms (32 contiguous samples), under
the same polling/startup/concurrent-HIGH limits. Fallback count is unavailable.
The model exclusion/capacity and installed-game native staging adversaries,
six pure groups, five hook groups, 37 inspection checks, 135 harness checks
and 83 native tests pass. A one-ulp compatibility correction now preserves
native movable mass, so newly received material is not advanced prematurely.
No production pipe activation or mixed save writer is enabled.

All ordinary inspection is read-only. Heat/germ reconciliation occurs at
deterministic pipe/thermal update boundaries, never because a hover or audit
was opened. A 100-read adversary with changed native temperature leaves the
inventory and reconciliation state unchanged. Scalar sums also normalize
to an order-independent exact expansion; 128 permuted reductions and wide
integer-oracle cases preserve bytes and correctly resolve half-ulp traces.
Previously encoded valid expansion bytes remain unchanged on re-encoding.

## World boundary settlement controls

`ColonyConduitSettlement.cs` computes a complete native job outcome before
changing any pipe account. Injection consumes only the committed mass and
returns undispatched reservation; its native U is authoritative and the
energy difference stays in a named boundary heat account. Actual sequential
passive observations supply the world-side quantity. Rounded-away increments,
erased old low parts and fractional quantities remain in the exact passive
representation account. Extraction retains every committed species, U and
passive, including quantum-cleanup constituents. Mechanical terms remain in
exact sparse accounts rather than becoming heat or overflowing a double.

The seventh pure control group covers full/partial/refused injection,
1e-240 mass and heat remainder, tiny/huge/fractional passive changes,
multiple extraction outcomes, duplicate/missing outcome watermarks and
unchanged replay bytes. A sealed save-boundary validator checks the pending
native-token suffix with at most k+2 queries: every pending token queued,
its predecessor retired and the next watermark unaccepted. Missing first,
middle or last jobs and stale watermarks are rejected. Its caller must first
pause submission, wait and drain/acknowledge committed outcomes; validation
does not create that barrier. These are model controls, not live component
ports or mixed-save acceptance.

The immutable `pipes-boundary-7e6-v1` fixture passes all 12 checks after
29 frames. Four constituent injections deliver 11 kg with simultaneous
fractional/tiny germ identities. Grouped phase extractions return three gas
constituents (1 kg) and two liquid constituents (9.9577839173929856 kg), each
with two germ identities, into actual mixed pipe cells. The gas capacity
boundary retains 1.3593399390905969e-17 kg in its owned port buffer. Zero
extraction also retires its token; the empty pending suffix and next token 8
agree with the native queue. Mass, U and every germ account close exactly,
including -0.055509135519969277 J retained boundary heat, two passive remainder
identities and exact K/G/J accounts. Later pipe conduction exchanges
22.85902061863537 J without changing that closure.

World mass/energy errors are 1.337e-16/3.694e-17, sink 0 kg/0 J, no owner fault,
cleanup verified. Diagnostic owner mean/p95/max is 190.783/236.985/238.318 ms
(29 samples), observed spacing 344.699/448.890/3071.215 ms (28 contiguous
samples); startup/polling/concurrent-HIGH caveats apply. Fallback count is
unavailable. Release83, seven pure groups, five hook groups, 37 inspection,
209 snapshot/backend controls and 135 harness checks pass. Receipts are
credited before legacy acknowledgement; inactive colonies avoid all optional
pipe receipt queries and acknowledgement allocations.

This boundary fixture is explicit and does not enable component ports. The
separate pump control below adds bounded component coverage. Vents, native
storage/recipe adapters, destruction, pipe phase behavior and saved physical
scheduling remain open. ONI's ordinary conduit save omits
transient movement staging, fairness directions and timers; an inventory-only
round trip does not establish deterministic gameplay continuation.

## Catalogue and real pump controls

The explicit `mixed-pipe-catalogue` control routes all 29 registered gases and
49 liquids through actual owned conduit cells, preserving two simultaneous
germ identities and exact inventory/heat accounts. Nineteen gas and 27 liquid
compatibility hashes are negative; authoritative identities remain native
ordinals. Immutable `pipes-catalogue-e652-v1` passes 5/5 checks in nine frames,
retaining 0.000078 kg in two buffers. This checks identity/routing, not every
material's phase or thermal behavior. World mass/energy errors are
5.902e-17/4.637e-17, trace sink zero, no owner fault, cleanup verified.

`mixed-pipe-pumps` owns fresh gas/liquid pump storage and queues their complete
phase extractions as durable jobs inside one ordered native actor batch.
Ordinary A/pump/B requests cannot cross a frame boundary between separate
enqueues. The optional batch capability is required before intake activation;
inactive colonies keep their original final actor batch. Native registrations,
power, rates, output requirements, consumed/stored notifications and
accumulators remain active. The original callback reaches its storage boundary
once, where owned inventory replaces the scalar packet write. Storage readers
and inspection report that actual inventory. Outlet publications and buffer
changes invalidate derived empty/blocked status without moving material.

The 25 fixed checks cover mixed single-cell outputs, both germ identities,
empty scalar storage, native notifications/accumulators, blocked storage
capacity, partial outlet refill, resumed intake, disabled intake with real
room, and exact mass/U/germ/mechanical accounts. Native `RequireOutputs` waits
for an empty outlet; the fixture honors that gate. Its first live v2 run passed
21/25: four setup assumptions incorrectly expected intake through a nonempty
outlet. Corrected stages fill storage with the dispenser off and outlet empty,
then restore an exactly full outlet using explicitly booked fixture supply.
V3 and v5 passed 24/25 and v4 passed 23/25: the remaining failures exposed stale
blocked indicators. A direct installed-component control now checks inactive
full storage, reopened room and emptied buffers without changing inventory.
V6 stopped during setup before the pump checks because native topology was not
ready; bounded readiness now precedes activation and shifts later stages by the
same delay. These failed runs do not establish pump acceptance.

Immutable `pipes-pumps-736-v7` passes all 25 checks in 71 completed frames.
Seventy original stored notifications run; blocked buffers hold exactly 1 kg
gas and 20 kg liquid beside exactly full 1/10 kg outlet cells. Partial outlets
refill without overflow. Queued intake rises 35 to 42 after the outlets empty,
then stays 42 after disabling pumps with actual intake room. Final held mass
is 35.250078 kg, fixture supply 0.20007798807907107 kg, extraction
35.050000011920929 kg; all pipe accounts close exactly. External pipe heat is
185.04549009075419 J, germ-1 exchange -16, two passive remainder identities
and all mechanical accounts retained. World mass/energy errors are
9.591e-16/5.563e-17, trace sink 0 kg/0 J, no owner fault, cleanup verified;
fallback count is unavailable. Receipt control compares 3155 byte-equal entries
in 128 nonempty/64 empty queues, with no prefix growth. Paired native-call means
are 0.002326 ms pinned and 0.487217 ms marshalled over 192 calls.

Diagnostic LOW owner mean/p95/max is 387.155/753.413/784.952 ms (71 samples),
observed 553.488/906.194/4904.638 ms (70 contiguous samples, no gaps). These
include startup, polling and receipt instrumentation; concurrent HIGH work was
permitted. They establish behavior, not a whole-game speed gain. Seven pure
groups, six installed hook groups, 37 inspection, 209 snapshot/backend and 135
harness checks pass. The native batch implementation independently passes the
parent's 83 CTests and concurrent ordered-batch controls.

Completed pump intakes now coalesce into one exact storage inventory before
notifications/discharge; pending reservations retain their independent job
identities. The previous buffer list could discharge an older arrival mixture
instead of the aggregate. A seventh installed hook control combines two
different compositions and two germ identities, checks byte-exact combined
inventory and proportional quarter discharge, and rejects duplicate mixing
without mutation. `pipes-storage-340-v1` passes 27/27 live checks in 71 frames:
the original 25 plus gas/liquid aggregate partial discharge. Gas storage
1 -> 0.75 kg has largest species residual 5.551e-17 kg; liquid 20 -> 17 kg has
zero residual. All per-species/germ/U proportions and exact pipe accounts pass.
Seventy-two native stored notifications still run; final port buffers fall
from 13 in v7 to 7 (including auxiliary fixture buffers). This is a correctness
and bookkeeping change, without a whole-game speed claim.

That run's world mass/energy errors are 2.744e-16/2.482e-13, trace sink zero,
no fault, cleanup verified; fallback count unavailable. Diagnostic LOW owner
mean/p95/max 198.703/376.249/415.441 ms (71 samples), observed
285.590/421.944/3046.178 ms (70 contiguous samples, no gaps), controls off.
Seven pure/seven hook/209 snapshot and 135 harness checks pass. All pipe runs
above use the unchanged legacy owner profile, Config.Flags=1; they do not select
`MA_COLONY_GAME_FLOOR=1` or establish game-floor/profile acceptance.

### Exact inventory cost

`PipeAmount` now uses TwoSum directly for two single atoms and retains an
already-canonical two-atom pair. It preserves exact sums and serialized atoms;
it changes neither fluxes nor saved physics. Private baseline/candidate seven
pure groups and a 9000-state serialized corpus agree (SHA256
`5F28739BF325A89F5D33FE8BFE4B89A4EAD4BC2B5A78C65264EDB4EB484C0F45`).
WHOLE CPU-only model measurements use five sets of 20,000 transfers after
50,000 warm-up moves, with the material delegate cached on both sides:

| Constituents | Before / after, microseconds per transfer | Before / after, bytes allocated |
| --- | --- | --- |
| 1 | 0.548 / 0.310 | 2332 / 1044 |
| 2 | 1.015 / 0.660 | 4652 / 2556 |
| 6 | 2.317 / 1.179 | 10948 / 4752 |

These are .NET 10 model medians and allocations, not Unity/whole-game speed
gains. Main seven pure, seven installed hook and 209 snapshot groups pass.
Immutable `pipes-account-e3f-v1` also passes all 27 live pump checks in 71
frames on the same legacy profile/native pair. Every pipe account remains
exact; world mass/energy errors 6.614e-16/3.478e-17, trace sink zero, no fault,
cleanup verified, fallback count unavailable. Diagnostic LOW owner
mean/p95/max 189.865/272.293/400.121 ms, observed
271.873/383.998/3078.944 ms (71/70 samples, no gaps), instrumentation off.
Concurrent-HIGH/startup caveats remain; this is a behavior regression check.

Full production activation and mixed saves remain off. Finite-body mixed-Cv
equilibrium, deterministic germ growth/death, foreign-phase cleanup returns,
phase teardown and saved component scheduling still need acceptance controls.

The finite-body control `mixed-pipe-thermal` is defined before its first run:
an isolated native radiant gas pipe at arena coordinate (3,3), surrounding
vacuum, no connected source/sink, and its real construction mass/material heat
capacity. At setup stage 8 the body is heated to 400 K; six frames later the
owned cell receives exactly 0.5 kg oxygen plus 0.5 kg hydrogen, both at 300 K.
Topology readiness may delay setup by at most 22 frames, shifting every later
stage equally. After 586 further frames, compare both temperatures with
`(Cbody*Tbody_initial + Cmixture*300)/(Cbody+Cmixture)`, requiring each within
0.1 K. Compare actual body heat loss with mixture U gain; its measurement bound
is two binary32 endpoint ulps of body temperature plus 1e-7 of fluid heat.
Also require both original species masses and exact pipe-account closure.
Report the body's real mass/Cv, both species Cv, temperature endpoints, heat
partition, booked heat and residual. A closed pipe ledger alone is insufficient.

First immutable run `pipes-thermal-340-v1` stops at owner frame 456 before the
final check: an unrelated whole-colony consumer at (103,333) extracts 1e-6 kg
CO2 beside brine/water traces and faults with no positive thermal energy above
reference. The result/Player.log detect the fault even though the last completed
frame's live report has an empty Error field. Cleanup passes; no private dump
was enabled. Actual initial body is 25 kg, Cv 9700.0002861022949 J/K; oxygen and
hydrogen Cv 1004.9999952316284/2400.0000953674316, analytic equilibrium
385.06906383847286 K. Final thermal metrics remain unresolved, with their
criteria unchanged. This run also uses legacy Config.Flags=1. Parent owns an
opt-in transfer-stage capture for a same-profile replay; a scoped arena would
be an explicit fixture change, not removal of this full-colony failure.

The unchanged parent capture bundle `79e0545`, legacy Flags=1, reaches frame
601 without a native fault or either transfer-capture file; the original 456
failure is not erased. Its thermal checks pass 3/5 with the original naive body
capacity: observed body/mixture 353.2601318359375/353.2594036595591 K, body heat
-453376.7345637898 J versus fluid/booked +90674.137143025291 J. Inspection of
the actual registration contract then identifies a fixture error:
`StructureTemperatureComponents.SimRegister` uses
`BuildingDef.MassForTemperatureModification`, not physical construction mass.
The corrected fixture reads that declared mass and reports both. A 5 kg thermal
mass predicts 353.2601236134025 K, but still leaves approximately -1.20977 J
body-plus-fluid residual, above the original two-endpoint-ulp bound. No energy
bound is loosened, and thermal acceptance remains unresolved. Capture-run world
mass/energy errors 1.772e-15/5.731e-17, trace sink zero, cleanup verified.

Opt-in `MA_COLONY_PIPE_THERMAL_LOG=<fresh private path>` now records each
exchange around native `ConduitTemperatureManager.Sim200ms`, before owned
reprojection: native pipe mass/T, shadow T/carrier Cv, registered body thermal
mass and native-float Cv, actual structure-record/callback/backing temperatures,
pending kJ, exact owned U change and inferred carrier work. It allocates/writes
only when selected, refuses an existing file, and is disabled in final timings.
Its `.work` log observes exact arguments to managed
`SimMessages.ModifyBuildingEnergy` and `StructureTemperatureComponents.ProduceEnergy`,
with body/source/thread/context; channels must not be double-counted. `.meta`
records whether both observer prefixes are installed. Whole-body snapshot
changes remain explicitly unattributed outside the proven isolated fixture.

`pipes-thermal-probe-34f-v1` reaches 601 frames and passes 4/5 checks with the
declared 5 kg thermal mass. Body/mixture 353.26016235351563/353.25942167434033 K
agree with analytic 353.26012361340253 K; original energy bound still fails:
body -90675.28770865465 J, fluid/booked +90674.16781319116 J, residual
-1.1198954635 J versus 0.2458838300 J. All 587 exchange rows have exact owned
delta-U equal to inferred rounded-carrier work (maximum residual zero), but
every synchronous structure-temperature delta is zero: body work publishes
later. The backing InternalTemperature stays at 400 K and is not the runtime
temperature. World mass/energy errors 1.746e-15/5.618e-17, trace sink zero,
no native fault/captures, cleanup verified. Diagnostic LOW owner mean/p95/max
180.430/199.774/430.519 ms, observed 231.271/315.350/3002.908 ms
(601/600 contiguous samples, no gaps); no performance claim.

The requested-work preflight `pipes-thermal-work-v2-run` intended 40 frames but
overshot to 151: named scenarios control their own completion and ignore that
frame setting; its external stop came late. This missed bound is recorded,
not called a completed acceptance. Game-group cleanup is verified; interruption
wrote no canonical final result. Both prefixes are installed, yet their work
file contains zero calls for the observed body, including out-of-context calls.
Its 136 hot exchanges book/infer exactly 90484.47283728456 J. The existing native
thermal result exposes temperatures/freeze/melt handles, no shared-energy
array. Exact requested body work or its native queue is the concrete missing
access; no invented representation account or relaxed bound is introduced.
Further probing stops pending that handoff; other pipe integration continues.

### Biological update still to implement

Current native reconciliation evolves only the projected germ identity;
minority identities remain in inventory but do not yet grow or die. Owned pump
buffers also need biological updates. Retention is not complete biological
acceptance.

Proposed mixed-medium rule, not adopted: on the fixed native disease tick,
evaluate each identity independently using ONI's public element-growth and
temperature functions. For a mixture, evaluate each constituent's material
rule against the whole cell's population and mass, then combine material
changes by exact constituent mass fractions; apply the temperature term once
at the mixture temperature. This reduces to the native material rule for a
single constituent. It does not distribute germs among fictitious native
packets. Each identity needs its own saved native rounding accumulator plus
exact sub-integer/overflow representation; every birth/death is booked to
that identity's named biological exchange. Native-compatible single-species
integer populations must retain the exact native count/rounding trajectory.
The extension outside native integer range, fractional populations, storage
tick timing, and mixed-medium truth remain unresolved. No rule is selected
until these limits and single/native versus mixed controls are established.

### Atomic output control

The next explicit `mixed-pipe-output` setup is fixed before measurement: two
isolated source cells hold gas 0.375 kg O2 + 0.625 kg CO2 and liquid 3 kg water
+ 7 kg polluted water, each pair carrying germ-0 quantity 0.25 and germ-1
quantity 1e-240. Move each full mixture into an owned buffer. A deliberately
out-of-rectangle native admission must preserve the exact source object and
both token frontiers; then a valid output atomically queues its two constituent
jobs with all passive slices. The source debits only after strong native batch
admission. After receipt settlement require both constituents of each buffer
to share a physical completed frame, all four exact masses to reach the world,
both germ identities to be booked, contiguous retired tokens and exact
pipe/boundary accounts. Undispatched reservation/remainder stays owned.
Completion-frame instrumentation is allocated only by this explicit control
and is absent from normal runtime. This is an output primitive for vents and
teardown, not completed component integration or save acceptance.

Immutable `pipes-output-79e-v1` passes all 10 checks in 19 frames, using the
SHA-identical parent `79e0545` native pair and legacy Flags=1. Both rejected
batches preserve source identity and unaccepted tokens 1/3. All four accepted
jobs complete in physical frame 13, delivering exactly 11 kg and germ quantities
0.5/2e-240; no mass/germ remainder remains. The named boundary heat account
retains -0.055493040752480738 J and the mechanical G account is retained; every
pipe/boundary account closes exactly. Native token 5 is unaccepted after all
outcomes retire. World mass/energy errors 5.899e-17/4.933e-17, trace sink zero,
no fault, cleanup verified, fallback count unavailable. Seven pure/seven hook,
209 snapshot and 135 harness checks pass. Diagnostic LOW owner mean/p95/max
181.436/261.063/261.063 ms, observed 393.974/3135.041/3135.041 ms (19/18
contiguous samples, no gaps), controls off. No performance acceptance is claimed.

### Conservative teardown control

`mixed-pipe-teardown` is defined before its first measurement. Its 8x6 arena
has a sealed solid region x=2..6, y=1..5, a central blocked spill cell (4,3), and
open spill cells (0,0)/(7,0). Each phase has one pipe in its open cell and one
in the central solid cell. Seed every gas pipe with 0.375 kg O2 + 0.625 kg CO2,
every liquid pipe with 3 kg water + 7 kg polluted water, and each mixture with
germ quantities 0.25/1e-240 at 300 K. Demolish actual native prefabs at stage
10. At stage 18 require all native conduits removed, 11 kg/four species spilled,
11 kg held in two refused spills, complete cell inspection, no repeated retry
within a physical frame and exact accounts. At stage 20 open the central cell;
by stage 32 require all 22 kg released, no pending/root/buffer inventory, hidden
spill inspection, all germ quantities delivered/booked and exact accounts.

The native dump receives its scalar contents by value. After reserving the
complete owned mixture, suppress that scalar emission while its caller retains
normal cleanup. Refused spill buffers remain owned at their source coordinate;
completed returned constituents merge before retry. Retries use completed
owner frames and stable IDs, with no wall-clock load input. Hover/selected-cell
details label held contents "Fluid waiting for space" without replacing native
solid descriptions. Inspection cannot dispatch or alter inventory. This remains
explicit-only; phase damage/returns and saved retry scheduling are separate
acceptances, not established by a warm open-spill check.

Immutable `pipes-teardown-ffd-v1` passes all 10 checks in 33 frames, legacy
Flags=1/SHA-identical `79e0545` native pair. Four real native prefabs disappear;
11 kg spills, 11 kg remains in two sealed spill trackers. The cell reader
reports both phases/all four constituents, 100 reads and repeated same-frame
ticks keep attempts 18 -> 18. After opening the cell, all 22 kg and germ
quantities 1/4e-240 reach the world, with zero held/pending/root state and no
remaining spill inspection. All accounts close exactly; retained boundary
heat is -0.095180099629942561 J and mechanical G remains booked. World
mass/energy errors 1.800e-16/5.583e-17, trace sink zero, no fault, cleanup
verified, fallback count unavailable. Seven pure/seven hook/37 inspection,
209 snapshot and 135 harness checks pass. Diagnostic LOW owner mean/p95/max
175.969/219.020/228.368 ms, observed 311.533/458.943/2982.569 ms
(33/32 contiguous samples, no gaps); no speed claim. Phase-induced pipe damage,
latent/foreign-phase returns, biological evolution and saved retries remain
open, so production activation stays off.

### Valve/shutoff acceptance

The fixed `mixed-pipe-valves` control builds manual valves and powered shutoffs
for both phases, with separate single-cell inputs/outputs and passive sinks.
Each gas input starts 0.375 kg O2 + 0.625 kg CO2; each liquid starts 3 kg water
+ 7 kg polluted water, with fractional/tiny germs at 300 K. Native topology
readiness can shift every stage equally by at most 20 frames. Hold closed for
10 frames, then set manual rates 0.1/1 kg/s and enable native powered shutoffs
for 10 frames. Manual output should be 0.2/2 kg within one native tick
(0.021/0.21 kg); shutoffs transfer full 1/10 kg within 1e-6 kg. Require original
mixture proportions within 1e-10 and zero transfer after closing. Fill manual
outputs to leave 0.01/0.1 kg room and verify full capacity/no overflow within
1e-7 kg. Empty shutoff outputs into owned sinks and refill inputs while still
disabled, proving closure with real output room; reopen and require full mixed
transfer. Final exact mass/U/germ accounts must close. No production setting
or native valve decision is replaced by this control.

The first setup timed out before initialized prefab ports were available. A
second setup creates the pipes after initialization; it reaches flow checks
but fails 9/15 at frame 35. Gas manual flow is 0.200000003 kg, liquid manual
flow 1 kg against the 2 kg expectation, and both shutoffs remain blocked by
their native automation controller. Refilling that still-full input is
correctly rejected. These are unresolved fixture/acceptance failures, not a
valve pass. Native world mass/energy errors are 2.957e-16/3.612e-17, trace sink
zero, no native fault, cleanup verified. Diagnostic LOW owner mean/p95/max
312.90/433.01/462.11 ms, observed 531.64/542.33/5095.39 ms (35/34 samples,
no gaps); no performance claim. The next setup supplies the unconnected
automation value through its initialized native controller and records native
rate/max, ports and elapsed game clock without changing the flow criteria.

The corrected `pipes-valves-e96-v4` control passes **23/23 in 45 frames**.
Both phases observe two native valve calls with dt sum 2 s (game-clock elapsed
1.933303833 s). Manual output is 0.200000003/2 kg and powered shutoffs deliver
1/10 kg, all with the original two-species composition. Closed leakage is zero;
partial outputs are 0.999999999776/9.99999999404 kg, with exact capacity never
exceeded. Disabled shutoffs retain full inputs while their outputs have real
room, then reopen with complete mixtures. All 41.689999997 kg and germ accounts
close exactly, with zero U residual. The change supplies the fixture's native
automation controller after initialization; it changes no production rate or
operational decision. The prior liquid 1 kg cause remains unproven because its
native clock was not recorded; the new observer is explicit-fixture-only.

World mass/energy errors are 5.050e-16/4.767e-17, trace sink zero, no fault,
cleanup verified, fallback count unavailable. Diagnostic LOW owner mean/p95/max
205.538/302.927/369.177 ms, observed 322.030/431.218/3071.469 ms (45/44
contiguous samples, no gaps); no performance claim. This does not activate
production mixed pipes or establish vent, thermal, biology or save acceptance.

### Vent surface-germ preparation (explicit controls only)

A small pure helper prepares the vent's surface exchange separately from
conduit scheduling. For a single integer population it preserves the native
integer-to-binary32, 5% scaling, then integer truncation sequence in each
direction. For a mixture, each supported fluid identity is sampled in stable
identity order using that same rule. Fractions and quantities beyond native
integer range stay in the fluid; total incoming counts are capped against
remaining body integer capacity before any native mutation. Empty fluid never
receives a massless germ population. Mass and heat are unchanged.

The building still has exactly its observed native single germ identity.
After native mutations, each identity's actual body change minus its planned
transfer is recorded as native competition/identity conversion. This differs
from biological growth/death and from fluid transfer; no hidden mixed body is
invented. Pure controls cover native integer rounding, simultaneous identities,
tiny fractions, huge populations, capacity, unsupported identities, arbitrary
observed identity changes and exact replay bytes. They do not establish live
stronger/weaker native competition, vent readiness, all-ID biological evolution
or saved continuation. A new explicit gas-vent control exercises the helper;
production activation remains off.

### Gas-vent acceptance under construction

Frozen before the first measurement: `mixed-pipe-gas-vents` has two sealed
one-cell sites at arena (2,2)/(5,2), each initially 100 kg O2 at 300 K, with
solid cardinal neighbours. Each native gas vent receives one inlet cell with
0.375 kg O2 + 0.625 kg CO2 at 300 K. Germ quantities are 1000.25 for identity 0
and 2000.5 + 1e-240 for identity 1. The two native bodies start with identity
0/1 and 100 germs respectively; actual native disease strengths are reported.
At stage 20 both vents must be blocked by native overpressure, with zero world
delivery and all 2 kg/two constituents/two germ identities retained across
inlets and owned storage. Exact accounts must close. Replace each site's gas
with 0.1 kg O2 at 300 K, then check at stage 60 that all 2 kg (0.75 kg O2,
1.25 kg CO2, within 1e-6 kg) and both germ identities reached the world, every
inlet/storage is empty without native storage packets, native readers report
unblocked, no jobs remain and every mass/heat/passive account closes exactly.
Host safety cap is 90 owner frames; hitting it is stopped, not accepted.

The adapter retains native consumer and Exhaust rate, power, capacity and
overpressure decisions, with compatibility scalars used only for original
notifications. Completed mixtures coalesce, pending output reservations keep
occupying storage, all constituents are admitted atomically, and committed
precise receipts settle the actual world changes. Surface transfer is staged
before admission; rejection restores the original buffer before any body
mutation. Native body competition remains measured independently. This is gas
only; liquid particles, thermal work, all-ID biological evolution, teardown of
component buffers and durable component/save continuation remain blockers.

The first bounded v1 run reaches stage 60 in 61 frames and passes 12/13.
Native overpressure retains both complete mixtures; opening delivers exactly
2 kg (0.75/1.25 kg), empties all inlets/owned buffers/jobs without native
storage packets, and every mass/U/passive account closes. The failed assertion
read `ToWorld.Germs`, but that ledger intentionally stores mass/U only;
`PassiveExchange` holds actual sequential MARECP02 before/after world changes.
The rerun corrects that field, preserving the requirement for both delivered
identities. Native bodies are observed as 0/100 -> 255/0 and 1/100 -> 1/99;
their measured transfer and competition remain separate. Public native disease
strength fields report 0/0, so differing-strength coverage is not established.

World mass/energy errors are 1.850e-16/5.521e-17, trace sink zero, no native
fault or transfer dump, cleanup verified. Retained boundary heat is
-2.910383e-11 J; one passive representation remainder and mechanical G remain
booked. Diagnostic LOW owner mean/p95/max 175.953/211.448/222.255 ms, observed
272.282/347.125/3085.409 ms (61/60 samples, no gaps). No timing gain or complete
vent acceptance is claimed from that first run.

The corrected v2 run passes **13/13 in 61 frames**, with the same native hashes,
inventories, stages and physical criteria. Actual sequential native world germ
changes are 1905.5/3802; exact representation remainders are 0/2e-240. Both
mixtures move completely, storage/ports empty, native readers and overpressure
remain coherent, and all mass/U/germ accounts close. Body outcomes and the
-2.910383e-11 J heat remainder match the first run. World mass/energy errors
are 1.731e-16/4.819e-17, trace sink zero, no fault or capture, cleanup verified.
Concurrent HIGH work makes this LOW run diagnostic: owner mean/p95/max
436.385/593.952/637.843 ms, observed 597.794/696.738/4965.419 ms (61/60
contiguous samples, no gaps); no performance comparison. Production/save
activation, different-strength body coverage, partial storage/teardown,
thermal accuracy, all-ID biological evolution and liquid particles remain open.

### Common integer germ-evolution oracle (control only)

`ColonyGermEvolution` is shared by future pipe and world adapters. It accepts
the native public API's material count change and temperature multiplier,
preserves binary32 accumulation and truncation, and returns the actual integer
birth/death change plus the next binary32 residue. Extinction resets residue;
unsupported conversion/addition overflow is refused without altering the
caller's exact population. This is the native integer domain only. Fractional
and overflow population evolution, mixture material weighting and saved clocks
are not implemented or adopted by this helper.

Before its first measurement, `mixed-pipe-biology` is fixed as follows: mixed
pipe ownership remains off; the game runs its original pipe disease updater.
At stage 5 create isolated gas conduits at (1,3), (3,3), (5,3). At stage 10 seed
0.5 kg per cell: O2/280 K with 16777219 food-poisoning germs, polluted O2/
293.15 K with 1000 slimelung germs, and O2/900 K with one slimelung germ. Disease
indices come from the actual database. A read-only observer predicts each
native update using the same public growth/temperature APIs, then compares
the native integer count, disease identity and binary32 residue bits. At stage
80 require at least 20 comparisons, both identities, zero mismatches or
unsupported steps, at least one native extinction reset, and both native birth
and death. Host cap 110; a cap stop is not accepted. World fluids still use
MacClosedWorld; only the auxiliary native pipe disease process is the oracle.

The first run is **5/6 at 81 frames**: all 177 actual count/identity/residue-bit
comparisons match, with two identities, 534021 deaths, one extinction reset,
zero unsupported steps and zero mismatches. The required birth was not
observed. That remains a coverage failure, not full biology acceptance. World
mass/energy errors 6.533e-16/4.627e-17, sink zero, no native fault, verified
cleanup. Diagnostic LOW owner mean/p95/max 172.960/206.156/226.059 ms, observed
251.775/291.503/2928.818 ms (81/80 samples, no gaps); no speed claim.

Native declaration inspection shows culture mass is captured by allocation;
later count/identity updates do not update that mass, and cached growth info
refreshes on disease-identity change. Their actual values must be measured and
saved, rather than reconstructing a restored culture from its current packet.
The common adapter therefore exposes captured-culture and current-mass world
entrypoints separately, sharing the same scalar accumulation implementation.

Before the next measurement, v2 retains all three real packets and every
native-match/extinction/domain criterion. It additionally creates an isolated
native disease manager using the existing temperature manager read-only. A
fixed ordered search of PO2, polluted water, water, O2; food/slimelung; counts
1000/100000/10000000; and existing temperature handles finds the first native
public-rule case that grows with positive allocated mass (0.5 kg gas/5 kg
liquid). It allocates only the isolated manager's data, never real pipe/world
data, and advances that original native method once per completed owner frame
at 0.2 s. Require at least 20 real-pipe comparisons plus 20 isolated reference
comparisons and reference births. Report real-pipe culture mass, deaths and
the isolated reference separately. This tests the general native equation's
positive branch; it does not claim ordinary pipe reproduction or adopt new
pipe/world evolution. Stage80/host110 bounds stay unchanged.

V2 is **5/7 in 81 frames**. Real-pipe comparison remains 177 bit-equal updates,
and measured captured culture mass is exactly 0..0. The selector found no
isolated positive-mass case because it demanded an immediate integer birth;
positive fractional growth was excluded. This remains a coverage failure.
V3 keeps the real-pipe setup fixed, adds candidate counts 1/10/100/10000/1000000
and accepts positive residue of at least 1/60 per step so the existing
70-step reference window can accumulate a birth. It reports the best candidate
if none qualify. No live pipe data, growth rules or acceptance criterion is
changed. V2 world M/E 3.677e-16/4.648e-17, sink zero, no native fault, verified
cleanup; LOW diagnostic owner 180.859/214.137/247.566 ms and observed
261.246/325.901/3102.410 ms (81/80 samples, no gaps).

V3 finds a positive public-rule case but stops at frame 11 with an ambiguous
reflection lookup between the declared native culture allocator and its
inherited generic allocator. This is an oracle binding failure; no native
evolution is replaced. V4 selects the declared two-argument, by-ref-contents
allocator explicitly and adds an installed-declaration control before another
game run. Inputs and all matching/growth criteria stay unchanged. V3 world M/E
5.681e-17/3.674e-17, sink zero, no native solver fault, cleanup verified; LOW
owner 187.449/251.709/251.709 ms, observed 525.319/3096.125/3096.125 ms
(11/10 samples). Earlier failures remain recorded.

V4 passes **7/7 in 81 frames**. All 247 original-native updates match count,
identity and residue bits: 177 real-pipe updates and 70 isolated positive-mass
updates, with 32 births, 534021 deaths, one extinction, two identities and no
unsupported steps or mismatches. The positive reference is 0.5 kg polluted
oxygen at 293.15 K with 1000 slimelung germs; real-pipe captured mass remains
exactly 0..0. No evolution is replaced by this oracle. World M/E errors are
6.807e-16/4.658e-17, trace sink zero, no native fault, cleanup verified. LOW
diagnostic owner mean/p95/max is 190.451/215.898/258.889 ms and observed is
280.405/332.944/3390.454 ms (81/80 samples, no gaps); concurrent HIGH work and
the user's GPU workload prevent an isolated speed claim. All-ID mixed-cell
evolution, fractional/overflow rules and saved clocks remain open.

### Early mixed-save refusal

While runtime persistence remains incomplete, active mixed-pipe controls refuse
saving at the public `SaveLoader.Save(string, bool, bool)`, `InitialSave`,
delayed initial/save and game/writer entrypoints. The file entrypoint refuses
before opening or truncating its destination. Writer entrypoints protect only
their own subsequent writes: an external caller may have opened its stream
already. Inactive ordinary owner saves pass these guards unchanged. There is
no callback dispatch, native save barrier or new payload in this temporary
guard; it does not establish mixed save/load support. The owner save writer
now captures the pipe runtime (MACOLP03 section 1); until its in-game controls
pass, only `MA_COLONY_PIPE_SAVES=1` sets `ColonyPipeRuntime.Persistable` and
lifts this guard.

The actual-game `mixed-pipe-save-guard` control passes **7/7 at 11 frames**:
all six patched methods refuse while an existing destination's sentinel bytes
and the supplied writer's position/bytes stay unchanged; ownership remains
active. No native fault, M/E errors 5.902e-17/4.586e-17, trace sink zero and
cleanup verified. LOW diagnostic owner mean/p95/max is
267.904/372.482/372.482 ms, observed 637.633/3134.905/3134.905 ms (11/10
samples, no gaps), without a speed claim. Release, nine pure control groups,
ten installed-declaration/direct groups, 216 snapshot/persistence controls and
326 Python checks (four skips) pass after rebasing onto `f329bff`. The installed
declaration test checks all targets and the direct guard; executing patched
public I/O is confined to this actual-game control because the game's bundled
Harmony cannot patch inside the .NET 10 runner (removed BinaryFormatter API).

## Represented body contact work (explicit thermal acceptance)

`MA_COLONY_PIPE_CONTACT_HEAT=1` selects the pending managed contact correction
only in the existing thermal fixture. The original setup, stage600 endpoint,
temperature limits and endpoint heat bound stay fixed. Native replay version1
captures registered inputs before the physical update; every post-call carrier
temperature must match its replayed bits before settlement. Each contact
credits exact represented body work `Cbody*(Tbefore-Tafter)`, with kJ-to-J
conversion retaining all arithmetic components. Separately observed earlier
carrier heat stays booked; the same contact's rounded carrier delta is not
credited again. A 2000-case independent integer oracle covers the complete
binary32 input exponent range and exact partition/cancellation identities.

This is conditional on a live unique body registration and an unchanged fluid
inventory during the native call; unsupported identities, phase transitions
or carrier mismatches fail explicitly. Phase/component-storage integration,
pending native body save state and production activation remain gated. The
actual601-frame acceptance will use the parent's frozen2569 native bundle;
existing4/5 failures remain recorded until the unchanged heat check passes.

The coupled run passes **5/5 at 601 frames** with that original bound:
body -90674.695667621563 J, mixture/booked +90674.695667621563 J, residual
exactly 0 J versus 0.2458838828 J permitted. Final body 353.2604675293 K and
mixture 353.2597317210 K both agree with 353.2601236134 K analytic equilibrium
within 0.001 K. Both 0.5 kg constituents remain; the exact pipe U audit closes.
All 586 recorded carrier validations pass. No fault, trace sink 0, world M/E
errors 2.964e-15/5.533e-17, 601/600 samples without gaps, cleanup verified.
Native rejected attempts 13704, skipped relaxations 0; no fallback counter is
exposed. LOW diagnostic owner mean/p95/max 359.812/454.501/718.199 ms and
observed 456.684/630.994/4540.225 ms include concurrent HIGH work, user GPU load and
thermal logging; these establish no speed gain. This clears the isolated
finite-body heat failure, while phase, storage, full runtime persistence and
production activation remain open.

## All-ID biology control (not production or saved runtime)

Before measuring `mixed-pipe-all-germs`, fix the setup: the same 8x6 oxygen
arena, isolated gas conduits at (1,3), (3,3), (5,3), (6,3), and liquid at
(3,4), constructed at stage5. At stage10 activate owned pipes and seed:

- gas (1,3): 0.5 kg O2/280 K, 16777219 food germs;
- gas (3,3): 0.25 kg O2 + 0.25 kg polluted O2/293.15 K;
- gas (5,3): 0.5 kg O2/280 K, food population `1e30 + 1e-240` and
  slimelung `1e-240`;
- gas (6,3): 0.5 kg O2/900 K, one slimelung;
- liquid (3,4): 0.5 kg water + 0.5 kg polluted water/293.15 K.

Both mixed-medium cells carry every actual native database disease ID with
population `1000.25 + 0.125*ID`. Every ID owns a separate clock and birth/death
account on the original native disease-manager cadence. Cells outside ownership
still run the original updater. One-material/one-ID integer cultures retain the
actual captured native mass, cached growth descriptor, current native
temperature and binary32 event residue. The single O2 culture is compared on
every update against a separate original-native manager with copied initial
culture data and the same read-only temperature handle.

For mixtures, sum public per-material effects in native ordinal order weighted
by mass fraction; apply the temperature effect once per identity. Fractional or
wide populations enter expected-population mode until extinction. That mode
uses public density cutoffs and half-life factors directly: under-population
death is an absolute rate, while normal/over-population changes are proportional
to the exact expansion population. Native pending residue is booked as an
actual change on mode transition. Every actual positive/negative change is a
per-ID birth/death; deaths stop at the available population. Unrepresentable
birth overflow refuses without changing the caller's population. No trace is
deleted through a projected dominant germ. Count/mass scaling was considered
but rejected before adoption because absolute under-population death would
change under that scaling.

At stage80 require at least20 independent original-native comparisons with zero
count/ID/residue mismatches, at least20 native cadence ticks and40 expected-mode
updates, a measured biological change for each database ID, wide food count
above int.MaxValue and below1e30, positive slimelung below1e-240, hot-cell
extinction and exact all-ID/mass/heat audit closure. Also compare the expected
material RHS against the original public equation for every database ID, four
materials, masses0/0.5/5 kg and counts1/1000/1000000: error at most
max(1e-9,1.3e-7*abs(native RHS)). Stage80/hostcap110 stay fixed. Full runtime
persistence, biology in component storage/particles and production activation
remain gated; this control does not claim those acceptances.

The first actual run passes **9/9 at81 frames**: 70 native cadence ticks,
70 independent original-native single-culture matches, all four database IDs,
144 public material-RHS checks (maximum absolute difference 0.0002784729,
within the fixed relative bound), 700 expected-population updates and exact
per-ID/mass/U audits. Wide food ends at9.6817040902771413e29 and minority
slimelung at1.4164164042483473e-241. All four IDs have measured deaths; the
hot integer culture extinguishes. Fresh pipe cultures retain native captured
mass0, so this run has no births; the separate positive-mass native oracle
above covers that branch. No native fault, sink zero, M/E4.501e-16/3.694e-17,
cleanup verified. LOW owner mean/p95/max183.936/225.821/325.855ms, observed
276.310/324.904/4613.815ms (81/80samples, no gaps), diagnostic only.

The follow-up also passes9/9 with the activation boundary pin enabled under
Mono. Game.StepTheSim runs disease before gas/liquid flow updates; the preceding
conduit jobs return only after the native JobManager worker wait (or inline
serial completion). Five inspected method-body hashes pin this supported
synchronous boundary before any row masking is enabled. Independent original
count/ID/residue comparisons stay70/70; all biological totals above are
unchanged. Optional `MA_COLONY_PIPE_BIOLOGY_TIMING=1` reports capture6.381119ms
and commit0.462489ms mean per native tick for five cells, including fixture
oracle work and cold compilation. This is a profiling baseline, not a speed
gain. Timers are disabled by default. Follow-up owner189.181/224.234/260.564ms,
observed271.504/339.701/3236.779ms (81/80samples, no gaps),
M/E4.100e-16/4.828e-17, sink zero, no fault and verified cleanup. Release,
nine pure groups, eleven installed-declaration/direct groups,216snapshot and
326Python checks (four skips) pass before rebase.

ONI 744825's Windows managed assemblies use different metadata tokens in those
five bodies. Their resolved instructions, locals, branches and exception regions
match Linux exactly. The activation guard pins both inspected raw signatures;
unknown signatures still refuse. This removes a Windows-only admission refusal,
without changing the scheduling or biology arithmetic. Actual WindowsPlayer
all-ID scenario acceptance is required separately.

The optional pure control `--biology-continuation` runs 30,000 fixed 0.2 s
updates per case (6,000 game seconds). It preserves the current step rules and
uses explicit proportional, absolute-death and mixed-material flux callbacks
with binary32 temperature factors. It excludes database lookup, conduit scans
and the independent native oracle. After 200 throwaway warm-up steps, early
and late windows each cover 1,000 updates including the actual birth/death
journal. The .NET 10/HIGH diagnostic run disables tiered compilation. These
are product costs, not whole-game timings.

| Culture | Early / late µs per tick | Late allocation B/tick | Maximum population atoms | Maximum model image B |
|---|---:|---:|---:|---:|
| Native integer | 0.076 / 0.074 | 192 | 1 | 33 |
| Fractional | 29.542 / 30.893 | 155491 | 20 | 337 |
| Wide + tiny | 22.231 / 21.244 | 119731 | 22 | 369 |
| Tiny decay | 2.016 / 1.906 | 8223 | 6 | 113 |
| Tiny growth | 1.582 / 1.568 | 6948 | 6 | 113 |
| Fractional absolute death | 0.068 / 0.066 | 160 | 1 | 33 |
| Mixed material | 57.159 / 59.060 | 313131 | 20 | 337 |

Every case completes all 30,000 ticks; exact audits and 30 cold continuation
checks per case pass, including byte-identical next population/clock/journal
images. Component counts and image sizes stabilize by the first 1,000-tick
sample. The image contains only exact population, mode/residue and two
biological journals, not the full pending runtime checkpoint. Allocation in
fractional/mixed products is a scale concern even though precision growth is
bounded in these cases. No precision, biology rule or production arithmetic
was changed for this measurement.

The subsequent exact scalar-product optimization accumulates the same rounded
atom products with `RawPlus`, then canonicalizes their sum once. It keeps
factor-zero/unit behavior and the saved representation. An independent integer
oracle checks 9,000 products; a private 300,105-operation full-range comparison
(including 111,279 exceptions) and all seven 30,000-tick continuation images
match the previous implementation byte for byte. On the same HIGH pure control,
mixed-material late cost falls from 56.185 to 21.989 µs/tick and allocation from
313,131 to 116,138 B/tick; fractional cost falls from 28.734 to 14.974 µs/tick
and allocation from 155,491 to 77,080 B/tick. These exclude database lookup,
conduit scans and native work; they establish no whole-game speed gain.
The optimized immutable bundle also passes the existing all-ID live control:
9/9 checks at frame 81, 70 native integer bit-matches, all exact pipe accounts,
zero sink and no fault. LOW owner mean/p95/max is
214.439/318.519/338.450 ms; observed is 310.850/428.321/3751.577 ms
(81/80 samples, no gaps). Maximum mass/energy errors are
2.54e-16/4.87e-17; rejected solver attempts 597, skipped relaxations zero;
fallback count is not exposed. Cleanup is verified. Concurrent HIGH work and
shared GPU load make this correctness evidence, not a speed comparison.
Optional live biology capture/commit means are 4.532583/0.471893 ms over 70
calls, including the independent fixture oracle and cold work.

### Frozen cold-body thermal control

`mixed-pipe-cold` is a separate fixture fixed before its first measurement
(2026-10-24). The original hot-body fixture is unchanged. An 8 × 6 sealed
vacuum arena starts at 300 K. At frame 5 it builds one radiant gas pipe at
local (3,3); topology must be ready by frame 30. Relative to readiness at
frame 8, the finite registered body is set to 300 K at frame 8 and the pipe
receives 0.06 kg hydrogen plus 0.01 kg each oxygen, polluted oxygen, carbon
dioxide and chlorine, all at 600 K, at frame 14. Contact coupling is explicitly
enabled. There are no supplied germs or other fluid contacts.

The endpoint is frame 600 plus the recorded topology delay. Both temperatures
must stay within 300–600 K (0.0001 K readout allowance) and finish within 0.1 K
of the analytic finite-capacity equilibrium. Each exact constituent mass must
remain unchanged. Body work plus fluid heat must close within the original
hot-fixture endpoint rule: twice the registered body capacity times the sum
of the initial/final binary32 temperature ulps, plus 1e-7 of the fluid heat
magnitude (at least 1 J). All exact pipe accounts must close independently.
This tests a cold body and a smaller, five-material fluid heat capacity; it
does not accept phase changes, shared bodies or the runtime save boundary.

Immutable `pipes-cold-9b-v1` passes **6/6 at 601 frames** (legacy Flags=1).
Body Cv 1940.000057 J/K, fluid Cv 177.410006 J/K; body +48763.518894338631 J,
fluid/booked -48763.518894338631 J, residual 0 J (bound 0.2417 J). Body/mixture
325.135834/325.136591 K against analytic 325.135897 K; 587 validated contacts;
all five masses and every pipe account exact. World M/E 2.77e-15/5.54e-17,
sink zero, no fault, cleanup verified; LOW owner mean/p95/max 180.5/211.3/318.8 ms
(601 samples, no gaps), diagnostic only.

## Precise owner receipts

The optional native adapter exports `ma_oni_colony_enable_precise_receipts`,
`ma_oni_colony_copy_precise_receipt` and `ma_oni_colony_wait`. Start and restore
reset the opt-in. Changing it requires an empty receipt queue; repeating the
current mode is harmless. Size queries and repeated copies leave receipts
intact; the existing acknowledgement retires both legacy and precise data.
The 48-byte legacy receipt, physical trajectory and native checkpoint bytes
remain unchanged. A solid byproduct has no precise fluid sidecar. This API
alone does not enable mixed pipe ownership.

**MARECP01** is an ephemeral, little-endian payload (claimed 2026-10-23 after
fetching `56b35ea`), never a solver checkpoint. The eight-byte magic is followed
by uint64 sequence, uint64 completed frame and uint32 parcel count. Each parcel
stores uint32 native element index, uint32 actual global cell index, uint32
injected flag, then binary64 mass, temperature and signed internal energy.
Four scaled terms follow, in order: resolved kinetic energy, potential energy,
x momentum, y momentum. Each stores its original binary64 mantissa, uint32
ASCII exponent length and exact signed decimal base-two exponent. Finally a
uint32 passive count precedes pairs of uint64 identity and binary64 quantity.
No exponent or conserved quantity is projected through float or through the
binary64 value range. Zero parcels and quantum-cleanup constituents are kept.
A phase extraction's per-species sidecar includes only that species; other
transfers include every original parcel associated with that legacy receipt.
Missing or retired sequences return zero bytes; an undersized buffer receives
no partial write and reports the required size.
An injection never gives its cell more than the request: when the game
kernel's binary64 sum rounds up, the cell keeps the value just below it and
the port keeps the sub-ulp rest (2026-10-25; before, about half of all vent
emissions into gas were refused as exceeding their reservation).
A split gives its smaller side that side's share at the inventory's
temperature and the larger side the exact complement, so the rounding never
lands in a trace; native float moves leave ever smaller residues, and one
at 1.5e-39 kg had reached 6e8 K and crashed ONI's native pipes. A projection
above ONI's 10000 K pipe limit is refused as an owner fault. The movable and
deferred float stages of a projected mass now sum to it exactly.

The focused native control covers exponents beyond int64 and binary64 range,
trace masses, multiple fractional passives, identity/copy/ack lifetime, and
identical enabled/disabled trajectory, checkpoint and legacy receipt bytes.
Mixed-pipe save integration must drain and credit relevant receipts while the
game submitter is paused, before releasing its save barrier. Waiting for the
owner does not acknowledge or deliver receipts. Exact return injection and
in-flight save/restore remain separate acceptance work.

The managed reader also accepts the agreed **MARECP02** job extension:
after each parcel's passive list, uint32 observation count and sorted
`{ uint64 identity, binary64 before, binary64 after }` entries. These are
sequential per-edit storage observations, so `before + supplied - after`
can be retained exactly in the schema-2 passive remainder account. The
ordinary MARECP01 reader remains unchanged. Reader controls cover huge
exponents, zero parcels, every passive, malformed framing and a fractional
increment rounded away beside a large native count. This is codec evidence;
ports remain disabled until native observations and in-flight job/save
controls establish the complete boundary account.

Full port activation must first require
`ma_oni_colony_precise_receipt_version() >= 2`; an older native bundle or
missing export refuses activation before any job is queued. Ordinary legacy
colonies do not query this optional export.

Native world passive quantities are binary64 per identity: adding a tiny
quantity to a large existing count can round away, and a large incoming
quantity can erase the old low part. Echoed input passives are therefore
insufficient evidence of what the world stored. Durable job requests accept
at most 65,537 sorted positive passives with identities through 0x100000000;
schema-2 jobs and passive remainder accounts enforce these same limits.
Actual sequential MARECP02 observations and a named signed representation
remainder are required for both injection and extraction. No complete
world/pipe germ-conservation or output/save acceptance has passed yet.
