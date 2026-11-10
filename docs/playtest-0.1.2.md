# Private playtest 0.1.2, 2026-11-09

Version 0.1.2 answers the user's [review of 0.1.1](playtest-0.1.1-review.md):
sublimation not working, oxygen draining fast and pools behaving badly. It
keeps everything in [0.1.1](playtest-0.1.1.md). The candidate was rebuilt the
same day with the gas source energy setting the user chose (it keeps the
version number, at the user's request); the first build, `57f9e44`, is
superseded. It is a private candidate; no public release or Workshop upload
was made.

## Liquid stability rebuild, 2026-11-10

Latest private code revision **`b75d712`** answers the 2026-11-09 recording's
liquid churn. Profile 12 sharpens along the reconstructed surface normal
and retains active flow resolution during wet remote intervals. The two
independent causes and conservative regression controls are described in the
[game kernel](../native/MAC_GAME_KERNEL.md#quiet-liquid-surfaces-profile-12-2026-11-10).
Checkpoint encoding/version 32 is unchanged; profile-12 arithmetic and
resulting checkpoint contents change. Profiles 2–11 keep their arithmetic.

Linux base game and WindowsPlayer/Proton Spaced Out! each pass all 17
liquid/flood/airflow/freeze-melt checks: the sealed basin's late variation is
70.93 kg under the unchanged 80 kg bound, with all 56,000 kg water retained.
The old Linux baseline fails at 421.57 kg. Clean verification passes 93 Linux
and 94 Windows/Wine native tests, 516 Python tests (11 platform skips), all
managed suites and each platform's 2,814-dependency contract with zero
differences. All 62 standard MAC invocations pass before/after: 59 outputs
are identical, one changes only timing fields, one adds regression lines,
and one changes only the profile-12 boiling trajectory.

Clean-manifest ZIP:
`/mnt/data/share/oni/playtest/MixedAtmospheres-0.1.2-b75d712/MixedAtmospheres-private-windows-0.1.2-b75d712.zip`.
SHA256 `7943a7fa2edf0a74fc7bef41172484a800d24ccadecdebbf2cd20df41cfc9e52`.
Both managed and both native DLLs match the passing WindowsPlayer game batch.
Close ONI, extract and run `install_windows.ps1`. This supersedes the earlier
0.1.2 ZIPs. The timestamped recording/log review is saved beside the video as
`Playtest-review-2026-11-09.md`; private evidence is in
`artifacts/liquid-instability-2026-11-10/` and `artifacts/video-0.1.2/`.

The supplied recording confirms the earlier candidate on actual Windows;
this rebuild's game test uses Proton. The user's exact final save and new
full-colony throughput remain unmeasured. Linux launches and advances in
actual [Steam Offline Mode](steam-offline-playtesting.md), after the user's
cached sign-in, while Windows Steam runs successfully.

## Changes

- **Starting oxygen reserve:** the bridge waits for the starting duplicants
  for 120 game seconds, so a paused start (Settings opened at once) no longer
  skips the oxylite reserve.
- **Natural sublimation:** exposed oxylite, polluted dirt, polluted mud and
  corium tiles sublimate into owned gas, and polluted water off-gasses at its
  surface, by ONI's own native rule
  ([natural sublimation](colony-owner.md#natural-sublimation)).
- **Pools (profile 12 kernel):** flooding no longer overfills liquid cells into
  megapascal spikes, so carbon dioxide no longer liquefies at room temperature,
  and a liquid above its boiling point in a liquid-full cell boils off in
  seconds ([game kernel](../native/MAC_GAME_KERNEL.md)).
- **Readings:** with friendly readings, a cell at least 95% liquid reads
  "Submerged" without its trapped gas and breathing warnings.
- **Gas source energy (user decision 2026-11-09):** **Gameplay → Gas sources →
  Gas source energy** chooses how gas from machines, vents, pumps, duplicants
  and sublimating tiles carries energy. **Compensated** (the default) gives a
  source's cell the gas's enthalpy and returns the same energy across its
  room: no cold spot at diffusers and vents, no warm spot at pumps, and room
  temperatures change as in ONI. **Real physics** keeps that flow work, so
  filling a sealed room heats it. **Internal energy** is the earlier model
  (cold spots at strong sources). It is saved with the colony and can be
  changed during play; colonies saved by the first 0.1.2 build load with
  Compensated ([source energy](../native/MAC_GAME_KERNEL.md#source-energy),
  [settings](settings.md)).

Profile 12 arithmetic changes (pools); profiles 2-11, the checkpoint format
and version 32 are unchanged. The colony settings record moves to version 4
(version 3 loads). With Internal energy, solver arithmetic is that of the
first build; natural sublimation and the reserve fix change no solver
arithmetic.

## Verification

Rebuilt candidate `d7eb0e3` (gas source energy):

- Linux: `tools/build.py --oni-native-observer` passes at `eb79a42` (93/93
  native tests, 516 Python tests with 11 platform skips, the managed suites
  including 375 settings checks); the 93 native tests pass again at
  `fba862c`, `832c763` and `d7eb0e3`. Between `f10f414` and this revision 61 of 63
  `ma_mac_*` test outputs are identical; `mac_game_kernel` differs by its
  source energy lines and the production oracle by timing fields, so
  Internal energy keeps the solver's arithmetic.
- New tests: the kernel's source energy controls (enthalpy split,
  isentropic draw, equal room shares, wet rooms ending as with internal
  energy, an empty sealed cell, a zero
  edit inside a room, in-place edits, a frame's return equal to compensated
  edits, and a sealed tube fed oxygen at room temperature: source cell / mean
  284.4 / 300.0 K with Internal energy, 302.3 / 302.3 K with Real physics,
  300.2 / 300.0 K with Compensated), the owner's rule API and ledgers, the
  replay journal and the colony record. They also pass under
  AddressSanitizer/UBSan. (The kernel test binary's existing zero-add check
  fails under the sanitizers at `f10f414` too.)
- Windows `d7eb0e3`: the MinGW cross-build passes 94/94 tests under Wine;
  both DLLs import only Windows system libraries; the package's game
  contract checks 2,814 dependencies with 0 differences.
- In game, gas in sealed 296 K rooms with two oxygen diffusers, and with a
  pump feeding a vent (`playtest-diffusers`, `playtest-pump-vent`, base
  game); every check passes in each mode:

  | Mode | Diffuser room gas | Pump and vent rooms gas |
  | --- | --- | --- |
  | Compensated (default), Linux `832c763` | 295.7-302.8 K | 295.2-301.7 K |
  | Compensated, WindowsPlayer under Proton `832c763` | 295.7-302.3 K | 295.2-301.5 K |
  | Compensated, Linux and WindowsPlayer `d7eb0e3` | every check passes | every check passes |
  | Internal energy, Linux (first build of the setting) | 286.6-301.9 K | 277.9-305.5 K |
  | Real physics, Linux (first build of the setting) | 381.1-451.0 K | 324.2-449.1 K |

- WindowsPlayer under Proton, final package (`d7eb0e3`): `starting-air`,
  both playtest arenas, `natural-sublimation`, `flood-pocket` and
  `flood-co2-corner` pass all 39 checks; on Linux the same game passes all
  39 too.
  `starting-air` reads the pod's air when it starts, so after other
  scenarios in one game it sees the duplicants' exhaled CO₂ (99.94% oxygen)
  and fails that one check; first in a game it passes, as with `57f9e44`.
- A loaded colony (a 0.1.1-era save, which loads with Compensated) changed
  to Real physics during play, saved and reloaded with Real physics.
- Linux game, base game, at `832c763` and (marked) `d7eb0e3`: electrolysis,
  vent overpressure, space heater, bottle emptier, gas generator,
  deoxidizer, breathing, breath particle, life support, mask station and
  oxyfern (18 checks); coal generator, rust deoxidizer, blocked
  electrolyzer, deodorizer, polymer press, oil refinery, geyser, meteor,
  rocket exhaust and sensor (15); pipes, filter, mixed pipes, pipe thermal,
  cold pipes, heat machines and liquid pump (28); the mixed-pipe pumps
  (27), gas vents (13, mass, heat and passive accounts exact), output (10)
  and machines (8), each in its own game (together they count one
  another's pumps and pending jobs); terrain, heat, phase, freeze-melt,
  door, sublimation, falling, airflow, debris heat, building melt and
  byproduct merge (31); and at `d7eb0e3` wheezewort, puft, hazards, pitcher
  pump and storage ledger (21). At `832c763` the storage ledger's mopped
  water (a puddle filled with air, a room without gas-only cells) was
  2 K warm, which `d7eb0e3` fixes (299.8 K). `fault-containment` crashes the
  game into ONI's error report two seconds in, also with the 0.1.1
  package: not checked here.
- Speed, the large industrial save (32 duplicants), 3,000 frames at speed 3,
  Linux, same session: Compensated 1.682 (1.692 at `d7eb0e3`) and Internal
  energy 1.700 game seconds per real second; transfer stage 14.7 and 9.7 ms a 150-frame mean,
  of which the frame's flow work return takes about 1.3 ms. The save's
  transfer batch is refused every frame by a few sub-ulp injections and
  retried edit by edit; compensating per call (`fba862c`) therefore cost
  26.6 ms and 1.554, which this revision removes. On a 256 × 128 grid of
  air, a batch of 600 edits takes about 3.3-3.6 ms; compensating it adds
  about 0.8 ms with walled 16 × 8 rooms and 3 ms as one open area.
- Seen while measuring, not changed: the owner's held precision inventory
  (sub-ulp injections kept for later release) grows by about 10-20 entries
  a frame on the industrial save in either mode and is not released (47,000
  entries after 3,000 frames; its stage then takes about 6 ms a frame).

First build `57f9e44`:

- Linux `57f9e44`: `tools/build.py --oni-native-observer` passes 93/93 native
  tests, 516 Python tests (11 platform skips) and the managed and
  installed-game suites. The kernel change's `ma_mac_*` comparison: 60 of 64
  outputs identical; the others are the profile 12 controls, the boiling
  control and oracle timing fields.
- Windows `57f9e44`: the MinGW cross-build passes 94/94 tests under Wine; both
  DLLs import only Windows system libraries. The package's game contract checks
  2,814 dependencies with 0 differences.
- WindowsPlayer under Proton, final package, base game: `starting-air`,
  `natural-sublimation`, `flood-pocket` and `flood-co2-corner` pass all 30
  checks (flood peak 216 kPa, no liquid CO₂; 70 kg of corner liquid CO₂ gone
  within 20 s; ledgers closed).
- Linux game (agents' branches, same code): the natural-sublimation scenario
  under the owner and under ONI's own simulation; terrain, heat, phase,
  freeze-melt, door, sublimation, falling, airflow, sensor, pumps, meteor,
  geyser, mixed-pipe and storage batches.
- Cost against 0.1.1: the Spaced Out! fault-world replay flows about 11%
  slower; the large industrial save runs 1.654 against 1.696 game seconds per
  real second; a Volcanic colony runs faster.

Not established here: a Spaced Out! colony in game (the harness Steam client
needs the user to sign in again), the actual Windows OS, and the user's exact
save.

## Earlier candidate, 2026-11-09 (superseded)

Code revision `d7eb0e32796b292b451502592cbc7d382b949637` (clean manifest).
Directory `/mnt/data/share/oni/playtest/MixedAtmospheres-0.1.2-d7eb0e3/`, ZIP
`MixedAtmospheres-private-windows-0.1.2-d7eb0e3.zip`, SHA256
`54deeaed830d4991f63cb3fd9cd500f82324008fa8ddc37cad86ec6c294511a0`. Install
as before: close ONI, extract, run `install_windows.ps1`. A new colony is
recommended. Evidence is under the ignored `artifacts/source-energy/`,
`artifacts/release-0.1.2e/` and, for the first build, `artifacts/release-0.1.2/`
and `artifacts/video-0.1.1/`. Earlier ZIPs of 0.1.2 are superseded:
`MixedAtmospheres-0.1.2-57f9e44/` (SHA256 `c595d0f8…c31ed3`),
`MixedAtmospheres-0.1.2-fba862c/` (SHA256 `60de5a20…ed70`, per-call
compensation, about 10% slower on large colonies) and
`MixedAtmospheres-0.1.2-832c763/` (SHA256 `126c05fc…4bd0`, wet rooms warmed
as with real physics).
