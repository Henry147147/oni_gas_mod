# Swept momentum with completed-state admission

`MacMomentumReconstruction::swept_physical` is explicit selector 3. It implements
both repairs identified by the [dual-remap diagnostic](MAC_DUAL_REMAP_DIAGNOSTIC.md).
Selectors 0, 1 and 2 retain their laws; the default remains 1. This component
does not enable production colony takeover or replace the game backend.

## Geometry and compatible work

The reconstructed parcel path uses the actual parcel volumes. The raw donor
path derives a volume sweep from that donor's mass and source volume, as
required by its existing transfer contract. Reconstructed parcel mass is never
used as a substitute for its independently supplied volume.

The same half-cell topology supplies dual volume sweeps. Their time-centred
first moments determine source dual centroids and swept slab centroids.
Least-squares gradients use physical centroid distances, with neighbour bounds;
both true one-dimensional axes have explicit rank-one stencils. Unresolved
geometry/stencils retain a reported first-order fallback. Fixed half dual cells
retain the donor wall treatment and receive no reconstructed impulse.
These are discrete swept first moments, not a general polygon-overlap proof.

For each free edge, the correction removes R from left J and adds it to right J.
Compatible paired work is `W = (u_left + u_right) * R / 2`. Each side's signed
work-energy change is `(u_left - u_right) * R / 2 - R² / (2 I_side)`.
`MacFaceEnergy.remap_work_energy` stores these changes separately from the
nonnegative upwind variance and wall heat. An edge limiter retains nonnegative
transported kinetic energy, including at a stationary face with no available
work. It does not require the combined variance/work remainder to be positive.

After both components and signed material-energy deposition are complete, the
candidate must meet the original species/passive, momentum/wall, stored
U+face-K, EOS-domain and entropy conditions. The entropy allowance is still
128 binary64 epsilons of the actual endpoint stored-U/T operands. Chemical
reference energy may be negative. No absolute energy or entropy floor is added.

A rejected candidate triggers a common correction-fraction search, without
changing the material parcels, timestep, grid or model coefficients. Even the
zero-correction donor fallback must pass those same completed-state checks.
Only an accepted complete candidate is returned. Reports retain the correction
fraction, rejection count, per-edge work limiter, separate signed work,
centroids, gradients and compatible-work events. The owning interval counts
the new events and carries its signed-work/rejection diagnostics. Checkpoints
retain selector 3 through the existing complete settings field; readers that
do not support that selector reject it.

## Current verification

Strict Windows MSVC, Linux GCC and Linux ASan/UBSan runs pass six new groups
per platform. The actual canonical outputs pass 138 selected affine readings
across 24 cases: both axes, both sweep signs, stretched and translated controls,
uniform velocity, zero sweep, a consistent `q = dt * u` control, below-binary64
full momentum and both true one-dimensional axes. This checks the actual
returned state, including both geometry and correction admission.

Those affine controls do not establish the pressure/remap source convention
under curved deformation. The later [curved nodal diagnostic](MAC_CURVED_NODAL.md)
reproduces 24 failed face-value readings on Windows and GNU Linux, while the
alternate dual-volume-mean source passes. The original pressure-step handoff
also separates the face position and dual centroid. A conservative momentum/K
mapping remains required; the successful alternate-source control is not an
accepted numerical repair.

The saved reader independently checks dual first moments and slab predictors,
upwind transport, all 240 compatible-work events, signed material deposition,
original balances, the actual EOS and entropy. It performs 169,236 checks per
platform and rejects corrupted actual momentum, paired work and trace gas.
There are 120 negative combined face remainders and four work-limited edges;
nonnegative variance and transported kinetic energy remain independently
verified. The review launches no native process or native EOS.

All 72 dry/wet, material-reconstruction, shock-heat and compression combinations
pass exact save/restore continuation per platform, including gravity, wall
corners, traces, passive inventory and dry thermal conduction. Both cross-OS
checkpoint reads preserve their original images and exact local continuation.
Independent Windows/Linux computations are not required to produce identical
checkpoint bytes.

The original lower-temperature boundary control correctly accepts its safe
full correction. A separate upper-boundary control rejects both the unsafe
full candidate and unsafe donor fallback without changing its source.

A subsequent two-dimensional deformed remap exercises actual intermediate
admission. Its unrestricted candidate reaches 299.9620683 K, while its donor
fallback remains above 300.0009458 K. A fixture-only EOS lower boundary of
300 K selects correction fraction 0.3356288673, with 30 rejected candidates on
Windows and 28 on Linux/ASan. The returned temperature retains the original
EOS arithmetic allowance of 32 epsilons; it is not clipped or warmed to a floor.
An independent 80-digit reading of the canonical state verifies that original
boundary and all original conservation/entropy bounds. All three platforms
pass 7,014 saved checks and three corruption rejections. This does not change
any production or strong-flow model boundary. Private witness evidence is
`local/mac-swept-admission-r1`, terminal SHA256
`8cb7108b4b18e04ec721e5619197666522ef7ad1df403001537d350e92bc8025`.
The published `ma_mac_physical_admission_tests` and saved reader preserve that
fixture verbatim. Strict captured public builds pass on all three platforms,
and every native stream is byte-identical to its matching private capture.
Together with the complete prerequisites, this gives 186 groups and 31 saved
reviews per platform. All 97 checked numerical inputs remain unchanged, and
the CMake difference adds only this explicit test target and its options.
Public readiness SHA256:
`91776d52c534ec3670ada129a8fd6481985951ef332c9e3fcf48e7506d216109`.

The initial global work restriction failed the consistent affine control at
a stationary face. That failed capture is retained; the per-edge work limiter
repairs it. A later test incorrectly expected the safe lower-boundary scene to
need restriction. Its assertion failed after all 72 continuations had passed;
the test is corrected and the unsafe-fallback rejection is added. Neither
failure is discarded or counted as acceptance. Four old diagnostic/transport
streams per normal platform remain byte-identical to their previous captures.

Private evidence: `local/mac-swept-momentum-r5`; terminal-review SHA256:
`840923e4d06b39caab44bb5b2787e305a1f739891d33bf4721c0976bae249a1a`.
The sanitizer build uses `MA_SANITIZERS` alone, with inspected compile rules
covering the new source and linked numerical dependencies. Every owned process
and pipe is terminal. This evidence precedes only an explicit standard-library
include added to the public header; numerical source and test bodies match.

The subsequent full `local/mac-swept-physical-owner-r1` cohort compiles that
final header and passes 185 groups and 30 saved reviews on each normal OS and
under Linux ASan/UBSan. It retains all 179 previous prerequisites and adds the
six physical-mode groups. Both normal backend ABI/detour controls pass, and
all three Windows game libraries are freshly built. The merged GPU-coordinate
adapter passes 126 Windows snapshot groups and builds with only the two known
MSB3277 reference conflicts; four WSL runner suites pass 116 tests without
skips. No game is launched. Both protected DLLs remain unchanged. Component
readiness SHA256:
`394416315e267b3158b1265a95cc4e405d959e26b2fdf578184eb94b04195a27`.

## Remaining acceptance

The changed-method Windows run passes all three Sod resolutions, then fails
the original collision density refinement bound. Density/pressure/velocity
ratios are 0.8563318058 / 0.7958889962 / 0.6133285918 against 0.85. Density L1
falls from 0.04270701864 at N64 to 0.03657137839 at N128. No grid, timestep,
coefficient, norm, 180-second budget or 2-MiB capture bound changes.

The native process exits 1 after 67.12 seconds without timeout. Its complete
573,152-byte stream passes the 59,205-check partial reader; all five completed
profiles meet their individual physical/accounting criteria. Collision N256,
all separating-flow profiles and Linux strong flow remain unexecuted. The same
failed method is not repeated on Linux. This result improves the components
and diagnoses their limits; it does not pass the strong-flow gate. Selector 3
remains opt-in and the default stays 1. Retained strong terminal-review SHA256:
`43eb2b4485940d8f14e7b4e0ce7765897795472cee7736e7bfc01d0a125898ca`.

Investigate the remaining accuracy defect before a distinct-method retry.
The previous selector-2 failure retains its own status and evidence.

The later [bounded carried-energy mode](MAC_BOUNDED_ENERGY.md) diagnoses and
addresses a further stationary-donor restriction as explicit selector 4.
Selector 3 and its retained strong-flow status remain unchanged.

Production world ownership, exact vacuum, physical material/phase/device
coverage, metabolism, live actors, Windows/Linux game UI, performance and the
full first-playable colony criteria remain required by the overhaul plan.
