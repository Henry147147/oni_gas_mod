# Spaced Out! private Windows playtest

The current engineering ZIP is
`MixedAtmospheres-windows-5d11aea-private.zip` (2026-10-28;
SHA-256 `36ed89745eaf3990ffb42ee2a84d595600958ec2876c5f24d69370acae31bdb0`).
It is packaged from clean revision `5d11aea`; its two managed and two native
DLL hashes match the final tested WindowsPlayer/Proton bundle. It includes
nitrogen removal, CO₂ limits off by default, Gameplay/Advanced settings,
reservoir lifecycle work, and the native load-transition/tile recreation fixes.
Ordinary Spaced Out! source/cold/menu loading passes with nine worlds and no
experimental overrides. Full deterministic continuation remains open; the exact
storage and native clock controls remain development-only. Actual Windows OS
feedback is pending. Start new colonies: old R&D saves are unsupported.

The previous engineering ZIP,
`MixedAtmospheres-windows-storage-retirement-private.zip` (2026-10-27;
SHA-256 `2af207557860504da4c05bf09d6d7fcf4eac9ab2c2036ad5f72d6339f7097983`),
contains the reservoir lifecycle implementation committed as `6b52c75`. Its
frozen manifest records `42fa304` with uncommitted sources. Its bounded
reservoir source/cold/menu controls passed under WindowsPlayer/Proton.

The older candidate **`bfb1168`**, 2026-10-27, supports ordinary new base-game and
Spaced Out! colonies. Spaced Out! stays enabled. This is a private candidate;
the broader release gate remains open and actual Windows OS testing is still
the user's part of the playtest.

## Install

1. Close ONI and extract `MixedAtmospheres-windows-5d11aea-private.zip`.
2. Open PowerShell in the extracted folder and run:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\install_windows.ps1
   ```

3. Launch ONI through Steam with Spaced Out! enabled. Enable Mixed Atmospheres
   in Mods and restart. Keep the other gameplay packs disabled for this build.
4. Start a **new** colony, using Classic or Spaced Out style. Existing vanilla
   colonies keep their saved simulation; mixture saves need this mod.
5. Default is recommended. Settings are on the Mods row and status panel;
   colony solver/breathing settings are saved with the colony. Reinstalling
   preserves configuration and presets outside the mod folder.

Plan sustainable oxygen production early. CO₂ breathing limits default off;
CO₂ still displaces oxygen. If you enable the limits in Settings → Gameplay,
plan CO₂ removal too. Solver presets and numeric thresholds are under Advanced. Check the Breathability overlay and
partial-pressure readings.

After testing, send `%USERPROFILE%\AppData\LocalLow\Klei\Oxygen Not Included\Player.log`.

## Verified scope

All game evidence here is **WindowsPlayer under Proton on Linux**, not Windows
OS evidence or a prediction for the user's 9950X/5070 Ti.

| Control | Evidence and provenance |
| --- | --- |
| Current loading fixes | The final Windows bundle passes ordinary Spaced Out! source/cold/menu loading: nine worlds, 24 saved tile recreation messages preserved, matching restored owner images and no experimental overrides (`release-loaded-tiles-spacedout-source` / `spacedout-load`). Its DLL hashes match clean `5d11aea`. This is not a complete continuation proof. |
| Whole cluster and camera changes | Nine Spaced Out-style asteroids pass mixture/ownership probes. Ordinary activation, source save, fresh-process load and menu reload pass on `34c63de`, without an owner environment override. |
| Rocket lifecycle and paired ports | `c942141` passes construction, gas/liquid ports in both directions, actual flight, airborne self-destruct, landed retirement and immediate grid-slot reuse, followed by fresh-process and menu reloads. All recovered constituent bounds and closed-ledger checks are unchanged. |
| Teleporters | `dc39f60` passes actual generated material endpoints in both directions, both phases, drained source/shared stores, exact pipe accounts and source/cold/menu checks. |
| Cargo | `0b93056` passes actual gas and liquid loading/unloading with 0.75 kg/0.25 kg mixtures and both reload paths. Filters are selected after native spawn validation. |
| Ordinary Classic New Game | `bfb1168` starts through normal clicks with Spaced Out enabled and no colony validation file. Its eight-world 636×404 cluster starts the owner, creates a real save, and quits normally. The save header records `EXPANSION1_ID`, Classic Terra, three duplicants and no sandbox. |
| Actual manual save and reload | The final package loads that UI-created save. The ordinary Save and Save and Quit buttons write at owner frames 84 and 100. The latter file then passes a fresh-process load to frame 108, exact restored-owner reencoding and normal exit. Maximum relative mass/energy errors are 7.12e-16/1.42e-15. |
| Build checks | Activation revision `34c63de`: 386 Python tests (8 skipped), 86/86 Linux CTests and managed/SDK checks. Native `6c91161`: 86/86 Windows cross-build CTests under Wine and clean all-MAC physical-output comparison. The final `bfb1168` managed build and Windows hook checks pass. |

The final revision changes only a status hint relative to `34c63de`; it does
not change activation, simulation or saves. Component evidence above retains
its original revision rather than becoming a new same-bundle pass. Its native
DLLs retain their `6c91161` build hashes. Full current-bundle base-game scenarios,
the nine-asteroid sweep (3,000 frames each), and the unchanged prepared
30,000-frame/ten-cycle run continue separately. The three accepted deferrals
and the other gameplay packs remain separate work.

During the inspected manual-save run, the strict read-only load harness reports
`failed-input-save-mutated`: the clicked Save button deliberately rewrites the
disposable working copy. That result is retained and is not counted as a
harness pass. The original UI source remains unchanged, and the newly written
frame-100 file passes the separate fresh-process load described above.

## Current package identity

The ZIP contains 23 entries, including the installer, with no game assemblies,
saves, colony validation configuration or private observer DLL. Every file
passes its manifest hash and the ZIP readback check.

- Source: `5d11aea49f1b96efd54c539af5e3f2ef378210b3`
- ZIP SHA256: `36ed89745eaf3990ffb42ee2a84d595600958ec2876c5f24d69370acae31bdb0`

## Historical bfb1168 package identity

The earlier ZIP contains 22 entries, including the installer repair, with no game
assemblies, saves or colony validation configuration. Every payload file and
ZIP entry was checked against the clean source package manifest.

- Source: `bfb116831c14dc5c0f179b3630c88964fc81297f`
- Manifest SHA256: `1b3751a6010bf2a3e4fc94b3aa4146363865c183f827aa4886f9a37228f0f462`
- ZIP SHA256: `5a9a843175190c147c628c9d130779ef19ca6acd55a4ff74c9b75dec18e59941`

This does not authorize public or Workshop publication.
