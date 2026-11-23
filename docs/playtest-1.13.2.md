# Mixed Atmospheres 1.13.2

1.13.2 is the first candidate of the 1.13 line meant for play: 1.13.0 and
1.13.1 were never uploaded, and it contains both. It adds the speed work
that keeps the fluid on every tick, reservoirs that give out their stored
mixture, and faster solver defaults (user decisions 2026-11-23).

## What is new since 1.12.2.3

- **Face forces** ([the interface](face-forces.md)): another mod's fans can
  push air through the solver; built for `../oni_fan_mod`.
- **Liquid is shown wherever there is any**
  ([the shown phase](colony-owner.md#the-shown-phase)): puddles, films and
  pool surfaces are visible and can be mopped; plants, vents, pumps and
  machines still read the air above a film.
- **Speed without slowing the fluid**
  ([the study](speed-study-2026-11-23.md)): less bookkeeping around each
  tick; a direct pressure solve for large cave-like regions, kept away from
  regular rooms; the tick's stages on several workers; the largest regions
  first. None of it changes an equation.
- **Reservoirs mix** ([reservoirs](mixed-pipes.md#reservoirs)): a liquid or
  gas reservoir gives out the mixture it holds, every stored element in
  proportion, where the game sent out one element after another.
- **Faster defaults**
  ([settings](settings.md#the-faster-defaults-of-1132)): pressure tolerance
  1e-6, one flow part per step, trace removal from 1 mg. A colony from an
  earlier version that still has that version's defaults takes them when it
  loads and says so; **Settings → Advanced → Solver → Exact** restores the
  earlier ones.

## Speed on the November 22 colony

Owner tick on that session's replay, 12 workers, quiet test host (a Ryzen 9
5900X; estimated game speed is 200 ms over the tick plus 25 ms):

| | Owner tick | Estimated speed |
| --- | ---: | ---: |
| 1.13.0 as the colony was saved | 151 ms | 1.13× |
| 1.13.2 with that colony's saved settings (Exact, tolerance 1e-6) | 108 ms | 1.50× |
| 1.13.2 with its defaults, which the colony takes when it loads | 64.9 ms | 2.22× |

The goal of 3× (41.7 ms) is not reached. What remains is about 31 ms in the
regions and 33 ms of work around them
([where the tick is now](speed-study-2026-11-23.md#the-exact-route-measured));
a further round on both is in progress and is not part of this candidate.
The slower fluid rates (1 tick in 4, 8 or 16) are still offered and are not
the default.

## What the faster defaults cost

Against the colony's saved settings after 90 s of the replay, on tiles
holding at least 100 g: pressure differs by 0.011 % on average (0.14 % at
the 99th percentile) and temperature by 0.15 K (2.0 K at the 99th
percentile). The direct pressure solve and the other speed work differ from
1.13.1 at the level of a tolerance change (0.003 K on average).

## What to look at while playing

- A reservoir fed with two liquids or several gases: its outlet pipe should
  carry the mixture, and a filter behind it should split it.
- Rooms with hot gas below cold: they should still turn over as before (the
  direct solve is kept away from regular rooms for this reason).
- The first load of an existing colony shows a notice about the faster
  solver defaults; the colony's settings window then shows 1e-6, 1 and
  1e-6 kg under Advanced → Solver.
- The speed the game reaches on fast-forward in the November 22 colony.

## Checks

Solver arithmetic changed in the pressure solve's iterates only (the direct
route and the multigrid's interpolation); equations, tolerances and the
native checkpoint format are unchanged. The managed settings record is
version 6.

| Check | Result |
| --- | --- |
| Native tests, Linux, at the solver commits | 96 of 96 |
| Native tests, Windows cross build under Wine | 97 of 97 |
| Every `ma_mac_*` output, before and after the pressure commits | 53 equal; 11 differ at tolerance level from the multigrid's interpolation along fine faces (the production-kernel tests) or by added PASS lines, all passing |
| Every `ma_mac_*` output, before and after the stage commits | equal but for one changed PASS line and timing fields |
| Replay fingerprints, 3, 6 and 12 workers | identical |
| `python3 tools/build.py` | passed (Python tests, 93 managed/native integration tests, the managed suites) |
| `tools/SettingsTests` | 588 checks |
| `tools/ColonyConduitTests` | passes with the reservoir draw |
| In game, build 763796: `reservoir-mix` | 8 of 8 |
| In game: `mixed-pipe-machines`, `pipe-throughput` | 8 of 8, 4 of 4 |
| In game: `base` with the new defaults | 4 of 4 |
| In game: an earlier profile-12 save loaded with the migration | loads, changes the three settings with the notice, runs on |

The staged package's own acceptance run is recorded under
[Staged package](#staged-package) once it has run.

## Staged package

Not staged yet.
