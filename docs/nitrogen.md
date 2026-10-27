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

## Starting air and progression

Design of 2026-10-27 for the first release (base game and Spaced Out!).

**User decisions, 2026-10-27:**

1. Nitrogen air is a new-colony option, off by default; the built-in *Hard
   mode* preset turns it on.
2. Generated nitrogen air holds 25% oxygen by moles, with the same mass and
   temperature as the generated oxygen it replaces.
3. Vanilla-air starts (the easy-mode default) also get the oxylite oxygen
   reserve when the ten-cycle bridge shows an oxygen shortfall, by the same
   mechanism as nitrogen-air starts. A carbon dioxide headroom shortfall stays
   a start notice only. The notice says what was added, and the reserve is
   deterministic for a given world and starting duplicants.

### Facts it rests on (build 744825)

- A duplicant uses 0.1 kg/s of oxygen and exhales 2% of that mass as carbon
  dioxide (`DUPLICANTSTATS`); the mod keeps these rates. Three duplicants
  need 1,800 kg of oxygen and exhale 36 kg (818 mol) of carbon dioxide in
  ten cycles.
- Generated starting air is pure oxygen, about 1.5–1.7 kg per cell around the
  printing pod (Spaced Out! teleport-world starts about 0.8 kg). The game's
  oxygen has molar mass 16 g/mol, so that is about 260 kPa in the owner.
- ONI's limits are cell masses, and they read the owner's gas mass: the
  electrolyzer and oxygen diffuser stop at 1.8 kg, gas vents at 2 kg, and
  oxylite debris sublimates (0.01·m^0.7 kg/s) only while its cell holds less
  than 1.8 kg.
- On generated oxygen, carbon dioxide runs out of headroom before oxygen.
  Above 100 kPa the 500 Pa limit binds before the 0.5% share, and it depends
  only on volume: 0.204 mol per cell at 295 K. Ten cycles of three
  duplicants exhale 818 mol, so they need about 4,000 connected cells of gas.
  In 25% nitrogen air oxygen binds first (about 0.13 kg breathable per
  1.67 kg cell).
  Rocket interiors start in vacuum.

### Design (implemented)

1. **Nitrogen air** (new-colony setting, off by default). When the owner
   imports a new colony, each cell's generated oxygen and polluted oxygen
   becomes air of the same mass and temperature with 25% oxygen by moles and
   nitrogen for the rest. Polluted
   oxygen stays polluted and keeps its germs. This applies on every owned
   asteroid, the whole cluster in Spaced Out!. ONI's world generation (seed,
   layout, templates, solids, liquids, other gases, geysers, POIs) is
   unchanged; only the imported gas inventory differs, and that is the
   colony's initial ledger inventory, with no new checkpoint field. Keeping the
   mass keeps ONI's mass limits meaningful. Pressure falls to about 64% (about
   164 kPa near the pod, oxygen partial pressure about 41 kPa); air below about
   0.61 kg per cell is under 15 kPa of oxygen. The import lays air out as one
   mixed layer, not as separate nitrogen and oxygen layers.
2. **Finite nitrogen reserves**: the converted air itself, in every biome
   that generates oxygen or polluted oxygen. Nothing produces nitrogen (no
   geyser or machine). Liquid and solid nitrogen cannot persist at generated
   temperatures (it boils at 77 K). Players move nitrogen with pumps, gas
   filters and reservoirs.
3. **Ten-cycle bridge.** At a new colony's start the mod counts the printed
   duplicants and reads each one's oxygen use (the game attribute, so traits
   count). Over the gas connected to the printing pod it computes the oxygen
   that can be breathed under the colony's rules and the carbon dioxide
   headroom, in cycles. Every start with an oxygen shortfall gets oxylite
   debris beside the pod for the ten-cycle shortfall. Oxylite is a vanilla
   item that sublimates only below 1.8 kg, so 25% air stays inside hard mode's
   35% at that limit wherever it holds at least 1.64 kg per cell; thinner air
   beside the reserve can pass 35%. No vanilla carbon dioxide sink works
   unattended. When either margin is under ten cycles, the start notice says so
   and names the remedies (open the cave, algae terrarium, carbon skimmer).
4. **Hard mode** keeps the existing maximum-oxygen setting (35%). A new
   built-in preset, *Hard mode*, sets it with nitrogen air. The window warns
   that hard mode without nitrogen air fails everywhere, because vanilla air
   is 100% oxygen. The colony's breathing record (MACOLP03 section 3) gains
   version 2, which adds the nitrogen-air flag. Version 1 is still written when
   nitrogen air is off, so existing saves keep their bytes.
5. **Gas share sensor** (new building): it picks a gas and compares the gas's
   mole share in its cell with a threshold, like the Atmo Sensor does with
   pressure. Players can then switch oxygen production or nitrogen vents to
   hold a room inside hard mode's window. It reuses the gas element sensor's
   animation, needs the same research as that sensor, and has its own
   description and strings.
6. **Ventilation, scrubbing and regulation** already exist early: the
   Ventilation research (gas pump, vent, pipe), Pressure Management (valve,
   airflow tile), Air Systems (carbon skimmer, deodorizer), the algae
   terrarium, the oxygen diffuser (no research) and Improved Oxygen
   (electrolyzer). Vents stop at 2 kg. Only the sensor is added.
7. **Nitrogen thermodynamics**: a catalogue row for liquid nitrogen, with
   latent heat 199 kJ/kg (NIST), bulk modulus 0.59 GPa (ρc² at 77 K),
   viscosity 1.6e-4 Pa·s and critical temperature 126.2 K. Gas and liquid then
   convert through the owner's saturation pair with latent heat, as water does.
   Without the row, the decimal element id fell back to organic-liquid
   surrogates. Solid nitrogen uses ONI's freeze/melt path, as other solids do.
8. **Breathing explanations**: the existing hover and status warnings,
   checked for hard mode's "too much oxygen".
9. **Checks**: a native import control; settings and record tests; in game,
   the start air and bridge on every base-game asteroid and a Spaced Out!
   cluster, an unattended ten-cycle start per mode on Terra, and a nitrogen
   filter/storage scenario.

### Implementation

- **Catalogue** (`native/src/oni_catalogue.cpp`): the liquid nitrogen row
  above, matched by id or by the decimal hash the running game uses.
  `ma_oni_catalogue_tests` checks the pair, latent heat and saturation at
  77.355 K and one atmosphere.
- **Import** (`oni_colony_import.cpp`, `ma_oni_colony_set_starting_air`): the
  same-mass conversion and the mixed air layer. `ma_oni_colony_import_tests`
  checks mass kept, 25% oxygen in every air cell, one mixed layer between a
  carbon dioxide floor and hydrogen above, and the unchanged default. With
  the option off nothing changes, byte for byte, and no checkpoint field is
  added.
- **Settings** (`ModSettings*.cs`): `breathing.nitrogenAir`, the *Hard mode*
  preset, the hard-mode warning and section 3 record version 2
  ([settings](settings.md)). `tools/SettingsTests` covers both record
  versions.
- **Bridge** (`ColonyStartingAir.cs`): counted before the import over the
  open cells connected to the printing pod, settled once the starting
  duplicants exist. It uses their `AirConsumptionRate`, the game's
  2% CO2 ratio and the colony's breathing rules, taking the gas as well mixed.
  Oxylite goes in chunks of at most 300 kg on floor cells beside the pod
  (left up to 12 cells, right up to 6), in a fixed order. The result goes to
  the log, the `StartingAir` report and, when oxylite was added or the carbon
  dioxide headroom is under ten cycles, a start notice. Validation scenes
  place oxylite only with `MA_COLONY_STARTING_RESERVE=1`.
- **Gas Share Sensor** (`GasShareSensor.cs`): an element filter and a 0–100%
  threshold; on the owned asteroid it reads the cell's mixture through
  `ColonySensors.GasShare`.
- **In game**: the `starting-air` scenario checks the settled bridge, the
  oxylite beside the pod (with the variable), the pod's air (25% oxygen with
  nitrogen air) and two gas share sensors switching as nitrogen dilutes an
  arena.

## Open items

- A Linux native-item rerun with the current adapter, including save, quit and
  resume.
- Arbitrary future Unity versions, and the cause of the helper-patch fault.
- The [milestone F work above](#starting-air-and-progression).
