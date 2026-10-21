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

A cell that native turned solid is left to the terrain edit. The status reports
the booked heat, mass in and out, and the number of changed cells. A shortfall
(native took more than the owner cell still holds after its step) is not yet
reported.

## Terrain

Digging (ONI's `Dig`) opens the cell as exact vacuum and refills it from its gas
neighbours: with m such neighbours each gives 1/(m+1) of its gas, so the cell
starts at their pressure. Building a tile (an element replacement with a solid)
displaces the cell's contents equally into its open neighbours and closes it;
the normal momentum on its faces becomes heat of the neighbour that carried it.
A sealed cell's contents go to the reservoir. Replacing a cell with a fluid
element opens it if needed and books the placed fluid. Moved material keeps its
U+K+G: each receiving cell gets it at the temperature whose internal energy
matches, after the potential at its height.

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
original native bytes and the colony checkpoint (`MACOLN01`: configuration,
material records, space mask, reservoir, ledgers, the `MACCLS01` physical image
and a SHA-256). Loading hands the native bytes to ONI's loader and restores the
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
3. **Gas.** In each region the gas is laid out again at one pressure without
   mixing: ONI keeps one element per cell and its own simulation sorts gases by
   density, while spreading every species through a region would mix a room's
   CO2 into its air (Terra's starting room holds 33 kg of CO2 at its floor,
   12.7 mol% of its gas, which mixed would be unbreathable). The region's gas
   keeps its heat at one common temperature; each species takes the volume
   n R T / p of a layer, the layers fill the region's gas room from the
   bottom, heaviest molar mass first, and only cells where two layers meet hold
   a mixture. This also fills headspaces and vacuum pockets.
4. Liquid masses depend on the gas pressure above them, so steps 2 and 3 repeat
   three times.

The import ignores the small hydrostatic gradient within gas.

**Control.** `ma_oni_colony_import_tests` (CTest `oni_colony_import_settle`)
settles a made-up world with an overfull pool, a high-pressure pocket, a sealed
overfull oil pocket, a vacuum pocket and open sky, checks conservation, the
reservoir, the level, one pressure per region and the density layering, then
advances the settled state 0.2 s and requires it to stay near rest.

**Measured on Terra (seed 147147, private capture, standalone).** 30,886 open
cells in 1,297 regions; the settle takes about 130 ms, leaves a relative
pressure spread of 6e-16 within regions and a species mass error of 1e-16, and
books 656 t (sealed crude oil and magma) to reservoirs. The starting room keeps
its CO2 as a floor layer under oxygen. The settled asteroid then takes full
0.05 s production steps after the first frames.

The reservoir's release (liquid seeping back as its region gains room) is not
implemented yet.

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
