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

The lower-temperature boundary control correctly accepts its safe full
correction. A separate upper-boundary control rejects both the unsafe full
candidate and unsafe donor fallback without changing its source. A nontrivial
intermediate global correction-fraction witness is still required; the current
boundary control must not be represented as one.

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

## Remaining acceptance

Complete the full original prerequisites and independent saved reviews before
running the changed method through the original strong-flow gate. Keep its
grids, timesteps, coefficients, norms, 180-second budget and 2-MiB capture bound.
The previous selector-2 strong result remains failed and is not rerun unchanged.
Do not promote selector 3 on the strength of affine or checkpoint controls.

Production world ownership, exact vacuum, physical material/phase/device
coverage, metabolism, live actors, Windows/Linux game UI, performance and the
full first-playable colony criteria remain required by the overhaul plan.
