# Retained material-node 1-D state

`ma_occupied_staggered` (`native/src/occupied_staggered.cpp`,
`occupied_staggered.hpp`) is a dormant 1-D experiment. It stores nodal momentum
on a material mesh instead of rebuilding it from cell momentum every step, the
defect of the `FluidWorld` occupied 1-D mode
([GAS_VACUUM.md](GAS_VACUUM.md#fluidworld-occupied-1-d-mode-fails)). It links to
`ma_core` for the EOS bridge and the occupied pressure/work solve; nothing else
uses it. It has no Eulerian remap (the opt-in remap experiments built on it are
in [STAGGERED_EXPERIMENTS.md](STAGGERED_EXPERIMENTS.md)) and validates no vacuum
front, shocks, general topology, performance or gameplay.

## State and initialization

The API is `OccupiedStaggeredState`, `initialize_occupied_staggered[_into]` and
`advance_occupied_staggered`. Initialization takes all positive material cells
and all N+1 nodes together, a positive cross-section, nonnegative downward
gravity and traction-free or rigid ends; empty arrays mean exact vacuum.
Interior gaps, cavitation, unsupported materials and nonpositive intervals
fail. Materials are the reference ideal gases and elastic liquid. The state
deep-copies the EOS and registry into a const allocation, so later changes to
the caller's model cannot reach it.

Nodal inertia is `I=(Mleft+Mright)/2` (vacuum contributes zero), J is canonical,
velocity is J/I and a rigid wall node has zero velocity. Cell views:

```
P_i        = M_i*(uL+uR)/2
variance_i = M_i*(uR-uL)^2/8
K = sum_nodes J_j^2/(2 I_j) = sum_cells (P_i^2/(2 M_i) + variance_i)
```

The variance is mechanical energy, not heat. Supplied cell P must match the node
means within `32*epsilon*(M*(|uL|+|uR|)/2+|P_supplied|)` or initialization
rejects; no cell-only state is converted silently. `preserve_temperature` keeps
U and reports the added nodal K as declared energy; `preserve_total_energy`
deducts it from U and validates the EOS domain. Both build a full candidate and
commit by noexcept move. Aggregate species mass is range-checked; overflow is a
typed failure, never an infinity.

## Pressure step and accounting

Each call passes the retained node velocities and inertias to the implicit
isentropic pressure/work solve and keeps the new J and K, with no PIC averaging,
artificial viscosity or correction heat. With requested volume change
`delta_i`, secant work pressure `p_i^w`, midpoint velocity `ubar` and residual
`r_i = delta_i - A*dt*(ubarR-ubarL)`:

```
deltaU_i = -p_i^w*delta_i
deltaK   = sum_i p_i^w*A*dt*(ubarR-ubarL) + body_work
deltaPE  = -body_work
delta(U+K+PE) = -sum_i p_i^w*r_i
```

The last term is the finite nonlinear-solve error, reported as signed residual
work, apart from stored-U rounding and force and gravity arithmetic. The
remainder must fit `128*epsilon` of the individual terms and the total energy
error the `2e-11` local budget. In the compressed-liquid regression (8796 J of
work) the energy residual `5.09e-5 J` is almost all residual work, and entropy
changes by `1.7e-7 J/K`: energy and entropy are not exact. Rigid walls take the
reaction impulse and do no work; the momentum bound is `64*epsilon` of the
absolute terms. Any range or precision failure rejects the whole candidate.

## Coordinates and numerical domain

Nodes use relative coordinates and a compensated two-double origin, anchored at
a rigid end or else the left node, so translation moves the origin without
subtracting large positions. Origin and cell-volume changes must match the
requested ones within 32 epsilon. Unresolved geometry is typed-fatal; there is
no density, pressure, species or volume floor, and a step that cannot advance
represented time fails.

`StaggeredInitialization.retain_local_geometry` stores each node as
`relative_position_m` plus `relative_position_low_m` and uses both everywhere.
The remap experiments only accept the scalar form and reject the paired form
explicitly, so a pair-aware remap is needed before composing them.

## Tests

CTest `retained_occupied_staggered` (`ma_occupied_staggered_tests`, 17 groups,
default build) covers both initialization policies and conflicts, K/P/variance
identities, ledgers, vacuum, stationary liquid, translation at huge origins and
local scales `2^-300`/`2^300`, symmetric gas and liquid expansion, one-cell
adiabats, Fourier dt-to-zero, an alternating-pressure force, acoustics with grid
and timestep refinement, free fall, wall impulse and reflection, hydrostatics,
denormal traces, typed rollback, EOS replacement after initialization, and
aggregate-mass overflow. The acoustic standing wave
(`u=a*sin(pi*x)*cos(c*pi*t)`, `a/c=1e-7`, Courant 0.1) converges at second
order:

| Measure | N16 | N32 | N64 |
| --- | ---: | ---: | ---: |
| Velocity L1 | 2.8202e-4 | 7.0788e-5 | 1.7720e-5 |
| Pressure L1 | 4.7962e-4 | 1.1986e-4 | 2.9959e-5 |
| Sound-speed relative error | 1.6359e-3 | 4.0939e-4 | 1.0240e-4 |

All 17 pass on MSVC, strict GNU and ASan/UBSan. The paired-coordinate controls
(`ma_retained_geometry_tests`) and the gas/vacuum driver are described in
[GAS_VACUUM.md](GAS_VACUUM.md#retained-nodal-replacement-path). Paired liquid
mechanics and remap composition are not validated.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_TESTS=ON
cmake --build build --target ma_occupied_staggered_tests
ctest --test-dir build -R retained_occupied_staggered --output-on-failure
```
