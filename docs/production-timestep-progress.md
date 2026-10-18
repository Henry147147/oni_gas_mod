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
