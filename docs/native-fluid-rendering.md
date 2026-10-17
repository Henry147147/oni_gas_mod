# Native fluid guards and world rendering

New native-domain controls also require [calibrated GPU byte checks](native-gpu-readback.md)
for gas colour, density-opacity and hazard channels. The native phase/mass
outline establishes page-texture coordinates before interpreting a readback;
CPU publication counters alone cannot pass this check.

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
Linux rendering, other liquid materials, free surfaces and sustained performance
still require evidence. The missing KPrefabID 246809 warning remains unresolved
for the older loaded water input. The later [Windows actor-batch water control](sealed-native-domain.md#actual-windows-actor-batches)
retains the repaired rendering through both reloads without that warning for
its newer input. Whole-colony and first-playable acceptance remain open.
