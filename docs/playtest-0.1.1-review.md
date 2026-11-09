# Review of the 0.1.1 Windows playtest, 2026-11-08

The user's 12:47 recording (2560x1440) and Player.log of a new Spaced Out!
colony on private candidate [0.1.1](playtest-0.1.1.md) reported sublimation
not working, oxygen draining fast, and water and liquids behaving badly. The
recording and the per-frame text extracted from it stay private under ignored
`artifacts/video-0.1.1/`. The log records 10,073 owner frames at 1.9x real time,
no owner fault and live settings changes (evaporation on to off, Always
simulate to Activity tiers, steps per tick 2 to 1, later back to evaporation on
and four steps).

## Findings

- **Starting oxygen reserve skipped.** The user opened Settings at the
  colony's start. The starting-air bridge waited 600 publications for the
  duplicants; while paused these passed in about four real seconds, so it
  recorded 0 duplicants and placed no oxylite (earlier runs placed about
  1,740 kg). Forty cells held 127 kg of oxygen, about 0.7 cycle for three
  duplicants. The base fell from about 50 kPa to 17 kPa within two minutes of
  the recording, CO₂ reached 30-60% near the floor, and all three duplicants
  were suffocating in cycle 2 while a sealed cavern above held 1.7 kg oxygen
  cells. The owner's ledgers stayed closed: nothing was deleted. Fixed: the
  wait counts game time (120 s), which a paused start does not advance.
- **Natural tiles never sublimate.** An exposed oxylite tile read "Emitting
  Oxygen: 0 kg/s" beside 0.23 kg gas cells. ONI's native simulation off-gasses
  exposed oxylite, polluted dirt, polluted mud and corium tiles and
  polluted-water surfaces; the mod suppressed that and replaced only debris
  sublimation. Fixed: ONI's own `DoSublimation` now runs on the owner's shadow
  of each gas or vacuum cell beside a sublimating tile, with the 1.8 kg cap
  against the owner's total gas, and its product is booked into the owner;
  polluted water follows ONI's surface rule
  ([natural sublimation](colony-owner.md#natural-sublimation)). In the
  `natural-sublimation` scenario the oxylite tile's mass follows ONI's own
  simulation exactly; WindowsPlayer under Proton passes it too.
- **Pools.** Duplicants dug beside and under a pool that flooded CO₂-rich
  pockets. Pool cells then held liquid carbon dioxide at 22 °C, and one
  bottom-corner cell held 1,070 kg, overfull, at 5.51 MPa for at least 14 s
  beside a 0.6 kPa cell. The `flood-pocket` and
  `flood-co2-corner` scenarios reproduced MPa transients (up to 19.9 MPa in
  offline replicas) and room-temperature liquid CO₂ that took minutes to boil
  off in liquid-full cells. Causes, all in the profile 12 kernel: the pressure
  solve let a liquid cell with little room take far more inflow than it could
  hold; faces advancing on different clocks could overfill a cell; and phase
  change ran at fixed cell volume, so liquid-full cells could not make room
  for vapour. The 5.51 MPa cell was a paused Activity-tiers region holding its
  last spike (regions never split a liquid body). Fixed: inflow is limited to a
  cell's room, a post-transport check refines a step that still overfills a
  cell, and a liquid above its boiling point in a held liquid body boils during
  the flow and displaces its neighbours; overfills up to 0.3% of a cell
  (about 0.26 MPa in water) are tolerated so stiff liquids such as magma do not
  cost refinements ([game kernel](../native/MAC_GAME_KERNEL.md)). Flood peaks
  now stay below 0.73 MPa, the catalogue's CO₂ saturation pressure at room
  temperature (in game 218 kPa), and 70 kg of liquid CO₂ in a pool corner boils off within 20 s.
  Profile 12 arithmetic changes; profiles 2-11, the checkpoint format and
  version 32 are unchanged. Cost against 0.1.1: the Spaced Out! fault-world
  replay flows about 11% slower (165 against 148 ms a frame), the large
  industrial save runs 1.654 against 1.696 game seconds per real second, and
  a Volcanic colony runs faster (2.55 against 1.52).
- **Underwater readings.** Trapped gas in nearly full liquid cells (often
  vapour boiled into a near-vacuum pocket under the boil-only rule) produced
  hover lines such as "Steam <100%" and oxygen warnings under water. Friendly
  readings now call such cells (95% liquid or more) submerged.
- **Cold gas at sources.** The oxygen diffuser building read 0.5 °C and its
  cell 5 °C in a 22 °C room. Injected gas carries internal energy, not flow
  enthalpy, so a strong source cools the cells it pushes into. Supplying
  enthalpy removes that but heats sealed rooms by compression as machines
  pressurize them: in `playtest-diffusers` the room's gas spans 284-302 K
  today and 380-451 K with enthalpy injection (`playtest-pump-vent`: 278-305 K
  and 322-512 K). That change was not adopted; the source energy model remains
  the user's decision.
