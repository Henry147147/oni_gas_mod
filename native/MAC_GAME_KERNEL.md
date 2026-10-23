# Diagnostic game kernel

Selector3 and game-profile family2 are explicitly opt-in for private in-game
validation. New colonies request flag4 (normally flags5 with conduction), and
the managed selector requires capability1. Old saves retain their own profile;
there is no implicit conversion. Physics gates and the reference keep their
existing implementations and settings.

The backend runs the accepted semi-implicit design's sparse pressure/transport,
fluid heat, latent phase relaxation, density layering and bounded liquid
interface correction. Every derived reading and edit uses its own EOS: ideal
gas plus constant-density linear-elastic liquid with saved stiffness. It never
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

## Open acceptance work

No private-playtest-ready or performance pass is claimed. The coupled material
transport remains donor-cell plus an interface correction; the limited-linear
FCT component is not integrated. Saved vacuum-front limiting, region sleep,
registered native-body contacts, sparse owner transactions/publication and the
fixed floor/reference comparisons remain open. The measured gravity/flow
entropy loss is still unresolved. These are tracked against the accepted
[design](../docs/game-kernel-spec.md) and
[private readiness checklist](../docs/private-playtest-release.md).
