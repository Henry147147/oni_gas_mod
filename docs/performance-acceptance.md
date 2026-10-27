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

See below; numbers are recorded with their revision and lock.
