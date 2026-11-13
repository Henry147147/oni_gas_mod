# Private playtest 0.1.8, 2026-11-13

Version 0.1.8 follows [0.1.7](playtest-0.1.7.md) with a probe of everyday
gas situations run on the solver alone, as the probe of liquids before
[0.1.6](playtest-0.1.6.md). It keeps everything in 0.1.7. It is a private
candidate; no public release or Workshop upload was made.

## What the probe found

Eleven situations were built on the game kernel with the mod's catalogue
(oxygen, carbon dioxide, hydrogen and water with the game's values and the
molecular masses of 0.1.4) and measured. Most behaved:

- A two-tile door between rooms at 140 and 70 kPa: the pressures meet within
  two frames with one 6 % overshoot and no ringing, and the rooms are at the
  temperatures compression and expansion give (282 K and 319 K in theory,
  286 K and 315 K after a second), which then relax over minutes.
- A breach to vacuum: equal pressure in 0.6 s, no tile below 200 K or above
  400 K at any time.
- A vent filling a room: pressure even within 200 Pa while filling, and with
  the default Compensated rule the room stays at 300.0 K.
- A 1 kW heat source: a plume at 0.3 m/s, the whole room 0.7 K warmer after
  five minutes, no tile above 301.3 K.
- A sensor's tile beside a door or a vent: smooth after the first three
  frames (at most 0.05 % a frame).
- Layers of carbon dioxide and hydrogen left at rest: they keep their rows
  for half an hour (seven millionths of the carbon dioxide above them).
- Carbon dioxide over oxygen through a one-tile hole in a floor: 40 % has
  come down after five minutes, about what a 1 m hole passes.

Four did not, or need a decision:

- **Two gases could not change places through an opening one tile high in a
  side wall.** A room of carbon dioxide beside a room of oxygen at one
  pressure, joined by one opened wall tile, exchanged 0.13 % in the first
  seconds and nothing after; hot and cold rooms behind a one-tile hole kept
  an 80 K difference for good. Fixed here.
- **Released gases mix more than they layer.** Carbon dioxide released at a
  ceiling ends 43 % in the bottom third of the room after half an hour
  (33 % would be an even mix); hydrogen released at the floor ends 42 % in
  the top third. Released where it belongs (carbon dioxide at the floor) it
  is 96 % in the bottom third after a minute and 76 % after fifteen. A puff
  of air through a room with resting layers stirs 14 % of a carbon dioxide
  layer into the room. Some of this is physics (a falling plume draws air
  in, and mixed gases do not separate again); some is the solver's
  first-order transport, which spreads a gradient about 400 times faster
  than diffusion while anything moves. The start dialog promises that gases
  layer by weight. Not changed here: how much layering the game should show
  is the user's call.
- **A pump cools the last of a room.** With the default Compensated rule a
  room pumped from one tile is at 298.8 K with two fifths of its gas left
  and 291 K with 10 kg (a fifteenth) left; that last 10 kg cools to 249 K
  as it expands into the emptied tile each frame. Not changed here.
- **A quiet region pauses with its gas still moving.** A region with no
  buildings, duplicants or plants is paused sixty seconds after its last
  touch whatever its gas is doing (the door's rooms stop at 291 K and 310 K
  and never reach 300 K). 0.1.7 keeps such a region stepping while its
  liquid levels; gas was left out on purpose, because gas rarely comes to
  rest. Not changed here.

## Changes

- **Gas passes a one-tile opening both ways.** Where two rooms of different
  gas density meet at an opening one tile high (the tile in the wall, or any
  tile of a corridor one tile high), the two tiles now swap gas at the pace
  of a lock exchange, the dense gas out along the floor and the light gas
  back above it
  ([game kernel](../native/MAC_GAME_KERNEL.md#gas-through-a-one-cell-opening)).
  The carbon dioxide room now loses 7 % of its gas to the oxygen room in a
  minute and 20 % in five (a two-tile opening passes 37 %, as before), and
  the hot and cold rooms close on each other. Gases of equal density still
  do not exchange through such an opening: only diffusion would mix them,
  and the game runs without it. Rooms more than one tile high and openings
  of two tiles or more are untouched.
- **Cheaper layering.** The timed runs of 0.1.7 looked slower, and replaying
  one world state of each full colony on each solver showed where: 0.1.7
  repeated layering five times in every region stepped once a second,
  because nearly every region overturns some gas. A long step now repeats
  layering only where liquid is exchanging, or gas through a one-tile
  opening; liquid over gas needs a litre of liquid (mist and droplets fall
  as they always did, and a colony's air holds them in thousands of
  tiles); and gases within a thousandth of each other in density do not
  count as different. The layering stage of the Spaced Out! replay went
  from 1.4 ms (0.1.5) to 6.4 ms and is now 3.5 ms, the sharpening stage is
  0.7 ms cheaper than in 0.1.5, and the whole flow advance is back at
  0.1.5's cost.
- **Scenario `side-opening`**, written by a helper.

Profile 12 arithmetic changes at side openings one tile high whose two
sides differ in gas density by more than a thousandth, where less than a
litre of liquid lies over gas (as before 0.1.6 there), and in steps longer
than a frame that exchange no liquid and no gas through such an opening
(one layering pass, as before 0.1.7). The checkpoint format and version 32
are unchanged, and 0.1.7 colonies load.

## Verification

On the working tree before the acceptance run:

- **Native.** 93 of 93 CTest tests. Of the 62 `ma_mac_*` invocations, 60
  print what 0.1.7 printed; `mac_production_kernel_oracle` differs only in
  timings, and `mac_game_kernel` gains the control's line, reads
  6,235.79 K where it read 6,241.27 K in the thin-gas heating control,
  whose leak is a one-cell hole, and moves two controls in their last
  digits (0.297073 for 0.297068 kg of air left in the drained room,
  1.11647 for 1.11646 kg at the tilted interface). Control: 17.4 % of a
  room's nitrogen is in the oxygen room beside it after 120 s (7e-9 %
  before), and rooms at 350 K and 250 K are 37 K apart after 300 s (100 K
  before), with mass and U+K+G closed. The probe's layered gases at rest
  keep their layers to the digit and its two-tile door run is unchanged.
- **In game** (Linux harness): `side-opening` 6 of 6 (18.4 % of the carbon
  dioxide is in the oxygen room and 20.5 % of the oxygen in the other after
  120 s; no liquid; ledgers 3e-15); batches 1 and 2 of the acceptance tier
  (terrain, heat, phase, freeze-melt, door, sublimation, natural
  sublimation, falling, airflow, debris heat, building melt) and the five
  liquid scenarios pass. With the cheaper layering, run again: the five
  liquid scenarios and `side-opening` pass in one batch (the sealed room
  98.7 % full, half full in 26 frames; 13.8 % of the carbon dioxide and
  16.2 % of the oxygen across, in a batch order that leaves the arena
  stepped once a second), batch 1 passes, and the frozen prepared save made
  by 0.1.4 loads and runs 300 frames (173 germ cultures, germ audit 1e-16).

- **Replays** (the flow advance alone on one captured world state of each
  full colony, 30 frames, 12 workers, locked cores; ms a frame):

  | Solver | Spaced Out!: flow / layering / sharpening | Industrial: flow / layering / sharpening |
  | --- | ---: | ---: |
  | 0.1.4 `152cb75` | 47.4 / 1.4 / 7.0 | 29.8 / 1.1 / 5.7 |
  | 0.1.5 `e3666a9` | 45.9 / 1.4 / 6.9 | 27.9 / 1.0 / 5.7 |
  | 0.1.8 as first pushed `e58fb3a` | 47.6 / 6.4 / 6.2 | 32.7 / 5.6 / 5.6 |
  | 0.1.8 with the cheaper layering | 46.9 / 3.5 / 6.2 | 26.8 / 2.6 / 4.9 |

  A replay has no owner inputs, so most of its regions go remote after two
  seconds; it shows the solver's cost on one state, not the game's speed.
- **Speed in game** (`7b60dcb`, the Linux harness as for
  [0.1.7](playtest-0.1.7.md#acceptance), the same day and the same other
  work on the machine): the industrial base 2.066 game seconds per real
  second (warmed windows: owner 63.2 ms, flow 50.7 ms a tick; 0.1.5 2.059,
  0.1.7 2.006), the Spaced Out! full colony 0.895 (198.1 / 177.2 ms; 0.1.4
  0.922, 0.1.5 0.905, 0.1.7 0.883). The industrial base is back at 0.1.5's
  figures; the Spaced Out! colony's differences are within the spread
  between windows of one run. That colony stays below 1× on this host (a
  Ryzen 9 5900X; the playtest PC is a 9950X) on every version measured that
  day.
- **Saves** (`7b60dcb`): a colony saved by this build after the `germs` and
  `pool-step` scenarios (233 germ cultures) loads and runs 200 frames; the
  frozen 0.1.4 save is in the in-game check above.

Not established: Windows itself, and the user's own look at a one-tile gap
between rooms in play.

## Open

- The three probe items above that were not changed, and everything open in
  [0.1.7](playtest-0.1.7.md#open).

## Acceptance

Pending.
