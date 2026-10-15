# Compressed-liquid thermodynamic reference

This directory supplies a standalone thermodynamic prototype and its independent
tests. Root CMake also links it through the explicit experimental
`FluidGeometry1D`/`FluidThermodynamics` path in `FluidWorld`; see
[the bounded interface integration](../FREE_SURFACES.md). Default closed
`FluidWorld`, `GasWorld`, managed catalogs/importers and the C ABI do not select
that model. The reference itself has no timestep or game ownership code.

The explicit surrogate has zero thermal expansion, positive constant intrinsic
Cv, zero tension, and a constant liquid sound speed. Its zero-pressure reference
density is a model parameter, not a per-cell fitted density. For a liquid with
`v0=1/rho0` and reference bulk modulus K0:

```
psi(v) = K0 [v0 log(v0/v) + v - v0]
p      = -d psi/dv = K0 (rho/rho0 - 1)
e      = energy_offset + Cv T + psi(v)
s      = Cv log(T/Tref)
```

`A=U-TS` supplies the compatible Helmholtz energy. Liquid compression stores
mechanical energy without adiabatic heating in this zero-expansion model.
The heat capacity and `dp/drho=K0/rho0` are positive. This is not a calibrated
wide-range EOS for real water, and there is no universal material called magma.
Every profile must provide provenance, finite temperature bounds and a maximum
compression ratio. Metadata contains the EOS/profile versions and all parameters.

Gas and liquid share pressure and temperature. At a trial pressure each liquid
occupies `(m/rho0)/(1+p/K0)`. For conserved U the remaining caloric temperature is
`(U-offset-elastic(p))/sum(m Cv)`. Actual ideal-gas moles supply `Vg=nRT/p`.
The scalar volume residual is monotone on the positive-temperature branch.
Implementation uses contraction from nominal volume to avoid subtracting two
nearly equal liquid volumes, and a series retains tiny positive elastic energy.
Gas partial volumes are thermodynamic partial volumes, not separate spatial
regions. Entropy includes ideal-gas mixing; its arbitrary molar-volume reference
is 1 m3/mol and Tref is 298.15 K.

Pure compressed liquid needs no seed gas. Underfilled liquid is represented by
uncompressed liquid and zero-pressure vacuum volume. That is an equilibrium
state closure only: moving free surfaces, cavitation kinetics, real vapor,
surface tension, thermal expansion, phase change, shocks, and immiscible spatial
separation are absent. Pressure iteration and valid-domain failures are explicit.
Thermal/compression boundaries have a 32-double-epsilon acceptance tolerance
relative to each value and its own bound. There is no 1 K or unit-compression
floor in that comparison. Returned temperature, mass, energy and density are
never clamped to fit. A positive result that is outside the representable
numerical range fails explicitly. Material-specific returned energies must
retain their thermal/elastic contribution relative to the formation reference;
finite but extreme offsets that erase that contribution are unsupported and
fail rather than returning a nonconservative state. Aggregate thermodynamic
and returned material energies are checked against conserved U as well.

`initialize_at_temperature` deliberately includes elastic energy and reports it
in a separate initialization ledger. A native-game import preserving mass,
temperature and volume would need this model-conversion accounting. `evaluate`
never adds that energy to an existing conserved state: retaining old caloric-only
energy instead yields a lower temperature or an explicit unsupported state.
These energy semantics must not be silently substituted for existing saves or ABI
inventories. `evaluate_into` retains its previous output on any failed solve.

## Isentropic volume and pressure-work reference

`make_isentrope(cell)` validates a conserved cell and captures an immutable
entropy reference bound to the complete model metadata. Another model instance
with identical metadata can use that reference; changed profile/material data
fail with `incompatible_entropy_reference`. No inventory or saved energy is
changed by making or querying a reference, and vacuum remains exactly empty.

For fixed masses, let `C=sum(m Cv)`, `G=n R`, `a=G/(C+G)` and `b=C/(C+G)`.
Common pressure and temperature on the gas-containing isentrope satisfy:

```
T(p)  = T0 (p/p0)^a
Vg(p) = Vg0 (p/p0)^(-b)
V(p)  = sum_i [(m_i/rho0_i)/(1+p/K0_i)] + Vg(p)
U(p)  = offset + C T(p) + elastic(p)
-dV/dp = sum_i [V_i/(K0_i+p)] + b Vg/p > 0
dT/dV = (a T/p) dp/dV
dU/dV = -p
```

Gasless liquid has constant temperature in this zero-expansion surrogate. Its
compressed branch ends at the nominal filled volume; larger volumes contain
liquid plus vacuum at zero pressure and constant U. A release crossing that
boundary retains the entire released elastic energy as pressure work. The
ordinary pressure derivative has different one-sided limits at the nominal
fill, so `isentropic_derivatives` reports `unsupported_derivative` there.
Vacuum also has no physical temperature derivative. State and work queries
remain available at both boundaries. The separate work-pressure derivative
exists for nonzero steps ending at the liquid-fill kink; its zero-length
two-sided limit at the kink is unsupported.

`isentropic_at_volume(reference,V)` returns an endpoint on this curve.
`isentropic_response(reference,deltaV)` also retains an explicitly supplied
increment smaller than the ULP of the reference volume. Both return endpoint
pressure separately from `work_pressure_pa=-deltaU/deltaV`. The zero-length
limit is p0. Energy changes use relative exponential/logarithmic differences
and the integrated liquid cold curve, excluding formation-energy cancellation.
The work-pressure derivative uses stable second divided differences, including
its ordinary zero-length limit `(dp/dV)/2`, rather than subtracting rounded
endpoint pressures. Invalid roots and output queries retain their prior state.

Finite double endpoints cannot represent every mathematical increment.
`volume_change_m3` and `internal_energy_change_j` are the separately retained
work ledger; `represented_volume_change_m3` and
`represented_internal_energy_change_j` describe the actual rounded endpoint.
The `endpoint_*_resolves_change` flags compare each pair using a 32-epsilon
relative bound. A false flag explicitly means the endpoint does not faithfully
represent that increment. Endpoint U is formed from its own thermal/elastic
terms, so expansion toward vacuum does not cancel away its remaining heat.
Callers must inspect these precision reports before treating a returned
endpoint as a conservative numerical update. This prototype does not commit,
project, floor, seed, or silently repair caller state.

Tests compare ideal-gas analytic isentropes, liquid pressure-work quadrature,
gas/two-liquid known roots, thermodynamic derivatives, independent work
quadrature, reversible cycles, both sides of the fill kink, empty references,
expansion toward vacuum, sub-ULP increments, genuine trace gas, model identity,
and failure rollback. These are thermodynamic reference checks only.

## Future vacuum/free-surface integration

The explicit `FluidWorld` geometry mode now supplies the first bounded 1-D
liquid/interface pressure-remap slice described in [FREE_SURFACES.md](../FREE_SURFACES.md).
The default closed mode retains its prior validation. The remaining discussion
describes the wider interface work and acceptance gates; it does not imply that
gas/vacuum rarefaction, cavitation or 2-D dynamics have passed.
Two-dimensional use needs interface geometry as well as volume fraction: the
same half-full inventory can wet different faces depending on its location.
Geometric VOF reconstruction and consistent momentum transport are established
examples in [Basilisk's VOF implementation](https://basilisk.fr/src/vof.h) and
[its momentum implementation](https://basilisk.fr/src/momentum.h).

The current closed integrator assumes every non-wall cell is occupied. It
divides velocity and conductivity by cell mass, splits impulses between both
neighbors, and uses whole moving-control-volume fractions for every remap.
Allowing empty cells alone would leave zero-mass divisions, momentum in vacuum,
and liquid flux across dry faces. A valid extension must distinguish geometric,
occupied and true void volume; use physical zero-pressure boundary faces with
one-sided material inertia; and transport occupied geometry, all constituent
masses, momentum and total energy using consistent conservative face records.
Empty pressure/heat unknowns are omitted without fictitious heat capacity or
density. Wet-face traction and thermal contact areas follow actual geometry.

For a gasless zero-tension surrogate the volume/pressure conditions are
`p>=0`, `Vvoid=Vgeom-sum(m/rho(p))>=0`, `p Vvoid=0`. They need a coupled
active-set/constraint treatment. Clamping a solved pressure afterward does not
supply the missing displacement, geometry or released elastic work. External
traction p=0 is also distinct from a compressed cell's time-discrete nonzero
work pressure during release.

Gas/vacuum adds an independent rarefaction acceptance gate: vacuum has zero
pressure/density and undefined velocity, and gas expands into it through a
rarefaction ([Clawpack's primary Euler chapter](https://www.clawpack.org/riemann_book/html/Euler.html#Riemann-problems-with-vacuum)).
A finite occupied gas inventory at positive temperature cannot have its EOS
pressure clamped to zero. Proposed first dynamical tests are a translating
liquid plug in vacuum, a stationary hydrostatic column with zero surface
traction, a released compressed liquid slab, a gas/vacuum rarefaction with
grid convergence, creation/removal of cut cells, constituent/total-energy
conservation and complete rollback of an excessive step. No such dynamical
or performance gate is satisfied by the thermodynamic queries alone.

The chosen water test (rho0=1000 kg/m3, K0=2.2 GPa) is illustrative, not material
calibration. At rho/rho0=1.407 it predicts 895.4 MPa and 114.822 kJ/kg of elastic
energy. Neither those parameters nor the density ratio certify a physically
stable liquid-water phase. IAPWS-95 is the appropriate primary reference for
validated water properties and phase/domain checks. The LAMMPS Tait reference
shows a different pressure-density curve; adopting it would likewise require
its integrated caloric/free-energy closure.

Primary references:

- [IAPWS-95 official release](https://iapws.org/technical-guidance/release/IAPWS-95)
- [LAMMPS official Tait EOS documentation](https://doc.lammps.org/stable/pair_sph_taitwater.html)

Tests include independent pressure-work quadrature, finite-difference Helmholtz/
entropy/Cv checks, common-pressure mixture roots, reversible entropy changes,
fixed-volume equilibrium heat capacity, trace gas, tiny compression, tiny
positive temperature domains, the minimum representable compression bound,
unrepresentable/cancelling energy references, explicit import-energy accounting,
metadata and failure rollback. These are reference
thermodynamic checks, not production performance or gameplay validation.

Manual MSVC build (after loading the VS x64 developer environment):

```
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /Fe:build-solver/liquid-reference/liquid_reference_tests.exe /Fo:build-solver/liquid-reference/ native/experimental/liquid_reference.cpp native/experimental/liquid_reference_tests.cpp
build-solver/liquid-reference/liquid_reference_tests.exe
```

Create the output directory first. Root CMake is intentionally unchanged.

Standalone GNU Linux CMake/CTest builds (GCC or Clang):

```
cmake -S native/experimental -B build-liquid-reference -DCMAKE_BUILD_TYPE=Release
cmake --build build-liquid-reference --parallel 2
ctest --test-dir build-liquid-reference --output-on-failure -V
cmake -S native/experimental -B build-liquid-reference-sanitized -DCMAKE_BUILD_TYPE=Debug -DMA_REFERENCE_SANITIZERS=ON
cmake --build build-liquid-reference-sanitized --parallel 2
ctest --test-dir build-liquid-reference-sanitized --output-on-failure -V
```

The opt-in sanitizer build instruments the separate library and test executable
with address/undefined sanitizers and frame pointers. CTest enables leak
detection and makes sanitizer findings fatal. It requires an installed sanitizer
runtime. Enabling this option with MSVC or another unsupported compiler fails
configuration explicitly; the ordinary MSVC reference build remains supported.
