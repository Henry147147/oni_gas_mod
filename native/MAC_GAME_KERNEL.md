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

## Open acceptance work

No private-playtest-ready or performance pass is claimed. The coupled material
transport remains donor-cell plus an interface correction; the limited-linear
FCT component is not integrated. Saved vacuum-front limiting, region sleep,
registered native-body contacts, sparse owner transactions/publication and the
fixed floor/reference comparisons remain open. The measured gravity/flow
entropy loss is still unresolved. These are tracked against the accepted
[design](../docs/game-kernel-spec.md) and
[private readiness checklist](../docs/private-playtest-release.md).
