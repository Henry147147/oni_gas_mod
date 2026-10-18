# Native nitrogen identities

`elements/mixed_atmospheres_nitrogen.yaml` registers three original element
identities through ONI's mod virtual filesystem. They give nitrogen a native
identity for display, storage and queries; the custom species registry carries
its physical properties. No game installation file or licensed rendering asset
is distributed. The shipped build 744825 has no nitrogen in
`StreamingAssets/elements/{gas,liquid,solid,special}.yaml`.

| Stable identity | Player name | SDBM hash | Reference template |
| --- | --- | ---: | --- |
| `MixedAtmospheresNitrogen` | Nitrogen | -1109340048 | Oxygen substance |
| `MixedAtmospheresLiquidNitrogen` | Liquid nitrogen | 1078696428 | Liquid oxygen substance |
| `MixedAtmospheresSolidNitrogen` | Solid nitrogen | 418600943 | Solid oxygen substance |

- Namespaced identities avoid assuming compatibility with another mod's
  nitrogen. A conflicting hash or substance fails explicitly.
- Element indices come from the game's final sort and are never hardcoded.
  Saves and owned imports use the stable strings, not a custom enum's numeric
  text.
- YAML supplies explicit description keys. The installed default getter uses
  current-culture `ToUpper()`, which produces missing keys for these IDs under
  Turkish casing.

## Element definitions

These are nominal native pure-phase definitions, version
`ma-native-nitrogen-reference-v1`, aligned with the reference species
catalogue. They are not the production material catalogue or mixture phase
solver.

| Property | Gas | Liquid | Solid |
| --- | ---: | ---: | ---: |
| Molar mass (g/mol) | 28.0134 | 28.0134 | 28.0134 |
| Constant native heat capacity (kJ/kg/K) | 1.040 | 2.040 | 1.500 |
| Conductivity (W/m/K) | 0.0258 | 0.14 | 0.2 |
| Native nominal full-cell mass (kg) | game's gas default | 808 | 1026 |
| Default temperature (K) | 293.15 | 70 | 50 |
| Colour (RGB) | (120,145,150) | (89,130,161) | (163,199,219) |

- The nominal melting and boiling links are fixed at 63.15 K and 77.355 K, not
  a pressure-dependent phase boundary. For context, the
  [NIST Chemistry WebBook](https://webbook.nist.gov/cgi/cbook.cgi?ID=C7727379&Mask=4)
  gives molecular weight 28.0134, a 77.34 K boiling measurement and a 63.14 K
  triple point. The other constants are explicit reference approximations, not
  fitted or certified data.
- The YAML `defaultPressure` is an ONI spawn/rendering field and is not an
  input to the custom pressure calculation.
- Native transitions do not account for the overhaul's latent heat.
  Registration is not evidence of conservative condensation, freezing or
  mixture equilibrium.
- The gas is unbreathable by itself and is not tagged as oxygen or an oxidizer.
  Registration adds no nitrogen to generated worlds.

Each substance clones the installed phase template at runtime and keeps its
native material and animation references, with its own colour, gradient,
UI/conduit colour and mutable material-property block. A witness checks that
the template's colour and gradient are unchanged. A valid substance may have
null material/animation references and rely on the renderer's normal fallback;
those nulls are preserved. The mixture presentation takes nitrogen's gas
colour from this substance.

## Game loader contract

These are reverse-engineered facts about the shipped loader, not an official
element authoring API.

- ONI treats an `elements` directory at the mod content root as layerable
  files. `KMod.Directory` supplies an alias mapped onto
  `Application.streamingAssetsPath`, and `KMod.Mod.Load` adds that source to
  `Klei.FileSystem`. `ElementLoader.CollectElementsFromYAML` enumerates
  `elements/*.yaml` through it, so a mod file at
  `<mod>/elements/<unique-file>.yaml` loads without copying anything into the
  game installation. The current KYaml/VYaml reader uses the supplied file
  handle's bytes.
- `ElementLoader.Load` hashes each `elementId` with SDBMLower, registers tag,
  name and description, copies the YAML properties and builds a `Substance`
  from the DLC-specific substance table. The base-game table key is the empty
  string `DlcManager.VANILLA_ID`.
- `FinaliseElementsTable` sorts elements by phase and hash and then assigns
  indices; resolve indices after that stage.
- The native `Sim.Element` constructor reads `Gradient` before its later null
  check, so a missing substance fails at startup.

A maintained community mod's
[registration patches](https://github.com/Sgt-Imalas/Sgt_Imalas-Oni-Mods/blob/master/RonivansLegacy_ChemicalProcessing/Patches/ElementLoader_Patches.cs)
register substances before `ElementLoader.Load` and adjust finalized elements
afterwards, and its
[element definitions](https://github.com/Sgt-Imalas/Sgt_Imalas-Oni-Mods/blob/master/RonivansLegacy_ChemicalProcessing/Content/ModDb/ModElements.cs)
add three nitrogen phases. That is implementation precedent only; do not copy
its code or use its values as physical authority. The
[VYaml mod-loading report](https://kleiforums.com/klei-bug-tracker/oni_beta/elementloader-yaml-loader-change-breaks-modded-element-loading-r52663/)
concerns an earlier beta and does not show a failure in the inspected loader.

## Loading only with the adapter

ONI loads a mod's `elements/` folder independently of its DLL. If the
adapter's `OnLoad` fails, it removes every Harmony patch it owns, so the
substance clones, `STRINGS` names and tag bridge would be missing and ONI would
build an empty `Substance` with integer tag text.

The YAML therefore declares `dlcId: MixedAtmospheres.AdapterLoaded` instead of
the base-game `""`. `ElementLoader.Load` skips an entry whose content key has no
substance table. Only the adapter's `NitrogenContentGate` supplies that key
(the last prefix, mapped to the base-game table) and removes it again (the
first postfix), restoring each element's `dlcId` to `""` before validation or
any other consumer. A failed or removed adapter registers no nitrogen at all
rather than half-registered phases. A save that already contains these
elements still needs the working adapter.

## Tag names

The shipped `SimHashes` enum cannot gain members. The original
`GameTagExtensions.Create` and `CreateTag` both turn an unknown enum value into
an integer string and pass it to `TagManager.Create`.

- The adapter leaves those Tag-returning helpers untouched. A prefix on the void
  `Tag.Name` setter maps only the three exact decimal strings in the identity
  table to their canonical names, before native string interning and SDBM
  hashing. This also covers native tag deserialization through that setter.
- `Tag(string)` construction stays native; code that builds a custom tag
  directly must use the canonical string.
- The three decimal names are reserved aliases while the adapter is loaded.
  Before patching, the adapter checks both native proper-name dictionaries and
  rejects an existing alias or hash collision without overwriting it. There is
  no `System.Enum` patch and no change to the shipped enum.
- The failed-load path removes this setter patch and clears runtime
  verification.
- At startup the adapter checks both original helpers by reflection, then calls
  the untouched helpers, tag manager, name setter and original constructor for
  native, custom and unknown identities, comparing names and hashes (Tag
  equality compares hashes only). `NativeTagHelpersVerified` is set only after
  every check and is required by the integration probe.

Patching the helpers themselves is unsafe. Ref-result and transpiler patches on
the two Tag-returning helpers fail intermittently in WindowsPlayer/Mono under
Proton, and prewarming does not fix it. A constructor-plus-setter variant
crashes at the patched `Tag(string)` call. No precise Unity/Mono cause is
known.

## Validation

`tools/NitrogenRegistrationTests` runs from `tools/build.py --game-dir`. Its ten
groups cover stable noncolliding hashes, native enum pass-through, exact custom
aliases, localization, YAML descriptions under Turkish culture, unchanged
helper IL and the single void patch target, the content gate, malformed
strings, collisions in either native name dictionary and the original-helper
preflight. Alias checks use the installed native tag implementations; only real
Unity runs certify Harmony dispatch. An optional second argument replays a
private game `nitrogen-validation.json` report.

The disposable presentation runner adds three native nitrogen items after its
mixture inspection fixtures. It checks phase links, template isolation,
hash/tag/prefab lookup, native spawning, exact item mass and temperature,
preserved native descriptions and the labelled single-material fluid fallback,
with a Unity screenshot of each selected item.

Current status:

- **WindowsPlayer under Proton.** The setter-only adapter passes all twelve
  cold source/load controls in the
  [native feature matrix](native-validation-matrix.md#windowsplayer-feature-matrix)
  and both repeated occupied-mixture load sessions, for the pinned player and
  build.
- **Actual Windows.** All three items pass, and after native Save and Quit and
  same-process resume the UI keeps their names, 1000 g masses, temperatures and
  descriptions. This was an inspection check, not an exact post-load comparison
  of every scalar or a mixture save test.
- **Linux.** All three items pass in a nine-world Spaced Out! colony. Reviewed
  screenshots show names, 1000 g masses, descriptions and temperatures of
  20 C, -203.2 C and -223.2 C. This used an older adapter revision, and the
  items were disposed before exit.

## Open items

- A Linux native-item rerun with the current adapter, including save, quit and
  resume.
- Arbitrary future Unity versions, and the cause of the helper-patch fault.
- Conservative phase-transition energy, starter-world nitrogen resources,
  mixture storage ownership and progression.
