# Radius consumption at the native fluid boundary

The installed build's `GasBreatherFromWorldProvider.ConsumeGas` submits a
radius-three `SimMessages.ConsumeMass` request. Previously the adapter accepted
only radius zero. Correct breathing reads alone therefore did not provide an
end-to-end duplicant consumption path.

The adapter now retains the real radius byte and original callback. An additive
native radial-consumption capability, version 1, uses transfer flags bits 8–15
without changing the existing 64-byte ABI 2 command. Radius zero retains the
original point rule. Older native libraries reject the new capability before
the adapter changes game state. Rectangular consumption and disease-bearing
injection remain separate unsupported contracts.

The custom owner allocates selectively from face-connected fluid cells, in
centre-first breadth-first order, up to the requested radius. Solids block the
search; the explicit sealed domain boundary is impermeable. It does not read
or consume native fluid outside that owner. This is the custom backend's
declared radius policy, not a claim that every detail of vanilla's search is
identical.

Every allocation belongs to one candidate. Overlapping requests see earlier
reservations, and a later invalid request publishes none of the batch. The
worker emits one combined consumption receipt and one original game callback,
with total mass, mass-weighted temperature and complete parcel U/K/momentum.
The callback's cell identifies the request centre, not a claim that every parcel
came from that cell.

The total and each parcel lie on one shared binary32 callback lattice. Their
sum is exact and the total never exceeds the requested or available mass.
Finer per-cell residual inventory stays in the physical source, including
positive traces below callback resolution. There is no physical constituent
cutoff or minimum extraction. Zero representable delivery produces a zero-mass
callback; it never rounds inventory upward into a parcel.

Point messages and registered actors now use the same phase classifier. This
also repairs a separate adapter check that rejected water point messages in a
gas/liquid cell even though the native occupied-mixture boundary admitted them.
Gas-only and filled-liquid controls keep their original phase admission.

## Verification and remaining game gate

The new native group `native_oni_radial_consumption` passes on GNU Linux, GNU
Windows binaries under Wine and Linux ASan/UBSan. It covers the binary32 range,
exact combined delivery, retained positive traces, connected order, solid
barriers, gas and wet owners, overlapping requests, one callback per request,
mass-weighted temperature, complete U+K/species accounts, late atomic rejection
and exact checkpoint continuation. Native stderr is empty. Both managed
declaration suites pass 156 groups; the new packet tests exercise the actual
radius-three layout, unchanged outside batches, mixed-phase water admission
and whole-batch rejection.

The real-game scene `native-radius-domain` (`radius` in the matrix and Windows
PowerShell runner) now submits a radius-three request for 0.4 kg after a
0.05 kg oxygen injection. The receipt must exceed the centre cell's original
oxygen plus that injection, proving that multiple physical source cells were
used. It also requires exactly one actual game consumption callback, one
radial request, complete physical accounts and original native dispatch
avoidance. Packaging and real source/save/reload acceptance remain pending.

These checks establish the native transaction and packet boundary. An actual
duplicant breathing inside owned cells now has its own
[source and saved-reload control](native-live-breathing.md), with actual Unity
acceptance pending. Exact continuation with live external requests
now has an [accepted-input journal and exact replay account](native-frame-inputs.md);
its actual Unity acceptance remains pending. A snapshot-only predictor cannot
invent the duplicant's future requests. Full conserved metabolism, body/food
inventories, suit/tube providers, rectangular device reservations and
whole-colony ownership remain open under the
[completion plan](completion-plan.md).
