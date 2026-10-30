# Performance acceptance measurements

## Native heat-copy replay, 2026-10-30

The native owner copies shared physical state once before the
heat retry loop. Each original admission, split, refusal, neighbour attempt and
deferred input remains in place. The captured base/DLC suffixes each contain
79 frames from the v5 storage probes. The replay checks their already-retained
heat prefixes before submitting new work; an earlier driver duplicated those
prefixes and is retained as invalid evidence.

Seven alternating paired runs plus one warm-up pair per world used WHOLE with
the other project jobs held. Each process runs cold and menu owner-restoration
branches. The primary metric is the median paired ratio of mean native-owner
advance time over the suffix, excluding its first restored frame.

| Capture / branch | Native-owner speedup | Paired range | Heat-stage speedup |
| --- | ---: | ---: | ---: |
| Base / cold | 2.216× | 2.137–2.364× | 4.771× |
| Base / menu | 2.226× | 2.072–2.340× | 4.767× |
| Spaced Out! / cold | 3.054× | 2.844–3.095× | 14.164× |
| Spaced Out! / menu | 3.059× | 2.609–3.167× | 14.019× |

All 32 processes complete, with every baseline/candidate complete owner and
numerical-status hash matching. The original 90 native tests, all 88 canonical MAC
outputs/exits (only existing `ref_ms`/`prod_ms` masked), 720 focused checkpoint
images and exact report/FPU/error comparisons pass their equivalence checks.
The eight known nonzero diagnostic exits remain unchanged. Allocation-failure
controls preserve prior physical state, metadata, caches and immutable aliases;
the allocation order changes, so there is no same-allocation-ordinal OOM claim.
The adopted source adds a permanent ownership/failure regression: clean Linux, Windows/Wine
and Ubuntu 22.04 each pass 91 native tests; Ubuntu requires GLIBC at most 2.34. Default captured game inputs also
retain all 32 independently captured cold/menu future images byte for byte.

These are native replay speedups with captured inputs. Actual game progress,
UI responsiveness and strict active game reload remain separate open gates.
The replay-generated future images are compared between implementations, not
to independently captured uninterrupted game images. The detach-only change is
integrated separately from runtime-continuation work; no secondary heat
optimization is included. The advance timer includes ledger work and excludes
the following projection tail. The combined ledger/projection stage measures
about 2.4–2.5 ms base / 6.1–6.2 ms DLC; existing timers cannot split it, so it
cannot be added wholesale to advance time. After detaching, flow accounts for
about 52–54% base / 61–62% DLC of advance time, making pressure and transport
preparation the next measured targets. Existing timer boundaries also bound
speedup through projection at about 2.11–2.13× base / 2.81–2.83× DLC; publication
and UI remain excluded. Stage bins include other work: the transfer bin also
contains activity/biology, and the flow bin includes post-flow processing.
Private evidence is in
`artifacts/release-ready-2026-10-30/heat-profile/actual-timings-whole-v1/` and
`heat-profile/detach-ready-proof.json`.

## Integrated detach follow-up, 2026-10-30

The frozen native `20c4361` plus guarded storage/strict-fixture managed source
passes all 24 storage scenarios across native Linux and WindowsPlayer/Proton,
with all four original 22-frame closed-heat windows and independent contact and
credit-scope audits passing. The unfinished runtime continuation and later
scalar lifetime repair are excluded from this particular frozen package.

Two original 900-frame speed-button-3 controls run on WHOLE with all other
project CPU work held. Actual full-game progress, including paused wall time,
is **2.917093× base / 2.335703× Spaced Out!**. Both finish with three living
duplicants, no owner faults or observed pauses, and no dropped submission or
publication samples. These are different cohorts from the earlier candidate,
so their difference is not a causal paired speedup measurement. The 3× and
smoothness requirements remain open, as does the longer normal-reserve gate.
Separate frame/pacing/publication diagnostics do not replace the uninstrumented
speed controls. They show about 1.6–2.6 ms mod work and 6–7 ms blocked native
calls per render frame, with about 18–19 ms left outside those mod scopes. That
remainder can include game code, rendering and waits; it is not yet attributed
CPU work. GC-containing frames still produce visible hitches. The DLC retained
pacing budget reaches its two-second cap and drops offered time without
accounting errors. A separate execution-only four-worker control reaches only
2.883547× base / 2.090191× DLC; automatic worker selection is unchanged.
Projection time increases with fewer workers, while captured pressure work
already mostly uses one-worker components. Those pressure paths remain the
focus of the separate private buffer-reuse candidate. Private evidence is in
`artifacts/release-ready-2026-10-30/heat-adoption-integrated-v1/`.

## Pressure-buffer and clock follow-up, 2026-10-30

The private integer pressure-buffer reuse candidate completes seven alternating
WHOLE pairs plus a warm-up pair for each captured base/DLC workload. All 32
processes preserve every complete owner and numerical-status hash. Both variants
also retain independent default cold/menu future images; a shifted-frame
negative oracle refuses. The original integer builder and numeric route are
unchanged, with a bounded aggregate 32 MiB retention budget and fresh-allocation
fallback. The candidate passes 4,063 builder/route controls, 100 full replay
comparisons across 1/2/4/8/12 workers, allocation-failure cleanup and all 88 clean
canonical output/exit comparisons. The eight known nonzero diagnostics remain
unchanged.

| Capture / branch | Median native advance ratio | Paired range | Median flow ratio |
| --- | ---: | ---: | ---: |
| Base / cold | 1.00973× | 0.97453–1.12516× | 1.02494× |
| Base / menu | 0.99813× | 0.87725–1.05140× | 1.01114× |
| Spaced Out! / cold | 1.02456× | 0.92579–1.06339× | 1.04555× |
| Spaced Out! / menu | 0.99068× | 0.98717–1.00754× | 0.99126× |

These are baseline/candidate paired means over the 78-frame suffix, excluding
the first restored frame. Cold first-frame median ratios are 0.94344× base /
0.99252× DLC; menu first-frame ratios are 0.99824× / 0.96364×. Fewer allocations
therefore do not establish a reliable owner-speed improvement. The candidate
remains private and unadopted. This is native replay evidence, not actual game
progress. Evidence: `pressure-post-detach/whole-v1/summary.json` under the same
private release-ready evidence directory.

A separate read-only WHOLE base-game clock census finishes the original
900-frame run at 2.925807×. In 2,495 eligible continuous same-speed Game.Update
calls it records two definite Unity clamp gaps totaling 1.4759190977 scaled
seconds before pacing receives the input. The conservative binary32 rounding
allowance is 0.00024551 s across those calls; invalid/error counts are zero.
The live maximumDeltaTime is 0.3333333433 s; the first observed clipped frame
offers deltaTime 1.0 s at scale 3 despite unscaledDeltaTime 0.7676832 s. Actual
Game.Update IL passes deltaTime unchanged to SimEveryTick, whose original
0.2 s cap is already handled by the retained pacing counter. That counter
cannot recover the earlier missing input.

Transition, startup and held calls are excluded, making this a lower bound
rather than a complete wall-clock reconciliation. No clock, global Unity
setting, pacing law or gate changes are adopted. Any repayment experiment must
audit all clock consumers and preserve bounded debt, pause/load/save semantics,
once-per-frame attribution and the owner busy fence. Recovering these gaps
alone would not establish DLC, late-colony or smoothness acceptance. Evidence:
`clock-census-v1/analysis.json` and `clock-census-base-v1/`.

## Latest measured candidate, 2026-10-30

The [release review](release-candidate-2026-10-30.md) records clean `0af5dfe`
WHOLE runs on the 5900X, with no competing project CPU work. Actual monotonic
full-game progress, including paused wall time, is:

| Control | Base game | Spaced Out! |
| --- | ---: | ---: |
| 900 owner frames, speed button 3 | 2.948404× | 2.134842× |
| 3,000 frames with normal starting reserve | 0.303476× | 2.154535× |

All four finish with three living duplicants, no pauses or owner faults and no
dropped submission/publication samples. The base run slows before its natural
autosave; frames 2,551–2,700 reach just 0.063×, with 1,619 ms mean flow,
1,663 ms native owner work and 3,179 ms between observed owner completions. A flow-only replay
of its natural owner-frame-2,752 checkpoint initially takes 232 accepted steps
and 35 rejected attempts, then relaxes to two accepted steps. It omits future
owner inputs and publication and does not reproduce that full live trajectory.

Moving the first interval's EOS array reduces one captured fresh-DLC flow
mean by 26.35% in interleaved WHOLE runs, with complete checkpoint byte equality.
All 88 clean old/new MAC command outputs and exits match, masking only existing
timing fields. The fresh full-game runs above show no material gain over the
previous frozen cohort. The fixed 100k/250k cost and 30-second disturbance
recovery gates still fail. Small accepted intervals, pressure/transport in hot
states, and conversion/publication in fresh DLC states remain measured targets;
there is no sustained representative 3× or Windows OS acceptance yet.

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

### Cached texture scalars

The next private 900-frame run enables bounded compact texture sampling with
the same native code, observer cadence30, seed, workers, GPU and speed setting.
It samples numeric presentation values from the immutable reading frame instead
of constructing full reading/presentation objects for each visible cell. A
viewport preflight originally rejected collisions, oversized bounds or unsupported tables
before using the original renderer. Cache keys include frame lease, model and
coordinates; retiring a lease clears references and prevents an old reader from
reinstalling them. At most 48 pooled buffers have 4,093 numeric slots each.

| Clean-window measure | Original texture path | Compact cache |
| --- | --- | --- |
| GC-containing frames per 15 s | 3 / 4 / 5 | 1 / 2 / 3 |
| Approximate heap growth, KB/Unity frame | 188 / 269 / 345 | 60 / 127 / 155 |
| Frame p99, ms per window | 90 / 104 / 146 | 32 / 101 / 108 |
| Warm owner/game progress | 2.588 / 2.590× | 2.667 / 2.668× |

Both runs pass; this is one ordered comparison with differing runtime inputs,
not a precise causal speedup estimate. Actual Mono allocation counting is
unsupported. The compact run admits 5,896 of 6,208 texture batches and has
13,630,746 cache hits versus 711,207 misses. It retains 126,883 numeric slots,
below the 196,464-slot bound. Paired correctness is disabled for this timing
run. The separate actual-game correctness control matches 6,144 CPU texture
destinations with zero mismatches; this is not GPU readback. Same-runtime
original/current controls also match 118,750,539 captured property, rejection
and unquantized-color bytes. Original rich validation/color loops remain the
independent oracle. These results justify the bounded cache, not sustained 3×
or the full smoothness gate. Late receipt work and GC remain open.
The production default now uses that cache, with `MA_COLONY_COMPACT_TEXTURES=0`
for the original path. Standard CI runs 569,496 controls in each mode, including
source/golden mutation negatives and optional getter/lifecycle fallback. The
full managed/game-contract suites and 439 Python tests pass; Windows contract
verification checks 2,587 dependencies with zero differences. Native code is
unchanged from the verified 89-test passive candidate.
Private evidence: `compact-observer/`, `linux-compact-observer900/` and
`texture-shortcut/` under the same artifact folder.

A separate publication-logging diagnostic ends at actual owner frame903. Across
932 nonempty publications, 35,970 byproduct calls consume 3.543 of 3.554 s of
dispatch, while receipt copy/append/ack total only 1.75/2.80/3.06 ms. Five ready
counts exceed the 1,500 bypass threshold, but long unbounded batches also occur
below it. Ordinary game polling can call `StepTheSim(0)`, which the existing
receipt gate treats as an unbudgeted step. Optional per-publication fields now
identify `step_s`, `limited`, `budget_ms`, `byproduct_max_ms` and `gc_delta`;
policy is unchanged. An explicit settlement-scope fix remains private pending
actual backlog, save/lifecycle and performance tests. Diagnostic logging adds
overhead; neither its timing nor receipt counts establish achieved throughput
or the number of newly created objects.

The follow-up diagnostic confirms 92 zero-time, unlimited publications below
the backlog threshold (at most 1,442 ready receipts): 1.816 s of dispatch,
15 calls over 50 ms and a 129.2 ms maximum. Positive budgeted publications
have a 2.137 ms p95; one 103.4 ms outlier contains a 102.9 ms single byproduct
call and a collection. Five positive-time backlog overrides cost another
815 ms. This ends at actual owner frame 906 and remains diagnostic evidence.

The explicit settlement-scope candidate then passes 900 requested frames but
is **not adopted**. Warm owner speed falls from 2.667 to 2.579× in one ordered
comparison, GC frames stay at six and late frame p99 rises from 108 to 148 ms.
Its 920 budgeted publications defer 537 times and hit the unchanged backlog
override 21 times (maximum copied count 1,600). Two milliseconds per simulation
publication cannot keep up with this stream. A published pending-count sample
of zero is stale/ineffective, not proof of an empty queue. Keep the original
policy while testing capacity across otherwise idle Unity frames. The private
candidate's 102,350 controls, including byte/order/ack and real barrier wiring
against a fake backend, do not replace actual save/lifecycle acceptance.
Evidence: `linux-receipt-diagnostic2/`, `linux-receipt-candidate900/` and
`receipt-budget/` under the same private artifact folder.

### Further private pacing tests, 2026-10-29

A private run raises retained time capacity from 0.4 to 2 simulated seconds,
keeping the original 0.2f argument cap, 1f/60f repayment limit and busy fence.
The original 17,138 controls plus 102,908 capacity controls pass; official game
Mono/Harmony passes the original 81 plus five configured checks in each mode.
The actual 900-frame test reaches 2.92756× owner / 2.92797× game progress over
the warmed span, versus 2.66721/2.66818× in the compact baseline. Its five
150-frame windows reach 3.014, 2.994, 2.785, 3.018 and 2.845× owner progress.
That does not establish sustained 3×.

Warm offered/admitted time is 153.6515/149.9982 s, repayment 23.2216 s, and
owed time grows 0.11198→1.98333 s. The 2 s bound is reached and 1.78196 s is
dropped. Accounting/exception/overflow/unexpected-addition counters are zero,
with balance within 8e-15. Clean-window GC frames rise from six to eight.
This is one ordered comparison with differing game inputs and the original
pacing-loss logger disabled in favor of retained-credit telemetry; it is not
a precise causal speedup estimate. Save/load/lifecycle controls remain open,
and production pacing is unchanged. Evidence: `retention2-run/` and
`linux-retention2-900/` under the tier-integration artifact folder.

A further private configurable repayment experiment retains the original
1f/60f default and tests 1f/30f, with the same hard argument cap and busy fence.
It passes the prior suites plus 101,886 added checks and official Mono/Harmony
default/configured controls. Proposed credit is recorded separately from
actual repayment and clipping; a larger proposal is not CPU headroom proof.
The 1f/30f run reaches 2.95351× owner / 2.95524× game progress. Warm repayment
is 26.8918 s versus 45.6025 s proposed credit; 872 offers are fully admitted
and 662 are clipped. It drops 1.13658 s and ends owing 1.08689 s. Accounting
faults stay zero, but sustained 3× is still unmet. This remains private.

### Idle-frame byproducts and combined pacing, 2026-10-29

A private pump drains only the already-observed byproduct prefix of a
successful managed publication during otherwise idle Unity frames. Session,
frame and highest-observed sequence fences exclude newly appended work;
ordinary callback, precise and pipe outcomes remain publication-only. Shared
delivery leases prevent reentry, and save/checkpoint/readiness/retirement guards
hold the pump. The original publication budget/backlog policy stays unchanged.
This changes Unity-time scheduling, not the fluid solver or its timestep.

The private controls pass 1,752 .NET and 1,750 installed-game Mono assertions
(two allocation assertions are .NET-only), with a disabled-pump negative
control. The raw 600-byte status/48-byte receipt layouts and 10,000 warmed
native interop iterations pass with zero .NET allocations and no clock advance.
These controls do not establish identical real-world callback timing.

Pump-only 900-frame warm progress is 2.80586× owner / 2.80626× game, versus
2.66721/2.66818× in the compact baseline. It acknowledges 12,393 byproducts;
maximum pending observed at pump entry is 1,129. Late frame mean falls
14.29→12.83 ms and the last clean-window maximum falls 217→128 ms; GC frames
rise six→eight. This is one ordered comparison with differing game inputs.
Runs include empty/fence-blocked checks. The snapshot lacks a final pending
count, and an unincremented stale-stop counter is not evidence of no stale
leases. Pump time is included once in the new top-level frame-log category;
receipt time remains nested within publication.

The combined pump plus 2 s retention/1f/30f repayment run reaches approximately
**3× actual progress** in this fresh base-game cohort: 150 owner seconds over
50.020943 real seconds, or **2.998744× owner / 3.000498× game**. Its five warm
windows reach 3.009/2.998/2.975/3.021/2.991× owner progress. Warm dropped credit
is zero; owed credit falls 0.06549→0.03084 s and peaks at 0.26221 s. Accounting
faults are zero, with balance within 4.4e-15. The pump acknowledges 9,274
byproducts; a fresh completion sample at owner frame 900 has **381 pending
receipts**, not an empty queue. Clean-window GC frames total five, with p99s
about 29/100/104 ms. No owner fault or critical log error is observed.

Both changes remain private. This is one starting-colony performance cohort,
not save/load, merge/dying-target, pipe-callback, lifecycle, retirement,
Windows OS or broad release acceptance. Targeted custody controls are next.
Evidence under the same private artifact folder:
`receipt-pump/`, `pump-run/`, `linux-pump900/`, `pump-retention3-run/` and
`linux-pump-retention3-900/`.

The same frozen combined package also passes a fresh Spaced Out! 900-frame
run: Expansion1 active, nine worlds, 79 species, 736×304 grid and 65,047 owned
cells. Warm owner/game speed is **1.95478/1.95592×**, with all five windows
between 1.940 and 1.974×. It reaches the 2 s credit bound and drops 78.474 s of
warm offered time; accounting faults remain zero. This is a separate native
performance limit, not a DLC 3× result.

Warm measured flow is 36.5–38.7 ms, heat 11.7–13.3 ms, transfer/activity/biology
8.3–9.8 ms and ledger/projection 6.2–6.4 ms; full submit/publication is 94–99 ms.
The last stage largely follows the partial owner timer, so these are not a
partition of its 59–61 ms. Flow includes solver advance and postprocessing;
pressure, transport and cloning are not yet individually attributed. The
native substep count is lower than the base run's, so the larger stage does
not establish extra substeps as the cause.

Every DLC texture batch falls back (0 fits/5,856 fallbacks); the rejection
reason is unrecorded. No scalar samples run, so the diagnostic table-species 0
does not contradict native 79 species. The pump delivers 0 and sees at most 12
pending receipts; fresh status at frame 900 has 0 pending at that instant.
Five clean 15 s windows each have one GC-containing frame; p99 is 119–139 ms.
There are no dropped timing samples, owner faults or critical log errors.
The next native experiment splits flow/postprocessing and ledger/projection in
a matched private replay, preserving owner/physical/receipt bytes. Evidence:
`linux-combined-dlc1-900/` and `pump-retention3-run/DLC1-README.md`.

The private combined build subsequently passes ordinary source overwrites at
owner frames 451/901, paused cold resave, independent file readback and menu
reload. Save barriers observe 44 pending receipts cumulatively and settle
before capture. The paused resave preserves the entire owner image and its
physical bytes; both readbacks restore writer owner hash `593dfec5…`.
That source has no pending packet/components. A separate current-combined
activity cohort passes all 16 original lifecycle checks, saves a queued actor
request at frame 34, preserves one pending frame during paused resave, rebinds
one actor without release and claims one packet. Independent file readback
restores writer hash `848703bc…`. Exact active-world continuation remains open;
later physical hashes differ, as in the older control.

The controlled real Lead fixture also passes after explicit setup failures
were repaired: genuine native sequences 31/32 reach the pump naturally. Normal
delivery keeps the same target and one live object; a truly absorbed dying
head produces two live survivors. Actual/mirror helper outputs match bitwise,
native rows remain pending until ACK, the remaining prefix stays exact and
mass survives the next Unity frame. This uses real target relocation and Grid
list registration, tracked external mirror adjustments and MarkedForMove to
isolate survival from unrelated landing merges. It establishes controlled
helper/ACK custody, not natural transport or identical whole-world timing.
The separate pipe/precise-outcome fixture now passes its original 12 checks
and a separately enabled backlog phase. Genuine byproducts precede a real
precise oxygen-pipe job; the pump stops at the protected receipt and leaves
its exact row, sidecar, job readiness and remaining prefix unchanged. Normal
publication then delivers and acknowledges that row once. These fixture
phases preserve the original acceptance bounds and 2 ms publication budget.

The pump and bounded pacing credit are now integrated separately on `main`,
with production opt-outs `MA_COLONY_BYPRODUCT_PUMP=0` and
`MA_COLONY_PACING_CREDIT=0`. Credit retains at most 2 s and offers at most
`1f/30f` extra per eligible call under the unchanged original argument cap
and busy guard. Permanent accounting, actual-body and game-Mono patch-order
controls run through `tools/build.py`; the full managed/game-contract suite
and 439 Python tests pass. These ports do not turn the private 3× starting
colony measurement into representative release acceptance. Detailed credit
telemetry is opt-in, and the old loss-only logger requires credit disabled.

A fresh final production-port base-game probe passes to owner frame 900 with
flags29/tiers, unset pump/credit flags (both default on), workers12 and RTX 5090
rendering on display `:99`. The private report-cadence30 observer remains the
only reporting change; compact admission diagnostics are off. Warm frames
151–900 achieve 150.021073 game seconds / 50.061325 wall seconds = **2.99675×**.
Five 150-frame windows are 2.998/2.997/3.003/2.960/3.027×, so this is nominal
3×, not every-window-at-least-3× acceptance. Session-end logs actually reach
904, with total progress 2.969× including startup. Three clean 15 s windows
have 1/2/4 GC-containing frames and p99 34.44/102.96/106.71 ms separately.
There are no owner faults or dropped submission/publication samples. This
repeat corroborates the narrow private starting-colony result, not the full
release speed/smooth-input matrix. Evidence: `linux-production-final-base900/`.

The final baseline DLC diagnostic explains the unused compact path: all
5,543 rejected batches are modulo-slot collisions (921 fresh / 4,622 cached
preflights), with no missing-cache, invalid, empty or unsupported result. The
first 736-wide viewport has 1,606 visible readings within 4,093 slots; global
indices 13,374 and 17,467 collide. Sorted/pooled source and 79 species are
confirmed. Existing admission still rejects collisions. Diagnostic warmed
game progress is 2.03572× (149.887742 / 73.628950 s), with five windows
2.021/2.001/2.028/2.061/2.068×. Counters and report cadence30 are enabled, so
this is not a clean acceptance or a causal speedup over the earlier DLC run.
A bounded conflict-miss experiment follows; memory bounds, complete cache
keys and slot locks remain fixed. Evidence: `linux-production-final-dlc1-900/`.

The collision experiment then uses one binary for diagnostic-off OFF/ON DLC
900-frame runs, with the same workers/renderer/cadence30 and unchanged inputs.
Warm progress is OFF 149.954393 / 78.656707 s = 1.90644× and ON
150.154433 / 77.085175 s = 1.94790×, a 2.17% observed gain in this ordered
comparison. Runtime workloads differ; the larger variation between earlier
baseline runs precludes a precise causal gain claim. OFF reports zero fits /
5,817 fallbacks; ON reports 5,445 fits / zero fallbacks, 10,761,988 cache hits
and 2,048,141 misses. Both pass without an owner fault. Actual session ends
are 903/904, not invented exact-900 end timestamps.

The late clean 15 s windows estimate OFF heap growth of
214.5/244.4/252.2/265.9 KB per Unity frame and one GC-containing frame each;
ON estimates 85.7/91.6/112.1 KB and 1/0/0 GC-containing frames. These are
unequal sets of process-wide heap estimates, not exact mod allocation counters
or aggregate latency percentiles. The rich/scalar same-process and actual
Linux Mono controls pass, including 8,000 concurrent all-field conflicts.
Separate actual CPU destination-byte proof precedes production adoption.
The unchanged native solver remains the larger DLC throughput limit.

The separate collision-ON DLC80 proof passes 6,144 actual CPU destination
comparisons (2,048 each for color, opacity and hazard) with zero mismatches,
722 admitted batches and no owner fault. It deliberately forces 273 rich
batches / 3,924 rich jobs; sampled frames are 5–40. This is sampled scalar/rich
and CPU destination proof, not GPU readback or performance acceptance.

Production now uses the bounded conflict-miss policy and removes its obsolete
occupancy scratch. Permanent .NET default/cache-opt-out tests each pass
569,506 assertions; actual Linux game Mono passes 569,496 in each mode,
omitting unsupported allocation checks. Full managed/game-contract suites
and 439 Python tests pass. The original rich constructor and full-provider
fallback remain independent; slot keys, locking and the 29,862,528-byte numeric
slot bound are unchanged. Native solver arithmetic and checkpoint bytes are
unchanged. Evidence: `compact-collision-final/production-source-freeze.json`
and `collision-production-full-managed.log`.

Finer matched DLC replay timing finds 222,885 held cells and 859 rebuilt cells;
64,152 held owned vectors copy 5,067,980 doubles (40.54 MB) per frame. Dense
setup/loop wall is 1.899/8.738 ms, compared with 10.951 ms enclosing dense/Derived.
The private immutable-vector snapshot prototype preserves retained mutable
reference semantics, signed-zero bits and independent data lifetime. A clean
new-ABI graph passes focused owner/cold controls and 248 storage assertions;
all 128 old/new replay outputs retain complete owner, physical and receipt
bytes. Warm means fall 26.726→23.707 ms (base) and 53.791→47.541 ms (DLC),
about 11% in both. The 28-sample p95s worsen 27.957→30.518 / 56.044→57.981 ms,
so widened tail, memory/lifetime and sanitizer checks precede adoption. A
second 32-frame/reversed-order matrix retains all 256 outputs, with warm means
27.125→23.569 / 53.945→47.326 ms but p95s 28.459→32.318 /
56.944→59.818 ms. The additional immutable pointer grows each material cell
104→120 bytes; a layout-preserving alternative is being investigated. These
are early captured replay windows, not fresh-game or DLC 3× acceptance.

A separate read-only 64-frame memory cohort preserves all 384 old/new-off/
new-on outputs. DLC source/result union mass-vector capacity settles near
41.70 MB with sharing versus 82.17 MB without it; sampled frozen references
are at most three. The overall allocated heap instead rises by about 3.09 MB
against the instrumented new-off variant, and RSS includes retained allocator
arenas. Weak-parent destruction controls pass; this bounded window establishes
neither indefinite memory stability nor a whole-process memory reduction.
Warm source-owned counts remain roughly 851–994 in DLC, so the slow frames
are not explained by repeatedly freezing all 64,000 vectors. Evidence:
`dense-frozen-memory/`; the 56-byte storage prototype remains unadopted.

The first layout-preserving descriptor also remains private. It restores
40-byte storage and 104-byte cells, passes 250 safety controls and preserves
all 256 outputs in a fresh 32-frame/reversed-order matrix. Base mean/p95 is
28.134/30.414→24.803/31.470 ms; DLC is
54.699/57.003→53.163/62.751 ms. Restoring size alone therefore does not remove
the tail regression, and its DLC mean benefit is small. A compact tagged
representation is the next bounded experiment; no native sharing variant is
part of the Windows candidate. Evidence: `dense-frozen-descriptor/`.

The next tagged variant uses 32-byte storage and 96-byte cells, preserving
owned deep copies and immutable zero/frozen alternatives without a pointee
mode flag. It passes 339 storage/transition controls, the wrong-alias negative,
native/cold/reference checks and 256 exact old/new replay outputs. In its
32-frame/reversed-order matrix, base mean/p95 is
28.120/29.674→26.668/33.682 ms; DLC is
55.258/62.819→45.110/48.859 ms. The DLC result supports actual-game testing;
base p95 still worsens, although these native replay times remain below a
66.7 ms frame budget at 3×. This remains private pending full-backend game
tests, sanitizer checks and broader output/platform verification. Evidence:
`dense-frozen-variant/`.

A fresh full backend built from the tagged variant passes its kernel, owner,
ABI and replay controls before private game testing. The actual DLC900 probe
then passes at 2.08992× warmed game progress (149.954392 / 71.751409 s), with
windows 2.080/2.057/2.098/2.131/2.085× and native owner means around
50.2–53.0 ms. The preceding rendering-fixed production-native run reaches
1.94790× with owner means 56.1–63.8 ms. This is a 7.3% observed improvement
in an ordered comparison with varying gameplay/environment, not precise
causal attribution or 3× acceptance. Compact admits all 5,021 batches; flags29,
RTX 5090, worker counts and private cadence30 are retained, with no owner
fault. Native overrides and the required private environment flag are recorded
separately in `native32-game-validation/probe-provenance.json`; the clean
package Git revision alone does not describe those native sources. Base-game,
sanitizer and broad platform/output checks precede production integration.

The private native32 BASE900 probe also passes at 2.99681× warmed game
progress (150.021065 / 50.060256 s), against production's 2.99675×. Individual
windows are 2.999/3.000/2.979/3.005/3.001×. Native owner means fall to
25.4–28.1 ms from 27.9–29.2; the game-clock result is unchanged within this
ordered comparison. Separate clean-window p99 values are
31.34/103.15/102.39 ms and GC counts 1/3/3, versus production's
34.44/102.96/106.71 ms and 1/2/4. These are per-window observations, not an
aggregate percentile or causal smoothness gain. No owner fault or dropped
timing samples occur. A fresh final receipt-pending count is not recorded by
this overlay/result, so pump counters do not establish an empty queue.

The native32 DLC run averages about 95.67 ms wall per 0.2 s owner frame;
true 3× needs 66.67 ms. Existing logs put the partial owner timer at
50.2–53.0 ms: edits 0.5–1.6, heat 5.6–6.2, transfers 7.5–9.3 and broad flow
35.7–37.0 ms. Flow includes solver advance and blocked-liquid, identity and
trace postprocessing; it is not a pressure-only timer. Ledger/projection
6.7–7.0 ms straddles the owner timer end, so these are not a disjoint partition.
Submit-through-managed-publication averages 88.8–92.7 ms and includes queueing,
owner work, projection and asynchronous observation.

Late native wait-log call windows report UpdateData 7.31–8.02 ms, next-frame
wait 9.77–14.55, shadow write 2.44–2.54, capture 1.35–1.42 and game-copy
3.66–3.80. Their call windows differ from the owner clock and must not be
added to it. FrameLog reports managed publication 1.782–2.325 ms per Unity
frame; views, snapshot, overlay, receipts and presentation are nested within
it. Residual “other” is 11.70–12.76 ms per Unity frame, meaning elapsed frame
minus the timed mod/blocked work, not necessarily vanilla CPU. Current logs
do not assign the entire gap to the solver.

The next exact investigations are a matched warm replay splitting broad flow
into pressure/transport/EOS/postprocessing, independent ledger/projection
timers, and bounded main-thread sampling of the residual, UpdateData frequency
and asynchronous handoff. Cold early-frame attribution does not substitute
for that warm capture. Preserve full owner/physical/receipt bytes and compare
timing separately; further cadence, timestep, physics or acceptance changes
are not justified by these measurements.

The final production source removes both experimental switches and adds a
permanent header-only storage test with 342 controls. Fresh full dependency
builds pass 90 Linux CTests and 90 Windows x64/Wine CTests, plus the complete
managed/game-contract suite and 439 Python tests. The exact public storage
header/test passes ASan/UBSan/leak checks; the frozen full-dependency variant
also passes kernel, owner, cold-load and captured-state sanitizer controls.
The final Windows DLL passes actual game-Mono raw/legacy 600-byte status and
48-byte receipt checks over 10,000 inactive calls without advancing the owner.
Wine/Mono results do not certify Windows OS gameplay or Mono allocation rates.

The same original 16-check activity cohort passes with the native variant,
saving a live callback/pending frame at owner frame 33. Paused cold resave
retains the full before/after owner bytes and one pending frame, rebinds one
callback with no release and claims the saved frame once; writer owner hash
is `6c2efced…`. Physical before/after/writer bytes match. Independent ordinary
cold readback restores that exact complete writer owner image and resumes
frame 33→113, retaining wrapper14/physical28, one rebound callback, no releases
and one packet claim without faults or serializer errors. Later continuation
hashes still differ, so these checks do not repair the existing active-world
trajectory divergence. Final public old/new comparison retains all 87 existing
MAC outputs and exit statuses after only the existing `ref_ms`/`prod_ms` masks;
the seven reference nonzero diagnostic/usage exits are unchanged. Long gates
were not run. Evidence: `dense-frozen-variant/final-public-mac87-verification.json`
and the retained comparison manifests.

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

