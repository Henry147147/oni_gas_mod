# Material catalogue

The colony owner builds one `MacClosedWorld` species per base-game gas and
liquid (`native/src/oni_catalogue.cpp`). The catalogue joins two sources:

- **Game values, read at runtime** from the installed game's element table and
  never committed: molar mass, specific heat, thermal conductivity, the liquid's
  full-cell mass, transition temperatures, targets and byproducts.
- **Reference values, committed** in the catalogue's table with their sources,
  or a documented surrogate: latent heat of vaporization, bulk modulus,
  viscosity and miscibility group.

## Interpreting game values

| Property | Use |
| --- | --- |
| Molar mass | The ideal-gas molar mass, exactly as the game defines it. The game gives oxygen and hydrogen their atomic masses (about 16 and 1 g/mol), so a kilogram of either exerts about twice its physical pressure. |
| Specific heat | The constant-volume capacity `cv` (a game cell has fixed volume), so a cell's heat content matches the game; `cp = cv + R/M`. |
| Liquid full-cell mass | The zero-pressure density in kg/m³: a cell holds that mass at zero pressure. |
| Transitions | Boiling and condensation temperatures anchor saturation pairs; other transitions become audited conversions. |

Temperatures are admitted from 1 K to 100,000 K. Liquids may compress to twice
their zero-pressure density. Formation enthalpies are zero except for the
vapour of a saturation pair.

## Saturation pairs

A liquid whose boiling target is a gas that condenses back to it (water and
steam) is a saturation pair. Both share an identity label and the vapour's
molar mass. The vapour's formation enthalpy makes the EOS latent heat equal the
catalogue latent heat at the game's boiling temperature and one atmosphere,
and its entropy offset puts the pair at saturation there. Boiling then follows
pressure ([phase change](../native/MAC_PHASE_CHANGE.md)): the pair boils at the
game's temperature at 101.325 kPa, colder at lower pressure and hotter under
pressure. One-way transitions (polluted water boiling to steam, polluted oxygen
condensing to liquid oxygen, crude oil cracking to petroleum) are conversions,
not pairs.

Each pair also has a critical temperature: at or above it no liquid exists, so
none condenses and any liquid evaporates. The EOS saturation curve (an elastic
liquid against an ideal gas) otherwise keeps going far past the critical point:
it gave hydrogen 4.6 MPa at 230 K, so Terra's dense hydrogen pockets condensed
into liquid hydrogen at room temperature.

## Reference data

Rows are keyed by the game's element id. Sources:

- NIST Chemistry WebBook, SRD 69: enthalpies of vaporization; liquid sound
  speed and density of cryogens, from which the bulk modulus is `rho c^2`.
- Engineering ToolBox: bulk moduli and viscosities of water, sea water, brine,
  ethanol and oils, and of mercury.
- CRC Handbook of Chemistry and Physics, 97th ed.: enthalpies of vaporization
  of the elements and of sodium chloride.
- Iida and Guthrie, *The Thermophysical Properties of Metallic Liquids* (2015):
  sound speed and density of molten metals at melting.
- Lange and Carmichael, *Rev. Mineral.* 24 (1990): silicate melt
  compressibility (magma and molten glass, about 15 GPa).
- NIST Chemistry WebBook and CRC Handbook: critical temperatures of water,
  ethanol, carbon dioxide, oxygen, hydrogen, methane, propane, helium, chlorine,
  sulfur, phosphorus, mercury and nitrogen.
- Custom nitrogen content and its liquid reference row were removed by the
  2026-10-27 user decision. The game catalogue uses only built-in elements.
  Standalone solver reference species and fixed gates remain unchanged.

### Saved catalogues

The physical checkpoint records the complete registry and EOS identity,
including materials absent from the colony. The removed nitrogen catalogue
is refused before native loading. There is no fallback to an older nitrogen
surrogate law: old R&D saves are not supported. New colonies use only the
current built-in-element catalogue.

## Surrogates

- **Latent heat:** Trouton's rule, 88 J/(mol K) times the game's boiling
  temperature, divided by the game's molar mass.
- **Bulk modulus** by class: aqueous 2.2 GPa, organic 1.3 GPa, cryogenic 0.5 GPa,
  molten salt or silicate 15 GPa, molten metal 100 GPa. A liquid without a row is
  classed by its name (`Molten…` metal, `Liquid…` cryogenic, water/brine/milk
  aqueous, otherwise organic).
- **Critical temperature:** Guldberg's rule, 1.6 times the game's boiling
  temperature (metals, salts, oils, magma).
- Fictional materials (super coolant, visco-gel) use these rules.

## Miscibility

Liquids in one nonzero group mix within one liquid phase; others keep separate
phases and layer by density. The group becomes `liquid_phase_group` on the
species: the solver's sharp interfaces never split one group, and
[settling](../native/MAC_TRANSPORT.md#liquid-settling) sorts different phases by
density. Molecular diffusion between miscible liquids (about 1e-9 m²/s) moves
less than a millimetre over ten cycles, so liquids mix by flow only.

| Group | Liquids |
| --- | --- |
| Aqueous | water, polluted water, salt water, brine, ethanol |
| Hydrocarbon | crude oil, petroleum, naphtha |
| Light hydrocarbon | liquid methane, liquid propane |

Molten metals, magma, glass, salts, cryogens other than the light
hydrocarbons, super coolant and visco-gel each form their own phase.

Viscosity is recorded for each liquid but the solver still applies one owner
viscosity.

## Verification

`ma_oni_catalogue_tests` (CTest `oni_material_catalogue`) builds a catalogue from
made-up records and checks pair detection, the calibrated latent heat and
saturation point, the constant-volume capacity, reference rows, surrogates and
rejection of malformed records.
