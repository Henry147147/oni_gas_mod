# Private playtest 0.1.1, 2026-11-08

Version 0.1.1 follows the user's review of the `edc362a` Windows playtest.
It is a private candidate; no public release or Workshop upload was made.

## Changes

- **Colony settings on an existing save** (user decision 2026-11-08): in a
  loaded colony the settings window edits that colony's solver tunables and
  solver presets, region simulation, evaporation below boiling and breathing
  thresholds. A change applies from the owner's next frame (breathing at once),
  is logged once, and the colony's next save records it; the main menu keeps
  choosing new-colony defaults. Native: additive
  `ma_oni_colony_change_options`; region simulation uses the existing
  `ma_oni_colony_set_activity_tiers`. Details: [settings](settings.md).
- **Windows memory growth:** the user's new Spaced Out! colony (Always
  simulate, two steps per tick) reached 52 GB after about 1,800 owner frames.
  The MinGW owner DLL's mimalloc never retired an ended thread's heap, and the
  game kernel ends threads every frame. mimalloc is now built with
  `MI_WIN_INIT_USE_FLS` ([cause and curves](windows-playtest-video-fixes.md#memory-growth-in-the-edc362a-playtest)).
  Linux was unaffected.
- **Status panel:** drag it by its title bar; **–**/**+** collapses it to the
  title bar and **×** hides it; an **Atmospheres** button before **Vitals** in
  the top bar shows or hides it. It shows the hovered cell's readings, a speed
  line and a frame/fluid-cell line; the "owns the asteroid's fluids" line and
  shortcut hints are gone. The hover card drops the trace/selection hint
  lines; "Breathing checks passed" is a Display preference, off by default.

`MacClosedWorld` arithmetic, numerical defaults, fixed gate inputs/bounds and
checkpoint encoding are unchanged. With no settings change, checkpoint bytes
are identical; a change is saved in the existing physical-image fields.

## Verification

- Linux `a914a00`: `tools/build.py --oni-native-observer` passes 93/93 native
  tests, 516 Python tests (11 platform skips), the managed suites (353
  settings checks) and the installed-game hook suites, including the top-bar
  declarations. The settings change compared all 61 `ma_mac_*` programs and the
  owner/backend/replay tests against `1c1eb33` from clean builds: identical
  output and exit status apart from timings and its added check.
- Windows `a914a00`: the MinGW cross-build passes 94/94 tests under Wine,
  including `native_oni_windows_allocator_thread_heaps`; both DLLs import only
  Windows system libraries. Both platform packages check 2,806 game-contract
  dependencies with 0 differences.
- WindowsPlayer under Proton, final package, the user's settings (Always
  simulate, two steps, evaporation on) on a new base-game colony: resident
  memory 3.91 GB at owner frame 150 and 4.04 GB at frame 1,050 (peak 4.08 GB);
  the `edc362a` DLLs grew 3.9 MB per frame in the same test. Passed with no mod
  errors.
- Linux game: the `playtest-ui` scene exercised collapse, drag, close, the
  top-bar tooltip and reopening at the dragged position. With the final
  `a914a00` package, the settings window changed steps per tick (1→2), region
  simulation (tiers→Always), evaporation (off→on) and the oxygen share
  (15→12%) on `base-defaults-p11-v1`; the colony saved at owner frame 333 and
  a fresh process restored exactly those values and continued. The settings
  record adds real mouse-click changes and further reloads.

Not established here: the actual Windows OS, a Spaced Out! colony (the host's
Steam session had been signed in on another computer, so Spaced Out! could not
activate) and representative-colony speed.

## Delivered private candidate

Code revision `a914a003acdd0b4d84ff2a72ef3fdf14f91472cb` (clean manifest).
Directory `/mnt/data/share/oni/playtest/MixedAtmospheres-0.1.1-a914a00/`,
ZIP `MixedAtmospheres-private-windows-0.1.1-a914a00.zip`, SHA256
`f2b7e0d58f342e9afcb20065692a5a0453e6b5543635ee1e21202ee7dd3ade0e`. Install
as before: extract, close ONI, run `install_windows.ps1`
([Windows validation](windows-validation.md)). Evidence is under the ignored
`artifacts/release-0.1.1/`.
