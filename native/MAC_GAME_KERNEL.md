# Diagnostic game kernel

Selector3 and game-profile family2 are explicitly opt-in for private in-game
validation. New colonies request flag4 (normally flags5 with conduction), and
the managed selector requires capability1. Old saves retain their own profile;
there is no implicit conversion. Physics gates and the reference keep their
existing implementations and settings.

An additional explicit creation selector, `MA_COLONY_KERNEL3_POSITIVE=1`,
selects profile3/core21 while kernel3 is enabled. The original selector alone
continues to create profile2/core20. `MA_COLONY_KERNEL3_SURFACE=1` creates
profile5/core23 ([below](#trace-removal-before-conduction-profile5core23)). Saves
restore their recorded law and work caps independently of any creation selector.
These profiles remain opt-in; floor and colony acceptance are open.

The backend runs the accepted semi-implicit design's sparse pressure/transport,
fluid heat, latent phase relaxation, density layering and bounded liquid
interface correction. Every derived reading and edit uses its own EOS: ideal
gas plus a saved liquid law and stiffness (linear in profile2, positive-volume
in profile3). It never
uses the reference evolution solver as a fallback. Once-per-tick connected
region audits check species, passives, U+K+G and canonical momentum with wall
and body impulses, at the accepted relative1e-12 operand bound. Traces enter
the named sink. Entropy is reported as a diagnostic, without admission retries.

The owner bridge retains dense compatibility state around the sparse tick.
Its per-edit copies and audit passes are now mostly removed; see
[parallel owner thread](#parallel-owner-thread-and-in-place-owner-edits). Native shadow/body heat remains an external booked exchange; duplicate
internal fluid conduction, phase relaxation and settling are skipped. Opened
cells start empty and expand through physical face flow. Core20 stores sparse
species/passives, energy, face momentum, geometry/masks, clock/revision,
numerical settings and calibrated phase pairs; no cache affects continuation.

The colony owner defaults to the number of physical CPU cores. The mod's
[Performance settings](../docs/settings.md#preferences) can select an explicit
count in the supported 1..256 worker range, including SMT threads. This
replaces the earlier logical-CPU-minus-two choice. Worker preferences apply
between frames and after restore, without entering the colony save. The game
kernel honours that request without a twelve-worker cap; small grids still
limit the pool to their fixed row blocks. `MacClosedAdvanceReport::game_threads_used`
reports the pool size, including the caller, without entering the checkpoint.
The focused control requests 1/12/13/22/30/256 workers on 31 blocks and checks
actual pool sizes, exact physical images and cold continuation against one
worker. This changes scheduling only; it does not establish a throughput gain.

On the Ryzen 9 5900X (24 logical CPUs), eight interleaved 20-frame Terra
profile5/core23 frame-50 snapshot runs per count under the whole CPU lock give
warm means of **84.44 ± 2.38 ms at 12 workers** and **90.73 ± 1.51 ms at 22**
(mean ± sample SD across runs, first frame excluded). The extra workers are
**7.45% slower**, with all eight pairs slower. Final checkpoint bytes match
between counts and parent `8c1285e`. This measures offline solver advance;
30-worker throughput on the user's 9950X remains unmeasured. Linux Release
passes 86 CTests; all 86 applicable MAC executable outputs and exit statuses
match the parent except timing fields and the new control's PASS line.
The changed C++ units also pass MinGW syntax checks; no Windows runtime check
is claimed.

Focused controls cover own-EOS imports/readings, core20 round trips,
worker-independent and cold continuation, heat/parcel/wall energy, full passive
observations and malformed images. A separate native-owner control exercises
flag5, door opening, heat, publication and exact saved continuation.
The first corrected whole-lock Terra60 at requested fast speed completed with flags5,120flow halves, no
rejections/faults/skipped phase, M/E1.67e-16/7.10e-16 and trace1.183e-7kg /
0.12784J. Owner mean/p95/max210.27/226.40/275.22ms; observed spacing
266.07/404.87/766.01ms. The first diagnostic had omitted trace receipt counts;
core and owner regressions now require those counts and actual sink booking.
This is basic execution/conservation evidence, not a floor or performance pass.


## Exact integration speed step

Caching material-profile indexes, sharing an unchanged settled face-mask state
(with the same revision increment), and moving tick arrays change no solver
arithmetic or checkpoint bytes. Clean Release85CTest and all100 applicable MAC
outputs (99 legacy invocations plus the new kernel control) match the parent,
apart from timing fields. Actual core20 twelve-tick images match at1/4/8/12
workers, and native-owner continuation controls pass at each count.

Clean whole-lock Terra60, requested fast2, identical managed assemblies and
12workers; mean/p95/max in milliseconds:

| Metric | Before | After |
| --- | --- | --- |
| Owner advance |214.421/237.971/259.746|194.112/210.494/247.371|
| Observed interval |256.714/342.880/747.360|239.111/344.533/726.670|
| Relative mass/energy error |2.20e-16/7.72e-16|1.67e-16/7.09e-16|
| Trace sink kg/J |1.17480e-7/0.127785|1.18098e-7/0.127624|

Both complete60frames,120flow halves, zero rejections/faults/skipped phase.
Measured owner mean falls9.47%, observed mean6.86%; this does not meet2x.
The user's vLLM workload shares the GPU outside CPU locks, so these runs do
not establish isolated causal speed gains or final GPU performance acceptance.
The owner timer ends before projection/publication. Whole-lock offline AB/BA
repeats pinned to physical cores give104.130/103.042ms per tick (~1%); the
larger gain is in owner edits/readings. Fixed floor errors are unchanged by
this exact step; their acceptance remains open.

Energy report fields expose the accepted audit residual and bound in joules,
with matching wide fields. This corrects a diagnostic units error; the native
owner ledger audit and core20 physical bytes are unchanged.

## Whole-asteroid reliability

The unchanged `f329bff` diagnostic bundle fails both new LOW-lock checks.
Oceania completes2/60 frames in two runs, then its post-transport EOS rejects
a liquid strain. Completed-frame mass/energy errors are1.52e-16/3.16e-17 and
1.95e-16/2.17e-17; trace sinks are8.85e-10kg/0.001013J and
9.02e-10kg/0.001019J. Volcanea completes0/60: a pressure component exhausts
the200-iteration cap. All runs exit and the game process is verified gone.
These failures block whole-asteroid acceptance for profile2. The profile3
checks below clear these particular failures; private-release acceptance stays open.

Exact captured inputs reproduce both failures offline. A private400-iteration
cap clears60 Volcanea ticks with global/region audits and sink1.06714e-7kg /
0.139128J; this is not an in-game or saved-profile acceptance. Oceania still
fails after experimental post-transport trace booking through the1mg ceiling.
Neither experiment is adopted. Raising stiffness alone also fails the captured
Oceania case; those probes initialize fresh worlds at declared temperatures,
and do not continue an old save under different settings.

A separate passive-free isolated-cell control exposed an export crash: an
absent sparse passive column has no offsets. Export now skips that column.
The focused control advances a quiet cell, repeats after a refused parcel,
and compares requested1/12-worker cold continuation. Existing successful
solver arithmetic and checkpoint bytes are unchanged.

An initial exact liquid-stage prototype reused existing EOS values and an
ordered phase buffer. Six whole-lock offline AB/BA runs on the captured Terra
world measured105.587 to103.057ms per tick (2.40%); all six30-tick terminal
images matched. Refreshed profile3 and in-game evidence is recorded below.

## Open acceptance work

No private-playtest-ready or performance pass is claimed. The coupled material
transport remains donor-cell plus an interface correction; the limited-linear
FCT component is not integrated. Saved vacuum-front limiting, region sleep,
registered native-body contacts, cached physical publication and the fixed
floor/reference comparisons remain open. The measured gravity/flow
entropy loss is still unresolved. These are tracked against the accepted
[design](../docs/game-kernel-spec.md) and
[private readiness checklist](../docs/private-playtest-release.md).

## Positive-volume profile

Profile3 preserves the linear liquid volume and elastic energy below half of
the saved stiffness. Above that pressure it uses continuous dyadic affine
volume segments, always positive, with elastic U integrated from `-p dV`.
Each mixed closure stays quadratic within a constitutive interval; selection
walks finite intervals, without Newton/bisection or a reference fallback.
Own readings, injection/extraction, conductivity, layering, interface energy
and pressure-dependent phase equilibrium all use that same law.

| Saved change | Effect and cost | Floor/reference acceptance |
| --- | --- | --- |
| Positive-segment liquid law | Progressively stiffens large compressions; pressure/elastic U and high-pressure boiling differ from the original linear model. No mass clipping or feature removal. Low-pressure branch is unchanged. | Resolved-reference error and affected floor metrics remain open. |
| Pressure/heat caps400/800 | Permits more work on difficult components; original tolerances and converged early exits remain. | Conservation/worker controls and three new-colony60-frame smokes pass; floor and long-run acceptance remain open. |

Synthetic controls cover2000 mixture closures, volume/U
continuity and elastic pressure work. Fresh-world offline60-tick replays at
requested1/12 workers give identical physical images on Oceania, Volcanea and
Terra, with global/region audits. Sinks are4.04869e-7kg/0.149010J,
1.05911e-7kg/0.135045J and1.04212e-7kg/0.117171J respectively. The original
1e-11kg threshold is retained; no experimental post-transport sink is used.
These initialize each own-EOS world at the original masses/J/temperatures,
recomputing its initial U. They are neither old-save continuation nor in-game
acceptance.

An immutable integrated bundle completes actual Oceania60 and Volcanea60,
requested fast2 under the LOW lock, with120flow halves and zero faults,
rejections or skipped relaxations. Frame1 images verify core21, profile3,
positive law and saved pressure/heat caps400/800.

| World | Owner mean/p95/max ms | Observed mean/p95/max ms | Relative mass/energy error | Trace sink kg/J |
| --- | --- | --- | --- | --- |
| Oceania |220.239/275.398/290.227|278.288/485.876/826.944|5.53e-16/6.75e-16|1.69768e-7/0.113583|
| Volcanea |269.067/319.556/335.748|318.367/528.619/914.984|2.36e-16/3.90e-16|1.10156e-7/0.141078|

Clean WHOLE-lock Terra60, same native/managed bundle, changes only the new
colony profile selector. At requested fast2:

| Metric | Profile2/core20 | Profile3/core21 |
| --- | --- | --- |
| Owner mean/p95/max ms |199.543/226.389/258.600|199.880/247.134/278.272|
| Observed mean/p95/max ms |250.684/323.738/746.792|252.286/343.988/806.033|
| Relative mass/energy error |3.75e-16/6.95e-16|3.17e-16/6.15e-16|
| Trace sink kg/J |1.19185e-7/0.128080|1.23840e-7/0.128921|

Both complete60frames,120flow halves, zero faults/rejections/skipped phase.
Owner/observed means change+0.17%/+0.64%: no clear runtime gain or cost.
The shared GPU workload remains active, so timings are diagnostic. Actual
spacing does not hold2x. All99 legacy diagnostic outputs/exits stay unchanged
apart from timings; the game control adds one PASS line. Clean Release86CTest
passes. Existing parcel control output and actual core20 twelve-tick bytes
stay identical. New core21 offline60-tick images match across1/12 workers and
cold restore halfway through each of the three asteroid inputs.

The same clean comparison at requested normal1x gives:

| Metric | Profile2/core20 | Profile3/core21 |
| --- | --- | --- |
| Owner mean/p95/max ms |199.855/224.859/263.114|201.837/231.071/264.022|
| Observed mean/p95/max ms |249.029/323.714/806.375|244.796/344.003/787.773|
| Relative mass/energy error |5.67e-16/7.05e-16|3.66e-16/7.01e-16|
| Trace sink kg/J |1.19134e-7/0.128261|1.22207e-7/0.128524|

Both again complete60frames,120flow halves and zero faults/rejections/skipped
phase. These contended normal/fast pairs establish execution and conservation;
they do not establish isolated timing differences or sustained2x readiness.


## Exact liquid stage reuse

Interface geometry reuses its already evaluated EOS for the same inventory/U.
Density layering reuses an ordered small phase vector instead of allocating a
map per cell, preserving key and accumulation order. Solver arithmetic, saved
settings and checkpoint bytes are unchanged. Clean parent/candidate Release
builds retain all99legacy diagnostic outputs/exits apart from timings, plus
unchanged game/parcel control outputs;86CTest passes. Requested1/12-worker controls and
three captured core21 worlds at60ticks with cold restore at30 match the parent
images exactly. Six alternating WHOLE/pin0..11 offline30-tick runs measure
102.438748 to99.745524ms per tick (2.63%); their six terminal images are equal.

Clean WHOLE-lock Terra60 at requested fast2, identical managed assemblies:

| Metric | Parent | Exact reuse |
| --- | --- | --- |
| Owner mean/p95/max ms |199.255/241.673/248.785|199.674/225.409/270.772|
| Observed mean/p95/max ms |250.165/344.152/745.163|245.186/343.360/787.634|
| Relative mass/energy error |2.79e-16/5.46e-16|2.44e-16/6.90e-16|
| Trace sink kg/J |1.22260e-7/0.127232|1.17472e-7/0.127668|

Both complete60frames/120halves with zero faults/rejections/skipped phase and
verified process exit. Owner mean changes+0.21%, observed mean-1.99%; no clear
whole-owner gain is established under shared GPU load. The user will arrange
a quiet GPU window later. No2x or floor acceptance follows.

A separate allocator-only diagnostic increases glibc tcache_count7 to65535:
101.330 to99.916ms offline, but peak RSS rises142275 to176149KiB. Its six
terminal images match; this host setting remains unselected and has no game
performance evidence.

## Further profile3 reliability controls

The immutable original core21 bundle passes all10 existing evaporation, door
and freeze/melt checks over139frames, with zero faults,
rejections or skipped phase. Maximum mass/energy errors3.600e-16/1.292e-15,
trace sink2.48880e-7kg/0.263928J. LOW-lock diagnostic owner mean/p95/max is
222.231/270.107/306.018ms, observed269.025/349.752/839.975ms. All139samples
and138intervals are present, package hashes are unchanged and cleanup verified.
This establishes phase/terrain smoke coverage, not the frozen accuracy floor.

Fresh own-EOS initializations from three older failure shapes each complete
60offline ticks, warm1worker and12workers with cold restore at30; each pair's
full checkpoint bytes match and global/region audits close:

| Captured shape | Trace sink kg/J | Maximum absolute energy residual J |
| --- | --- | --- |
| Frame4387 energy-share fault |8.20727e-8/0.110901|0.000241|
| Frame3319 preconditioner fault |7.61204e-8/0.107834|0.000216|
| Frame1071 fault |7.53416e-8/0.102525|0.000250|

These retain original masses/T/J and explicitly recompute initial U using the
new law. They do not migrate or continue an old save, establish actual late-game
reliability, or replace the required fresh-profile colony run. Core21's frozen
F1–F5 comparison remains unmeasured; the recorded F1 failure belongs to profile18.

## Free-surface profile (profile4/core22)

A gas-free liquid cell that is only partly full sits at vacuum pressure. The
face above it, to an empty cell, received the gravity impulse of half the
cell's mass every tick with nothing to oppose it, so its momentum fell freely
while no material could cross; the body work drained the liquid's U into this
phantom kinetic energy. When the dropped oil of floor F5 landed on such
momentum, the liquids churned for about 70 s with cells at up to 3.4 times
their nominal mass. Captured Oceania showed 349 such faces at up to 281 m/s.

Profile family4 closes, for one interval, every face whose momentum predicted
from the current pressures would carry material out of an empty cell: no body
or pressure impulse acts on material that is not there. Momentum stranded on
such a face (its donor emptied during the previous transport) goes to the
boundary impulse and its kinetic energy becomes heat in the occupied
neighbour, so U+K+G, momentum and region audits close as before. New colonies
originally selected it with `MA_COLONY_KERNEL3=1 MA_COLONY_KERNEL3_SURFACE=1`;
it also saves
the adopted gas heat mixing8 (user decision 2026-10-24, floor F1 split).
Profiles2 and3 are unchanged.

A focused control keeps a resting gas-free surface at rest (profile3 control
still shows the free fall), turns stranded momentum into heat with closed
U+K+G, and checks checkpoint22 cold round trip and 1/12-worker continuation;
the owner control creates core22 with saved mixing8 and restores it without the
selector. All99 legacy outputs/exits, the parcel output and three captured
worlds' core21 sixty-tick images (cold restore at30) match the clean parent;
core22 images match at 1 and 12 workers. Floor results are in
[base-game-floor.md](../docs/base-game-floor.md).

## Trace removal before conduction (profile5/core23)

An offline Oceania continuation exposed tiny cells produced by transport whose
heat capacity was below the conduction solve's residual scale. Their shared
heat flux could exhaust sensible U before the end-of-tick trace sink ran.
Family5 books the existing sub-threshold trace sink before conduction as well,
refreshes the remaining cells' EOS and then solves heat. The saved threshold
stays1e-11 kg per constituent per cell; all removed mass, U+K+G, J and passives
remain in the sink. The surface selectors now create23 with saved mixing8.
Old profiles20–22 retain their policy and checkpoint bytes.

Clean parent/candidate Linux Release builds pass86/86CTest; all99 legacy
diagnostic outputs/exits match (timings excluded), and the parcel output is
unchanged. A generated hot-gas front exercises the new ordering with closed
energy and both passive identities, exact cold continuation, and an unchanged
no-trace control. The old Oceania failure still reproduces under22; a scratch
replay explicitly selecting23 completes60 ticks with byte-identical1/12-worker
and cold30 endpoints. This is diagnostic replay evidence. Fresh selected-profile
game checks, the floor and long-colony acceptance remain open.

The subsequent fluid-only Oceania stress probe reaches923 ticks, then SaltWater
falls below its supported temperature range. It first crosses its low identity
transition at tick193, but that standalone driver omits the owner's
SaltWater-to-Brine conversion and solid byproduct. This result establishes a
limit of that diagnostic, not an integrated-owner failure; an actual game run
with conversion and freezing remains required.

## Parallel owner thread and in-place owner edits

The 12-thread owner thread was about 88% serial (2026-10-26, Terra frame-50
dump): the pressure stage's setup and publication, component layout, region
and whole-state audits, trace sinks, sharpening and transport setup, layering
and the dense/sparse conversions each ran one serial pass over the grid. Most
of these now run in the grid's fixed row blocks
([determinism](MAC_KERNEL.md#determinism)); the rules below keep every
physical value bit-identical to the serial passes, for any worker count.

- A cell that sums terms from several faces gathers them in the order of the
  original face loops (left and right x faces, then bottom and top y faces).
  Pressure assembly and the post-pressure energy pass work this way.
- Work whose order spans cells or faces (trace-sink, layering and sharpening
  bookings, interface-cluster registration, transport face scheduling) finds
  its cells and faces in row blocks, then books them serially in the original
  order.
- Sparse results are packed from per-block counts.
- Pressure components are slices of flat arrays, numbered by lowest cell.

Audit sums are the one place association changes. The tick's opening and
closing captures sum each block's cells, then its x and y faces, into the
global totals and into per-block region partials, merged in block order;
phase-conversion booking does the same, and the pressure stage's wall and
body impulse sums are block partials. Only the last bits of audit residuals
and reported impulses change, identically for every worker count.

Owner edits no longer copy the state for each batch. A game world keeps the
audit totals its producing operation summed (the tick's closing totals,
carried through heat and transfer edits). A transfer audit books only the
touched cells and the edited cells' faces against those whole-world operands.
An expiring owner (`std::move(world).with_cell_heat(...)`, likewise
`with_fluid_transfers`) whose state no other world shares is edited in place;
an edit journal records each cell, face momentum and occupied volume before
its first change, supplies the audit's original values in both forms and
undoes a refused batch. The colony owner passes its frame world this way, so
only the frame's first edit copies the published state, and its ledger closes
on the kept totals.

Rejected after measurement: persistent transport scratch (1.3% slower at one
thread), building dense cells on the pool (3% slower at twelve), and frozen
shared inventories (a state copy is dominated by its cell array, not the
inventories). Checkpoint formats and bytes, saved settings and physics are
unchanged; each commit message gives its interleaved A/B measurement.


## Large pressure and heat components

Each connected pressure or heat component runs its own IC(0)-PCG. Small
components run side by side on the pool, but one large connected
atmosphere ran its whole solve on one thread: in the
[colony benchmark](../docs/performance-acceptance.md) a 30,826-cell room
network spent 374 ms of pressure per 0.2 s frame at twelve workers while the
pool waited. The iteration count depends on the acoustic CFL number (about
250 per half-tick for oxygen at 0.1 s, more with hydrogen), not on the
component's size, so the cost grows linearly with connected cells.

Components of at least 4,096 cells now solve one at a time across the pool
(`PressureParallelPlan`, `component_sweep_plan`). The operator gathers each
row's faces in face order. The IC(0) forward and backward sweeps run in bands
of whole grid rows, cut into column chunks and pipelined so that every row
still reads its finished lower neighbours; the backward sweep gathers the
serial scatter's updates in descending row order. Every row's arithmetic is
therefore the serial solve's. Only the dot products differ: they sum fixed
1,024-row blocks in long double and add the block sums in order. Results are
deterministic and independent of the worker count, and a component under
4,096 cells keeps the serial solve bit for bit (`ma_pressure_tests` checks
one-block plans against the serial solve under several schedules and
concurrent threads, blocked plans against each other, and refuses a plan that
breaks an IC dependency). The heat solve keeps its serial component solves:
the same plan measured only about 10% faster there, since its few
iterations are not where the heat stage spends its time.

The physical model, tolerances, iteration caps, saved settings and checkpoint
format are unchanged. Large components' trajectories differ from the serial
solve's only by the reduction order of their dot products.

## Pressure intervals after digging

The 2026-10-28 Windows playtest exposed a transport/pressure splitting defect.
The pressure solve freezes compliance and face velocities while local transport
microsteps finish. At a newly opened vacuum cell, those microsteps could keep
bringing in gas before pressure was recomputed. In the user's colony, twelve
actual native digs drove the observed neighbour from 1.67 kg to 15.36 kg,
pressure above 2 MPa, and temperature below 195 K. The same colony without the
added digs stayed near 304–308 K. The original save remains private and unchanged.

Game profile6, saved as physical checkpoint24, checks the net swept volume
before any pressure work, transport or ledger mutation:
`abs(q_right - q_left + q_top - q_bottom) <= 0.4 * cell_volume`.
The fraction is the existing numerical 0.4-cell scale. A rejected pressure
trial returns its unchanged EOS storage; its shorter replacement solves
pressure again, including work, damping and canonical momentum. Accepted
intervals cover the requested time, with proposals growing by at most two.
Both accepted and rejected pressure trials count against the attempt budget;
non-progress refuses the advance. The net-volume criterion permits rapid
through-flow with zero discrete divergence, which has a separate circulation
control using multiple transport microsteps.

Pressure time refinement applies independently to components separated by
solid walls. Each component runs the complete tick in its bounding rectangle;
explicit face barriers stay within that component. The merged image retains
global cell/face coordinates, translates membrane conflicts, and corrects
trace-sink potential energy for the rectangle's vertical origin. Every child's
physical audits and a global closed account must pass before the candidate is
returned. The attempt budget bounds each independent temporal sequence;
`report.attempts` records the deepest sequence while stage counters sum all
component work. Component stage durations can exceed enclosing elapsed wall
time when components run concurrently.

The focused Linux and Windows/Wine controls pass: a new and a nearly empty
cell agree with a fine temporal reference that does not use the new criterion;
a one-dimensional pre-reflection rarefaction retains its initial pressure
bound; mass/energy/momentum audits, budget refusal, cache retry, worker
equivalence and cold continuation pass. Controls also cover 1,024 isolated rooms
with a two-attempt temporal budget, moving regions at several worker counts,
shifted gravitational sinks, and global membrane coordinates. Both full native
suites pass 89 tests. Clean builds compare all 88 MAC targets: 87 standard
programs retain the same legacy physical outputs and exit codes, with only
three new test messages and timing differences; the long Riemann gate remains
unrun as required. Existing diagnostic failures stay recorded.

The WindowsPlayer/Proton regression uses the uploaded colony's unchanged
physical state and an identical private helper for twelve actual native digs.
Across frames504–584, the watched cell stays at 32.338–34.561°C,
1.6462–1.6818 kg and 249.852–254.394 kPa; the earlier policy reaches
−78.636°C, 15.364 kg and 2.173 MPa. All twelve inputs match exactly.
Maximum relative mass/energy errors are 8.48e-16/3.53e-15.
The full colony retains expensive remote flows inherited from the earlier
policy: the corrected regional run averages 2.67 s per 0.2 s frame. The
global-refinement prototype took about 18–22 s; this motivated independent
components.

A fresh Spaced Out! Fast-preset run also passes 80 frames and twelve digs,
with mass/energy errors 3.41e-16/6.02e-14 and no first-flow fault. Its observed
cell stays at 19.266–22.697°C. On the development host's assigned cores it
averages 1.37 s per 0.2 s frame (p95 2.01 s); the other CPU slot was active.
These measurements establish no real-time or Windows OS performance pass.
Completed-frame32 writing and ordinary cold/menu loading pass: both loads
restore Fast and advance twelve frames to44, with mass/energy errors
2.63e-16/3.52e-14 and verified menu return. Focused AddressSanitizer/UBSan
pressure, regional and worker/continuation controls pass. The complete
instrumented game-kernel executable stops earlier at the unchanged account
zero-add non-finite bit-pattern fixture; an isolated copy using the baseline
header also reproduces that fixture failure. Its acceptance checks are not
changed. The compact private comparison is
`artifacts/windows-temperature-flux-comparison.json`.

Checkpoint24 retains version23's field layout and identifies the new arithmetic.
The core reader preserves versions20–23 and their original policies. Ordinary
mod loading refuses those R&D saves with instructions to start a new colony;
there is no supported save migration. A private same-state version23-to-24
copy is used only to compare the affected colony under the two policies.

### Performance follow-up

A shared executor for regions of at least4,096 cells was measured and rejected.
On the same saved frame32 physical state, eight advances repeated three times
were byte-identical to the accepted code, but mean advance time increased
from1.4423 to1.9311 s (+33.9%). The accepted implementation and private ZIP
retain the previous executor ownership. The paired evidence is
`artifacts/temperature-pooled-physical-benchmark/summary.json`.

An observational profile of the accepted code also retains all eight checkpoint
images exactly. A 2,846-cell remote magma region accounts for most work:
6,066 of7,953 retries, all caused by the net-volume limit. Two cells contain
magma/rock-gas mixtures with substantial canonical momentum. Over eight
advances that region spends4.66 s in pressure,3.60 s in transport,1.03 s in
EOS work and1.29 s in audits; heat costs only3.5 ms. The report is
`artifacts/temperature-accepted-region-profile/summary.json`.
This is a same-state physical-kernel benchmark without owner-level inputs or
conversions; it is not a whole-game performance acceptance run. No timestep,
phase-conversion rate, trace threshold or acceptance bound was changed to
obtain these measurements. Further optimization must address that local flow
without restoring the overfill defect.

## Boil-only phase rule (profile6/core25)

New colonies save the [boil-only rule](MAC_PHASE_CHANGE.md#boil-only-rule)
(user decision 2026-10-28) as physical checkpoint25: the tick's phase stage
compares saturation with the cell's total gas pressure, the sum of the Dalton
partials, instead of the vapour's partial pressure. Liquids vaporise only by
boiling, vapour below its boiling point condenses and the air holds no
humidity; a cell whose only gas is the vapour gives identical bytes. Profile6
with **Evaporation below boiling** on keeps core24 and its arithmetic. The
phase stage's entropy remains a diagnostic: it reports the mixture entropy,
which falls when unsaturated vapour condenses, while the rule's own entropy
(each gas at the total gas pressure) rises.

## Retry storms and speed profile 7

The bounded intervals of profile6 multiplied the work of a few regions. In
the fresh Spaced Out! frame-32 state (`writer32`) one 2,846-cell magma region
made 2,062 pressure attempts for 1,024 accepted intervals per 0.2 s advance.
The cause is a face, not the region: between an 8.3 kg magma cell (308,219)
and a rock-gas cell above it holding 2.7e-7 kg, the face momentum was
−4,998 kg·m/s on 4.1 kg of face inertia, −1,207 m/s. The 0.4-cell net-volume
bound then admits at most 3.3e-4 s, and fixed halving and doubling settled
on alternating 1.95e-4 s accepted and 3.9e-4 s rejected trials.

An owner replay of the same fresh colony from frame3 with its recorded
inputs shows how that velocity forms. Interface sharpening moves about 11%
of the cell's magma per tick into its fuller neighbour, and its equal-volume
parcels leave face momentum unchanged (layering has the same rule), so the
face kept J while its inertia fell from 345 to 1.7 kg: −4.8 m/s at frame3,
−24 at frame10, −1,690 at frame32. The kinetic energy came from internal
energy; the cell cooled from 1,722 K to 1,345 K. The rock-gas cell above is
not exactly empty, so the free-surface closure does not close the face, and
its trace mass carries almost nothing, so pressure cannot build to stop it.
This is a modelling defect of the stages that move material without its
momentum, not a physical flow; correcting it changes the equations and is
reported as an option rather than adopted here.

The other expensive regions of fresh colonies are physical: gas and vapour
of nanograms to grams per cell expanding into vacuum pockets at 100–700 m/s.
The per-cell bound applies to them regardless of mass. In the replayed
frame-60 state the four slowest regions accepted 150–330 intervals per
advance, limited by cells whose own or donor mass was below 1 µg in about
70% of rejections.

Three changes reduce the cost of these intervals:

- *Changed-cell EOS readings* (all profiles, no arithmetic change). After an
  accepted flow interval, a trace prune and layering, cells whose inventory
  and energy are bitwise unchanged keep their reading.
- *Cached component structure* (all profiles, no arithmetic change).
  `solve_components_cached` keeps the component layout, face order, IC(0)
  sparsity and reduction plan while the active cells and coupled faces are
  unchanged. On a one-worker pool (each region of a multi-region world) it
  runs each component's PCG with the general solve's exact arithmetic:
  single reductions below 4,096 cells, 1,024-row reductions above. A control
  compares both paths bit for bit. Setup had been about 0.8 ms of a 1.3 ms
  one-iteration solve on a 9,000-cell component.
- *Game profile7 / physical checkpoint26* (new colonies still create
  profile6). Profile6's equations, criterion, tolerances and caps; each
  interval is proposed from the swept volume the previous trial measured: a
  net-volume rejection retries at `0.9 / ratio` of its interval (at most
  half), other refinements halve, an accepted interval proposes 90% of the
  bound (growth two, shrink half), a remainder shorter than two proposals is
  split in halves, and the proposal carries into the second flow part.
  Accepted intervals still satisfy the same bound, and the expansion control
  checks profile7 against the resolved temporal reference.

Same-state physical benchmarks (Ryzen 9 5900X, twelve workers, the high CPU
lock, three interleaved repeats of eight 0.2 s advances, no owner inputs):

| State | Parent profile6 mean / p95 s | This series, profile6 | This series, profile7 |
| --- | --- | --- | --- |
| Fresh frame 32 (`writer32`) | 1.905 / 2.978 | 1.532 / 2.297 | 1.171 / 1.724 |
| Fresh replay frame 30 | 1.735 / 2.791 | 1.663 / 2.834 | 1.022 / 1.710 |
| Fresh replay frame 60 | 0.591 / 1.498 | 0.541 / 1.331 | 0.337 / 0.750 |

The fresh replay states come from the owner replay above (Spaced Out!
colony, recorded inputs from frame3, profile6). Concurrent builds on the
other CPU half made these runs slower than unloaded ones; the comparison
within each row is interleaved.

Profile6 checkpoint images of the series equal the parent's in every
compared advance. Clean Release builds give the same exit codes and outputs
for all 87 run MAC programs (the long Riemann gate is not run), apart from
timing fields and the game-kernel test's new PASS lines: before rebasing onto
the boil-only rule (`94c42f6` against this series' cached structure and
profile7) and after it (`1058d0d` against the series' last commit). All 89
CTests pass on both. Profile7 also runs the boil-only rule (version26,
selector1) on the fresh frame-60 state.

The overfill regression was replayed offline through the colony owner with
the recorded inputs of the twelve-dig Proton run (the uploaded colony's
unchanged physical state, frames505–584, twelve workers). Profile6
reproduces the recorded in-game bounds of the watched cell: 32.340–34.560°C,
1.6462–1.6818 kg and 249.85–254.39 kPa. Profile7 stays within
32.350–34.520°C, 1.6492–1.6818 kg and 249.75–254.39 kPa, while the mean
owner frame falls from 2.94 to 1.49 s. An owner replay of the fresh
Spaced Out! colony (frames4–43) averages 1.31 s per frame with profile6 and
1.16 s with profile7; the magma face still forms there (up to −2,081 m/s).
Without interface sharpening it never forms (at most −70 m/s) and the same
replay averages 1.05 s; that is a measurement of the cause, not a setting
change. Making profile7 the new-colony default is a one-line change in the colony
owner's settings, but one control must first be rebuilt: the owner's legacy
liquid-boundary law check (`oni_colony_owner`, an image relabelled as owner
wrapper11) expects a water front to cross an airflow-tile column within one
frame. On profile6 253.6 kg crosses; on profile7 the front reaches the
column more gradually, each frame's expulsion returns it, and none crosses in
six frames. A wrapper11 image with a profile7 core cannot exist, so that
fixture should be created on profile6. The owner's creation, option and
restore controls pass with profile7 and core26 expectations.

Profile7 roughly halves the attempts of the storm regions;
it does not reach real time on these states. The remaining cost is the
interval count of the fast flows described above, and the per-interval cost
of transport (about 1–2 ms per interval on 5,000–9,000 cells), pressure and
readings. Options that would change the model (momentum-carrying sharpening
and layering; a net-volume bound that ignores cells and donors below a mass
floor) are measured in the orchestration report, not adopted.
