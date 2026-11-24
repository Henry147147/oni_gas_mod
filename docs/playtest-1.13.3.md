# Mixed Atmospheres 1.13.3

1.13.3 is [1.13.2](playtest-1.13.2.md) with one fix from the user's Windows
playtest of 2026-11-24. Everything in the 1.13.2 notes applies: the speed
work, reservoirs that give out their mixture, the faster solver defaults.

## The fix: liquid traces in mid-air no longer draw liquid tiles

The recording shows yellow-green liquid tiles hanging in the air of a ladder
shaft and at the top of a room. The hover over two of them reads "Liquid
volume 0.00119 % (Polluted Water, Water)" in a cell of 1.75 kg of gas, and
0.00238 %: 12 and 24 g of liquid.

- **Cause.** Since 1.13.0 a tile shows its liquid from 0.001 % of its
  volume, wherever the tile is. The solver, though, gives a liquid a fall of
  its own only from 0.1 % of a cell; less than that is mist that travels with
  the cell's gas, and a colony's air holds some in thousands of cells. 1.13.0
  made that mist visible as liquid tiles.
- **Fix.** A liquid below 0.1 % of its tile (about 1 kg of water) shows only
  where it rests: on a solid tile, on an airflow tile, or on a pool. In
  mid-air the tile shows its gas; the hover still reports the liquid. From
  0.1 % a liquid shows wherever it is, so a falling stream is still liquid
  ([the shown phase](colony-owner.md#the-shown-phase)).
- **What stays.** Films and puddles on floors still show from about 10 g, as
  decided on 2026-11-23 (they can be mopped). On the November 22 colony 1,027
  tiles show such a resting film and 105 that were shown in mid-air now show
  their gas.

The solver's results are unchanged: only which element the game's grid shows
for those tiles.

## Checks

| Check | Result |
| --- | --- |
| Native control `shown phase by support` (new): 50 g of water in mid-air, 2 kg in mid-air, a film on a full pool | air, water, water |
| Native control `shown phase` (films on a floor, a trace, a pool, a drop) | unchanged |
| November 22 replay | the fingerprint of 1.13.2 (`909b034e724d1520`); 105 views change their shown element and mass, nothing else |
| In game, build 763796: `shown-liquid` | 14 of 14 |

The staged package's own acceptance run is recorded under
[Staged package](#staged-package) once it has run. The load matrix on the
three supported game builds passed for 1.13.2 (6 of 6) before that run was
stopped for this fix.

## Staged package

Not staged yet.
