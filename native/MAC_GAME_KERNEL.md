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

The initial owner bridge retains dense compatibility state around the sparse
tick. This deliberately leaves copy and publication costs to measure and then
remove. Native shadow/body heat remains an external booked exchange; duplicate
internal fluid conduction, phase relaxation and settling are skipped. Opened
cells start empty and expand through physical face flow. Core20 stores sparse
species/passives, energy, face momentum, geometry/masks, clock/revision,
numerical settings and calibrated phase pairs; no cache affects continuation.

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
registered native-body contacts, sparse owner transactions/publication and the
fixed floor/reference comparisons remain open. The measured gravity/flow
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
