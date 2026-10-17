# Native nitrogen identities

`elements/mixed_atmospheres_nitrogen.yaml` registers the following original
identities through ONI's mod virtual filesystem. No game installation file or
licensed rendering asset is distributed.

| Stable identity | Player name | SDBM hash | Reference template |
| --- | --- | ---: | --- |
| `MixedAtmospheresNitrogen` | Nitrogen | -1109340048 | Oxygen substance |
| `MixedAtmospheresLiquidNitrogen` | Liquid nitrogen | 1078696428 | Liquid oxygen substance |
| `MixedAtmospheresSolidNitrogen` | Solid nitrogen | 418600943 | Solid oxygen substance |

Namespaced identities avoid assuming compatibility with another mod's nitrogen
definition. A conflicting hash or substance fails explicitly. Element indices
are assigned by the game's final sort and are never hardcoded. Save and owned
import identities use the stable strings, not a custom enum's numeric text.

The three substances clone the installed phase template at runtime and retain
its native material/animation references. Each has independent colour,
gradient, UI/conduit colour and mutable material-property block. A witness
checks that the original template's colour and gradient remain unchanged.
Null native material/animation references are preserved when the original
substance uses the renderer's normal fallback. Gas uses RGB (120,145,150),
liquid (89,130,161), and solid (163,199,219). The mixture presentation obtains
nitrogen's gas colour from this registered substance.

## Loading only with the adapter

ONI loads a mod's `elements/` folder as layerable files independently of its
DLL. If the adapter's `OnLoad` fails, it removes every Harmony patch it owns,
so the substance clones, `STRINGS` names and tag bridge would all be absent:
ONI would manifest an empty `Substance` (no material or animation; the ground
renderer reports an error for the solid phase on every colony load) with
integer tag text. The YAML therefore declares `dlcId:
MixedAtmospheres.AdapterLoaded` instead of the base-game `""`. ONI's
`ElementLoader.Load` skips an entry whose content key has no substance table,
and only the adapter's `NitrogenContentGate` supplies that key (the last
prefix, mapped to the base-game table) and removes it again (the first
postfix), restoring each element's `dlcId` to `""` before validation or any
other consumer. A failed or removed adapter therefore registers no nitrogen at
all rather than half-registered phases. A save that already contains these
elements still requires the working adapter.

The shipped `SimHashes` enum cannot acquire new members. Scoped IL changes to
`GameTagExtensions.Create` and `CreateTag` resolve these three hashes to
their actual element names; native and unknown hashes retain their exact
`Enum.ToString()` names. The original `TagManager.Create` call and struct
return instructions stay intact. A changed instruction shape rejects, and
Before installing Harmony patches, the adapter invokes both original helpers
through reflection with a native oxygen identity. Startup checks then invoke
both actual patched helpers directly for native, custom and
unknown identities before the rest of the adapter is installed. This
avoids integer prefab names without patching `System.Enum` globally.

The first modded WindowsPlayer/Proton run exposed a startup exception in the
earlier ref-result prefix while ordinary native tags were being created. The
IL-only attempt still fails the direct runtime check. Both failed runs remain
retained. Pre-invoking the original helpers before composition passes both
original and patched checks, normal database initialization, and main-menu
contract export in one real WindowsPlayer cohort, but a later instrumented
build still fails. Prewarming alone is not a reliable repair. The precise
Unity/Mono implementation cause is not established; an alternative Harmony
method-generation path is under test. Standalone licensed-Mono experiments under Wine and Proton
pass both warmed and cold forms and do not reproduce Unity's failure.
`NativeTagHelpersVerified` is written only after the direct patched checks
complete, and probe completion requires that result. The subsequent physical
scene and reload gates remain separate.

The merged adapter now passes native Windows ONI startup, gas and water actor
source/save scenes, and both loaded water sessions described in the
[Windows actor-batch controls](sealed-native-domain.md#actual-windows-actor-batches).
This executes the startup check against both actual patched helpers before
the integration probe and colony initialization. The Windows .NET 10 test build
initially rejects the unguarded reflection cast with CS8600/CS8603. The adapter
now explicitly requires the instruction label list, with a useful rejection if
Harmony's field contract is unavailable. All eight nitrogen groups then pass,
alongside 106 snapshot/actor-boundary and 31 inspection groups. The failed
compile attempt remains retained; it never packages or launches the game.

The later clean `c346d93` Windows package also includes original-helper
preparation and the explicit `NativeTagHelpersVerified` report field. All nine
nitrogen groups pass, including the new pre-composition invocation check. The
actual Windows water-emitter scene passes both original and patched helper
checks and records the verification flag before completing its physical/save
control; see [the bounded result](sealed-native-domain.md#initially-disabled-emitters).

## Property scope

These are nominal native pure-phase definitions, version
`ma-native-nitrogen-reference-v1`, aligned with the existing reference species
catalogue. They do not supply the production material catalogue or mixture
phase solver.

| Property | Gas | Liquid | Solid |
| --- | ---: | ---: | ---: |
| Molar mass (g/mol) | 28.0134 | 28.0134 | 28.0134 |
| Constant native heat capacity (kJ/kg/K) | 1.040 | 2.040 | 1.500 |
| Conductivity (W/m/K) | 0.0258 | 0.14 | 0.2 |
| Native nominal full-cell mass (kg) | game's gas default | 808 | 1026 |
| Default temperature (K) | 293.15 | 70 | 50 |

The nominal melting and boiling links use 63.15 K and 77.355 K. These are fixed
reference temperatures, not a pressure-dependent phase boundary. For context,
the [NIST Chemistry WebBook nitrogen entry](https://webbook.nist.gov/cgi/cbook.cgi?ID=C7727379&Mask=4)
reports molecular weight 28.0134, a 77.34 K boiling measurement, and a 63.14 K
triple point. That source does not validate this constant-property model over
arbitrary world temperatures or pressures. The remaining constants are
explicit reference approximations, not fitted or certified physical data.

The YAML `defaultPressure` is an ONI spawn/rendering field. It is not an input
to the custom physical pressure calculation. Native transitions do not
account for the overhaul's latent heat; registration must not be treated as
proof of conservative condensation, freezing, or mixture equilibrium. Gas is
unbreathable by itself and is not tagged as oxygen or an oxidizer. No nitrogen
is added to ordinary generated worlds by this registration step.

## Validation

`tools/NitrogenRegistrationTests` checks stable noncolliding hashes, exhaustive
native enum pass-through, the custom tag bridge, localization casing, explicit
YAML description mapping through the actual native `ElementEntry` getter under
Turkish culture, and the installed helper signatures. Both actual helper IL
bodies are rewritten and executed against every native enum value, unknown
values and all three custom identities. Changed enum/branch/duplicate bodies
reject. A ninth group invokes both original helpers before composition and
ensures this alone cannot certify patched helpers. Its optional second argument replays a private
actual-game `nitrogen-validation.json` report.

The disposable presentation runner adds three real native material items after
the six mixture inspection fixtures. It checks phase links, template isolation,
hash/tag/prefab lookup, native spawning, exact item mass and temperature, native
description preservation, and the labelled single-material fluid fallback.
Three Unity screenshots retain the selected gas, liquid and solid objects.
An interactive run retains those native items for a separate real
Save/Quit/Resume check; ordinary automatic runs dispose them before exit.

Windows run `local/colony-runs/presentation-windows-v15` passed all six mixture
inspection surfaces and all three native nitrogen items, retaining 21 automatic
Unity screenshots. The same ONI process then used native Save and Quit, returned
to the main menu, resumed its unique disposable save, and selected all three
native items. The UI retained their names, 1000 g masses, consistent formatted
temperatures and descriptions. Four further Unity screenshots record those
panels and the restored native oxygen overlay. This was an inspection check,
not an exact post-load binary comparison of every scalar or a mixture save test.
All five observed teardown boundaries cleared the mixture provider, model,
cached frame and inspection bindings. The runner restored settings byte for
byte and archived only its own disposable save. Six standalone checks and the
optional Windows actual-game replay passed on both Windows and Linux. The
independent review is in that run's `presentation-review.json`; Linux results
are in `local/linux-presentation/nitrogen-v15/result.json`. The unrelated old
ambient `Player-final.log` retained by that runner version is explicitly
excluded; the invocation's explicit `Player.log` records the matching save/load.

The retained actual Linux `20261016T001005Z-a5b1a6` presentation run also passes
all three native items in a nine-world Spaced Out! colony. Its three original
Unity images have now been individually reviewed: the selected gas, liquid and
solid retain their names, 1000 g masses, descriptions and formatted temperatures
of 20 C, -203.2 C and -223.2 C. The machine report verifies native spawning,
template isolation, target selection and unchanged descriptions. The fluid
items retain the explicit single-material reading; none is presented as an
owned mixture.

This is legacy evidence from `933f0d2` with the recorded pacing changes
(`source_dirty: true`), on the same installed Linux game binary hashes; it is
not a new current-adapter regression run. Private image review SHA-256:
`29a362b717a66c3f4f6753bd2d0cc0082d990374e9af06c02232544a3ac9b263`.
The items were disposed by the automatic fixture, so this does not prove a
Linux native-item save/quit/resume. Conservative phase-transition energy,
starter-world nitrogen resources, mixture storage ownership and progression
also remain separate acceptance requirements.
