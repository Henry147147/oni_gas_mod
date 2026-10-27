# Colony owner

The colony owner is the normal-game path: one `MacClosedWorld` owns every gas
and liquid in the colony, with one species per catalogue material
([material catalogue](material-catalogue.md)). It replaces the bounded
development rooms of the [configured domain](native-configured-domain.md) for
play.

## Activation and data flow

- **Activation.** With the native backend attached, the mod starts the owner on
  a new base-game or Spaced Out! colony at the first completed native frame.
  Base-game colonies have one asteroid; Spaced Out! owns the validated cluster
  grid, including rocket interiors, with unallocated holes closed. It builds the catalogue from
  the game's element table, reads every cell of the asteroid from that frame
  (solids are walls; vacuum in the Space zone is open sky) and calls
  `ma_oni_colony_start` (`native/include/mixed_atmospheres/oni_colony.h`). A
  fluid cell holding an element outside the catalogue keeps native physics.
  While any gameplay pack other than Spaced Out! is active (Frosty, Bionic,
  Prehistoric, Aquatic or an unknown one), the owner never starts and the
  colony keeps ONI's fluids; Mixed Atmospheres saves are refused before
  loading ([refusals](distribution.md#refusals)).
  A package carrying the native backend attaches it by default; no development
  file or environment variable is needed (`native-backend-observation.json`
  may disable it or, when present, adds the development report). In an
  ordinary game only the owner routes native messages; the colony-owner
  scenes use the same routing plus their report counters.
- **Notices.** The player is told when the owner does not take a new colony
  (unsupported geometry or gameplay packs, or a failed start), when a loaded colony
  was saved without the mod (it keeps ONI's fluids) and when the owner faults
  (fluids freeze, saving is refused): one dialog per problem
  (`ColonyNotices.cs`); validation scenes only log.
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
- **Starting air** (`ColonyStartingAir.cs`, [starting air](starting-air.md)): before the
  import, the gas connected to the printing pod is counted; once the starting
  duplicants exist, their oxygen use and the colony's breathing rules give the
  cycles of breathable oxygen and carbon dioxide headroom (the
  `StartingAir` report and a log line). An oxygen shortfall over ten cycles
  is placed as oxylite debris beside the pod and named in a start notice.
  Validation scenes place it only with `MA_COLONY_STARTING_RESERVE=1`.
- **Breathing:** by default, at least 15% oxygen by moles and 15 kPa oxygen
  partial pressure; polluted oxygen counts as oxygen. CO₂ limits default off.
  When enabled they apply the saved thresholds (default 0.5% and 500 Pa).
  A breathable cell offers its oxygen of the more abundant kind. The live
  Gameplay switch updates both the duplicant reader and presentation, and is
  saved with the colony. Numeric thresholds and an optional oxygen ceiling are
  chosen under Advanced before starting a colony.


A faulted owner keeps publishing its last frame (fluids freeze), logs the error
and refuses saves. An exception escaping one of the mod's paths inside the
game's per-frame update (receipt settlement, the owner's publication and
messages, owned pipe moves, intakes, heat and germs, vents, sensors and pipe
readers, and as a last net the game's sim step) faults the owner the same way,
once (`ma_oni_colony_fault`, `ColonyFault`): owned pipes freeze with the fluids,
the notice shows, and the exception never reaches the sim step, which would
otherwise stop every later step without telling the player.

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
vanilla projection and is counted. The cell hover lists every germ identity,
and a duplicant holding its breath or suffocating is told why the air at its
head fails (the hover's breathing warnings, `ColonyBreathingStatus.cs`).

## Native bodies

ONI's tiles, buildings, items (element chunks), pipes and element
consumers/emitters exchange heat and mass with cells inside the native frame.
The next native frame waits for the preceding owner step before applying its
messages. This also covers a frame queued before managed pacing observes that
the owner is busy; its inputs cannot enter the preceding step.
Before each positive frame's `UpdateData` the backend writes the latest
completed owner cells (element, mass and temperature of the projection) into
both native cell arrays, a shadow. This publication also waits for any queued
owner step. Native code then runs unchanged against it,
at vanilla rates, except that conduction between two owned cells stays the
owner's. After `UpdateData` every change native made to an owned cell becomes a
booked owner input for the next step:

- same element and mass, new temperature: heat `m c ΔT` using ONI's projected
  cell capacity. This books the observed shadow work; equality with every
  native body's represented endpoint loss still requires a coupled proof.
  The [closed storage heat control](mixed-pipes.md) misses its strict bound by
  0.703 J after the queued-projection credit repair; the user classified that
  measured residual as non-consequential on 2026-10-26, so it is not a blocker;
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

Each emitter's added mass is booked per element to an external source
ledger named after what stands at its cell (a geyser, a building;
`EmitterSources` in the report).

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
numerous germ to the game; a germ sensor counts all of them.

**World evolution** (`oni_world_biology.cpp`, owner mode 1, every new colony
whose game supplied its disease table through `ma_oni_colony_set_diseases`).
Once per native 0.2 s frame the owner applies the game's world rules to every
disease identity of every owned cell, each identity on its own (a cell holds
several; there is no competition between them):

- *Diffusion* first, from the frame's starting counts: between two
  neighbouring cells of the same shown phase an identity moves an eighth of
  the difference in count, scaled by the richer cell's element and truncated
  to whole germs; the richer cell must hold its element's minimum count and
  have held the identity its minimum number of frames.
- *Growth and death* then: the temperature half-life of the disease and the
  element's rule inside, below (an absolute death rate) or above its density
  window. A mixed cell weights each constituent's rule by its mass, like pipe
  biology. A whole-germ culture keeps the game's binary32 count with its
  fractional residue; a culture whose count is not whole (flow splits counts
  by mass) follows the expected population in binary64 until it dies out.
  A culture left below one germ dies, as the game keeps whole germs.

Births, deaths, culled sub-germ cultures, diffused germs and game edits are
booked per identity (`ma_oni_colony_biology`); each stage checks that the
world holds what the accounts expect. Native code sees no germs on owned cells,
so its own world update no longer runs there; disease emitters still act on
the shadow and their germs are booked in, and the game's cell germ edits
(`ModifyDiseaseOnCell`) of owned cells go to the owner
(`ma_oni_colony_queue_germs`). Clocks (mode, residue, frames held), queued edits
and accounts are saved (owner version 10). Saves from before keep the legacy
mode: native code evolves the shown identity on the shadow and the change is
booked to the owner.

The world rules are written from the game's measured behaviour. The explicit
oracle control (`MA_COLONY_GERM_ORACLE=1`, scenario `germ-oracle`) keeps a new
colony in the legacy mode and predicts every native step of a sealed
single-cell culture from the same inputs: in game 778 of 778 steps agree
exactly (growth inside the density window, over-density decay, heat death,
extinction below the minimum count).

The read-only `ma_oni_colony_read_passives` ABI reports every passive in selected
cells, including non-disease identities, without reducing them to that legacy
projection. Select 1–4096 sorted, unique public cells. A null-row count query
returns a session/frame/time stamp; use that session/frame for the subsequent
copy. If the publication advances, `BUSY` requires a fresh query. Rows are sorted
by public cell and identity; quantities retain their binary64 bits. Version 1
uses a 32-byte stamp and 24-byte rows, capped at 1,048,576 rows. Invalid input,
stale labels and insufficient output capacity preserve every caller output.
The managed `ColonyWorldPassives` reader validates both calls and returns only
one complete publication. Empty selections are invalid; an empty result for
selected cells is valid.

Owner controls cover offset geometry, minority/tiny/wide quantities, a
non-disease identity, unchanged checkpoint bytes, cold restore, stale labels
and atomic failures. Managed controls cover layout, canonical rows and corrupt
epochs/counts. This adds observation, without enabling biological evolution,
changing the saved profile or establishing actual-game all-germ acceptance.

`MA_COLONY_WORLD_PASSIVE_CONTROL=1` enables a separate live observation control.
Once per completed publication it selects an owned cell (preferring a germ
carrier), reads its full inventory and checks the legacy maximum/count without
choosing a new winner for equal-count ties. Non-disease identities do not compete
for that projection. An advanced publication is retried, rather than compared
against an older view. Reports expose `WorldPassiveComparisons`,
`WorldPassiveRows` and `WorldPassiveBusy`; the control is off during timing runs.
An actual fresh profile3/core21 base run passes32 observations/32 rows with
zero stale reads, alongside the existing four base checks and an ordinary save.
This verifies live ABI/projection agreement, without accepting full world
minority biology.

## Machines

A converter's output into the world (`ElementConverter`, not stored) is a
reserved transaction (`ColonyMachines.cs`): when no owned cell within two of
the output can take it the converter does not run, so it takes neither its
inputs nor, through its building's idle state, its power. A gas always fits an
open cell, after which ONI's own overpressure limits apply; a liquid needs a
cell within two that is under 98% liquid. A generator's gas exhaust (coal,
natural gas, petroleum, wood) is gated the same way. A wrong element arriving
at a pipe input is stored or dumped as ONI configures it, and dumped into the
building's cell where ONI would destroy it, so nothing is deleted. An addition
nobody is told about (a machine output or dumped item, callback −1) that finds
no open cell within two waits in the reservoir at its cell and seeps back when
there is room, instead of being lost; its germs are not kept.

**Balance ledgers** (`ColonyBalance.cs`). Machines keep ONI's rates. Each
`ElementConverter` conversion and each generator tick books, per building kind,
the energy of what left its storage, what arrived there or went to the owner,
and the electricity drawn (or generated); the difference is that machine's
balance ledger (`BalanceLedgers` in the report). Energies use the catalogue's
caloric model (c·T plus formation offset; c·T for solids). The owner books the
fluids it receives exactly in its own ledgers either way.

## Sensors

The mod's Gas Share Sensor (`GasShareSensor.cs`) compares one gas's mole share
of its cell's gas with a threshold; off the owned asteroid a cell's single
element counts as all or none of it.
An element sensor (`LogicElementSensor`) on the owned asteroid is true when its
element is present above the trace threshold, 0.1% of the mass of its phase in
the cell (`ColonySensors.cs`, a transpiler on its sampler). Pipe element sensors
read the pipe's packets, which carry one element each as in vanilla. Pressure
sensors read the owner's pressure through `Grid.Pressure`. A germ sensor
(`LogicDiseaseSensor`) counts every disease identity of an owned cell, not only
the one the grid shows.

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
Native solid transition/postprocess observations also identify a solid that
swaps into owned fluid and melts during the same native update. The displaced
scalar projection is a copy, never an external source or sink: the real mixture
stays with the owner, whose terrain and flow operators move it. Only the solid's
melt is admitted; heat and germ changes following the displaced copy are booked
to its real inventory. Ambiguous endpoint matches produce a visible owner fault.
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
owner keeps that mark per cell (saved with the colony). New game-kernel owners
record boundary law12. Faces touching liquid close before external edits and
are refreshed before flow. If a moving liquid front would cross a marked
cell's face, the complete trial interval is discarded and repeated from its
original state with that face closed, including a new pressure/work solve.
Condensation inside a marked cell isolates its faces before the next tick's
flow and interface operations. Only accepted flow, conversions and trace
removal are booked; closing momentum is reported as wall impulse. Dry paths
still pass gas.

Wrappers8–11 retain the earlier mask computed once before external edits,
including its moving-front limitation. Both laws expel direct-source liquid
and condensate from marked cells into an open neighbour holding the most
liquid. Mesh tiles pass both phases. The core checkpoint stores game-kernel
face masks; the owner wrapper records which boundary law rebuilds them.

## Pacing

The owner's step runs on its own thread. While a step runs,
`Game.SimEveryTick` still runs the 1/60 s sub ticks up
to the next 200 ms tick and holds there. The game clock (`Time.timeScale`)
runs at the player's speed or 95% of the owner's measured rate (game seconds
per second of solving, smoothed), whichever is slower, so play runs in slow
motion rather than stop-go. Pause, the camera and menus use unscaled time and
stay responsive during those pacing holds. A native-frame fence also waits for
the preceding owner step before applying the next frame's messages and
contacts, covering frames posted before managed pacing sees the busy flag.
It preserves the physical frame order without advancing an extra step or
dropping game time. The solver uses
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

The canonical exact-v3 kernel3 bundle has a separate WHOLE-lock GPU99 Terra60
diagnostic at fast speed. All 60 owner samples and 59 intervals are retained;
owner mean/p95/max is 203.095/236.185/260.667 ms, observed spacing
250.900/341.254/766.029 ms. Maximum mass/energy errors are
3.749e-16/6.717e-16, trace sink 1.193e-7 kg / 0.127787 J, with no fault,
rejection or skipped relaxation and verified cleanup. Fallback count is not
exposed. Instrumentation is enabled, so this is not a speed acceptance run.

The 50 observed intervals after frame 10 have an aligned observed-minus-owner
gap of 39.627 ms (p95 106.704, max 167.661). Logs contain 841 publications
across startup/measurement/shutdown; frames 10–59 contain 419, or 8.38 per
owner frame. Their costs are:

| Publication work | Mean ms per publication | Summed ms per owner frame |
| --- | ---: | ---: |
| Native fluid-view copy/status | 0.0589 | 0.493 |
| Managed snapshot capture | 1.0722 | 8.985 |
| Overlay | 0.9395 | 7.873 |
| Receipt handling | 1.0173 | 8.525 |
| Actor publication | 0.00223 | 0.0187 |
| Retirement | 0.01230 | 0.103 |
| Presentation hand-off | 0.08015 | 0.672 |

Native end projection averages 2.183 ms over six sampled frames. Presentation
copying averages 0.621 ms per completed frame; pacing update/tick CPU costs
are 0.181/0.0198 ms. The next submission precedes observation of the previous
completion in 29 of 50 steady cases. These costs overlap; neither the table
sum nor the submission logs alone identify removable critical-path time.
Receipt sub-laps now distinguish copy, dispatch, byproduct creation, event
append, native acknowledgement and managed settlement under the existing
publication flag. They preserve all original callbacks and ordering and are
disabled with that flag in final timing runs.

The user subsequently confirmed a concurrent vLLM GPU workload. A current
observation shows 29,208 MiB held by vLLM on the 32,607 MiB RTX 5090, at 98%
GPU utilization. CPU locks do not isolate that workload; the runs above have
no recorded historical GPU-load trace and remain potentially contended
diagnostics. The successful runs show the NVIDIA renderer and no observed
GPU out-of-memory error. A quiet GPU window is needed for final game-speed
acceptance. This does not explain the separately recorded invalid cluster
argument or absent GPU-display setting, which failed launch configuration.

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

The fixed owner rectangle now also supplies a cached public-cell map, rebuilt
on creation and restore and retired on session reset. This removes repeated
integer division/remainder from publication and initial import. It retains
fresh native snapshots, all terrain and `Grid.Element` checks, identical event
order, receipts, actors and presentation. No solver arithmetic, saved setting
or checkpoint bytes change. `MA_COLONY_UNCACHED_CELL_MAP=1` selects the original
calculation for comparison; `MA_COLONY_CELL_MAP_CONTROL=1` compares every index
and is disabled for timings. Offset/same-count shape/edge/invalid-geometry
controls pass as part of 217 snapshot tests. A real flags5/core20 control checks
55,345,152 indices with no mismatch over40frames, no gaps or faults, closed
ledgers and verified cleanup.

A subsequent WHOLE-lock Terra60 pair uses one immutable package and seed147147,
changing only that cache bypass. Native instrumentation and index comparisons
are off; managed publication laps are deliberately on. Each run has60owner
samples/59intervals with no gaps, faults, retries or skipped relaxation;
fallback count remains unavailable. Package hashes match before and after.

| Public-cell map | Owner mean / p95 / max ms | Observed mean / p95 / max ms | Mass / energy error | Trace sink kg / J |
| --- | ---: | ---: | --- | --- |
| Original calculation | 204.891 / 226.328 / 262.522 | 250.482 / 361.558 / 765.871 | 6.940e-16 / 7.551e-16 | 1.22055e-7 / 0.1270655 |
| Cached geometry | 198.195 / 219.398 / 248.868 | 244.009 / 300.531 / 786.563 | 3.750e-16 / 7.183e-16 | 1.19237e-7 / 0.1278435 |

In frames10–59 overlay cost per publication is0.941660→0.845575ms (10.20%
lower in this pair). Publication counts differ432→379, while snapshot cost
per publication stays1.04175→1.05300ms and receipts cost8.585→8.523ms per owner.
One baseline view-copy lap stalls153ms; its cause is not yet attributed.
A separate mapping-only .NET10 six-pair AB/BA control gives0.262992→0.129417ms
for98,304cells with identical destinations. The live pair is potentially GPU
contended by vLLM and includes managed instrumentation; its whole-frame means
are diagnostic observations, not an isolated gain or2× acceptance.

For the unexplained long view-copy laps,
`MA_COLONY_VIEW_TIMING_LOG=1` separately records backend mutex wait and native
copy/status work, with the call/session/frame and copy outcome. It preserves
the post-call floating-point environment and errno. Existing managed
publication laps annotate increases in the process collection count between
stage boundaries; that is a correlation, not a measured GC duration. Native
printing is outside the native elapsed interval but inside the caller's lap.
Both diagnostic flags must be off in final timing runs. Release and backend
controls pass with timing off/on; active-game stall attribution is pending.

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

### Performance log

Every owned colony writes a compact summary to Player.log (Windows:
`%USERPROFILE%\AppData\LocalLow\Klei\Oxygen Not Included\Player.log`): one
`perf header` line with the OS, CPU and memory, then one `perf frames` line per
150 owner frames (30 s of game time) with the owner step's mean/p95/max, its
mean stage times (edits, heat, transfers, flow, space sink, ledger and
projection), the real time between completed steps, game seconds per real
second, the clock scale, Unity's frame rate and its slowest frame. The status
reports only the newest step's timing, so when a slow Unity frame sees two steps
finish the earlier one goes unsampled; each line covers its fixed 150 frame
numbers and ends with `samples`, how many of them it timed.
`MA_PERFORMANCE_LOG=0` turns it off.

## Saves

**Native loop record reserved, 2026-10-27**, immediately after fetching
`f6ed4fe`: MACOLP03 section5 will hold `MACNRC01` schema1, the native game's
loop context needed by its retained nonfluid services. Its canonical 40-byte
record is eight magic bytes, uint32 schema1, int32 native padded width/height
and game-cell count, uint32 RNG state, int32 iteration/displacement directions
(each -1 or +1), uint16 tick count and uint16 reserved zero. The dimensions
must match the live native world; game-cell count is the padded interior area.
No native addresses, handles or process-local ordinals are stored.

The writer stays behind `MA_COLONY_NATIVE_CLOCK_CONTROL=1`, requiring the
existing exact-storage/native-schedule development control. A saved section
selects the same control on load. Capture runs at the completed native frame
barrier. Restore is requested on the main thread and applied on the native
worker in a zero-time frame, after prefab startup messages settle and before
positive time. Ordinary payloads and owner wrapper13/core23 bytes stay
unchanged. This reservation covers loop state only; managed scheduling and
native thermal state require their own continuation work and proofs.

The gated writer now implements this section. Native clock ABI1 exposes a
completed 36-byte typed capture and a queued restore; it retains no native
pointer between hooks. Every input and live geometry is validated before any
field is written. Original message processing completes first; pending writes
refuse positive time and are cleared on shutdown. Zero-time frames do not run
`UpdateData`, so the capture also samples their completed `ProcessNextFrame`
boundary. The main thread fences and verifies the restored fields before
resuming. Ordinary saves omit section5 and retain their prior bytes.

`release-native-clock-source-v2` passes the seven-check drained-reservoir
control. Cold and menu loading in `release-native-clock-load-v2` restore every
saved counter; the independent native trace records both zero-time restores
from tick0 to saved tick40 before positive time. All eight complete pipe
images remain byte-identical across source/cold/menu. The owner still first
differs at frame41: breathing and solid-heat inputs remain different. The
earlier load lacking zero-time capture is retained as a failed control.

WindowsPlayer/Proton also passes all seven checks and cold/menu loading
(`release-native-clock-windows-source` / `windows-load`). Saved and restored
loop fields match, and its eight complete pipe images also match in all three
branches. The runner records `--native-clock-control` as a development
override requiring the exact-storage control. This is not Windows OS evidence.

The Ubuntu 22.04 build also passes this source/cold/menu control on the Linux
development host (`release-native-clock-compatible-source` / `compatible-load`),
with matching loop fields and all eight complete pipe prefixes. Both native
libraries meet glibc2.34, and all 89 container CTests pass. This does not claim
an Ubuntu 22.04 Unity run. Focused native controls verify bounded writes,
original-failure retry, reset, positive-time refusal and 1,000 threaded
handoffs; the observer test executable also passes under ThreadSanitizer.
Linux and Windows/Wine full native suites each pass 89 tests; managed codec,
228 snapshot checks, both platform builds and Python436 (nine skips) pass.

Mixture loading also scopes native load-transition checks to preserve the
saved projection before its owner is active. ONI otherwise condenses six
sampled Rock Gas projections into Magma and adds 1.5 K while loading, using
its fixed transition temperature rather than the owner's pressure-based law.
`ColonyNativeLoadGuard` brackets the verified native load in a `finally` scope.
The native guard validates the saved rectangle, excludes its padded border,
rejects nested/foreign tokens, and restores normal forwarding on scope exit.
It grants no ownership and suppresses no ordinary positive-time update.

The private cell observer (`MA_ONI_RUNTIME_CELLS`, comma-separated public cell
IDs, with the runtime context trace) records raw old/updated element, mass and
temperature fields before/after message processing. Both native vector layouts,
signed-zero bits, short vectors and unchanged inputs have focused controls.
The 180-cell Linux comparison shows no old/updated difference at the source
save boundary. With the guard, all six gas cells retain their exact identity
and temperature; five solid temperatures still change during startup's second
zero-time message frame. Full world continuation therefore remains open.

`release-load-guard-source` passes the seven-check drained-store control;
`release-load-guard-load` passes cold/menu loading and guards 98,304 checks per
load. This is a count of skipped checks, not 98,304 physical phase changes.
WindowsPlayer/Proton passes the same path (`release-load-guard-windows-source`
/ `windows-load`) and preserves all eight complete pipe prefixes. Ordinary
Linux schema4 source/cold/menu checks also pass with experimental scheduling
and clock controls off (`release-load-guard-default-source` / `default-load`).
Ordinary Spaced Out! WindowsPlayer/Proton generation and cold/menu loading
also pass with no owner overrides: nine worlds, both controls off, matching
restored owner images, and 223,744 guarded checks per load
(`release-load-guard-spacedout-source` / `spacedout-load`).
Guard controls cover exact bounds, token lifetime, overflow geometry and
40,000 parallel calls, including the ThreadSanitizer observer executable.
Host Linux, Windows/Wine and Ubuntu 22.04 container builds each pass all
89 native tests. The compatible libraries pass the same seven-check source
control in host Unity; this does not establish Unity execution on Ubuntu 22.04.

**Reserved 2026-10-27, immediately after fetching `f3d5390`:** owner wrapper
`MACOLN01` version **13** and MACOLP03 section **4** (`MACCBR01`) are assigned
to callback-owned requests in flight at a save ([below](#callback-owned-requests)).
Wrapper 13 encloses an unchanged base wrapper 8–12 and is written only when such
a request is queued; every other save keeps its base wrapper and bytes. Older
builds refuse both. No physical `MACCLS01` version changes.

**Reserved 2026-10-26, immediately after fetching `3cce360`:** owner wrapper
`MACOLN01` version **12** is assigned to the conservative airflow boundary law
for new game-kernel owners. It retains wrapper11's layout, including an empty
precision-inventory block when unused. A liquid crossing discovered during a
trial closes its incident face on the original interval state; the complete
pressure/work solve repeats before any trial output is committed. Wet marked
cells are isolated at tick entry, including after condensation in a merged
interval. Wrappers8–11 retain their existing boundary law and bytes. Core23,
profile5 and its numerical equations remain unchanged. Native controls pass
water blocking, gas passage, merged condensation, M/E/J and new/old cold
continuation. In-game acceptance remains pending.

**Reserved 2026-10-25, immediately after fetching `3391741`:** owner wrapper
`MACOLN01` version **11** is assigned to durable precision-overflow inventory
for passive-free precise gas admissions. Each record preserves its original
source/radius, mass and signed energy corrections, including energy remaining
after mass release. Native-held inventory and actual grid credits must close
together through refusal, release and cold continuation. Legacy reservoir
behaviour and core23/profile5 arithmetic stay unchanged; an unused facility
retains existing wrapper bytes under the older boundary law. The original
WindowsPlayer pump-retirement check and ordinary save/cold continuation pass.

The implemented owner block follows the version-10 germ section (mode zero is
also valid in wrapper11) and precedes the core image length. It is a u32 byte
length followed by `MAPREC01`, u32 record count, then each record's u32 original
public source, public anchor, radius (2), native species index, binary64 mass,
binary64 nominal source temperature, and canonical `MacEnergySum` total U+K+G.
Each sum uses the existing parcel-wire count and mantissa/exponent components.
The block is bounded to 16 MiB and 65,536 records. Wrapper11 is sticky after
first use, including after the inventory empties; older-boundary owners that
never use it retain their existing 8/9/10 writer and bytes. New boundary-law12
owners include this block even when empty. The core remains version23.

Only durable precise, stationary, passive-free gas injections can enter this
inventory, after every legal original-radius destination refuses its mass as
unrepresentable. Admission transfers actual mass and represented energy to
native ownership and books the external ledger once. `MARECP02` describes
transfer to the native owner (grid plus this durable inventory), not necessarily
a changed grid cell. Its passive observation stays unchanged for held admission.
`MAPOUT01` continues to describe actual grid credits only. The existing source
job acknowledgement and committed-outcome save barrier apply unchanged.
Grid readings remain grid
readings; the separate published inventory and checkpoint expose held state.
The owner mass and energy audit includes both. Identical source/anchor/species/
temperature records coalesce only when their binary64 mass sum is exactly
representable; energy additions retain every expansion term.

Every owner frame retries the original source's current open-cell radius-two
BFS through the existing moving-parcel law. Actual grid mass and U+K+G credits
debit held inventory without a second external receipt or ledger input. The
stationary law must credit zero momentum apart from its existing wall impulse.
The nominal temperature remains separate from the signed energy balance, so
partial mass release cannot turn a primary-write correction into an extreme
material temperature. Zero-mass signed energy remains durable and retries the
ordinary heat law, accepting only an exact reduction in its magnitude. There
is no epsilon pruning, wider search, invented trace sink or permanent-release
shortcut. Unsupported passives and liquids retain their prior refusal: this
change adds neither a held-culture clock nor a new entropy or heat model.

**Assigned 2026-10-24 by main after a fetch at `3eaccfa`:** owner wrapper
`MACOLN01` version **10** holds world germ evolution ([germs](#germs)): after the
version 9 job list, u32 mode, u64 biology frames, the culture clocks, the queued
germ edits and the per-identity germ accounts. Legacy-mode owners keep writing 8/9.

**Reserved 2026-10-23, immediately after fetching `7e3db57`:** owner wrapper
`MACOLN01` version **9** is assigned to durable explicit-energy/momentum
moving-parcel jobs and their full passive inventories. The parent gameplay
worker owns the additive ABI, owner queue/receipt framing and version-9 codec;
Codex owns conservative solver admission. A physical mixed parcel commits or
refuses as one ordered batch; refusal consumes nothing and leaves the exact
managed reservation available for retry or return. Existing mass/temperature
jobs and version-8 bytes remain unchanged when the new facility is unused.
The explicit native queue now writes wrapper9 after its first admitted moving
job and preserves the canonical request through cold continuation. Completed
outcomes must be settled before saving. Production managed callers and full
mixed-save gameplay acceptance remain gated. Core physical checkpoint versions
are assigned separately.
The checked full-range request/outcome formats and integration boundaries
are specified in [moving-parcels.md](moving-parcels.md).

The read-only `ma_oni_colony_parcel_context_version()` capability 1 and
`ma_oni_colony_copy_parcel_context()` expose the actual coordinate reference
without enabling moving-parcel admission. The 72-byte record contains its
size/version, saved flags, reserved zero, session, public-grid and owner
geometry, cell width/height and saved solver gravity from one immutable
publication. Potential energy is **core-local**: a public cell's centre is
`(public_y - offset_y + 0.5) * cell_height_m`. The managed particle owner must
convert its continuous global pose with that same saved origin and gravity;
no hard-coded 9.81 or second global-height correction is allowed. Invalid or
inactive queries leave the caller's record unchanged. Queries preserve
checkpoint bytes, and a restored session reports the same physical reference
with its new session identity. This provides metadata only; parcel admission,
durability and full liquid ports remain separate acceptance.

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
and a SHA-256). Inputs in flight at the save keep their queue place. A request
that answers a game callback (a breath, a consumer's draw, an emitter's answered
output) keeps it too: owner wrapper 13 stores it under an ordinal and MACOLP03
section 4 names the persistent actor that answers it (below). Older wrappers
dropped such an extraction and kept such an injection unanswered.

<a id="callback-owned-requests"></a>**Callback-owned requests (wrapper 13, MACCBR01).** A callback index or actor
handle is session-local, so the save names the actor instead: the component
(prefab instance, exact type, ordinal among that type's components) or state
machine instance holding the callback data, plus the callback method
(`ColonyCallbackRequests.cs`, codec `ColonyCallbackRequestImage.cs`). A breath
or other one-shot consumed-mass callback is registered again on the restored
actor; an emitted-mass callback reuses the restored component's own
persistent handle; a consumer's draw goes to the restored element consumer's
new handle. Rebinding happens before positive time: until every restored
request is bound, a positive tick runs as a paused zero-time tick, so actor
registrations can arrive (after 600 such ticks the rest is released). A request
whose actor has no persistent identity is released exactly as an older save
did and counted. Owned element consumers and emitters run in the order of these
identities, so a loaded colony queues them as the uninterrupted one did
whatever order they registered in. A save with no callback-owned request keeps
its base wrapper and bytes. Wrapper 13 is `MACOLN01`, 13, the base wrapper
version (8–12), the request count, then the base layout unchanged; every queued
transfer with a callback or consumer flag must carry the next ordinal. The
native controls (`ma_oni_colony_tests --callback-rebind-only`) prove that a
rebound restored owner reproduces the uninterrupted receipts and owner bytes,
that releasing reproduces the older policy, and that corrupt ordinals refuse.

`MA_COLONY_INPUT_JOURNAL_DIR=<directory>` (diagnostic, default off) writes every
committed owner input batch, in commit order, to `inputs-<session>.txt` with
exact hexadecimal values. `tools/colony_continuation.py --source-inputs/
--loaded-inputs` compares two branches' journals, ignoring native tickets and
callback values, and reports the first frame whose inputs differ; a state may
differ only from that frame on. The journal also writes the game's disease
table (`diseases-<n>.bin`), each durable job's passives or moving payload and
the number of native frames in each owner frame.

`ma_oni_colony_replay SAVED.owner JOURNAL DISEASES REFERENCE_PREFIX FRAMES
[--menu]` proves owner continuation under identical inputs: it restores an
actual saved owner image in a fresh process (a cold load) and, with `--menu`,
again after stopping it in the same process (a menu reload), rebinds the
restored callback-owned requests, feeds each restored owner exactly the
uninterrupted branch's journaled inputs, and requires every frame's complete
owner state (ledgers, counters, germ clocks and accounts, held precision
inventory and the physical image; not the next frame's queued inputs) to equal
the uninterrupted branch's `MA_COLONY_CONTINUATION_DUMP` image byte for byte.
The restored queue must be the start of the first resumed frame's inputs. Its
`--self-test` (CTest `oni_colony_replay_self`) journals a synthetic owner with
a breath, a consumer and unanswered and answered injections, saves it with
those requests in flight, and replays it cold and after a menu restore; the
negative control, releasing the in-flight requests as the previous policy
did, must diverge at the first resumed frame.

The owner steps one frame ahead of what the game has seen, so at a save its
last completed frame still has receipts the game has not received.
`ColonySaveBarrier` runs at `SaveLoader.Save(string, bool, bool)` entry, before
ONI touches any file: it posts ONI's own zero-time sim step (`UnsafeSim200ms(0)`,
as every paused frame does) and waits until ONI's sim thread has ended that
native frame and every earlier one (observer next-frame/end-frame counters), so
no positive frame is still being handed to the owner. It then waits for the
solver thread and publishes through `StepTheSim(0)` until no receipt is pending,
so every callback, consumer mass and byproduct is dispatched by the game's
normal code before the native section is captured. Zero time submits no owner
frame; the capture must find the same frame.
A save requested inside an unfinished `Game.StepTheSim` is deferred through
`Game.StartDelayedSave`. The payload is MACOLP03 when something is in flight
outside the owner image (docs/mixed-pipes.md#saved-framing), otherwise exact
MACOLP01. ONI does not save `CO2Manager` particles, whose mass is in no ledger
until they deposit; the save keeps every active one as a MACO2P01 image and a
load recreates them through the original factory before the manager's first
update or spawn, requiring a byte-identical re-encoding. A colony whose
breathing settings differ from the defaults adds section 3
([settings](settings.md#save-record)); Default colonies write none.

A mixed-pipe colony adds MACOLP03 section 1, the pipe runtime's own
MACPIP01 schema-3 image (docs/mixed-pipes.md#saved-framing), captured after the
owner checkpoint and before ONI serializes conduit contents. A loaded image is
restored right after the owner, once the game has started, and always before
positive time or any conduit update. If the owner or pipe restore fails
(different catalogue or geometry, unknown version, a pipe projection that
differs from ONI's conduit contents), `ColonyLoadRefusal` holds simulation time,
shows "Colony not loaded" with the reason and returns to the main menu; the
colony never continues on ONI's own fluid simulation and the file is unchanged.
The load menu checks a recognized mixture save before leaving the menu and
reports incompatible ONI/material versions, payload sections and owner formats.
Unrecognized files continue through ONI's loader. The refusal harness accepts
only an explicitly expected refusal with a reason and no active owner.

Loading hands the native bytes to ONI's loader and restores the
owner at the first completed frame; a different catalogue or geometry rejects
the save. A game without the mod passes the envelope to its native loader,
which refuses it.

A restore reports the physical profile it actually runs, read from the owner
image (`ColonyCheckpointMetadata.Profile`: `MACCLS01` version and, for cores
20-22, the saved gas heat mixing, liquid law and pressure/heat caps), never
from config flags.
Under validation the restored owner is re-checkpointed immediately and must
reproduce the saved image byte for byte; a same-process menu reload must
restore the same profile and image hash. The fresh core21 base save
(`base-core21-d964`, frame 32) passes a cold load and a menu reload in one
process: both sessions restore core21, law 1, caps 400/800, identical image
hashes and re-encodings, and continue 30 frames each; the source hash is
unchanged.

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

**Starting air:** generated oxygen and polluted oxygen are imported without
nitrogen conversion. The removed nitrogen element registration and starting-air
ABI are no longer shipped. Saves containing that catalogue are refused before
native loading; older R&D saves need not be preserved.

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
through the game's own cell messages (its inner walls and floors with the seal),
fills it two frames later, runs some owner
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
| `byproduct-merge` | A phase-change byproduct delivered to a cell whose first debris item was just absorbed (and awaits destruction) keeps its mass as a separate item. Focused control; not in the acceptance batches. |
| `geyser` | A steam geyser shifted to its active period erupts into the owner. |
| `rocket-exhaust` | A steam rocket engine put into its burning state (as at launch) exhausts hot steam into the owner. |
| `meteor` | A rock meteor falls through the arena's air and strikes its floor with the owner's ledgers closed. |
| `sensor` | A gas element sensor set to carbon dioxide switches on in a cell where carbon dioxide is a minority (the cell shows oxygen). |
| `filter` | A pump sends oxygen and carbon dioxide through a carbon dioxide filter: its filtered port carries only carbon dioxide, its output only oxygen. |
| `pacu` | A pacu in an owned pool stays in its water. |
| `liquid-pump` | A liquid pump in a pool sends its water through a pipe into a reservoir; pump, pipe and reservoir hold what the owner lost. |
| `world-germs` | Slimelung and food poisoning in one polluted-oxygen cell each follow their own rule and spread; slimelung beyond a wall in oxygen follows its rule; the germ accounts close (owner mode). |
| `germ-oracle` | With `MA_COLONY_GERM_ORACLE=1`: native code evolves six sealed single-cell cultures on the shadow (growth, over-density decay, heat death, extinction) and the owner predicts every step exactly. |
| `skimmer`, `terrarium`, `deodorizer` | A carbon skimmer, algae terrarium or deodorizer in a two-gas mixture takes only its element; outputs reach the owner or its storage. |
| `rust-deoxidizer`, `coal-generator`, `oil-refinery` | Oxygen and chlorine, exhaust carbon dioxide, or natural gas reach the owner; the mixture's other gases are kept; petroleum stays stored. |
| `electrolyzer-blocked` | An electrolyzer in a mixture above its overpressure limit takes no water and draws no power. |
| `hazards` | Duplicants in hot air, dense air and water are scalded, get popped eardrums and hold their breath. |
| `germ-exposure` | A duplicant breathing air carrying slimelung is exposed. |
| `fault-containment` | A throw injected into each per-frame owner and pipe path is contained without stopping the game's step; a last, real one faults the owner once: it and its pipes freeze, a heater built afterwards still gets power and saving is refused. Run it last in a batch. |

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
- Save controls (environment, colony-owner modes): `MA_COLONY_SAVE_CONTROL`
  runs, from half the frames (with a scenario: at `MA_COLONY_SAVE_FRAME`) and
  in order, `save` (an ordinary paused save of whatever is in flight),
  `particle` (a known CO2 parcel in flight at an ordinary paused save,
  `particle.sav`), `autosave` (the game's own `GameClock.DoAutoSave` path while
  the colony runs) and `io-failure` (a save whose destination is a directory:
  the write fails, nothing is left there and the owner keeps running).
  `MA_COLONY_CONTINUATION_FRAMES=N` holds the sim tick after each of the first
  N positive frames following a restore and records the owner image hash at
  exactly restored + k, per session of a menu reload.

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

Owner telemetry and the overall validation report are replaced atomically so
concurrent measurement readers see complete documents. A fault writes its final
state/error and last completed-frame counters before validation stops; it does
not leave a success-looking report from the previous frame.

Environment variables of the game process (the harness passes them through):

- `MA_COLONY_FLOOR_ADMISSION_DUMP=path`: F4 writes a private `MAFLRQ01`
  marker describing its exact requested binary32 fill and border. Before native
  UpdateData, the backend waits for either raw native array to match every
  requested tuple and writes both arrays once, without edits. The fresh-path
  `MAFLPR01` capture stores six u32 geometry fields, u64 native ticket, binary64
  step seconds, u32 matching-array mask and count, then each u32 public cell
  and both arrays' u32 element/float mass/float temperature. The request has
  the same geometry, u32 count and one such tuple per cell. Missing matching
  input is not admission; this diagnostic establishes no floor pass or owner
  heat alignment by itself. Leave it unset in timing runs.
- `MA_COLONY_KERNEL3=1`: explicitly request the diagnostic flag-4 game kernel
  for a **new** colony. It is exclusive with `MA_COLONY_GAME_FLOOR=1` and
  requires native capability `ma_oni_colony_game_kernel_version() == 1`;
  older bundles refuse it clearly. Normal defaults are unchanged. Loads ignore
  both new-colony selectors and restore the save's recorded flags/timestep.
  The backend's implementation and acceptance are separate solver work; this
  selector does not establish a new-profile or performance pass.
- `MA_COLONY_TRANSFER_DUMP=path`: on an individual fluid-transfer failure,
  write the exact pre-edit material/world image and a `path.request` sidecar.
  Both paths must be fresh; existing evidence is never overwritten. The world
  has the same material-record/timestep/checkpoint framing as flow dumps.
  The little-endian `MATREQ01` request contains failed frame (u64), actual
  config flags and topology (u32 each), local cell and registry species (u64),
  injection and passive-observation flags (u32), binary64 mass and temperature,
  then passive count (u32) and identity/quantity pairs (u64/binary64).
  This captures failures before flow, including legacy trace extraction faults.
  Verify both files and the checkpoint checksum before replay. Capture failure
  leaves the original owner fault intact. Successful frames do no capture work;
  physical arithmetic and checkpoint bytes are unchanged. Leave it unset for
  timing runs. Owner reports also expose actual config flags and maximum step;
  do not infer a saved profile from a fixture name (pipe controls use flags 1).
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
  Nonempty receipt deliveries also split native copying, dispatch, byproduct
  creation, event appending, acknowledgement and managed settlement; empty
  copies over 0.1 ms are logged separately.
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
