# Unfinished Cartesian/hybrid remap experiments

The committed retained material-state library and its 17-group
`retained_occupied_staggered` test remain in the default seven-test native
build. `MA_BUILD_STAGGERED_EXPERIMENTS` defaults to **OFF** and controls only
three additional standalone libraries and six additional tests. The normal
`tools/build.py` explicitly sets OFF, including when reusing an opted-in
build cache. None of these libraries is linked into ma_core or the game DLL.
There is no C ABI, FluidWorld mode, save format or gameplay activation for
this state.

The opt-in dependencies are `ma_occupied_staggered_remap` (Cartesian transport,
linked to the retained library), `ma_occupied_staggered_hybrid_geometry` (A0,
linked to the Cartesian grid helpers), and
`ma_occupied_staggered_hybrid_remap` (A1, linked to A0). Their explicit build
aggregate `ma_staggered_experiment_tests` builds all six additional test
executables. It does not assert that their numerical gates pass.

## Scoped evidence and preserved failure

The original Cartesian test `positive_staggered_rezone` still fails its
strict nonuniform free-boundary jump/dt assertion: six of seven groups pass.
Its executable returns 1; its real CTest run returns 8. The test algorithm,
criterion and failure are unchanged. An unfiltered CTest run with the option
ON has thirteen registered tests and is expected to return 8. There is no WILL_FAIL,
skip, result-regex override, threshold change or automatic retry.

The four previously validated additional gates have limited meanings:

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

A separate test-only nonlinear passive gate is now registered; its native
completion is insufficient without its fresh exact-rational/analytic Python
oracle. The predeclared288 nonlinear plus16 uniform-control histories, actual
variable-density half-mass rebalance, physical balances, four sample times,
packet oracles and strict refinement/settling criteria are specified in
[HYBRID_NONLINEAR_PASSIVE.md](HYBRID_NONLINEAR_PASSIVE.md). The first strict
MSVC native 304-case physical/export run passed, and the separately calibrated
oracle completed all 304 per-history state/packet/balance checks. The full
nonlinear refinement gate **FAILED** at the first N32-to-N64 velocity
supremum comparison: ratio 0.964869095141856 against the unchanged strict 0.8
criterion, C=1/4 and tau=1/32, phase 0/carrier -1/linear/unreflected family.
Both maxima are at the right inner cap node; physical outer packets stay
exact. That oracle exited1 and its procedure failed. GNU/sanitizer and any
numerical rerun of this new gate have not been executed after the failure.
The full states, first failure and original source/data archives remain
preserved. All-node temporal rate and whole A2 remain false.

These are supported experimental foundations, not proof of general remap
convergence. The new smooth nonlinear passive transport fails its predeclared
supremum-refinement criterion; pressure/remap composition, a gas-to-vacuum
fan and physical front convergence remain unvalidated. Reported actual mass,
momentum and U+K+PE errors remain separate
from signed arithmetic ledgers; no exact-conservation claim follows from
the finite-tolerance checks. The full-front gate in `GAS_VACUUM.md` remains
false. There are no performance or gameplay claims here.

Follow-on private exact-rational work tests a different consistent corner-mass
representation. Six constant/affine/hat and reflected cases retain four
mechanical corners and two homogeneous material-energy owners each. A
simultaneous three-face solve gives common traces, preserves momentum,
translated first momentum and total energy, and assigns each corner its own
nonnegative unresolved kinetic-energy increment. Constant and affine states
stay unchanged. Both hat controls reject a once-through scalar sweep as a
continuity solution. A separate fixed virtual midpoint displacement satisfies
the proposed `A=Z*D` potential's exact secant-work identities; omitting that
stress leaves a nonzero local defect in both hats.

The first finite run and an independent serialized-record review pass, covering
24 corners at each stage, 12 actual material-energy owners and 18 faces.
The independent checker completes 28,788 exact checks without
rerunning the original solve, EOS, entropy or density routines. The hats change
the fragment's outer traces, so full-cap and omitted-neighbor compatibility
remain unresolved. This is a representation/constitutive hypothesis and a
virtual-work control, not a time step or a selected production model. Moving
primal-volume work, actual entropy guards, nonlinear accuracy and performance
remain unproved; no native gate or earlier counterexample changes. Frozen
original sources/results and the independent review remain under
`local/occupied-staggered-validation/canonical-moment-face-exact-v1` and
`canonical-moment-face-exact-v1-managed-independent-review-r2`.

A subsequent full-domain diagnostic closes that cap question negatively for
the unchanged representation. It retains all fourteen mechanical corners,
seven actual material-energy owners and thirteen internal joins in each of
the six cases. Exact substitution verifies the supplied simultaneous solves,
all ninety assembled momentum equations, local signed work and nonnegative
mechanical-Z increments, global P/R/H, separate material U+H, species/traces
and reflection. Constant and affine states remain unchanged. Both localized
hat cases nevertheless change both complete required cap supports and all
four required cap traces. The recorded verdict is
`FINITE_IDENTITIES_WITH_INTERNAL_JOIN_CAP_INCOMPATIBILITY`; initializer and
mechanics acceptance remain false.

This obstruction cannot be repaired by choosing another internal impulse or
retained Z while keeping the same constraints. At fixed positive-definite
local mass matrices, full continuity and every assembled nodal momentum fix
one continuous velocity through the assembled positive-definite system. That
unique hat velocity changes the caps. A richer source-compatible velocity
field or a separately justified change to remap momentum constraints would
require a new representation and new work/thermodynamic obligations.

The one finite attempt uses 83,800 of 100,000 arithmetic work units and at most
1,714 of 12,288 Fraction bits, without a physical timestep, EOS call, native
execution or benchmark. Its report is
`866848d8237527aa59e7c15aa7d9294083a2b7984896375f5e030552d5d9902c`.
Independent serialized-arithmetic and closed-evidence review is
`2ca356141d8769f6c329e69244a0931bd301b1f8c6f2c099001059fe346045f1`
under `local/occupied-staggered-validation/` in
`canonical-moment-face-full-domain-v1-managed-independent-review-final-r2`.
Reviewer-only metadata failures remain preserved; their corrections did not
repeat the original diagnostic or its arithmetic. No earlier numerical,
entropy, performance or native gate is promoted by this result.

### Retaining source kinks: finite nesting and material work

A separate six-case exact-rational control retains the original piecewise
linear velocity field, adding only its missing source kinks to the target
partition. It passes with 88 base pieces and 176 harmlessly subdivided pieces,
at most 32 per state. Both versions recover all 84 coarse moment records,
90 assembled momenta, 42 actual material owners and both complete boundary
caps in every case. The hat fields retain four nonzero differences between
physical traces and the coarse projected endpoint values. Derived coarse Z
is a projection diagnostic; it is not added to the already resolved fine
kinetic energy.

The same control authenticates a seven-owner volume incidence against actual
material geometry. Symbolic pressure forces and material work cancel with
explicit exterior terms. Omitted and duplicate owner controls reject even
when their incorrect force/work formulas remain mutually adjoint. Canonical
roundtrips, input/parent retirement, positive trace retention, reflection and
atomic rejection of unsupported piece growth also pass.

The single attempt uses 98,911 of 100,000 arithmetic units and at most 268 of
12,288 Fraction bits. Its report is 1,236,710 bytes, within the unchanged
2 MB limit; SHA256 is
`e24b8d13c7af3d59dd2d017f6bb07338c153eb382986b864d33bda8b3100da93`.
Independent completed review is
`8c514c7f6e53ec54d7714bc5a150b5988a412998752417d2b5a4a1588df1b1b2`
under the private `canonical-source-kink-nesting-v1-independent-result-review`.
Its earlier static work estimate understated the actual count by 440; the
review corrects that estimate without changing a limit or repeating the run.
This is a finite nesting/work identity result. Representation selection,
thermodynamic closure, a physical update, moving geometry, native integration
and performance remain open. The prior cap-incompatibility result is unchanged.

## Reproducing standalone evidence

Use separate fresh build and output directories. A default build contains
seven tests, including the retained material-state17 gate:

```sh
cmake -S . -B build-default -G Ninja -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_TESTS=ON -DMA_BUILD_STAGGERED_EXPERIMENTS=OFF -DCMAKE_COMPILE_WARNING_AS_ERROR=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-default --parallel 4
ctest --test-dir build-default --output-on-failure
```

Explicitly opting in registers all thirteen tests. A plain `ctest` is expected
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

In experimental mode, the runner requires twelve applicable native passes plus the independent nonlinear oracle, direct
legacy exit1 with the exact six PASS/one preserved FAIL, single-test CTest
exit8, and unfiltered CTest exit8 with only that failure. It also executes
all 26 reference groups. The nonlinear oracle runs as an explicit fresh
subprocess after each applicable/unfiltered native run; a native exit0 alone
cannot pass it. Missing/modified registrations, result overrides,
unexpected success, different failures, timeouts, sanitizer diagnostics and
stale output are evidence failures. No old log is accepted as a fresh run.
Raw subprocess bytes, commands/statuses, source snapshots, binary hashes,
compile commands and passive profiles are frozen in the output directory.

`procedure_succeeded=true` means that evidence contract was satisfied.
The current frozen nonlinear run has `procedure_succeeded=false`; the
runner does not convert its numerical failure into an expected pass.
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
