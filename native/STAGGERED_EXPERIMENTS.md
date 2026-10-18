# Cartesian/hybrid remap experiments (opt-in)

These dormant 1-D experiments add a conservative remap to the
[retained material-node state](OCCUPIED_STAGGERED.md), so that a material mesh
can be rezoned onto a fixed grid. They are built only with
`MA_BUILD_STAGGERED_EXPERIMENTS=ON` (default OFF; `tools/build.py` always passes
OFF, even into an opted-in cache). None of them is linked into `ma_core` or a
game library, and there is no C ABI, `FluidWorld` mode, save format or game use.
There is no composed pressure-plus-remap timestep, so the
[gas/vacuum front gate](GAS_VACUUM.md) is not addressed by them.

| Library | Source | Builds on | Content |
| --- | --- | --- | --- |
| `ma_occupied_staggered_remap` | `occupied_staggered_remap.cpp` | `ma_occupied_staggered` | Cartesian rezone |
| `ma_occupied_staggered_hybrid_geometry` (A0) | `occupied_staggered_hybrid.cpp` | the Cartesian grid helpers | Hybrid geometry |
| `ma_occupied_staggered_hybrid_remap` (A1) | `occupied_staggered_hybrid_remap.cpp` | A0 | Instantaneous hybrid transport |

## Remap method

The remap is instantaneous and first order. It is applied to a retained state
whose cells keep constituents and U and whose nodes keep J and inertia:

1. Split each moving cell at its midpoint into two corners of bulk mass M/2.
   A corner moves with its attached node: momentum m*u, kinetic energy m*u*u/2.
   Constituents and U use conservative primal overlaps; no constituent is
   dropped or given a floor.
2. Intersect old corners with the target half-intervals, using the same
   piecewise-constant density as the primal remap, and carry mass, momentum and
   kinetic energy by positive intersections. Corner masses must match primal
   target masses to a derived local arithmetic bound. A zero-length move
   reproduces the old corners exactly.
3. Rebalance each cell's corners back to M/2 by moving the excess mass, with the
   same positive fraction of its momentum and kinetic energy, from the larger
   corner to the smaller. Constituents and U are unchanged. This local
   relocation is this project's choice, not a published method; it is zero on an
   unchanged mesh and preserves uniform velocity.
4. Gather corners at nodes and set u = J/I. Transported minus nodal kinetic
   energy is nonnegative by convexity in exact arithmetic; it goes to adjacent
   cell U by corner mass. A negative material discrepancy is reported, never
   clipped.
5. Preserve gravitational first moments in the primal remap and add the
   (either-sign) difference between transported and occupied-centroid potential
   energy.

A remap that pushes momentum into a rigid wall node records the opposing wall
impulse and moves the removed kinetic energy to adjacent U; the wall does no
work. External mass injection or extraction is not supported. The approach is
motivated by compatible staggered work
([Caramana et al.](https://cnls.lanl.gov/~shashkov/papers/comp.pdf)), subcell
remapping ([Loubere and Shashkov](https://www.math.ntnu.no/conservation/2004/058.pdf))
and consistent corner masses in rezoning
([Owen and Shashkov 2014](https://laro.lanl.gov/esploro/outputs/journalArticle/Arbitrary-Lagrangian-Eulerian-remap-treatments-consistent/9916370004303761),
DOI 10.1016/j.jcp.2014.05.023). Those sources do not verify this algorithm.

The **hybrid** variant keeps protected finite material caps at both ends of the
material interval, a fixed Cartesian grid in the interior, and transition cells
between them. A0 authenticates that immutable geometry and its coverage. A1
transports primal species/U and virtual-corner J/K through it, with exact
physical outer packets and compatible heat. A preferred-grid planner inserts and
retires cells as the interval moves. "A2" denotes the repeated-remap stage:
bounded jump and rate at every node, including inner caps, and convergence over
many calls. Whole A2 is not established.

## Tests and current results

All six tests carry the CTest label `staggered-experimental`; the custom target
`ma_staggered_experiment_tests` builds their executables.

| CTest | Executable | Result |
| --- | --- | --- |
| `positive_staggered_rezone` | `ma_occupied_staggered_remap_tests` | **Fails** 6 of 7 groups (also labelled `staggered-known-unresolved`) |
| `retained_hybrid_geometry` | `ma_occupied_staggered_hybrid_tests` | 9/9 A0 groups pass |
| `protected_hybrid_material_rezone` | `ma_occupied_staggered_hybrid_remap_tests` | 12/12 A1 groups pass |
| `protected_hybrid_first_boundary_event` | `ma_occupied_staggered_hybrid_event_tests` | 4/4 pass |
| `protected_hybrid_passive_diagnostic` | `ma_occupied_staggered_hybrid_passive_tests` | 128 cases pass their bounded trends |
| `protected_hybrid_nonlinear_passive` | `ma_occupied_staggered_hybrid_nonlinear_tests` | Native 304 cases pass; the independent oracle **fails** |

- **Strict Cartesian gate (open).** The seventh group of
  `positive_staggered_rezone`, manufactured carrier and free-boundary
  refinement, fails with "Nonuniform free boundary has an unbounded per-call
  smoothing defect as dt tends to zero". The executable returns 1 and CTest
  returns 8. The six passing groups cover an independent dyadic primal/corner/node
  oracle, identity/vacuum/uniform translation, the planner and positive free
  cuts, fixed-wall impulse and K-to-U, gravitational first moment and entropy,
  and denormal shares with atomic failures. There is no `WILL_FAIL`, skip,
  threshold change or retry; the gate closes when that group passes unchanged.
- **First boundary event.** The real planner history 7 to 7 to 8 to 7 cells
  matches an independent packet/heat oracle and capacity rollback. Inner
  shared-cap nodes still project.
- **Passive diagnostic.** 128 uniform-density, affine-velocity, sinusoidal-tag
  histories over carrier time 1/8 (both directions, four phases, reversed
  cycles, N16/32/64 and fixed-N timestep comparisons) keep physical outer
  packets exact and pass the predeclared `<0.8` grid-ratio and decreasing
  timestep-difference checks. Inner-cap jump/dt is not bounded at fixed grid
  spacing; its largest measured rate is about 10.8265.
- **Nonlinear passive gate (open).** Fails its first N32-to-N64 velocity
  supremum comparison with ratio 0.964869 against the strict 0.8 bound, at the
  right inner cap node. Criteria and the measured cause are in
  [HYBRID_NONLINEAR_PASSIVE.md](HYBRID_NONLINEAR_PASSIVE.md).

Reported mass, momentum and U+K+PE errors are kept separate from the signed
arithmetic ledgers; finite-tolerance checks are not exact-conservation claims.
Pressure/remap composition, the gas-to-vacuum fan and front convergence remain
unvalidated.

## Reproduce

Use fresh build and output directories. The default scope has seven tests:

```sh
cmake -S . -B build-default -G Ninja -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_TESTS=ON -DMA_BUILD_STAGGERED_EXPERIMENTS=OFF -DCMAKE_COMPILE_WARNING_AS_ERROR=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-default --parallel 4
ctest --test-dir build-default --output-on-failure
```

Opting in registers thirteen. A plain `ctest` is expected to return 8 because of
the strict Cartesian gate; `ctest -LE staggered-known-unresolved` skips it:

```sh
cmake -S . -B build-experiments -G Ninja -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_TESTS=ON -DMA_BUILD_STAGGERED_EXPERIMENTS=ON -DCMAKE_COMPILE_WARNING_AS_ERROR=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-experiments --parallel 4
cmake --build build-experiments --target ma_staggered_experiment_tests
ctest --test-dir build-experiments --output-on-failure
```

Build the standalone thermodynamic reference separately:

```sh
cmake -S native/experimental -B build-reference -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-reference --parallel 4
```

The evidence runner (Python 3.11+) checks the registered tests and compile
graph, then runs fresh subprocesses into an output directory that must not yet
exist:

```sh
python tools/validate_staggered_experiments.py --mode default --build-dir build-default --reference-build-dir build-reference --output-dir evidence-default
python tools/validate_staggered_experiments.py --mode experimental --build-dir build-experiments --reference-build-dir build-reference --output-dir evidence-experiments
```

In experimental mode it requires: twelve applicable native passes; the legacy
test's direct exit 1 with exactly six PASS and one FAIL; exit 8 from the
single-test and unfiltered CTest runs with only that failure; and the nonlinear
oracle run as a separate subprocess after each native run (a native exit 0 alone
cannot pass it). Both modes then run the reference project. Missing or changed
registrations, result overrides, unexpected success, different failures,
timeouts (300 s native, 1800 s oracle by default), sanitizer diagnostics and
stale output are evidence failures. Raw output, commands, source snapshots,
binary hashes, compile commands and profiles are kept in the output directory.

`procedure_succeeded=true` in its summary means the evidence contract held, not
that physics passed; `strict_cartesian_gate=FAILED` is reported alongside. With
the current nonlinear oracle failure, experimental mode reports
`procedure_succeeded=false`. The runner requires every one of the
`REFERENCE_GROUPS` (27) distinct liquid-reference groups to pass.

`.github/workflows/staggered-experiments.yml` is configured to run both modes
with Windows/MSVC, GNU and ASan/UBSan and to keep the evidence when the
procedure fails; the ordinary workflow uses the default scope. For GCC/Clang sanitizer builds use
Debug with `MA_SANITIZERS=ON` and
`-DCMAKE_CXX_FLAGS_DEBUG="-O1 -g -D_GLIBCXX_ASSERTIONS -fno-sanitize-recover=all"`,
and `MA_REFERENCE_SANITIZERS=ON` for the reference project. No game files are
needed.
