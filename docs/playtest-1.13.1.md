# Mixed Atmospheres 1.13.1

1.13.1 is 1.13.0 with less work around every tick. It computes exactly what
1.13.0 computes: the save format, the checkpoint, every setting and every
result are the same. It comes out of the
[speed study of 2026-11-23](speed-study-2026-11-23.md), which also lists the
settings that trade accuracy for speed, what each costs, and how to reach 3×
on the November 22 colony.

## What changed

Twelve changes to the colony owner's own work around the regions' flow
([the list](speed-study-2026-11-23.md#what-was-built-the-same-results-sooner-1131)):
buffers kept between frames, heat edits that no longer search for their
refused entries by trial, and whole-grid passes (space sink, germs, activity
clocks, merge, projection, the next prepared world) moved onto the workers.

Owner tick on the November 22 colony's replay, quiet test host, 12 workers:

| Fluid update rate | 1.13.0 | 1.13.1 | Estimated game speed |
| --- | ---: | ---: | ---: |
| Every tick (as the user played) | 151 ms | 137 ms | 1.13× → 1.24× |
| 1 tick in 4 | 67 ms | 54 ms | 2.19× → 2.55× |
| 1 tick in 8 | 51 ms | 38.5 ms | 2.64× → 3.15× |

With the two fast solver settings of the study (trace removal threshold
1 mg, one flow part per step) 1.13.1 takes 90 ms a tick with the fluid on
every tick (1.74×) and 40 ms with it on 1 tick in 4 (3.09×). Those settings
exist in 1.13.0 too, under Settings → Advanced → Solver and Performance;
nothing about them changed.

## Checks

Solver arithmetic and checkpoint bytes are those of 1.13.0.

| Check | Result |
| --- | --- |
| Native tests, Linux | 95 of 95 |
| Native tests, Windows cross build under Wine | 96 of 96 |
| `python3 tools/build.py` | passed; 593 Python tests, 93 managed/native integration tests |
| Every `ma_mac_*` test and diagnostic output, 1.13.0 against 1.13.1 | 62 of 62 equal (timing fields masked) |
| Owner replays, fluid every tick, 1 in 4 and 1 in 8, November 22; every tick, November 21 | the fingerprints of 1.13.0 (`ea4534603137f6be`, `aef2a20271b519f2`, `bcebd5251b4c7346`; `c658996d69343921`) |
| `tools/colony_speed.py --regression` | 12 of 12 cases |
