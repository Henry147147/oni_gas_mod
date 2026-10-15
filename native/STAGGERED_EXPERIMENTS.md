# Unfinished Cartesian/hybrid remap experiments

The committed retained material-state library and its 17-group
`retained_occupied_staggered` test remain in the default seven-test native
build. `MA_BUILD_STAGGERED_EXPERIMENTS` defaults to **OFF** and controls only
three additional standalone libraries and five additional tests. The normal
`tools/build.py` explicitly sets OFF, including when reusing an opted-in
build cache. None of these libraries is linked into ma_core or the game DLL.
There is no C ABI, FluidWorld mode, save format or gameplay activation for
this state.

The opt-in dependencies are `ma_occupied_staggered_remap` (Cartesian transport,
linked to the retained library), `ma_occupied_staggered_hybrid_geometry` (A0,
linked to the Cartesian grid helpers), and
`ma_occupied_staggered_hybrid_remap` (A1, linked to A0). Their explicit build
aggregate `ma_staggered_experiment_tests` builds all five additional test
executables. It does not assert that their numerical gates pass.

## Scoped evidence and preserved failure

The original Cartesian test `positive_staggered_rezone` still fails its
strict nonuniform free-boundary jump/dt assertion: six of seven groups pass.
Its executable returns 1; its real CTest run returns 8. The test algorithm,
criterion and failure are unchanged. An unfiltered CTest run with the option
ON has twelve registered tests and still returns 8. There is no WILL_FAIL,
skip, result-regex override, threshold change or automatic retry.

The four additional passing gates have limited meanings:

- A0: authenticated immutable hybrid geometry, protected finite cap
  identities, capacity and positive fixed-grid coverage; no transport.
- A1: instantaneous positive primal/species/U and virtual-corner J/K
  transport, exact physical outer packets, compatible heat and atomic
  rejection; no composed physical timestep.
- First boundary event: actual preferred-planner 7-to-7-to-8-to-7 history,
  independent packet/heat oracle and capacity rollback. Inner shared-cap
  nodes still project.
- Bounded passive diagnostic: 128 uniform-density, affine-velocity,
  sinusoidal-tag histories over fixed carrier time 1/8. Both directions,
  four phases, reversed cycles, N16/32/64 and separate fixed-N timestep
  comparisons preserve physical outer packets. Predeclared <0.8 grid checks
  and decreasing timestep-difference checks pass in the frozen evidence.
  Point-nodal and geometric-dual errors are distinct. Inner-cap jump/dt is
  not bounded at fixed grid spacing; its maximum measured rate was about
  10.8265. Passing this bounded diagnostic does not establish whole A2.

These are supported experimental foundations, not proof of general remap
convergence. Smooth nonlinear density/velocity transport, pressure/remap
composition, a gas-to-vacuum fan and physical front convergence remain
unvalidated. Reported actual mass, momentum and U+K+PE errors remain separate
from signed arithmetic ledgers; no exact-conservation claim follows from
the finite-tolerance checks. The full-front gate in `GAS_VACUUM.md` remains
false. There are no performance or gameplay claims here.

## Reproducing standalone evidence

Use separate fresh build and output directories. A default build contains
seven tests, including the retained material-state17 gate:

```sh
cmake -S . -B build-default -G Ninja -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_TESTS=ON -DMA_BUILD_STAGGERED_EXPERIMENTS=OFF -DCMAKE_COMPILE_WARNING_AS_ERROR=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-default --parallel 4
ctest --test-dir build-default --output-on-failure
```

Explicitly opting in registers all twelve tests. A plain `ctest` is expected
to fail the unresolved strict Cartesian gate:

```sh
cmake -S . -B build-experiments -G Ninja -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_TESTS=ON -DMA_BUILD_STAGGERED_EXPERIMENTS=ON -DCMAKE_COMPILE_WARNING_AS_ERROR=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-experiments --parallel 4
cmake --build build-experiments --target ma_staggered_experiment_tests
ctest --test-dir build-experiments --output-on-failure
```

Build the independent 26-group thermodynamic reference separately:

```sh
cmake -S native/experimental -B build-reference -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-reference --parallel 4
```

The evidence runner requires Python 3.11 or newer and checks the requested registration/compile graph and
executes fresh subprocesses. Its output directory must not already exist:

```sh
python tools/validate_staggered_experiments.py --mode default --build-dir build-default --reference-build-dir build-reference --output-dir evidence-default
python tools/validate_staggered_experiments.py --mode experimental --build-dir build-experiments --reference-build-dir build-reference --output-dir evidence-experiments
```

In experimental mode, the runner requires eleven applicable passes, direct
legacy exit1 with the exact six PASS/one preserved FAIL, single-test CTest
exit8, and unfiltered CTest exit8 with only that failure. It also executes
all 26 reference groups. Missing/modified registrations, result overrides,
unexpected success, different failures, timeouts, sanitizer diagnostics and
stale output are evidence failures. No old log is accepted as a fresh run.
Raw subprocess bytes, commands/statuses, source snapshots, binary hashes,
compile commands and passive profiles are frozen in the output directory.

`procedure_succeeded=true` means that evidence contract was satisfied.
`strict_cartesian_gate=FAILED` remains a numerical failure in the same
summary. A successful dedicated workflow verifies that the declared failure
was reproduced; it does not report all native physics green. The separate
`.github/workflows/staggered-experiments.yml` runs Windows/MSVC, GNU and
ASan/UBSan configurations and preserves evidence even when the procedure
fails. The ordinary workflow uses the default seven-test scope.

For GNU/Clang sanitizer builds, use Debug with `MA_SANITIZERS=ON`,
`-O1 -g -D_GLIBCXX_ASSERTIONS -fno-sanitize-recover=all`, and separately
`MA_REFERENCE_SANITIZERS=ON` for the reference. The workflow records these
flags and uses halt-on-error sanitizer options. No installed game or game
assembly is needed for these checks.
