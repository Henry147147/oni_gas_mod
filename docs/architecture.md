# Physics and integration contract

## Scope and units

The public target is a complete replacement for gas and liquid physics, not a
second composition table attached to vanilla fluid motion. All fluid-producing
and fluid-consuming operations must use the authoritative mixture inventory.
The release target covers the base game, Spaced Out! and all official gameplay
packs on Windows and Linux, with new colonies only. Dissolved O2/CO2 and aquatic
gas exchange are required by the [accepted completion plan](completion-plan.md)
and remain unimplemented. Environmental fires/explosions, microscopic droplets,
capillary effects and detailed solution chemistry are outside the agreed scope.

Native calculations use kilograms, metres, seconds, kelvin, pascals, joules,
and moles. Inspection shows kPa and degrees Celsius with Pa/K equivalents,
constituent moles and kg/g masses where useful: `grams = 1000 * kg`. A gas's
breathing fraction is its mole fraction, not its mass fraction. Each cell has
an explicit geometric volume; the reference grid uses 1 m × 1 m × 1 m cells by
default.

For gas constituents `i`:

```
n_i = m_i / M_i
x_i = n_i / sum(n_j)
V_gas = V_cell - sum(m_liquid / rho_liquid) - V_solid
p = sum(n_i) R T / V_gas
p_i = x_i p
rho_gas = sum(m_i) / V_gas
```

Mass and energy are authoritative. Temperature is reconstructed from species
internal energies; it is never averaged arithmetically. The current reference
model uses constant heat capacities and formation energies; the in-game owner
uses a declared ideal-gas/elastic-liquid EOS. Inventories preserve trace
quantities rather than deleting small species to make room for a fixed number
of constituents.

## Buoyancy and mixing

Gravity acts on the bulk mixture. Pressure gradients and density differences
produce buoyancy; there must not be an arbitrary per-species upward/downward
swap rule. At equal temperature and pressure a hydrogen-rich parcel has lower
density than surrounding air, and a warm air parcel can rise. Gases diffuse and
remain mixed; ordinary room air must not rapidly sort into pure oxygen and
nitrogen layers. Hydrostatic equilibrium must remain stationary numerically.
The closed owner now supports conservative, species-specific
[mixture-averaged gas diffusion](../native/MAC_DIFFUSION.md), with enthalpy
transport and zero net diffusive mass flux. Its Maxwell–Stefan driving force
includes pressure diffusion in an entropy-producing discrete form. Faces
touching empty cells carry no diffusive flux. New gas development regions select
physical rates; accelerated mode uses the same equations and checks. Old
checkpoints retain disabled diffusion until explicitly upgraded. Thermal
(Soret) diffusion and liquid diffusion remain outside its scope. Property
correlations are extrapolated, and counted, outside their validated ranges.

## Production fluid solver

The production colony solver is `MacClosedWorld` (`native/src/mac_*.cpp`),
decided by the user on 2026-10-18. ONI's native SimDLL fluid simulation is what
the mod replaces and is never used as the solver. The owner is a closed,
occupied regular grid ([closed MAC owner](../native/MAC_SOLVER.md)):

- State is constituent mass, stored internal energy U and canonical face-normal
  momentum J per cell/face. No scalar total energy reconstructs U or J.
- EOS material pressure work updates J directly; midpoint face velocities give
  swept volumes. Shared limited reconstruction samples constituents, U and
  passive quantities in each swept parcel, and the parcel's actual mass drives
  primal and dual [MAC transport](../native/MAC_TRANSPORT.md).
- Compression/shock heat, internal conduction, optional Newtonian viscosity and
  opt-in [gravity](../native/MAC_PRESSURE.md#gravity) commit inside the same interval.
  Wall impulse, transport projection heat and mixing heat have separate
  accounts; unresolved stored rounding is explicit.
- Whole intervals are prepared atomically. Invalid candidates retry smaller
  steps without partial changes; exhausting the budget rejects the interval.
- Energy and entropy are both checked, rather than assuming one follows from an
  arbitrary energy update; see
  [De Michele and Coppola](https://arxiv.org/abs/2210.01251).

The pressure-work closure is a project derivation for constant-property
mixtures. In a material cell the constituent masses and liquid volume stay
fixed and gas and liquid share one temperature. With `Q = Cv_total * T`,
`k = n_gas * R / Cv_total` and gas headspace `Vg`, reversible compression gives
`Q_new = Q_old * (Vg_old / Vg_new)^k`. The work pressure is
`p_work = -(Q_new - Q_old) / (V_new - V_old)`, with its analytic limit at zero
volume change and a series expansion against cancellation. The same face areas
and midpoint velocities define both the volume change and the kinetic-energy
change, so pressure work cancels staggered kinetic energy face by face. In the
original reference step, completely filled incompressible cells keep their
volume and solve for pressure; the in-game owner uses an elastic liquid EOS.

Current limits: the owner rejects vacuum and partially filled free surfaces,
has no phase topology or immiscible interfaces, and does not implement
range-extended vertical gravity work. The original strong-flow separation and
gas/vacuum front gates fail; status and next steps are in
[playtesting readiness](playtesting-readiness.md#gate-2-production-timestep).

The production solver must treat pressure implicitly while preserving
compressibility, vacuum behavior, energy and positive constituent masses.
Research precedent supports this direction but does not provide a ready-made
multiphase implementation: [Tavelli and Dumbser, all-Mach staggered method](https://arxiv.org/abs/1612.08866)
and [Dumbser and Casulli, conservative semi-implicit finite volume method](https://iris.unitn.it/handle/11572/148704).

### Reference components

These remain validation references; none advances colony fluids:

- `GasWorld`: explicit gas finite-volume solver with HLLC fluxes, HLL
  fallback and hydrostatic reconstruction, conserving species, momentum and
  total energy including gravitational potential. It is still behind the
  public C ABI, `ma_demo` and the probe's scoped reference world, which is
  never bound to a live colony. Explicit acoustic timesteps make it unsuitable
  at colony scale.
- A sparse pressure operator (compressibility diagonal plus face conductances,
  preconditioned conjugate gradients, residual-checked, rejecting unanchored
  components) and a frozen-coefficient backward-Euler acoustic step, with
  cell compliance `C = V / (rho * c²)` and face inertia
  `M = rho_face * area * centre_distance`. Its quadratic acoustic energy is not
  thermodynamic total energy.
- A donor-cell gas/liquid bulk remap that moves every constituent, momentum and
  total energy with common weights, turns unresolved kinetic energy into heat
  and rejects condensed overfill before changing inputs.
- The integrated [`FluidWorld` prototype](../native/FLUID_WORLD.md) and its
  [coupled ABI](../native/COUPLED_ABI.md); its [gas/vacuum front gate](../native/GAS_VACUUM.md)
  fails.

Heat exchange between bodies and cells uses backward-Euler conduction with
pairwise conservative transfers and series half-cell resistances, solved
relative to each connected component's equilibrium to avoid cancellation in
stiff contacts; the maximum principle and closed-system entropy are checked.
Vacuum has no artificial heat capacity or conductance. The game adapter must
reserve and commit both endpoints of every exchange.

## Liquids and phase changes

Miscible liquids share a composition; immiscible liquids retain separate phase
fractions and separate under gravity. Liquid volume cannot exceed cell volume
by silently deleting mass. Phase density, thermal expansion, surface behavior
and real-fluid equations of state need an explicit supported property range.
Adding a falling-cell rule to a gas solver would not meet this contract;
production liquid flow needs a validated free-surface treatment and
phase-specific inventories.

Closed-vessel phase helpers balance mass and energy and use vapor partial
pressure. They reject unsupported temperatures and filled containers and are
not yet coupled to grid transport. Temperature-dependent properties and
validated phase tables must replace the reference approximations before
broad-temperature gameplay. [NIST Chemistry WebBook](https://www.nist.gov/programs-projects/nist-chemistry-webbook)
and [NIST-JANAF](https://www.nist.gov/srd/chemistry) are property-data sources;
individual properties must retain their source, units and validity range.

## Breathing and playability

Defaults in the standalone model:

| Setting | Default |
| --- | --- |
| Hard mode oxygen fraction | 15–35%, inclusive |
| Easy mode oxygen fraction | At least 15% |
| Minimum oxygen partial pressure | 15,000 Pa |
| Maximum carbon dioxide fraction | 0.5% |
| Maximum carbon dioxide partial pressure | 500 Pa |
| Initial breathable air | 21% O₂ / 79% N₂ at 101,325 Pa and 293.15 K |

All limits are gameplay parameters, not medical exposure guidance. Easy mode
removes the upper oxygen-fraction bound while retaining pressure and
contamination checks. Failure flags distinguish too little oxygen, excessive
oxygen, thin air, carbon dioxide and other contaminants. Species-specific
hazard limits are placeholders requiring balance review.

World generation must guarantee enough starting mixed air, inert-gas reserves,
water and early research materials. Required gameplay additions, not yet
registered ONI buildings:

- A mixture sensor and overlay showing pressure, partial pressures, temperature,
  constituent grams and the reason air is unbreathable.
- Accessible carbon-dioxide scrubbing with captured waste and regeneration costs.
- Ventilation fans and pressure-aware vents, bulk mixture pumps and selective
  separators with electrical/thermal costs.
- Nitrogen storage and generation, pressure regulation and later air
  separation. Nitrogen must never be a starting-map lottery.
- Mixed-fluid pipes/reservoirs with constituent-preserving transport, explicit
  phase compatibility, backpressure and overpressure behavior.

## Mixture presentation

Presentation is a read-only view of completed, explicitly supplied inventories;
the full policy is in [mixture presentation](mixture-presentation.md) and
[owned containers](container-inventories.md).

- `CaptureFrame` projects owned world cells and containers from one completed
  colony snapshot. `MixturePresentationRuntime` publishes one immutable view
  per Unity frame and pins it across texture/overlay worker jobs; reentrant
  publication cannot mix revisions.
- Containers bind by persisted full allocation handles; stale, foreign, retired
  or replaced bindings display unavailable and cannot adopt an allocation by
  its string ID. A failed provider displays unavailable instead of inferring a
  mixture from a native dominant element or packet. Unregistered native
  inventories remain labelled single-material packets.
- Normal rendering keeps ONI's shipped gas shader and passes; only the gas
  colour, visual-density and danger textures receive owned-cell overrides.
  Colours blend constituent sRGB palettes in linear RGB with a world-anchored
  4-by-4-cell pattern. Pressure queries and overlays use double-precision Pa;
  the danger channel and warnings come from the numerical assessment, not tint.
- Four overlays expose composition, total pressure, oxygen partial pressure and
  breathability. Unowned cells keep native colours and native breathing.
  Colour-only mode and contrast (default 0.04) change no inventories or policy.
- Worker sampling and colour lookup allocate zero after publication. Teardown
  retires providers, pinned frames, texture schedules and inspection bindings
  before native state is freed; session generations stop an old worker lease
  repinning a previous colony.

## Machines, metabolism and energy

Transfers are transactions. A full output must leave the input unchanged.
Bulk pumps move the source mixture proportionally; filters remove selected
species while retaining the remainder. Reference process models quote gas
compression with isentropic efficiency, incompressible liquid pumping with
hydraulic efficiency and isothermal selective gas separation. Streams carry
enthalpy `H = U + pV`; pump inefficiency heats the output. Separation uses the
ideal-gas demixing entropy for minimum work, and actual work is rejected as
heat at the feed temperature. The models do not yet reserve electrical power,
outlet capacity or a heat sink in game.

Example reactions include water electrolysis, hydrogen/methane/carbon
combustion, glucose-based metabolism and photosynthesis. Every reaction is
checked for elemental balance, and chemical formation energy participates in
the energy ledger. Fuel generators consume oxidizer and produce accountable
exhaust. Duplicant metabolism consumes stored food as well as O₂ and produces
CO₂, water and heat; breathing cannot turn O₂ into CO₂ without carbon input.
All other conversions, including fictional ONI materials, need documented or
surrogate compositions and balanced routes.

The reaction API enforces the first law and available reagents. It does not
yet enforce entropy production, equilibrium, reversible-work bounds or machine
efficiencies, so it cannot certify the absence of every thermodynamic exploit.
Energy includes formation energy and may be negative; positive temperature and
full-system conservation are the relevant checks.

## Native ownership and compatibility

The production integration is direct **in-memory native SimDLL integration**,
specified in [the native backend contract](native-backend-integration.md).
Hooks route fluid work to the custom engine within ONI's schedule while
retaining verified nonfluid services. Native scalar fluid cells are
projections, never the physical owner. Complete native reader/writer coverage,
explicit cross-domain transfers and completed-frame publication are required
before takeover; the forwarding observer grants no fluid ownership.

Constraints of ONI 744825 that shape this design
([inspected boundary](interop-contract.md)):

- The native simulator is a process-wide singleton. `Grid` holds pointers into
  arrays returned through `Sim.GameDataUpdate`.
- Cell mutations, component registration, consumers, emitters, temperatures,
  diseases, radiation, callbacks, saves and conduit thermal services cross
  separate native paths; most mutation calls bypass `Sim.HandleMessage`.
- There is no fluids-only disable switch. Skipping native frames stops other
  services, updating cells after vanilla simulation cannot undo emitted events,
  and a second native initialization does not create an isolated world.

The full adapter must publish custom merged arrays, preserve all callback
contracts, handle construction/digging/displacement atomically, and route every
consumer, producer and transport system through the mixture backend. Dominant
element projections may serve vanilla rendering but never become the physics
source of truth. Unsupported game/mod services must fail before a new colony
starts, with a clear compatibility message. Sound, gameplay
disease/radiation/thermal behavior and material-specific effects need their
own adapters. The earlier vacuum-shadow import experiments are historical
controls; zero-fluid snapshots are not hook-ownership evidence.

## Saves and acceptance

Standalone native saves and managed colony checkpoints use stable species IDs,
material-property compatibility checks, versions and corruption detection.
The in-game owner saves through paired native/physical checkpoints at one
worker barrier ([checkpoint format](../native/MAC_CHECKPOINT.md)). Colony-level
persistence must also save pipes, machine inventories, transport queues,
breathing settings, reaction state and conversion versions. A mixture colony
must never silently load with vanilla fluid simulation, and an old vanilla
colony must not be converted.

Release requires measured conservation, positive states, hydrostatic stability,
buoyant plumes, shock/rarefaction behavior, liquid settling and mixing,
multiphase transitions, blocked machine outputs, saved-state continuation,
all-material conversion audits and representative colony performance on both
platforms and all supported gameplay-pack configurations. A paused visual
fixture or standalone model suite does not satisfy the game integration gates;
see the [completion plan](completion-plan.md#release-acceptance).
