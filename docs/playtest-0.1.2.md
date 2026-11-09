# Private playtest 0.1.2, 2026-11-09

Version 0.1.2 answers the user's [review of 0.1.1](playtest-0.1.1-review.md):
sublimation not working, oxygen draining fast and pools behaving badly. It
keeps everything in [0.1.1](playtest-0.1.1.md). The candidate was rebuilt the
same day with the gas source energy setting the user chose (it keeps the
version number, at the user's request); the first build, `57f9e44`, is
superseded. It is a private candidate; no public release or Workshop upload
was made.

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

Rebuilt candidate `fba862c` (gas source energy):

- Linux: `tools/build.py --oni-native-observer` passes at `eb79a42` (93/93
  native tests, 516 Python tests with 11 platform skips, the managed suites
  including 375 settings checks); after the compensation cost change
  (`fba862c`) the 93 native tests pass again. Between `f10f414` and this
  revision 61 of 63 `ma_mac_*` test outputs are identical; `mac_game_kernel`
  differs by its two new source energy lines and the production oracle by
  timing fields, so Internal energy keeps the solver's arithmetic.
- New tests: the kernel's source energy controls (enthalpy split,
  isentropic draw, equal room shares, wet rooms, an empty sealed cell, a zero
  edit inside a room, in-place edits, and a sealed tube fed oxygen at room
  temperature: source cell / mean 284.4 / 300.0 K with Internal energy,
  302.3 / 302.3 K with Real physics, 300.2 / 300.0 K with Compensated), the
  owner's rule API and ledgers, the replay journal and the colony record.
  They also pass under AddressSanitizer/UBSan. (The kernel test binary's
  existing zero-add check fails under the sanitizers at `f10f414` too.)
- Windows `fba862c`: the MinGW cross-build passes 94/94 tests under Wine;
  both DLLs import only Windows system libraries; the package's game
  contract checks 2,814 dependencies with 0 differences.
- Linux game, base game, the setting's first build (same physics): every
  check of `playtest-diffusers` and `playtest-pump-vent` passes in each mode
  (two oxygen diffusers, and a pump feeding a vent, in sealed 296 K rooms):

  | Mode | Diffuser room gas | Pump and vent rooms gas |
  | --- | --- | --- |
  | Compensated (default) | 295.7-302.8 K | 295.3-302.0 K |
  | Internal energy | 286.6-301.9 K | 277.9-305.5 K |
  | Real physics | 381.1-451.0 K | 324.2-449.1 K |

- Cost: on a 256 × 128 grid of air, a batch of 600 edits takes about
  3.3-3.6 ms; Compensated adds about 0.8 ms with walled 16 × 8 rooms and
  3 ms when the grid is one open area.

Queued behind another agent's hold on the shared game slot (a GPU study),
to run on this exact package: the live change and save/reload of the
setting, the regression and machine/pipe/terrain scenario batches, the
WindowsPlayer run under Proton and the large industrial save's speed with
Compensated against Internal energy.

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

## Delivered private candidate

Code revision `fba862c347b56ab6b51942e8505f257de529beaa` (clean manifest).
Directory
`/mnt/data/share/oni/playtest/MixedAtmospheres-0.1.2-fba862c/`, ZIP
`MixedAtmospheres-private-windows-0.1.2-fba862c.zip`, SHA256
`60de5a205a9e6382368860d8fbf755188b76e80cb8cc6053b0a915e8b65fed70`. Install
as before: close ONI, extract, run `install_windows.ps1`. A new colony is
recommended. Evidence is under the ignored `artifacts/source-energy/`,
`artifacts/release-0.1.2c/` and, for the first build, `artifacts/release-0.1.2/`
and `artifacts/video-0.1.1/`. The first build's ZIP
(`MixedAtmospheres-0.1.2-57f9e44/`, SHA256 `c595d0f8…c31ed3`) is superseded.
