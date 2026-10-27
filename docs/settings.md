# Settings

Players configure the mod in one window. It opens from the **Settings** button
on Mixed Atmospheres' row in ONI's **Mods** screen, from the **Settings** button
on the status panel (main menu and colony), or from an optional shortcut. A
modal layer under the window keeps clicks, keys and scrolling from reaching the
game and pauses a running colony while it is open; Escape or **Close** closes
it. Code: `ModSettings*.cs` (model, files, window, runtime); tests:
`tools/SettingsTests`, `native/tests/oni_game_kernel_tests.cpp`.

## Two kinds of settings

- **Colony settings** (Solver, Breathing) are impactful. A new colony takes the
  current choice and records it; a loaded colony keeps what it recorded and
  ignores later changes. In a colony the window shows that colony's values,
  read-only. The solver values are saved in the colony's own physical image
  (checkpoint core23, unchanged format); the breathing rules in the save's
  MACOLP03 section 3 ([below](#save-record)).
- **Preferences** (Performance, Display & keys) control worker count,
  presentation and controls. They apply at once, everywhere, including in
  loaded colonies, and do not enter colony saves.

The solver is always the supported one: game profile 5 (core23) with mixed
pipes. No setting selects the production kernel, the game-floor profile, an
older game-kernel profile or single-element packet pipes; only profile 5's own
saved tunables vary, within the ranges below. A value outside the measured one
is marked **experimental** in the window. The native library refuses values
outside the ranges (`ma_oni_colony_set_options`); a refusal starts the colony
with Default and shows a notice.

## Presets

Built-in presets set the colony settings and keep the player's preferences.

| Preset | Colony settings |
| --- | --- |
| Default (recommended) | Exactly what a new colony gets without settings; byte-identical checkpoint (native control). |
| Robust | Pressure/heat iteration caps 1600/3200. A converged solve stops early, so results are identical wherever Default completes; a difficult solve gets more room before it faults. |
| Fast (experimental) | Pressure tolerance 1e-6: fewer pressure iterations, pressures settle less exactly. |
| Precise (experimental) | Two solver steps per game tick, pressure tolerance 1e-10 and the Robust caps; about twice the solver time. |
| Relaxed breathing | Oxygen ≥ 10% and 10 kPa; carbon dioxide ≤ 1% and 1 kPa. |
| Strict breathing | Oxygen ≥ 18% and 18 kPa; carbon dioxide ≤ 0.3% and 300 Pa. |
| Hard mode | Oxygen 15–35% (maximum oxygen share 35%) with nitrogen air for new colonies ([nitrogen](nitrogen.md#starting-air-and-progression)). |

Lower caps are not offered: they are never faster, and reaching a cap stops
the simulation. Players save the shown settings (colony settings and
preferences) as their own presets and overwrite, rename, delete, load, copy to
the clipboard and import from it. In a colony, **Save this colony's
settings** keeps that colony's values as a preset; loading presets and resetting
colony settings happen in the main menu. **Reset colony settings to Default**
and **Reset preferences** restore the defaults.

## Files

Everything lives in `<ONI user folder>/mods/config/MixedAtmospheres/`
(`Util.RootFolder()`: on Windows `Documents/Klei/OxygenNotIncluded`, on
Linux `~/.config/unity3d/Klei/Oxygen Not Included`), outside the mod's own
folder, so reinstalling or updating the mod keeps it. **Open settings folder**
opens it.

- `config.json`: `Format` `MixedAtmospheres.Settings`, `Version` 1, `Preset`
  (the current choice's id or `custom`), `Colony` and `Preferences` objects of
  the keys below. A missing key takes its default; an unknown key is logged and
  ignored. A file that cannot be used (bad JSON, another version, a value of the
  wrong type or out of range) gives Default with a visible notice; the next
  change keeps the old file as `config.json.invalid`. On the first start without
  `config.json`, the older `appearance.json` supplies the pattern contrast.
- `presets/<id>.json`: `Format` `MixedAtmospheres.Preset`, `Version` 1, `Name`,
  `Description`, `Colony` and, for saved presets, `Preferences`. The id is the
  name in lower-case letters, digits and hyphens. A preset that cannot be read
  is listed with its error and can only be deleted.

Validation scenes (a `colony-validation.json` request) never read these files:
they use Default with exact readings, or the preset that
`MA_COLONY_SETTINGS_PRESET` names (a built-in id or a preset file path; a
file's own preferences, such as friendly readings for captures, then apply).
Harness games see only `/tmp` and their output folder, so keep such a file
under `/tmp`.
`diffusion.json` belongs to the development configured domain only.

## Colony settings

All are recorded per colony and read-only in a loaded colony. Native ranges are
those of `MAOniColonyOptions` (`oni_colony.h`).

| Key | Name | Default | Range (window steps) | Effect | Storage |
| --- | --- | --- | --- | --- | --- |
| `solver.stepsPerFrame` | Solver steps per game tick | 1 | 1, 2, 4 (≠1 experimental) | Maximum step 0.2/0.1/0.05 s (binary32): finer fast flow, 1×/2×/4× solver time | core23 timestep |
| `solver.pressureTolerance` | Pressure solve tolerance | 1e-8 | 1e-12–1e-4 (decades; ≠1e-8 experimental) | Relative pressure-root accuracy | core23 |
| `solver.pressureIterationCap` | Pressure iteration cap | 400 | 400–65536 (400…12800) | Room for difficult pressure solves; no result changes where Default converges | core23 |
| `solver.heatIterationCap` | Heat iteration cap | 800 | 800–65536 (800…25600) | Same for heat solves | core23 |
| `solver.flowParts` | Flow parts per step | 2 | 1–8 (≠2 experimental) | Transport sub-steps per solver step | core23 |
| `solver.gasHeatMixing` | Gas heat mixing | 8 | 0–32 (≠8 experimental) | Temperature mixing by moving gas, energy conserved (user decision 2026-10-24) | core23 |
| `solver.liquidStiffness` | Liquid stiffness | 0.04 | 0.01–1 (≠0.04 experimental) | Fraction of the catalogue bulk modulus; sealed liquids stiffer | core23 |
| `solver.sharpenLiquidInterfaces` | Sharpen liquid surfaces | on | on/off (off experimental) | Bounded interface correction keeps surfaces crisp | core23 |
| `solver.interfaceDetail` | Thinnest sharpened film | 0 | 0–0.5 of a cell (≠0 experimental) | Thinner films are left unsharpened | core23 |
| `solver.phaseRelaxationSeconds` | Boiling/condensation time | 1 s | 0.1–10 s (≠1 experimental) | Liquid/vapour relaxation time constant (other conversions keep 1 s) | core23 |
| `solver.traceThresholdKg` | Trace removal threshold | 1e-11 kg | 1e-11–1e-6 kg (≠1e-11 experimental) | Per-constituent booked trace sink; 1 mg ceiling | core23 |
| `breathing.minimumOxygenFraction` | Minimum oxygen share | 15% | 5–25% | Breathable with at least this oxygen share by moles (polluted oxygen counts) | section 3 |
| `breathing.minimumOxygenPressurePa` | Minimum oxygen partial pressure | 15 kPa | 1–50 kPa | …and at least this oxygen partial pressure | section 3 |
| `breathing.maximumCo2Fraction` | Maximum carbon dioxide share | 0.5% | 0–10% | …and carbon dioxide below this share | section 3 |
| `breathing.maximumCo2PressurePa` | Maximum carbon dioxide partial pressure | 500 Pa | 0–10 kPa | …and below this partial pressure | section 3 |
| `breathing.maximumOxygenFraction` | Maximum oxygen share | no limit (1) | 30–100% | Cells richer in oxygen fail (1: easy mode, no limit). Below 100% needs nitrogen air; the window warns otherwise | section 3 |
| `breathing.nitrogenAir` | Nitrogen air | off | on/off | New colonies only: generated oxygen and polluted oxygen become air of the same mass, 25% oxygen by moles and nitrogen for the rest (user decision 2026-10-27) | section 3 (version 2) |

## Preferences

**Performance → Solver workers** defaults to **Physical cores**: one worker
per physical CPU core (12 on a 5900X, 16 on a 9950X), without the former
two-logical-CPU deduction. Enter a count from 1 through 256 and press **Set**
to use fewer workers or SMT threads; **Auto** restores the physical-core
default. Small grids still use fewer workers when they
have fewer blocks. A running colony takes the change between frames, with
identical physical results and checkpoint bytes. More workers can be slower;
the [Terra measurement](../native/MAC_GAME_KERNEL.md) records that result.

Automatic detection uses Linux's core sibling topology within the current
CPU affinity, or Windows' processor-core records (honouring process affinity
on a single processor group). It counts physical cores directly rather than
halving the logical CPU count. Without topology information it uses one
worker. Sources: [Linux CPU topology](https://docs.kernel.org/admin-guide/cputopology.html),
[Windows processor relationships](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformationex).

| Key | Name | Default | Range | Effect | Storage |
| --- | --- | --- | --- | --- | --- |
| `performance.workerThreads` | Solver workers | Physical cores (`0`) | `0` automatic, or integer 1–256 | Execution only; applies between frames, including after loading a save. | config.json |
| `display.readingPrecision` | Reading precision | 4 significant figures | `friendly`, `exact` | Hover card and status panel: kPa/°C/kg to 4 figures, or every digit with Pa and K. Values are the same. | config.json |
| `display.patternContrast` | Mixture pattern contrast | 0.04 | 0–0.15 (0 off) | Map pattern for mixed gases (also set from the overlay legend) | config.json |
| `display.traceMoleFraction` | Trace gas threshold | 1% | 0–10% | Gases below it are listed as traces | config.json |
| `panel.expanded` | Show panel details | on | on/off | Status panel starts expanded | config.json |
| `panel.showBoundary` | Outline development regions | on | on/off | Outline of a bounded development region | config.json |
| `keys.cycleOverlay` | Cycle mixture overlays | F5 | None, F1–F12 | Shift or Ctrl plus the key | config.json |
| `keys.toggleDetails` | Status panel details | F8 | None, F1–F12 | Shift or Ctrl plus the key | config.json |
| `keys.findFluid` | Find simulated fluid | F9 | None, F1–F12 | Shift or Ctrl plus the key | config.json |
| `keys.toggleBoundary` | Region outline | F10 | None, F1–F12 | Shift or Ctrl plus the key | config.json |
| `keys.openSettings` | Open settings | None | None, F1–F12 | Shift or Ctrl plus the key | config.json |

The status panel describes only the cell under the cursor: a solid tile reads
"solid tile, no fluid". The hover card's liquid volume line names the liquids.

## Save record

MACOLP03 section 3 (assigned by main, 2026-10-25), little endian:
`u32 version`, `u32 count`, then binary64 minimum oxygen share, minimum
oxygen partial pressure (Pa), maximum carbon dioxide share, maximum carbon
dioxide partial pressure (Pa), maximum oxygen share. Version 1 has count 5.
Version 2 (2026-10-27) has count 6 and adds nitrogen air (1.0); it is written
only for a nitrogen-air colony. Version 1 is written when a breathing rule
differs from its default and nitrogen air is off, so those colonies keep their
earlier bytes. Default colonies write no record; an absent record means the
defaults on generated air, and older saves load unchanged. Another record
version, count or flag value, or a value outside its range, refuses the save
before loading, with the reason.

## Not settings

Fixed by design: game gravity, thermal conduction, the profile's vacuum and
liquid laws, ledger fault bounds (1e-8 mass, 1e-6 energy), the 1 s identity
conversion time and 2 s immiscible settling (unsaved constants: changing them
would change arithmetic without a record), pacing (the game clock follows the
solver; physics never adapts to wall-clock load) and mixed pipes.

Test-only environment variables stay out of the window and override nothing a
player chooses unless set:

| Purpose | Variables |
| --- | --- |
| Solver/pipe selectors for development runs (unsupported for play) | `MA_COLONY_GAME_FLOOR`, `MA_COLONY_KERNEL3`, `MA_COLONY_KERNEL3_POSITIVE`, `MA_COLONY_KERNEL3_SURFACE`, `MA_COLONY_MIXED_PIPES=0`, `MA_COLONY_PIPE_SAVES=0`, `MA_COLONY_SUBSTEP_TRACE`, `MA_COLONY_FAST_PHASE_EOS`, `MA_COLONY_GERM_ORACLE`, `MA_MAC_WORKERS` |
| Harness | `MA_COLONY_STARTING_RESERVE` (validation scenes place the starting oxylite reserve), `MA_COLONY_SETTINGS_PRESET`, `MA_SETTINGS_OPEN` (opens the window on tab N at the menu and in a colony, unpaused; `mods` clicks the Mods-screen button), `MA_COLONY_SAVE_CONTROL`, `MA_COLONY_SAVE_FRAME`, `MA_COLONY_CONTINUATION_FRAMES`, `MA_COLONY_EXPECT_LOAD_REFUSAL`, `MA_COLONY_PIPE_CONTACT_HEAT` |
| Exactness controls | `MA_COLONY_CELL_MAP_CONTROL`, `MA_COLONY_UNCACHED_CELL_MAP`, `MA_COLONY_COPY_CONTROL`, `MA_COLONY_MARSHALLED_OUTPUT`, `MA_COLONY_MARSHALLED_RECEIPTS`, `MA_COLONY_RECEIPT_COPY_CONTROL`, `MA_COLONY_UNCACHED_VIEWS`, `MA_COLONY_VIEW_CACHE_CONTROL`, `MA_COLONY_WORLD_PASSIVE_CONTROL` |
| Logs and dumps | `MA_COLONY_LEDGER_LOG`, `MA_COLONY_HEAT_LOG`, `MA_COLONY_TERRAIN_LOG`, `MA_COLONY_STAGE_LOG`, `MA_COLONY_PUBLICATION_LOG`, `MA_COLONY_VIEW_TIMING_LOG`, `MA_COLONY_DUPE_LOG`, `MA_COLONY_PIPE_BIOLOGY_TIMING`, `MA_COLONY_PIPE_THERMAL_LOG`, `MA_COLONY_FAULT_DUMP`, `MA_COLONY_DUMP_FRAME`, `MA_COLONY_SLOW_DUMP`, `MA_COLONY_TRANSFER_DUMP`, `MA_COLONY_RELEASE_DUMP`, `MA_COLONY_FLOOR_ADMISSION_DUMP`, `MA_COLONY_FLOOR_PREFIX_DUMP`, `MA_ONI_BODY_HEAT_TRACE`, `MA_ONI_CHUNK_HEAT_TRACE`, `MA_ONI_STORAGE_HEAT_TRACE`, `MA_KERNEL_DEBUG` |
| Native harness and builds | `MA_VERIFY_*`, `MA_NATIVE_*`, `MA_TEST_NATIVE_LIBRARY`, `MA_BUILD_*` |

## Mod Manager

[Mod Manager](https://steamcommunity.com/sharedfiles/filedetails/?id=1843965353)
documents no interface for other mods; its update feed
([oxy.mods](https://github.com/O-n-y/oxy.mods) `version/info`) lists only its
own Workshop id. It shows a mod's version from its DLL, so
`MixedAtmospheres.Oni.dll` carries the package version (0.1.0, as
`mod_info.yaml`). Its backups and folder buttons work unchanged: the settings
folder is outside the mod folder and survives updates. Nothing depends on it.
