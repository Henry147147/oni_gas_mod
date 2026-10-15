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

The shipped `SimHashes` enum cannot acquire new members. Scoped patches to
`GameTagExtensions.Create` and `CreateTag` resolve only these three hashes to
their actual element tags; all native and unknown hashes pass through. This
avoids integer prefab names without patching `System.Enum` globally.

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
Turkish culture, and the installed helper signatures. Its optional second argument replays a private
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

Actual Linux Unity rendering, native phase-transition energy, starter-world
nitrogen resources, mixture storage ownership and progression remain separate
acceptance requirements.
