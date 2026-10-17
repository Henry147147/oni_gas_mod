# Geometric compatible momentum transport

The appended `quadratic_limited_linear` momentum selector (2) uses the same
predictor with [quadratic heat admission](MAC_QUADRATIC_LIMITER.md). Selectors
0/1 and the default remain unchanged. Both modes have explicit geometric and
owning controls; selector 2 has not passed the strong-flow gate.

The scoped Windows/Linux controls below pass. The explicit `limited_linear`
momentum mode uses actual primal/dual mass fluxes in MAC transport and applies the
[compatible limiter](MAC_MOMENTUM_LIMITER.md) before material heat deposition.
The standalone transport API retains its donor reference default. The
[owning timestep](MAC_MOMENTUM_OWNER.md) now selects reconstructed momentum
and also constrains actual transported work capacity.

Source face velocities use canonical J/I. Each free dual face has minmod
normal/transverse slopes; stencils touching fixed faces are flat in that
direction. Centered Courant numbers use half the sum of the two oriented mass
fluxes divided by source inertia. The face prediction is half the upwind
normal slope minus half the sum of both Courant-times-slope terms. Its change
from the donor is bounded between donor and target source velocities. No
quantity is projected to binary64 to form a slope, predictor or correction.

The oriented correction is mass flux times the bounded velocity change.
Each free/free edge applies the existing positive-remainder limiter to the
current upwind J and pending heat. Fixed face J, wall impulse and wall heat
are left untouched. Transported kinetic energy starts from the actual upwind
flux accumulation and receives the paired -W/+W contributions; it is never
replaced by an invented K+Q identity. Final local/global kinetic, momentum,
species/passive and material U accounts retain their original 128-epsilon
actual-operand bounds. Reconstructed material parcels supply their actual
mass fluxes, independently of momentum encoding.

The six control groups are:

1. Uniform velocities with varying gas composition are an exact donor-state
   identity, including all species/passives.
2. A wet uniform-composition material-parcel case and a varying wet case with
   a solid corner exercise both momentum components. Species/passives match
   donor transfers bit-for-bit; fixed wall impulse/heat stay identical.
3. Full canonical J below binary64 produces positive full correction/heat and
   reports unresolved material heat; positive helium/passive traces survive.
4. Invalid mode, boundary flux, stale projection and malformed shape reject
   without changing the original source.
5. Normal scalar momentum advection for a complete period on N32/64/128,
   two invariant transverse rows, prescribed C=0.2 and five N timesteps.
   Initial u=1+0.2 sin(2π(x+0.5)/N). Both error ratios stay below 0.35;
   N128 error below 0.3 of donor. All heat stays nonnegative, U stays in the EOS
   domain, trace inventories stay positive and endpoint total U+K passes the
   original bound.
6. Actual diagonal shear advection u_x=1+0.2 sin(2π(y+0.5)/N), u_y=1 on
   N8/16/32 square periodic grids, dt=0.1/N and ten N timesteps. Require
   invariant x, nonnegative heat, EOS-admissible U, positive traces, endpoint total energy,
   both error ratios below 0.45 and N32 error below 0.4 of donor.

A separate saved reviewer must derive source/target inertias, dual mass fluxes,
upwind momentum/variance/wall accounts, geometric stencils and predictions,
every correction and transferred work, material heat deposition and all
constituent/passive accounts from saved operands. Wrong geometric prediction,
work flux and trace constituent records must reject. Refined profile norms
must be computed independently. Existing component and world controls remain
required. Keep first failures, strict source/compiler identities and fresh
bounded output directories. Initial limits: build 180 s, new native 180 s,
review 120 s, at most 1 MiB per raw pipe for this expanded record format.
Existing prerequisite limits and scientific sources are unchanged. No strong
retry is authorized by this component alone; world integration and its full
prerequisites must pass first. No performance, game or shipping claim follows.

The first Windows build passed. Its first native run rejected the wet fixture:
varying volume sweeps with unchanged source volumes did not fill fixed target
volumes. That failure is retained. The repaired fixture uses constant,
divergence-free sweeps with the same varying velocities and wet inventories;
the target-volume account and all numerical criteria remain unchanged.

The next run completed the first four groups, then exposed an invalid new test
assertion: internal energy includes chemical reference offsets and is negative
for valid 300 K air. The test now evaluates each wave cell through the EOS and
requires positive temperature. Detailed records also retain source, advected
and final temperatures. Actual signed U is conserved under the original
128-epsilon U+K account; no energy is offset or repaired to pass a positivity
test. Refinement limits and positive constituent/passive checks are unchanged.

## Accepted component results, 2026-10-16

All 78 native groups and their saved reviews pass under strict MSVC on Windows
and GCC on Linux: 66 existing groups, six compatible-limiter groups and six new
geometric-transport groups. The new reviewer passes 3,964 exact-fraction saved
checks per OS and rejects all three geometry/work/trace mutations. The limiter's
420 saved checks and three mutations also pass after removing its dependency
on transport. Linux ASan/UBSan additionally passes both six-group suites and
their saved reviews, with leak detection, fatal findings and empty native
stderr. No independent agent review is claimed.

| Control | Coarse L1 | Middle L1 | Fine L1 | Refinement ratios | Fine / donor |
| --- | --- | --- | --- | --- | --- |
| Normal, N32/64/128 | 0.0107951145673 | 0.00327739832241 | 0.000927664717293 | 0.303600 / 0.283049 | 0.062769 |
| Transverse, N8/16/32 | 0.0831404939461 | 0.0297882482862 | 0.0119282835528 | 0.358288 / 0.400436 | 0.219670 |

Every original refinement, donor-comparison and 128-epsilon energy bound passes.
The saved review derives dual geometry, source/target inertia, predictions,
sequential corrections, paired transported work, signed material U deposition,
wall reactions and every constituent/passive transfer from actual operands.
Full corrections below binary64 and unresolved material heat remain explicit.
The wet-parcel fixture deliberately uses uniform composition/U/passive
concentrations; nonuniform material slopes have the existing independent
material reconstruction suite. These advection controls do not establish full
Euler accuracy or a general entropy proof.

These component captures preceded integration into the closed pressure owner.
The later [owning integration](MAC_MOMENTUM_OWNER.md) passes its prerequisites
and performs one changed-source strong-flow retry; collision refinement still
fails. It also carries the actual local stage bounds through cancellation and
checks full transported-energy capacity before each work transfer.
The default game DLL is unchanged. No C ABI, Unity, free-surface, production or
performance acceptance follows from this component.

Private immutable captures and terminal reviews are in
`local/occupied-staggered-validation/mac-momentum-reconstruction-windows-r4`,
`mac-momentum-reconstruction-linux-r6` and
`mac-momentum-reconstruction-linux-asan-r1`. Terminal-review SHA256 values:

- Windows: `d61b4fb39776052f729b2394ee47841bb90da5bef4c7a93121225cfeb652e22c`.
- Linux: `01bc920e1e01e773266e39f9a27597bd2e990b8f544168d91045ae808445aaed`.
- Linux ASan/UBSan: `d1c69f78702b6d4d13060967ad34cabcbb8d8d678855ebf11b065afefda50e3b`.

The first Windows R1/R2 failures described above remain retained. R3 was
superseded before execution to correct a const-pointer initializer type. Linux
R4/R5 compiled but their preparation/approval scripts rejected malformed path
handling before any native execution; their first outcomes remain retained.
No numerical bound was changed for those harness repairs.

After integrating shared `main` through `8ba8e55`, a source comparison confirms
unchanged computational MAC sources (allowing Git line-ending normalization).
The CMake change only moves the optional native-backend include; that backend
is disabled in these controls. Fresh merged-source Windows/Linux builds pass
the 12 limiter/geometric groups and both saved reviews. Their private cohorts
are `mac-momentum-reconstruction-{windows,linux}-postmerge-r1`; terminal SHA256:

- Windows: `9db24d880c31588131756805c52d91e7c1b24b2051a12ad8be4905a99dfc6d77`.
- Linux: `a6d20684b7d6071f70e8681133c2cc08710a3f770fc45d04f71dd296f854ed61`.
