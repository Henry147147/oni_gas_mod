# Native fluid rendering and status display

This document covers how cells owned by the
[sealed native domain](sealed-native-domain.md) reach ONI's own world renderer,
and the player-visible status display that reports the mod's state. The
calibrated GPU byte check that binds rendered gas colour, density-opacity and
hazard channels to the completed physical frame is in
[GPU texture readback](native-domain-readers.md#gpu-texture-readback); CPU
publication counters alone cannot pass it. The presentation model itself is in
[mixture presentation](mixture-presentation.md).

## Player-visible status and navigation

The mod installs a persistent status display once its load checks succeed. It
appears in menus and colonies, including normal observation with no custom
fluid owner. It distinguishes native physics, visual fixtures, preparation,
active custom fluid and a stopped domain; domain and presentation failures stay
visible. It uses the immutable presentation provider; native scalar cells are
never interpreted as complete mixture inventories, and an ordinary load reports
native ONI physics with zero custom readings rather than manufacturing mixture
data for unowned cells.

The display shows the reading frame, supplied cell count, renderer state and
the mixture under the cursor, falling back to the first supplied cell. Mass,
temperature, gas pressure, oxygen partial pressure, composition and breathing
warnings share that completed reading. Liquid cells also show their mechanical
pressure and liquid volume fraction. Small positive values use scientific
notation instead of rounding to zero.

An optional cyan outline marks a filled rectangular set of supplied cells; it
does not connect sparse readings across unowned cells.

| Shortcut (Shift or Control) | Effect |
| --- | --- |
| F8 | Expand/collapse the details |
| F9 | Focus the camera on the region |
| F10 | Show/hide the boundary |
| F5 | Cycle composition, gas pressure, oxygen partial pressure, breathability and normal view |

Focusing sets the camera's current **and target** zoom through
`SnapTo(position, size)`; assigning only the current size drifted back to the
old colony-wide target. The F5 cycle handling on Windows is described in
[Windows validation](windows-validation.md#windows-game-checks-2026-10-17).

The [README](../README.md#compile-install-and-enable) has build, installation
and manual visual-scene instructions. The packaged mod defaults to observation;
`tools/configure_visual_demo.py` explicitly selects a disposable air, water or
mixed-fluid scene, or a `pillared` / `pillared-water`
[configured room](native-configured-domain.md) with interior solids (short
scenes only). Preparation pauses after its 120-frame control so a person can
inspect it. `--long-run` instead keeps the scene running for ten ONI cycles
(30,000 frames, or `--frames` up to 100,000) under a 48-hour limit and appends
[development telemetry](../tools/ONI_HEADLESS.md#long-development-runs). These
remain bounded development domains.

Development scenes write `status-display.json` beside their other reports: the
displayed state, reading frame, sample, camera size, accepted shortcuts and
overlay. Ordinary mod use performs no status-report file I/O. The GUI object
survives scene loading while the presentation lifecycle retires old colony
readings.

## Flow guards and liquid fill

The sealed domain keeps ONI's gas/liquid flow guard flags on the original
native compatibility cells, so the original simulator cannot advance fluid that
belongs to the custom owner. Those flags must not reach game readers as real
blocked cells.

The owned game-data copy therefore clears just those two synthetic flags for
its owned fluid cells. Other properties and all unowned cells stay copied from
the native snapshot, and the original native guards, mutation exclusions and
saved compatibility state stay intact. Grid projection checks require that the
published fluid is not liquid-impermeable.

Without this, ONI's stock phase texture classified the published guard flags
as an impermeable tile and hid valid water colour data, so full water cells
looked empty (beige). The repair keeps the shipped liquid shader, colour/data
textures, lighting, caustics, animation and layer ordering; it does not replace
the liquid renderer with a coloured overlay.

Acceptance bytes from matched Windows controls (same native binaries, same
water-gravity input, identical final physical images):

| Readback | Before repair | After repair |
| --- | --- | --- |
| Published property byte, each owned cell | `3` | `0` |
| Phase buffer and bound phase texture | `[250, 0, 0, 140]` | `[0, 255, 0, 140]` |
| Water texture | `[14, 101, 130, 255]` | unchanged |

## Gas texture channels

The legacy gas-pressure and danger textures store their scalar bytes in
alpha. The diagnostic `ExpectedGas.Pressure` is that visual opacity byte (150
or 151 in the six-cell air scene), not physical pressure; the physical
pressure stays in the immutable thermodynamic reading (about 100.5 kPa).

`RenderingInputs` records the owned texture bindings and bounded scene pixels:
the phase buffer, bound phase texture, water texture and the bound gas colour,
pressure and danger textures. Each read covers only the room's rectangle, runs
after the private scene completes on the publication thread and restores the
previous render target. These diagnostics are not a performance measurement.

## Verified status

All results are six-cell development scenes on ONI 744825.

- **Windows OS (MSVC).** Repaired water loads, passes the paused autosave and
  120 exact continued frames, and keeps the repaired pixels through a second
  session after a return to the main menu; hover and selected details show
  blue filled water, 1100 kg, 220 MPa, no gas phase and no breathable
  atmosphere. A fresh gas scene matches all six GPU gas colours to the
  presentation samples, with opacity matching every cell's density encoding.
  Mouse hover without selection shows nitrogen 78.295% and oxygen 21.705%;
  selected details keep the helium trace (`7.21491E-26%`). An observer on the
  real hover drawer records the same immutable physical object, completed
  revision and publication generation as the GPU report. The later
  [Windows actor-batch water control](sealed-native-domain.md#current-evidence)
  keeps the repaired rendering through both reloads.
- **Linux Unity.** The water scene shows blue liquid, and the panel and hover
  agree on 1100 kg, 300 K, 220 MPa fluid pressure, 100% liquid volume and no
  breathable gas; the README screenshot is the original capture of this run.
  Ten physical Shift-key actions (collapse/expand, boundary hide/show, focus,
  four overlay modes, normal view) pass at an unchanged physical frame with a
  stable camera size. Calibrated GPU checks pass for gas and water in two
  sessions each ([GPU readback](native-domain-readers.md#gpu-texture-readback)).
  A normal colony reports native ONI physics through 120 forwarding controls
  with byte-identical native snapshots and no mixture renderer binding.
- **WindowsPlayer under Proton.** A Linux-origin gas colony and the Linux water
  save each load, resave before advancing, match 120 continued frames and pass
  the calibrated GPU checks and the same ten key actions; the water source save
  stays byte-identical and an independent decoder accepts its frame-input
  journal. This is not Windows OS evidence.

Scripted function-key input must use direct XTEST key events: the desktop tool
inferred an extra Alt modifier for function keys. The display checks change no
physical state and do not relax the rendering or save gates.

## Open

- Other liquid materials, free surfaces and sustained rendering performance.
- Four-gas production hover in an owned colony
  ([mixture presentation](mixture-presentation.md)).
- The boundary outline can cross a hover card at some zooms, and the panel
  samples the hovered or first cell, which can differ from the selection.
- An older Windows water input logs `Missing KPrefabID reference: 246809` on
  resave ([persistence](native-domain-persistence.md#same-process-main-menu-reload));
  newer inputs do not.
- Whole-colony and first-playable acceptance.
