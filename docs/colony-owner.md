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
  identities are announced to ONI's grid caches. `Grid.Pressure` returns the
  owner's mechanical pressure.
- **Transactions.** Mass emission, mass consumption (radius), additive cell-mass
  edits and cell heat in the asteroid go to the owner and commit at the start
  of the next native frame, with consumed/emitted callbacks returned through the
  copied game data. Extraction takes up to the requested mass from the open
  cells within the radius, nearest first, in a binary32-exact total; injection
  goes to the cell or the nearest open cell within two.
- **Native bodies and actors** ([below](#native-bodies)): ONI's own heat and
  element-consumer/emitter code meets a shadow of the owner's cells and its
  changes are booked to the owner.
- **Ledgers.** Every species and U+K+G are reconciled each frame against the
  imported inventory plus every booked flow (injections, extractions, the
  space sink, heat); drift beyond 1e-8 (mass) or 1e-6 (energy) faults the owner.
- **Open sky.** Whatever reaches a Space cell leaves through the sink, booked.
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
volume) and hands the frame to the mixture presentation runtime. The gas
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
- same element, less mass: an extraction at that cell; more mass: an injection
  at the emitted parcel's temperature;
- another fluid element: the old contents extracted, the new injected.

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

## Element consumers

ONI's native element consumers (pumps, terrariums, skimmers, plants and
critters that breathe) read one element per cell, the owner's projection, so a
carbon dioxide consumer in an oxygen-dominated mixed room would find none. The
owner runs every consumer instead (`ColonyConsumers.cs`): ONI still registers it
natively, for its handle, but its rate messages (`SetElementConsumerData`) stop
at the mod, so the native consumer stays at rate zero. At each positive native
frame each consumer with a positive rate queues rate × dt as an owner
extraction within its radius: its element, or for an any-gas or any-liquid
consumer (pumps) the whole mixture of that phase (`MA_ONI_COLONY_EXTRACT_GAS`,
`_LIQUID`), each cell's share split over its constituents. The receipts carry
the consumer flag and return as ONI's consumed-mass events for the handle, one
per constituent, so a pump's storage holds the mixture it drew.

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

## Terrain

Digging (ONI's `Dig`) opens the cell as exact vacuum and refills it from its gas
neighbours: with m such neighbours each gives 1/(m+1) of its gas, so the cell
starts at their pressure. Building a tile (an element replacement with a solid)
displaces the cell's contents equally into its open neighbours and closes it;
the normal momentum on its faces becomes heat of the neighbour that carried it.
A cell inside a block closing at once sends its contents to the nearest
open cell through the block (within 16); only a sealed cell's contents go to
the reservoir. Replacing a cell with a fluid element (ONI's `ReplaceElement`)
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

## Saves

ONI's native section of the save becomes a `MACOLONY` envelope holding the
original native bytes and the colony checkpoint (`MACOLN01` version 5:
configuration, material records, space mask, reservoir, ledgers, continuing
counters, the inputs queued for the next step, the `MACCLS01` physical image
and a SHA-256). Inputs in flight at the save keep their mass but not their
callbacks, which do not survive a load: an injection (exhaled gas, an emitter's
output) and native-side changes are kept; an extraction answering a callback (a
breath, a consumer) is dropped and asked again after the load. Loading hands the native bytes to ONI's loader and restores the
owner at the first completed frame; a different catalogue or geometry rejects
the save. A game without the mod passes the envelope to its native loader,
which refuses it.

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
   temperature; it is never deleted. Kept cells keep their temperatures, shifted
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
| `door` | A pneumatic door between oxygen and vacuum seals when closed, passes gas when opened and displaces its cells' gas when closed again. |
| `sublimation` | Oxylite dropped in thin oxygen releases oxygen into the owner. |
| `falling` | A sand tile placed over gas falls: its cell reopens, it lands and closes a floor cell, and the gas is kept. |
| `breathing` | The colony's duplicants keep full breath; oxygen is taken from the owner and carbon dioxide exhaled into it. |
| `airflow` | A column of airflow tiles between water under air and thinner air passes the air but no water. |
| `electrolysis` | An electrolyzer splits stored water into oxygen and hydrogen that reach the owner. |
| `vent-overpressure` | A pump feeds a vent into air above the vent's limit: the vent stays shut and the pipe holds the gas. |
| `space-heater` | A powered space heater warms the arena's air. |
| `bottle-emptier` | A gas canister emptier releases its stored carbon dioxide into the owner. |
| `gas-generator` | A natural gas generator burns stored methane; its carbon dioxide exhaust reaches the owner. |
| `germs` | Slimelung released with polluted oxygen spreads with the gas. |
| `puft` | A puft breathes the arena's polluted oxygen out of the owner. |
| `plant` | A wild mealwood in owned oxygen reads normal pressure and a safe atmosphere. |
| `geyser` | A steam geyser shifted to its active period erupts into the owner. |
| `rocket-exhaust` | A steam rocket engine put into its burning state (as at launch) exhausts hot steam into the owner. |
| `meteor` | A rock meteor falls through the arena's air and strikes its floor with the owner's ledgers closed. |
| `sensor` | A gas element sensor set to carbon dioxide switches on in a cell where carbon dioxide is a minority (the cell shows oxygen). |
| `filter` | A pump sends oxygen and carbon dioxide through a carbon dioxide filter: its filtered port carries only carbon dioxide, its output only oxygen. |
| `pacu` | A pacu in an owned pool stays in its water. |
| `liquid-pump` | A liquid pump in a pool sends its water through a pipe into a reservoir; pump, pipe and reservoir hold what the owner lost. |

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
