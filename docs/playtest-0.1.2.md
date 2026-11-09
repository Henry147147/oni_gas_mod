# Private playtest 0.1.2, 2026-11-09

Version 0.1.2 answers the user's [review of 0.1.1](playtest-0.1.1-review.md):
sublimation not working, oxygen draining fast and pools behaving badly. It
keeps everything in [0.1.1](playtest-0.1.1.md). It is a private candidate; no
public release or Workshop upload was made.

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

Profile 12 arithmetic changes (pools); profiles 2-11, the checkpoint format
and version 32 are unchanged; no setting was added. Natural sublimation and the
reserve fix change no solver arithmetic.

## Verification

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

Code revision `57f9e446ea3cab1026677eb5ea8beea6f4555df2` (clean manifest).
Directory `/mnt/data/share/oni/playtest/MixedAtmospheres-0.1.2-57f9e44/`,
ZIP `MixedAtmospheres-private-windows-0.1.2-57f9e44.zip`, SHA256
`c595d0f8e25692da036efb02430a5384d492498eb5408acd69dbe9bdc4c31ed3`. Install
as before: close ONI, extract, run `install_windows.ps1`. A new colony is
recommended. Evidence is under the ignored `artifacts/release-0.1.2/` and
`artifacts/video-0.1.1/`.
