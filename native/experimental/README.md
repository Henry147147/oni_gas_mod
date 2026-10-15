# Compressed-liquid thermodynamic reference

This directory is a standalone prototype. Production `GasWorld`, `FluidWorld`,
`SpeciesRegistry`, managed catalogs/importers, the C ABI, and root CMake targets
do not use it. It has no timestep, transport, interface, or game ownership code.

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
