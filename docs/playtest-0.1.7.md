# Private playtest 0.1.7, 2026-11-13

Version 0.1.7 replaces [0.1.6](playtest-0.1.6.md), which was withdrawn
because it did not load earlier colonies, and settles what its record left
open that needed no decision from the user: wide pools that stopped
levelling, water poured onto oil, and the steam of the 0.1.4 recording. It
keeps everything else in 0.1.6. It is a private candidate; no public release
or Workshop upload was made.

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

- **Colonies of 0.1.4 and 0.1.5 load again.** 0.1.6 gave an older colony's
  germs their carriers while restoring it, so the restored owner no longer
  matched its saved image and the load was refused; its acceptance run
  caught this on the frozen 0.1.4 save and 0.1.6 was withdrawn
  ([0.1.6](playtest-0.1.6.md#acceptance)). The saved world is now restored
  as it is and its germs get their carriers in the first germ stage.
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
- **Earlier and new saves** (Linux harness, the check 0.1.6 lacked): the
  frozen prepared save made by 0.1.4 loads and runs 300 frames (170 germ
  cultures, germ audit 1e-16); a colony saved by this build after the
  `germs` and `pool-step` scenarios (201 cultures, in water and in air)
  loads and runs 200 frames.
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

Code revision **`f659a40`**. Windows ZIP:
`/mnt/data/share/oni/playtest/MixedAtmospheres-0.1.7-f659a40/MixedAtmospheres-private-windows-0.1.7-f659a40.zip`,
SHA256 `19d713b8dbf702c3e19091914b03152c801c1673eaf0dbc6815df7d36b83262a`.
Close ONI, extract and run `install_windows.ps1`. Colonies saved by 0.1.4
or 0.1.5 load; a colony saved by 0.1.7 does not load in 0.1.5 or earlier.

The quick tier of `tools/acceptance_suite.py` ran in full on `f659a40`
(46 steps, tag `quick-v1`): 45 passed, 1 failed, none skipped.

| Step | Result |
| --- | --- |
| Linux build; Windows (MinGW) build with its tests under Wine (94 of 94) | pass |
| Linux batches 1-12, 14, 15 | pass |
| Nine starting asteroids, 3,000 frames each | pass |
| One cycle on the frozen prepared save (made by 0.1.4) | pass: 3,033 frames, a natural autosave, three duplicants alive, mass error 3e-15 |
| Proton batches 2, 3, 4, 6, 7, 10, 11, 12 | pass |
| Proton contact, base source, prepared cycle, autosave cold | pass |
| Proton settings source and load, Spaced Out! source, cold load, menu reload, ordinary new game and its reload | pass |
| Linux batch 13 | fail: `storage-ledger`, "the emptied gas brings its germs once": owner +53,398 germs against a floor of 68,000 |

`linux-b13` is the check left for the user to rule on
([0.1.6](playtest-0.1.6.md#open)): the scenario samples at its fallback
frame, fifty seconds after the emptier's store is empty, and reads the
game's own die-off; its other thirteen checks and the batch's other three
scenarios pass. The frozen-save cycle is the step that stopped 0.1.6.

The Proton steps are the WindowsPlayer under Proton, not Windows itself.

Speed in game (Linux harness, GPU display, one run each; one cycle of 3,000
frames at speed 3 loaded from the prepared saves). The last three columns
were measured on 2026-11-13 with other work on the machine (a media
transcode held one core throughout); the first is the run of 2026-11-11:

| Save | 0.1.4, 11 Nov | 0.1.4 again | 0.1.5 `e3666a9` | 0.1.7 `f659a40` |
| --- | ---: | ---: | ---: | ---: |
| Industrial base, 32 duplicants: game seconds per real second | 2.255 | | 2.059 | 2.006 |
| ... warmed windows: owner / flow ms a tick | 58.3 / 46.4 | | 63.1 / 50.6 | 65.4 / 52.7 |
| Spaced Out! full colony: game seconds per real second | 0.974 | 0.922 | 0.905 | 0.883 |
| ... warmed windows: owner / flow ms a tick | 182.0 / 162.5 | 192.7 / 172.8 | 196.1 / 176.0 | 201.8 / 180.4 |

The same 0.1.4 package ran 5 % slower on the 13th than on the 11th, so the
two days do not compare; within the 13th, 0.1.5 is 2 % and 0.1.7 4 % behind
0.1.4 in the flow stage, against a spread of 5 % between windows of one
run. Replaying one world state of each colony on each solver (the flow
advance alone, [0.1.8](playtest-0.1.8.md#verification)) shows what is real
in that: the liquid drag of 0.1.5 costs nothing, and 0.1.7's repeated
layering in remote regions cost 4 to 5 ms a frame, which 0.1.8 removes. The
Spaced Out! colony is below the 1× beta bar on every version measured that
day.

Not established here: Windows itself, and the user's own look at pools,
one-tile openings and oil over water in play.
