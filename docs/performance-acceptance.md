# Performance acceptance measurements

The release performance targets are in the
[completion plan](completion-plan.md#release-acceptance) (Performance) and its
2026-10-18 real-time amendment. This page says how each target is measured
and records the current numbers. No target passes yet.

| Target | Bound |
| --- | --- |
| 100,000 active cells, Ryzen 5900X | ≤ 25 ms per 0.1 simulated s (≤ 50 ms per 0.2 s frame) |
| 250,000 active cells across all asteroids, 32 duplicants | p95 solver plus adapter ≤ 62.5 ms per 0.1 s (≤ 125 ms per frame), sustained 1× and ≥ 30 FPS in normal play |
| Scene | Evolving gas, mixed phases, machines and entirely changed presentation frames; uniform equilibrium cannot certify |
| Memory | ≤ 2 GiB extra, bounded snapshot retention, no growing publication backlog |
| Presentation | Worker sampling and colour lookup allocate nothing after publication |
| Disturbances | Fault-free 10,000-cell decompression and 1,000-cell flooding/phase events recover to sustained 1× within 30 wall-clock seconds |

## Colony owner benchmark

`ma_colony_benchmark` (`native/apps/colony_benchmark.cpp`) drives the actual
colony owner through its native API, exactly as the game's frames do: each
0.2 s frame queues the duplicants' and machines' transfers and heat, advances
the owner (inputs, flow, sinks, ledgers, projection and publication), copies
the complete published views as the adapter does and acknowledges the
receipts. New colonies use the default game profile 5 with the physical-core
worker count, the playtest default.

The generated asteroid is a grid of 40 × 24 rooms with upper doors and floor
hatches, so the atmosphere is one connected region. Six room kinds cycle:

- breathing rooms (O2/N2 checkerboard over a CO2 layer),
- hot/cold convection cells,
- still pools under humid air,
- 371 K pools under steam (evaporation and condensation),
- crude oil under dirty water (the immiscible layers invert),
- hydrogen under chlorine, or with `--no-hydrogen-rooms` CO2 over O2.

Thirty-two duplicants walk the breathing and convection rooms, each taking
0.02 kg of oxygen and exhaling 0.4 g of CO2 per frame. Every third room holds a
machine: an electrolyser with a gas pump and scrubber, a liquid pump and outlet
with a heater, or an oil well. The materials are rounded public physical
properties, not game data. Every owned cell changes its published view every
frame (`changed_view_fraction`), so presentation frames are entirely changed.

`--events` replaces the lower-left corner with three disturbances, each
started once the previous one has recovered or timed out:

- a 100 × 100 air vault whose 100-cell wall opens into an equal vacuum chamber
  (10,000-cell decompression);
- the 50-cell floor of a 50 × 20 water tank opening over an air room
  (1,000-cell flood);
- 1.7e8 J into each cell of a 50 × 20 pool at 360 K, capped at 400 K
  (1,000-cell phase event).

Recovery is the wall time from the event to the first of `--sustain-frames`
(25) consecutive frames within `--budget-ms` (200 ms, one frame at 1×).

`--world-dump FILE` instead replays a private captured world
(`MA_COLONY_DUMP_FRAME=n MA_COLONY_FAULT_DUMP=path`) through the flow advance
alone and reports the game kernel's stages; such dumps stay private.

The report gives frame, owner and stage times (mean, p50, p95, maximum),
the active-cell count, ledger errors, faults, receipts, phase change and
resident memory. The per-frame owner timer stops before projection;
`after_owner_timer` holds projection, publication and the thread hand-off.

### Running the suite

```sh
cmake --build build-production --target ma_colony_benchmark
tools/run_locked.sh whole python3 tools/run_colony_benchmark.py --build build-production --suite release
```

Results go to `artifacts/performance/<revision>-<time>/` (ignored). The
`smoke` preset also runs as the `colony_benchmark_smoke` CTest.

### Not covered here

In-game frame rate, the managed adapter (C# publication, hover and colour
lookup), and the 250,000-cell, 32-duplicant Spaced Out! colony need the game
harness and a prepared private save. The managed presentation benchmark is in
[mixture presentation](mixture-presentation.md#performance).

## Current measurements

Ryzen 9 5900X, `tools/run_locked.sh high` (12 logical CPUs, cores 12–23,
twelve workers), 2026-10-27. Other agents' unlocked builds and an in-game run
on the sibling cores kept the load average between 25 and 115, so these are
contended timings: they establish order of magnitude and the relative change,
not acceptance. Frame times are per 0.2 s frame (solver plus native adapter
and the view copy); halve them for the per-0.1 s targets.

| Case | Active cells | `828fd3a` mean / p95 ms | Parallel pressure mean / p95 ms | Target per frame |
| --- | ---: | ---: | ---: | ---: |
| 100k, default settings | 100,209 | owner fault: pressure solve did not converge (cap 400) | same | ≤ 50 |
| 100k, pressure cap 4000 | 100,209 | 1,955 / 2,754 | 1,598 / 2,361 | ≤ 50 |
| 100k, no hydrogen rooms | 100,209 | 1,468 / 2,064 | 723 / 974 | ≤ 50 |
| 250k, no hydrogen rooms | 250,566 | 13,955 / 15,963 | 3,626 / 3,956 | p95 ≤ 125 |
| Terra frame-50 replay (flow only) | 19,656 | 64.5–71.2 | 65.0–67.4; identical image | — |

The Terra row is three interleaved pairs of 8-frame replays. Every
synthetic case changes at least 99.999% of the owned views each frame and closes
its ledgers (relative mass/energy errors ≤ 6.2e-16/1.7e-15), without faults
apart from the default-settings 100k case. Extra resident memory peaks at
326–377 MiB at 100k and 808–882 MiB at 250k, within the 2 GiB bound, but
resident memory still rose by 42–133 MiB over the measured frames; whether it
levels off is not yet shown. The adapter's complete view copy takes 0.4 ms
(100k) to 1.5 ms (250k).

The disturbance case (100k layout, no hydrogen rooms) runs all three events
fault-free with closed ledgers, but no frame reaches the 200 ms budget, so
none recovers to 1×: frames average 2.8–4.1 s at `828fd3a` and 0.7–1.2 s
with the parallel pressure solve.

### Where the time goes

In the connected 100k atmosphere the pressure solve was about 80% of the
flow: one serial IC(0)-PCG over the 100,000-cell component, about 260
iterations per half-tick without hydrogen and about 440 with it. The iteration
count follows the acoustic CFL number (sound crosses about 33 cells of oxygen
or 130 of hydrogen in a 0.1 s half-tick), so the default cap of 400 is
exceeded by large hydrogen-bearing regions. Terra's frame-50 atmosphere is
fragmented into pockets under 1,000 cells, which already solve side by side.
[Large components](../native/MAC_GAME_KERNEL.md#large-pressure-and-heat-components)
now solve across the pool. The remaining time at 100k is about two-thirds
pressure and one-third transport, interface sharpening, heat, phase change,
layering and audits, each of which also exceeds the whole budget on its own.

Offline prototypes on the captured 100k systems (scratch, not adopted):
smoothed-aggregation multigrid PCG with Jacobi smoothing converges in 26–33
iterations instead of IC(0)'s 272–441, and a two-level IC(0) with 4 × 4
aggregates in 61–107. Both keep the equation and its tolerance.

### Fresh-colony storms, 2026-10-28

Bounded pressure intervals (profile6) made the fresh Spaced Out! colony
spend most of each advance in a few regions with fast flows; see
[retry storms and speed profile 7](../native/MAC_GAME_KERNEL.md#retry-storms-and-speed-profile-7).
On the same saved states, physical-only advances (twelve workers, high CPU
lock) went from 1.905/2.978 s mean/p95 (frame 32) and 0.591/1.498 s
(frame 60) at the parent to 1.171/1.724 and 0.337/0.750 s with profile7
and the bit-identical reading and pressure-structure changes. This is
still several times slower than one 0.2 s frame per 0.2 s of game time.

