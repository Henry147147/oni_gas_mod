# Native fluid guards and world rendering

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
Its three GPU reads cover only a 3-by-2 rectangle, run after the private scene
completes on the publication thread, and restore the previous render target.
These diagnostics are not part of a production performance measurement.

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

This accepts the observed full-water appearance in the bounded Windows scene.
Linux rendering, gas-scene regression after this repair, other liquid materials,
free surfaces, exact visual hover-cell/frame binding and sustained performance
still require evidence. The missing KPrefabID 246809 warning remains unresolved.
Whole-colony and first-playable acceptance remain open.
