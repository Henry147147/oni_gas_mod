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
Linux sanitizer and rendered Unity verification of this new scenario remain
separate obligations. This is a six-cell occupied liquid control; it does not
implement free surfaces, phase change, arbitrary liquid materials, production
devices, persistent live actors or whole-colony ownership.

### Actual Windows native and Unity controls

The clean `018c0df` merge is frozen in `windows-native-domain-liquid-gravity-r1`
and rebuilt with native MSVC. All 13 native CTests, 91 Core groups, 41
presentation groups and coupled/passive/mutation controls pass. The actual
Windows 1,200-frame native test reports U+K+G residual
`-0.0002220792849216835 J`, below the unchanged `0.0059458607686322575 J` bound,
and a 6223.113030016 Pa pressure head. Exact checkpoint continuation also passes.

Actual Windows ONI 744825 / Unity 6000.3.5f2 then completes 120 frames /
2,520 substeps / 24.000000358 s, including both real water mass callbacks.
The 12 rejected adaptive attempts remain reported. Relative mass/energy drift
is 1.516e-15 / 1.167e-15. The transfer energy residual is -5.071591662075825e-6 J
against the unchanged 0.005945860768673391 J bound; maximum gravity residual is
0.0001220703125 J. Body impulse, potential and boundary-potential checks pass.
The ordinary paired save retains physical SHA-256
`9166cd6b0c966dc94c64f082be3c6acc42b8ffc848c16eaae5c55eed2dc4f451`
and file SHA-256
`3f39913d86243addb62ed8c85885234409da9bcfbaab87e7f3b50b3b7f510289`.
Its complete 10,643,848-byte body and header match disk readback. Private source
terminal review SHA-256:
`9ce05bc67ea16e4763a2325b97f835736e02dab31f06a14f2197ff02db857f19`.

`windows-native-liquid-gravity-reload-r1` loads that input in a fresh Windows
Unity process, returns through the main menu and loads it again in the same
process. Native sessions advance from 1 to 2. Each passes its positive paused
tick challenge, frame-zero deferred autosave/header/body readback and all 120
exact continuation frames. Both reach 48.000000715 s with physical SHA-256
`5922bef797e809529d648cf3cb10a276870c3337d11aa164b1df89e312d4c65b`.
The original GUID, three duplicants and source/staged input hashes are retained.
Private reload terminal review SHA-256:
`ce11b2e4b061ed2819588995d07632ad20ae772e7ff94cb171026c40a55c383e`.

Actual mouse hover updates mass, temperature and supplied fluid pressure as
water moves, and second-session selected details retain water mass/moles,
100% liquid volume, no gas phase and explicit breathing status. Original Windows
screenshots are retained. Both processes close successfully, remove temporary
mods and restore all 13 profile files byte-for-byte. The missing KPrefabID
246809 warning persists during resaving. Liquid fill rendering, exact visual
cell/frame binding, general managed-reference integrity, scientific hydrostatic
convergence and first-playable acceptance remain open.

The later [matched fluid-rendering repair](native-fluid-rendering.md) identifies
the hidden-water cause and verifies blue full-water appearance on Windows,
with the same complete physical continuation image. Its scope does not accept
Linux liquid rendering or general free-surface geometry.
