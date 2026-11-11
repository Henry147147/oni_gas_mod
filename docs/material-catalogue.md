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
| Molar mass | The ideal-gas molar mass: the game's value times the atoms in the element's molecule ([molecular masses](#molecular-masses-014-2026-11-11)). |
| Specific heat | The constant-volume capacity `cv` (a game cell has fixed volume), so a cell's heat content matches the game; `cp = cv + R/M`. |
| Liquid full-cell mass | The zero-pressure density in kg/m³: a cell holds that mass at zero pressure. |
| Transitions | Boiling and condensation temperatures anchor saturation pairs; other transitions become audited conversions. |

Temperatures are admitted from 1 K to 100,000 K (the game kernel admits gases
at any positive temperature, below). Liquids may compress to twice
their zero-pressure density. Formation enthalpies are zero except for the
vapour of a saturation pair.

### Molecular masses (0.1.4, 2026-11-11)

The game's element table gives oxygen, polluted oxygen, hydrogen and chlorine
the mass of one atom (about 16, 16, 1 and 34.5 g/mol). Their gases are
molecules of two atoms, so until 0.1.3 a kilogram of any of them exerted
twice its physical pressure and normal base air read about 250 kPa. By user
decision (2026-11-11) the mod multiplies the game's value by the atoms in
the molecule: 2 for `Oxygen`, `ContaminatedOxygen`, `Hydrogen`, `ChlorineGas`
and their liquids `LiquidOxygen`, `LiquidHydrogen` and `Chlorine`; 1 for every
other element (`MolecularMass` in the core, `OniMolarMass` in the mod, which
every reader of a molar mass goes through). The rule scales the game's own
number, so no game value is committed and a changed element table follows.

- 1.6-1.8 kg of oxygen in a tile at 20 °C reads 122-137 kPa (it read
  245-275), and a pocket of hydrogen or chlorine half its former pressure.
  Carbon dioxide, natural gas, steam and the other gases read as before.
- γ = 1 + R/(M cv) falls for these gases: oxygen 1.52 to 1.26, chlorine 1.50
  to 1.25, hydrogen 4.44 to 2.72 (still beyond any physical gas, below).
- Gases layer by molecular weight: hydrogen (2), natural gas (16), steam (18)
  and sour gas (19) rise above oxygen (32); carbon dioxide (44) sinks below
  it and chlorine (69) below that. With the game's values oxygen sat above
  natural gas and chlorine above carbon dioxide.
- The reference latent heats of liquid oxygen, hydrogen and chlorine are per
  kilogram and unchanged; their saturation curves now use the molecule's
  mass.
- Phosphorus and sulphur gas keep the game's atomic values: their vapours
  are mixtures of molecules (P₄/P₂, S₈ to S₂) that change with temperature,
  and no single mass is right. Fictional gases keep the game's values.
- The catalogue's identity changes, so the save envelope's material model
  is 4 and a colony saved by 0.1.3 or earlier is refused at the menu with
  "Start a new colony with this version"; there is no migration
  ([saved catalogues](#saved-catalogues)).

Control: `MolecularMass` in `MixedAtmospheres.Presentation.Tests`
("diatomic game elements take the mass of their molecule").

### Hydrogen expansion below 1 K (fixed 2026-11-02)

The game's specific heat and molar mass made hydrogen an ideal gas with
γ = 1 + R/(M cv) = 1 + 8249/2400 = 4.44, beyond any physical gas (at most
5/3); with the molecule's mass (0.1.4) it is 1 + 4124/2400 = 2.72. Carbon gas
(1.98) also exceeds 5/3; every other base-game and Spaced Out! gas lies
between 1.005 and 1.37 (sulphur gas; oxygen was highest at 1.52 until 0.1.3
and is now 1.26). The game's specific heats
are kept (delegated decision 2026-11-02: they set hydrogen's gameplay heat
behaviour). The rest of this section describes the γ 4.44 gas of the time.
An isentropic expansion cools as
ρ^(γ−1) = ρ^3.44, so a hydrogen pocket that spreads into vacuum to a 29th of
its density cools from 319 K below 1 K. The Spaced Out! `spaced-out-rail-gun`
arena did this deterministically: closing cells beside a natural pocket (seven
cells of 0.1128 kg at 319 K and a 3.857 kg chlorine cell) pushed it into
vacuum, and at owner frame 3 a cell held 1.9 g at 0.95 K, below the
catalogue's 1 K floor, and the owner faulted ("EOS outside its
thermal/pressure domain").

This was not lost or misbooked energy. A traced replay of the exact pre-fault
world (`MA_COLONY_FAULT_DUMP`) shows ordinary pressure work, with each step
cooling the pocket less than its isentrope; the cell's internal energy
(−856.8 J) is negative only through hydrogen's formation offset, and its
thermal part is positive (+4.5 J). The game kernel now admits an ideal gas at
any positive temperature (`mac_game_kernel.cpp`, `table`); positive thermal
energy still guards every reading, and liquids keep the 1 K floor. The saved
reference material, and so the EOS identity and checkpoint bytes, are
unchanged. A trajectory whose gases stay at or above 1 K computes exactly as
before, except that an EOS trial search may now accept a candidate gas state
below 1 K that it used to reject; the change applies to every game profile,
saved ones included. The exact
pre-fault world now advances 60 frames under its saved profile 9 and under
profile 10, and the original rail-gun scenario (the `0bf4268` managed build
with the fixed native libraries) passes in game on profiles 10 and 9 instead
of faulting at frame 3; `mac_game_cold_gas_control.hpp` expands a γ 4.44 gas below 1 K
under both. Very cold hydrogen then condenses and freezes through its game
transitions like any other cold hydrogen.

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
