# Private playtest 0.1.6, 2026-11-12

Version 0.1.6 continues the testing asked for after
[0.1.5](playtest-0.1.5.md): a probe of everyday liquid situations run on the
solver alone, the open germ check of the last three acceptance runs, and the
one new failure of 0.1.5's run. It keeps everything in 0.1.5. It is a private
candidate; no public release or Workshop upload was made.

## What the probe found

Ten situations were built on the game kernel with the game's catalogue and
measured (digging under and beside a pool, a dam break, a waterfall, a drain,
communicating vessels, wide pools, two liquids, a pump, warm water, stray
droplets). Seven behaved: a dam break is level in 11 s without overshoot,
vessels equalise in 6 s, a pump draws a pool down evenly, a 15-tile waterfall
leaves grams behind, water under 125 kPa of air boils from 105.4 °C and not
below, a stray droplet falls within a second, and nothing made steam at room
temperature. Three did not:

- **Water would not drain through a one-tile opening into a closed space.**
  It ran until the air below was compressed enough to bear it and then stood
  on that air for good: 31.7 of 40 t stayed above a sealed room, and a
  flooded shaft kept an air pocket under two tiles of water. Fixed here.
- **Wide or shallow pools level slowly**, by the liquid drag of 0.1.5: a
  500 kg step is under 25 kg after 10 s on 35 columns, 51 s on 70 and 215 s
  on 140 (three tiles deep; half a tile deep is three times slower), never
  overshooting. With Activity tiers a region left alone pauses after 60 s,
  so a 140-column pool can stop with a quarter-tile slope until something
  touches it. Not changed here.
- **A tilted water and oil interface is held by interface sharpening**: it
  takes minutes to level and can stop short, where it levels in under a
  minute with sharpening off; a resting pool of water under a shallow oil
  layer could even stir itself. Fixed here.

The recording of 0.1.4 was looked at again for the first of these: its shaft
was two to four tiles wide and full of water throughout, so that defect is
not what it shows.

## Changes

- **Liquid over gas.** Where a tile's liquid rests on the gas of the tile
  below, the two now change places at free-fall pace (a quarter of the
  smaller volume a step at most), as the solver already overturns a light
  gas under a heavy one
  ([game kernel](../native/MAC_GAME_KERNEL.md#liquid-over-gas-profile-12-2026-11-12)).
  The sealed room fills (half of its 40 t in 23 s, 99 % in 58 s; a two-tile
  opening takes 46 s), a breached air pocket or cave floods and its air
  comes up through the pool. Small bubbles under a flat ceiling away from
  the opening leave slowly.
- **Interface sharpening never lifts net mass**
  ([game kernel](../native/MAC_GAME_KERNEL.md#sharpening-never-lifts-net-mass)).
  It was pumping air down under oil and water up into it. A tilted water and
  crude oil interface now levels in 44 s (it stopped 40 to 300 kg short), and
  water under a shallow oil layer stays at rest. Water surfaces under gas
  are unchanged to the digit. Water poured onto oil still spreads slowly.
- **Germs stay with their phase.** A tile holding water and air keeps two
  counts of each germ, the water's and the air's
  ([game kernel](../native/MAC_GAME_KERNEL.md#carried-passives-profile-12-2026-11-12),
  [owner](colony-owner.md#germs)). Before, air leaving a pool's surface took
  the water's germs with it at the water's count per kilogram, a droplet
  falling through germy air left its own behind, and, the larger effect, a
  pool tile less than half full counted as one culture in gas, so its
  water's germs diffused into the room. A pump now takes the water's germs,
  a breath over a pool the air's.
- **Fault reports keep their origin.** A fault that passed through one of
  the mod's own clean-up hooks lost its stack trace (the hook's host
  restarts it); the log now adds "first thrown at" with the original one.
- **Scenario `sealed-drain`** and a germ time series in `storage-ledger`'s
  log.

Profile 12 arithmetic changes where liquid lies over gas, and its germ
counts are split by carrier; the checkpoint format and version 32 are
unchanged. Colonies of 0.1.4 and 0.1.5 load (their germs are given to the
phase holding most of each tile's mass); a colony saved by 0.1.6 does not
load in 0.1.5.

## Verification

On the working tree before the acceptance run:

- **Native.** 93 of 93 CTest tests. Of the 62 `ma_mac_*` invocations, 59
  print what 0.1.5 printed; `mac_game_kernel` gains three control lines,
  `mac_production_kernel_oracle` differs only in timings, and
  `mac_phase_change`'s profile-12 boiling line reads 6.21 kg and 306.9 kPa
  (5.87 kg and 280.6 kPa; bounds more than 2.6 kg and below 500 kPa). New
  controls: a sealed nine-cell room under 12 t of water fills through a
  one-cell hole (half by 5.2 s; 8,873 kg of water and 0.30 of its 15.9 kg
  of air after 120 s); a resting water and oil pool keeps 1.8e-7 kg of
  column range and a tilted interface is under 25 kg from 51 s; after a
  pool levels, every cell is within 4e-9 germs of 1,000 a kilogram of water
  and 500 a kilogram of air (854 off before).
- **In game** (Linux harness): `sealed-drain` 6 of 6 (the room 97.8 % full
  after 120 s with 0.66 of its 22.7 kg of air left, half full in 7 s, no
  steam, ledgers 2.6e-15); `pool-step` (range 0.16 kg after 20 s),
  `liquid-settling`, `flood-pocket` and `flood-co2-corner` pass; batch 13's
  `mixed-pipe-colony`, `mixed-pipe-catalogue` and `pitcher-pump` pass and
  `storage-ledger` passes 13 of 14: "the vented gas brings its germs to the
  owner once" reads +81,033 of 90,000 (it read +100,357), and the pool
  room's air holds no germs at any logged frame.
- **Managed and tools.** Settings 451 checks, distribution 250, presentation
  50 tests, Python 516 tests.

Not established: Windows itself, and the user's own look at one-tile
openings and oil over water in play.

## Open

- **The one-off load fault of 0.1.5's acceptance run** (a NullReference under
  the game's `EnergyGenerator.EnergySim200ms` on the first step after an
  autosave was loaded) did not recur in twelve reloads of that autosave, ten
  of them with this version's trace keeping. Its origin is not known.
- **`storage-ledger`, "the emptied gas brings its germs once"** now reads
  low: +53,396 germs against a floor of 68,000 (it read +435,032 against a
  ceiling of 88,000). The scenario's new log shows why. The pool room's air
  holds no germs at any time, so the excess is gone. Of the 80,000 germs
  given to the emptier the game's own storage rule leaves 76,000 on the gas
  (5 % go to the building), and they reach the room in full (about 75,400 by
  the time the store is empty, the rest being the game's die-off on the
  way). But the scenario re-records its "emptier done" frame on every
  frame, so its "six frames later" never comes and it samples at its
  fallback, frame 400, fifty seconds on; the carbon dioxide layer on the
  floor is above the germ's density window by then and loses 350 germs a
  second by the game's over-population rule. Sampled where its comment says
  (six frames after the store empties) it reads +69,875 and passes. Earlier
  passes at frame 400 (74,695) were the leak making up the die-off. The
  check is an acceptance check, so it is left as it is for the user to
  rule on; the correction would be to record the frame once, bounds
  unchanged.
- **Wide pools and the pause**, and the interface item above.
- **The steam of the 0.1.4 recording.** No probe at 22 °C made room steam
  with "Evaporation below boiling" off, digging included (a tile dug beside
  water flashes at most 1.8 g, gone in seconds). The 2.0-2.4 % the recording
  shows is the saturation humidity of 22 °C air at 138 kPa (2.6 kPa), which
  is what that option gives when it is on. Worth checking in the colony's
  settings before anything else.
- Going back to ONI's own liquid simulation stays the user's decision
  ([0.1.5](playtest-0.1.5.md#open)); everything open there and in
  [0.1.4](playtest-0.1.4.md#open) stands.

## Acceptance

Pending.
