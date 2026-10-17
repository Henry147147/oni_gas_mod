# Gas/liquid coexistence in the native owner

The first WindowsPlayer source/save and local continuation now pass. The
same-process and cross-platform controls remain separate acceptance steps.
Existing gas-only and filled-water scenes retain their original behavior.

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
optional/platform skips). The WindowsPlayer controls below now pass; native Linux and heated-mixture
acceptance remain separate gates.

## First WindowsPlayer source and repaired reload

Frozen `51d5ce9` and native v15 pass an actual WindowsPlayer/Proton OpenGL
source in a fresh nine-world cluster. All six cells retain gas and water with
compressed EOS partial volumes through physical frame 121 and a complete
paired-save readback. Its file SHA-256 is
`62338587bca44026737c23248816f2bb30eb3ba82559133e71191670e66ade4f`;
the complete physical image is
`970fbe7484369770dbbc66e0ae7a612f2d0c881068a4462d065e3c69e2920d55`.

The actual source PNG was viewed with its completed cell 103106/revision 121
hover: nitrogen 82.6584% and oxygen 17.3416% by gas moles, liquid volume
20.59537543%, pressure 100317.68126107301 Pa, temperature 300.0000227607583 K
and total mass 206.8798267410566 kg. The bulk rows, trace notice and breathing
status are visible. Image-review SHA-256 is
`3c5be23bb6f342eb738ce9f1d6bca81fcadfd00b365e383c05d2a3036993c11d`.
This accepts the bounded hover facts; liquid world geometry remains unverified.

The first reload compares all 120 complete frames exactly. A second-session
native save-root startup race is then repaired by the
[frame-zero lifecycle wait](native-orbital-references.md#waiting-for-the-restored-native-start-lifecycle).
With `2df8e7b` and the unchanged v15 native libraries, both Windows sessions now
pass all 120 complete frames, paused autosaves/readbacks, native readers and
calibrated GPU checks. The second session naturally waits once for ten native
roots while retaining the exact initial image at frame zero. Both final images
equal the original successful session's
`020d8824ffb622c72c3c4b42d0a2e8ee759cb297895902730dd0b8eb46246c20`.
The raw result SHA-256 is
`65199f80815acaf94a6b4c238aaf10c9ea628250ebd6b514f371f9858b855f76`.
Native Linux now also loads the unchanged Windows source twice. Both sessions
complete paused saves/readbacks, readers and GPU checks and compare all 120
complete frames exactly. Its second session naturally waits once for the same
ten roots without changing the original frame-zero image or physical time.
Both final Linux images are
`102767292cfcbc8a48b2386cf7036cfdcdaa5e520592827f39be71b1d874991c`.

An independent complete-state comparison checks all 119 stored quantities, all
16 registry entries, exact material/model/geometry/settings/walls/clock/revision
and native cell mapping. Both inputs are actor-free. Thirty-four quantities
differ numerically; the largest relative difference is 1.9120647643104946e-9,
below the original 1e-8 bound. Zeros remain exact and traces receive no absolute
floor. Comparison SHA-256:
`a8b9ec4598969a7a7d9541421fe9daee7d8e2500659ee2dfee713948f55d93cd`.
The source/four-session saved-receipt review passes 36 checks; SHA-256
`aaed03f27ffa1f2ac611e462d8227d2adcba295532db0178475c9c44fa103846`.
This accepts bounded cross-platform occupied-mixture continuation, without
liquid interface rendering, actual Windows OS or whole-colony acceptance.

The separate [nonlinear conduction component](native-mixed-conduction.md) now
passes its native and saved-only controls. The new scenario 5
heated-mixture fixture still needs its own Unity source/load acceptance;
scenario 4 continues to disable conduction.
