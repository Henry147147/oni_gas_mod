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

Native scenario 4 now selects that policy and retains both phases in all six
occupied cells. Its published liquid volume is the EOS's compressed volume;
gas partial volumes use each constituent's ideal-gas volume at the same
completed temperature and mechanical pressure. The managed reading retains
those exact phase facts, including trace headspace smaller than the rounding
unit of the liquid-volume field. A scenario flag never supplies the fraction.
Scenario 4 checkpoints and durable actor records admit both gas and water;
existing scenarios retain their original phase admission.

`native-mixed-domain` selects the scene in either player runner. The matrix
scene is `mixed`, and `run_colony_validation.ps1 -NativeDomain mixed` exposes
it on Windows. The portable reviewer independently reconstructs every
constituent partial volume, compressed-water volume, gas moles/mass and complete
occupied volume. Restored scenario 4 reports select the same strict check.
Liquid world rendering and free-surface dynamics are explicitly unverified.
Source/save and fresh-process continuation still require actual Unity controls,
including manual mixture hover and calibrated GPU readback. Mixed-cell thermal
conduction needs a separate variable-capacity operator; the current
single-phase conduction rule cannot be relabelled as mixed-phase support.
Free surfaces, immiscible interface geometry, phase conversion, dissolved gases
and whole-colony ownership retain their separate completion gates.

The explicit component target `ma_mac_mixed_transfer_tests` now passes six
accepted transfers and eight rejection controls on GNU Linux, GNU Windows
binaries under Wine, and Linux ASan/UBSan. Checkpoint continuation matches
exactly, and the existing single-phase entry still rejects phase mixing.
The saved-only reviewer reconstructs caloric/elastic U, phase volumes, face
inertias, carried momentum/kinetic/potential energy and complete balances with
90-digit arithmetic. All three captures pass 1,036 checks and reject altered
parcel U, trace mass, face momentum and liquid volume. It imports no native
solver code and retains the original operand/volume bounds. The native worker now passes its separate pressure-transport, water-injection
and oxygen-removal callback control, complete constituent/U+K accounting,
late-batch rejection and exact restored continuation. It is the fifteenth
backend CTest; all 15 pass on host Linux and GNU Windows binaries under Wine.
The new group also passes Linux ASan/UBSan. Four additional installed-assembly
reading groups pass on both platforms, alongside 230 Python checks (four
optional/platform skips). Actual Unity coexistence acceptance remains pending.
