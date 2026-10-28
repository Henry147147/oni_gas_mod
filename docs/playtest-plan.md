# Base-game playtest plan

**Current regression, 2026-10-28:** the user's Windows recording and uploaded
colony reproduce large temperature, pressure and mass swings after digging.
Profile6 / core24 bounds pressure intervals before transport and advances
independent regions separately. The same twelve-dig WindowsPlayer/Proton
sequence, fresh Fast-preset digging, completed-frame32 writing and ordinary
cold/menu loading pass. Focused reference/conservation/continuation controls
and both 89-test native suites pass. Performance remains below release targets;
see [pressure intervals](../native/MAC_GAME_KERNEL.md#pressure-intervals-after-digging).

**Priority amendment, 2026-10-27:** the next usable private Windows playtest
must support Spaced Out! fully. See [DLC work](dlc-compatibility.md) and the
[accepted amendment](completion-plan.md#amendments). The base-game-only
candidate does not meet the user's installed DLC requirement.

Accepted 2026-10-20 (user decisions). This is the current work. One worker
takes it through on `main`. It comes before the milestone order of the
[completion plan](completion-plan.md) and supersedes the
[readiness work plan](playtesting-readiness.md#work-plan). The completion
plan's release requirements still apply afterwards.

**Gameplay amendment, 2026-10-27:** nitrogen content and starting-air conversion
are removed; older R&D saves need not be preserved. CO₂ breathing limits
default off and can be switched during play. Technical settings and solver
presets belong under Advanced. See [settings](settings.md) and
[starting air](starting-air.md).

## Goal

The user can start a new **base-game** colony on any base-game starting
asteroid and play the full game on it on Linux and Windows x64: dig, build, breathe, pipe, run
every machine, heat and cool, farm, ranch and launch rockets. `MacClosedWorld`
owns every gas and liquid on the asteroid. ONI's fluid simulation is never used.

The 2026-10-25 amendment adds Windows x64, validated here with WindowsPlayer
under Proton and confirmed on Windows by the user's private playtest. Real-time
speed, Spaced Out! and the gameplay packs follow this plan; the completion
plan's targets are unchanged.

## Decisions

- **Balance.** Machines, duplicants, critters, plants and geysers keep ONI's
  rates, power and recipes. The solver decides how fluids move, mix, compress,
  carry heat and change phase. Where a vanilla rate breaks physical energy
  balance (an electrolyzer splitting water on 120 W, a steam turbine condensing
  steam), the difference is booked to a named balance ledger for that source,
  so every audit closes and nothing is hidden. The user tunes balance later.
- **Breathing.** At least 15% O2 by moles and 15 kPa O2 partial pressure. CO2
  limits default off; enabling them applies the saved thresholds (default
  0.5% and 500 Pa). Polluted oxygen counts as oxygen. World generation stays
  vanilla; no nitrogen content or nitrogen-air option.
- **Liquids mix.** A cell's liquid is a mixture, like its gas: ONI's liquid
  elements are its constituents and travel together. Liquids that do not mix
  in reality (oil and water, molten metals) keep separate phases and layer by
  density. There are no dissolved gases: gas and liquid exchange mass only by
  phase change.
- **Pipes.** Gas and liquid pipes both carry mixtures. A pump takes in the
  mixture at its intake, a filter removes its selected element and passes the
  rest, and a machine takes only what its recipe uses.
- **Asteroids.** Every base-game starting asteroid loads. Development and the
  harness use Terra, seed 147147.
- **Speed** is measured and reported, never a gate here and never a reason to
  cut a feature.
- **Platforms:** Linux and Windows x64. Windows OS behaviour and speed need
  the user's PC; Proton results are labelled separately.
- **Evidence stays light** ([below](#evidence)).

## What needs to be done

### Scope checklist

Capture the base-game registry (no DLC) with `tools/runtime_inventory.py` and
mark each [coverage inventory](ownership-coverage.md) entry in or out of this
playtest. The in-scope entries are the checklist: the playtest is ready when
each has an adapter and an in-game check.

### Materials

- **Catalogue:** one definition per base-game gas and liquid. The game's element
  files list 31 gases and 50 liquids outside Spaced Out!; the registry capture
  gives the base-game subset. Five exist today (N2, O2, He, water, CO2).
- **Sources:** values the game has (molar mass, specific heat, conductivity,
  transition temperatures and targets, liquid density) are read from the
  installed game at runtime and never committed. Values it lacks (latent heats,
  viscosity, compressibility) come from public reference data, cited in the
  catalogue. Fictional materials get documented surrogates.
- **Miscibility:** the catalogue groups the liquids that mix (the water family
  with ethanol; the oils among themselves). Each group is one liquid phase.
- **Phase change for every pair:** each liquid/vapour pair boils by pressure,
  anchored at the game's transition temperature at 1 atm, with latent heat, as
  water does today. Liquid/solid transitions hand the solid side to ONI
  ([terrain](#terrain)).
- **Transitions with byproducts:** transitions that change identity or leave a
  byproduct (salt water boils to steam plus salt; crude oil cracks to
  petroleum; petroleum to sour gas) are audited conversions. Solid byproducts
  go to ONI as debris.
- **Germs:** ONI's diseases travel with the fluid as passive quantities. They
  grow and die by medium, as in vanilla.

### Solver

Standalone work, each item with its own controls:
- Many constituents in one world; check that a cell's cost follows what it
  holds, not the catalogue size.
- Miscible liquid mixtures. Sharp interfaces (selector 3) separate every pair
  of liquids today; a miscible group must act as one phase there. Liquids mix
  by flow, at a diffusion rate set in the catalogue.
- Cells holding gas and more than one liquid phase (oil on water under air).
- Turning a cell between fluid and solid, conserving every constituent, energy
  and momentum. Building a tile pushes its fluid into the neighbours; digging
  opens a cell that fills from around it. No such operation exists today.
- Open sky: gas reaching space leaves through a booked sink.
- Conversions that change identity, and the balance ledger.
- Phase change switched on in the game owner.

Open accuracy items (strong-flow separation, collision refinement) do not block
the playtest.

### Taking over the asteroid

- **Normal games:** the mod starts on a normal new colony with no development
  file. Messages are routed outside the validation classes.
- **Whole-asteroid import:** every base-game starting asteroid imports, and one
  owner covers it. The configured domain is capped at 4,096 cells and 256 per
  side; a base-game asteroid is 256 × 384.
- **Vacuum:** empty and partly filled liquid cells (a1e3db3) remove the recorded
  compressed-liquid admission failure. The configured Linux scene passes rest,
  gas expansion, Grid/GPU publication, save and exact fresh-process/menu reload;
  8,401-frame continuation also passes, using user-authorized developer
  invincibility in the unowned colony. This certifies no life support.
  Whole-asteroid ownership still needs the geometry and catalogue work above.
- **Readers:** every game reader sees the owned state, including rendering,
  element/mass/temperature queries, pressure, pathing, placement checks and
  overlays.
- **Pacing:** the owner advances on ONI's simulation worker inside its frame,
  so a slow step delays the game. When the solver falls behind, the game clock
  slows the way a lower speed setting does. The camera, menus and pause stay
  responsive.
- **Speed:** measure a full asteroid and report the result.

### Terrain

- Digging, building and deconstructing.
- Doors and airlocks opening and closing; mesh and airflow tiles.
- Tiles melting into liquid and liquid freezing into tiles.
- Falling sand and debris; entombment.
- Off-gassing and sublimation (oxylite, bleach stone, polluted dirt, slime).
- Meteors.

### Heat

- Heat between fluids and every solid tile, building, item, piece of debris and
  body, at every owned face.
- Buildings' heat output; buildings overheating and melting.
- Heat-moving machines: thermo aquatuner, thermo regulator, space heater, liquid
  tepidizer, ice-E fan and wheezewort.

### Life

- Breathing for every duplicant under easy mode; masks and suits; exhaled CO2.
- Critters that breathe, eat or emit gas or liquid.
- Plants' atmosphere and pressure needs, gas intake, irrigation and emissions.
- Germ exposure.
- Scalding, frostbite, submersion and popped eardrums.

Already exists: breathing selection, live breathing for one tracked duplicant,
and body heat.

### Pipes and storage

- Owned pipe contents for gas and liquid.
- Pumps take the cell's mixture: a liquid pump takes only liquid, a gas pump only
  gas.
- Vents stop at overpressure.
- Filters, valves, shutoffs and bridges.
- Pipe sensors. An element sensor is true when its element is above the trace
  threshold.
- Contents are dumped on deconstruction; pipes freeze and overheat.
- Bottles, canisters, the pitcher pump, mopping and reservoirs.

Already exists: the reservoir bridge, not yet connected to the owner.

### Machines and conversions

- Every base-game converter and recipe runs as one reserved transaction covering
  inputs, output room, power and heat. A blocked output consumes neither input
  nor power.
- A machine takes only the elements its recipe uses. Anything else arriving in
  its input is stored or dumped, as vanilla does with a wrong element, and is
  never deleted.
- Geysers, vents and volcanoes, with external source ledgers.

Already exists: point, radius and rectangle transfers with receipts, and
registered consumer and emitter actors.

### Rockets

Base-game starmap rockets:
- exhaust and heat at launch and landing;
- fuel and oxidizer tanks;
- gas and liquid cargo.

Fluid that leaves and returns with a rocket is booked to an external ledger.

### Presentation and automation

- Composition, pressure, oxygen and breathability overlays, hover and details on
  the owned asteroid. Liquid mixtures are listed like gas mixtures.
- Pipe contents.
- Atmo, hydro, element, germ and thermo sensors read the real mixture.
- Breathing failures are explained.

Already exists: display fixtures for hover and the overlays.

### Saves

- Manual saves and autosaves; loading from the load menu; overwriting a save.
- A mixture save is refused when the mod is missing.
- Saves with transfers in flight, and interrupted saves.

Already exists: paired checkpoints, frame-zero and natural autosaves, and
same-process menu reload in the validation modes.

### Ready to play

- Every in-scope checklist entry has an adapter and an in-game check.
- From a prepared mid-game save, an unattended harness run completes ten cycles
  with every duplicant alive, no domain fault and closed conservation ledgers.
- Then the user plays.

## Order

Materials come before the whole-asteroid import, because a real asteroid cannot
load without them. The import comes before the gameplay adapters, which need an
owned asteroid. Beyond that the worker chooses the order, keeping `main`
buildable and the asteroid loadable.

## Evidence

- **Every commit:** a Linux Release build and the CTest suite. When a change can
  alter solver arithmetic:
  - compare the `ma_mac_*` outputs with the parent revision from clean builds
    (touch the sources first);
  - say in the commit message whether arithmetic or checkpoint bytes change.

  New behaviour gets one focused control, not a family of them.
- **Sanitizers, ThreadSanitizer, MinGW/Wine, MSVC and Proton** only when a change
  touches threading or memory ownership, or a failure calls for them.
- **In game:** one short Linux harness scenario per system, showing that it
  works and its ledger closes. Add a screenshot only when the result is visual.
- **Commit messages and docs** stay short: what changed, what was checked, what
  is open. Keep [implementation status](implementation-status.md) to a line or
  two per system, without evidence narratives.

## What plays differently from vanilla

- The current default profile5/core23 uses a positive-volume liquid law. Below half
  the saved bulk stiffness it retains the earlier linear law; higher pressures
  progressively stiffen compression, with continuous volume and elastic energy.
  High-pressure readings and boiling therefore differ from profile2/core20.
  The remaining accuracy-floor proofs continue after the first private
  playtest under the 2026-10-25 amendment. Old saves continue with their saved law.
- The current game kernel dampens very rapid pressure waves and
  makes liquids more compressible (saved stiffness K/25). One 0.2 s solver
  step with two flow parts covers a default tick; donor-cell gas transport spreads composition
  fronts, internal fluid heat updates once per tick and latent phase relaxation
  once per second. A bounded sharp-interface correction remains for liquids.
  Mixing, inertia, per-cell physical pressure and latent heat remain present.
  Profile5 also saves gas heat mixing8. These numerical choices belong to the
  game profile; the strict reference and production physics gates are separate.
- In the mixed-pipe implementation, a deconstruction spill that cannot
  fit stays owned at its source and appears as "Fluid waiting for space". It
  releases when room opens. Mixed pipes and their saves are on by default
  under the 2026-10-25 amendment.
- A liquid vent over open space puts its mixture into its own cell; the solver
  carries it down. Vanilla's falling drip animation is not used.
- Gases mix and stay mixed, and so do miscible liquids. CO2 does not collect in
  pits the way it does in vanilla, so scrubbing serves whole rooms.
- Plan sustainable oxygen production; the finite starting oxylite reserve
  covers a ten-cycle oxygen shortfall. With CO2 limits enabled, arrange CO2
  removal too: a sealed cave can exceed them while still rich in oxygen;
  check Breathability and CO2 partial pressure along reachable routes.
- Pressure is physical. Gas compresses and expands into vacuum. Liquid is nearly
  incompressible, so there are no 1,000+ kg compressed liquid cells. A liquid
  lock holds only the pressure its liquid height supports.
- Boiling depends on pressure, and phase changes take or release latent heat.

## After this plan

- Real-time speed.
- Spaced Out! and the gameplay packs.
- Nitrogen content and nitrogen air (removed by the 2026-10-27 amendment).
- The completion plan's remaining release requirements.
