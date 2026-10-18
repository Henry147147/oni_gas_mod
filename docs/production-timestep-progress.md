# Production timestep implementation progress

2026-10-18. This implements part of the first milestone in the accepted
[real-time completion plan](completion-plan.md). The complete overhaul and
milestone A's physics/scaling exit remain unfinished. Normal colony activation
and release packaging have not been enabled.

## Implemented

- Compiler/linker-checked IPO defaults on for Release and RelWithDebInfo.
  `-DMA_ENABLE_IPO=OFF` retains a comparison build. Sanitizers disable IPO;
  sanitizer flags now precede every target, fixing missing runtime linkage in
  opt-in MAC tests. Strict floating-point options remain in force.
- Inline ordinary exponent operations retain the existing limb fallback.
  Normal binary64 normalization, projection and aligned addition use exact
  exponent-bit operations. Subnormal and extreme-range paths, positive traces,
  exact additive components and projection-rounding flags remain checked.
- A combined isentropic work/state/tangent query avoids solving the same
  thermodynamic root twice. Pressure line-search buffers are reused across
  iterations and rejected trials. No physical selectors or saved settings changed.
- Native frame publication retains two immutable physical owners and the
  corresponding durable actors and accepted inputs. Full serialization and
  hashing happen when a snapshot/journal reader requests a capture. Repeated
  reads reuse the serialized result; the next frame retires the previous pins.
  Save requests still serialize fully and retain pending-work guards.
- Exact equilibrium certification permits different disconnected sealed regions
  and exactly balanced discrete hydrostatic states. Any nonzero force residual
  wakes the full solver. Enabled conduction must have no temperature difference;
  gravity and wall impulse remain accounted. An evolving region still prevents
  whole-owner skipping; per-block sleeping inside an evolving world is open.
- Benchmark schema 2 admits 250,000 cells, supports consecutive intervals, counts
  cells that actually evolved, records selectors, exports the complete endpoint,
  and compares checkpoint bytes against an earlier build. Reference comparisons
  include temperature, pressure, O2 partial pressure and every positive
  constituent. Wave-pressure error uses the 20 Pa disturbance amplitude as well
  as reporting the background-relative difference. Uniform scenes cannot pass
  an active-cell timing claim.

## Measurements

Native GNU 15.2 Release on the Ryzen 9 5900X, Linux x86-64. The baseline is the
unmodified source at `28fb6414d4b25f0a0d3d69800892dee497cf1993`, without IPO;
the optimized runs use the unchanged physical defaults. Construction, reference
comparison and requested checkpoint serialization are outside solver timing.

| Case, 0.1 simulated seconds per sample | Samples | Median | p95 |
| --- | ---: | ---: | ---: |
| 256-cell wave, original source | 5 | 1,201.64 ms | 1,318.00 ms |
| 256-cell wave, optimized, repeated source | 5 | 692.78 ms | 726.30 ms |
| 256-cell wave, optimized, consecutive intervals | 5 | 705.90 ms | 776.27 ms |
| 4,096-cell wave, optimized, repeated source | 3 | 11,874.56 ms | 11,925.75 ms |

Every cell evolved in the completed wave captures; none used equilibrium
skipping or timestep retries. Constituent drift, including helium trace, is
below 5e-17 and energy drift below 2.1e-16. The repeated-source and consecutive
256-cell endpoints match the original build byte for byte. Their checkpoint
SHA256 values are respectively
`58ac2c5ba161a2b2a030dd718eee4dd17d3e8b2d334cb73b4b6b0b28c675b826` and
`4d250703f98fd1b00af3a7da505aaac1e7a7aa5405f24644e9b299893e8cf763`.

A 250,000-cell wave with consecutive sampling did **not** finish its first
0.1-second sample within a 45-second wall limit. It produced no timing,
accuracy or continuation result. GNU time recorded 2,339,280 KiB peak resident
memory for that standalone invocation, including benchmark state and candidate
buffers. This is not a measurement of additional memory inside ONI. No
colony-scale budget or credible real-time scaling path is established.

Captures, invocations, native-source/binary hashes, raw output and resource
records are under the ignored `local/production-timestep/` directory:
`baseline-wave/`, `final-wave/`, `final-continuation/`, `final-4096-wave/`,
`bounded-250k-wave/` and `250k-resources.txt`. Historical measurements in
[playtesting readiness](playtesting-readiness.md) use different builds and runs.

## Verification and remaining failures

- Linux Release CTest: 22/22 pass, including native ownership, configured rooms,
  transactions, thermal coupling, checkpoints and exact equilibrium. The updated
  frame-input control also passes after adding future-actor reconfiguration to
  the immutable-capture checks.
- Additional MAC executables: 35/36 pass. Saved-operand reviews pass for those
  with readers, with the appropriate declared heat/compression selectors.
  The original strong-flow test still fails the collision density refinement
  criterion: about 0.856 against 0.85. Its partial saved review validates the five
  completed profiles and reports the gate failed. No bound or input was changed.
- Linux ASan/UBSan: energy, large exponents, equilibrium, checkpoints and native
  frame-input/capture controls pass with fatal diagnostics and leak detection.
  The independent thermodynamic project passes in Release and ASan/UBSan,
  including exact fused/separate query comparisons, traces and typed failures.
- Python: 295 tests run with four existing skips and no failures. Benchmark controls reject
  equilibrium timing claims, incomplete active-cell counts and missing
  disturbance/constituent comparisons.
- MinGW Windows-target Release/IPO binaries: energy and exponent saved reviews,
  equilibrium and checkpoint controls pass under Wine. These are not Windows OS
  evidence or MSVC evidence. The configured Windows host refused SSH, so actual
  Windows validation remains open. No new Unity run is claimed.

The pressure/remap momentum-and-kinetic-energy conversion, persistent primary-field
rounding remainders, true vacuum, free surfaces, phase/solution transport,
the full material catalogue, a production colony ABI, whole-world ownership,
resource/power/heat transactions, the Unity solid-heat correction, gameplay
adapters, progression, complete persistence and cross-platform water
continuation remain open. All 3,072 coverage entries still lack acceptance
evidence. Existing conservation gates, breathing thresholds, new-colony-only
scope and verified ONI 744825 profiles remain the governing requirements.

## Reproduce

```sh
cmake -S . -B build-production -G Ninja -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_ONI_NATIVE_BACKEND=ON
cmake --build build-production -j 8
ctest --test-dir build-production --output-on-failure
cmake --build build-production --target ma_mac_playtest_benchmark ma_mac_energy_tests ma_mac_exponent_tests ma_mac_riemann_tests -j 8
python3 tools/run_mac_playtest_benchmark.py --executable build-production/ma_mac_playtest_benchmark --output local/new-wave-capture --profile reference --scene wave --continuation
```

Use a fresh output directory. For cross-build continuation checks, add
`--compare-checkpoint path/to/prior/endpoint.mac`; compare the same dimensions,
sample count, sampling mode and physical settings. A successful capture verifies
its declared scope and conservation, not release acceptance. Keep `balanced`
as a benchmark experiment; it has not been promoted.

## Follow-up: range-extended vertical gravity

The next implementation slice removes the explicit gravity rejections for full
vertical J, volume sweeps and mass fluxes. Body force/impulse, midpoint body work,
actual-mass potential flux and signed internal work use `MacScaled`/`MacEnergy`.
Gravity accounts and the composed energy budget use full operands; the existing
double reports are explicit projections, with a separate projection count. Adjacent
cell work shares accumulate in full range before one stored-U projection. Ordinary
represented pressure/body force cancellation still precedes the timestep product.

Equilibrium certification uses the same full forces. A new control finds a
subnormal force pair whose binary64 projections cancel while their full operands
do not, and requires the solver to wake. A rigid one-row equilibrium retains its
below-range body/wall impulse while matching reference checkpoint bytes.

`ma_mac_gravity_range_tests` adds five groups: incoming below-range vertical J with
9.81 m/s² gravity; very thin layered gas with below-range body impulse, work and
actual-mass potential flux; minimum-subnormal gravity with below-range velocity and
volume sweep; deterministic source/endpoint restores and late-budget rollback;
and 10,000 consecutive closed steps with a mid-run restore. The long control keeps
every primary U, constituent and passive amount unchanged and retains the full
accumulated face impulse (about 2^-1196 kg m/s). It is a range/accounting control,
not an active-cell performance or general endurance pass.

`review_mac_gravity_range.py` reconstructs the saved face work, potential flux,
cell shares, wall/body impulse and stage bounds using exact fractions. It rejects
six corruptions, including zeroing a tiny work/flux, hiding an unresolved-U count,
and altering the long-run kinetic residual. Full canonical momentum and numerical
settings use the existing checkpoint representation; no schema or default changes.

Primary U and material volumes remain binary64. Requested U changes that cannot
be stored are counted and visible in the full substep report, but their rounding
remainders are **not persisted**. This remains an explicit production blocker;
the range extension does not complete milestone A or its conservation contract.
The bounded native adapter still requires representable scalar projections for
cell mechanics and gravity diagnostics. Its existing ABI/import path has not been
widened by this solver slice; production integration must consume full report
operands and provide explicit projection metadata.
New captures and reviews are under `local/production-gravity/`. The existing
strong-flow gate still fails, and no new Unity or actual Windows OS evidence is
claimed.

Follow-up verification:

- Linux Release CTest: 23/23 pass, including the new 10,000-step control and both
  full-force equilibrium cases. Ordinary gas/liquid/layered hydrostatic speeds
  remain below 2.2e-11 m/s, and the original stratified pressure, velocity and
  entropy refinement bounds pass.
- ASan/UBSan with fatal diagnostics and leak detection: gravity range, ordinary
  gravity, canonical range, checkpoints and equilibrium pass. The final range
  and equilibrium controls also pass as static MinGW Windows-target binaries
  under Wine; this remains distinct from Windows OS/MSVC evidence.
- Fraction-based saved reviews pass for gravity range on Linux, sanitizers and
  Wine, and for the ordinary gravity, canonical range, momentum owner,
  compressive owner and swept reconstruction captures. The original strong-flow
  test still fails collision density refinement at about 0.856 against 0.85;
  its partial review validates five profiles and correctly reports failure.
- Five consecutive 256-cell zero-gravity wave intervals retain the original
  checkpoint SHA256 `4d250703f98fd1b00af3a7da505aaac1e7a7aa5405f24644e9b299893e8cf763`.
  These captures check continuation. The earlier run overlapped builds; their
  timings are not a new performance comparison or a real-time acceptance result.

Reproduce the new range control and its saved-operand review:

```sh
cmake --build build-production --target ma_mac_gravity_range_tests ma_mac_gravity_tests ma_mac_equilibrium_tests -j 8
build-production/ma_mac_gravity_range_tests > gravity-range.stdout
python3 native/tests/review_mac_gravity_range.py gravity-range.stdout
```

`local/production-gravity/manifest.json` records source/capture hashes and the
separate failed/unverified gates. `asan/` and `wine/` retain executable hashes,
exit codes, raw diagnostics and saved reviews. `final-wave-continuation/`
contains the final solver build's byte-identical baseline continuation capture.

## Follow-up: conservative 1-D nodal conversion

[Selector 5](../native/MAC_NODAL_CONVERSION.md) supplies a conservative point-to-dual
source conversion and its kinetic work. The original curved point oracle passes on
both axes/signs and at scale 2^-1200; a saved-operand reader verifies pair gathering,
material work transfer, U+K, constituent/passive conservation and EOS entropy, and
rejects six corruptions. Zero stretch matches selector 4 exactly. The selector and
complete canonical continuation survive save/reload.

Linux Release CTest: 24/24 pass. The new controls and saved reviews pass under
ASan/UBSan and as Windows-target MinGW binaries under Wine. Existing swept/bounded
energy controls also pass. No new actual Windows OS or Unity run is claimed.

The conversion is deliberately limited to 1-D single-component motion and leaves
production defaults unchanged. A bounded original N64 separation probe improves
velocity L1 from 0.18507 to 0.13251 but worsens density/pressure to 0.08597/0.07989.
The full sequence stops on the existing range restriction in coupled compression.
This repairs a component defect, not the strong-flow gate or the full overhaul.
Captures are under `local/nodal-conversion/`.

## Follow-up: sparse constituent accumulation

Transport now uses contiguous, ordered sparse rows for actual positive constituents
and their incoming donor union. It preserves contribution order, full-range amounts
and exact-zero semantics. The arbitrary 16-million cell/parcel-times-species limits
are removed; grid coverage and donor-outflow checks remain. Parcel validation also
avoids allocating and checking conservation accounts for absent species, while
rejecting any introduced positive trace. Primary cell vectors, reconstructed
prototypes and reconstruction fields remain dense; reusable sparse production
blocks and candidate reuse are still open.

The new control transports 4,097 catalogue entries through 4,096 cells and actual
EOS reconstructed parcels, exceeding the former product cap. With two positive
constituents per cell it allocates 8,192 full-range accumulators (1 MiB in the
128-byte accumulator build), rather
than 16,781,312 (about 2 GiB). This counts accumulator objects only. Separate controls
keep all 4,097 species positive, verify analytic two-axis trace transfers and reject
invented traces without changing the source. Existing primary-parcel controls retain
quarter-denormal recombination. There is no trace cutoff or species-count cap.

These are capacity, conservation and continuation controls. Uniform identity
transport does not establish active-cell performance. Five consecutive default
256-cell wave intervals still match the original checkpoint SHA256
`4d250703f98fd1b00af3a7da505aaac1e7a7aa5405f24644e9b299893e8cf763`.
A bounded 250,000-cell wave again completed no sample within 45 seconds. Its
999,780 KiB observed peak is from an incomplete invocation and cannot establish
the full memory budget or a peak-memory improvement at equivalent completion.
Captures are under `local/sparse-constituents/`.

Linux Release CTest: 25/25 pass. The new capacity/trace controls pass with fatal
ASan/UBSan diagnostics and leak detection, and as static Windows-target MinGW
binaries under Wine. Saved primary-parcel and nodal-conversion reviews pass
unchanged. The complete standalone capacity/EOS test uses 677,836 KiB peak resident
memory in its recorded Release invocation; this includes dense test inventories
and temporary parcel plans. No new Unity or actual Windows OS evidence is claimed.

```sh
cmake --build build-production --target ma_mac_sparse_constituent_tests -j 8
build-production/ma_mac_sparse_constituent_tests
ctest --test-dir build-production --output-on-failure
```

## Follow-up: compact exact accumulators

The [normal arithmetic path](../native/MAC_RANGE.md#additive-expansions-and-accounts)
stores four binary64 components inline and promotes an unchanged original expansion
for subnormals, overflow, additional components or independent exponents. It preserves
every additive component, contribution order and projection remainder. The object
shrinks from 128 to 72 bytes on Linux x86-64; the sparse capacity control's 8,192
accumulators now occupy 576 KiB. Full-range fallback uses a component vector, so this
does not imply an allocation-free extreme-range path.

The new control compares 1,490,300 intermediate expansions with the original
independent-exponent recurrence, including both public addition overloads, every
binary64 exponent bin, huge exponents, overflow/capacity promotion, full cancellation,
copy/move operations and atomic nonfinite rejection. Linux Release CTest: 26/26 pass.
All 40 MAC component executables were rebuilt: 39 pass, with the original strong-flow
collision refinement failure unchanged. All 38 available comparable saved captures
match their previous verified outputs byte for byte. Six controls pass ASan/UBSan
with fatal diagnostics and leak detection; the large sparse/EOS control also passes.
Four MinGW Windows-target controls pass under Wine. Sixteen saved-operand reviews
pass across Linux, sanitizers and Wine; the separate partial Riemann review correctly
reports the physical gate failed. No new Unity or Windows OS evidence is claimed.

Five consecutive default 256-cell wave intervals still match the original checkpoint
SHA256 `4d250703f98fd1b00af3a7da505aaac1e7a7aa5405f24644e9b299893e8cf763`.
The 250,000-cell wave again completes no sample within 45 seconds; its incomplete
invocation peaks at 1,695,296 KiB. Different progress at timeout prevents comparing
that peak directly with earlier incomplete runs. No active-cell real-time or full
memory budget is certified. Captures and source/executable hashes are under
`local/compact-accumulators/`.

## Follow-up: stage-local shock-heat EOS reuse

The heat stage now reuses validated pair-input and endpoint EOS readings. Each
accepted heat pair updates both inventories' cached readings; bit-identical stored
U retains its prior reading. The cache lasts only for that stage, so changes to
material, geometry, model or heat between calls always start fresh. Source/final
entropy snapshots retain only the temperature and gas volume they use. There is
no approximate matching, changed physical coefficient or relaxed acceptance check.

The new replay control passes 72 stages and compares all 2,064 receipts with fresh
EOS calls, including 1,440 changed-inventory reuse witnesses. It covers three laws,
walls, periodicity, wet mixtures, traces, below-range updates and edits between calls.
The control and the three existing heat suites pass Release, fatal ASan/UBSan with
leak detection, and MinGW Windows-target builds under Wine. Linux Release CTest:
27/27 pass. Eight targeted heat/compression components pass and their complete
saved outputs match the previous build; the original strong-flow failure also
matches byte for byte. Fourteen saved-operand reviews pass. No new Unity or actual
Windows OS evidence is claimed.

The separate baseline build is commit `9ceebad` with the same GNU Release/IPO and
physical settings. Sequential 256-cell wave continuation captures (five intervals)
measure median/p95 490.88/496.13 ms before and 436.43/446.34 ms after this change,
per 0.1 simulated seconds. Every cell evolves, with no retries or equilibrium skip,
and both endpoints retain the original checkpoint SHA256
`4d250703f98fd1b00af3a7da505aaac1e7a7aa5405f24644e9b299893e8cf763`.
At 4,096 cells (three consecutive intervals), median/p95 improve from
7,561.93/8,322.64 ms to 6,625.70/7,192.22 ms per 0.1 simulated seconds. All cells
evolve without retries, and both endpoint checkpoints have SHA256
`7bb7bf006aa435dfffd81df0cd89e38a32636369c9e73132f2e28b35550e30e4`.
These component timings do not pass a colony performance gate. Captures, paired
build settings and source/executable hashes are under `local/shock-heat-cache/`.

## Follow-up: coupled-pressure full motion and actual cell work

Coupled compression now retains full incoming canonical velocity corrections and
midpoint sweeps. Each cell checks its actual stored-U change against its full
midpoint pressure work at the existing operand bound. It reports full volume/work
and unresolved update counts separately from the constitutive root's binary64
volume parameter. No new heat, budget allowance, pressure selector or timestep
default is introduced. Primary-U/volume remainders remain unpersisted.

Eight saved gas, wet-mixture and compressed-liquid controls cover both axes/signs,
below-range and rounded-subnormal velocities, divergent microscopic motion,
positive traces and exact source/endpoint continuation. An additional regression
combines a projected incoming velocity with active compressive heating. A zero-U
case with unstorable actual work still rejects, and an exhausted interval budget
publishes no partial result. The saved reviewer rebuilds actual cell sweeps/work
with fractions and EOS readings at 100 digits, and rejects seven corruptions.
Its pure-liquid branch also checks analytic cold-curve pressure and elastic work;
the earlier gas/mixed reader paths retain their acceptance checks.

After incorporating `1bb1787` from main, Linux Release CTest passes 28/28. The range
and ordinary coupled-owner controls pass fatal ASan/UBSan with leak detection and
MinGW Windows-target runs under Wine. Eight saved reviews pass across those builds
and previous gas/mixed captures. The ordinary coupled-owner physical records remain
unchanged; only its formerly unsupported-range rejection is replaced by a stale
canonical-projection rejection. The final default 256-cell continuation still has
SHA256 `4d250703f98fd1b00af3a7da505aaac1e7a7aa5405f24644e9b299893e8cf763`.
No new actual Windows OS or Unity evidence is claimed.

The default strong-flow failure retains identical saved operands. An opt-in
2 / 4 / 3 / 2 probe completes Sod and collision and separation N64 without retries,
then reaches the 180-second process limit before completing the remaining profiles.
Selector 5's automatic pressure preconditioner reaches a separate transverse-motion
restriction at Sod N128. Jacobi preserves its planar motion through Sod N256, but
collision N128 pressure refinement is about 0.947 against 0.85. None of these
diagnostics passes the full flow gate or changes its inputs/bounds. Captures and
source/executable hashes are under `local/coupled-range-probe/`.

The next cost to remove is duplicate thermodynamic work in coupled compression:
the pressure root computes an isentropic response/tangent and then invokes a
constitutive callback that solves the same response again. Reusing that completed
response preserves the physical law and is needed before larger flow runs.
