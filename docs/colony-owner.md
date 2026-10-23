# Colony owner

The colony owner is the normal-game path: one `MacClosedWorld` owns every gas
and liquid on the asteroid, with one species per catalogue material
([material catalogue](material-catalogue.md)). It replaces the bounded
development rooms of the [configured domain](native-configured-domain.md) for
play.

## Activation and data flow

- **Activation.** With the native backend attached, the mod starts the owner on
  a new base-game colony (not a loaded save; no Spaced Out!; exactly one
  asteroid) at the first completed native frame. It builds the catalogue from
  the game's element table, reads every cell of the asteroid from that frame
  (solids are walls; vacuum in the Space zone is open sky) and calls
  `ma_oni_colony_start` (`native/include/mixed_atmospheres/oni_colony.h`). A
  fluid cell holding an element outside the catalogue keeps native physics.
- **Frames.** Each positive native frame hands its timestep to the owner's
  solver thread after `UpdateData` and returns; the step's result publishes at
  the first data request after it completes. Native pressure, liquid,
  displacement and gas-interaction phases are suppressed for owned cells;
  transitions, sublimation and solid post-processing are suppressed over the
  whole asteroid until their terrain adapters exist. Item contacts with owned
  cells are adiabatic until the heat adapter exists.
- **Publication.** After each data request the mod copies ONI's game data and
  overwrites every owned cell with the owner's projection: the dominant
  constituent of the dominant phase, that phase's mass, temperature. Changed
  identities are announced to ONI's grid caches (`Grid.Element` follows only
  these announcements, so a cell the owner leaves is announced back with
  ONI's element). A cell a solid has just replaced shows the solid at once.
  ONI sees temperatures within 1–9,999 K (a trace's formal temperature can
  lie far outside), and its native code sees a cell under a milligram as
  vacuum. `Grid.Pressure` returns the owner's mechanical pressure. A reading leaves
  out constituents below 1e-300 kg (kernel rounding products that the
  reference EOS cannot evaluate); the cell keeps them. A cell whose reading
  still fails keeps its previous view for the frame.
- **Transactions.** Mass emission, mass consumption (radius), additive cell-mass
  edits and cell heat in the asteroid go to the owner and commit at the start
  of the next native frame, with consumed/emitted callbacks returned through the
  copied game data. Extraction takes up to the requested mass from the open
  cells within the radius, nearest first, in a binary32-exact total; injection
  goes to the cell or the nearest open cell within two. A cell an extraction
  empties to within the total's binary32 quantum gives up all of that
  constituent, and the rest of its phase when that is below the quantum; a cell
  that would keep less than the quantum in all gives that up too. The cell's
  smallest constituents leave first, so no trace is left holding only rounding
  residue of the cell's energy. Taking a wet cell's last gas opens a liquid
  surface. A transfer too small to change its cell in binary64 (a few
  nanograms on a full liquid cell) is dropped with a zero receipt.
- **Heat and terrain edits** a cell cannot take (the reference EOS refuses
  it, as for a liquid trace below binary64's normal range) do not fault the
  owner; the rest of their batch applies. Refused heat goes to an open
  neighbour of its cell, else waits, unbooked and merged per cell, for the
  next frame; a refused terrain edit waits for the next frame.
- **Native bodies** ([below](#native-bodies)): ONI's own heat code and other
  native processes meet a shadow of the owner's cells and their changes are
  booked to the owner; the owner runs element consumers and emitters
  ([below](#element-consumers-and-emitters)).
- **Ledgers.** Every species and U+K+G are reconciled each frame against the
  imported inventory plus every booked flow (injections, extractions, the
  space sink, the trace sink, heat, phase change); drift beyond 1e-8 (mass, relative to the
  species' booked flows, its phase change counted gross) or 1e-6 (energy)
  faults the owner. `MA_COLONY_LEDGER_LOG=1` reports on stderr each frame
  that raises the worst species' mass drift above 1e-12.
- **Open sky.** Whatever reaches a Space cell leaves through the sink, booked.
- **Trace sink** ([base-game floor](base-game-floor.md)). A colony started with
  `MA_ONI_COLONY_GAME_FLOOR` runs the saved game-floor profile: constituents
  below its threshold (1e-11 kg per cell, the smallest passing tested decade)
  leave through the
  solver's audited trace sink at both ends of the flow's interval and after
  the owner's edits that follow it. Their mass per species, U+K+G and momentum are
  booked as their own ledger term (`trace_sink_kg`, `trace_sink_j` in the
  status), apart from the space sink. The game does not select the profile yet.
- **Breathing** (easy mode): a cell is breathable with at least 15% oxygen by
  moles and 15 kPa oxygen partial pressure, carbon dioxide below 0.5% and
  500 Pa; polluted oxygen counts as oxygen. A breathable cell offers its oxygen
  of the more abundant kind.

A faulted owner keeps publishing its last frame (fluids freeze), logs the error
and refuses saves.

## Presentation

After each completed owner frame the mod builds one mixture reading per open
owned cell holding material (`ma_oni_colony_readings`: temperature, pressures,
internal energy, occupied and liquid volume, each constituent's mass and
volume) and hands the frame to the mixture presentation runtime. The readings
come from the projection's EOS evaluation on the solver thread; the game
thread only copies them (about 3 ms on Terra), and a worker builds the
presentation frame (about 0.3 s), one at a time, so the presentation can skip
a frame but never holds the game thread. The gas
renderer, hover, overlays and status display then show the real mixture; each
species uses the game's element id, name and substance colour, and breathing
follows the easy-mode rule. A cell whose reading fails validation keeps the
vanilla projection and is counted.

## Native bodies

ONI's tiles, buildings, items (element chunks), pipes and element
consumers/emitters exchange heat and mass with cells inside the native frame.
Before each positive frame's `UpdateData` the backend writes the latest
completed owner cells (element, mass and temperature of the projection) into
both native cell arrays, a shadow. Native code then runs unchanged against it,
at vanilla rates, except that conduction between two owned cells stays the
owner's. After `UpdateData` every change native made to an owned cell becomes a
booked owner input for the next step:

- same element and mass, new temperature: heat `m c ΔT` (ONI's own capacity, so
  the owner gains exactly what the native body lost);
- same element, less mass: an extraction at that cell (from a gas cell, of its
  gas mixture by mass; a cell left at no mass gives up all of its gas); more
  mass: an injection at the emitted parcel's temperature;
- another fluid element: the old contents extracted (a gas cell's whole gas),
  the new injected.

- the cell turned solid (native froze it): its liquid leaves (with any traces,
  which would otherwise keep only rounding residue of the cell's energy) and the
  cell closes;
- a native solid cell turned fluid (melted, or sublimated away): the cell opens
  and takes the fluid.

Native state transitions run on an owned cell only when they freeze it (the
target is a solid); fluid targets are the owner's [phase change](#phase-change).
Sublimation and transitions of native solids run natively. A cell native built
over this frame (a tile) is not written and stays native. The status reports
the booked heat, mass in and out, and the number of changed cells. A shortfall
(native took more than the owner cell still holds after its step) is not yet
reported.

## Element consumers and emitters

ONI's native element consumers (pumps, terrariums, skimmers, scrubbers, plants)
and emitters read and write one element per cell, the owner's projection, so a
carbon dioxide consumer in an oxygen-dominated mixed room would find none. The
owner runs every one (`ColonyActors.cs`) and ONI's native simulation holds
none of them:

- While the owner runs, a registration (`AddElementConsumer`,
  `AddElementEmitter`) stops at the mod, which answers it with its own handle
  through the frame's component-state messages; the actor's rate, modify and
  remove messages stop there too.
- Registrations from before the owner started (a loaded save's buildings, a new
  world's plants) are watched from the game's start, with their native handles.
  At the owner's first frame the mod removes them from the native simulation
  and runs them under those handles.

At each positive native frame each consumer with a positive rate queues
rate × dt as an owner extraction within its radius: its element, or for an
any-gas or any-liquid consumer (pumps) the whole mixture of that phase
(`MA_ONI_COLONY_EXTRACT_GAS`, `_LIQUID`), each cell's share split over its
constituents. An any-gas or any-liquid consumer that names an element (a mask
station's oxygen and polluted-oxygen consumers) takes only that element, since
ONI stores a receipt only of its element and deletes the rest. The receipts
carry the consumer flag and return as ONI's consumed-mass events for the
handle, one per constituent, so a pump's storage holds the mixture it drew. An emitter injects its rate × dt at its cell (ONI
sends the output element's generation rate in kg/s; its germs count per
emission interval), unless the cell's dominant phase already holds the
emitter's maximum (kg), and answers its blocked and unblocked callbacks on
each change.

Left native, a consumer ate the projection's one element. The shadow's other
mass changes ([above](#native-bodies)) are chiefly ONI deleting gas exposed to
space; booked as takes of the projected element, they left the other gases of
Terra's space-exposed pockets as near-empty traces, and frames took 17 to 90 s
by frame 50.

## Germs

ONI's germs are passive quantities of the owner's cells (identity: ONI's
disease index + 1): the solver carries them with the mass through flow,
settling and every transfer. A cell's germs come from the game at import; an
injection brings the germs of its message (emitters, exhaled gas, dumped
pipe contents); an extraction takes the cell's germs in proportion to the mass
it removes and its receipt returns the most numerous one, so a breath or a
pump gets the germs of what it took. Each published frame shows a cell's most
numerous germ to the game. Growth and death by medium, disease emitters and
consumers and cell edits stay ONI's: they act on the native shadow, and the
change is booked to the owner at the next step.

## Machines

A converter's output into the world (`ElementConverter`, not stored) is a
reserved transaction (`ColonyMachines.cs`): when no owned cell within two of
the output can take it the converter does not run, so it takes neither its
inputs nor, through its building's idle state, its power. A gas always fits an
open cell, after which ONI's own overpressure limits apply; a liquid needs a
cell within two that is under 98% liquid. A wrong element arriving at a pipe
input is stored or dumped as ONI configures it, and dumped into the
building's cell where ONI would destroy it, so nothing is deleted.

## Sensors

An element sensor (`LogicElementSensor`) on the owned asteroid is true when its
element is present above the trace threshold, 0.1% of the mass of its phase in
the cell (`ColonySensors.cs`, a transpiler on its sampler). Pipe element sensors
read the pipe's packets, which carry one element each as in vanilla. Pressure
sensors read the owner's pressure through `Grid.Pressure`.

## Phase change

The production kernel has no phase change, so after each step the owner relaxes
every cell holding a liquid/vapour pair toward saturation at fixed internal
energy and volume (`MacClosedWorld::with_phase_relaxation`, the reference path's
`advance_mac_phase_change`): liquid evaporates while its vapour's partial
pressure is below saturation and vapour condenses while above; a gas-free
liquid boils only beyond 5 K of superheat at its own pressure. Pairs come from
the game's transitions (a liquid whose boiling target condenses back to it) and
boil by pressure from the game's boiling point ([catalogue](material-catalogue.md)).
Mass moving within a pair is booked per species in a conversion ledger; U is
unchanged. A cell moves 1 − exp(−dt / 1 s) of the way to equilibrium per step,
so a superheated pool boils over seconds instead of flashing (Terra's small
liquid carbon dioxide pools under thin gas otherwise collapse the step to 2 ms).

## Liquids

Liquids of one [miscibility group](material-catalogue.md#miscibility) mix in
one phase; other liquids keep separate phases. After phase change the owner lets
them [settle](../native/MAC_TRANSPORT.md#liquid-settling): oil on water rises
and water sinks, with a 2 s time constant (`SettledKg` counts what moved).

## Conversions

A fluid past one of its game transitions whose target is another fluid outside
its own pair (polluted water boiling to steam, salt water to steam plus salt,
crude oil cracking to petroleum) converts at the cell's temperature, the same
share per step. The source leaves and the product enters as booked transfers,
so the energy difference is booked with them (the game's own transitions keep
the temperature too). A solid byproduct (salt) leaves the owner as a byproduct
receipt and the game spawns it as debris. A conversion too small to change the
cell's masses waits.

A liquid below its freezing point freezes natively when the projection shows it
(the cell closes, see [native bodies](#native-bodies)). A liquid the projection
hides (a puddle under gas, a minority liquid) would never freeze that way, so it
freezes here: the same share per step leaves the owner and the game spawns it
as debris of its solid at the cell's temperature.

## Terrain

Digging (ONI's `Dig`) opens the cell as exact vacuum and refills it from its gas
neighbours: with m such neighbours each gives 1/(m+1) of its gas, so the cell
starts at their pressure. Building a tile (an element replacement with a solid)
leaves the cell to ONI at once (the shadow no longer writes it and native
physics is no longer suppressed there, so the native solid stays as placed);
at its next step the owner displaces the cell's contents equally into its open
neighbours and closes it. A solid gone again before that step (unstable sand
falling at once) cancels the close; the fluid ONI puts back there is its copy of
the owner's, so nothing is injected. A solid placed after its close was queued
and melted within the same native update leaves fluid in a cell the owner then
closes: the shadow reopens such a cell once and takes ONI's fluid (a queued
open marks the wall as opening, so nothing opens or is taken twice);
the normal momentum on its faces becomes heat of the neighbour that carried it.
A cell inside a block closing at once sends its contents to the nearest
open cell through the block (within 16); only a sealed cell's contents go to
the reservoir, as do those of a closing cell whose displacement the reference
EOS refuses (the cell is solid in ONI already; the contents seep back from the
reservoir rather than the close waiting every frame). Replacing a cell with a fluid element (ONI's `ReplaceElement`)
replaces it: the owner removes the cell's fluid, booked as extracted, then
books the placed fluid; replace-and-displace adds the placed fluid instead. Moved material keeps its
U+K+G: each receiving cell gets it at the temperature whose internal energy
matches, after the potential at its height.

## Airflow tiles

ONI marks an airflow tile's cell liquid-impermeable (`SetCellProperties`); the
owner keeps that mark per cell (saved with the colony). At the start of each
step every face of a marked cell with liquid on either side is
[closed](../native/MAC_KERNEL.md#coverage-and-admission) for that step, so gas
passes through the tile and liquid cannot enter it. Liquid that still reached a
marked cell during a step (its faces close only at the next step) moves on to
the open neighbour holding the most liquid. Mesh tiles change nothing: both
phases pass.

## Pacing

ONI's game thread posts each 200 ms simulation tick and then waits for it, so
the owner's step runs on its own thread and the game thread never waits for
it. While a step runs, `Game.SimEveryTick` still runs the 1/60 s sub ticks up
to the next 200 ms tick and holds there. The game clock (`Time.timeScale`)
runs at the player's speed or 95% of the owner's measured rate (game seconds
per second of solving, smoothed), whichever is slower, so play runs in slow
motion rather than stop-go. Pause, the camera and menus use unscaled time and
stay responsive. A tick posted while a step runs joins the next step (at most
10 s of game time waits), so the owner never drops game time. The solver uses
predictive step control ([MAC_SOLVER.md](../native/MAC_SOLVER.md)).

The output arrays for cell views and composition readings are blittable and
stay pinned only during their synchronous native copy. This avoids Mono's
per-element array marshalling. An in-game control compared 61 view outputs
and 60 reading/constituent outputs byte for byte with the old binding, with
no changed-frame skips. Median view-copy time in the diagnostic runs fell
from 5.479 to 0.617 ms; the other publication work remains.

The matched fast Terra60 pair below uses one immutable native/managed package
(`c625615` native code) and saved profile 18; only the copy-binding benchmark
selector differs. Publication logs, stage logs and the byte control are off.
Both runs complete 60 frames (60 owner samples, 59 observed intervals), with
zero faults or skipped relaxations. Regional flow prohibits reference
fallback in code; the status does not expose a separate fallback counter.

| Output binding | Owner mean / p95 / max ms | Observed mean / p95 / max ms | Maximum relative mass / energy error | Trace sink kg / J |
| --- | ---: | ---: | --- | --- |
| Previous array marshaller | 160.7 / 228.5 / 302.1 | 252.6 / 358.7 / 2832.8 | 2.35e-16 / 4.68e-17 | 5.563e-8 / 0.019187 |
| Pinned arrays | 164.9 / 208.0 / 311.5 | 248.5 / 351.8 / 2845.9 | 2.36e-16 / 4.72e-17 | 5.365e-8 / 0.018930 |

The mean observed improvement is small in this single pair; owner times and
flow rejection counts vary between colonies. For profiling only, frames
10–60 give owner/observed means 159.7/205.2 versus 163.1/202.9 ms, reducing
the approximate gap from 45.5 to 39.9 ms. Full-run means above retain the
initial UI hand-off. The gap still includes native end-of-frame projection,
managed publication, pacing and Unity scheduling. This is an exact copy
optimization; it changes no physics, saved profile or checkpoint bytes, and
does not establish the 2× target or the accuracy floor.

A separate profile-18 diagnostic splits the remaining work. Per owner-frame
interval, pacing update/tick checks take 0.179/0.022 ms and presentation copying
0.447 ms. Native end-of-frame projection averages 8.97 ms (six sampled frames).
Per publication, views/snapshot/overlay/receipts average
0.603/0.860/0.940/0.967 ms over 336 steady calls. Some publication work overlaps
the solver; these costs must not be added directly to the observed spacing.
The clock calculation itself offers little scope for a large speed gain.

Fluid-view copies reuse the immutable native publication when its session and
completed frame have not changed. Status still refreshes, including live native
heat/mass exchanges; snapshots, overlays, receipts, actors and presentation run
on every publication. New buffers and restored sessions force a full copy. An
optional older native library retains the complete-copy path. A profile-18
Terra60 control compared 455 publications byte for byte with complete copies:
61 copies and 394 cache hits, with no changed-frame comparisons or mismatches.
There were no faults or skipped relaxations; maximum relative mass/energy errors
were 4.30e-16 / 4.88e-17, and the trace sink held 5.380e-8 kg / 0.019311 J.
This LOW-lock diagnostic establishes copy equivalence, not a frame-time gain.
Native controls also cover unchanged-buffer guards, live exchanges, new frames,
restored sessions and byte-exact checkpoint continuation. No physical arithmetic,
saved profile or checkpoint bytes change.

A diagnostic WHOLE-lock fast Terra60 pair uses the same immutable
native/managed bundle and profile 18, changing only
`MA_COLONY_UNCACHED_VIEWS`. Each run completes 60 frames, with all 60 owner
samples and 59 observed intervals. The observed maxima include startup.
Native stage logging was unintentionally enabled: the old implementation
treated `MA_COLONY_STAGE_LOG=0` as present and therefore active. These numbers
are retained as diagnostics, not instrument-free performance acceptance.

| Fluid views | Owner mean / p95 / max ms | Observed mean / p95 / max ms | Maximum relative mass / energy error | Trace sink kg / J |
| --- | ---: | ---: | --- | --- |
| Complete copy every publication | 173.76 / 303.13 / 346.83 | 261.61 / 372.06 / 2954.43 | 2.42e-16 / 2.78e-17 | 5.264e-8 / 0.018796 |
| Reuse unchanged publication | 173.16 / 321.91 / 426.67 | 266.34 / 448.78 / 3030.38 | 2.36e-16 / 5.17e-17 | 5.414e-8 / 0.019185 |

Both runs have zero faults or skipped relaxations. A separate fallback counter
is unavailable; regional flow prohibits reference fallback. The cached run
makes 61 copies and reuses 694 publications, versus 700 complete copies in the
uncached run. Frames 10–60 give owner/observed means 174.08/211.90 versus
172.44/217.92 ms. This single pair establishes no whole-game speed improvement;
it does not meet the 2× target. Native timing and trajectory vary with the
game's live input history even though the copy operation is exact.

The legacy 48-byte receipt output also stays pinned during its synchronous
native copy, avoiding per-element marshalling of the 4096-entry destination.
Receipt selection, ordering, callbacks, precise sidecars and acknowledgement
remain unchanged. An optional control compares the returned prefix with the
previous binding before acknowledgement; newly appended outcomes may increase
the second count without invalidating the first prefix. In a 139-frame
profile-18 evaporation, freeze/melt and door run, 128 nonempty comparisons
covered 4277 receipt entries, plus 64 empty-queue comparisons, with no growth
or byte mismatches. All ten gameplay checks passed, with no faults or skipped
relaxations, maximum mass/energy errors 4.28e-16 / 4.69e-17 and trace sink
1.166e-7 kg / 0.038514 J. This LOW-lock control establishes equivalence;
frame-time measurements use the same package with the control disabled.

The matching WHOLE-lock fast Terra60 pair below changes only
`MA_COLONY_MARSHALLED_RECEIPTS`; view caching is enabled in both. Managed byte
controls and publication logging are disabled, but the same stage-flag mistake
above enabled native diagnostics. These are diagnostic results. Each run
completes 60 frames with all 60 owner
samples and 59 observed intervals. Observed maxima retain startup.

| Receipt binding | Owner mean / p95 / max ms | Observed mean / p95 / max ms | Maximum relative mass / energy error | Trace sink kg / J |
| --- | ---: | ---: | --- | --- |
| Previous array marshaller | 173.09 / 213.08 / 353.96 | 301.70 / 365.46 / 5096.28 | 2.10e-16 / 4.67e-17 | 5.542e-8 / 0.018870 |
| Pinned array | 190.36 / 348.12 / 528.30 | 322.39 / 488.10 / 5218.97 | 3.50e-16 / 4.64e-17 | 5.587e-8 / 0.019620 |

Both runs have zero faults or skipped relaxations; a separate fallback counter
is unavailable. Frames 10–60 give owner/observed means 170.97/212.37 versus
192.06/239.03 ms. This pair establishes no whole-game speed improvement. The
physical work histories also differ: 190 versus 276 rejected flow attempts,
802 versus 844 committed transfers and 378 versus 468 publications. Component
timing or replay of identical inputs is needed to isolate the binding's cost.
Both games exited cleanly with no retained game process; neither establishes
the 2× target or an accuracy-floor pass.

A separate F1 diagnostic isolates empty-queue copy calls within the same
publication. Across 64 byte-control pairs, pinned versus marshalled mean times
are **0.00123 / 0.25140 ms**, p95 **0.0010 / 0.3187 ms**, and maxima
**0.0428 / 0.3796 ms**; medians are **0.00050 / 0.23045 ms**. No nonempty
receipts occurred in that floor run. This supports about **0.250 ms per empty
receipt publication** of component savings, not an end-to-end speedup. These
are LOW-lock diagnostics and retain call warm-up; normal timing runs disable
the byte control and all paired timers.

The replacement WHOLE-lock fast Terra60 series uses one immutable package
with the corrected stage switch, saved profile 18, diagnostic environment
variables removed and zero diagnostic log lines/control counts verified in
each run. A→B changes only view caching; B→C changes only receipt marshalling.
Each completes 60 frames with 60 owner samples and 59 observed intervals.

| Path | Owner mean / p95 / max ms | Observed mean / p95 / max ms | Maximum relative mass / energy error | Trace sink kg / J |
| --- | ---: | ---: | --- | --- |
| A: complete views, old receipts | 167.28 / 202.35 / 315.78 | 269.71 / 339.46 / 3857.64 | 2.60e-16 / 4.66e-17 | 5.566e-8 / 0.019158 |
| B: cached views, old receipts | 169.77 / 222.24 / 342.15 | 261.79 / 331.12 / 3074.75 | 2.13e-16 / 4.64e-17 | 5.383e-8 / 0.019236 |
| C: cached views, pinned receipts | 177.03 / 301.48 / 391.32 | 265.18 / 377.37 / 3035.15 | 4.29e-16 / 2.79e-17 | 5.242e-8 / 0.018566 |

All have zero faults or skipped relaxations and verified process cleanup;
fallback count remains unavailable. The maxima include initial UI hand-off.
Frames 10–60 give observed means 205.57 / 207.58 / 219.26 ms, so these single
runs do not establish an end-to-end gain or meet 2×. CPU locks exclude other
agent builds/tests; a separate process inspection found background CPU service
activity, so these are not exclusive-hardware measurements. Component byte
equivalence and receipt call savings remain separate evidence. The child's
catalogue-wide pipe fixture also verifies 127 receipt entries and 64 empty
queues without mismatch; 74 paired calls average 0.00103 / 0.26837 ms pinned
versus marshalled, including actual nonempty mixed-pipe outcomes.

Submission/completion timing uses independent queued frame identities. ONI can
submit the next tick before publishing the previous completion, so pairing a
completion with the latest submission gives invalid latencies. A focused
control covers that ordering, skipped observations and restored frame numbers.
The diagnostic leaves clock targets, tick admission and physical state unchanged.

With the pinned bindings, a fresh optional profile-19 colony also passes the
evaporation/vapour, freeze/melt, space-heater and gas-generator exhaust
scenarios (201 frames, low CPU lock). No faults or skipped relaxation;
maximum mass/energy errors 7.00e-16 / 4.31e-17 and trace sink
1.624e-7 kg / 0.051353 J. This checks those gameplay windows, not the full
accuracy floor; profile 19 remains an opt-in experiment.

## Saves

**Reserved 2026-10-23, immediately after fetching `81b0021`:** owner wrapper
`MACOLN01` version **8** is assigned to durable mixed-pipe job tokens, queued
full-passive injections/extractions and the accepted-token watermark. This is
an owner adapter format, separate from `MACCLS01` physical versions. Its
native queue/receipt/save controls pass; full managed integration and in-game
mixed-pipe save acceptance are pending. Older versions 6/7 and their recorded
solver profiles remain readable. No colony silently acquires mixed pipe
ownership.

Fixed-floor diagnostics can retain up to 64 immutable completed publications
through `ma_oni_colony_sample_history`, restricted to owners of at most 4096
cells. `ma_oni_colony_sample_frame` reads an exact frame and fails if it was
not retained; the latest selector is `UINT64_MAX`. Its extra heat output is
cumulative accepted external heat from that frame's ledger, including body
exchange. It is separate from live queued-input counters. The history is
disabled in normal games and absent from checkpoints. Floor F1/F2/F3/F5 drain
consecutive frames and check physical 0.2 s intervals; F4's native solid
temperature alignment remains open.

ONI's native section of the save becomes a `MACOLONY` envelope holding the
original native bytes and the colony checkpoint (`MACOLN01` version 8, which
also reads version 6/7 saves: configuration, material records, space and
liquid-blocking masks, reservoir, ledgers including the trace sink's, continuing
counters, the inputs queued for the next step, the `MACCLS01` physical image
and a SHA-256). Inputs in flight at the save keep their mass but not their
callbacks, which do not survive a load: an injection (exhaled gas, an emitter's
output) and native-side changes are kept; an extraction answering a callback (a
breath, a consumer) is dropped and asked again after the load. Loading hands the native bytes to ONI's loader and restores the
owner at the first completed frame; a different catalogue or geometry rejects
the save. A game without the mod passes the envelope to its native loader,
which refuses it.

The optional durable pipe-job API accepts contiguous tokens from 1, with
full sorted passive lists for injections. Repeating a token never queues
another transfer. State queries distinguish future, queued, committed and
fully acknowledged work; committed jobs report the last receipt sequence so
the client can group every constituent before settling its reservation. Pipe
outcomes are hidden until the complete physical frame is committed and
published. A save refuses unacknowledged committed pipe outcomes. Pending
pipe injections and extractions survive a load with their tokens/passives;
callbacks remain transient. Version 8 adds uint64 accepted-token watermark,
uint32 pending-job count and, for each queued job, uint32 saved transfer index,
uint64 token, uint32 passive count and ordered uint64 identity/binary64 quantity
pairs after the frozen-cell list. The existing SHA-256 covers them.

Pump consumers require `ma_oni_colony_actor_batch_version()` capability 1 and
the atomic ordered actor-batch enqueue. A 24-byte descriptor identifies a
transfer index, passive offset/count, reserved zero and uint64 job token.
Descriptors preserve actor order; new job tokens are contiguous and fresh.
Every descriptor, passive payload and transfer is checked before admission.
After allocations succeed, transfers and durable metadata commit under one
input lock, so a worker cannot take ordinary actor A before its intervening
pump job and later actor B are queued. Replaying a batch containing jobs
rejects without duplicating ordinary actors. The single-job idempotent API
and owner8 persistence rules are unchanged. Controls compare live ordering,
receipts and checkpoints with the existing enqueue sequence, compare cold
continuation under the existing transient-callback policy, reject malformed
or repeated batches without mutation, and race 50 three-request batches
against worker advances without splitting a batch across completed frames.

This adapter addition changes owner-wrapper bytes, not the physical solver
image, its profile or transfer arithmetic. Controls cover fractional/multiple
passives, zero results, token gaps/reuse, partial acknowledgements, old-version
framing and byte-identical queued injection/extraction continuation. Germ
representation remainders in binary64 world cells are exposed through optional
per-edit observations. Full managed accounting and port acceptance remain
pending.

`ma_oni_colony_precise_receipt_version()` reports capability 2; a pipe client
requires it before submitting any job. Durable jobs use **MARECP02**, the
MARECP01 layout plus, after each parcel's passive list, a uint32 observation
count and sorted triples of uint64 identity, binary64 before and binary64 after.
These are the actual source/destination cell counts immediately around that
individual transfer, including zero transfers and multiple jobs touching one
cell. The default-off `MacFluidTransfer.observe_passives` flag records this
metadata without changing arithmetic, batching, state or checkpoint bytes.
Ordinary clients keep MARECP01 and empty observation fields. Native controls
verify identical physical/default parcel results and exact observation of
tiny/huge/fractional same-identity remainders. Rebuild C++ clients after the
trailing public metadata fields change; the game C ABI structs are unchanged.

## Import

`settle_import` (`native/src/oni_colony_import.cpp`) turns the game's
one-element-per-cell inventory into a near-equilibrium owner state. It creates
and deletes no mass; per-species totals of the result plus the reservoir equal
the imported totals.

The game's state is far from physical equilibrium. Liquid cells scatter about
their full-cell mass by a few percent (with water's bulk modulus that is
megapascals of pressure noise), sealed crude-oil pockets hold two to four times
the oil that fits, partly filled liquid cells have no gas in their headspace,
and gas cells beside each other differ in pressure by up to a hundredfold.
Started as is, the solver spends its steps on violent transients; settled, a
whole asteroid starts at rest.

1. **Regions.** Connected open cells. Space cells (vacuum exposed to space) are
   excluded: they stay empty and are the open-sky sink.
2. **Liquid.** Each connected body of one liquid empties into one pool that
   fills its basin from the bottom: the lowest open cells reachable from the
   body first, at one shared level, the top layer at a common fraction. Full
   cells hold their hydrostatic mass for the level, at the region's gas
   pressure (or zero under a sealed ceiling). The region keeps room for its gas
   at no more than ten times its pressure (at least 1 MPa). Liquid that cannot
   fit stays booked to the region in a **reservoir** entry, with its
   temperature; it is never deleted. A region its compressed liquid fills
   completely books its gas to the reservoir too. Kept cells keep their temperatures, shifted
   uniformly so the body's sensible heat is unchanged; flooded cells take the
   body's mean.
3. **Phase equilibrium.** A region's vapour beyond its saturation pressure
   (whole region room, region temperature) condenses into its liquid, laid from
   the region's lowest cells up before levelling. Terra's dense cold chlorine and
   carbon dioxide pockets otherwise rain for minutes after the start. The
   reverse holds where a liquid sits under less gas pressure than its own
   vapour pressure (magma beside a vacuum pocket): it boils in moments, so the
   region starts holding saturated vapour at the liquid's mean temperature.
   Otherwise its first expansion into the void starts the owner with fast thin
   fronts (Terra's magma: 35 substeps a frame). Under gas at higher pressure
   evaporation is slow and stays with the owner.
4. **Gas.** In each region the gas is laid out again at one pressure without
   mixing: ONI keeps one element per cell and its own simulation sorts gases by
   density, while spreading every species through a region would mix a room's
   CO2 into its air (Terra's starting room holds 33 kg of CO2 at its floor,
   12.7 mol% of its gas, which mixed would be unbreathable). Each cell keeps the
   temperature it had (its gas, else its liquid, else the region's mean): the
   game's thermal field matches the rock around it, and one region temperature
   started fast convection in Terra's dense hydrogen pockets. The region's
   pressure makes the moles fit, p = n R / sum(V/T); the layers fill the gas
   room from the bottom, heaviest molar mass first, each cell holding p V/(R T),
   and only cells where two layers meet hold a mixture. A vapour of a liquid/vapour pair holds at most its saturation
   partial pressure in a cell, so its layer spreads over more cells and the next
   layer's gas fills the rest; vapour that still finds no room condenses at the
   bottom and the layout is redone. This also fills headspaces and vacuum
   pockets.
5. Liquid masses depend on the gas pressure above them, so steps 2 and 4 repeat
   three times.

The import ignores the small hydrostatic gradient within gas.

**Control.** `ma_oni_colony_import_tests` (CTest `oni_colony_import_settle`)
settles a made-up world with an overfull pool, a high-pressure pocket, a sealed
overfull oil pocket, a vacuum pocket and open sky, checks conservation, the
reservoir, the level, one pressure per region and the density layering, then
advances the settled state 0.2 s and requires it to stay near rest.

**Measured on Terra (seed 147147, private capture, standalone).** 30,886 open
cells in 1,297 regions; the settle takes about 130 ms, leaves a relative
pressure spread of about 1e-15 within regions and a species mass error of 1e-16, and
books 656 t (sealed crude oil and magma) to reservoirs. The starting room keeps
its CO2 as a floor layer under oxygen. The settled asteroid then takes full
0.05 s production steps after the first frames.

**Reservoir release.** Each owner step, every reservoir entry (import overflow,
or the contents of a cell closed with no open neighbour) seeps back into the
open cell with the least liquid within three cells of its anchor, at most
50 kg/s and only while that cell is under 90% liquid, at the entry's
temperature. A sealed overfull pocket therefore keeps its liquid booked until
digging gives it room, and then yields the game's amount. Released mass is
booked as an injection.

## Scenarios

`tools/oni_headless.py run --mode colony-owner --scenario NAME` checks one
system in game (`managed/MixedAtmospheres.Oni/ColonyScenarios.cs`). Each
scenario seals an 8 × 6 arena of Unobtanium nine cells right of the printing pod
through the game's own cell messages, fills it two frames later, runs some owner
frames and records its checks in `colony-owner.json`; the run fails on a failed
check or an owner fault. Scenario buildings are built instantly and powered by
setting their powered flag (harness only).

| Scenario | Checks |
| --- | --- |
| `terrain` | Digging a granite column lets gas reach the vacuum beyond it; a built tile closes its cell; both conserve the gas. |
| `heat` | A 450 K granite floor warms the air above it through ONI's own conduction, with the owner's energy ledger closed. |
| `phase` | 330 K water evaporates into the dry 330 K air above it. |
| `freeze-melt` | Ice in warm air melts into an owned cell; water on a 40 K block freezes. |
| `deoxidizer` | An oxygen diffuser's oxygen from stored algae reaches the owner. |
| `pipes` | A gas pump in oxygen and carbon dioxide feeds a pipe to a vent in vacuum: both gases arrive, and owner, pipe and pump keep the gas. |
| `door` | A manual airlock between oxygen and vacuum seals when closed, passes gas when opened and displaces its cells' gas when closed again (a pneumatic door does not block gas in ONI). |
| `sublimation` | Oxylite dropped in thin oxygen releases oxygen into the owner. |
| `falling` | A sand tile placed over gas falls: its cell reopens, it lands and closes a floor cell, and the gas is kept. |
| `breathing` | The colony's duplicants keep full breath; oxygen is taken from the owner and carbon dioxide exhaled into it. |
| `airflow` | A column of airflow tiles between water under air and thinner air passes the air but no water. |
| `electrolysis` | An electrolyzer splits stored water into oxygen and hydrogen that reach the owner. |
| `vent-overpressure` | A pump feeds a vent into air above the vent's limit: the vent stays shut and the pipe holds the gas. |
| `space-heater` | A powered space heater warms the arena's air. |
| `bottle-emptier` | A gas canister emptier releases its stored carbon dioxide into the owner, and a bottle emptier's water falls as particles into owned cells. |
| `gas-generator` | A natural gas generator burns stored methane; its carbon dioxide goes through its exhaust pipe and a vent into the owner. |
| `germs` | Slimelung released with polluted oxygen spreads with the gas. |
| `puft` | A puft breathes the arena's polluted oxygen out of the owner. |
| `plant` | A wild mealwood in owned oxygen reads normal pressure and a safe atmosphere; one in thinner air beyond a wall reads low pressure. |
| `wheezewort` | A Wheezewort's gas consumer runs through the owner, and the gas it takes returns to the arena or is held by the plant. |
| `life-support` | The dev life-support building's consumer and emitter run through the owner: carbon dioxide in, oxygen out, and the emitter sees its full cell. |
| `mask-station` | An oxygen mask station fills its storage with the owner's oxygen; the arena loses what the station holds. |
| `polymer-press` | A polymer press works stored petroleum; its steam and carbon dioxide go through its exhaust pipe and a vent into the owner. |
| `oxyfern` | A wild oxyfern's consumer takes the owner's carbon dioxide. |
| `debris-heat` | Hot debris warms the owner's air through the native shadow; the energy ledger stays closed. |
| `building-melt` | A lead ladder melting (the game's melt path) gives its mass to the owner as liquid. |
| `geyser` | A steam geyser shifted to its active period erupts into the owner. |
| `rocket-exhaust` | A steam rocket engine put into its burning state (as at launch) exhausts hot steam into the owner. |
| `meteor` | A rock meteor falls through the arena's air and strikes its floor with the owner's ledgers closed. |
| `sensor` | A gas element sensor set to carbon dioxide switches on in a cell where carbon dioxide is a minority (the cell shows oxygen). |
| `filter` | A pump sends oxygen and carbon dioxide through a carbon dioxide filter: its filtered port carries only carbon dioxide, its output only oxygen. |
| `pacu` | A pacu in an owned pool stays in its water. |
| `liquid-pump` | A liquid pump in a pool sends its water through a pipe into a reservoir; pump, pipe and reservoir hold what the owner lost. |

### Floor arenas

`--floor f1..f5` runs one fixed comparison arena of
[docs/base-game-floor.md](base-game-floor.md) instead of scenarios
(`managed/MixedAtmospheres.Oni/FloorArenas.cs`): under the owner
(`--mode colony-owner`) or under ONI's own simulation (`--mode colony-vanilla`,
where the owner never starts). The arena is carved nine cells right of the
printing pod, after the plants, critters and debris the world left there are
removed (they keep their own temperature and would exchange heat with the
gas; the record counts them, and what their removal drops goes at the fill),
filled two ticks later (oxygen from the ideal-gas law at the
stated pressure, liquid as full cells at the catalogue's reference density at
1 atm, `ma_oni_colony_liquid_density`) and released by the protocol. Every
200 ms tick's published game data is sampled for the arena's interior into
`floor-<id>.txt`, with `floor-<id>.json` holding the event ticks, the placed
masses and each sampled element's properties read from the game.
`tools/floor_metrics.py floor-<id>.json` computes the protocol's metrics.
Numbers only: no saves or captures are kept.

Owner samples use `ma_oni_colony_sample`: requested-cell compositions from one
completed physical world, its projected binary32 temperatures, source status
and full booked fluid-plus-reservoir mass. Unlike the presentation table it
retains material whose EOS projection was refused and flags that refusal.
Records include frame/time stamps and audit counters, so repeated observations,
conversion flows and actual omissions can be distinguished. The exporter uses
the native published temperatures for solid cells. This is optional fixture
instrumentation; ordinary readers, physics, settings and checkpoint bytes are
unchanged. Earlier per-cell owner samples remain provisional until rerun.

## Verification

- `ma_oni_colony_tests` (CTest `oni_colony_owner`): a made-up asteroid with
  rock, a pool, gases and open sky; transfers with receipts, heat, the space
  sink, projection, composition, suppression, a dig and a build, ledger closure,
  exact checkpoint continuation, and rejection of a different catalogue or a
  corrupt image.
- In game (Linux, Terra seed 147147): `tools/oni_headless.py run --mode
  colony-owner --frames N` starts a normal new colony and passes after N owner
  frames without a fault; `--save-colony` also saves through ONI's ordinary save,
  and `--mode colony-owner-load --load-save <file>` loads it in a fresh process.

## Diagnostics

Bounded prepared-colony replay, 2026-10-23: the immutable `b36ed23` native pair
with the pinned managed output path restored `base-trace-144` unchanged at
owner frame 32 (saved physical profile 18). It completed 4,351 further frames,
to counter 4,383, then the flow watchdog exceeded eight seconds before frame
4,384. The run stopped at that first severe stall; its complete private
65,913,344-byte version-18 world dump passed the MAC checksum and was handed
to the solver worker. The game process group's termination was verified.
The ten-cycle check remains pending this shorter check.

This was a LOW-lock diagnostic replay with concurrent HIGH-lock work and
stage/watchdog instrumentation, **not performance acceptance**. The sampled
owner mean/p95/max was 339.119/821.478/2320.494 ms (4,309 samples); polled
observed spacing was 412.109/903.235/3509.717 ms (4,308 intervals, averaged
across missed counters). These completed-frame statistics exclude the
unfinished flow that exceeded eight seconds. No fault or skipped relaxation
was reported before termination; no separate fallback counter is exposed.
Maximum relative mass/energy errors were 6.641e-15/5.236e-17, trace sink
1.309969e-6 kg and 0.962829 J, with 71,836 rejected attempts. No profile
migration, numerical setting change or ten-cycle pass is implied.

Environment variables of the game process (the harness passes them through):

- `MA_COLONY_FAULT_DUMP=path`: on a failing flow step, write the material
  records and the world before that step; the scratch benchmark loads it.
- `MA_COLONY_DUMP_FRAME=n` (with the path): also write the world before frame
  n's flow.
- `MA_COLONY_RELEASE_DUMP=path`: fixed F1/F3/F5 owner fixtures atomically publish
  a geometry-bound gate marker before queuing removal. The owner writes a
  private material/world dump immediately before the first fully open flow,
  once per session, with frame/time in the log. The path must be fresh; malformed
  markers, failed writes and geometry mismatches fail the diagnostic. Other
  scenarios produce no release capture. It changes no fixture, physics,
  checkpoint or solver setting; leave it unset for timing runs. Verify the
  completed dump's MAC checksum before reference work and keep it private.
  This is before fluid advance but **after the owner applies terrain edits**.
  Opening a gas-filled room immediately refills new cells from its neighbours;
  the F1 image therefore already holds 6.25% of the gas in the divider. These
  captures are game intermediate states, not frozen-protocol initial worlds.
  They may support exact replay of that suffix, but must not seed initial-state
  oracle comparisons. F3/F5 also include pre-release evolution; reconstruct
  resolved starts from the fixed protocol and runtime catalogue instead.
- `MA_COLONY_SLOW_DUMP=s` (with the path): also write the world before the
  first flow step still running after s seconds (a watchdog, so a step that
  never finishes is caught).
- `MA_COLONY_STAGE_LOG=1`: every tenth owner frame, each stage's wall time and
  the frame's edit counts (in the run's `Player.log`).
- `MA_COLONY_PUBLICATION_LOG=1`: each managed publication's view copy, snapshot
  capture, overlay, receipts, actors, retirement and presentation hand-off in
  milliseconds, plus the presentation copy and pacing CPU time separately.
  Positive native-frame submissions record actor queuing and time since the
  first observed completion; first completion observations record time since
  the matching submission and owner advance time. Submission targets are
  numbered independently of the last observed completion: the next tick can
  be queued before the previous completion is published. Discard unmatched
  startup observations or aggregated timesteps when comparing latencies. Use with the
  stage log to locate work between owner advances; logging changes timing.
- `MA_COLONY_COPY_CONTROL=1`: compare pinned output-array copies against the
  previous array-marshalling path, byte for byte on matching completed frames
  (up to 64 frames). `colony-owner.json` records view/reading comparison counts
  and publications that changed between the paired calls. This adds copying
  work and is excluded from speed measurements.
- `MA_COLONY_MARSHALLED_OUTPUT=1`: benchmark the previous output-array binding
  with the same package and physical profile. Otherwise blittable output
  arrays stay pinned for the duration of the native copy. This changes no
  solver setting or saved data.
- `MA_COLONY_UNCACHED_VIEWS=1`: benchmark complete fluid-view copies on every
  publication with the same package and saved physical profile. Status and all
  other publication work run in either mode.
- `MA_COLONY_VIEW_CACHE_CONTROL=1`: compare reused views with a complete copy,
  byte for byte on matching native session/frame pairs (up to 512 comparisons).
  `colony-owner.json` records copy, hit, comparison and changed-frame counts.
  This adds copying and is excluded from speed measurements.
- `MA_COLONY_MARSHALLED_RECEIPTS=1`: benchmark the previous receipt-array
  binding with the same package and saved physical profile.
- `MA_COLONY_RECEIPT_COPY_CONTROL=1`: compare pinned receipt prefixes against
  the previous binding before acknowledgement (up to 128 nonempty and 64 empty
  queues). The owner report records comparison, entry and growth counts. This
  adds copying and is excluded from speed measurements. It also records paired
  native-call durations and compared-prefix sizes. Timing excludes the byte
  comparison; the two calls share one live queue, with no acknowledgement
  between them. Queue-lock contention and first-call warm-up remain visible.
- `MA_COLONY_DUPE_LOG=1`: every 150 owner frames, each duplicant's breath,
  health, calories and stress with the air at its cell and the cell above
  (mass, composition, breathability), and the prepared base's machines.
- `MA_COLONY_HEAT_LOG=1`: each heat or mass change ONI's native update made to
  an owned cell (its cell, element, mass, temperatures, booked joules), and
  each native item chunk in contact with one (its temperature and heat
  capacity); for small arenas, since it writes lines every frame.
- `MA_COLONY_TERRAIN_LOG=1`: every queued terrain edit, its cell and source
  (managed, shadow close/open, or waiting with the refusal).
- `MA_COLONY_GAME_FLOOR=1`: new colonies run the game-floor profile (test runs
  only until the profile is selected).

A failing transfer names its cell and the cell's contents in the fault.
