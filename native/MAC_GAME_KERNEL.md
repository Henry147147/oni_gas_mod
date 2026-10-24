# Diagnostic game kernel

Selector3 and game-profile family2 are explicitly opt-in for private in-game
validation. New colonies request flag4 (normally flags5 with conduction), and
the managed selector requires capability1. Old saves retain their own profile;
there is no implicit conversion. Physics gates and the reference keep their
existing implementations and settings.

An additional explicit creation selector, `MA_COLONY_KERNEL3_POSITIVE=1`,
selects profile3/core21 while kernel3 is enabled. The original selector alone
continues to create profile2/core20. Saves restore their recorded law and work
caps independently of either creation selector. Neither profile is the
ordinary colony default or a physics-gate setting.

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

An unadopted exact liquid-stage prototype reuses existing EOS values and an
ordered phase buffer. Six whole-lock offline AB/BA runs on the captured Terra
world measure105.587 to103.057ms per tick (2.40%); all30-tick images match.
It still needs the in-game comparison before adoption.

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
