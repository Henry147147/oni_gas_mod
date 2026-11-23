# Speed study on the November 22 colony (2026-11-23)

The user asked for in-depth testing of ways to make 1.13.0 faster, with the
accuracy each way gives up and the speed it buys, and for 3× game speed on
their save. They also asked whether simulating each enclosed space on its own
thread would help. This note records what was measured, what was built, what
was tried and dropped, and what would come next.

A survey of published techniques read against this solver, requested by the
user the same day, is in
[fluid-optimization-research.md](fluid-optimization-research.md): it ranks
what the literature offers (a pressure multigrid that is robust to the
gas/liquid jump, a cheaper cycle, deflation, a direct solve as a yardstick,
owner-computes subdomains) and is research only.

## How it was measured

Everything is a game-free replay of the private capture
`local/perf/nov22-session` (the colony the user played on 2026-11-22, owner
frames 210820 to 211269, 12 workers; a second capture, `nov21-late-window`,
checks that a result is not peculiar to one save). `ma_colony_benchmark`
replays the journaled inputs through the whole owner frame and reports the
owner tick, its stages and the regions' costs.

- `--set NAME=VALUE` changes a saved solver option after the first replayed
  frame, through the same call the settings window uses.
- `--views-dump PATH` writes the final frame's cell views;
  `local/perf-tools/compare_views.py` then says how far two final states are
  apart after the 450 replayed ticks (90 game seconds): gas pressure
  (relative, mean / 99th percentile / largest), temperature (K), the share of
  the liquid volume that sits in other cells, and the number of cells that
  show another element.
- `local/perf-tools/sweep.sh CAPTURE TAG [--set ...]` runs one replay under
  the timing lock and prints both.

The speed figure is the replay's estimate, `200 ms / (owner tick + 25 ms)`:
the 25 ms are the part of the game's own native frame a tick waits for. 3×
needs an owner tick of 41.7 ms.

**Deviation is not error.** The flow is chaotic in places (liquid sloshing,
convection): two runs that differ only in the pressure tolerance (10⁻⁶
against 5·10⁻⁹) end 90 s later with gas pressures 2·10⁻⁶ apart on average
and 0.17 % at most, temperatures 0.008 K apart on average and 5.5 K at most,
and 22 of 250,000 cells showing another element. That is the floor every
figure below should be read against. Mass and energy stay closed in every
run (10⁻¹² and 10⁻¹⁴ relative).

The user's colony already runs with the pressure tolerance of the Fast
preset (10⁻⁶); every other solver option is the default.

## Where the tick goes

Owner tick 152 ms on the quiet test host (1.13.0, all settings as saved):

| Part | ms |
| --- | ---: |
| The regions' flow, run side by side (`regional_run`) | 90 |
| Flow stage outside it: sparse and dense conversion, capture, merge, rebuild, trace sink, clocks | 34 |
| Transfers, heat edits, space sink, projection and ledger, terrain | 28 |

The regional run is as long as its slowest region. Three regions matter:

| Region | Cells | ms a tick | Flow intervals a tick | What it is |
| --- | ---: | ---: | ---: | --- |
| (82,41) | 12,374 | 81 | 2.0 | The caves and shafts around the base, one connected space; 6,073 of its cells hold some liquid, 896 at least half a cell |
| (376,121) | 7,091 | 60 | 7.6 | The surface: 3,800 cells of gas below 1 kPa over a few dozen cells of water film that boil into near-vacuum |
| (144,178) | 5,722 | 30 | 2.5 | A second cave system |

All regions together are 219 ms of work a tick; 294 regions are stepped. The
main region's 81 ms are pressure 30 (two solves of about 26 multigrid
iterations, 0.47 ms each), transport 12.5, surface sharpening 11, layering 8,
trace pruning 3.4, heat 3.3, phase change 3.2 and audits, readings and
entropy 9. One flow interval costs it about 21 ms and the work done once a
tick about 38 ms.

**A region does not get faster with more workers.** The main region takes
106 ms with two workers of its own, 92 with four (the present limit) and 93
with eight: its stages are loops over a few thousand cells each, too short
for a parallel pass to pay, with serial parts between them. A CPU profile
shows the same from the other side: while the owner waits for the regions,
2.7 of 12 workers are busy on average.

## What was built: the same results, sooner (1.13.1)

Twelve changes to the work the owner's thread does around the regions' flow.
None changes a result: every replay below ends on the fingerprint it had,
with the fluid on every tick and on 1 tick in 4 and in 8, all 62 solver test
and diagnostic outputs equal 1.13.0's, and the twelve fixed regression cases
match.

- Whole-grid reading buffers (25 MB each) are kept between frames instead of
  allocated and zeroed every tick; the trace sink takes its cells as a mask.
- Heat edits journal energies only, read their cells on the workers, and a
  refused batch says which edit failed (about 25 cooling edits a tick are
  refused on this colony, and finding them by bisection ran about 70 failed
  batches a tick).
- The space sink's scan, the carried germs' scan, the activity clocks' checks
  and steps, the regional tick's clock checks, merge and list rebuild, the
  projection's join and the copies for the next prepared world run on the
  workers in fixed blocks.
- A mixture's mass sum skips its zero entries (the sum is the same bit for
  bit), germ edits are staged by cell slot instead of in a map, and the
  replaced publication is freed after the lock readers take.

On a tick that advances no flow (the reduced fluid rates) the owner's work
falls from 40 to 22 ms; outside the regional run of a full tick from 62 to
47 ms.

## Speed and accuracy, option by option

Owner tick on the quiet test host, 300 measured ticks, each configuration
run twice in turn with the others (the two runs agree within 2 ms). "Fast
settings" are two existing Advanced solver settings: **Trace removal
threshold 1 mg** (default 0.01 µg) and **Flow parts per step 1** (default
2). The fluid rates are the existing **Fluid update rate** setting.

| Configuration | 1.13.0 | 1.13.1 | Estimated speed | Deviation after 90 s (pressure mean / 99th pct; temperature mean / 99th pct / largest) |
| --- | ---: | ---: | ---: | --- |
| As saved: fluid every tick | 151 ms | **137 ms** | 1.13× → 1.24× | none: identical results |
| Fast settings | | **90 ms** | 1.74× | 0.02 % / 0.2 %; 0.13 K / 1.9 K / 60 K; 6,957 near-vacuum cells become vacuum |
| Fluid 1 tick in 4 | 67 ms | **54 ms** | 2.19× → 2.55× | 0.19 % / 0.8 %; 2.5 K / 65 K / 133 K |
| Fluid 1 tick in 8 | 51 ms | **38.5 ms** | 2.64× → **3.15×** | 0.28 % / 1.6 %; 2.8 K / 83 K / 3,320 K |
| Fast settings, fluid 1 in 4 | | **40 ms** | **3.09×** | 0.20 % / 0.8 %; 0.8 K / 17 K / 176 K |
| Fast settings, fluid 1 in 8 | | **32 ms** | 3.51× | 0.28 % / 1.7 %; 1.2 K / 28 K / 263 K |
| Fluid 1 tick in 16 | | **30 ms** | 3.62× | 0.33 % / 2.4 %; 3.7 K / 90 K / 264 K |

What each option gives up:

- **Trace removal threshold, 1 mg.** A gas or liquid of which a cell holds
  less than a milligram leaves through the booked trace sink: about 0.08 Pa
  of oxygen, 0.14 Pa of steam or 1.2 Pa of hydrogen in a cubic metre. Rooms
  are untouched; near-vacuum becomes vacuum, the thin edge of a gas cloud is
  eaten, and matter leaves the colony at that rate (it is booked, not lost
  from the accounts). On this colony it removes the plume of water vapour
  that a few dozen cells of water film on the surface boil into near-vacuum:
  the surface region goes from 60 ms and 7.6 flow intervals a tick to less
  than 9 ms (it leaves the three costliest regions), and the liquid traces
  that half the cave cells carry go too (the main region falls from 81 to
  69 ms). The
  largest single setting: 20 % of the tick alone. 1 mg is the ceiling the
  user authorized earlier; intermediate values (10⁻⁹ to 10⁻⁷ kg) gave part
  of the effect in runs on a busy host and were not measured again.
- **Flow parts per step, 1.** A region's pressure is solved and its fluid
  moved once in a 0.2 s tick instead of twice. A region that needs more
  intervals (fast flow) still takes them. Fast transients are resolved half
  as finely: 0.2 K of mean temperature difference after 90 s, 7 K at the
  99th percentile. 9 % of the tick alone.
- **Fluid update rate, 1 tick in 4, 8 or 16.** The existing setting. Tile
  fluid movement, heat exchange between fluids and phase change run at a
  quarter, an eighth or a sixteenth of game speed, while duplicants, pumps
  and machines keep their clocks. Gas spreads that much more slowly, a
  machine's heat and output gather in its cell between updates (the 3,320 K
  cell above), and a pump can empty its intake tile. It is the only option
  that changes how the colony plays, and the only one that reaches 3×
  today.
- **Surface sharpening off, or films below 5 % of a cell left unsharpened**
  (existing settings): 18 % and 11 % in runs on a busy host; liquid surfaces
  smear over neighbouring cells. Not part of the fast settings: the trace
  threshold removes most of the same cost without touching pools.
- **Pressure tolerance**: nothing to gain (below).

The second capture (the November 21 colony's late window, same colony a day
earlier) agrees except where the surface plume was the gain:

| Configuration, November 21 capture | 1.13.0 | 1.13.1 | Estimated speed |
| --- | ---: | ---: | ---: |
| As saved: fluid every tick | 134 ms | 122.5 ms | 1.26× → 1.36× |
| Fast settings | | 104 ms | 1.55× |
| Fluid 1 tick in 8 | | 37 ms | 3.20× |
| Fast settings, fluid 1 in 4 | | 45 ms | 2.86× |
| Fast settings, fluid 1 in 8 | | 34 ms | 3.39× |

**Routes to 3× on this save with 1.13.1** (estimates from the replay, to be
confirmed in the game on the user's PC):

1. Fluid 1 tick in 8, everything else as saved: 3.15×.
2. Fast settings and fluid 1 tick in 4: 3.09×, with fluids twice as fast as
   in route 1 and smaller temperature deviations (2.86× on the November 21
   capture, where there was less near-vacuum gas to remove).

Without slowing the fluid, 1.13.1 reaches 1.24× as saved and 1.74× with the
fast settings. 2× and more at the full fluid rate needs the regions
themselves to get cheaper, which is the next section.


## A gentler rate, and larger steps in game time

Follow-up to the user's questions on the table above (can the reduced rate
be more accurate, or take a larger step instead). Both were prototyped on
1.13.1 behind switches (`artifacts/v1.13.1/study/rate2-and-true-time-step-prototype.patch`;
not in any release) and measured while the acceptance run shared the host,
so each figure may be about 7 % high.

What a reduced fluid rate does is skip fluid time: on 1 tick in N the fluid
advances 0.2 s while the game advances N × 0.2 s. The honest measure of an
option is therefore how much *fluid* time it simulates per real second, next
to the game speed:

| Configuration | Owner tick | Game speed | Fluid seconds per real second |
| --- | ---: | ---: | ---: |
| 1.13.1 as saved | 137 ms | 1.24× | 1.24 |
| Fast settings | 90 ms | 1.74× | 1.74 |
| Fast settings, a full 0.4 s fluid step every 2 ticks (prototype) | 74 ms | 2.02× | 2.02 |
| Fast settings, fluid 1 tick in 2 (prototype: the solver accepts 4, 8, 16) | 58 ms | 2.40× | 1.20 |
| Fast settings, fluid 1 in 4 | 40 ms | 3.09× | 0.77 |
| Fluid 1 in 8 | 38.5 ms | 3.15× | 0.39 |

- **1 tick in 2** would be a small change (one more choice in the existing
  setting): 2.4× with the fluid at half speed, which is about as much fluid
  per real second as the user's colony gets today at 1.24×. Without the fast
  settings it gives 83 ms, 1.85×.
- **A larger step in true game time** (the fluid advances the whole elapsed
  time every N ticks, so nothing is slowed): every 2 ticks it gives 74 ms
  with the fast settings (2.02×) and 111 ms without (1.48×). Every 4 or 5
  ticks gives nothing more (73 and 72 ms) and the tick that does the step
  takes 265 to 410 ms, a visible hitch. The reason is that the solver needs
  its flow intervals per game second whatever the cadence (a region with
  liquid keeps 0.1 s intervals, a fast flow more), so a longer step only
  saves the work done once per update. Temperatures differ from the
  every-tick run by 0.7 K on average and 41 K at the 99th percentile after
  90 s (fast settings alone: 0.13 K and 1.9 K): machines still put their
  heat and gas into single cells every tick and the flow now spreads them
  every 0.4 s. A product version would also need the activity clocks of
  regions with duplicants to carry pending time, which the saved clocks do
  not allow today.

### Larger steps, tested further

The user ruled out slowing the fluid and asked for larger steps tested in
its place. Two prototypes, both keeping the fluid at game time
(`artifacts/v1.13.1/study/larger-step-prototypes.patch`):

- **A, per region:** a region with duplicants or recent activity steps
  every N ticks by N ticks' time.
- **B, whole colony:** the existing held and sampled ticks of the fluid
  update rate, but a sampled tick advances every awake room by the N ticks
  elapsed instead of one. The ticks between do no fluid work at all.

Each was run with the solver's present rule for long steps (a region with
liquid keeps intervals of one tick) and with the swept-volume bound left to
size the intervals ("sized"). Fast settings throughout unless noted;
measured while the acceptance run used the host, so the tick is given
relative to the fast settings alone in the same batch. "Real tiles" are the
tiles that hold at least 100 g in both runs: the earlier whole-grid figures
were dominated by near-vacuum cells.

| Step | Prototype | Tick, of fast settings alone | Est. speed (quiet host) | Longest ticks (95th pct) | Real tiles: temperature mean / 99th pct | Liquid in other cells |
| --- | --- | ---: | ---: | ---: | --- | ---: |
| every tick | fast settings alone | 100 % (90 ms) | 1.74× | 120 ms | 0.15 K / 2.0 K | 0.007 % |
| 2 ticks | A, sized | 77 % | 2.1× | 162 ms | 0.17 K / 2.4 K | 0.04 % |
| 2 ticks | B | 80 % | 2.0× | 171 ms | 0.16 K / 2.1 K | 0.009 % |
| 2 ticks | B, sized | 80 % | 2.0× | 195 ms | 0.16 K / 2.3 K | 0.01 % |
| 3 ticks | A | 82 % | 2.0× | 223 ms | 0.17 K / 2.3 K | 0.009 % |
| 3 ticks | A, sized | 71 % | 2.2× | 204 ms | 0.22 K / 3.8 K | 0.15 % |
| 4 ticks | A, sized | 73 % | 2.2× | 275 ms | 0.29 K / 4.9 K | 0.4 % |
| 4 ticks | B | 82 % | 2.0× | 304 ms | 0.18 K / 2.6 K | 0.01 % |
| 5 ticks | A, sized | 79 % | 2.0× | 347 ms | 0.43 K / 7.0 K | 0.7 % |
| 8 ticks | B, sized | 69 % | 2.2× | 413 ms | 0.56 K / 8.6 K | 1.0 % |
| 2 ticks, saved settings (no fast) | B | 83 % of 137 ms | 1.45× | 291 ms | 0.05 K / 0.8 K | 0.003 % |
| 2 ticks, regions on alternate ticks | A | 98 % | 1.7× | 165 ms | 0.16 K / 2.1 K | 0.009 % |

What this shows:

- **A step every two ticks is the useful one**: about a fifth off the tick
  (2.0× to 2.1× with the fast settings, 1.45× without), and on the tiles
  that hold real mass the result is as close to the every-tick run as the
  fast settings alone are. The cost is unevenness: ticks alternate between
  about 25 and about 125 ms.
- **Longer steps do not pay.** At 3 to 8 ticks the average tick stays
  between 69 % and 82 % while the long tick grows to 0.3 or 0.4 s (a
  visible stall), liquid starts to end up in other cells, and the surface
  plume returns between trace removals. A region's update costs almost in
  proportion to the time it covers: its flow intervals are set by how fast
  the fluid moves, and the stages that run once an update (layering, the
  liquid exchange, phase change) repeat for each tick's length of it. Only
  the audits and bookkeeping are saved.
- **Letting the solver size its intervals** is worth a few percent at 3 and
  4 ticks and costs liquid accuracy; with the present rule liquid stays
  where the every-tick run puts it.
- **Putting regions on alternate ticks** evens the ticks but loses the
  gain: regions already run side by side, so spreading them over ticks
  makes each tick as long as its largest region again.
- B is the form to build if this becomes a setting: it reuses the tested
  held and sampled ticks, needs no change to the saved activity clocks, and
  measures the same as A.

So larger steps buy about 2× on this colony with the fluid at full speed,
and no cadence buys more. Beyond that the regions themselves must get
cheaper.

So with what exists or is nearly free, game speed above about 2× is bought
by simulating less fluid time. 3× with the fluid at full speed needs the
regions themselves to cost a third of what they do, which is the next
section.

## Simulating each enclosed space separately

**This is already how the solver works.** Every space enclosed by solid
tiles (a room behind closed doors, a sealed pocket, a pipe-fed tank room) is
its own region with its own time steps and pressure solves, and the regions
of a tick run side by side on the workers: 294 of them on this colony. A
door that opens joins two regions and one that closes splits them again.
The rooms of the user's base are such regions already and cost little.

What is left is the opposite case: one connected space that is large. The
main region is not a room but the cave and shaft network around the base.
Cutting it at one-tile openings (doorways, shafts, crawl spaces) changes
nothing: of its 12,374 cells 12,041 stay in one piece, because the caves are
joined by wider openings and by more than one path.

It can be cut, though, at places that are merely narrow. Recursive straight
cuts that each sever as few open tile faces as possible
(`artifacts/v1.13.0/perf2/cuts.py`) give:

| Region | Pieces | Largest piece | Open faces cut | Of them at visible liquid |
| --- | ---: | ---: | ---: | ---: |
| Main (12,374 cells), 3 levels | 11 | 3,027 (24 %) | 165 | 20 |
| Main, 4 levels | 21 | 1,589 (13 %) | 277 | 31 |
| Surface (7,091), 3 levels | 8 | 1,528 (22 %) | 269 | 0 |
| Second caves (5,722), 3 levels | 10 | 1,314 (23 %) | 61 | 1 |

So about one face in a hundred would lie on a cut. If each piece ran as its
own region, the regional run would no longer be bounded by the 81 ms of the
largest region but by the 219 ms of total work spread over twelve workers:
about 20 to 30 ms, a saving of about 60 ms a tick, the largest single lever
found in this study. It is an estimate from the measured per-region costs,
not a measurement: nothing was built.

What it would take, and what it would cost in accuracy:

- **The pieces must exchange fluid across the cut faces**, since the space is
  really open there. Solved separately, each piece sees a cut face as a wall
  during its step; an exchange step then moves gas, heat and germs across
  each cut face. The simple exchange (equalise the two cells on either side,
  as ONI's own simulation does everywhere) is stable and conserves
  everything, but pressure then crosses a cut one cell-exchange a tick: a
  strong source on one side (1 kg/s of oxygen through a four-face cut)
  would hold several kPa more on its side than the open space really would,
  and momentum (a draught through the opening) is lost at the cut. A better
  exchange solves the cut faces' pressures together (a small coupled system
  over a few hundred faces with each piece's solve as its building block);
  that keeps pressures right to the solver's tolerance but is a new solver
  component.
- **Liquid must not be cut**, or needs its own exchange rule with gravity.
  Twenty to thirty of the cut faces above touch visible liquid; the cuts
  would have to avoid them or the pieces be joined again while liquid
  crosses.
- **The cuts change with the map** (digging, building) and with where the
  liquid is, so pieces are re-cut as the colony changes, without losing
  their state. The regions' machinery (boxes, per-region clocks, audits per
  closed-face area) carries most of this; activity clocks stay per enclosed
  space.

This is the recommended next piece of solver work for large open colonies.
It is a project of its own with new gates (two rooms through a cut must
equalise as the uncut room does within a stated bound; mass, energy and
germs closed across cuts; cuts moving under a dig), not a change to slip
into a point release.

### The cut prototype, measured

After the user ruled out the slower fluid and the step every N ticks, the
cut was built as a prototype behind switches
(`artifacts/v1.13.1/study/cut-regions-prototype.patch`, in
`regional_tick.hpp` only; not in any release). A wall-separated region of
more than twice the target size is cut by the straight cuts above into
pieces; each piece is stepped as a region of its own with the cut faces as
walls; after the step gas crosses the cut faces in an exchange step, and
liquid above a horizontal cut falls into the piece below. Mass and energy
close to the solver's audits (the exchange carries each gas with its
enthalpy, pays the lift, and settles the change in the neighbouring faces'
kinetic energy; momentum left on a face when it becomes a cut turns into
heat and is booked as a wall's).

Speed, fast settings, measured while the 1.13.1 acceptance run shared the
host (the fast settings alone read 98 to 105 ms in the same batches, 90 ms
on the quiet host):

| Pieces of about | Regions' flow (`regional_run`) | Owner tick | Of the fast settings alone |
| --- | ---: | ---: | ---: |
| uncut | 53 to 57 ms | 98 to 105 ms | 100 % |
| 3,000 cells | 23.5 ms | 71 ms | 72 % |
| 1,600 cells | 16 to 21 ms | 65 to 77 ms | 62 to 78 % |
| 800 cells | 12 to 17 ms | 57 to 80 ms | 58 to 76 % |

With the saved settings the tick went from 162 to 84 ms in the same batch
(regions 108 to 26 ms). The regions' flow falls as estimated; the spread in
the tick is the busy host and the exchange's own cost, which is not yet
optimised (a hash map and an equation-of-state read per transfer). What is
left of the tick is then almost all owner-thread work: about 44 ms.

Accuracy depends entirely on the law by which gas crosses a cut. Deviation
from the uncut run after 90 s, fast settings, tiles holding at least 100 g
(the fast settings alone: pressure 0.011 % on average, temperature 0.15 K
on average and 2.0 K at the 99th percentile):

| Exchange law | Pressure, mean / 99th pct | Temperature, mean / 99th pct | What goes wrong |
| --- | --- | --- | --- |
| Pieces sealed (no exchange) | 0.7 % / 4 % | 0.5 K / 4 K | rooms drift apart |
| The two cells at each cut face equalised (carrying internal energy, faces beside a film closed) | 0.67 % / 3.2 % | 0.64 K / 7.5 K | a 248 K and a 308 K cell at a cut; pieces joined only by a wet shaft sealed off, 2 kPa steps |
| The same, carrying enthalpy, wet faces open | 0.49 % / 2.1 % | 0.45 K / 4.6 K | the cut faces match (9.9 Pa mean step against 9.4 uncut), but a one-cell shaft passes only what levels two cells a tick: a chamber fed from below ran 2 % above and the one above it 1.7 % below their uncut pressures |
| All pieces of a region levelled at once from the two cells at each cut | worse (1 % / 29 %) | 1.0 K / 9.5 K | the cells at the cut are disturbed by the last transfer, so the law chases its own spikes; pieces needed 6 intervals a tick |
| **All pieces levelled at once from each piece's mean pressure carried to the cut's height**, at most 20 % of a donor cell a tick, half relaxed | **0.044 % / 0.9 %** (0.041 % / 0.35 % with 1,600-cell pieces) | **0.32 K / 3.9 K** (0.28 K / 3.3 K) | nothing visible in these figures |

So the last law keeps rooms level through narrow passages, and with it the
cut costs about as much accuracy again as the fast settings do. The
prototype was then given the two things a colony cannot do without: germs
cross with the phase that carries them, and liquid levels across a vertical
cut (half the difference a tick) as well as falling across a horizontal one.
With both, 1,600-cell pieces and the fast settings: 73 ms against 106 ms for
the fast settings alone in the same batch (69 %; about 62 ms and 2.3× on a
quiet host by that ratio), pressure 0.040 % and temperature 0.28 K on
average, liquid placed as the fast settings place it. What it still leaves
out, each of which a real version needs:

- **Momentum does not cross**: a draught through an opening is stopped at a
  cut and restarted by the pressure it builds.
- **Heat crosses only with the gas**, not by conduction through the cut
  face.
- A face force (a fan) on a cut face is ignored.
- The pieces are re-cut from scratch when the map changes.

The second opinion asked for (Codex, `artifacts/v1.13.1/study/codex/answer1.txt`)
argues for the exact form instead: keep one pressure system for the whole
region, use the pieces as a two-level preconditioner for it, and split each
other stage (transport, layering, sharpening, heat) over the pieces inside
one tick, so that every face keeps one owner and the discrete physics is
the uncut solver's. That removes the list above by construction and is the
better end state; it is also a rewrite of the tick's stages rather than of
the region bookkeeping. The prototype answers what the simple form buys
(regions 53 → 16 ms) and what it costs (the table), which is the case for
or against doing either.

### Cuts with walls do not carry convection (the probe)

On 2026-11-23 the user put release preparation on hold until the speed
target is met, so the prototype was measured again without the acceptance
run beside it and then tested for what the 90-second replay comparison
cannot show: slow exchange across a cut.

Clean timings, November 22 colony, 12 workers (two rounds; "bookkeeping
commits" are the six exact commits listed under the next heading):

| Configuration | Owner tick | Estimate | Regions' flow |
| --- | ---: | ---: | ---: |
| As saved (1.13.1) | 140 to 151 ms | 1.14 to 1.21× | 94 to 101 ms |
| Fast settings | 91.7 ms | 1.71× | 50 ms |
| Fast settings, 1,600-cell pieces | 65 to 66 ms | 2.2× | 15 to 16 ms |
| Fast settings, 800-cell pieces | 63.5 to 66 ms | 2.2× | 12 to 13 ms |
| Saved settings, 1,600-cell pieces | 85 ms | 1.8× | 26 ms |
| Fast settings with the bookkeeping commits | 83.1 ms | 1.85× | 49 ms |
| The same with 1,600-cell pieces | 53.6 ms | 2.54× | 15.4 ms |
| The same, largest regions stepped first | 51.2 to 51.5 ms | 2.62× | 13.6 ms |

The result does not depend on the number of workers (6 and 12 give the same
state), and the prototype ran the other captures (`industrial-32`,
`spacedout-full`, `published-nov19` and the surface captures) without a
fault.

The probe (`native/tests/oni_cut_probe.hpp` in the prototype patch, run as
`ma_oni_colony_tests --cut-probe`) is a sealed room 80 cells wide and 20
high, oxygen at 350 K in its left half and at 290 K in its right half, cut
into 400-cell pieces. Mean temperature of the two halves:

| After | Uncut | Cut (the law above) | Cuts alternating between two sets of lines, momentum kept on cut faces | The same, the kept momentum carrying gas |
| --- | --- | --- | --- | --- |
| 10 s | 337.3 / 300.5 K | 348.4 / 291.3 K | 346.5 / 292.8 K | 346.9 / 292.4 K |
| 50 s | 312.4 / 323.3 K | 348.3 / 291.4 K | 338.7 / 299.3 K | 342.1 / 296.2 K |
| 100 s | 318.9 / 316.6 K | 348.3 / 291.4 K | 334.9 / 302.1 K | 339.5 / 298.2 K |
| 600 s | 317.7 / 317.7 K | 348.3 / 291.4 K | 322.7 / 311.6 K | 333.7 / 302.6 K |

Uncut, the hot air runs over the cold as a current and the room is mixed in
about 100 s. With the cut it never mixes: the two halves level their
pressure once and then stand side by side at 348 K and 291 K for good, an
invisible wall for heat. A player would see that. The cause is not the
missing conduction but the wall: a piece's pressure solve brings the flow
toward a cut to rest within one tick (sound crosses a piece many times in a
tick), so no current can run through a cut line, and the exchange law only
answers a difference of the pieces' mean pressures.

Two repairs were tried in the prototype
(`artifacts/v1.13.1/study/cut-regions-prototype-v2-alternate-keep.patch`):
a second set of pieces cut along other lines that steps every other
interval, so that every face is an ordinary face half the time
(`MA_X_CUT_ALTERNATE`), and momentum kept on a cut face while it is a wall
(`MA_X_CUT_KEEP`, with its kinetic energy settled as the cells' masses
change). They let heat cross, about ten times too slowly (the last two
columns), because every other interval still stops the current inside each
piece; they cost about 5 ms of the tick (57 against 51.5 ms) and change the
90-second figures little (pressure 0.048 % against 0.040 %).

The conclusion: a cut that is a wall to the pressure solve cannot be made
faithful by an exchange step. Coupling the pieces' momentum explicitly
across the cut is unstable at this time step (sound crosses 68 cells a
tick, which is why the pressure solve is implicit in the first place). What
remains sound is the second opinion's form: **one pressure solve for the
whole region and every other stage split over workers inside the one
tick**, with identical results. That route is being built now: a faster
pressure solve for the large region (multigrid convergence, a direct solve)
and stage-by-stage parallelism in `Tick`. The cut prototype stays as the
measure of what such a split can buy (regions 49 → 14 ms).

### Bookkeeping around the regions, second round

Six further commits with identical results (fingerprints of the saved,
fast, every-eighth-tick and November 21 replays unchanged), 83.1 ms against
92.6 ms with the fast settings over three interleaved rounds:

| Change | Measured |
| --- | --- |
| The trace sink after the flow works on the traced cells, their faces and the germ cells alone, in place when nothing shares the world | 6.8 → 1.3 ms on its frames |
| A reaper thread frees the replaced publication and the world before the flow | 4 ms of the time between ticks, 2 ms of the tick |
| Activity regions are refilled only around changed walls | 4.9 → 2.7 ms on frames that dig or build |
| `dense()` keeps a stepped cell that the rebuild would give back bit for bit (48 % of cells, 16 % of chunks) | 0.5 ms, and 0.4 ms of projection |
| Germs whose carrier left their cell are relabelled in place | 0.8 ms on half the frames |
| Heat-edited chunks are copied on the pool when no prepared world is ready (a game that waits on the owner) | 9 → 2.3 ms of the heat stage in that case; nothing in the replay |

And one in the regional tick: the regions of a tick are handed to the
workers largest first (13.6 against 15.4 ms of regions' flow with pieces;
no change where two regions have workers of their own).

## Tried and dropped

| Idea | Result |
| --- | --- |
| Start each pressure solve from the previous solution | No gain: the best of the last two solutions is no closer to the new one than zero (residual 4.24·10¹ against 4.29·10¹ and the like); the pressure increments of successive intervals are unrelated. |
| A looser pressure tolerance (10⁻⁵, 10⁻⁴) | 14 % and 26 % fewer iterations but 2 % of the tick, and liquid pressures get noisy (largest pressure deviation 7 % and 29 %). The solver's residual falls quickly for five iterations and slowly for twenty more whatever the bound. |
| More workers inside a region | No gain, above. |
| Exempting near-vacuum gas from the swept-volume bound (below 1 or 5 kPa) | Nothing at 1 kPa; the surface region halves at 5 kPa, but the trace threshold does better with less risk. |
| Allocator tuning (no trimming, no mmap for large blocks) | No gain. |
| A red-black Gauss-Seidel smoother (earlier, 1.13.0) | 32 % more iterations. |
| Stepping regions with duplicants or machines once a second, in true game time | 1.75× to 2× by estimate, but heat and gas that machines put into single cells then pile up for a second before the flow spreads them: one cell reached 9,570 K. Not usable without spreading sources first. |
| A longer boiling and condensation time (10 s) | No gain. |
