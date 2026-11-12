# Private playtest 0.1.7, 2026-11-13

Version 0.1.7 follows [0.1.6](playtest-0.1.6.md) with the two things its
record left open that could be settled without the user: wide pools that
stopped levelling, and the steam of the 0.1.4 recording. It keeps everything
in 0.1.6. It is a private candidate; no public release or Workshop upload
was made.

## The steam of the 0.1.4 recording

The recording's rooms read 2.0-2.4 % steam at about 22 °C, and one air tile
held about 1.4 kg of water. The steam is the saturation humidity of 22 °C
air at 138 kPa (2.6 kPa), which is what "Evaporation below boiling" gives
when it is on; with it on, vapour also condenses where air is cooler, the
likely source of the water in the air tile (not checked). The user's own
game logs of 8 and 9 November on the playtest share
(`Player_11_8.log`, `Player_old.log`) print the new-colony settings of that
installation with `liquids.evaporationBelowBoiling=on`, and show the option
switched in a colony that night. With it off, no probe made room steam at
room temperature, digging beside water included
([0.1.6](playtest-0.1.6.md#open)). So the recording most probably shows the
option and not a fault (no log of 11 November itself is on the share);
nothing on screen said it was on.

## Changes

- **The start dialog says when evaporation is on.** With "Evaporation below
  boiling" on, the item about boiling in "What plays differently" (the start
  dialog and Settings → Gameplay → How it plays) reads "Evaporation below
  boiling is on: air near water holds about 2% steam and wet rooms form dew
  (Gameplay → Boiling and condensation)." The option's default stays off.
- **A pool keeps levelling until it is level.** With Activity tiers a region
  left alone was paused 60 s after its last touch with whatever slope its
  liquid still had: a quarter-tile slope on a 140-tile pool, 189 kg on a
  shallow 70-tile one. A region that reaches that deadline while its liquid
  still moves now keeps its once-a-second steps until the liquid is level
  (within 1 to 3 kg here) or ten minutes have passed since its last touch
  ([game kernel](../native/MAC_GAME_KERNEL.md#levelling-keeps-a-region-remote)).
  Gas moving over a pool at rest does not count, and a pool at rest pauses
  as before. Worked out and measured by a helper; the ten-minute limit is
  mine.

- **Liquid sinks past gas at the same pace in a quiet region.** A region
  with no buildings, duplicants or plants that nothing has touched for two
  seconds is stepped once a second, and the exchanges that let liquid sink
  past gas and layers sort themselves took one capped share a step. The new
  `sealed-drain` room filled at 250 kg/s in one run and 700 kg/s in another.
  Those exchanges now run once for each frame a step covers
  ([game kernel](../native/MAC_GAME_KERNEL.md#layering-in-a-remote-step)).

- **Water poured onto oil spreads.** Sharpening gathered the thin water
  film back from its edges as fast as it spread (78 kg from level after
  400 s). Between two liquids a sideways face is now sharpened only where
  both tiles' surfaces face it
  ([game kernel](../native/MAC_GAME_KERNEL.md#sharpening-never-lifts-net-mass));
  the pour ends 18 kg from level after 400 s, as with sharpening off. Pools
  of one liquid are unchanged to the byte.

| Pool with a 500 kg step, touched once | 0.1.6: paused at, slope left | 0.1.7 |
| --- | --- | --- |
| 35 tiles, 3 deep | 60 s, 0.0015 kg | unchanged |
| 35 tiles, half a tile deep | 60 s, 2.5 kg | 76 s, 0.5 kg |
| 70 tiles, 3 deep | 60 s, 14.5 kg | 99 s, 1.2 kg |
| 70 tiles, half a tile deep | 60 s, 189 kg | 302 s, 1.3 kg |
| 140 tiles, 3 deep | 60 s, 261 kg | 365 s, 2.6 kg |
| 140 tiles, half a tile deep | 60 s, 452 kg | 600 s, 31.5 kg (the limit) |

Solver arithmetic: profile 12 layers several times in a step longer than a
frame, and which frames a region is stepped in changes with Activity tiers;
frame-long steps and profiles 2-11 are unchanged. The checkpoint format and
version 32 are unchanged, and 0.1.6 colonies load.

## Verification

On the working tree before the acceptance run:

- **Native.** 93 of 93 CTest tests. Of the 62 `ma_mac_*` invocations, 59
  print what 0.1.6 printed; `mac_game_kernel` gains the levelling control
  line, the remote half of the drain control's line and the poured case of
  the two-liquid control's (15.3 kg from level after 300 s; 75.8 kg before),
  `mac_production_kernel_oracle` differs only in timings, and
  `mac_phase_change`'s profile-12 boiling line, which advances a second in
  one step, reads 6.91 kg and a 226.5 kPa peak (6.21 kg and 306.9 kPa;
  bounds more than 2.6 kg and below 500 kPa). Controls: a 70-column pool
  with a 500 kg step, touched once, keeps its remote steps until 98.6 s and
  pauses 1.16 kg from level, and the same pool at rest pauses at 59.6 s as
  before, with exact cold and 12-worker continuation; the sealed room of
  the drain control holds 8,755 kg after 30 s in a remote region against
  8,745 kg stepped every frame (6,410 kg before).
- **In game** (Linux harness): `pool-step`, `sealed-drain` (6 of 6; 9,946 kg
  in the room after 10 s where the same batch read 4,204 kg before the
  remote layering change), `liquid-settling`, `flood-pocket` and
  `flood-co2-corner` pass.
- **Managed and tools.** Settings 453 checks (two new, for the start dialog
  with the option on and off), distribution 250, presentation 50 tests,
  Python 516 tests.

Not established: Windows itself; a wide pool in a real colony (the table is
from the solver alone); the cost of longer remote stepping on the
full-colony saves, which the timed runs will show.

## Open

- How fast a wide pool levels is unchanged (the liquid drag of 0.1.5 makes it
  diffusive: 215 s to a 25 kg range on 140 tiles). A longer drag time levels
  wide pools faster but brings the overshoot back on 35 tiles (1.5 s: one
  37 kg reversal; 2 s: two, 82 kg), so no single value serves both.
- The `storage-ledger` emptier check waits for the user's ruling, and the
  one-off load fault of 0.1.5's run has not recurred; both as in
  [0.1.6](playtest-0.1.6.md#open).

## Acceptance

Pending.
