# Gas/liquid coexistence in the native owner

Implementation is in progress. Existing gas-only and filled-water native
controls remain separate from this new path; no coexistence Unity result is
claimed yet.

The next bounded owner must retain gas and liquid constituents in the same
physical cell, with their actual EOS partial volumes, one mechanical pressure
and one stored internal energy. A liquid fraction comes from the completed EOS
state. It cannot be inferred from a scenario flag or nominal liquid density.
Gas hover percentages continue to use gas moles, with liquid inventory reported
separately. Native scalar compatibility fields remain a projection.

The first dependency is an explicit occupied-mixture point-transfer policy.
The existing single-phase entry keeps its previous admission behavior. The new
policy admits gas/liquid additions and selective removals only while the
complete resulting EOS state remains occupied and valid. Liquid parcels retain
their pre-edit pressure and elastic energy; gas parcels retain their actual
temperature. Removal carries local face momentum, kinetic energy and gravity
potential. Constituent and U+K+G accounts keep the existing 128-epsilon operand
bound, with no absolute floor. Every batch builds a separate candidate, so a
late invalid edit cannot publish earlier changes.

Before selecting this policy in the native worker, controls must cover gas
and liquid injection into a wet cell, selective removals of both phases, phase
appearance from a filled single-phase state, trace preservation, at-rest
injection and carried mechanics/gravity. Independently inspect saved operands
for mass, phase volumes, parcel energy and total energy/momentum. Reject late
invalid EOS, void creation, invalid policies and passive metadata without
changing the source checkpoint. Restore and subsequent physical continuation
must retain every authoritative field. Existing single-phase controls must
still pass unchanged.

Then add an explicit native scenario, restore its exact mixed state and publish
its actual partial volumes through the existing completed-snapshot boundary.
Verify source/save and fresh-process continuation in both installed players,
including manual mixture hover and calibrated GPU readback. Mixed-cell thermal
conduction needs a separate variable-capacity operator; the current
single-phase conduction rule cannot be relabelled as mixed-phase support.
Free surfaces, immiscible interface geometry, phase conversion, dissolved gases
and whole-colony ownership retain their separate completion gates.

The explicit component target `ma_mac_mixed_transfer_tests` now passes six
accepted transfers and eight rejection controls on GNU Linux, GNU Windows
binaries under Wine, and Linux ASan/UBSan. Checkpoint continuation matches
exactly, and the existing single-phase entry still rejects phase mixing. A
saved-operand review and native worker/Unity integration remain required.
