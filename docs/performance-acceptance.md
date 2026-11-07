# Performance acceptance measurements

## Thin-gas trace heating in spaced-out-worlds, 2026-11-07 (user decision pending)

The Spaced Out! acceptance scenario `spaced-out-worlds` faults at owner frame
3 from `ca77650` on (Linux and Proton): `Private flow EOS cell 276 (24,9 of
28x40)`, a 1e-12 kg hydrogen trace at 1.08e9 K, above the EOS domain. It
passes at `258134c`, `f10fe22` and `2095964`. `ca77650` changes no solver code:
its one-frame input lag changes this scenario's world enough to expose a
defect of profile 12's thin-gas drag (`f10fe22`). A fault dump reproduces it
offline with the current solver, and the same dump with the drag off runs
cleanly; `MA_COLONY_KERNEL3_PROFILE11=1` in game has no fault.

Mechanism: a 386 kPa pocket next to an empty cave leaks hydrogen (ONI's
1.008 g/mol and 2.4 kJ/(kg K) give a heat-capacity ratio near 4.4). The drag
holds the thin front back, so each front cell fills faster than it drains:
compression and the drag's friction heat raise its temperature, and the next
cell fills from it, 309 K to 1.6e8 K within about 30 flow intervals; the last
step put 9.7e-13 kg at 1.08e9 K into an empty cell. Mass and energy stay
exactly balanced; the energy is real pressure work concentrated on traces.
Profile 11 flashes the same front to 5.4e5 K within the frame and settles near
800 K.

Options, measured on the fault dump (10 frames) and on the Spaced Out! full
colony (flow ms a frame, 12 workers):

| Option | Fault dump | Hottest in-frame | Flow ms |
| --- | --- | --- | --- |
| profile 12 as adopted (1 g, 10 ms) | faults | 1.08e9 K | 57.8 |
| A: no drag (profile 11) | passes | 5.4e5 K | 318 |
| B1: drag 1 g, 100 ms | passes | 2.4e7 K | 85.4 |
| B2: drag 0.1 g, 10 ms | passes | 7.3e6 K | 101.3 |
| C1: thin gas shares heat across drag faces (prototype) | passes | 3.1e8 K | 56.1 |
| C2: thin gas not heated above the hottest gas it touches; the excess goes to its heaviest neighbour (prototype) | passes | 1.05e5 K | 59.1 |
| C1 + C2 | passes | 8.1e4 K | 59.3 |

Every passing option looks ordinary at the end of the frame and keeps the
ledgers exact. Recommended: keep 1 g / 10 ms and design C properly (an
implicit heat exchange between thin cells and the gas they touch within each
flow interval, which would also remove profile 11's 5e5 K flash), in place or
as a new profile. Ordinary new colonies have not hit it. Private evidence,
dump, prototype patch and scripts: `artifacts/trace-energy-2026-11-07/`.

A second failure in the same scenario on `133a4a1` (and on `b4a8e70` with
profile 11): "sealed probes are empty before the source" (probe cells hold
0.017-4600 kg). It passed at `2095964`; it likely follows `ca77650`'s
one-frame input lag in the scenario's timing and is not yet changed.

## Windows playtest recording, 2026-11-07

The user recorded 18 minutes of a new Spaced Out! colony (cycles 1-2, three
duplicants) on their Windows PC (Ryzen 9 9950X, RTX 5070 Ti, 2560×1440 HDR)
with the beta ZIP `40a1465`, speed 3. The recording is private; the figures
below were read from it.

- **Frame rate.** NVIDIA's overlay (14:40-14:50) shows 52-113 fps, mostly
  52-74, with the GPU at 8-17% and the CPU at 20-60%: the main thread limits,
  not the GPU. Camera pans have no repeated captured frames (at least 30 fps).
- **Game speed.** The status panel says `game clock 3.00×` and solver pace
  6-11×, but its owner frame counter advances 4-7 frames a second (0.6-1.4×
  real time over each 4.5-minute stretch; cycle 2, which began at 8:02, was
  still running when the recording ended at 18:15). Sampled at 10 Hz it holds for 0.6-1.3 s and then
  jumps by exactly 3 (2138, 2141, 2144, 2147, 2150, 2153, then 2161): ticks
  come in bursts of three with about a second between them.
- **Not reproduced off Windows.** The same ZIP under Proton on this host
  (WindowsPlayer, the same new colony, speed 3) runs at 3.0× (fps 40-51); the
  Linux build at 3.0× (fps 67-99). The cause is therefore specific to Windows
  itself (scheduling, timers, memory) or to that PC, and is not yet located.
  The `perf frames` lines now end with [`native_ms`](colony-owner.md#performance-log):
  the time from posting a tick to its hand-off, the solver thread's wake-up and
  whole frame, and ONI's native frame phases, so the next Windows Player.log
  shows where each tick waits.

**Cause, from the session's Player.log.** The log (Windows 11, Unity
6000.3.5f2, Direct3D 11, 2560×1440 borderless at 499.9 Hz, V-Sync on by
Unity's default) has 37 `perf frames` windows: about 101 fps (114,014 Unity
frames in 1,125 s), owner frames of 20-28 ms, yet tick intervals of 140-300 ms
and 0.67-1.9× game speed at speed 3, with few held ticks (0.5-0.9 a tick) and
a "slowest frame" of 2-25 s in every window that the recording never shows.
In the window of frames 2863-3012 the game advanced 30 s in 44.7 s of wall
time over about 4,650 frames: Unity reported about 2.2 ms a frame while 9.6 ms
passed and put the rest into rare multi-second frames, which ONI caps at 0.2 s
of simulation. That is one 2 ms refresh of a 500 Hz variable-refresh display
per presented frame: Unity derives delta time from the refresh count with
V-Sync on. The one window at 3.0× (frames 4213-4362, 64 fps, slowest frame
184 ms) is when the NVIDIA overlay and the Start menu were open, which
typically turns variable refresh off. The same frames cost the same at speed 1
(0.7× there), so the solver and the pacing were never the limit. The
[frame timing fix](settings.md) now detects this and turns V-Sync off; the
`perf frames` lines report `engine_frame_ms`, `real_frame_ms`, `short_frames`
and `timing_fix`.

Main-thread work found in the same review and changed: the gas textures were
recomputed and uploaded every frame ([gas texture reuse](native-fluid-rendering.md#gas-texture-reuse):
whole frame 11.8 to 10.5 ms on the Linux harness), and the status panel
rebuilt and re-measured its text on every `OnGUI` call. Other findings:
the panel's status line was cut off, the panel covered ONI's notifications and
full-screen screens, it lagged the cursor by up to 0.2 s, and air cells showed
`liquid 2.3E-12% by volume`; all changed (see
[native fluid rendering](native-fluid-rendering.md#player-visible-status-and-navigation)).
Seen and left for decisions: a cell that is two-thirds water is titled by its
gas (`OXYGEN`) and reads `Breathable` (the hover's breathing status judges
the gas only), and an open cell inside a 270 kPa room (124, 218) read
`no gas or liquid` for at least 15 s at 17:40-17:55, which looks like a cell
the owner does not hold.

## Follow-up cache, transport and presentation experiments, 2026-11-07

The next performance target is the fluid calculation: it accounts for
roughly 86–89% of native owner time in this follow-up. Keep the qualified
parcel changes in `40a1465` for the Windows rebuild. The further prototypes
below remain research; they have not completed production qualification or
established another live-game speedup. The existing full-cycle results below
remain the production comparison, and sustained smooth 3× remains open.

These tests start from the same private large-colony images and input
journals as the preceding investigation, with no benchmark-layout or physics
changes. The base pilot replays 600 positive owner ticks (120 game seconds);
Spaced Out! replays 450 (90 game seconds). Each run discards its first 50
samples, uses 12 workers on CPUs 12–23 and holds both project CPU locks.
The controls use `40a1465`. These are short fixed-input native comparisons,
not new live-game or full-cycle measurements. Peak RSS includes the complete
parsed journals and is not game memory.

### Material-table reuse

A private prototype retains one derived material table per thread, keyed by
the immutable species definitions, EOS, liquid stiffness and liquid law.
It replaces a species-wide check on each reading and a table rebuild on each
transfer. It retains the registry and EOS owners, so an address cannot become
a false cache hit after its previous owner dies.

The initial old/new/new/old base comparison gives 72.530 → 73.719 ms per
owner tick (+1.64% time). The reverse-order confirmation changes the direction.
Across all eight base runs, four per revision and 2,200 warmed samples per
side, the means are **72.896 → 72.433 ms (−0.64%)**. Control run means range
70.877–74.183 ms; prototype means range 70.578–73.881 ms. Transfer work falls
consistently, 6.337 → 5.623 ms (−11.28%); projection falls 2.245 → 2.100 ms.
The total benefit is small compared with the overlapping run variation.

The separate four-run Spaced Out! comparison gives **153.523 → 150.162 ms
(−2.19%)**, with transfers 11.516 → 10.224 ms (−11.22%). Flow also falls
136.141 → 134.159 ms although this prototype changes no flow arithmetic,
so the entire observed improvement should not be attributed to the cache.
Mean peak replay RSS is 4277.76 → 4237.69 MiB on base and
1012.80 → 1024.65 MiB on SO; these mixed process peaks establish no memory
saving. Retain the prototype for possible later qualification, without
promoting the stage saving into a proven colony-wide gain.

### Remap scratch capacity

This prototype retains mass, flux, swept-volume, clock, stamp and work-list
capacity between remap calls, rewriting every logical entry before use. The
six-mode synthetic benchmark averages 0.16340 → 0.15872 ms (−2.86%), with
overlapping run ranges. Mean synthetic-process peak RSS rises
6.496 → 6.922 MiB. That small-operation result does not carry through to the
large colonies in the separate old/new/new/old pilots:

| Remap-capacity pilot | Control | Prototype |
| --- | ---: | ---: |
| Base mean owner tick, ms | 72.132 | 72.292 |
| SO mean owner tick, ms | 152.795 | 155.376 |
| Base mean peak replay RSS, MiB | 4266.52 | 4311.82 |
| SO mean peak replay RSS, MiB | 1011.60 | 1003.69 |

Owner time is +0.22% on base and +1.69% on SO. The SO prototype runs average
150.840 and 159.912 ms, versus controls at 151.909 and 153.681 ms; all runs
are retained. There is no established large-colony gain or consistent memory
saving, so this prototype is not integrated.

### Reusing unchanged presentation data

A separate diagnostic compares every published positive-mass cell reading
with the preceding publication. It compares bit patterns of all six scalar
fields and the ordered constituent species, masses and partial volumes;
packed array offsets are ignored. Groups contain 64 consecutive public cell
IDs. A group is unchanged only when its reading membership and all its
readings match, including removal or addition of a reading.

| Reading-reuse opportunity after warmup | Base | Spaced Out! |
| --- | ---: | ---: |
| Mean positive-mass readings per frame | 23,225.7 | 53,099.2 |
| Unchanged readings | 58.23% | 79.18% |
| Groups containing readings | 1,290 | 2,060 |
| Entirely unchanged groups | 31.88% | 67.54% |

These are whole-colony opportunities, not visible-texture hit rates, rendered
pixel comparisons, allocation savings or FPS gains. Native publication
already reuses unchanged cells and blocks. Managed reading buffers are also
pooled; their compact texture cache is invalidated when a buffer gets a new
lease. A cache spanning publications must also account for viewport use,
ownership, passives, catalogue/model changes, load/reset and expired leases.
The next useful measurement is visible texture work across publications,
including the cost of validating reuse. The diagnostic's additional reading
copies are excluded from performance comparisons.

### Smaller candidates and concurrency

The late-cycle held inventory has 35,959 entries: 33,535 still hold mass and
2,424 hold only energy, across seven distinct source/radius keys. Destination
searches already run once per such key in a pass. The previously rejected
fresh coalescing index remains rejected.

A projection-only model uses those 2,424 actual energy balances. At one
recipient, recomputing versus remembering the projected amount costs
0.06885 versus 0.06954 ms per inventory. At the maximum 13 radius-two
recipients, the costs are 0.78324 versus 0.07540 ms. Each measurement uses
200 inventories, in old/new/new/old order, with identical result checksums within each recipient count.
The latter saving, about 0.71 ms, assumes every balance visits every nonempty
recipient; production can skip empty cells or stop sooner. It is an operation
model, including an opaque function-call cost, not an observed owner or game
speedup. No projected-energy cache was adopted.

Ordinary transfers already have an edit journal. Moving-parcel transactions
still need to encompass the caller's receipt, energy and wire checks; merely
adding an in-place core overload would not preserve atomic refusal. Existing
285-window base diagnostics show about two accepted moving-parcel retries
per sampled frame. This does not support treating that remaining copy as the
main bottleneck after the landed const-read fix. `PipeInventory.WithHeat`
already shares its immutable mass/passive arrays; avoiding repeated validation
is a possible small CPU optimization, not an array-allocation elimination.
No new managed-allocation or GC reduction has been established.

Game-thread/owner overlap is already in production. The remaining positive
native-frame fence orders messages, shadow writes, native contacts and paired
heat capture before handing the next frame to the owner. Removing it retains
the previously demonstrated contact/heat errors. Further exact overlap needs
state-independent native jobs or owner-confirmed contact transactions; it is
not another wait-removal switch.

The first flow proposal resets to `dt / parts` on every advance (0.1 s here);
the sized proposal carries between its flow parts, not between owner ticks.
Pressure and implicit thin-gas drag depend on the trial duration, so scaling
the first swept-volume ratio is not a guarantee that the next trial passes.
Remembering the previous tick's successful interval would change accepted
partitions and need a cold-continuation design. The SO region's repeated
accepted transport remains expensive even if the initial rejected trial is
made cheaper. Keep the 1 g drag and fixed gates unchanged. Multigrid and
conservative pressure/transport subcycling remain numerical research options,
with the earlier measured trajectory differences still relevant.

### Scope of verification

All 20 timed replays and both reading probes reproduce their respective
control endpoint bytes. The base endpoint at frame 1023 is
`0e04a12eb960e97580b5eddc44d1644f8fdaf8978a5d4d9db160335932117fc9`;
the SO endpoint at frame 7575 is
`e2caed727a430125f62b6538f99225d535281c18bc08b95e36798e26a6f0f8d6`.
Maximum relative mass/energy errors match within every cohort:
8.974e-16/5.714e-17 on base and 5.503e-15/2.201e-15 on SO. These are endpoint
and ledger checks, not a comparison of every intermediate checkpoint or a
claim of physical ground truth.

Both prototypes pass the full game-kernel control executable, including
worker-count and cold-continuation cases. The material-cache control also
compares complete readings and copied/in-place transfers against fresh-thread
results while alternating registries, EOS and liquid settings. The remap
controls retain the original pinned dense/sparse/empty/reversal outputs and
error/recovery expectations. No existing gate input or bound changed.

Neither prototype is included in `main`. The required full production suite,
all 88 diagnostic comparisons, 15 Linux scenario batches, Windows checks and
fresh live-game pairs have not been rerun for these unadopted prototypes; the
qualification recorded below applies only to the landed parcel changes.
Solver arithmetic, checkpoint bytes/formats, profiles and runtime defaults
on `main` are unchanged by this follow-up.

Private records in `artifacts/performance-next-20261107/` retain the patches,
binaries, commands, hashes, per-run measurements and host-load observations.
`REPRODUCE.txt`, `pilot-final-summary.json`,
`material-base-confirm-summary.json`, `texture-base.json`, `texture-so.json`
and `heat-projection-summary.json` describe the measurements. Saves, journals,
owner images and captured inventory remain private. The public findings page
is updated once after this research pass, as requested.

## Large-colony exact improvements and research, 2026-11-07

The requested 32-duplicant industrial benchmark is a frozen, instantly
constructed large state: 1,474 building/floor/wire/conduit objects, 141 tracked
devices (117 operational), 611 wires, 109 gas pipes, 144 liquid pipes and
34,441 owned cells. The private save is `large-industrial-base-32-v3.sav`,
SHA-256 `4eafe7b3570b08c442fc41f9844f1edb074b2da8ed9dc790b74063b67e64717e`.
The separate `full-colony-spacedout-v2.sav` stress case retains 14 duplicants,
1,147 such objects and about 67,857 owned cells, including its vented compact
second base; SHA-256
`774db516823f326f8f9f9735367b1b561adace161caa2b675ff03f2b189bce60`.
The saves and licensed captures stay private. The public findings and current
recommendations are at <https://finding.hilberto.com>.

A full cycle is 3,000 positive owner ticks, approximately 600 game seconds,
from a fresh frozen save at requested speed 3. The stress-case confirmation
uses 900 ticks and its own matched control; its vented region evolves, so a
short run cannot be compared directly to a full-cycle rate. Timed games hold
the game slot and both project CPU locks, use GPU display `:99`, and retain
commands, source/package/save hashes, timestamps and actual denominators.
The host is a Linux Ryzen 9 5900X with roughly 60 GiB RAM and an RTX 5090.
The game logs a 640×480 window. These are not Windows or 1080p performance
measurements. Unrelated host workloads remain outside the project locks.

### Exact implementation

Two pass-local caches avoid revisiting the whole held-precision inventory:
admission maintains the encoded-size total; release validates the inventory
once and then validates each replacement against the same count/byte bounds.
Failed replacements leave both the parcel and cached totals unchanged.
Zero balances, signed energy and the existing 65,536-entry/16 MiB limits are
preserved. The caches add no saved state.

Moving-parcel deposition now reads shared cells through a const view while
initializing occupied volumes, snapshotting face neighbours and accounting
for heat/energy. Previously, mutable indexing detached shared cell chunks
even when merely reading them. Writes still use the mutable state. The
regression control requires exactly one copied cell chunk for one zero-momentum
admission, including a chunk-boundary recipient and an existing partial-liquid
volume array, in all three kernels. It also checks exact conservation and
that the source checkpoint is unchanged. Solver arithmetic, operation order,
checkpoint bytes/formats and numerical defaults are unchanged.

### Fresh production comparison

The old code is `a426b58`; the candidate combines both parcel caches and the
const-read fix, with identical managed assemblies in both game packages.
Four fresh 3,000-tick native replays use the same initial image and 635 MiB
input journal in old/new/new/old order, 12 workers, CPU affinity 12–23 and both
project CPU locks. The first 150 ticks of each run are excluded; each side
contributes 5,700 warmed samples. Quantiles below pool those samples per side.

| Native replay | Old | New |
| --- | ---: | ---: |
| Mean owner tick, ms | 93.746 | 85.619 |
| Mean transfer stage, ms | 19.218 | 9.027 |
| Owner p99, ms | 215.236 | 123.837 |
| Worst owner tick, ms | 1003.417 | 150.318 |
| Worst transfer stage, ms | 919.964 | 38.960 |
| Mean peak replay RSS, MiB | 4378.1 | 4387.5 |

This is **8.67% less native owner time**, or 9.49% more native capacity, and
53.03% less transfer work. The old runs average 96.189 and 91.303 ms; the new
runs average 85.633 and 85.605 ms. The difference between the two controls is
retained rather than selecting the slower one. Replay RSS includes the journal
and parsed input history; it establishes no memory saving and is not game RSS.

All four endpoint owner hashes are
`a23e65512f83f817f8c316c196a1ab19cc4e10dae39443aa90c1978b6cf80d76`, matching the
historical fixed-input endpoint too. All 128 images from eight separate
16-tick controls match old/new at both 1 and 12 workers in both colonies.
The full-cycle check compares endpoints, not every intermediate full-cycle
checkpoint. Maximum relative mass/energy errors are unchanged at
3.581e-15/2.621e-16. There is no measured state deviation in these equivalence
checks; this is not a claim that the original algorithm is physical ground truth.

Fresh live base runs load the same frozen 32-duplicant save. Old/new advance
601.125/600.925 game seconds in 428.756/395.422 wall seconds: **1.402× → 1.520×**,
8.39% higher observed throughput. Warmed rates are 1.405× → 1.527× (+8.68%),
with mean owner ticks 110.04 → 97.38 ms. Both retain all 32 duplicants, include
the natural Cycle 2 autosave, pass telemetry and have no owner fault. This is
one ordered live pair; scheduling changes live inputs. The fixed-input ABBA
replay supplies the stronger isolated code-cost and exactness evidence.

Base peak sampled RSS is 4.154 → 4.191 GiB; managed-heap peaks are
1.639 → 1.612 GiB. These do not establish a whole-game memory reduction.
Project locks exclude other project benchmarks, while unrelated video work
continued on the shared host and was recorded. The 85.6 ms native mean alone
still exceeds the 66.7 ms budget for 3× before game/native handoff overhead;
sustained smooth 3× remains open.

The separate fresh 900-tick Spaced Out! pair retains all 14 duplicants and
passes telemetry/owner checks, but establishes **no whole-game speedup**:
old/new rates are 1.069× → 1.038× (−2.93% observed), or 1.104× → 1.058× after
warmup (−4.10%). Actual denominators are 180.939/180.739 game seconds in
169.250/174.164 wall seconds. Warm owner means are 160.52 → 165.94 ms, with
flow 140.40 → 144.76 ms and transfers 12.70 → 13.04 ms. Sampled RSS is
4.971 → 4.861 GiB and managed heap 2.131 → 2.053 GiB; this single pair does
not establish a lasting memory reduction.

That live slowdown prompted a fresh 450-tick input capture and a second
old/new/new/old native comparison on Spaced Out! (50 warmup ticks excluded,
800 warmed samples per side). Old run means are 157.693/150.583 ms; new means
151.158/153.153 ms. The combined means are 154.138 → 152.156 ms (−1.29%),
with overlapping run ranges. Transfer means are 11.443 → 11.579 ms; flow
136.766 → 134.573 ms. Replay RSS is 1017.95 → 1013.71 MiB. This check did
not reproduce the live penalty, and does not establish a useful SO gain.
It measures 90 game seconds under identical requested inputs, rather than
replacing the 180-second live result. All four endpoint hashes are
`e2caed727a430125f62b6538f99225d535281c18bc08b95e36798e26a6f0f8d6`;
maximum relative mass/energy errors match at 5.503e-15/2.201e-15. The extra
capture is diagnostic and is not counted as another timed live comparison.

Mean Unity-frame time on the base is effectively unchanged (44.32 → 44.24 ms).
The observed SO means are 30.65 → 31.36 ms. GC-containing frames include other
work and occasional startup/autosave stalls; they are not isolated GC pause
measurements. Faster owner computation has not established smoother input.

### Copy microbenchmark

A synthetic 65,536-cell world measures 100 independent zero-momentum,
single-cell admissions per process, in old/new/new/old order for each case.
The first case starts without an explicit occupied-volume array; the second
already has one because a distant cell contains partial liquid, and deposits
across a cell-chunk boundary. Both revisions use the same benchmark source,
12-23 CPU affinity and both project CPU locks. Each case's physical checkpoint
matches byte for byte across all four runs.

| Operation | Old mean ms | New mean ms | Cell chunks copied | Mean peak process RSS, old → new |
| --- | ---: | ---: | ---: | ---: |
| Gas-only, initialize volume array | 5.780 | 0.614 | 64 → 1 | 77.14 → 41.81 MiB |
| Existing partial-liquid volume array | 0.190 | 0.116 | 2 → 1 | 41.62 → 41.80 MiB |

These are operation costs, not whole-game gains. The first case is 9.4× faster
with about 35 MiB less synthetic-process peak RSS; the partial-volume case
uses 39% less operation time with no established RSS reduction. Deposits that
actually heat neighbours can legitimately write more than one chunk.

### Production qualification

The final production source passes `tools/build.py`: 93/93 CTests, the Python
suite (516 tests reported, 11 existing skips), and managed/integration checks.
The launcher test fixture also now mocks the newly required Steam-offline
step and verifies that it precedes launch; real Steam-offline protection is
unchanged.

All 15 required Linux scenario batches pass, covering all 66 named scenarios.
The strict coverage check confirms each expected scenario and its acceptance
checks, with no owner fault and no modified inputs or bounds. The four timed
large-colony loads also pass their telemetry and owner checks.

Every default `ma_mac_*` executable was built and run against separate clean
baseline/candidate trees: all 88 stdout/stderr and exit codes match, masking
only the explicit `ref_ms`/`prod_ms` timing fields. Eight default invocations
remain nonzero on both revisions: `curved_nodal_diagnostic`,
`mixed_transfer_tests`, `multiphase_gates`, `playtest_benchmark`,
`riemann_probe`, `riemann_slab_diagnostic`, `riemann_tests` and
`vacuum_front_diagnostic` (all with the `ma_mac_` prefix). These are unchanged
baseline failures, not newly passing gates. No input, grid or bound was changed.

Both Windows x64 native DLLs also build with the MinGW toolchain. The precision
inventory controls and the reference/production/game moving-parcel tests pass
as Windows executables under Wine. This checks compilation and focused native
behaviour; Windows OS gameplay, performance and interactive responsiveness
remain for the user's rebuild/playtest.

The exact implementation is qualified for integration into `main` and the
user's Windows rebuild. It does not complete the sustained smooth 3× goal.
Private reproducibility records are retained in the speed worktree under
`artifacts/performance-integration-20261107/`: `REPRODUCE.txt` and
`reproduce-game.py` describe fresh-tag, locked frozen-save reruns;
`final-summary.json`, `replay-qualification.json`, `so-matched-summary.json`
and `copy-benchmark.json` hold the timings and state identities.
`candidate-source-hashes.json`, `all-mac-comparison.json`,
`fifteen-batches.json`, `candidate-final-build.log` and `windows-check.log`
identify the tested source and qualification results. Retained control and
candidate game packages use the same managed assembly. Saves, licensed
captures and journals are private and are not included in the repository or
public page.

### Further candidates and measured limits

A fresh coalescing index each frame was rejected. The captured held inventory
has 35,959 entries and 19,866 distinct exact keys. A construction-inclusive
microbenchmark with 16 queries, half missing and first-match semantics,
takes 0.410 ms by scanning versus 2.232 ms rebuilding an index. Over 285 warmed
diagnostic frames, held admissions have median 13, p95 19 and maximum 30.
The index only wins at much higher query counts (128 queries: 3.193 versus
2.186 ms). A persistent index would need separate order/invalidation controls;
this experiment establishes no whole-game gain.

The earlier diagnostic frame places the industrial base's large-region work
at 27.57 ms pressure, 16.05 ms interface correction and 4.32 ms transport.
The vented Spaced Out! region instead needs 66 substeps/83 attempts: pressure
61.38 ms, transport 69.26 ms, interface correction 2.56 ms. These are diagnostic
samples, not exclusive whole-cycle averages. Reducing rejected trials or
pressure iterations alone does not remove the repeated accepted transport.
Local conservative subcycling is a substantial numerical candidate, not an
implemented optimization. Splitting the base's connected interface cluster
needs complete read/write dependencies and preserved per-cell ordering.

Production already overlaps game-thread work and the owner. The experimental
extra native/owner overlap hides roughly 13 ms of `UpdateData`, with an extra
0.2 game seconds of reader lag. Historical short live pairs improved
1.726→1.978× on base and 1.030→1.108× on Spaced Out!, but synthetic contact
controls demonstrate 0.25 kg freezing and 0.15 kg withdrawal discrepancies,
and 4,000 J skipped heat after a cell empties. The owner-only ledger can still
close. The removed-wait prototype is not adopted; it needs owner-confirmed
native contacts, heat reconciliation and lifecycle fences.

Multigrid and a 1 mL interface-detail threshold remain numerical options,
not exact wins. In the separate 6 November research cohort, a matched
full-cycle replay with parcel caches falls from
78.6 ms to 66.0 ms with multigrid, or 50.3 ms with multigrid plus that threshold.
Against the original algorithm, populated-cell temperature RMS/max deviations
are 3.72/38.87 K and 2.85/136.06 K respectively; liquid-volume L1 deviations are
0.573 and 9.980 m³. These compare trajectories under the same requested inputs,
not physical ground truth. The 136 K maximum occurs in a cell with about
3 g remaining; above 100 g of gas the maximum is still 49.14 K. Mass/energy
remain conserved, but local differences are material. Keep the adopted 1 g thin-gas drag: stronger drag suppresses
far-field dilute-plume transport. No numerical option or extra overlap is
included in the exact integration.

Ordinary game-kernel transfer edits already use `EditJournal` in
`mac_game_kernel.cpp`. The remaining transactional-copy candidate is
moving-parcel admission/retry staging in `mac_parcel_deposit.cpp`; it must
preserve atomic refusal, energy/face/passive accounting and immutable
publications. The const-read fix removes unnecessary cell detachment but does
not implement a larger moving-parcel rollback design. No speed gain is claimed
for that future design.

Managed publication/receipt churn, bounded pipe-contact scratch reuse and
texture reuse for unchanged immutable reading blocks remain source-grounded
candidates. Existing publication buffers and compact texture sampling are
already optimized. The diagnostic runtime's per-thread allocation counter
failed its sanity check; process-wide GC-free heap growth is only a proxy,
not an exact allocation attribution or pure GC pause measurement. No further
managed speed or memory improvement is claimed here.

## Spaced Out! full colony on profile 12, 2026-11-06

`full-colony-spacedout-v1` was built before profile 12 and restores profile
11, so it loads without the thin-gas drag (about 0.2×). It was rebuilt with
the same layout on `779d49f` (`tools/full_colony_benchmark.py` build, settle
two cycles, publish) as `local/saves/full-colony-spacedout-v2.sav`: profile
12, cycle 3, 14 duplicants, 1,147 buildings, 67,857 owned cells. Its settle
run averaged 0.98× (steady windows 1.04-1.10×; other builds shared the
machine).

Timed loads of v2, Linux harness, RTX 5090 display, whole game slot, speed 3,
450 frames, windows after the first:

| Run | Owner ms | Flow ms | Interval ms | Game per real | Unity fps |
| --- | --- | --- | --- | --- | --- |
| `779d49f` | 166-170 | 145-149 | 187-191 | 1.05-1.06 | 31-35 |
| same, game-thread probe | 169-172 | 148-150 | 190-195 | 1.02-1.05 | 34-36 |

The owner sets the pace: ONI's native frame waits about 135 ms a tick for the
owner frame before it, and the part before its handoff takes 15 ms
(`UpdateData` 12 ms). Garbage collection runs 1-3 times per 15 s at 170-220 ms
each; the slowest frames are 250-570 ms.

**Where the owner frame goes.** One flow region, the second asteroid's
compact base (1,548 cells, box 85×54 at 284,44), is the regional critical
path: 124-148 ms a frame, 63-65 substeps, about 80 attempts and 2,000-2,200
pressure iterations (the next region, 5,076 cells above it, takes about 61 ms
in parallel; ordinary regions take two substeps). Every refinement is the net
swept-volume bound; no pressure solve fails. Each frame's first 0.1 s trial
exceeds the bound 130-150 times at (337,75), the top corner of hall 1, and a
0.7 ms retry still exceeds it 1.4-1.5 times. The base's halls open into the
asteroid's near-vacuum caves (0.1-0.2 g of steam a cell beyond a one-cell gap
at 319,112): hall 1 holds about 2 g of oxygen a cell (about 150 Pa), and from
about owner frame 1,100 the base's duplicants are at breath 0 in unbreathable
air. The benchmark's second base is therefore vented, not a sealed colony;
its leak flow is what costs the owner.

**Thin-gas drag threshold, measured, not adopted.** A private build (not
committed) let an environment variable replace profile 12's drag mass; same
save and run length, 10 ms time constant:

| Drag mass | Game per real | Owner ms | Flow ms | Compact-base region |
| --- | --- | --- | --- | --- |
| 1 g (adopted) | 1.06 | 168 | 147 | 128 ms, 65 substeps |
| 3 g | 1.19 | 144 | 123 | 103 ms, 53 substeps |
| 10 g | 1.37 | 123 | 101 | 76 ms, 43 substeps |

All three passed with closed ledgers (relative mass error 5e-15, energy
2e-15). The hall's faces carry about 1.9 g, just above the adopted 1 g
threshold, but even 10 g leaves 43 substeps, so the drag is not the whole
cause. The user chose 1 g on 2026-11-05; changing it is the user's decision.

## Overlapped pacing, 2026-11-06

The user adopted overlapped ("post-ahead") pacing on 2026-11-05
([colony owner](colony-owner.md#overlapped-pacing)): the game posts its next
tick once the owner has been handed the previous one, and every game-thread
reader sees owner frame T-2 at the tick that posts T (serial pacing: T-1).
Before it each tick was serial: ONI's native frame (about 17 ms before the
handoff: messages, shadow, `UpdateData` 14-15 ms, capture), then the owner
frame (about 63 ms), then the game thread noticing the completion and posting
the next tick (the rest of a roughly 110-130 ms interval). Now the game
thread's work runs beside the owner, and the interval is the owner frame plus
the native frame before its handoff.

Linux harness, RTX 5090 display, whole game slot, load of
`full-colony-base-v1` at speed 3, 900 frames, stage and native-wait logs on
unless noted, windows after the first (other agents' builds shared the
machine; `main-1` overlapped one):

| Run | Owner ms | Interval ms | Game per real | Whole run | Submit to publication ms | Unity fps | Heat stage ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `f0ff869` serial, run 1 (a build beside it) | 73-80 | 134-154 | 1.33-1.50 | 1.40 | 142 | 24-30 | 5.0 |
| `f0ff869` serial, run 2 | 63-76 | 106-130 | 1.55-1.88 | 1.70 | 119 | 33-37 | 4.6 |
| `f0ff869` serial, run 3 | 64-69 | 114-136 | 1.49-1.75 | 1.56 | 126 | 23-31 | 4.1 |
| overlapped (committed package), run 1 | 62-68 | 87-94 | 2.12-2.30 | 2.13 | 190 | 27-36 | 4.4 |
| overlapped, run 2, no logs (contended: owner 72-77 ms) | 72-77 | 104-112 | 1.79-1.92 | 1.80 | 226 | 23-29 | 5.4 |
| overlapped, run 3, no logs | 61-67 | 88-94 | 2.13-2.29 | 2.12 | 190 | 24-30 | 4.4 |
| overlapped (an earlier package, same pacing), run 1 | 61-65 | 88-94 | 2.14-2.28 | 2.14 | 189 | 23-30 | 4.1 |
| same, run 2 | 61-73 | 87-102 | 1.96-2.28 | 2.09 | 195 | 24-27 | 4.4 |
| same, run 3, no logs | 61-64 | 87-93 | 2.16-2.29 | 2.12 | 189 | 26-32 | 4.4 |
| overlapped, rebased onto `2095964` (batched debris, flat germ frames), no logs | 59-62 | 85-89 | 2.25-2.34 | 2.20 | 181 | 29-36 | 4.2 |

"Submit to publication" is the real time from posting a tick to the
publication that shows its owner frame: the readers' lag in real time.
Overlapped, the native frame waits on ONI's worker for the owner frame before
it (41-48 ms a tick) and hands its own frame about 16-18 ms later; the next
tick follows at once, so the owner frame plus that part of the native frame
is the interval (88-95 ms). 3x needs that chain under about 67 ms (the clock
runs at 95% of the owner's measured rate). The research prototype's pacing
(timing-dependent readers, `artifacts/3x-research/all-prototypes.patch`) gave
2.08-2.20 on the same tree.

The heat stage did not grow (4.0-4.4 ms against 4.1-5.0 serial; no chunk
copies in the stage log, flow 58-59 copies in both): the solver is still idle
while the native frame before each handoff runs, and prepares the next
frame's private world copy then. Retired handoff publications are freed on
the game thread (`show_frame`), not on the native frame that hands the next
frame, which had cost 1.9 ms on the chain (`advance` 1.86 to 0.01 ms).

Correctness: the owner's frames, inputs and checkpoint bytes are serial
pacing's (`ma_oni_colony_tests` `overlapped_pacing_control`: three owner
timings end on the serial run's owner image). All 15 Linux scenario batches
of `tools/acceptance_suite.py` (`LINUX_BATCHES`) pass on the committed
package, rebased onto batched byproduct debris (`2095964`): 66 of 66
scenarios, relative mass error at most 2.9e-15 and energy error at most
1.0e-16; `tools/build.py` and the 93 native tests pass. The first packages failed
`phase`, `door`, `airflow`, `debris-heat` and `building-melt`: message
routing then trusted the lagged view of which cells the owner holds (an
arena's replacement of a freshly opened cell skipped its clear), which
replacements, digs and `ColonyRouting` now fix; the arenas' fluid fill and the
building-melt baseline then needed the one-frame shift described in the
colony owner page. No check, bound or gate input changed.

### Native frame beside the owner, a proposal (2026-11-06)

User decision 2026-11-05: prototype ONI's own native frame running
concurrently with the owner frame, against the last completed owner state.
Not adopted; the patch is private (`artifacts/3x-research/alongside-update.patch`,
`MA_COLONY_ALONGSIDE=1`, on top of overlapped pacing). A positive native
frame N no longer waits for owner frame N-1: it waits only until the solver
has started every handed frame, writes the owner state owner frame N-1
started from (owner frame N-2) into its shadow, runs `UpdateData` beside owner
frame N-1, and hands frame N, which the solver starts when N-1 ends. Readers
then see owner frame T-3 at tick T.

| Run | Owner ms | Interval ms | Game per real | Whole run | Submit to publication ms | Unity fps | Heat stage ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| native frame beside the owner, run 1 | 64-71 | 74-78 | 2.58-2.69 | 2.53 | 233 | 24-27 | 5.6 |
| native frame beside the owner, run 2 | 63-68 | 70-75 | 2.65-2.84 | 2.59 | 226 | 23-26 | 5.5 |

The owner frames now run nearly back to back (interval = owner + 9-10 ms);
the clock (95% of the owner's measured rate, `scale` 2.6-2.8) and the owner
frame set the pace. Its costs, measured on the same load (900 frames):

- **What goes stale.** Native heat exchange with owned cells (buildings,
  tiles, items, pipes, bodies, chunks: about 3,500 edits a frame) is computed
  on the shadow of owner frame N-2 and booked to frame N, which starts from
  N-1. 12.8% of those edits (about 450 a frame, 2.6e5 J) hit a cell whose mass
  had changed by more than 0.1% in between; energy stays exact (joules are
  booked), but the temperature step is that of the older mass. No edit hit a
  cell that had emptied. Native mass edits (3 in 900 frames) and freezes (none
  on this colony; the freeze/melt scenarios make them) act on the older mass:
  a freeze turns the frozen projection into a native solid while the owner
  removes the liquid the cell then holds, so mass would not balance.
- **Reconciliation it needs.** Freezes as owner-confirmed requests (the
  owner freezes what the cell holds and native places that solid next frame)
  instead of native freezing the shadow; a withdrawal larger than the cell
  then holds clamped and its shortfall returned to native; heat to an emptied
  cell returned to its body or a neighbour; and ownership the native frame
  reads frozen for its whole frame (121 owner open/close edits in 900 frames
  changed `owned[]` while a native frame ran).
- **The idle copy is gone.** With no idle gap the prepared world copy is not
  ready: the heat stage grows from 4.1 to 5.6 ms with 77 chunk copies, and
  the frame's input copy takes 0.8 ms. Preparing it on its own thread when a
  frame publishes would recover about 2 ms.
- **Readers one more frame late** (0.6 s of game time behind the owner,
  about 230 ms in real time), and the routing overlay one more frame long.

## Batched byproduct debris, 2026-11-05

User decision 2026-11-05: the microgram phase-change debris ONI spawned one item
at a time is batched, by the best measured method
([design](colony-owner.md#byproduct-batches)). Base full colony
(`full-colony-base-v1`, base game, cycle 4, 18 duplicants), Linux harness,
RTX 5090 display, whole game slot, load at speed 3, 900 frames.

**Where the debris came from.** A private trace of every spawned byproduct on
`f0ff869` (one row per spawn, and the owner's composition of each cell the first
time it spawns): 20,253 byproduct receipts in 181 s of game time (903 ticks,
22 per tick), all from the owner's `convert_identities`: 18,338 ice and 1,871
polluted ice from hidden liquids freezing, 44 dirt from polluted water boiling.
They came from only 65 cells (74 cell and element pairs), and each pair made a
receipt nearly every tick (median gap one tick, consecutive masses within
0.7-1.4x of each other): a steady trickle, not the decaying tails of single
freezes. 63 of the cells are cold air (257-273 K; about 2.2 kg oxygen and
0.15 kg hydrogen) holding liquid traces (water, polluted water, liquid carbon
dioxide) that the flow carries in; each tick 18% of a cell's hidden liquid
freezes. Two are polluted-water pools whose minority water freezes.

| Debris mass (kg) | Receipts | Share | Share of mass |
|---|---|---|---|
| 1e-9 to 1e-8 | 4,682 | 23.1% | 0.01% |
| 1e-8 to 1e-7 | 5,221 | 25.8% | 0.06% |
| 1e-7 to 1e-6 | 4,516 | 22.3% | 0.5% |
| 1e-6 to 1e-5 | 3,593 | 17.7% | 4.0% |
| 1e-5 to 1e-4 | 1,720 | 8.5% | 15.2% |
| 1e-4 to 1e-3 | 449 | 2.2% | 35.7% |
| 1e-3 to 1e-2 | 71 | 0.4% | 39.7% |
| 1e-2 to 1e-1 | 1 | 0.005% | 4.9% |

Together 0.355 kg. A spawn took 137.5 µs on the game thread (mean; p95 371 µs):
250 µs when ONI created a new item (46% of receipts) and 41 µs when
`Substance.SpawnResource` added the mass to a same-element item already in the
cell (54%), about 3.1 ms per tick. The publication budget (2 ms) and the idle
pump spread it over Unity frames.

**Candidates.** (1) Owner-side batches per cell and element, released by mass
or by owner time. Replaying the traced receipt stream (counts per 15 s of real
time at the run's 1.74 game seconds per second):

| Batch mass | Wait | Items per 15 s | Per tick | Mean / max delay (s) |
|---|---|---|---|---|
| none | - | 2,917 | 22.4 | 0 |
| 0.1 g | 10 s | 220 | 1.69 | 4.1 / 10.2 |
| 1 g | 5 s | 183 | 1.41 | 4.5 / 5.2 |
| **1 g** | **10 s** | **116** | **0.89** | **8.1 / 10.2** |
| 1 g | 30 s | 63 | 0.49 | 17.9 / 30.2 |
| 1 g | none | 32 | 0.24 | 12.2 / 165 |
| 10 g | 10 s | 96 | 0.74 | 9.9 / 10.2 |

At 1 g and 10 s the spawn work falls to about 0.1-0.2 ms per tick; a longer
wait saves under 0.1 ms more while debris appears later. (2) Merging each
receipt into a same-element item in its cell is what ONI already does for 54%
of them, and still costs 41 µs a receipt (about 0.8 ms per tick here) plus the
receipt itself; most trickle cells are in mid-air, where the item has fallen
away, and moving the mass to the pile below at once would be a flash. (3)
Leaving the trace liquid in the cell until it is heavy would change the
solver's trajectory and show supercooled liquid to every reader, with no bound
on how long it stays. The first was adopted with 1 g and 10 s.

**After.** Same load, `MA_FRAME_LOG=1` (windows after the load), batched build
against `f0ff869` (the private trace hook disabled):

| Per 0.2 s tick | `f0ff869` | Batched |
|---|---|---|
| Byproduct spawns per 15 s window (per tick) | 3,382 (20.4) | 114 (0.60) |
| Spawns that made a new item | 46% (trace) | 77% (524 of 681) |
| Receipt lap ms | 2.16 | 1.02 |
| Idle byproduct pump ms | 1.47 | 0.07 |
| Whole publication ms | 4.49 | 3.23 |
| Unity frame ms, mean / p95 (windows) | 26.9-36.4 / 33-139 | 24.6-27.8 / 34-64 |
| Collections per 15 s window | 3-5 | 1-3 |

The batched debris is 22-308 g per window in all (the trickle's own mass);
its decades spread from 1e-9 kg (pairs whose 10 s carried only micrograms) to
1e-2 kg. With `MA_COLONY_PUBLICATION_LOG=1` a publication's receipt delivery
spawned 8.7 byproducts in 1.29 ms before (budget-limited, the idle pump took
the rest) and 0.76 in 0.35 ms after; its other 50-55 receipts (consumers,
emitters, pipe outcomes) took about 0.9-1.2 ms in both and are what remains of
the receipt lap. Without the frame log (stage and wait logs only), three runs each,
alternated:

| Build | Game per real | Unity frames | Tick interval minus owner frame, ms (window means) |
|---|---|---|---|
| `f0ff869` | 1.763, 1.683, 1.789 | 3,606, 3,491, 3,475 | 45.2, 49.6, 46.4 |
| Batched | 1.728, 1.825, 1.819 | 3,939, 3,887, 3,700 | 44.1, 42.5, 44.5 |

The owner frame (61-75 ms, varying between runs) still dominates the tick
interval, so game per real moves within its noise (means 1.745 and 1.791); the
game thread's share of each tick falls by about 3.4 ms (47.1 to 43.7 ms), and
it no longer grows as the colony's trickle grows (the base runs' late windows
reach 52-55 ms). Unity frames rise 9%.

**Exactness.** Batching changes delivery only: no solver arithmetic, owner
world, ledger or checkpoint byte changes, and the save barrier releases every
batch before a save (`ma_oni_colony_release_byproducts`). Native controls
(`ma_oni_byproduct_batch_tests`) check release at 1 g with the mass-weighted
temperature, release after 10 s in cell and element order, compensated sums
(2^30 kg plus 2^20 parts of 2^-24 kg exact), summed germ identities, closing
cells, the census copy and the unbatched 0 kg mode. In `ma_oni_colony_tests` a
5 kg hidden freeze keeps every frozen kilogram in a receipt or a pending batch,
releasing the batches leaves the checkpoint byte-identical and a restored
owner holds none. The receipt-pump controls deliver released batches at a save
after the queued receipts with unchanged payloads, including a batch made by a
positive frame that reaches the owner while the barrier waits for ONI's
zero-time frame (the first in-game save released before that wait and was
refused; a negative control with that order fails), refuse a capture holding a
batch and refuse an unknown batch version; the melt custody census counts
pending batches.

## Game-thread pipe and publication work, 2026-11-05

After the precision-retry fix the base full colony (`full-colony-base-v1`)
ran at about 1.5x with a tick interval near twice the owner frame: the game
thread's own work and its garbage collections were half of every tick. Six
exact changes (nothing the game or the owner sees changes) cut it:

1. Exact pipe sums (`PipeAmount`) kept every expansion as its own array, and
   each addition, product and normalization built new arrays and a list: a
   30-atom germ population allocated about 17 KB per add-and-scale. Up to two
   atoms now live inline and the operations work in stack buffers.
2. The pipe scans (thermal and phase boundaries, biology, contact heat, save,
   audit) walked the whole grid for both phases; a bitset of allocated cells
   now steps between them. Biology reads each conduit temperature with a
   direct call instead of reflection with a boxed handle.
3. A pipe inventory is immutable; its temperature (an exact sum over its
   constituents) was recomputed several times per cell per tick and is now
   remembered for the pipe catalogue until the catalogue changes.
4. Biology capture boxes a cell's temperature handle once, keys native rows by
   the handle's own equality, allocates no closure for a germ-free cell, and
   keeps a germ-free cell's culture-free state object.
5. Sums, differences and products normalize in one scratch buffer and allocate
   only a result of three or more atoms; normalization skips the subtraction
   pass when the top atom is already the correctly rounded value.
6. The publication overlay reads the game's element tables once and each view
   by reference; its substance changes and irradiated cells go into reused
   buffers that their consumers copy.

Linux harness, RTX 5090 display, whole game slot, load of the save at speed 3,
900 frames, development allocation probe on (main-thread time with no
collection inside, and heap growth, of `Game.StepTheSim` and the pipe and
publication entry points), windows after the load. Per 0.2 s tick:

| Build | StepTheSim ms / MB | Pipe biology | Pipe heat | Pipe flow | Publication | Collections per 15 s (mean pause, share of wall time) | MB per frame | Tick interval ms |
|---|---|---|---|---|---|---|---|---|
| `5a10e27` (before) | 17.7 / 9.2 | 5.3 / 4.6 | 3.1 / 1.7 | 3.0 / 1.6 | 5.0 / 0.9 | 14-17 (193 ms, 19.7%) | 9.0 | 124-132 |
| 1 inline atoms | 12.8 / 1.9 | 3.5 / 0.9 | 2.2 / 0.3 | 1.2 / 0.2 | 4.7 / 0.2 | 4 (195 ms, 5.2%) | 1.7 | 118-123 |
| 2 cell scans | 11.4 / 2.0 | 3.0 / 0.9 | 1.4 / 0.3 | 1.3 / 0.3 | 4.6 / 0.2 | 3-5 (5.6%) | 1.8 | 108-128 |
| 3 temperatures | 9.4 / 1.4 | 2.3 / 0.7 | 0.8 / 0.2 | 0.7 / 0.1 | 4.4 / 0.2 | 4-5 (5.4%) | 1.6 | 111-122 |
| 4 biology capture | 9.3 / 1.4 | 2.1 / 0.6 | 0.9 / 0.2 | 0.7 / 0.1 | 4.4 / 0.2 | 4-5 (5.2%) | 1.5 | 107-116 |
| 5 fused sums | 7.6 / 0.5 | 1.3 / 0.1 | 0.6 / 0.1 | 0.5 / 0.03 | 4.2 / 0.1 | 3-4 (172 ms, 3.9%) | 0.6 | 110-118 |
| 6 overlay | 7.7 / 0.5 | 1.4 / 0.1 | 0.6 / 0.1 | 0.6 / 0.03 | 4.1 / 0.1 | 3-4 (4.0%) | 0.6 | 111-120 |

The colony has only 76 occupied pipe cells (about 22 with germs): long exact
expansions (germ populations, energies, the exchange accounts) carried the
cost. Step 5 was measured against step 4 with the same probe (9.0 ms / 1.3 MB,
5.1%). The overlay costs 0.95 ms before and 0.79 ms after per base publication
(98,304 cells); on the Spaced Out! full colony (`full-colony-spacedout-v1`,
223,744 cells, radiation on) 4.2 and 2.9 ms, and that save's allocation fell
from 2.0-2.3 to 0.3-0.4 MB per frame. Its collections (5-7 per 15 s, heap
about 2 GB) did not fall with it, and the owner frame (about 830 ms) still
sets that colony's speed.

Without the probe (the stage and wait logs only), the same load, windows
after the first; the change set measured on `5a10e27`:

| Build | Owner ms | Tick interval ms | Game per real | Whole run | Unity frames |
|---|---|---|---|---|---|
| `5a10e27` (three runs) | 62-67 | 119-131 | 1.53-1.68 | 1.551-1.556 | 2,499-2,553 |
| six changes (two runs) | 62-69 | 106-117 | 1.73-1.92 | 1.763-1.770 | 3,590-3,631 |

The Spaced Out! full colony stays owner-bound (owner frame 820-830 ms): 150
frames gave 0.208 and 0.213 game per real, at 25 and 33 frames per second.

**Exactness.** `ColonyConduitTests` compares the production `PipeAmount` with
the previous array implementation (kept as the test oracle `LegacyPipeAmount`)
over about 985,000 random operations (sums, differences, negation, products,
population weights, saved images, long expansions up to 39 atoms, half-ulp
ties around powers of two): every result has the same atoms, readout bits and
sign, and the 23,707 failures the same exception and message.
`RememberedTemperatureTests` checks remembered temperatures against fresh ones
across a catalogue change and that failures recur. A private control build
(not committed; `artifacts/3x-research/exactness-control-v3.patch`) ran the
replaced code beside the new code in game on the same state and counted
mismatches: the allocated-cell scan against a full array scan every tick,
every remembered temperature against a fresh computation, the original
biology capture against the new one (every update's row, native data, culture
keys and clock bits, inventory and change bytes) and the original overlay loop
on copies of the same publication buffers (all cell buffers, substance changes
and radiated cells). Zero mismatches in 1,505 base ticks (111,462 biology
updates, 32,878 with germs; 554,230 remembered temperatures; 650
publications), 123 Spaced Out! ticks (900 publications, all irradiated) and
the `mixed-pipe-biology`/`all-germs`/`pumps`/`thermal`/`gas-vents`,
`mixed-pipes` and `mixed-pipe-colony`/`catalogue`/`pitcher-pump`/
`storage-ledger` batches. The pipe scenario batches of the Linux acceptance
suite (6, 7 and 9-13) pass before and after. A same-input in-game byte
comparison is not available: two runs of one build already differ (scenario
details, as for a loaded colony's inputs), so the controls above compare in
the same process.

What remains on the game thread per tick (after): publication about 4 ms, of
which receipts about 2 ms (byproduct debris: several thousand microgram ice
spawns per 15 s, budgeted at 2 ms a publication, the rest by the idle pump;
[batched since](#batched-byproduct-debris-2026-11-05)),
the snapshot copy about 0.9 and the overlay 0.8 ms a publication; pipe
work about 2.6 ms; ONI's
own native frame (UpdateData about 14 ms) and its other updates. The owner
frame (62-68 ms) and the game thread's chain run in series, so the interval
stays about owner + 50 ms. The research prototype that overlaps them
(pipelined pacing, `artifacts/3x-research/all-prototypes.patch`, private and
not adopted) on this change set gives 2.08-2.20 game per real at speed 3
(whole run 2.06) and 2.11-2.25 uncapped (`MA_COLONY_DEV_SPEED=10`; whole run
2.11), against 1.88-2.11 (whole 1.91) on `4ac93b9`. Its chain is then the
native frame before the owner starts (15-16 ms) plus the owner frame (about
70 ms wall), and the next frame waits about 50 ms for the owner: with the
overlap the owner, not the game thread, sets the pace, so 3x needs the owner
frame plus that native frame under about 67 ms.

## Thin-gas drag, a proposed game-profile option (2026-11-05)

**Adopted (user decision 2026-11-05) with the mild setting, 1 g and 10 ms,
as game profile 12 / checkpoint 32 for new colonies**
([game kernel](../native/MAC_GAME_KERNEL.md#thin-gas-drag-profile-12--checkpoint-32)).
The measurements below were taken with the uncommitted prototype.

The Spaced Out! full colony's flow cost is thin gas (milligrams to a few
grams per cell) expanding into vacuum caves at hundreds of m/s: a cell's
acceleration into vacuum does not depend on its density, so a trace front
races like a dense one, and the swept-volume bound and the transport's
subcycles follow it. Raising the bound's mass floor alone cut flow only
about 35% (below).

**Prototype (uncommitted, `artifacts/fullcolony-speed/thin-gas-drag.patch`).**
A face whose inertia I (half the mass of its two cells) is below a
threshold m_thin feels an implicit drag on its momentum J at the rate
(1/tau)(m_thin/I - 1): zero at the threshold, growing as the gas thins. In
the backward-Euler pressure step the face enters the linear system with
inertia I(1 + gamma), gamma = (dt/tau)(m_thin/I - 1), and its new momentum
is (J_old + pressure impulse + body impulse)/(1 + gamma). The kinetic work
the drag removes, gamma J^2/I, joins the step's existing damping heat in
the face's cells, and the momentum it removes is booked as an external
load (like a wall). Mass and energy stay exact; thin gas seeps (velocity
about proportional to the pressure difference) instead of jetting. Faces
at or above m_thin are untouched: with the prototype disabled the world-dump
image hash equals the parent's, and with it enabled every cell holding
more than 0.1 kg stays bit-identical over a 2 s replay of the Spaced Out!
dump.

Spaced Out! world dump (frame 4140), locked cores; flow per 0.2 s frame,
and after 2 s without inputs the gas that entered the 28,627 initially
empty cells (and how many of them hold more than 1 mg):

| m_thin, tau | Flow ms | Pressure attempts | Into vacuum after 2 s | Gas moved differently |
| --- | --- | --- | --- | --- |
| off | 580-651 | 433 | 1.108 kg (3,620 cells) | |
| 1 g, 100 ms | 117 | 272 | 1.094 kg (2,411) | 0.0010% |
| 1 g, 10 ms | 72 | 245 | 1.081 kg (886) | 0.0016% |
| 3 g, 100 ms | 84 | 253 | 1.096 kg (1,405) | 0.0024% |
| 3 g, 10 ms | 78 | 239 | 0.983 kg (796) | 0.0047% |
| 10 g, 100 ms | 62 | 239 | 0.907 kg (879) | 0.0082% |
| 10 g, 1 ms | 64 | 232 | 0.569 kg (327) | 0.0103% |
| 30 g, 10 ms | 53 | 234 | 0.661 kg (374) | 0.0096% |

"Gas moved differently" is the summed per-cell mass difference from the
unchanged physics, over the world's 15,283 kg of gas. In game (Linux
harness, speed 3, the Spaced Out! save, windows after the first):

| Build | Owner ms (flow, transfers) | Interval ms | Game/real |
| --- | --- | --- | --- |
| main (`c238db1`-era) | 845-857 (656-667, 168-189) | 973-1006 | 0.20 |
| drag 1 g, 10 ms | 186-190 (146-148, 30-34) | about 516 | 0.39 |
| drag 10 g, 1 ms | 109-116 (83-85, 19-24) | 354-402 | 0.50-0.57 |

The in-game ledgers closed (relative mass error 4.5e-15, energy 7e-16), with
no fault. In game the transfer edits also fell, from about 2,500-3,300 a
frame to about 500. With the drag on, the interval is no longer the
owner's: the game ran at 5-6 fps, and the game thread's native calls took
only about 19 ms a tick (UpdateData 12, copy to game 3.3, shadows 2.5).
On the base full colony the drag changes the owner frame by nothing
measurable (owner replay of 38 journaled frames: 71.3 ms off, 70.5-72.0 ms
on); its state differs only where thin gas flows.

Adopting it would be a new game profile (a saved setting, so a new
checkpoint version), with the in-game scenario batches rerun on it. The
MAC gates (V vacuum front among them) run the reference and production
paths, not the game kernel, so they are unaffected.

## Full-colony speed, 2026-11-05

The full-colony base save (`local/saves/full-colony-base-v1.sav`: 18
duplicants, 790 buildings, 32,701 owned cells, 16,158 held precision
parcels at cycle 4) ran at about 0.5x: the owner frame was 320-340 ms, of
which 250-270 ms was the precision-inventory retry inside `transfers`.
`MA_COLONY_STAGE_LOG=1` now prints that stage's parts every tenth frame
(`transfers frame N: plan .. commit .. receipts .. precision ..`, with the
held count and the core retries attempted and accepted). Each frame about
3,150 retries reached `MacClosedWorld::with_moving_parcels`, which copies
the colony state, and every one was refused: the core admits a constituent
only near-whole (its rounding remainder at most 2^-24 of the parcel), and a
held parcel is held because it is far below that against its own cell. The
retry's scalar pre-check now applies that same rule before calling the
core. Also from the earlier worker's draft: a refused retry copies nothing,
each source's destination search runs once per frame, the neighbourhood
search clears only the cells it marked, and holding a new parcel sizes the
inventory without re-encoding it.

Linux harness, RTX 5090 display, whole game slot, load of the save at
speed 3, windows after the first (owner ms / interval ms / game per real):

| build | owner ms | interval ms | game/real |
|---|---|---|---|
| `f036519` (before) | 295-322 | 347-400 | 0.52 |
| this change | 64-68 | 128-135 | 1.48-1.57 |

The precision part fell from 250-270 ms to about 2 ms; flow (about 42-47
ms, regional run about 27-32 ms) is now the largest owner stage, and the
interval is about twice the owner frame, so the game thread also limits.
Exactness: `ma_oni_colony_tests` gains `precision_admission_control` (every
skipped retry is one the core refuses; every parcel the core accepts still
reaches it). A 64-frame replay of this save's owner under one journaled
input sequence (`MA_COLONY_INPUT_JOURNAL_DIR`), cold and after a menu
restore, gives byte-identical owner images on `f036519` and on this change.

The held inventory still grows (by about 2,000 parcels a cycle over the settle) toward
its 65,536-entry bound; the per-parcel cost is now a few scalar checks.

On the Spaced Out! full-colony save (`full-colony-spacedout-v1`: 14
duplicants on the start asteroid plus a second colony, 67,522 owned cells,
cycle 2) the owner frame was about 850 ms (0.2x): flow 655-670 ms,
transfers 170-190 ms. Its transfer plans (about 2,200-3,000 a frame) each
zeroed a grid-sized source buffer, 45-55 ms a frame; one buffer zeroed at
each plan's search cells now serves them all (planning 1-3 ms). A 25-frame
replay of that save's owner under one journal is byte-identical on
`f036519` and on this change.

What remains on that save is physics in one region. Temporary (uncommitted)
per-region timers on a world dump of frame 4140 show the regional run is
one region's serial run: the region in the box at owner x 244, y 101
(128x52, 5,086 fluid cells) takes 535-855 ms a frame with 208-319 pressure
attempts. Every other region is far cheaper (the next, 1,548 cells at x 284,
y 44, 58-146 ms; all others under 36 ms). The colony's digging opened its
caves onto empty ones: a sealed vacuum cave of about 556 cells above a
one-cell shaft at (320, 113), and a funnel of Space-zone cells (x 318-332,
y 68-87) that the owner's space sink empties every frame. Thin gas (0.1-3 g
per cell) expands into them at hundreds of m/s, and the 0.4-cell
swept-volume bound sets almost every accepted substep: 511 of about 1,400
accepted substeps over three replayed frames were limited by the shaft's
mouth. In replays of the save's journal the worst swept ratios are 200-260
at the full 0.1 s trial, at the vacuum cave's expansion front (y 127-134).
Replay variants of the bound's 1e-6 kg mass floor (a profile change, not
adopted): 1e-4 kg -> flow mean 576 ms (627 at 1e-6), 1e-3 kg -> 412 ms. At
1e-3 the region needs only 37-47 attempts, but each costs more (transport
subcycles at the front speed), so it still takes 290-480 ms.

**Open sky inside the flow (user option 1, 2026-11-05), measured and not
adopted.** A kernel change emptied Space-zone cells before every flow trial
(booked with the trace sink, its share reported apart for the space-sink
ledger) and left them out of the swept-volume bound; every `ma_mac_*`
output was unchanged. Owner replay of 15 journaled frames of this save
(locked cores, mean owner frame): 764 ms before, 979 ms with it, and about
350 instead of about 300 substeps a frame. With the Space cells kept empty
the cave vents faster (0.5 instead of 0.3 kg a frame) and the bound binds
one cell in, at the openings. Closing every face between gas and Space
cells instead (a temporary experiment: no venting at all) gave 791 ms,
because the substeps are set mostly by the expansion into the ordinary
vacuum cave, which no space rule touches. The patch is kept privately in
`artifacts/fullcolony-speed/`.

## Long-colony headroom, 2026-11-04

ONI caps speed 3 at 3× real time, so a steady 3.00× hides the slack. The
development-only override `MA_COLONY_DEV_SPEED=<x>` (2 to 20, read once, never
set by a package or the acceptance suite) lets the pacing target x× instead of
the game's speed-3 scale; `MA_COLONY_NATIVE_WAIT_LOG=1` prints where the game
thread waits. Natural autosaves of the RC1 ten-cycle run (`258134c` +
`7c87744`, `f9eeffa`), Linux harness, RTX 5090 display, 660 frames, windows
after the first (owner ms / interval ms / game per real):

| Save | Speed 3 | Dev 10× | Game-thread wait on the owner at 10× |
| --- | --- | --- | --- |
| Cycle 5 | 23-25 / 67 / 2.99-3.02 | 22 / 45-49 / 4.35-4.69 | 18.3 ms per tick |
| Cycle 8 | 23 / 67-68 / 2.98-3.02 | 22 / 46-60 / 3.84-4.60 | 19.7 ms |
| Cycle 11 | 22 / 67-68 / 2.99-3.01 | 22 / 46-49 / 4.41-4.61 | 19.9 ms |
| Pinned V9 Spaced Out!, new colony | (2.99-3.00, above) | 24-28 / 57-61 / 3.29-3.53 | 0.0 ms |

So the long base colony runs at about 4.5× uncapped, and the owner is not the
limit by itself (22 ms per 200 ms tick would allow 9×). At 10× the game
thread waits about 19 ms of the owner's 22 ms: the owner frame runs almost
in series with the game thread's own work (about 28 ms: UpdateData 13-14,
shadow write/capture 1.7, copy to game 1.5, the rest ONI's). Overlapping the
two is the largest remaining lever (bounded near 28 ms per tick, about 7×). On
the pinned Spaced Out! world nothing waits on the owner; the game thread
itself (UpdateData 8, copy to game 4, shadow 2.6 ms plus ONI's own work) bounds
it at about 3.4×.

The lower first window after loading a save (about 2.33× here, and the
"start-up hitch" in whole-run figures) is the post-load pause, not slow
frames: that window reports `pause_observed 1` with the same mean tick interval
as steady play (66.8 ms against 66.7-68.0 ms). A development-only run that
skipped the per-call hook counters (about 290,000 native hook calls per
UpdateData on the 256×384 base world: post-process and state-transition once
per cell, pressure 55,000, temperature 31,000) left UpdateData at 12.8-13.4 ms,
so that time is ONI's own per-cell work, not hook overhead.

One stall remains in the long base colony: shortly after loading the cycle-4
autosave (frames about 11160-11460, the same moment the ten-cycle run hit in
cycle 3), single flow steps take 1.7-2.8 s, in the regional run (`regional ...
run 2770 ms`). This happens on roughly one frame in fifteen for about 300
frames and drops those windows to 0.87×. A world dump taken at the start of
frame 11195 replays at normal cost (flow mean 18.8 ms, max 69 ms over 10
frames). The stage log now also prints every frame slower than 250 ms.

The watchdog dump of a slow step itself (`MA_COLONY_SLOW_DUMP=1`) replays at
2.4-2.7 s with 434 pressure attempts (normal frames about 108). It is the
comet-exhaust case again, in a cell the pre-flow open-sky sink does not cover:
a meteor shower's comet leaves 1.667 kg CO₂ in a vacuum cell just above the
ground (x 140, y 344) that is not a Space-zone cell, and during the frame the
puff expands across the surface vacuum, one connected 12,544-cell region. The
0.4-cell swept-volume bound then holds substeps near 0.35 ms; almost every
limiting cell is a front holding about 3 µg, just above the 1e-6 kg floor.
Replay variants (not adopted; each is a physics or profile change):
floor 1e-5/1e-4/1e-3 kg → frame mean 1.06/0.79/0.44 s; exempting the sky rows
from the bound → about 40% fewer attempts. The substep count is physics.
An exact restriction of each substep to the cells that hold mass would gain
little: the trace puff reaches all 10,114 fluid cells of the region by
substep 248 of 526 (mean 8,005 occupied cells in the first frame, 8,975 over
three), and the work already scales with the occupied cells (pressure solve
on active cells, remap microticks on affected cells). Only the whole-box
passes, about 1.3-1.7 of 4-4.5 ms per substep, could shrink, by at most their
unoccupied share: 10-13%. Running the region on 4-12 inner threads (exact,
hash unchanged) gave 5-15%, within noise.

**Comet exhaust in sky-connected vacuum sinks at once, user decision
2026-11-04.** In the game activity profiles (9-11) a comet's exhaust parcel
landing in vacuum joined through vacuum to a Space cell is booked as injected
and space-sunk at once and never enters the world
([colony owner](colony-owner.md), "Open sky"). Same cycle-4 load, 2,700
frames (8760-11464), speed 3, base game, Linux harness, RTX 5090 display:

| Build | Windows 11011-11460 owner_ms (mean / max) | Whole run game/real |
| --- | --- | --- |
| Before (`g-c4-slow`, `g-c4-hitch`) | 89-232 / 2,624-3,458 | 2.248, 2.208 |
| Exhaust sunk at once | 32.0-33.6 / 56.7-62.1 | 2.947 |

All other windows stay at 23.8-25.9 ms mean. No stage line reached 250 ms
(largest flow stage 36 ms). The stage log booked 31 exhaust parcels (51.67 kg)
in 17 frames between 11107 and 11463; the ledgers closed (mass 4.9e-15,
energy 3.0e-15). The `meteor` and `spaced-out-comets-geysers` scenarios pass.

## Copy-on-write cells and owner reuse, 2026-11-03

Exact changes on main `29b49c4`..`61f6f16` (all 88 `ma_mac_*` programs
identical from clean builds, checkpoint bytes unchanged): cells in shared
copy-on-write chunks of 1024 with chunk serials and cell stamps
(`mac_cell_store.hpp`); flow rebuilds and converts only changed cells and
hands its state on; the projection reuses unchanged blocks and cells; the
prepared copy pre-copies the last frame's heat chunks; registries, flow-built
masses and the radiation field are shared instead of copied. Profile 11,
Linux harness, RTX 5090 display, speed 3; steady = the 150-frame windows after
the first (which holds the start-up hitch):

| Run | Owner ms (before → now) | Steady game/real by window | Whole run |
| --- | --- | --- | --- |
| Pinned V9 Spaced Out! world, 600 frames (×2) | 41 → 26-31 | 3.000 2.994 2.991; 2.810 3.156 2.999 | 2.898, 2.873 |
| Base world, new colony, 600 frames (×2) | 15-18 | 3.001 3.000 3.000; 3.001 3.001 2.998 | 2.955, 2.952 |
| Cycle-2 base save (`base-cycle2.sav`, profile 10), frames 2903-3418 | 30 → 22-30 | 2.973 2.881 3.088 3.013 (was 2.749) | 2.816 |
| Frozen save `base-defaults-p11-v1`, 660 frames | 17-18 | 3.000 3.010 3.000 | 2.822 |

The pinned world's stages at frame 500 went: flow 28.8 → 15.8 ms (world-dump
replay 21-22 → 12.5 ms, byte-identical image), heat 7.5 → 1.9 (chunk copies
moved to idle time), projection 7.1 → 2.5, germ evolution 4.5 → 1.7. Steady
windows sit at the speed-3 cap; the remaining time per 200 ms tick is the game
thread (`MA_COLONY_NATIVE_WAIT_LOG=1`: write_shadow 1.4, UpdateData 8.0,
capture_shadow 1.2, copy_to_game 4.0 ms) plus the owner frame, which the
pacing holds the next tick for. Refused flow trials (swept-volume
refinements) cost 1.0 ms over 600 frames on the pinned world (74 trials,
stage log `run refinements`): not a measurable target there.

## Rejected attempts of the game kernel (2026-11-03)

**What they are.** For game profiles 9-11 `rejected_attempts` is not a failed
timestep. Each is one pressure refinement of the flow stage
(`native/src/mac_game/flow_local.hpp`, `tick.hpp`): a trial interval is thrown
away before any work, transport or ledger entry and retried shorter. A kernel
advance reports the deepest region's refinement count. The four criteria are
a component pressure solve failure, an unconverged pressure solve, negative or
non-finite pressure after the one-pass nonnegative correction, and a cell's net
swept volume above 0.4 cell. `MA_COLONY_REJECT_LOG=1` (`=2` per event;
`docs/colony-owner.md`) logs the criterion, frame, owner-grid cell, ratio and
rejected interval. It changes no arithmetic, timestep or saved byte.

**Causes.** In every run below all rejections were the swept-volume criterion;
solve failures, unconverged solves and negative pressures were zero. The
ratio is the largest net swept volume over 0.4 cell: from just above 1 (a front
barely over the limit) to 139 (profile 11, frame 489, owner cell 192,117).

**Counts**, 3,000 frames, all rejections in the swept-volume criterion:

| Run | Rejected attempts | Substeps |
| --- | --- | --- |
| Frozen save `base-defaults-p11-v1` loaded (profile 11), clean env | 56 | |
| Same save, `MA_COLONY_REJECT_LOG=1` | 51 (34 logged events, 18 frames, all in frames 189-421, owner cells x 107-112, y 162-171) | |
| Frozen save `base-defaults-p10-v1` loaded (profile 10) | 77 | |
| Fresh colony, pinned world (`--story-traits V9 --mixing 0`, starting reserve), profile 11 | 7,819 | 30,346 |
| Same world, `MA_COLONY_KERNEL3_PROFILE10=1` | 5,723 | 22,507 |
| Same world, `MA_COLONY_KERNEL3_PROFILE9=1` | 1,109 | 26,306 |

Run-to-run counts on one save differ (56 and 51) because duplicant actions and
frame timing differ; the log itself changes nothing. In the pinned world 70% of
the profile-11 worst cells (855 of 1,216 advances with a rejection) and 70% of
profile 10's (759 of 1,090) lie in x 100-130, y 170-200, the start room; profile
9 has none there, and its worst cells sit on the top rows (y 300+) and at
(103,332). Why profiles 10 and 11 reject more in the start room was not isolated: the
logs show only that the 0.4-cell swept-volume bound trips there with ratios up
to 139 at the full 0.1 s trial interval and is met after halving.

**History of the zero bound.** `zero_rejected_attempts` was in the archived
Codex ten-cycle driver `codex-ten-cycle.py` (2026-10-25), copied into the later
Claude and Codex final-acceptance drivers, and moved unchanged into
`tools/acceptance_suite.py` by `c668182` (2026-10-27, "checks are those of the
archived private drivers"). It was set when the frozen save ran on game profile 5
(physical version 23, the older kernel, whose counter is that kernel's own
step retries): every recorded ten-cycle run had `RejectedAttempts` 0 (`376bddd`, `cb59116`, `dde2a09`). No document gives a
reason beyond "a clean owner has none". Profiles 9-11 count refinements, so the
bound has not been meaningful since the game kernel was adopted. Decision (delegated, 2026-11-03,
[completion-plan amendment](completion-plan.md)): the bound now applies to the
failure criteria only. The suite checks `zero_failed_pressure_attempts` (the
pressure-solve failure, unconverged-pressure and negative or non-finite-pressure
counts, each 0, from the owner report's `RejectionCriteria`) and records the
swept-volume refinement count as a number. The counters
(`ma_oni_colony_rejection_counts`, reset when a session starts or restores,
never saved) count events over all regions for this session only, so they can
exceed or fall short of `RejectedAttempts`, which keeps the deepest region's
count and is saved with the colony. The 3,000-frame run on `base-defaults-p11-v1`
with the new check passes all 19 checks: 0 pressure-solve failures, 0 unconverged
pressure solves, 0 negative or non-finite pressures, and 38 swept-volume
refinements this session (`RejectedAttempts` 55 including the frames before the
save).

## Grown-colony slowdown, 2026-11-02

The 0.063× late window of the normal-reserve base control has two causes.

1. **A blocked game thread doubled every slow frame.** ONI posts a zero-time
   native frame every Unity frame. Its `NextFrame` hook waited for the running
   owner step, so the game thread's next `PrepareGameData` blocked for the whole
   step (up to 3.3 s), input froze, the blocked wall time did not become game
   time, and the clock only then moved on to the next 200 ms tick: the interval
   between owner frames was about 1.9× the owner time. Activity-tier owners
   claim their inputs at submission, so those frames now pass; positive frames
   still settle before their messages and shadow (exact storage keeps the full
   fence). Loading the natural cycle-2 autosave (owner frame 2752) under WHOLE,
   frames 2762–2812, with diagnostic logs on in both runs:

   | Build | Owner progress / wall s | Owner mean | Interval mean | Longest main-thread block |
   | --- | ---: | ---: | ---: | ---: |
   | `722c0a3` | 0.0701× | 1512 ms | 2851 ms | 3279 ms |
   | Narrowed fence | 0.1006× | 1958 ms | 1989 ms | 0.06 ms |

   The interval now equals the owner time (1.015×, was 1.89×); the live comet
   inputs differ between runs, so owner times differ. No solver arithmetic,
   checkpoint byte or input order changes.
2. **Comet exhaust in open sky.** The upper region is 10,120 Space cells. A
   comet emits 1.67 kg CO₂ (50 kg/s × 1/30 s) into a Space cell each 33 ms
   sub-tick, and the parcel expands into vacuum across the region for the whole
   0.2 s frame (about 455 accepted substeps) before the Space sink removes it.
   Flow is about 97% of the 1.5–2 s owner frame. This is physics, so the remedy is a
   user decision. A private, default-off prototype also empties due Space cells
   **before** flow, so a parcel placed in open sky leaves at once instead of
   first expanding: on the same save (narrowed fence, frames 2753–2816) owner
   time falls from 1810 to 34 ms and the interval from 1835 to 67 ms (0.106× →
   about 3× once loaded; 1.783× including the load). Gas reaching Space from a
   base breach still expands for its frame before the existing sink. **Adopted
   for profile 10** (delegated decision 2026-11-02) **and extended to profile 9**
   (2026-11-03) so saved grown colonies do not collapse; older profiles keep the
   after-flow sink only (`MA_COLONY_SPACE_PRESINK=1` forces it for diagnostics).
   On main with that change the same cycle-2 save (profile 9) loads and runs
   frames 2753–2816 at 1.773× including its load frame (owner 34.7 ms, interval
   70.6 ms), against about 0.3× without it. Excluding the load window, the
   steady state over frames 2903–3419 (516 frames, 103.5 game seconds, main at
   `868d0d5`) is **2.749×**; one 150-frame window drops to 2.27× (flow 34.6 ms),
   the others run 2.92–3.12×.

**Spaced Out! (fresh, 600 frames, WHOLE):** the narrowed fence changes nothing
here (2.232× → 2.209×; owner 44–50 ms). Per owner frame the critical path is
native pre-owner work (write shadow 2.6, ONI `UpdateData` 7.4, capture 1.3 ms),
the owner (median 44 ms: flow 28–36, first state copy 4.1, heat apply 1.8,
activity census/steps/germs 6.6, conversions 1.3 ms), projection 7.2 ms, then up
to one Unity frame until the next tick: median submit-to-handoff 29.5 ms above
the owner time, interval median about 74 ms, mean 84–94 ms with occasional
100+ ms frames. 3× needs 66.7 ms.

**After adoption (`09b5e60`, profile 10, WHOLE lock):** a fresh 3,000-frame
normal-reserve base colony (`MA_COLONY_STARTING_RESERVE=1`) holds 2.985× over
the whole run, 2.9–3.0× in every 150-frame window, with three duplicants
alive. Its world drew different story traits from the 0.30× run and booked no
open-sky mass, so it does not repeat the comet case; that case rests on the
forced-sink measurement above and the native control. The cycle-2 save from
`0af5dfe` was refused on the speed packages built after `7a1f72b` ("This save
evolves world germs and needs the game's disease table"). The cause was
packaging, not main: those packages paired a managed DLL built before the
radiation germ kill (it requires native world-biology capability 1) with a
native library built after it (capability 2), so `ColonyWorldGerms.Configure`
declined, never registered the disease table, and the germ save was refused.
A package built from one tree (`dotnet build` of the managed adapter, then
`package_probe.py`) loads the save on `9276f92`; rebuild the managed adapter
before packaging. The restore error now names the unavailable germ rules.

Profile 9 against 10 in game (same `09b5e60` package, `MA_COLONY_KERNEL3_PROFILE9=1`
for 9, 600 fresh frames, WHOLE; worlds draw different story traits per run):
Spaced Out! profile 9 2.118×/2.084× (owner about 52 ms), profile 10
1.987×/2.002× (owner 54–62 ms); base 2.955× / 2.951× (both at the 3× speed
cap, owner 27 / 29 ms). The same Spaced Out! runs reached 2.36–2.38× on the
`722c0a3`-based package, so commits since then (with profile 10 about 5% of it)
cost Spaced Out! roughly 10–15%; the per-stage log puts the extra time in the
first-edit stages and the transfers/activity bucket, not in flow alone.

**Flow profile on the pinned world** (owner dump before frame 450, 223,744
grid cells, 50,879 fluid, profile 10; `ma_colony_benchmark --world-dump`, 12
workers on the high cores, 12 frames, 2 warm-up; final image hash unchanged
in every variant): flow 22–28 ms per frame, of which sparse conversion
4.0–4.8 ms, the region ticks 6.7–8.4 ms and the dense rebuild 7.7–10.5 ms.
Only about 700 cells are rebuilt per frame; the dense step still constructs all
223,744 cells (setup 3–12 ms, copy loop 3.5–9 ms), and the ticks' own stages sum
to about 2.8 ms. Building dense cells on the pool, constructing them in place,
pre-sizing the regional merge, and glibc mmap/trim thresholds each stayed
within the ±2 ms run-to-run spread (trim threshold alone doubled flow time);
none is adopted. Most of the remaining flow time is format conversion between
the owner's per-cell world and the kernel's sparse fields.

Reusing unchanged 1,024-cell projection blocks keeps every view, reading and
checkpoint byte (native control) but finds almost no unchanged block in fresh
Spaced Out! (projection 7.2–7.5 ms either way); not adopted.

Preparing that first state copy on the idle solver thread between frames
keeps every checkpoint byte (native control, 5 of 6 frames from the copy). On
random worlds the gain was hidden by world-to-world spread; on the pinned
world (`--story-traits V9 --mixing 0`, 600 frames, WHOLE) the owner's first
edit stage falls from about 6 to 1.8 ms and owner time from 43–47 to 39 ms:
2.495×/2.395× against 2.135×/2.266×/2.287× without. Adopted. The parallel
native shadow (`2ae0865`) cut `write_shadow` from 2.6 to 1.2 ms per frame.
A dense germ-medium cache showed no stage gain and is not adopted. On the
pinned world, profile 9 at `3009148` (2.230×/2.178×), profile 9 at current
main (2.027×/2.224×) and profile 10 (2.135×/2.266×) agree within the
run-to-run spread: the apparent 10–15% regression was world variation. Other
agents' unlocked builds raised the machine load to about 48 during part of this
session, so a fresh base pair from that window is discarded.

## Actual renderer/report follow-up, 2026-10-31

The clean committed `3d8c20f` package completes the original900-frame speed 3
base-game and Spaced Out! controls with all other project CPU jobs held. It
includes compact renderer channels and serialized report writers, excludes
runtime-continuation WIP and private instrumentation, and predates the remap
integration in `4174d6c`. Each game uses its own WHOLE game slot. Original
automatic workers, inputs, clocks and Unity settings are unchanged.

| Full owner 0–900 window | Game seconds | Wall seconds | Actual game rate | Actual owner rate |
| --- | ---: | ---: | ---: | ---: |
| Base game |180.020626|61.796534|2.913118×|2.912785×|
| Spaced Out! |180.020634|76.936349|2.339865×|2.339596×|

Both finish with three duplicants, no owner fault, no observed pause and zero
dropped submission/publication samples. Their rates are close to the earlier
separate20c/managed-fixture controls,2.917093×/2.335703×. Different cohorts do
not establish a causal comparison. The renderer's isolated method gains do
not establish a whole-game speedup, and neither current full window meets 3×.
Smooth input, normal-reserve performance, sustainable progression and actual
Windows acceptance remain open.

The primary immutable result samples end at 900. The ordinary shutdown logs
also retain partial 901–911 base /901–907 DLC telemetry; those later windows
are recorded separately and do not replace the fixed 0–900 observation.
Private package/source proofs, commands, results and analysis are in
`artifacts/release-ready-2026-10-30/actual-speed-3d8c20f-v1/`, with the two
original game outputs alongside it.

## Velocity-cache feasibility, 2026-10-31

A separate private64-entry cache reuses immutable old face velocities within
each original128-face worker block/microtick. It preserves lazy first-use
validation and introduces no persistent state. It passes 6182 Release and
targeted ASan/UBSan/errno controls,24 full hot owner/receipt/pre-flow/status
frames,348 fresh cold/menu frames and a valid shifted-reference negative.

Against the adopted remap baseline, seven isolated hot pairs plus one warm-up
pair give median steady native advance ratio 1.014323× (5/7 faster,
range 0.980210–1.076161×). The first restored advance is slower: median
ratio 0.967028×, only 1/7 faster. Eight balanced fresh pairs plus one warm-up,
36 processes total, retain every original owner/status and independent future
image, but give the following steady-advance ratios:

| Default-OFF captured case | Cold | Native menu restore |
| --- | ---: | ---: |
| Base game |0.999331×|0.994748×|
| Spaced Out! |0.965330×|1.016068×|

These baseline/candidate ratios do not establish a useful general gain. The
small mixed hot gain, first-advance cost and fresh DLC-cold cost do not justify
adoption. The cache stays private; broader all 88/Windows qualification remains
unrun. Source-operation counts alone do not predict processor cost. Both hot
and fresh plans used WHOLE with all other project CPU jobs explicitly held;
neither measures actual game speed. Evidence is under
`artifacts/release-ready-2026-10-30/face-cache-prototype-v1/whole-{hot,fresh}-v1/`.

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

A private IL census scans 84,724 managed methods with zero resolution failures
and finds 392 Unity clock calls. State-machine elapsed time, creature heat
sampling and mask/suit interactions also use Unity's absolute clock; some
physical writers read deltaTime directly. Repaying clipped time only through
SimEveryTick therefore needs a wider consistency audit. Continuation must also
preserve clock epochs and derived heat-writer operands. No new clock provider
or pre-offer repayment is adopted.

The release player's recorder API supplies eleven requested metrics. A WHOLE
Spaced Out! 900-frame diagnostic, with other project CPU jobs held, completes at
2.317408× with no faults, pauses or dropped samples. Selected-method timers and
existing frame/credit logs are enabled; this is instrumented attribution, not
replacement acceptance or a causal speedup comparison. Warm native buckets
150–899 contain 2,271 render calls:

| Measurement | Mean per render frame |
| --- | ---: |
| Unity CPU main-thread frame time | 27.933 ms |
| Unity CPU render-thread frame time | 1.584 ms |
| Game.UnsafeSim200ms, including waits/mod publication | 10.968 ms |
| StateMachineUpdater.RenderEveryTick | 0.841 ms |
| World.LateUpdate | 0.604 ms |
| State-machine simulation sub-ticks, selected exclusive time | 0.464 ms |
| Registered animation updates | 0.054 ms |

Inclusive markers overlap. Method scopes remove only instrumented descendants
and include waits; recorder values describe the previous completed render frame.
The table is not an additive CPU partition. About half of main-thread time
remains outside these methods, motivating a broader lifecycle/UI probe. Four
warm GC events have maximum recorded GC time 125.2 ms. GPU and two graphics-wait
metrics have no positive readings; this does not establish zero GPU work.
The timer stack passes independent interval-union, nesting, exception,
invalid-order and allocation controls; two deliberate accounting mutations are
rejected behaviorally. Evidence: `unity-profile-v2/whole-dlc-analysis.json` and
`stack-negative-proof.json`.

Continuing owner input now substantially reproduces the older normal-reserve
base hot state. Its upper region has 10,124 fluid cells, including 10,120 Space
cells. Keeping the original saved queue, space mask and sink but adding no
future input produces 479/35, 124/24 and then 2/0 accepted/rejected substeps per
frame. Repeating the exact saved 1.6666667461395264 kg CO2/300 K parcel at the
original cell before each subsequent owner frame instead sustains 455/26.
The original live suffix averages about 528/31; other live inputs are absent
from this controlled reproduction. A Comet exhaust IL/operand match is a source
hypothesis, not identification of the actual live actor.

All 48 original/instrumented advances retain complete owner, receipt, pre-flow
world and numerical-status bytes. An independent 8,503-trial audit attributes
every refusal to the original volume bound. One stable repeated-input frame
has 518 pressure solves and 1,499 remap microticks visiting millions of active
cells/faces. Transport/remap takes about 45–54% of measured regional functional
segments, pressure solves 22–38% and assembly 5–8%; these are diagnostic phase
shares under shared CPU conditions, not acceptance timings. The plume reaches
all 10,124 cells, so an empty-cell-only shortcut cannot remove most work.
No timestep bound, space law, original gate or source input changes in
production. Evidence: `late-flow-audit-v1/`.

A private call-local remap-plan optimization now passes the clean 88-program
output/exit comparison, 6,182 Release and fresh ASan/UBSan controls, all 8,503
original trial records and 696 complete fresh owner images. Under WHOLE, seven
alternating hot-replay pairs are faster in every pair: median steady advance
ratio 1.04657× (range 1.02567–1.05456×), with median times 1,508.38→1,442.00 ms
per 0.2 simulated seconds. First advances improve 1.03965×. Peak RSS ranges
overlap. This repeats the original saved parcel; it is not the complete live
trajectory or actual game speed.

The separate three-pair fresh default-OFF cohort retains every original owner,
status and independently captured future-state oracle. Its steady advance
ratios are base cold/menu 0.96072×/0.98335× and DLC 0.98127×/1.10831×, with wide
paired ranges. That small sample does not establish fresh nonregression or a
consistent benefit. Adoption remains pending further cost review and target
qualification. No arithmetic, checkpoint, timestep or acceptance bound changes
are proposed. Private evidence: `late-flow-audit-v1/all88-86e5eb3/current-replay/`
under `whole-v1/` and `whole-fresh-v1/`.

A broader private 300-frame DLC functional probe covers 252 managed lifecycle
and UI rebuild methods, with no scope faults or exceptions. Its 499 warm render
calls put PropertyTextures.LateUpdate near 3.03 ms and text rebuilds near
0.62 ms. Other project work shares the CPU, so these are leads rather than
exclusive timing acceptance. Evidence: `unity-profile-v3/functional-analysis.json`.

Compact pressure and hazard texture jobs now compute their own channel bytes
without also calculating discarded colour values. The existing full scalar
sample remains the byte oracle, and the private destination proof still checks
it against the independent rich sample and actual CPU writes. Both compact
cache modes pass 881,866 .NET assertions; the original 12,000-case scalar/rich
loop passes 879,372 assertions under actual game Mono, including 10,412 valid
inputs, 4,764 model rejections and five opacity ranges. Both target DLLs are
byte-identical and their actual game-Mono dependency checks pass 2,589/0
(Windows Mono runs on Wine, not Windows OS). The frozen DLC proof passes all
6,144 actual CPU destination comparisons, exercising forced full fallback with
zero mismatches. No whole-game speedup, GPU readback, new solver arithmetic or
checkpoint change is claimed. Evidence: `compact-channels-v1/`.

An isolated actual-game-Mono method benchmark adds seven alternating pairs per
channel, after warmup, with equal checksums and zero Unity clock calls. Median
method speedups are 2.32877× for opacity and 7.21132× for hazard (paired ranges
2.28803–2.35394× and 7.14449–7.26947×). Each sample makes 1,048,576 calls over
2,048 valid scalars. These measure the channel methods; they do not establish
texture-job or whole-game speedups. Evidence: `mono-bench-whole-summary.json`
in that same private directory.

The private engine-phase probe then brackets 139 original Unity loop nodes
with timestamp callbacks, verifying their original order, delegates, native
function pointers and conditions before and after installation. Fourteen
synthetic structure/mutation controls pass; actual Mono resolves 2,600/0
dependencies. The repeated shared-CPU 300-frame DLC diagnostic completes with
zero timing-scope faults. Its 500 warm loops concentrate measured wall time in
script Update/LateUpdate (about 19.00/5.04 ms); frame rendering is about 1.77 ms
and canvas updates 1.05 ms. These are functional diagnostic observations,
including waits, not exclusive acceptance timings or an additive partition.
Evidence: `unity-loop-profile-v2/functional-analysis.json`.

The first phase run remains failed: during world generation, before the phase
callbacks installed, concurrent report writers collided on the shared JSON
temporary file. The production report writer now serializes complete writes
and object serialization through one process-local gate. The unchanged writer
reproduces the collision; eight .NET I/O controls and twelve actual-Mono
I/O/serialization controls pass after repair, including readers, cleanup and
exception recovery. The same 300-frame game then passes without the report
error. The build driver includes the permanent regression. This coordinates
writers within one process; it does not certify cross-process writers or actual
Windows OS behavior. A later actual Windows-game-Mono test under Wine fails
the concurrent-reader phase: `File.Replace` briefly leaves the destination
missing. Private MoveFileEx and FileRenameInfoEx alternatives also fail there
(open-reader access denial / unsupported operation); neither is adopted.
Linux I/O and serialization controls remain passing. Retain this Windows reader
gate as open, separately from the repaired concurrent-writer collision.
Evidence: `report-json-fix-v1/{qualification,windows-mono-qualification}.json`.

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
| Fresh Spaced Out! replay, frames 4–43 | 583 / 707 ms | `advance()` plus `wait()` through owner completion |
| Uploaded colony's twelve-dig replay, frames 505–584 | 860 / 932 ms | Same complete owner observer |

The replays were contended. On those measured conditions, 66.7 ms would
require about 8.7×/12.9× lower mean cost; this is not a hardware conversion or
prediction for the Windows PC. Terra does not certify Spaced Out! or a
developed base. Profile8's earlier 23.7% improvement is already in this baseline.

### Linux ultra probe after the first implementation

A frozen private profile8 package with the transfer optimization and new
diagnostics completes the fresh base-game Terra validation at frame 450 without
an owner fault or mod-log error. It runs on the 5900X/NVIDIA Linux host with
12 workers, ultra selected and the whole-machine CPU lock. Startup frames 1–150
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

The bounded report (frames 0–450) retains 90.000001 owner seconds and
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
workers helps. Giving it 8 or 12 inner workers is worse than 4 and raises summed
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
Warmed frames 151–450 reach 2.7469× owner / 2.7470× game progress, below the
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

A separate publication-logging diagnostic ends at actual owner frame 903. Across
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
flags29/tiers, unset pump/credit flags (both default on), workers 12 and RTX 5090
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

The private native 32 BASE900 probe also passes at 2.99681× warmed game
progress (150.021065 / 50.060256 s), against production's 2.99675×. Individual
windows are 2.999/3.000/2.979/3.005/3.001×. Native owner means fall to
25.4–28.1 ms from 27.9–29.2; the game-clock result is unchanged within this
ordered comparison. Separate clean-window p99 values are
31.34/103.15/102.39 ms and GC counts 1/3/3, versus production's
34.44/102.96/106.71 ms and 1/2/4. These are per-window observations, not an
aggregate percentile or causal smoothness gain. No owner fault or dropped
timing samples occur. A fresh final receipt-pending count is not recorded by
this overlay/result, so pump counters do not establish an empty queue.

The native 32 DLC run averages about 95.67 ms wall per 0.2 s owner frame;
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

## Full-colony benchmarks

The long-run benchmarks above play a small three-duplicant colony. To ask
whether 3x holds on a full colony, two harness scenarios build one on the
pinned seed (147147, story traits V9, no mixing) in a single deterministic
build, then let it run:

- `full-colony-base` (base game, Terra): three stacked halls and a lower one
  joined by ladder shafts, tunnels into the neighbouring biomes, four sealed
  vaults (a hot steam vent with a pump and steam turbine, a natural gas vent
  feeding a natural gas generator, a 235 K carbon dioxide room with
  wheezeworts, and two stirred pools), piped and powered machines (oxygen,
  water, food, power, refining, filters, valves, reservoirs), 18
  duplicants, plants and critters.
- `full-colony-spacedout` (Spaced Out!): the same base on the starting
  asteroid with 14 duplicants, and a compact one (no upper hall or vaults,
  6 more duplicants) on a second asteroid.

The build is harness-only: buildings appear instantly and machines get an
initial stock, but nothing is invincible, free or refilled afterwards.
`tools/full_colony_benchmark.py` runs it: `build` plays the scenario and
keeps its save, `settle` loads that save and plays at least three cycles at
speed 3 with the ordinary cycle autosaves, and `publish` copies the settled
save to the private `local/saves/<name>.sav` with `<name>.json` beside it
(SHA-256, cycle, duplicant, cell, building and conduit counts, revision and
the run's measured windows). Run `build` and `settle` through
`tools/game_slot.sh`. The windows are diagnostics of one run on a shared
machine, not acceptance numbers; timings belong to a locked run.

First base run (2026-11-04, 102a6d9 with the scenarios uncommitted): the
build ran at 1.5x; the settle (frames 422-10,023, cycles 1-4) averaged
0.55x, starting near 1.4x and falling to 0.4-0.6x. The owner's
`transfers` stage grew from 30 to 300 ms a frame with the held precision
inventory (sub-ulp gas admissions, `oni_precision_inventory.hpp`): 1,368
parcels after the build, 12,079 at the cycle 2 autosave and 16,158 at
cycle 4, from 14 sources. Each frame retries every held parcel.

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

