# Rectangular device consumption

The native fluid boundary accepts ONI's width/height overload of
`SimMessages.ConsumeMass`. It reserves the complete footprint before dispatch,
including requests whose anchor is outside the custom region but whose
rectangle overlaps it. Requests spanning native and custom owners reject
before either owner changes. Entirely outside requests keep native dispatch.

The additive rectangular-consumption capability is version 1. The existing
64-byte transfer ABI 2 remains unchanged. Nonzero flags bits 16–23 select
height; bits 8–15 then specify width. Height zero retains the original point
or connected-radius encoding. The adapter checks the capability before
preparing a source scene.

The lower-left anchor and bottom-to-top, left-to-right order match the inspected
native rectangular loop. Solid cells are skipped within that explicit
footprint. A zero-width request produces one zero-mass callback. A rectangle
cannot wrap a row or extend outside its reserved physical owner.

Allocation uses the same aggregate binary32 callback lattice as
[radius consumption](native-radial-consumption.md). Every selected parcel is
representable, their sum is exact, and finer positive residual inventory stays
in each source. Overlapping requests see earlier reservations in the same
atomic batch. One receipt and one original game callback carry the combined
mass, mass-weighted temperature, complete material energy, kinetic energy and
momentum. The original source buffers remain unchanged if any later request
fails. Accepted-input journals retain both dimensions and replay the complete
physical result exactly.

`native-rectangular-domain` selects the actual Unity control on either Python
player runner. The matrix and Windows PowerShell selector is `rectangular`.
The scene injects 0.05 kg of oxygen, then requests 0.4 kg across the three-by-two
footprint. Acceptance requires one callback delivering more than the original
anchor inventory plus the injection, complete physical accounts, no original
native mass dispatch, normal rendering and an ordinary paired save. Reloads
must pass paused frame-zero saves and complete exact continuation.

Native gas/wet controls cover row order, skipped solids, the binary32 exponent
range, overlapping requests, traces, full U+K/species accounts, late rejection
and exact journal replay/checkpoint continuation. Both installed declaration
suites pass 174 scenarios, and Python passes 265 checks with four explicit
skips. All 20 native groups pass on GNU Linux and as Windows binaries under
Wine. Sanitizer, compatibility-build and actual Unity controls are in progress.
Pumps, pipe/container transfers, conversion chemistry and whole-colony device
ownership remain separate parts of the [completion plan](completion-plan.md).
