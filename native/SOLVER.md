# Native solver code

This page is the entry point for the C++20 code under `native/`. It says which
solver is in production, what the other solver paths are for, where the code
lives, and how to build and test it.

## Production solver: `MacClosedWorld`

**Decision (2026-10-18):** `MacClosedWorld` is the project's production colony
fluid solver. Its owner is `native/src/mac_closed_world.cpp` and its components
are the other `native/src/mac_*.cpp` files (headers
`native/include/mixed_atmospheres/mac_*.hpp`). ONI's native SimDLL fluid
simulation is what the mod replaces. It must not be used as the solver. New
colony fluid physics belongs in `MacClosedWorld`; the other paths below are
references or experiments.

`MacClosedWorld` is a closed, occupied regular-grid owner. It keeps canonical
MAC face momentum, solves EOS pressure work, and uses compatible primal/dual
transport, compression, shock heating, conduction, viscosity, gravity and a
physical checkpoint. Its equation of state is `FluidThermodynamics`
(`native/src/fluid_thermodynamics.cpp`), which wraps the
[liquid thermodynamic reference](experimental/README.md): ideal gases and a
zero-expansion elastic liquid. Its design, component contracts and open gates
start at [MAC_SOLVER.md](MAC_SOLVER.md), which indexes the `native/MAC_*.md`
notes.

The in-game owner is `native/src/oni_domain.cpp`. It runs `MacClosedWorld`
behind the `ma_oni_domain_*` C ABI (`native/include/mixed_atmospheres/oni_domain.h`)
inside the SimDLL hook library `mixed_atmospheres_oni` (`oni_backend.cpp` plus
`oni_domain.cpp`), which is built only with `MA_BUILD_ONI_NATIVE_BACKEND=ON`.
Managed code reaches it through `managed/MixedAtmospheres.Oni/OniNativeDomain.cs`.
Today it owns bounded sealed and configured development domains, not a whole
colony. See [the sealed native domain](../docs/sealed-native-domain.md),
[the configured domain](../docs/native-configured-domain.md),
[native backend integration](../docs/native-backend-integration.md) and the
open gates in [playtesting readiness](../docs/playtesting-readiness.md).

## Other solver paths

| Path | Code and build target | Used by | Role |
| --- | --- | --- | --- |
| `GasWorld` gas reference | `grid.cpp`, `atmosphere.cpp` and shared physics in `ma_core`; C ABI `api.h`/`api.cpp` in the default shared library `mixed_atmospheres` | Managed `MixedAtmospheres.Core.FluidWorld` (`NativeMethods.cs`). `Mod.cs` loads the library when the mod loads; `IntegrationProbe` runs a private 2x1 world. Managed test hosts | Reference and integration probe. It never owns colony physics. Tested by `physics_and_abi`; see [architecture](../docs/architecture.md) |
| `ma::FluidWorld` closed mode | `fluid_world.cpp`, `pressure.cpp` in `ma_core`; C ABI `coupled_api.cpp` in `mixed_atmospheres_coupled` (built on request) | Managed `CoupledSimulationWorld` and `CoupledColonySimulation`; in game, only the `coupled-presentation` validation mode loads it | Closed 2-D gas/liquid validation reference. [FLUID_WORLD.md](FLUID_WORLD.md), [COUPLED_ABI.md](COUPLED_ABI.md) |
| `ma::FluidWorld` occupied 1-D mode | `FluidGeometry1D` overload in `fluid_world.cpp` | Tests only | Dormant experiment. Its gas/vacuum front gate is false. [FLUID_WORLD.md](FLUID_WORLD.md#occupied-1-d-mode), [GAS_VACUUM.md](GAS_VACUUM.md) |
| Retained material-node 1-D state | `occupied_staggered.cpp`; library `ma_occupied_staggered` | Tests only | Dormant experiment. [OCCUPIED_STAGGERED.md](OCCUPIED_STAGGERED.md) |
| Cartesian/hybrid remap | `occupied_staggered_remap.cpp`, `occupied_staggered_hybrid*.cpp` | Tests only, with `MA_BUILD_STAGGERED_EXPERIMENTS=ON` | Dormant experiment with two failing gates. [STAGGERED_EXPERIMENTS.md](STAGGERED_EXPERIMENTS.md), [HYBRID_NONLINEAR_PASSIVE.md](HYBRID_NONLINEAR_PASSIVE.md) |
| Canonical material owner | `canonical_material*.cpp`, `retained_scalar*.cpp`, headers in `include/mixed_atmospheres/experimental/`; libraries `ma_canonical_material`, `ma_retained_scalar`, `ma_canonical_diagnostics` | Nothing outside its own tests | Dormant 1-D experiment. [CANONICAL_MATERIAL.md](CANONICAL_MATERIAL.md) |
| Liquid thermodynamic reference | `native/experimental/liquid_reference.cpp`; library `ma_fluid_reference`, linked into `ma_core` | `FluidThermodynamics`, hence the production `MacClosedWorld` and the 1-D experiments | Production EOS, despite the directory name. [experimental/README.md](experimental/README.md) |

None of the experiments is linked into `mixed_atmospheres`, `mixed_atmospheres_oni`
or a managed assembly.

## Directory map

- `native/include/mixed_atmospheres/`: public headers. C ABIs are `api.h`
  (`GasWorld`), `coupled_api.h` (`FluidWorld`), `oni_backend.h` and `oni_domain.h`
  (in-game hook library). `experimental/` holds the canonical-material headers.
- `native/src/`: implementation. Shared mixture, pressure, transport and thermal
  building blocks form `ma_core`; `mac_*.cpp` form the MAC libraries;
  `oni_*.cpp`/`oni_*.hpp` form the in-game library.
- `native/experimental/`: the liquid thermodynamic reference and its standalone
  CMake project.
- `native/tests/`: C++ test drivers, fixtures and diagnostics, plus data-only
  Python reviewers (`review_*.py`). A reviewer checks saved stdout or snapshots
  with exact or high-precision arithmetic and never runs native code.
- `native/apps/`: `ma_demo` (`GasWorld` CSV demo), `ma_fluid_benchmark`
  (`FluidWorld`), and `ma_mac_playtest_benchmark` (`MacClosedWorld`; driven by
  `tools/run_mac_playtest_benchmark.py`).
- `cmake/OniNativeBackend.cmake`: the in-game library, pinned funchook 1.1.3 and
  diStorm sources with local patches, and its CTest tests.

## Build options

| Option | Default | Effect |
| --- | --- | --- |
| `MA_BUILD_TESTS` | ON | Registers the CTest tests below. |
| `MA_SANITIZERS` | OFF | GCC/Clang only: ASan and UBSan, with every finding fatal (`-fno-sanitize-recover=all`). Runtimes stay dynamic. |
| `MA_BUILD_ONI_NATIVE_BACKEND` | OFF | Builds `mixed_atmospheres_oni` and adds 15 tests. Windows/Linux x64 only; fetches pinned sources. |
| `MA_BUILD_STAGGERED_EXPERIMENTS` | OFF | Builds the Cartesian/hybrid remap experiments and adds 6 tests, one a known failure. |

## Tests

CTest groups:

- **Default (7):** `physics_and_abi` (`GasWorld` and its C ABI),
  `integrated_fluid_world`, `integrated_fluid_free_surfaces`,
  `gas_vacuum_consistency`, `pressure_preconditioning_and_rollback`,
  `mixture_property_compatibility`, `retained_occupied_staggered`.
- **`MA_BUILD_ONI_NATIVE_BACKEND=ON` (+15):** `native_oni_observer_abi`,
  `native_oni_detour_relocation`, `native_oni_sealed_domain`,
  `native_oni_arithmetic_environment`, `native_oni_mixed_domain`,
  `native_oni_radial_consumption`, `native_oni_frame_inputs`,
  `native_oni_configured_domain`, and the `MacClosedWorld` tests
  `mac_gas_boundary_transfers`, `mac_occupied_conduction`,
  `mac_mixture_conduction`, `mac_physical_viscosity`, `mac_native_body_heat`,
  `mac_physical_checkpoint`, `mac_exact_equilibrium`. The sealed and configured
  domain tests take about two minutes each in Release and about nine under ASan.
- **`MA_BUILD_STAGGERED_EXPERIMENTS=ON` (+6):** labelled `staggered-experimental`.
  `positive_staggered_rezone` is also labelled `staggered-known-unresolved` and
  fails by design, so an unfiltered `ctest` returns 8. See
  [the evidence contract](STAGGERED_EXPERIMENTS.md#reproduce).
- **Standalone reference project** (`native/experimental`):
  `liquid_reference_thermodynamics`.

Many targets are excluded from the default build and from CTest: most
`ma_mac_*` component tests and diagnostics, the coupled-ABI tests, the
canonical-material and retained-scalar tests, and the retained gas/vacuum
drivers. Build one with `cmake --build <dir> --target <name>`, run the
executable directly, and check its saved output with the matching
`native/tests/review_*.py`. The owning document gives the commands.

On 2026-10-18 all 22 tests of the default plus backend configuration passed on
Linux (GCC 15, Release and ASan/UBSan). The MinGW-w64 GCC 13 Windows build of
the same 22 test executables passed under Wine.

## Build commands

`tools/build.py` first runs `tools/coverage_inventory.py validate` and the
Python unit tests. It then configures with Ninja in Release, forces
`MA_BUILD_STAGGERED_EXPERIMENTS=OFF`, builds, and runs CTest. Without
`--native-only` it also runs the managed test hosts, which need `dotnet`.

```sh
python3 tools/build.py --native-only                          # build/, default tests
python3 tools/build.py --native-only --oni-native-observer    # adds the in-game owner
python3 tools/build.py --native-only --sanitizers --oni-native-observer --build-dir build-sanitized
```

The equivalent manual configuration, with a filter for only the
`MacClosedWorld` and in-game owner tests:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_ONI_NATIVE_BACKEND=ON
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -R '^(mac_|native_oni_)'
```

On Windows, run inside an x64 Visual Studio developer environment;
`tools/build.py` then defaults to `build-windows`. Linux release binaries for
packaging need the older-glibc build in `tools/build_linux_compat.py`; see
[the Linux native build](../docs/linux-native-build.md). The CI workflow
`.github/workflows/build.yml` is configured to run `tools/build.py
--oni-native-observer` on Windows and Linux, a Linux sanitizer build, and the
standalone reference project in both configurations.
