# Filled-liquid gravity in the native backend

Native domain ABI 2 adds scenario `3`: a completely filled compressed-water
room with downward gravity of 9.81 m/s2. The existing gas, water and gas-gravity
scenarios keep their original values and energy representation. The new
scenario uses the same worker barrier, conserved mass/canonical momentum,
elastic EOS, heat operators and native fluid exclusions.

The private Unity control sends real water injection and consumption messages
through the custom owner, publishes their once-only game callbacks, and checks
body impulse, cell-centre potential and the carried boundary potential. The
ordinary paired save and fresh-process continuation controls also apply.

```sh
python3 tools/oni_headless.py run --package /path/to/probe \
  --mode native-liquid-gravity-domain --frames 120 --save-colony \
  --timeout 1800 --output artifacts/liquid-gravity-001
```

Windows uses `tools/run_colony_validation.ps1 -NativeDomain water-gravity`.
The isolated licensed-native controls are `sealed-water-gravity` and
`sealed-restore-water-gravity`; they do not launch Unity.

## Internal energy coordinates

Small mechanical/thermal updates must not repeatedly round against water's
large material reference energy. In this scenario the owner stores water's
energy anomaly about 300 K, including its elastic compression energy. The
original physical material reference remains an explicit function of the
authoritative water mass:

`physical U = stored U + mass * (original e0 - owner e0)`.

This is an affine change of energy coordinates. The owner registry/EOS use the
same shifted caloric reference. Mass transport carries that coordinate, and
the physical reference contribution follows the same constituent mass. Heat
and pressure work update stored U without adding or removing reference energy.
Every published cell U, boundary receipt and domain energy account includes
the original reference again, formed with additive energy expansions. Managed
material definitions retain their original caloric reference. Neither the
water's physical formation energy nor carried gravitational potential is
discarded.

The checkpoint contains the shifted registry and the distinct complete EOS
identity `oni-sealed-domain-liquid-gravity-v1`. Restore must match scenario 3,
its phase, gravity and complete identity. Stateless continuation recognizes
only the two explicitly supported native profiles; unknown identities reject.
Existing physical-format-2 checkpoints for scenarios 0..2 retain their model.

## Validation and current scope

The retained original full-U experiment loses 0.128481 J over 1,200 frames,
exceeding its 0.00594586 J endpoint bound. A separately retained transport
rounding experiment also fails and is not enabled. With separated reference
energy, the same native-domain control reaches 1,200 frames / 240 s with a
0.000250265 J U+K+G residual under that unchanged bound, and a maximum native
residual of 0.000386573 J. It also checks the pressure head, body force,
injection/extraction at different elevations and exact checkpoint continuation.
Initial physical U, pressure and temperature are compared with the original
unshifted water model. Gas retains its existing fixed energy limits.

The licensed Linux and Windows-under-Wine native save/restore controls pass,
including their exact twelve-frame physical replays. Their independent U+K+G
residuals are +0.0000610352 J and -0.0001220703 J against the unchanged
0.00594585 J bound. Both-platform adapter builds and 101 declaration/snapshot
checks per platform pass, along with all 13 Linux native tests and 166 Python
checks with four platform-specific skips. The Windows-under-Wine 1,200-frame
test also passes with -0.000190683 J residual. Its native suite passes all 13
cases across a separate rerun of the final checkpoint case after a Wine server
connection reset; the original infrastructure failure remains retained.
All 13 ASan/UBSan native tests also pass. The frozen `b4a6552` Ubuntu 22.04
compatibility build v9 passes all 13 tests, including the 1,200-frame control;
both shared libraries require at most glibc 2.34.

The frozen `b4a6552` runner/package and compatibility build v9 now pass the
actual Linux Unity water-gravity source scene and ordinary colony save in
`liquid-gravity-unity-matrix-v1/water-gravity-save`. It reaches frame 121 /
24.200000361 s, publishes both real water callbacks once, and passes gravity,
native-cell, grid/rendering and save readback checks. Its independent U+K+G
residual is +0.0000661246 J against 0.005945860769 J; maximum native energy
residual is 0.000122070313 J. Source physical SHA-256 is
`b7d6bda27871f5e040f19879e86b6415e56c4b7cb423b29520fae6d74a571bad`,
and colony-file SHA-256 is
`aa66e98078bbb825c23958e407176696c18da47766f91156b791ea814376c5ee`.
The fresh-process load passes its frame-zero deferred autosave and all 120
exact continued physical images through 48.200000718 s. Its final physical
SHA-256 is
`1553d8fadc0f65ce4e6759781f4e7e45a54c872a592c6c9745854b2bbf679285`;
the initial autosave retains the exact source image and has file SHA-256
`2dc8e3c7c8148ec19c41ddbe78cf6ae42608e90c125dfc9c31cd4d39fd11b5ee`.

`liquid-gravity-in-process-reload-v1` also passes: one Unity process observes
shutdown/main menu, advances native session 1 to 2, performs both paused initial
autosaves and reproduces that same frame-120 physical image in both sessions.
The staged input remains unchanged; the second autosave file hash is
`a0e3223883502b1a461582109407f33cda03a4e62f634c83933f17b7b4cf6621`.
The supervising workflow then resumes the larger frozen feature matrix.
Windows Unity verification remains open.

This is a six-cell occupied liquid control;
it does not implement free surfaces, phase change, arbitrary liquid materials,
production devices, persistent live actors or whole-colony ownership.
