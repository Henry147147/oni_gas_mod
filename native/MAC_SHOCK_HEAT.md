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

The mixed root has 128 iterations and stops at 128 epsilons of its actual
temperature operands. Energy checks use the same 128-epsilon operand rule.
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

Static review identifies a further mixed-EOS range case to address before
default integration: the current nonlinear bracket evaluates each mixture at
the other's starting temperature. An endpoint trial can exceed a material's
valid range even when the required partial heat exchange is admissible. This
needs an explicit regression and a bracket confined to admissible thermal
states. The present controls do not cover that case, and no fix is claimed.

After that repair, owning integration must account for each actual heat stage,
retain an explicit disabled reference and pass all owning prerequisites before
another original strong-flow retry. Existing pressure/transport/owner source,
scientific tests and default game DLL are unchanged by this component. There
is no new game, production, strong-flow or release acceptance.
