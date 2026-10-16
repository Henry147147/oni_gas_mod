# Compression-activated heat redistribution

This separate occupied-grid component investigates the central overheating
localized in the [failed collision gate](MAC_RIEMANN.md). It is not integrated
into the owning timestep or game backend. Existing scientific inputs and
acceptance limits remain unchanged; no new strong-flow acceptance is claimed.

The motivation is the excess heating on shock formation studied by
[Noh (1987)](https://www.sciencedirect.com/science/article/pii/002199918790074X),
who compares artificial viscosity with an additional heat flux. This component
is an explicitly specified regularization for this solver, not an implementation
or validation of Noh's complete algorithm. It is separate from material thermal
conductivity and from physical gas diffusion.

## Pair operation

At fixed constituent masses and volumes, let CL and CR be the EOS equilibrium
heat capacities and Gdt the integrated nonnegative thermal conductance. Find
the signed heat Qeq that partitions the two actual internal energies at a
common temperature. Constant-capacity gas or liquid endpoints use the exact
capacity-weighted partition. Gas/liquid endpoints use a bounded scalar root
with the full EOS, retaining elastic and reference energies. No caloric-only
temperature is substituted for a mixed EOS.

Set x = Gdt (1/CL + 1/CR), f = 1 - exp(-x), and transfer Q = f Qeq from left
to right. Apply -Q and +Q to the two actual stored U fields. For constant
capacities this is exact two-body conduction and has the semigroup property.
The mixed-EOS extension uses initial capacities for its relaxation rate; it is
a first-order nonlinear regularization, not an exact nonlinear conduction
solution. It still moves towards the same thermal equilibrium and conserves
the actual pair energy within its operand roundoff bound.

`expm1` resolves ordinary small x. Below 2^-53, f=x has relative truncation
error below 2^-54 and retains the full exponent. At x>=64, one is the correctly
rounded binary64 fraction. No positive range contribution is replaced by its
zero binary64 projection. Reports retain the full heat, fraction and relaxation
number, and flag U updates too small to resolve in the primary binary64 field.

The mixed root has 128 iterations and checks actual temperature operands and
the energy-storage certificate described below. Energy checks use the same
128-epsilon operand rule.
Temperatures must remain in their original interval without crossing thermal
equilibrium beyond operand roundoff. Actual stored-state entropy must not
decrease beyond 128 epsilons of the actual stored-U/temperature operands.
No field is clipped or replaced to make an account close. Unsupported EOS
states or unresolved roots return no candidate and never mutate the source.

## Grid operation

For a normal cell pair, take the larger positive normal closing-velocity
difference in its two cells. If neither cell compresses, the face is inactive.
The integrated face conductance is dt times this speed, divided by grid
spacing, times the harmonic equilibrium heat capacity of the two cells.
Capacities and conductances are frozen from the source of one component call.
The dimensionless coefficient is one; no sound-speed term or fitted coefficient
is introduced. Each active face receives a half step in forward order and a
half step in reverse order, reversing the entire axis order too. For constant
capacities this symmetric composition gives second-order temporal accuracy.

There is no heat flux through a wall. Stationary contacts, uniform translation,
expansion and purely tangential shear do not activate the compression switch.
Uniform temperature produces zero heat even on an active face. Every species,
including traces, passive quantity, grid field and canonical face momentum is
unchanged. The component reports actual endpoint energy/entropy and each
ordered pair transfer; it does not advance the world clock.

## First-source validation plan

Six native groups and a separate saved-data reviewer are required on Windows
and Linux before integration:

1. Exact unequal-capacity gas/liquid pairs, reflection, stiff relaxation,
   inactive pairs and the constant-capacity semigroup.
2. A true mixed gas/liquid equilibrium partition, positive heat below binary64
   range and a full-range stiff relaxation number.
3. Periodic and solid-corner wet grids, with complete actual U, constituent,
   passive, geometry and momentum accounts.
4. Stationary contacts, tangential shear and compressed uniform temperature.
5. A fixed three-body constant-capacity network with an independent exponential
   solution. N1/2/4/8/16 time refinements must reduce error by less than 0.3
   successively; the final maximum temperature error must be below 0.05 K.
6. Full-range grid activation with unresolved U flags and eight immutable
   invalid-input rejections.

The data-only reviewer reconstructs the EOS and thermal partitions in 80-digit
arithmetic, the ordered face geometry from source J and masses, both signed U
updates, the complete evolving pair chain, trace/passive preservation and
actual entropy/energy accounts. It must reject altered heat, trace and geometry
records. Builds retain strict floating-point flags and warnings as errors.
Execution bounds are 180 seconds for a build or review, 60 seconds for native
controls and 1 MiB per raw pipe. First failures are retained. No independent
agent review is claimed.

Passing this component does not prove the owning shock profile improves. That
still needs explicit timestep integration, all owning prerequisites, and the
original strong-flow gate before production or game acceptance.

## Accepted component results and remaining work

All six native groups pass strict Windows/MSVC and Linux/GCC builds. Their
saved reviews pass 11,040 and 11,028 checks respectively and reject all three
heat/trace/geometry corruptions. Linux ASan/UBSan also passes all six groups
and 11,028 saved checks, with leak detection and fatal findings enabled and
empty native stderr. The fixed three-body network's errors are 2.146985,
0.536746, 0.134136, 0.033530 and 0.008382 K; the original 0.3 refinement and
0.05 K absolute limits pass unchanged.

The first Windows saved review fails at its compression-switch reconstruction.
It compares an unrounded J/I quotient against the represented normal velocity
used by the solver. One saved difference is 2.2888744e-16 in exact quotient
arithmetic, versus 4.4408921e-16 after the actual quotient rounding. The review
now reconstructs that represented velocity and its normal difference before
checking geometry. The native code, data and limits are unchanged. A fresh
saved-only review passes the original Windows output; neither the native
controls nor the build are repeated for this parser correction. The first
failed receipt remains retained.

Private immutable cohorts under `local/occupied-staggered-validation`:

- `mac-shock-heat-windows-r1`: strict build and six native groups pass; first
  saved-review failure, terminal SHA256
  `1f3cbd4f3f383815772f2044d04054297efde8d188140e744945fb6d2679d4d6`.
- `mac-shock-heat-windows-r2`: accepted saved-only correction using those same
  native operands, source-freeze SHA256
  `6b5aa9ce8583cf60a5bb849ab92055ce0bb4e5d1bde00caa745932e6f7467ff0`,
  terminal SHA256
  `3c1518c688f6667648cf4377e0a16568ba628945ea6d3a90424adc9555dac6f9`.
- `mac-shock-heat-linux-r1`: source-freeze SHA256
  `051888eb7f58a5f8940a2382fe03af8ac1b01f5afb2d1baa13a43858f2e1c212`,
  terminal SHA256
  `b7d41d3c6eca5a077172f354ef3097e5b9f5da847b5f65d3f186c70e220a3af9`.
- `mac-shock-heat-linux-asan-r1`: source-freeze SHA256
  `100adafe306a8af5efe5d103bd6e2dadbf13d57f95db00437a066a0ab3011082`,
  terminal SHA256
  `10006712c02488f520e92a2c1d53b04d5e86fbe180a51b03315c030e775d5973`.

The first cohort's nonlinear bracket evaluates each mixture at the other's
starting temperature. Static review identifies a valid heat exchange for which
that endpoint trial leaves the material range. The following changed-source
regression and repair address that case; the original six-group receipts above
do not establish this additional coverage.

Owning integration must account for each actual heat stage,
retain an explicit disabled reference and pass all owning prerequisites before
another original strong-flow retry. Existing pressure/transport/owner source,
scientific tests and default game DLL are unchanged by this component. There
is no new game, production, strong-flow or release acceptance.

The checkpoint is rebased over the shared Windows-domain and fault-freeze
changes at `83b1541`. A source comparison confirms the heat implementation,
reviewer and all its static dependencies are unchanged. The incoming CMake
change affects packaging of the two shared game DLLs only; the changed native
backend files are outside this component's build. No identical native control
is rerun for that merge. The private source-review SHA256 is
`db2695e49247b7b2aa135beadb2ed6ef1edc6d3f5aca372a7ccfc80a8268f04c`.

## Mixed-EOS bracket repair

A new standalone regression runs before changing the accepted heat code. A
small nitrogen inventory at 10,000 K exchanges heat with a 250 K, 2 GPa wet
mixture. Both initial states and the common-temperature partition are valid.
Trying the wet mixture at 10,000 K exceeds its compression domain. The first
strict Windows build passes, then the regression exits 1 with normal closed
pipes at `Cell cannot fit inside its compression and thermal domain`. The
original six groups and strong-flow gate are not replayed. Private cohort
`mac-shock-heat-windows-bracket-first-r1` has failed terminal SHA256
`ccfa5de4a2d8c489f93c8585dd572be16b3f7247573783aa334a4430a3bf870c`.

For this zero-expansion elastic EOS, at fixed volume,
Cv <= Ceff <= Cv + nR. In the mixed branch the extra capacity is
nR times pCL/(pCL+Vgas), where CL is liquid compliance; this fraction lies
between zero and one. Thus Qeq is bounded above by
min(Cv_hot+nR_hot, Cv_cold+nR_cold) times the initial temperature difference.
The code constructs that bound with an outward 128-epsilon operand allowance
and bisects heat directly. It never evaluates either mixture at the other's
initial temperature. A trial beyond a thermal/compression domain narrows the
upper heat bound; other EOS failures still propagate without a candidate.

Cancellation in U +/- Q must not erase the roundoff incurred by those actual
operands. Each successful nonlinear root therefore exports its two actual
equilibrium temperatures, their full residual, rejected-trial count and bound.
The bound is 128 epsilons of the two actual temperatures plus, for each cell,
(abs(U_before)+abs(Q)+abs(U_after))/Cv. Since Ceff>=Cv, this bounds temperature
uncertainty from storing the energy update. This additional storage certificate
is accepted only on the cooling side of equilibrium. A temperature crossing
still requires the original 128-epsilon temperature-operand bound. No stored
U, heat or temperature is adjusted to make the certificate pass.

The expanded suite has eight groups. In addition to the six original groups,
three asymmetric/high-capacity-ratio mixed brackets must complete, including
reflection and a 100,000 K small inventory. Uniform-pressure, nonuniform
temperature/composition wet contacts must remain bit-identical under the heat
operator. The saved reviewer must derive the storage certificate from actual
U/mass/Cv operands and reject an altered equilibrium-temperature record as a
fourth corruption. All previous physical and time-refinement limits remain
unchanged.

The first changed-source run completes the original six groups and new cases
11/12, then its auxiliary 2 K case fails the pre-existing material-specific
energy-reference precision guard. A separate source-only diagnostic initializes
that inventory at U=-308.58960610408968 J, then ordinary EOS evaluation rejects
it before any heat operation. This is not an admissible starting inventory for
the heat test under the existing precision contract. The auxiliary asymmetric
cooling case now uses 200 K. No EOS bound, heat method or original scientific
input is changed for that fixture correction. Both failed receipts are retained:
`mac-shock-heat-windows-r3`, terminal SHA256
`49cd2ea4f30f6bab3431ba052d4d9a71dc73bd9f3a18a20efaed2571e36a6dba`,
and `mac-shock-heat-windows-source-first-r1`, terminal SHA256
`3b38ffe3c99347661f24803a5b4d7e4aebfe080c5bcb0e83e154521b70e0a32b`.

The repaired eight-group suite passes strict Windows/MSVC and Linux/GCC
builds. The saved reviewers pass 13,270 / 13,252 checks and reject all four
heat/trace/geometry/root corruptions. Linux ASan/UBSan also passes all eight
groups and 13,252 saved checks with empty native stderr, fatal findings and
leak detection enabled. The bracket regressions actually reject one/two
inadmissible intermediate trials in cases 11/12, then return valid partitions.
Actual energy, entropy, monotone-temperature and time-refinement limits pass
unchanged. The source-only 2 K precision failure remains separate evidence.

Private accepted cohorts:

- `mac-shock-heat-windows-r4`, source-freeze SHA256
  `aa9ca66278936cf68a3a5dc4a831cf408b2889c771998c64d559a64c50d6c609`,
  terminal SHA256
  `e8d52f1b66467c7065f91a690587b6d4fe9d47a021942614127f0394b3697f40`.
- `mac-shock-heat-linux-r2`, source-freeze SHA256
  `c647246be9e79c2f5f6dfdef17f35b10a8e58aa0dab08d9a042bb937786861de`,
  terminal SHA256
  `970525ec22e1e5e5a58db8cdf8d1b96a037ca92a749bd2f8c3c2ef0ae90c1bca`.
- `mac-shock-heat-linux-asan-r2`, source-freeze SHA256
  `5f5c8b4f8ee0c75e66cabcd22317b2ac0a37f370ba007692728f45312a436743`,
  terminal SHA256
  `71196dbabf3cfc99066007957d531d90828f760ac6ad9c3d4cb1af893dacd7aa`.

The component remains excluded from the owning timestep and game dispatch.
There is no strong-flow retry or new game, production or release acceptance.

The repair is rebased over the shared physical-conduction work at `5d39a15`.
Its heat implementation, EOS dependencies and reviewer remain unchanged.
The incoming owner stages use material conductivity and are disabled by
default in standalone settings; they are distinct from this regularization.
The source-review SHA256 is
`7a06a278ddfd168b4f7feecf4a60504097749eb943710d0aa371ab0ff500bd40`.
Owning integration must also exercise both operators together when physical
conduction is enabled. No merged-runtime or new game evidence is inferred
from that source comparison.
