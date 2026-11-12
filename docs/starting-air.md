# Starting air and oxygen reserve

World generation keeps ONI's usual oxygen and polluted oxygen. Mixed
Atmospheres adds no nitrogen elements or nitrogen-air conversion. The former
Hard mode preset and nitrogen registration tests have been removed. Old R&D
saves containing that catalogue are refused; start a new colony.

On a new colony, the mod measures the gas in the open region connected to the
printing pod, on that world only. Once the starting duplicants exist, it uses
their oxygen consumption and the colony's oxygen thresholds to estimate
breathable oxygen, assuming the region is well mixed. If that falls short of
ten cycles, it places the missing oxygen as oxylite debris beside the pod and
tells you how much. Existing oxygen in the region counts toward the reserve.
The reserve is finite; build sustainable oxygen production. Oxylite releases
oxygen while its surroundings are thin and may remain as debris in dense gas.

**CO₂ breathing limits default to Off.** CO₂ still mixes, displaces oxygen,
passes through pipes and contributes to pressure. Duplicants always need at
least 15% oxygen by moles and 15 kPa oxygen partial pressure by default, and
air in which chlorine, sour gas and nuclear fallout together reach 5% cannot
be breathed. Polluted oxygen counts toward oxygen. An oxygen-poor room remains
unsafe.

Use **Settings → Gameplay → CO₂ breathing limits** to enforce CO₂ thresholds.
This can be changed during play and is stored with the colony. The default
optional thresholds are 2% and 5 kPa (0.5% and 500 Pa before 2026-11-11);
change the numeric thresholds under **Advanced → Breathing thresholds**, at
the main menu for new colonies or in a running colony for that colony. With limits on,
the starting notice also estimates CO₂ headroom and advises removal if it is
short. The oxygen reserve does not remove CO₂.

## The start-of-colony dialog

Once the starting duplicants exist, one dialog (**Mixed Atmospheres**) opens
in every new colony. It leads with what was done: the oxylite placed and that,
with the pod's own air, it covers ten cycles; then, as context, how long the
pod's air alone would last (without a reserve: how long the pod's air lasts).
With CO₂ limits on it adds the CO₂ headroom and, when it is short, how to plan
removal. It ends with **What plays differently**, at most eight one-line items
built from the colony's own settings (`ColonyPrimer.cs`):

- Gases stay mixed. A Gas Filter takes out the gas you select and lets the
  rest through.
- Duplicants need air with at least 15% oxygen and 15 kPa of oxygen
  (polluted oxygen counts): see the Oxygen margin overlay.
- Chlorine, sour gas and nuclear fallout make air unbreathable once together
  they are 5% of it (fallout only with Spaced Out!; with CO₂ limits on the
  line adds carbon dioxide above its limits; with no toxic gas limit and CO₂
  limits off there is no such line).
- An electrolyzer releases twice as much hydrogen as oxygen, by moles, into
  the same air: filter or pump the hydrogen out.
- A liquid lock holds back only about 10 kPa per tile of water height, and
  base air is about 125 kPa: seal off vacuum with airlocks.
- Gases layer by molecular weight: natural gas and steam rise above oxygen;
  chlorine sinks below carbon dioxide.
- Boiling follows pressure: water in vacuum or thin air boils at room
  temperature. With **Evaporation below boiling** on, this item is replaced by
  "Evaporation below boiling is on: air near water holds about 2% steam and
  wet rooms form dew (Gameplay → Boiling and condensation)."
- A Gas Element Sensor reacts to its gas from 0.1% of the air by mass; a Gas
  Share Sensor switches at a share you choose.

Each item was checked against the code or follows from the element table:
an electrolyzer splits water, so with 32 g/mol oxygen and 2 g/mol hydrogen
([molecular masses](material-catalogue.md#molecular-masses-014-2026-11-11))
its 0.888 kg/s oxygen and 0.112 kg/s hydrogen are 27.8 and 55.5 mol/s
(both go into the owner's cell, the `electrolysis` scenario); gases layer by
molar mass (2, 16, 18, 32, 44 and 69 g/mol for hydrogen, natural gas, steam,
oxygen, carbon dioxide and chlorine);
water at 1000 kg/m³ under 9.81 m/s² holds 9.8 kPa per one-metre tile; the
boil-only rule boils water at room temperature in vacuum
([phase change](../native/MAC_PHASE_CHANGE.md)); element sensors use a 0.1%
trace threshold by mass of their phase (`ColonySensors.cs`). **Settings →
Gameplay → How it plays** shows the same list again, with the shown
settings' numbers (the colony's in a colony, new colonies' at the main menu).

The **Gas Share Sensor**, unlocked with the gas element sensor, compares the
selected constituent's share by moles with a percentage threshold. Use it to
control oxygen production or filtration. It reads the full mixture in its
cell, including gases that are not the dominant constituent.

Implementation: `ColonyStartingAir.cs`, `StartingAirBridge.cs`,
`GasShareSensor.cs`; engine-free arithmetic/settings tests in
`tools/SettingsTests`. The in-game `starting-air` scenario checks the reserve,
vanilla starting oxygen, absence of nitrogen elements, CO₂ opt-out and live
opt-in, the matching display, oxygen still being required and share sensors.
Validation arenas place the reserve only with `MA_COLONY_STARTING_RESERVE=1`.
Standalone solver reference species and fixed physics gates are unchanged;
their synthetic nitrogen fixtures do not register game content.
