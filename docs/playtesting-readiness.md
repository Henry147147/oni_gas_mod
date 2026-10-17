# Playtesting readiness and simulation cost

The full overhaul is **not ready for an ordinary colony playtest**. The default
installation remains a development probe. A visible six-cell chamber does not
establish whole-colony mixture ownership.

The next playable gate needs:

1. Colony fluid ownership across walls, vacuum, liquids and every supported
   constituent. The current sealed native owner accepts only 2–16 cells per
   dimension and a small material catalogue.
2. A reliable production timestep. The original strong-flow separation test
   still fails velocity and central density/pressure bounds. Its reproducer
   and limits remain intact; see [the diagnostic](../native/MAC_CURVED_NODAL.md).
3. Complete transactions for breathing, machines, pipes, storage, construction,
   heat and callbacks, using actual inventory and energy reservations.
4. Complete saving of these owners and live objects: manual saves, autosaves,
   reload and asteroid switching on both OSes. Current small-domain tests cover
   only implemented paths.
5. Ten actual colony cycles with life support, coherent rendering, every
   non-trace gas on hover, harmful-trace warnings and bounded simulation and
   publication latency. See [milestone D](completion-plan.md#d-first-playable-development-colony).

## Accuracy choices

The 2026-10-17 request authorizes investigating fidelity tradeoffs now.
Conservation, inventories, breathing thresholds and truthful presentation remain
requirements. Original reference and release tests stay available.

| Change | Benefit | Accuracy cost and admission |
| --- | --- | --- |
| Certified exact equilibrium | Avoid repeated pressure, reconstruction and heat solves | No physical approximation. Implemented for identical occupied inventories, exactly zero canonical momentum and zero gravity. |
| Lower-order conservative transport | Less reconstruction | More numerical mixing and smeared fronts. Benchmark-only `balanced` uses donor mass/momentum transport and a 0.1 s maximum step; it is not enabled in the game. |
| Reused topology, EOS identity and workspaces | Less allocation and assembly | Exact invalidation for construction, phase/material changes and load. Further work. |
| Better timestep prediction and growth | Fewer rejected attempts | Persist or deterministically derive history affecting continuation. Keep admission and conservation guards. Further work. |
| Low-Mach pressure treatment for calm gas | Avoid sound-wave timescales | Lose acoustic/shock detail only in admitted slow-flow regions. Retain EOS bulk pressure, source/heating expansion, buoyancy, pressure work and a compressible route for fast flow/vacuum. Further work. |
| Coarser pressure or slower slow-process updates | Less large-domain work | Keep species at tile resolution, conservative interface fluxes, bounded thermal/diffusive error and immediate wake-up for devices or hazardous fronts. Further work. |

Low-Mach projection filters acoustics and can make advective velocity determine
the timestep; see [AMReX-Hydro documentation](https://amrex-fluids.github.io/amrex-hydro/docs_html/Projections.html).
This is a candidate design, not an implemented property of this mod. Clamping
pressure or dropping traces would instead change gameplay physics.

## Implemented exact equilibrium

`MacClosedWorld::advanced` accepts an execution policy. Reference execution
retains the original path. Native worker, checkpoint advance and input-journal
replay consistently select `exact_equilibrium`. It compares every constituent
and internal-energy value and the entire authoritative momentum fields,
including values below binary64 range. Comparisons have no tolerance.
Identical inventories at equal volume imply identical EOS pressure/temperature;
zero momentum and gravity make the current mechanical/thermal fluxes zero.
Different passive amounts remain unchanged: this owner has no passive diffusion.

Only time and revision advance. The report records one certified interval,
`exact_equilibrium_intervals=1`, and zero pressure/thermal stages; no work counts
are invented. Physical settings, checkpoint schema and all state values remain
unchanged. Adding diffusion, phase kinetics or another evolving process requires
extending this certificate first.

`ma_mac_equilibrium_tests` compares complete checkpoint bytes with full solves
for gas/water, walls, conduction, viscosity and reload. It checks wake-up for
heat edits, one-ULP energy/trace changes, normal and below-range momentum, and
gravity; invalid arguments reject before shortcuts. MSVC Windows and GNU Linux
pass. The retained first water-test failure incorrectly expected pressure
iteration after heating a zero-expansion liquid. The corrected test checks
conduction and complete full-solver agreement.

## Reproduce measurements

```sh
cmake -S . -B build-playtest -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_ONI_NATIVE_BACKEND=ON
cmake --build build-playtest --config Release --target ma_mac_playtest_benchmark ma_mac_equilibrium_tests
python tools/run_mac_playtest_benchmark.py --executable build-playtest/ma_mac_playtest_benchmark --output local/wave-reference --profile reference --scene wave
python tools/run_mac_playtest_benchmark.py --executable build-playtest/ma_mac_playtest_benchmark --output local/wave-balanced --profile balanced --scene wave
python tools/run_mac_playtest_benchmark.py --executable build-playtest/ma_mac_playtest_benchmark --output local/still-100k --profile equilibrium --scene uniform --width 400 --height 250 --samples 7 --no-reference
```

For Visual Studio, use the executable under `Release/`. Each output directory
must be new. The runner preserves commands, binary/native-source hashes, raw
stdout/stderr, exit/timeout outcomes and metrics. World construction and
reference review are outside advance timing. Samples use identical initial
states, not a sustained colony. The declared test EOS has constant heat capacity
and elastic water. `--no-reference` explicitly omits the expensive full solve;
comparison fields are null. Conservation still includes positive helium trace.
A passing capture does not mean its performance target passed.

On the Windows Ryzen 9950X, seven 100,000-cell **uniform** samples take
16.52–17.49 ms per 0.1 s, p50 16.79 ms, with zero mass/energy drift and no solves.
That run omits full-reference comparison; small exact-byte controls supply that
comparison. This is sleeping-region evidence, not active cells, publication,
sustained 1x or 5900X certification.

GNU Linux on the same host also passes all 21 registered native test groups.
Its seven 100,000-cell sleeping samples have p50 2.46 ms and p95 8.47 ms, again
with zero mass/energy drift and no full-reference solve at that size. MSVC
Windows passes the same 21 groups, the separate eight-group closed-owner
control, the 177-scenario managed declaration/FTZ-DAZ suite and the adapter
build (with its two existing assembly-version warnings). These scoped checks
do not establish current Unity rendering or a playable colony.

The managed suite additionally reproduces a Windows hover-review race: a
renamed file can be visible while the native rename DELETE handle is still
open. The reader now shares deletion, but never writing. The old reader rejects
the controlled DELETE handle; the repaired reader reads complete bytes, while
an unfinished exclusive writer still rejects. All four eager-publication tests
pass. This repairs the private review protocol, not the full colony gate.

For a 16×16 periodic 20 Pa pressure wave, five reference samples have p50/p95
1796.81/1840.48 ms. Five benchmark-only balanced samples have p50/p95
1043.78/1199.78 ms, constituent drift below 1.3e-15 and energy drift below 6e-16.
Endpoint differences from the reference are at most 1.17e-5/1.63e-5 relative
density/pressure and 0.00140 K temperature. Pressure differs by about 1.63 Pa,
roughly 8% of the 20 Pa perturbation; normalization only by the 100 kPa background
hides this flow error. Both modes reject 95 attempts over five samples. Larger
steps and lower order alone do not meet the target. Raw captures are in
`local/playtest-performance-r1`.

The active-cell target remains **25 ms per 0.1 simulated seconds for 100,000
cells on a Ryzen 5900X**, plus actual Windows/Linux game frame times, bounded
memory and no growing publication backlog. It remains open.
