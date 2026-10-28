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

### Regional critical-path experiment during integration

A private execution-only prototype replays two original profile8 saved states
and their real input journals under the whole-machine CPU lock, with 12 total
workers. Two matched 12-frame repetitions reverse configuration order; their
first two frames are excluded from the table. All 192 owner frames retain both
physical checkpoint bytes and full-owner hashes across configurations/repeats.
The prototype assigns four inner workers to one measured expensive region and
eight workers to the other regions, retaining fixed reduction blocks. These
timings include the complete native owner through publication; they omit ONI,
the managed adapter and rendering. The prefix does not cover all twelve digs.

| Replay | Original owner mean | Four inner workers | Observation |
| --- | --- | --- | --- |
| Fresh Spaced Out! | 591.1 ms | 590.6 ms | No meaningful gain: another region remains critical |
| Digging colony | 842.9 ms | 605.3 ms | 28.2% lower owner time; kernel 770.2 →529.7 ms |

The fresh replay's selected region costs 416 ms while a competing region
costs 450 ms. Making only the selected region infinitely fast would improve
the complete owner by at most about 1.6% in this window. The digging replay's
selected region costs 714 ms versus 167 ms for the next region, so sharing idle
workers helps. Giving it 8 or 12 inner workers is worse than4 and raises summed
process CPU time sharply. Cell count alone selects the wrong region in both
scenes; a production scheduler needs observed cost, dominance, a bounded worker
budget and a serial fallback when activity/topology changes.

Aggregate stage durations identify pressure and transport as substantial work:
fresh pressure 729 ms / transport 661 ms, digging pressure 417 ms / transport 350 ms.
These are sums over concurrently advancing regions and are **not** exclusive
wall-time shares. The owner outside the kernel costs roughly 104/73 ms in these
two scenes. The fixed physics, tolerances, timestep and acceptance bounds are
unchanged. This experiment does not certify integrated tiers or actual 3×.
Private evidence: `artifacts/tier-integration-2026-10-28/region-experiment/whole/`.

### Integrated tiers: Linux Terra probe

The frozen private r6 package completes 450 frames on the same 5900X/NVIDIA host,
seed 147147, base-game Terra, 12 workers and ultra selection, under the whole CPU
lock. Expensive activity-mask checks and publication strings are off; frame and
native wait logging match the earlier probe. This is a fresh-colony performance
case, not the save-restoration acceptance case: later callback-readiness repairs
addressed the separate pending-save control.

| Owner frames | Owner / game seconds per real second | Partial owner mean / p95 | Submit → publication mean / p95 |
| --- | --- | --- | --- |
| 151–300 | 2.811× / 2.811× | 30.0 / 36.7 ms | 74.5 / 126.8 ms |
| 301–450 | 2.791× / 2.791× | Not isolated from the printed cleanup window | Not isolated from the printed cleanup window |

The second clock window is derived from the bounded frame 450 report minus the
first two 150-frame windows. The printed 301–451 window includes a cleanup/save
pause and must not be used as an unpaused throughput window. Combined warmed
owner progress is about 2.80× versus 1.938× in the earlier profile8 probe, about
44.5% faster. The requested actual 3× remains open. The complete 0–450 report
retains 90 owner seconds and 89.934719 game seconds over 33.368039 monotonic wall
seconds (2.697× / 2.695×), without an observed pause or dropped timing sample.

The first warmed window spends 16.1 ms in flow, 8.6 ms in heat inputs and 4.7 ms
in the stage labelled `transfers`. For profile9 that stage also includes activity
census, scheduling and germ evolution; it is not exclusive transfer work.
Native UpdateData remains about 13.5–14.2 ms, with roughly 1.1 ms
each for shadow writing/capture and 1.5 ms copying to the game. Mean native
next-frame waits are 0.46–1.51 ms in the later groups, with a 31.65 ms maximum.
The publication latency includes observation/queueing and is not an exclusive
CPU phase. One clean 15 s frame-log window has 3 GCs (previously 5), frame p99
95.09 ms and 121.1 ms mean GC-containing frames; exact allocation telemetry is
unavailable. The one-GC/15 s and input-response targets remain open.

The actual game caller discards time clipped/skipped by the pacing prefix;
it does not retain that argument. The original game also caps its admitted
argument at 0.2 s. The new opt-in `MA_COLONY_PACING_LOSS_LOG=1` observer separates
raw offered time, original cap loss, pacing loss and intentional guard loss in
bounded 50-owner-frame windows. It makes no timestep/debt/fence change and is
absent when disabled. Measure these losses before changing admission policy.
Private compact evidence: `artifacts/tier-integration-2026-10-28/linux-3x-run-r6/compact-summary.json`.

The r7 trace then measures warmed frames 151–450: raw offered 63.846806 simulated
seconds, original cap loss 2.558634, pacing loss 1.281474, admitted 60.006697;
other-guard loss and all accounting-error counters are zero. The wall span is
21.289978 s, so discarded time explains nearly all the gap to nominal 3×.
An ignored, opt-in bounded-retention prototype passes 17,138 pure accounting
checks and 81 controls using the real Mono/Harmony libraries. Its actual 450-frame
test reaches only 2.862× owner / 2.861× game, roughly 1.6% above the traced default,
while still dropping 2.513 simulated seconds in its warmed window. It is **not
adopted**. The raw harness flags its diagnostic word `errors` despite every such
counter being zero; the original result is retained, with the independent
clock/accounting analysis in
`artifacts/tier-integration-2026-10-28/linux-retention-3x-r7/compact-summary.json`.
The small gain does not justify declaring 3× or changing the default. More CPU
headroom and full handoff timing remain necessary.

### Further hot-path evaluation

Heat validation builds a full thermodynamic reading for every edit although
the caller uses only temperature. The exact batch experiment reuses the
transfer EOS helper, including its uncommon absent-liquid rejection fallback.
Two original 79-species saved worlds and their actual journals retain physical
and full-owner checkpoint hashes, complete receipt fields and job IDs across
96 owner-frame executions. Heat-stage means fall from 1.247 to 0.838 ms in the
fresh Spaced Out! replay and 3.773 to 2.633 ms in the digging replay. The fresh
complete owner is unchanged within noise; the digging mean improves about
1.9%, with enough variance that it cannot all be attributed to heat.
Isolated journal batches fall from 3.655 to 2.079 ms and 6.070 to 4.069 ms.
These are native replay measurements, not achieved game speed.

The setup cost matters: a single fresh-world edit regresses from about 108 to
114 µs, while 32 edits improve from 128 to 120 µs and 4096 from 2539 to
1406 µs. The implementation retains the original path below 32 edits and builds its
table lazily after the original edit and energy validation. This preserves
small retries, validation order, the 4096-edit cap and rollback. Private
differential controls cover 6057 comparisons across 2019 accepted/refused
inputs, plus both real journal cohorts. Evidence:
`artifacts/tier-integration-2026-10-28/heat-exact/`.
The complete 80-frame twelve-dig replay retains all physical and owner hashes.
Final verification passes 89 Linux and 89 Windows x64/Wine CTests, all managed
and game-contract checks, 439 Python tests and the 87 short MAC output/exit-code
comparison (one added heat-control PASS line).

The heat-only frozen package then passes an actual 450-frame Linux Terra ultra
run with the same seed/settings/logging and unchanged r7 managed libraries.
Warmed frames151–450 reach 2.7469× owner / 2.7470× game progress, below the
earlier traced 2.818× run. Heat costs 8.2/8.1 ms in its two warmed windows, but
transfers cost 10.3/10.5 ms rather than 4.8/6.8 ms. This is one fresh-game
comparison with differing runtime inputs, so it establishes neither a total
speedup nor the cause of the transfer increase. One clean 15 s window has two
GC-containing frames averaging 146.3 ms, frame p99 94.74 ms and 166,368 lazy
cell reads. Accounting errors, owner faults and dropped timing samples are 0.
Compact evidence: `artifacts/tier-integration-2026-10-28/linux-heat-only-3x-r7/compact-summary.json`.

The remaining investigation priorities are based on the integrated run:

| Path | Measured scope | Next exact experiment |
| --- | --- | --- |
| Presentation sampling | About 169,000 lazy cell reads and 3 GCs in a clean 15 s window | Sample texture scalars directly from the retained compact frame; prove all sampled bits, rejection messages, snapshot lifetimes and allocation behavior against the full reading path |
| Transfers/activity/biology | Reported stage roughly 4–7 ms in the earlier probe and 10 ms in the heat-only run | Separate reservoir, transfer, census, schedule and germ costs before optimizing |
| Native UpdateData | Roughly 14 ms per call | Attribute the remaining original work and hook overhead; preserve solids, boundary exchange and public observer counters |
| Flow | Roughly 16–17 ms per fresh Terra owner frame | Profile the active regions' pressure and transport after tiers; large connected colonies still need a faster pressure solve |
| Projection readings | Ledger/projection aggregate around 2.8 ms, partly overlapping the partial timer | Validate immutable registry context once per worker batch instead of rescanning it per cell |

Four atomic counter updates execute in each native hook. A short synthetic
per-hook sharding experiment, retaining the global active-call guard, reduces
mixed-hook cost from about 38–39 to 34–35 ns/call at twelve threads. It omits
game work and live status/lifecycle behavior, so it is not adopted. Naively
sharding the active-call guard can miss activity during a scan and permit an
unsafe detach. The counter result does not establish an UpdateData gain.
Full publication latency includes scheduling and observation; it must not be
added to the component timers as another CPU phase.

The private split logger retains physical/owner hashes over the measured legacy
replays and a controlled tiered repeat. Legacy fresh/dig biology spends about
7.42/7.44 ms in passive edits, chiefly cloning, while the controlled tiered
fixture spends 0.76 ms; these are different workloads. Fresh reservoir work
averages 5.16 ms and ordinary transfer application 3.38 ms; digging averages
0.87/7.29 ms. The costly first application moves between reservoir release and
ordinary transfers, suggesting a common opening-total/state cost. Nearby
searches themselves take about 0.06 ms per frame. All measured reservoir
anchors are open, so an empty-neighborhood cache would not help these cases;
saved precision inventories are empty too. Fixed-order parallel audit totals
remain a private proposal; the passive-edit result follows below. No tolerance, cache
invalidation rule or checkpoint is changed by this profiling.

The controlled tiered case repeats saved actors without new external heat or
transfers and borrows the earlier disease table. It is not actual game
continuation or a replacement for the next current-input attribution capture.
Scoped evidence is in `artifacts/tier-integration-2026-10-28/stage2-profile/`.

A first controlled replay from the current DLC frame 34 anchor reproduces the
frame 35/36 reference hashes. Its first transfer edits in place and spends
8.10 ms obtaining missing audit totals (8.09 ms in `dense_totals`), versus
0.019 ms in edits and 0.011 ms in their audit. The next frame reuses totals in
0.0005 ms. These are cold-frame observations, not steady-state means or proof
that every default-profile frame pays that cost. The parallel-total experiment
must retain fixed cell/face addition and block-merge order, cache invalidation,
exceptions and checkpoint/receipt bytes before any adoption.

### Expiring passive-edit comparison

The actual base-game diagnostic capture then identifies a steady cost over
frames 151–200: the reported transfer/activity/biology stage includes 7.410 ms
of biology, 0.978 ms of census and 0.927 ms of scheduling. Transfer application
is only 0.252 ms. Within passive edits, cloning costs 4.268 ms, edits 0.009 ms
and canonicalization/result construction 0.250 ms. That run includes a deliberate
save and input journaling, so its throughput is not accepted.

The implemented expiring passive path removes that redundant clone when the
state is unique, while retaining the copying path for shared states and other
kernels. The prior activity snapshot remains alive through its final history
read, then leaves scope before biology. All failable staging/result construction
precedes nonthrowing commit; empty/nonempty cache policies stay unchanged.

| Captured inputs, full native owner | Original mean / p95 | Expiring mean / p95 | Mean change |
| --- | --- | --- | --- |
| Base game, frames 102–117 | 31.836 / 36.178 ms | 27.057 / 28.558 ms | −15.0% |
| Spaced Out!, frames 35–50 | 68.518 / 80.429 ms | 54.736 / 59.325 ms | −20.1% |

Two reversed repetitions per scene use 12 workers and the whole-machine lock;
each excludes its first two frames, leaving 28 samples per mode. Timing includes
queue/wait and native publication, excluding comparison serialization/hashing.
Complete physical/owner bytes and canonical receipt/precise/job/moving records
match between modes and repeats; this does not imply nonempty coverage of every
record kind. The result is a native replay gain, not actual game throughput.
All 89 Linux and Windows-under-Wine native tests, 439 Python tests,
managed/game-contract checks, the full
87-program MAC output comparison and focused ASan/UBSan controls pass. The new
permanent passive PASS line is the only MAC output addition.

The validation harness itself also materializes and atomically writes a full
owner report on every Unity update. Ordinary play skips this configured harness
path. Two private 900-frame runs use identical binaries with report cadence
1 versus 30, retaining every clock observation, fault/scenario/save/target check
and forced final report. Reports fall from 5,106 to 196 and direct report cost
from 5.679 to 0.311 s (8.3% to 0.45% of total wall time). This is observer cost,
not a solver speedup. Both runs pass, but warmed owner/game speed is
2.641/2.638× versus 2.588/2.590×: no throughput gain is established. Both reach
about 2.827× early and slow later. Each has 12 GC-containing frames across three
clean 15 s windows; approximate heap growth falls, while collection counts
remain 3/4/5 per window. Actual Mono allocation totals are unsupported.

This is one ordered pair with differing gameplay inputs, so it cannot establish
a precise causal whole-game change. Frame p99s are 95/110/129 versus
90/104/146 ms per window. There are no dropped timing samples, owner faults or
critical game-log errors. The private observer and compact-rendering changes
remain unadopted; late main-thread work and GC still need attribution.
Private evidence under `artifacts/tier-integration-2026-10-28/`:
`passive-expiring/`, `linux-base-stage2-capture/` and `harness-overhead/`.

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
   Profile9/core28 is integrated through the owner and selected for new game
   colonies. Wrapper14 persists immutable actor/input cuts and restored frame
   claims; actor footprints include occupied cells and primary/secondary ports.
   Actual contact and external inputs wake regions; passive solid conduction
   alone does not. Germs, conversions, trace and space sinks share the selected
   due cells, with saved fractional biology time. Always simulate retains
   profile8 physical fields exactly after normalizing profile metadata.
   Held regions now skip local extraction and EOS, and their dense fields
   remain byte-identical; connectivity is cached by wall geometry. Global
   packing and conservation audits remain. See [controls](region-activity-controls.md).
   The profile8 numbers above remain the pre-integration baseline until a
   separately measured, diagnostic-free game run completes.
2. **Regional pressure and parallelism.** With multiple regions, the original
   `RegionalTick` gives each regional `Tick` one worker. The current scheduler
   uses nonsaved measured cost to give a dominant expensive region at most 4
   inner workers, with the remaining worker budget processing other regions.
   It needs two serial samples, at least 20 ms predicted work and 2× dominance;
   hysteresis and periodic original serial probes avoid unstable routing.
   Unknown/changed regions and competing hot regions keep the original path.
   Fixed reduction order, physics and saved activity policy are unchanged.
   The matched general-policy prefix reduces digging owner time 842.3 →690.6 ms
   (18.0%); fresh 586.8 →592.1 ms is within repeat noise and selects no inner
   region. All 96 frames retain the original physical bytes and owner hashes.
   Process CPU rises 16.6% on digging: this exchanges otherwise idle cores for
   lower wall time, not lower total CPU. See the experiment above and
   [kernel details](../native/MAC_GAME_KERNEL.md).
   Test CPU aggregation/multigrid preconditioning with
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
5. **Main-thread work and allocation.** The later profile8 ultra baseline has
   five GCs/15 s, frame p99 120–123 ms and 13–14 ms mean native UpdateData.
   Current exact changes copy native readings directly from a pinned immutable
   publication and append/prepend events directly into their final unmanaged
   buffer, omitting two native vectors and temporary managed arrays. The event
   control verifies byte order, aliasing, overflow refusal and zero managed
   allocation after warmup. These isolated controls do not certify Unity GC.
   Pool remaining snapshot storage, avoid duplicate per-cell render
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
receipts. This benchmark deliberately retains the fully active profile8
control with the physical-core worker count. New game colonies use profile9;
sleeping benchmark cells would change the fixed active-cell workload.

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

