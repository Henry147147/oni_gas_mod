# Settings

Players configure the mod in one window. It opens from the **Settings** button
on Mixed Atmospheres' row in ONI's **Mods** screen, from the **Settings** button
on the status panel (main menu and colony), or from an optional shortcut. A
modal layer under the window keeps clicks, keys and scrolling from reaching the
game and pauses a running colony while it is open; Escape or **Close** closes
it. Code: `ModSettings*.cs` (model, files, window, runtime); tests:
`tools/SettingsTests`, `native/tests/oni_game_kernel_tests.cpp`.

## Finding the settings

- **Gameplay** opens first. Its **How it plays** button shows the start-of-colony
  dialog's "What plays differently" list again
  ([starting air](starting-air.md#the-start-of-colony-dialog)), with the shown
  settings' numbers. **CO₂ breathing limits** defaults to **Off**.
  Enough oxygen is always required; CO₂ still mixes, moves through pipes and
  displaces oxygen. Turn the switch on to enforce the saved CO₂ thresholds
  (by default 2% and 5 kPa, which bind together in normal base air of about
  250 kPa; that is ten times the CO₂ headroom of 0.5% and 500 Pa, the
  defaults before 2026-11-11).
  During play this switch applies immediately to the current colony and its
  next save. At the main menu it chooses the default for new colonies.
- **Gameplay → Boiling and condensation → Evaporation below boiling** defaults
  to **Off** (user decision 2026-10-28): liquids turn to vapour only by boiling,
  and vapour below its boiling point condenses back to liquid, as in ONI. The
  boiling point depends on the surrounding gas pressure (water: 100 °C at
  normal pressure, lower in thin air, and it boils in vacuum), and the air
  holds no humidity. Every gas follows the same rule with the total gas
  pressure, so a minor gas in a strongly compressed mixture can condense
  (chlorine in Terra's ~19 bar hydrogen pocket). **On** restores realistic
  evaporation into drier air and equilibrium humidity. It is saved in the
  colony's physical image and can be changed during play
  ([boil-only rule](../native/MAC_PHASE_CHANGE.md#boil-only-rule)). It is a
  gameplay rule rather than a solver tunable, so it is on Gameplay; solver
  presets keep it as chosen.
- **Gameplay → Gas sources → Gas source energy** defaults to **Compensated**
  (user decision 2026-11-09). It sets how gas from machines, vents, pumps,
  duplicants and sublimating tiles carries energy across their boundary.
  **Compensated:** a source's cell gets the gas's enthalpy, so it forms no
  cold spot, and a pump's or breather's cell gives up the extra energy of
  being drawn out, so it forms no warm spot. The same energy goes the other
  way across the source's room, so room temperatures change as in ONI.
  **Real physics:** the gas also carries that flow work, so filling a sealed
  room heats it and emptying one cools it, as a tank heats when filled.
  **Internal energy** (0.1.2 and earlier): room temperatures change as in
  ONI, but strong sources leave cold spots and pumps warm spots. It is saved
  in the colony settings record and can be changed during play
  ([source energy](../native/MAC_GAME_KERNEL.md#source-energy)).
- **Gameplay → Region simulation** defaults to **Activity tiers** for new
  colonies. Regions around actors and recent inputs run each game tick;
  disturbed remote regions run at a reduced cadence and eventually pause.
  Untouched regions pause without catching up when disturbed later. **Always
  simulate** updates every fluid region each game tick. The choice is saved
  per colony and can be changed during play; profiles 6–8 always simulate and
  keep it read-only.
- **Display & keys** contains reading precision, mixture appearance, status
  panel options and shortcuts. Changes apply immediately.
- **Advanced** contains **Solver** (including solver presets), **Breathing
  thresholds** (oxygen minimums, the toxic gas limit, CO₂ thresholds and the
  oxygen ceiling), **Performance**, **Display** and **Saved presets**. Each technical section
  is explicitly labelled Advanced. Solver settings and numeric breathing
  thresholds are saved per colony; at the main menu they are chosen for new
  colonies, in a colony they change that colony
  ([changing a loaded colony](#changing-a-loaded-colony)). Performance and
  display preferences remain editable.

Nitrogen elements, nitrogen starting air and the Hard mode preset have been
removed. World generation keeps its usual oxygen and polluted oxygen.
This is R&D: older saves need not be preserved. Saves containing the removed
nitrogen catalogue, missing the current breathing record, using its older
versions or using the earlier pressure profiles20–23 are refused with a reason
before native loading. Start a new colony for the corrected pressure update.

Colony settings are stored in the physical image (solver, evaporation rule and
region policy), the integrated owner checkpoint (region delivery and lifecycle
state), and MACOLP03 section 3 (breathing, the gas source energy and the toxic
gas limit; version 5, and version 3 and 4 records load with the defaults for
what they lack). Preferences live in the user's config and are not stored in
colony saves.

New colonies use game profile 12 (core32, owner wrapper14; profile 9 plus the
layered vertical remap of 2026-11-02, the convective adjustment of 2026-11-03
and the thin-gas drag of 2026-11-05) with mixed pipes and either phase rule.
Supported colonies created earlier retain their saved game profile 6
(core24/25), 7 (core26), 8 (core27), 9 (core28), 10 (core30) or 11 (core31).
See the
[game kernel](../native/MAC_GAME_KERNEL.md) for the profile history.
No setting selects the production kernel, the game-floor profile, an older
game-kernel profile or single-element packet pipes; profile 12's saved
tunables vary within
the ranges below. A value outside the measured one
is marked **experimental** in the window. The native library refuses values
outside the ranges (`ma_oni_colony_set_options`); a refusal starts the colony
with Default and shows a notice.

## Changing a loaded colony

In a colony (user decision 2026-11-08) the window shows that colony's current
colony settings and every one of them can be changed: solver tunables and the
solver presets (Default/Robust/Fast/Precise) on Advanced → Solver, Region
simulation, Evaporation below boiling, the CO₂ switch and the breathing
thresholds. Loading complete presets and **Reset colony settings to Default**
still choose new colonies' settings and stay at the main menu.

- A change applies to the running colony between owner frames and is logged
  once in Player.log (`Colony settings changed in this colony: key old → new`).
  The colony's next save records it: solver tunables and evaporation in the
  physical image, region simulation in the owner wrapper (a queued policy
  change included), breathing in section 3. Loading a save uses the values it
  holds.
- As the CO₂ switch always did, a change in a colony does not change the
  main-menu choices (`config.json`) for new colonies.
- Solver tunables and evaporation go to the owner's options
  (`ma_oni_colony_change_options`, additive to options version 2). The call
  waits for a frame in progress; every later frame, including frames already
  handed to the solver, uses the new values. Game frames stay 0.2 s; Solver
  steps per game tick only sets the largest step within each, and pacing
  holds the game clock to the solver, so no queued frame, pacing state or
  kernel cache keeps the old step.
  A changed liquid stiffness re-derives occupied liquid volumes as loading the
  save would; readings follow at the next published frame.
- Region simulation goes to the owner's activity policy
  (`ma_oni_colony_set_activity_tiers` with the session) at the next frame or
  zero-time flush. Always simulate wakes every region from that frame without
  catching up paused time; Activity tiers classifies regions from their saved
  activity history.
- The gas source energy goes to the owner (`ma_oni_colony_set_source_energy`
  with the session) between owner frames. The owner does not save it: the
  colony settings record does, and a loaded colony hands it to the owner
  before it restores (`ma_oni_colony_set_source_energy` with session 0).
- Breathing thresholds and the CO₂ switch apply at once in managed code.
- Game profiles 6–12 take solver and evaporation changes (profile 6 saves the
  phase rule as core24 or core25); profiles 9–12 take region simulation.
  Profiles 6–8 keep Region simulation read-only (they always simulate), and a
  development solver without saved options keeps its solver settings
  read-only; the window gives the reason on the row.
- A value outside its range, a stale or faulted owner or a pending rocket
  cleanup is refused: the colony keeps its values and a notice says why.
- With no change made, solver arithmetic and checkpoint bytes are unchanged;
  bit-equal options change nothing. No save format changed.

## Presets

Under **Advanced → Solver**, Default, Robust, Fast and Precise apply only
solver tunables, keeping your gameplay choice. Under **Advanced → Saved
presets**, complete presets replace all colony rules, including breathing,
and keep preferences unless a user preset explicitly contains them. Relaxed
breathing is the exception: it keeps the CO₂ switch (user decision
2026-11-11), so it only lowers the oxygen minimums.

| Preset | Colony settings |
| --- | --- |
| Default (recommended) | Default solver and oxygen rules; CO₂ limits off; no evaporation below boiling; Activity tiers. |
| Robust | Pressure/heat iteration caps 1600/3200. A converged solve stops early, so results are identical wherever Default completes; a difficult solve gets more room before it faults. |
| Fast (experimental) | Pressure tolerance 1e-6: fewer pressure iterations, pressures settle less exactly. |
| Precise (experimental) | Two solver steps per game tick, pressure tolerance 1e-10 and the Robust caps; about twice the solver time. |
| Relaxed breathing | Oxygen ≥ 10% and 10 kPa; keeps the CO₂ switch as the player set it (the CO₂ thresholds take their defaults). |
| Strict breathing | Oxygen ≥ 18% and 18 kPa; enables CO₂ limits of 1% and 2.5 kPa. |

Lower caps are not offered: they are never faster, and reaching a cap stops
the simulation. Players save the shown settings (colony settings and
preferences) as their own presets and overwrite, rename, delete, load, copy to
the clipboard and import from it. In a colony, **Save this colony's
settings** keeps that colony's values as a preset and the solver presets
apply to that colony; loading complete presets and resetting colony settings
happen in the main menu. **Reset colony settings to Default**
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

All are recorded per colony and can be changed in a loaded colony
([above](#changing-a-loaded-colony)). Native ranges are
those of `MAOniColonyOptions` (`oni_colony.h`, options version 2, 88 bytes,
which adds `evaporation_below_boiling`).

| Key | Name | Default | Range (window steps) | Effect | Storage |
| --- | --- | --- | --- | --- | --- |
| `solver.stepsPerFrame` | Solver steps per game tick | 1 | 1, 2, 4 (≠1 experimental) | Maximum step 0.2/0.1/0.05 s (binary32): finer fast flow, 1×/2×/4× solver time | core24/25 timestep |
| `solver.pressureTolerance` | Pressure solve tolerance | 1e-8 | 1e-12–1e-4 (decades; ≠1e-8 experimental) | Relative pressure-root accuracy | core24/25 |
| `solver.pressureIterationCap` | Pressure iteration cap | 400 | 400–65536 (400…12800) | Room for difficult pressure solves; no result changes where Default converges | core24/25 |
| `solver.heatIterationCap` | Heat iteration cap | 800 | 800–65536 (800…25600) | Same for heat solves | core24/25 |
| `solver.flowParts` | Flow parts per step | 2 | 1–8 (≠2 experimental) | Transport sub-steps per solver step | core24/25 |
| `solver.gasHeatMixing` | Gas heat mixing | 8 | 0–32 (≠8 experimental) | Temperature mixing by moving gas, energy conserved (user decision 2026-10-24) | core24/25 |
| `solver.liquidStiffness` | Liquid stiffness | 0.04 | 0.01–1 (≠0.04 experimental) | Fraction of the catalogue bulk modulus; sealed liquids stiffer | core24/25 |
| `solver.sharpenLiquidInterfaces` | Sharpen liquid surfaces | on | on/off (off experimental) | Bounded interface correction keeps surfaces crisp | core24/25 |
| `solver.interfaceDetail` | Thinnest sharpened film | 0 | 0–0.5 of a cell (≠0 experimental) | Thinner films are left unsharpened | core24/25 |
| `solver.phaseRelaxationSeconds` | Boiling/condensation time | 1 s | 0.1–10 s (≠1 experimental) | Liquid/vapour relaxation time constant (other conversions keep 1 s) | core24/25 |
| `solver.traceThresholdKg` | Trace removal threshold | 1e-11 kg | 1e-11–1e-6 kg (≠1e-11 experimental) | Per-constituent booked trace sink; 1 mg ceiling | core24/25 |
| `simulation.regionActivity` | Region simulation (Gameplay) | Activity tiers | Activity tiers / Always simulate | Select adaptive regions or simulate every region each tick | core28 policy and owner wrapper14 |
| `liquids.evaporationBelowBoiling` | Evaporation below boiling (Gameplay) | off | on/off | Off: liquid/vapour pairs compare saturation with the total gas pressure (boil-only, ONI-like condensation, no humidity); on: the vapour's partial pressure (evaporation into unsaturated gas) | core27/28 phase-rule selector (1 off, 0 on) |
| `gases.sourceEnergy` | Gas source energy (Gameplay) | Compensated | Compensated / Real physics / Internal energy | How machine, vent, pump, duplicant and sublimation gas carries energy: enthalpy at the cell with the room returning it; enthalpy (flow work); internal energy only | section 3 v4 (`ma_oni_colony_set_source_energy` modes 2/1/0) |
| `breathing.co2Limits` | CO₂ breathing limits | off | on/off | Enforce the CO₂ share and partial pressure thresholds; oxygen and the toxic gas limit remain required when off | section 3 v3 |
| `breathing.minimumOxygenFraction` | Minimum oxygen share | 15% | 5–25% | Breathable with at least this oxygen share by moles (polluted oxygen counts) | section 3 |
| `breathing.minimumOxygenPressurePa` | Minimum oxygen partial pressure | 15 kPa | 1–50 kPa | …and at least this oxygen partial pressure | section 3 |
| `breathing.maximumToxicFraction` | Maximum toxic gas share | 5% | 0–100% (0, 1, 2, 5, 10, 20, 50%, No limit) | Not breathable once chlorine, sour gas and nuclear fallout (where the content has it) together reach this share by moles; 100% is no limit. Always applies, like the oxygen minimums | section 3 v5 |
| `breathing.maximumCo2Fraction` | Maximum carbon dioxide share | 2% (0.5% until 2026-11-11) | 0–10% | Only when CO₂ limits are on: carbon dioxide at most this share | section 3 |
| `breathing.maximumCo2PressurePa` | Maximum carbon dioxide partial pressure | 5 kPa (500 Pa until 2026-11-11) | 0–10 kPa | Only when CO₂ limits are on: at most this partial pressure | section 3 |
| `breathing.maximumOxygenFraction` | Maximum oxygen share | no limit (1) | 30–100% | Cells richer in oxygen fail. Below 100% can make pure generated oxygen unbreathable; the window warns | section 3 |

## Preferences

**Advanced → Performance → Solver workers** defaults to **Physical cores**: one worker
per physical CPU core (12 on a 5900X, 16 on a 9950X), without the former
two-logical-CPU deduction. Enter a count from 1 through 256 and press **Set**
to use fewer workers or SMT threads; **Auto** restores the physical-core
default. Small grids still use fewer workers when they
have fewer blocks. A running colony takes the change between frames, with
identical physical results and checkpoint bytes. More workers can be slower;
the [Terra measurement](../native/MAC_GAME_KERNEL.md) records that result.

**Advanced → Performance → Log frame timing** is off by default. Turn it on, play for a
minute or two, then send Player.log (or its `frame-log` lines); `MA_FRAME_LOG=1`
does the same without the window. The `perf frames` line the owner writes every
150 frames is always on.

The default stays at physical cores. Reserving cores for Unity was measured
and was not clearly better (Ryzen 9 5900X, 12 cores, game and solver on the
same 12 logical CPUs, base-game Terra; a 9950X was not available). Solver
alone, 100,000 active cells, owner step ms (mean, one run each): 12 workers 1,423;
10: 1,762; 8: 1,553; 6: 1,683; 4: 1,723; 2: 3,456 (the pool scales poorly past
about 4-6 workers). In the game at ultra speed, where the owner is nearly
always busy, Unity frame p95/p99 ms and owner step ms: 12 workers 81/182 and
93; 8: 85/207 and 112; 6: 75/150 and 90 (differences are inside the run-to-run
spread); at fast speed 24 and 6 workers gave 78/184 and 69/197. A player who
sees camera hitches while the solver is saturated can try physical cores minus
two; `MA_FRAME_LOG=1` shows whether Unity's frame time follows
([main-thread smoothness](colony-owner.md#main-thread-smoothness)).

Automatic detection uses Linux's core sibling topology within the current
CPU affinity, or Windows' processor-core records (honouring process affinity
on a single processor group). It counts physical cores directly rather than
halving the logical CPU count. Without topology information it uses one
worker. Sources: [Linux CPU topology](https://docs.kernel.org/admin-guide/cputopology.html),
[Windows processor relationships](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformationex).

| Key | Name | Default | Range | Effect | Storage |
| --- | --- | --- | --- | --- | --- |
| `performance.workerThreads` | Solver workers | Physical cores (`0`) | `0` automatic, or integer 1–256 | Execution only; applies between frames, including after loading a save. | config.json |
| `performance.logFrameTiming` | Log frame timing | Off | on, off | Writes a `frame-log` line to Player.log every 15 s (Windows: `%USERPROFILE%\AppData\LocalLow\Klei\Oxygen Not Included\Player.log`): Unity frame time mean/p50/p95/p99/max, frames over 33/50/100 ms, milliseconds per frame in this mod, blocked in native calls and elsewhere, garbage collections and allocation per frame. For diagnosing stutter ([main-thread smoothness](colony-owner.md#main-thread-smoothness)); applies at once, not saved in colonies. | config.json |
| `performance.frameTimingFix` | Frame timing fix | On | on, off | With V-Sync on, some displays (very high or variable refresh rates, such as a 500 Hz G-SYNC monitor) make Unity report each frame as one refresh (2 ms) while about 10 ms pass, and put the missing time into rare multi-second frames that ONI caps at 0.2 s: the colony runs at a fraction of its speed and motion crawls, then jumps. When the mod measures that (two 240-frame windows with most frames reported under 75% of their real length and the total under 80%), it turns V-Sync off and caps the frame rate at the display's refresh rate, and logs it. Off restores the game's V-Sync and frame cap. Each `perf frames` line reports `engine_frame_ms`, `real_frame_ms`, `short_frames`, `engine_longest_ms`, `vsync` and `timing_fix`. | config.json |
| `display.readingPrecision` | Reading precision | 4 significant figures | `friendly` (4), `sig3`, `sig2`, `sig1`, `exact` | Hover card and status panel: kPa/°C/kg to 4, 3, 2 or 1 significant figures (1 figure: 1,247 kg reads 1000, 269.7 kPa reads 300), or every digit with Pa and K. Values are the same. | config.json |
| `display.patternContrast` | Mixture pattern contrast | 0.04 | 0–0.15 (0 off) | Map pattern for mixed gases (also set from the overlay legend) | config.json |
| `display.traceMoleFraction` | Trace gas threshold | 1% | 0–10% | Gases below it are listed as traces | config.json |
| `display.showBreathingPassed` | Show “Breathing checks passed” | off | on/off | Hover card adds that line when a cell passes every breathing check; failed checks always show | config.json |
| `panel.visible` | Show status panel | on | on/off | Status panel shown in a colony; its × button and the **Atmospheres** button beside Vitals in the top bar change it | config.json |
| `panel.expanded` | Show panel details | on | on/off | Readings and speed/frame lines below the title bar; its –/+ button and the details shortcut change it | config.json |
| `panel.showBoundary` | Outline development regions | on | on/off | Outline of a bounded development region | config.json |
| `keys.cycleOverlay` | Cycle mixture overlays | F5 | None, F1–F12 | Shift or Ctrl plus the key | config.json |
| `keys.toggleDetails` | Status panel details | F8 | None, F1–F12 | Shift or Ctrl plus the key | config.json |
| `keys.findFluid` | Find simulated fluid | F9 | None, F1–F12 | Shift or Ctrl plus the key | config.json |
| `keys.toggleBoundary` | Region outline | F10 | None, F1–F12 | Shift or Ctrl plus the key | config.json |
| `keys.openSettings` | Open settings | None | None, F1–F12 | Shift or Ctrl plus the key | config.json |

The status panel describes only the cell under the cursor: a solid tile reads
"solid tile, no fluid". Below the readings it shows the solver and game speed,
the owner frame and fluid cell count, and the renderer or pipe state only when
unusual. Drag its title bar to move it; the position is remembered in the
game's player preferences (`MixedAtmospheres.StatusPanel.X`/`Y`), not in
config.json. The main menu shows only its title bar and **Settings** unless
something is wrong. The hover card's liquid volume line names the liquids;
trace gases are left out without a hint line (select the cell for all of them).

## Save record

**Reserved 2026-10-27 immediately after fetching `f22f7ed`:** MACOLP03
section 3 record version **3** replaces the removed nitrogen-air record.
Every new save writes it, including defaults. This makes older builds refuse
rather than silently apply their former CO₂ limits. Old R&D saves are not
supported.

Version 3, little endian, 56 bytes: `u32 version = 3`, `u32 count = 6`, then
binary64 minimum oxygen share, minimum oxygen partial pressure (Pa), maximum
CO₂ share, maximum CO₂ partial pressure (Pa), maximum oxygen share, and CO₂
limits (`0.0` off, `1.0` on). Version 4 (2026-11-09, 64 bytes, count 7) adds
the gas source energy mode (`0.0` internal, `1.0` enthalpy, `2.0`
compensated). Version **5** (2026-11-11, 72 bytes, count 8), which every new
save writes, adds the maximum toxic gas share (0–1). Version 3 and 4 records
load with the defaults for what they lack (Compensated, 5%). Missing records,
other versions/counts, invalid switches or modes and out-of-range thresholds
refuse before loading. The physical solver
checkpoint format was unchanged by this historical settings change; the later
profile9 integration writes core28 and owner wrapper14.

## Verification (2026-10-27)

The full Linux `tools/build.py` run passes: 89 Release CTests, 433 Python tests
(9 platform skips), 92 managed/native checks, 46 presentation tests, 299
settings checks and the game declaration/hook suites. A rebuilt Windows x64
native suite passes 89/89 under Wine; this is not a Windows OS playtest.

The Linux in-game `starting-air` scenario passes all 13 checks: vanilla oxygen,
no registered nitrogen phases, the oxygen reserve, share sensors, CO₂ limits
Off by default, live opt-in/opt-out and matching display, and pure CO₂ still
being unbreathable. A saved colony cold-loads with CO₂ limits Off, independent
of settings changed after its save. An old save with nitrogen is refused before
native loading, without starting an owner or changing the source file. The
Gameplay and Advanced pages were inspected and clicked in the actual game;
README captures show them. Private run records are under ignored
`artifacts/gameplay-settings-*` and `artifacts/gameplay-nitrogen-refusal`.

MacClosedWorld arithmetic and fixed solver gate inputs/bounds are unchanged.
Game catalogue identity changes when nitrogen content is removed, and every
new save carries breathing record v3. The native physical checkpoint format
is unchanged. These focused checks do not close the separate performance,
accuracy and full release-acceptance work.

## Verification of the boil-only setting (2026-10-28)

Linux Release passes 89 CTests. All 96 `ma_mac_*` program outputs (62 CTests
and 34 further programs) match the parent from clean builds except the added
boil-only control's lines. The settings (310), managed/native (92),
presentation (46) and snapshot (228) suites pass. In the Linux game (Terra),
the `phase` scenario passes in a Default colony: 57 °C water stays liquid, the
0.131 kg of vapour placed in its air falls to 8.0e-7 kg within 12 s, and the
mass/energy ledgers close (1.5e-16/1.1e-15). With **Evaporation below boiling**
on (a preset file), the same pool evaporates 7.2 kg into the air instead.

## Region activity controls (2026-10-28)

The integrated profile has native controls for paused, active and remote
regions; ordered frame inputs; zero-time lifecycle changes; saved pending
frames and cold continuation; and selected biology, conversions, trace removal
and space sinks. Engine-free managed controls cover footprint geometry,
including owner-boundary contacts, census transitions, delivery admission and
wrapper14 metadata. A clean integration build passed 89 Linux Release CTests
and 437 Python tests; the activity controls passed 134 managed checks and 12
native-image checks. The 87 compared MAC program results matched the prior
profile8 revision in numeric output and exit status, with timings excluded.
Focused native owner controls also passed ASan/UBSan.
The build preceded a subsequent managed diagnostic guard fix, so the full
checks must be repeated on the final source.

The separate [real-game activity controls](region-activity-controls.md)
exercise live actors, solid footprints, ports and pending-frame save/load.
Their current-package game results and Windows OS playtest remain pending;
these controls do not establish sustained 3× or the other release gates.

## Verification of in-colony changes (2026-11-08)

Native: the `oni_game_kernel` live-options control (profiles 8 and 12 with a
water pool) shows that bit-equal options keep the checkpoint bytes; a change
is reported by `copy_options` at once and saved in the core27/core32 fields;
four solver steps per tick start with the next frame; the changed image
continues byte for byte after a cold restore; out-of-range values and a stale
session are refused without change; and a core20 colony is incompatible. The
replay self-test journals an options change after its save and replays it
exactly. All 93 Release CTests with the ONI backend pass, and `tools/build.py`
passes (516 Python tests with 11 skips, 21 CTests, 92 managed/native, 46
presentation and 345 settings checks). All 61 `ma_mac_*` programs (with their
CTest arguments where registered) and the owner, backend and replay tests
give the same output and exit status as the parent `1c1eb33` from clean
builds, timings excluded, apart from the new PASS line.

Linux game, base-game `base-defaults-p11-v1` (profile 11), `colony-owner-load`:

- Window controls driven by `MA_SETTINGS_CHANGE`: pressure cap 1600, two
  solver steps, Always simulate, evaporation on and a 12% oxygen share,
  logged once each and read back from the owner; 300 frames, then a save. A
  new process restored exactly these values, switched back to Activity tiers
  and one step, set liquid stiffness 0.1, CO₂ limits on and 12 kPa oxygen,
  ran 300 frames and saved; a third process restored those and ran 300
  frames without a change.
- Real mouse clicks in the open window: evaporation on, Always simulate, the
  Robust solver preset, a 3200 pressure cap and a 12% oxygen share, while
  the colony ran; then the pause menu's Save. A new process restored the
  clicked values and ran 300 frames.

Every run passed with the owner healthy, no mod errors and closed ledgers
(mass ≤ 2.5e-15, energy ≤ 1.2e-14). Private run records and screenshots are
under the ignored `artifacts/live-settings/`.

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
| Solver/pipe selectors for development runs (unsupported for play) | `MA_COLONY_GAME_FLOOR`, `MA_COLONY_KERNEL3`, `MA_COLONY_KERNEL3_POSITIVE`, `MA_COLONY_KERNEL3_SURFACE`, `MA_COLONY_KERNEL3_PROFILE8`, `MA_COLONY_MIXED_PIPES=0`, `MA_COLONY_PIPE_SAVES=0`, `MA_COLONY_SUBSTEP_TRACE`, `MA_COLONY_FAST_PHASE_EOS`, `MA_COLONY_GERM_ORACLE`, `MA_MAC_WORKERS` |
| Harness | `MA_COLONY_STARTING_RESERVE` (validation scenes place the starting oxylite reserve), `MA_COLONY_SETTINGS_PRESET`, `MA_SETTINGS_OPEN` (opens the window on tab N at the menu and in a colony, unpaused; `mods` clicks the Mods-screen button), `MA_SETTINGS_CHANGE` (`key=value;…;solver-preset=<id>`: once the window opened in a colony, makes those changes through its controls), `MA_COLONY_SAVE_CONTROL`, `MA_COLONY_SAVE_FRAME`, `MA_COLONY_CONTINUATION_FRAMES`, `MA_COLONY_EXPECT_LOAD_REFUSAL`, `MA_COLONY_PIPE_CONTACT_HEAT` |
| Frame-time measurement (docs/colony-owner.md#main-thread-smoothness) | `MA_FRAME_LOG=1` (frame-time log in Player.log; the preference "Log frame timing" does the same), `MA_COLONY_WORKERS` (solver worker count for sweeps, overrides the preference), `MA_COLONY_RECEIPT_BUDGET_MS` (receipt time per publication, 0 for no limit), `MA_COLONY_PRESENTATION_EAGER=1` (build every reading at publication, the earlier behaviour), `MA_COLONY_NATIVE_WAIT_LOG=1` (native frame phase timing to stderr) |
| Exactness controls | `MA_COLONY_ACTIVITY_CONTROL`, `MA_COLONY_CELL_MAP_CONTROL`, `MA_COLONY_UNCACHED_CELL_MAP`, `MA_COLONY_COPY_CONTROL`, `MA_COLONY_MARSHALLED_OUTPUT`, `MA_COLONY_MARSHALLED_RECEIPTS`, `MA_COLONY_RECEIPT_COPY_CONTROL`, `MA_COLONY_UNCACHED_VIEWS`, `MA_COLONY_VIEW_CACHE_CONTROL`, `MA_COLONY_WORLD_PASSIVE_CONTROL` |
| Logs and dumps | `MA_COLONY_LEDGER_LOG`, `MA_COLONY_HEAT_LOG`, `MA_COLONY_TERRAIN_LOG`, `MA_COLONY_STAGE_LOG`, `MA_COLONY_PUBLICATION_LOG`, `MA_COLONY_VIEW_TIMING_LOG`, `MA_COLONY_DUPE_LOG`, `MA_COLONY_PIPE_BIOLOGY_TIMING`, `MA_COLONY_PIPE_THERMAL_LOG`, `MA_COLONY_FAULT_DUMP`, `MA_COLONY_DUMP_FRAME`, `MA_COLONY_SLOW_DUMP`, `MA_COLONY_TRANSFER_DUMP`, `MA_COLONY_RELEASE_DUMP`, `MA_COLONY_FLOOR_ADMISSION_DUMP`, `MA_COLONY_FLOOR_PREFIX_DUMP`, `MA_ONI_BODY_HEAT_TRACE`, `MA_ONI_CHUNK_HEAT_TRACE`, `MA_ONI_STORAGE_HEAT_TRACE`, `MA_KERNEL_DEBUG` |
| Native harness and builds | `MA_VERIFY_*`, `MA_NATIVE_*`, `MA_TEST_NATIVE_LIBRARY`, `MA_BUILD_*` |

## Mod Manager

[Mod Manager](https://steamcommunity.com/sharedfiles/filedetails/?id=1843965353)
documents no interface for other mods; its update feed
([oxy.mods](https://github.com/O-n-y/oxy.mods) `version/info`) lists only its
own Workshop id. It shows a mod's version from its DLL, so
`MixedAtmospheres.Oni.dll` carries the package version (0.1.2, as
`mod_info.yaml`). Its backups and folder buttons work unchanged: the settings
folder is outside the mod folder and survives updates. Nothing depends on it.
