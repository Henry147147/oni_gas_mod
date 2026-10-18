# Liquid thermodynamic reference

`liquid_reference.hpp`/`.cpp` define `ma::experimental::LiquidThermodynamicReference`,
an equation of state for ideal gases and a zero-expansion elastic liquid, with
equilibrium closure and isentropic work queries. Despite the directory name it
is in production use: root CMake builds it as `ma_fluid_reference` inside
`ma_core`, and `FluidThermodynamics` (`native/src/fluid_thermodynamics.cpp`)
wraps it as the EOS of the production `MacClosedWorld`, the `FluidWorld`
occupied 1-D mode and the 1-D retained-node and canonical experiments (see
[SOLVER.md](../SOLVER.md)). `GasWorld`, its C ABI and the closed `FluidWorld`
mode do not use it. It has no timestep or game code. This directory also holds
its independent CMake test project.

## Model

The liquid has zero thermal expansion, constant positive Cv, zero surface
tension and constant sound speed. With `v0=1/rho0` and bulk modulus K0:

```
psi(v) = K0 [v0 log(v0/v) + v - v0]
p      = -d psi/dv = K0 (rho/rho0 - 1)
e      = energy_offset + Cv T + psi(v)
s      = Cv log(T/Tref)
```

`A=U-TS` is the Helmholtz energy; compression stores mechanical energy without
heating. Every profile gives provenance, finite temperature bounds and a maximum
compression ratio; metadata records the EOS version
(`ma-zero-expansion-elastic-liquid-v1`), profile version and all parameters.
The constructor serializes that metadata once. `metadata()` borrows it from an
lvalue model (keep the model alive) and returns a copy from a temporary.

This is not a calibrated water EOS. The in-game owner uses water with
rho0 = 1000 kg/m3, K0 = 2.2 GPa and density ratio at most 2 (profile
`oni-sealed-domain-v2`); at rho/rho0 = 1.407 that gives 895.4 MPa and
114.822 kJ/kg of elastic energy, which is not a stable liquid-water state.
Validated properties would come from
[IAPWS-95](https://iapws.org/technical-guidance/release/IAPWS-95); a Tait curve
([LAMMPS](https://doc.lammps.org/stable/pair_sph_taitwater.html)) would need its
own caloric/free-energy closure.

## Equilibrium closure

Gas and liquid share pressure and temperature. At pressure p a liquid occupies
`(m/rho0)/(1+p/K0)`; the caloric temperature is
`(U-offset-elastic(p))/sum(m Cv)`; gas fills `Vg=nRT/p`. The volume residual is
monotone on the positive-temperature branch; the solver contracts from nominal
volume and uses a series for tiny elastic energy. Gas partial volumes are
thermodynamic, not spatial. Entropy includes ideal-gas mixing (1 m3/mol
reference, Tref 298.15 K), evaluated with `log1p` near one so it cannot
overflow through a quotient. Pure compressed liquid needs no seed gas;
underfilled liquid is uncompressed liquid plus zero-pressure vacuum (a state
closure only: no free-surface motion, cavitation, vapour, tension, expansion,
phase change or shocks).

Contract:

- Failures are typed `ReferenceError`s. Bounds use a 32-epsilon relative
  tolerance with no unit floor. Results are never clamped; an unrepresentable
  positive result fails.
- A returned material energy must keep its thermal/elastic part relative to the
  formation reference; offsets that erase it fail. Energies are checked against
  conserved U.
- `initialize_at_temperature` adds elastic energy and reports it in a separate
  ledger. `evaluate` never adds it to an existing conserved state, so old
  caloric-only energy yields a lower temperature or an unsupported state. Do not
  apply these semantics silently to existing saves or ABI inventories.
- `evaluate_into` keeps its previous output on failure. A trace too small to
  share in a remap fails; it is never deleted.

## Isentropic queries

`make_isentrope(cell)` captures an immutable entropy reference bound to the
model metadata; a model with different profiles fails with
`incompatible_entropy_reference`. With `C=sum(m Cv)`, `G=n R`, `a=G/(C+G)`,
`b=C/(C+G)`:

```
T(p)  = T0 (p/p0)^a
Vg(p) = Vg0 (p/p0)^(-b)
V(p)  = sum_i [(m_i/rho0_i)/(1+p/K0_i)] + Vg(p)
U(p)  = offset + C T(p) + elastic(p)
-dV/dp = sum_i [V_i/(K0_i+p)] + b Vg/p > 0
dT/dV = (a T/p) dp/dV,   dU/dV = -p
```

Gasless liquid is isothermal; beyond the nominal fill it is liquid plus vacuum
at zero pressure, so release returns all elastic energy as work. At the fill
kink `isentropic_derivatives` reports `unsupported_derivative`; state and work
queries still work there.

`isentropic_at_volume(reference,V)` and `isentropic_response(reference,deltaV)`
return the endpoint pressure and, separately, `work_pressure_pa=-deltaU/deltaV`
(zero-length limit p0). `isentropic_response` keeps increments below the ULP of
the reference volume. Energy changes use relative exponential/logarithmic
differences, so formation energy never cancels them. `volume_change_m3` and
`internal_energy_change_j` are the retained work ledger; the `represented_*`
fields describe the rounded endpoint, and an `endpoint_*_resolves_change` flag
is false when the endpoint does not represent the increment within 32 epsilon.
Callers must check those flags before using an endpoint as a conservative
update. The reference never commits, floors, seeds or repairs caller state.

## Gas-only small-work precision

For gas-only isentropes the inverse is analytic,
`log(p/p0) = -log(V/V0)/b`, evaluated with `log1p(deltaV/V0)` so a small change
survives when the endpoint volume rounds to V0. Gas/liquid and liquid roots are
unchanged. The earlier bisection tolerance caused work-pressure errors up to
`5.29809e-9 Pa` in uniform motion, found by the
[canonical owner's](../CANONICAL_MATERIAL.md#current-source-validation)
free-fall control. Its test checks pressure, tangent and balanced energy at
signed volume changes `2^-20` to `2^-120 m3` against Simpson quadrature within
32 epsilon.

## Tests and build

`liquid_reference_tests` runs 27 groups: independent pressure-work quadrature,
finite-difference Helmholtz/entropy/Cv derivatives, mixture roots, reversible
cycles, heat capacity, vacuum and fill states, traces, domain bounds,
cancelling energy references, metadata, rollback, the isentropic cases, sub-ULP
increments and entropy extremes. All 27 pass on Windows/MSVC and WSL
Linux/GNU. They are thermodynamic checks only.

```
cmake -S native/experimental -B build-liquid-reference -DCMAKE_BUILD_TYPE=Release
cmake --build build-liquid-reference --parallel 2
ctest --test-dir build-liquid-reference --output-on-failure -V
cmake -S native/experimental -B build-liquid-reference-sanitized -DCMAKE_BUILD_TYPE=Debug -DMA_REFERENCE_SANITIZERS=ON
cmake --build build-liquid-reference-sanitized --parallel 2
ctest --test-dir build-liquid-reference-sanitized --output-on-failure -V
```

`MA_REFERENCE_SANITIZERS` needs GCC or Clang with an installed sanitizer runtime;
findings are fatal and leak detection is on. Manual MSVC build from an x64
developer environment (create the output directory first):

```
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /Fe:build-solver/liquid-reference/liquid_reference_tests.exe /Fo:build-solver/liquid-reference/ native/experimental/liquid_reference.cpp native/experimental/liquid_reference_tests.cpp
build-solver/liquid-reference/liquid_reference_tests.exe
```

## Notes for vacuum and free-surface work

Empty cells need more than volume fraction: interface geometry decides which
faces are wet ([Basilisk VOF](https://basilisk.fr/src/vof.h),
[momentum](https://basilisk.fr/src/momentum.h)). A valid extension separates
geometric, occupied and void volume, uses zero-pressure faces with one-sided
inertia, transports geometry, constituents, momentum and energy through
consistent face records, and omits empty unknowns without fictitious heat
capacity. For gasless liquid, `p>=0`, `Vvoid>=0`, `p Vvoid=0` need a coupled
active-set solve; clamping pressure afterwards loses displacement and elastic
work. Gas into vacuum is a rarefaction ([GAS_VACUUM.md](../GAS_VACUUM.md)).
