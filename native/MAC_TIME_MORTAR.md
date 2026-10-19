# Shared face integration across local time grids

`mac_kernel_time.*` supplies dyadic time coordinates and one shared quadratic
face-momentum integral. This is a pressure-stage primitive. The production
owner still uses its existing steps; automatic calm classification, the
multirate pressure root and local transport/remap are unfinished.

## Time coordinates and moments

Cell level `l` partitions a macro interval into `2^l` equal local intervals.
A face uses the union of its adjacent grids: `2^max(left,right)` pieces.
Integer piece indices determine each cell's local interval, midpoint and
relative duration. No accumulated floating clock or saved history is involved.
The level cap of 20 bounds index arithmetic; it does not choose a production
schedule or replace the owner's work budget.

For one piece of duration `h`, write its force as
`F(tau)=F0+sqrt(3)*(2*tau-1)*F1`, with fixed inertia `I` and area `A`.
With `k=sqrt(3)/6`, its shared moments are

```text
J1 = J0 + h*F0
Q0 = h*A/I * ((J0+J1)/2 - k*h*F1)
Q1 = k*h²*A/I * F0
```

`Q0` integrates the same quadratic momentum path used by `J1`; `Q1` is its
piece-local first Legendre moment. Either adjacent cell re-expresses it as

```text
cell_first_moment = P1_cell(piece_midpoint)*Q0 + (h/cell_dt)*Q1
```

Both cells receive the same `Q0` with opposite signs. Their different first
moments are projections of that same face path. For prescribed piece force,
`Delta K=(F0*Q0+F1*Q1)/A`, before storage rounding. Expressing force through
both cell traces gives the corresponding two-cell pressure-work identity.
No endpoint energy correction is used.

The ordinary path uses binary64. Flagged intermediate products retain their
range with `MacEnergy`, including a subnormal or underflowing piece duration,
a below-range sweep that carries a representable later parcel, and basis
products below the primary range. The returned full values must reach the
caller; storage rounding and its interval count/audit belong to that caller.
This helper does not establish admission of a full multirate pressure step.

Related background is provided by
[time-window interface coupling](https://arxiv.org/abs/2112.06099) and
[energy-consistent Petrov–Galerkin time discretization](https://smai-jcm.centre-mersenne.org/item/10.5802/smai-jcm.127.pdf).
The formulas and range paths here are implementation-specific; those papers do
not prove this mod's numerical gates.

## Component controls

`ma_mac_kernel_time_tests` checks signed forces, momentum reversal, zero net
sweep, interfaces at levels 0/0, 0/2, 3/0 and 2/3, every pair of levels 0–20,
normal final results after flagged intermediates, a retained `2^-1080` basis
moment, malformed admission and exact one/twelve-worker output. Face kinetic
work uses the original 128-epsilon account. The antisymmetric volume check
retains every exact accumulator component instead of rounding an aggregate.

`review_mac_kernel_time.py` independently reconstructs dyadic coordinates,
polynomial integrals, both cell projections and kinetic work with exact
fractions of the saved binary64 inputs. Scientific inputs and the 128-epsilon
bounds are pinned. It passes 1,057 checks and rejects fourteen altered captures.
All 51 Release CTests pass with the ONI backend and warnings as errors. This
suite and its reader also pass Debug ASan/UBSan, ThreadSanitizer and MinGW/Wine.
The preceding pressure/kernel sources and checkpoint formats are unchanged.
These are Windows-target Wine controls; actual Windows OS runs remain open.
These controls cover the shared-face primitive, without a nonlinear EOS,
transport, phase changes, a whole-owner advance or real-time acceptance.

```sh
cmake --build build-production --target ma_mac_kernel_time_tests
build-production/ma_mac_kernel_time_tests > CAPTURE
python3 native/tests/review_mac_kernel_time.py CAPTURE
```
