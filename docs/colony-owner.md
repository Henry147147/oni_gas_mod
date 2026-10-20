# Colony owner

The colony owner is the normal-game path: one `MacClosedWorld` owns every gas
and liquid on the asteroid, with one species per catalogue material
([material catalogue](material-catalogue.md)). It replaces the bounded
development rooms of the [configured domain](native-configured-domain.md) for
play.

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
3. **Gas.** In each region, gas moves along a breadth-first spanning tree until
   every cell with gas room has the region's pressure, at the cell's equilibrium
   temperature (a headspace takes its liquid's temperature). Moving a fraction of
   a cell's gas moves that fraction of each species and of its heat, so species
   are conserved exactly. This fills headspaces and vacuum pockets.
4. Liquid masses depend on the gas pressure above them, so steps 2 and 3 repeat
   three times.

The import ignores the small hydrostatic gradient within gas.

**Control.** `ma_oni_colony_import_tests` (CTest `oni_colony_import_settle`)
settles a made-up world with an overfull pool, a high-pressure pocket, a sealed
overfull oil pocket, a vacuum pocket and open sky, checks conservation, the
reservoir, the level and one pressure per region, then advances the settled
state 0.2 s and requires it to stay near rest.

**Measured on Terra (seed 147147, private capture, standalone).** 30,886 open
cells in 1,297 regions; the settle takes about 130 ms, leaves a relative
pressure spread of 2e-12 within regions and a species mass error of 1e-16, and
books 656 t (sealed crude oil and magma) to reservoirs. The settled asteroid then
takes mostly full 0.01 s production steps.

The reservoir's release (liquid seeping back as its region gains room) is not
implemented yet.
