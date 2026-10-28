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

## Requested 3× release goal, 2026-10-28

The user wants sustained actual **3×** for release, also to accelerate testing,
in representative base-game and Spaced Out! colonies on their Ryzen 9 9950X
Windows PC, with smooth input, camera and menus. Linux engineering and
verification proceed while that playtest is pending. The fixed targets above
retain their inputs and bounds.

A 0.2 simulated-second owner frame has **66.7 ms wall time at 3×**. The
current pacing controller reserves 5%, requiring its measured owner work to
average below about **63.3 ms**, before omitted work is considered. Roughly
50 ms is a useful engineering headroom target, not a new acceptance bound.
Final acceptance measures full-game throughput, frame-time tails, input,
memory and publication backlog together.

| Current profile8 case on the 5900X | Mean / p95 | Timing scope |
| --- | --- | --- |
| Fresh Linux Terra, 180 frames at normal speed | 66.2 / 85.6 ms | Status timer **before** projection/publication; the run achieved about 1× |
| Fresh Spaced Out! replay, frames4–43 | 583 / 707 ms | `advance()` plus `wait()` through owner completion |
| Uploaded colony's twelve-dig replay, frames505–584 | 860 / 932 ms | Same complete owner observer |

The replays were contended. On those measured conditions, 66.7 ms would
require about 8.7×/12.9× lower mean cost; this is not a hardware conversion or
prediction for the Windows PC. Terra does not certify Spaced Out! or a
developed base. Profile8's earlier 23.7% improvement is already in this baseline.

### Linux ultra probe after the first implementation

A frozen private profile8 package with the transfer optimization and new
diagnostics completes the fresh base-game Terra validation at frame450 without
an owner fault or mod-log error. It runs on the 5900X/NVIDIA Linux host with
12 workers, ultra selected and the whole-machine CPU lock. Startup frames1–150
and the two cleanup frames are excluded from the warmed windows below.

| Owner frames | Owner / game seconds per real second | Partial owner mean / p95 | Submit → managed publication mean / p95 | Average FPS |
| --- | --- | --- | --- | --- |
| 151–300 | 1.921× / 1.928× | 65.5 / 78.0 ms | 114.6 / 182.5 ms | 59 |
| 301–450 | 1.956× / 1.943× | 64.8 / 75.5 ms | 109.0 / 171.0 ms | 63 |

Both windows are unpaused. The first has 143 latest-work samples for 150
completed frames; absolute clocks still retain all 30 physical seconds.
There are no unmatched publications or dropped timing samples. The partial
timer's smoothed capacity estimates are 3.16×/3.23×, demonstrating why that
estimate and the selected speed cannot certify actual throughput. Flow alone
averages 51.4/50.9 ms, before full adapter/native handoff and UI work.
Publication latency includes queueing and observation, so subtracting the
partial owner timer does not isolate adapter CPU cost.

The bounded report (frames0–450) retains 90.000001 owner seconds and
89.901379 game seconds over 49.101678 monotonic wall seconds: 1.833×/1.831×,
including startup, with no observed pause. The warmed windows remain below
actual 3×. This fresh Terra run does not certify developed colonies, Spaced
Out!, Windows performance or smooth input. Two clean 15 s frame-log windows
each have five GCs, frame p99 123/120 ms and GC-containing frame means
140/131 ms. The one-GC/15 s target remains open. Native 20-call phase summaries
show next-frame waits averaging 11.75–22.61 ms (maximum 120.8 ms) and UpdateData
13.43–14.15 ms; those groups can straddle warmup boundaries. Unity's main-thread
allocation API is unavailable, so approximate heap growth cannot certify
allocation-free sampling. Compact private evidence is
`artifacts/region-tiers-2026-10-28/linux-3x-run/measurement-summary.json`.

### Measurement and test harness first

- Measure both `GameClock` and owner-clock deltas against monotonic wall time
  over warmed-up, unpaused windows. `--speed 3` selects ONI's ultra preset;
  inspect its multiplier and report achieved speed separately.
- Time the full owner cycle through projection/publication and native
  handoff. `last_advance_ms` stops before `project()`/`publish()` and is now
  labelled as a partial work/capacity estimate. The performance log measures
  owner and game clock deltas against monotonic wall time, including skipped
  completions and pauses. It reports matched submit-through-managed-publication
  latency separately, including queueing and observation delay. The bounded
  game report saves these independent clocks in `colony_owner.Performance`;
  short runs also flush a final partial log window. Focused controls cover
  skipped/duplicate polls, saved origins, clock/session resets, pauses,
  startup and publication matching.
- Establish whole-CPU-lock profile8 baselines without competing builds for
  fresh Terra, fresh Spaced Out!, the digging colony and a developed connected
  base. Include other owned asteroids/rocket interiors and regional critical
  paths. Re-profile after activity tiers before projecting combined gains.
- Extend long-run orchestration with bounded telemetry, physical-progress
  watchdogs, memory/backlog and conservation checks. Its 100,000-frame and
  48-hour ceilings cannot cover the required 100/1,000-cycle runs, which need
  about 300,000/3,000,000 native frames. At perfect 3×, 1,000 cycles still
  require 55.6 hours of advancement, excluding loads/pauses. Preserve cycle
  counts; resumable segments need verified next-frame save continuation.

Standalone physics/unit programs already run without the game speed limiter.
Selecting 3× accelerates in-game progression and cycle tests; those standalone
programs benefit from solver improvements and efficient test scheduling.

### Optimization order

1. **Adopted region activity tiers.** Pause untouched regions without
   catch-up, step remote actor-free regions every 1 s and active regions at
   0.2 s. Persist clocks and activity across saves/splits/merges; use complete
   actor/input provenance, excluding passive solid conduction as a wake
   source. Coordinate flow, heat, germs, conversions and sinks. Stagger remote
   due times deterministically to avoid synchronized bursts. Older probes
   showed substantial CPU savings, but 1 s stepping did not shorten the
   largest region's amortized critical path. Fully active bases and the fixed
   active-cell benchmarks cannot benefit from sleeping their active cells.
   Profile9/core28 supplies an experimental, opt-in solver core; the game
   continues to use profile8. Before enabling tiers, complete the main-thread
   actor census and native input/thermal-contact provenance, pending-input save
   state and ordered germs/conversions/space-sink scheduling. An aggregate due
   duration alone does not establish correct ordering for those owner stages.
   Paused core regions still incur extraction, EOS and whole-state audit work;
   measure that cost before claiming a whole-game gain.
2. **Largest-region pressure and parallelism.** With multiple regions,
   `RegionalTick` gives each regional `Tick` one worker; the slowest region
   dominates after the others finish. Develop execution that can use idle
   capacity within large regions without nested oversubscription or changing
   reduction order. The prior shared-executor prototype was slower, so keep
   a measured cost gate. Test CPU aggregation/multigrid preconditioning with
   unchanged equations/tolerance: captured-system prototypes took 26–33
   iterations versus IC(0)'s 272–441; full-frame gain and setup cost remain
   unproved. This is a standard CPU method ([hypre guide](https://hypre.readthedocs.io/en/stable/solvers-boomeramg.html)),
   not a dependency selection. The game kernel forces IC(0), so the existing
   production multigrid is not a ready switch. Different floating-point
   iterates require profile and continuation controls.
3. **Transport, state movement and audits.** Reuse interval workspaces,
   sparse storage and addressing; reduce repeated region extraction,
   bounding-box copies, global packing and unchanged EOS/audit work. Preserve
   accounts and ordering. Re-profile phase and sharpening after tiers;
   critical-temperature skipping already exists.
4. **Owner and space sink.** Batch actual sink work and reuse derived data
   while preserving exact parcel/boundary ledgers. The older Spaced Out!
   sink cost about 65 ms; fresh Terra measured 0.5 ms. They are different
   states and need current workload-specific measurements.
   Exact transfer-reading reuse now cuts isolated batch work by 25–36% on
   two private 78-material states, with all 192 batch checkpoints and receipts
   matching. This does not measure a whole-owner gain; see
   [the workload and controls](../native/MAC_GAME_KERNEL.md#exact-transfer-reading-reuse).
5. **Main-thread work and allocation.** The current clean Linux window has
   two GCs/15 s, about 109 ms GC-containing frames and 51 ms p99 native
   blocking. Pool snapshot/event storage, avoid duplicate per-cell render
   construction and cache the fluid part of repeated publications. The 520
   publication attempts in one window include only about 75 physical ticks;
   unchanged owner frames can still carry new native body work and events.
   A submit/consume split must preserve shadow pairing, heat/receipt delivery,
   one-shot events and save/lifecycle drain fences. Re-test camera/menu/pause
   response at achieved 3×.

Fewer pressure iterations do not equal whole-game speedup: removing an
illustrative 45% pressure share entirely caps total CPU gain at about 1.8×.
Measure critical paths as well as summed CPU work. Keep adopted sharpening,
stiffness, trace settings and fixed physics bounds. Any further gameplay
tradeoff needs measurements and the user's choice.

Each implementation gets a focused control plus the required native suite
and MAC comparisons, including old-profile bytes where relevant. Then test
one immutable Linux package at actual 3× with disturbances, save/load,
memory/backlog and representative gameplay. Windows OS/hardware acceptance
remains separate. See [Linux feature work](feature-completion-status.md#linux-work-while-windows-playtesting-is-pending).

## Colony owner benchmark

`ma_colony_benchmark` (`native/apps/colony_benchmark.cpp`) drives the actual
colony owner through its native API, exactly as the game's frames do: each
0.2 s frame queues the duplicants' and machines' transfers and heat, advances
the owner (inputs, flow, sinks, ledgers, projection and publication), copies
the complete published views as the adapter does and acknowledges the
receipts. New colonies use the default game profile 8 with the physical-core
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

