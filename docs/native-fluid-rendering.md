# Native fluid guards and world rendering

New native-domain controls also require [calibrated GPU byte checks](native-gpu-readback.md)
for gas colour, density-opacity and hazard channels. The native phase/mass
outline establishes page-texture coordinates before interpreting a readback;
CPU publication counters alone cannot pass this check.

## Player-visible status and navigation

The mod now installs a persistent status display after its load checks succeed.
It appears in menus and colonies, including normal observation with no custom
fluid owner. It distinguishes native physics, visual fixtures, preparation,
active custom fluid and a stopped domain. Domain/presentation failures remain
visible. It uses the existing immutable presentation provider; native scalar
cells are never interpreted as complete mixture inventories.

The display shows the reading frame, supplied cell count, renderer state and
the mixture under the cursor, falling back to the first supplied cell. Mass,
temperature, gas pressure, oxygen partial pressure, composition and breathing
warnings share that completed reading. Liquid cells also show their supplied
mechanical pressure and liquid volume fraction. Positive small values use
scientific notation instead of rounding to zero.

An optional cyan outline marks a filled rectangular set of supplied cells. It
does not connect sparse readings across unowned cells. Shift+F8 toggles the
details, Shift+F9 focuses the region, Shift+F10 toggles its boundary and Shift+F5
cycles composition, gas pressure, oxygen partial pressure, breathability and
normal view. Control is also accepted as a modifier. Focusing sets the camera's
current **and target** zoom through `SnapTo(position, size)`; the earlier
one-sided size assignment drifted back to the old colony-wide target.

The main [README](../README.md#compile-install-and-enable) contains build,
installation and manual visual-scene instructions. The packaged mod defaults
to observation; `tools/configure_visual_demo.py` explicitly selects a disposable
air, water or mixed-fluid scene. Preparation pauses after its 120-frame control
so a person can inspect it. These remain bounded development domains.

Private development scenes retain `status-display.json` alongside their other
reports. It records the displayed state, reading frame, sample, camera size,
accepted shortcuts and overlay. Ordinary mod use performs no status-report file
I/O. The GUI object survives scene loading while the existing presentation
lifecycle retires old colony readings.

### Actual game checks, 2026-10-17

`visible-status-Windows-load-v66` loads a Linux-origin gas colony in the real
Windows Unity player under Proton, resaves before advancing, and matches 120
continued physical frames. Its calibrated GPU/hover checks pass. Ten physical
key actions verify collapse/expand, boundary hide/show, camera focus, all four
overlay modes and return to normal view. The camera stays at size 4 and every
action retains physical frame 120 and the paused state. This is WindowsPlayer
on Linux, not an actual Windows OS run.

`visible-status-Linux-water-v70` uses final display source `cac50ce` and passes
the native Linux water scene, calibrated texture/phase checks and ordinary
paired save. Its ten Shift-key actions all pass at the unchanged physical frame
121, including a stable camera size of 4. The actual world shows blue liquid;
the panel/hover agree on 1100 kg, 300 K, 220 MPa fluid pressure, 100% liquid
volume and no breathable gas. The screenshot in the main README is the original
Unity capture from this run.

Earlier scripted shortcut attempts failed because the desktop tool inferred an
extra Alt modifier for function keys. Direct XTEST physical key events receive
the correct modifiers and pass; the earlier failure records remain retained.
These controls test the display without changing physical state or relaxing
the existing rendering/save gates.

Retained SHA-256 records:

| Record | SHA-256 |
| --- | --- |
| Windows gas result | `4423a274c510947fdfead3bbb8e3aabe62864ecfb2f2565207b9c29c53153644` |
| Windows gas physical-key review | `b9d292ae4cfb3cbfe3bcd9e3e983a65be793ccae60fd9d6ea4d78addbb9dc764` |
| Linux water result | `8afc01327f1ee19fef93566dff6f521b47647c4606696b3e5899d9b708c71d80` |
| Linux water physical-key review | `f530466df2acaf1e72b86ac7377178d3837c4c9d4105e49b27471adf8e7a6502` |
| Original water screenshot | `b7b65e004ae6b30ce7ec31e1678fcddc2122486790575656a3c056513493d619` |

The sealed native development domain keeps gas/liquid flow guards in the
original ONI compatibility cells. Those guards prevent the original simulator
from advancing fluid that belongs to the custom physical owner. They must not
be presented as real blocked cells to game readers.

The owned game-data copy now clears just those two synthetic flags for its
six fluid cells. Other properties and all unowned cells remain copied from the
native snapshot. The original native guards, mutation exclusions and saved
compatibility state remain intact. Grid projection checks also require that
the published fluid is not liquid-impermeable.

This fixes full water cells that previously looked empty. ONI's stock phase
texture classified their published guard flags as an impermeable tile, hiding
otherwise valid water colour data. The repair retains the shipped liquid
shader, colour/data textures, lighting, caustics, animation and layer ordering.
It does not replace the liquid renderer with a coloured overlay.

## Matched Windows evidence

The read-only `windows-liquid-render-baseline-r2` control and
`windows-liquid-render-fixed-r1` use the same native binaries and staged
water-gravity colony input. Both pass 91 Core groups, 41 presentation groups,
31 inspection groups, actual Windows Unity load/paused-autosave checks and all
120 exact physical continuation frames. Their final complete physical images
are identical:
`5922bef797e809529d648cf3cb10a276870c3337d11aa164b1df89e312d4c65b`.

Before repair, all six published property bytes are `3`, and GPU readback of
both the phase buffer and the shader's bound phase texture is
`[250, 0, 0, 140]`. The water texture already contains `[14, 101, 130, 255]`.
After repair, published property bytes are `0`; both phase readbacks are
`[0, 255, 0, 140]`, while the water colour data is unchanged. The original
native flow fence remains verified in both runs. Actual Windows screenshots
show the change from a beige, apparently empty room to blue water, alongside
the same mass, temperature and fluid-pressure reading.

`RenderingInputs` records the owned texture bindings and bounded scene pixels.
The matched water controls read the phase buffer, bound phase texture and water
texture. The later controls below also read the bound gas colour, pressure and
danger textures. Each read covers only a 3-by-2 rectangle, runs after the private
scene completes on the publication thread, and restores the previous render
target. These diagnostics are not part of a production performance measurement.

Both game processes close successfully, preserve the source/staged input hashes,
remove their temporary mods and restore all 13 original profile files exactly.
The native libraries are reused only after checking their hashes and unchanged
native build inputs against the preceding completed MSVC/native readiness run.
The first baseline attempt stops at an adapter compile error before packaging
or game launch; its evidence is retained separately.

Private terminal review hashes:

- Baseline: `db86eb8c653cd8387d206a38f7e07a01799ae9777ab63c937d441b416f06900c`.
- Fixed, explicitly scoped review:
  `90f8a36d444be6a53075d2cb0f79f56a7e745391b609a61ed5ae82d1c8d5d9d2`.

## Mixed-gas regression and actual hover binding

The later `windows-native-gas-render-r1` uses the repaired adapter in a fresh
six-cell gas scene. All 91 Core, 41 presentation and 31 inspection groups pass,
as do the adapter/package and actual Windows native source/save controls. At
completed native frame 121, all six GPU gas colours exactly match the shared
presentation samples. The phase textures identify gas, the water texture is
empty, and visual opacity matches every cell's expected density encoding.

The legacy gas-pressure and danger textures store their scalar bytes in alpha.
The diagnostic `ExpectedGas.Pressure` is that visual opacity byte (150 or 151
in this run), not physical pressure. Actual gas pressure remains separately
available in the immutable thermodynamic reading, approximately 100.5 kPa.

Actual mouse hover on cell 102371 shows nitrogen 78.295% and oxygen 21.705%
without selecting the tile. Paused camera zoom preserves the readings. Selected
details retain helium at `7.21491E-26%` and partial pressure `7.2509901E-23 Pa`,
alongside each constituent's mass and moles. An observer invoked by the real
hover drawer records the exact immutable physical object, completed revision
121 and matching presentation/texture publication generation 134. Its selected
and unselected records agree with the GPU report and original screenshots.

The ordinary deferred save and full header/body readback pass. The completed
physical source hash is
`d86f0afc4474eba39fbdf621d3f0fdfddafd48dce24c25ddd59e09f994391924`.
The process exits successfully, its temporary mod is removed and all 13 original
profile files are restored exactly. Private terminal review SHA-256:
`c52db76f869c9c3774b180e9234b61eb76a47112f863091c3ccfc57f29ded37f`.
This verifies two bulk gases; it does not replace the four-gas/contaminant hover
criterion in [mixture presentation](mixture-presentation.md).

## Repaired water through the main menu

`windows-fluid-render-reload-r1` reuses that same instrumented package and the
unchanged original water-gravity input. One Unity process loads it twice,
observes shutdown and the returned main menu, and advances native session 1 to
2. Both paused autosaves and complete header/body readbacks pass. Each session
matches all 120 continued physical frames, retaining the same final physical
hash `5922bef797e809529d648cf3cb10a276870c3337d11aa164b1df89e312d4c65b`.

Both sessions retain phase pixels `[0, 255, 0, 140]`, water pixels
`[14, 101, 130, 255]`, empty gas channels and the native flow fence. Actual
second-session hover and selected details on cell 103107 show blue filled water,
1100.001648 kg, 220.003626 MPa, no gas phase and no breathable atmosphere.
The water row identifies gas partial pressure as not applicable. Both actual
drawer records retain the identical completed physical object at frame 120 and
matching presentation/texture generation 247; the first session's GPU report
has matching generation 123. Source and staged inputs remain byte-identical,
the process closes successfully and all 13 original profile files are restored.
Private terminal review SHA-256:
`973c5127ad7958b7881417ed5c09df30a23674d2239f429c1a9fe30cca380d66`.

These controls accept full-water appearance and the observed gas regression in
the bounded Windows scenes, including the recorded source binding and reload.
Other liquid materials, free surfaces and sustained performance still require
evidence. The missing KPrefabID 246809 warning remains unresolved
for the older loaded water input. The later [Windows actor-batch water control](sealed-native-domain.md#actual-windows-actor-batches)
retains the repaired rendering through both reloads without that warning for
its newer input. Whole-colony and first-playable acceptance remain open.

## Linux physical GPU and actual inspection

The later [calibrated Linux controls](native-gpu-readback.md) pass both loaded
sessions for gas and filled water. Their complete native phase/mass outline
establishes the page-texture coordinates before checking every owned gas
colour, density-opacity and hazard byte. An earlier uncalibrated read addressed
different OpenGL rows and falsely suggested an upload defect. Native upload
and rendering remain unchanged.

The gas colours match their completed samples in all six cells. Water retains
the correct phase pixels, liquid colour and visibly blue fill. Actual hover
and selected details retain the same completed physical object/publication as
the textures. Both 120-frame images remain identical to the preceding physics
controls, and all four paused autosaves and private-display cleanups pass.
The linked record retains the original screenshots, raw hashes and scoped
image review. This closes the bounded Linux appearance/inspection gap while
retaining the wider production and performance limits above.
