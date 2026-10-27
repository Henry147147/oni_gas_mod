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
least 15% oxygen by moles and 15 kPa oxygen partial pressure by default.
Polluted oxygen counts toward oxygen. An oxygen-poor room remains unsafe.

Use **Settings → Gameplay → CO₂ breathing limits** to enforce CO₂ thresholds.
This can be changed during play and is stored with the colony. The default
optional thresholds are 0.5% and 500 Pa; change the numeric thresholds before
starting a colony under **Advanced → Breathing thresholds**. With limits on,
the starting notice also estimates CO₂ headroom and advises removal if it is
short. The oxygen reserve does not remove CO₂.

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
