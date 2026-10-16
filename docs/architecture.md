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
breathing fraction is its mole fraction, not its
mass fraction. Each cell has an explicit geometric volume; the reference grid
uses 1 m × 1 m × 1 m cells by default.

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
internal energies. The current reference model uses constant heat capacities
and formation energies; it does not average temperatures arithmetically.
Inventories preserve trace quantities rather than deleting small species to
make room for a fixed number of constituents.

## Buoyancy, mixing, and liquids

Gravity acts on the bulk mixture. Pressure gradients and density differences
produce buoyancy; there must not be an arbitrary per-species upward/downward
swap rule. At equal temperature and pressure a hydrogen-rich parcel has lower
density than surrounding air. A warm air parcel can also rise. Gases diffuse
and remain mixed; ordinary room air must not rapidly sort into pure oxygen and
nitrogen layers. Hydrostatic equilibrium must remain stationary numerically.
The current diffusion model is mixture-averaged Fick diffusion, without
barodiffusion or thermal diffusion.

The reference gas solver conserves species, momentum fluxes, and total energy
including gravitational potential. It uses HLLC fluxes with an HLL fallback and
hydrostatic reconstruction. Explicit acoustic time steps make it unsuitable
for the intended colony scale. The production solver must treat pressure
implicitly while preserving compressibility, vacuum behavior, energy, and
positive constituent masses. Research precedent supports this direction but
does not provide a ready-made multiphase implementation:
[Tavelli and Dumbser, all-Mach staggered method](https://arxiv.org/abs/1612.08866)
and [Dumbser and Casulli, conservative semi-implicit finite volume method](https://iris.unitn.it/handle/11572/148704).

The sparse pressure component now solves a compressibility diagonal plus
pairwise face conductances using preconditioned conjugate gradients. It rejects
unanchored components and verifies the true equation residual before returning.
Manufactured-solution and barrier tests pass. It supports the pressure-work
components and separate integrated fluid prototype below; the public C ABI and
game runtime still advance the earlier gas reference solver.

A frozen-coefficient acoustic step now couples cell pressure to face velocity.
For each cell, its volume compliance is `C = V / (rho * c²)`; a zero value
represents the incompressible limit. Each oriented face carries an inertia
`M = rho_face * area * centre_distance`. Backward Euler solves
`C * delta_p + dt * sum(outward_area * u_new) = 0` together with
`M * (u_new - u_old) = dt * area * (p_left_new - p_right_new) + dt * M * acceleration`.
Eliminating velocity produces the existing symmetric pressure operator.
Pressure increments keep a balanced gravity/pressure gradient stationary.
Closed incompressible components use a gauge, with the original volume
continuity equations checked after solving.

Tests cover an analytic two-cell acoustic mode beyond the acoustic CFL limit,
hydrostatic balance, divergence removal with circulation retained, body-force
work, and disconnected compartments. The step records quadratic acoustic
energy, backward-Euler dissipation, and body-force work separately. This is a
linearized subproblem: its energy is **not** thermodynamic total energy, and
its stability does not establish nonlinear positivity, shock accuracy or
colony performance. It still needs conservative transport, nonlinear material
properties and pressure-work coupling before it can advance fluid inventories.

A separate nonlinear material-volume pressure step now supplies a compatible
pressure-work closure for constant-property mixtures. For a material cell,
the constituent masses stay fixed, the liquid volume stays fixed, and gas and
liquid share one temperature. Define `Q = Cv_total * T`,
`k = n_gas * R / Cv_total`, and gas headspace `Vg`. Reversible compression gives
`Q_new = Q_old * (Vg_old / Vg_new)^k`. The discrete work pressure is
`p_work = -(Q_new - Q_old) / (V_new - V_old)`, with its analytic limit at zero
volume change. A series expansion avoids cancellation for small changes.

Face velocities use an implicit midpoint pressure force. The same face areas
and midpoint velocities determine the change in material-cell volume. The
pressure work then cancels staggered kinetic-energy change face by face,
leaving only explicit body-force work. A damped nonlinear solve uses the
positive secant-pressure derivative to build the existing symmetric pressure
matrix. Completely filled incompressible cells retain their volume and solve
for pressure; closed liquid compartments use a pressure gauge. The solver
checks the volume residual, reconstructs the endpoint equation of state, and
checks total energy before publishing. Unsupported vacuum, partial liquid
fill without gas, fixed solids, liquid tension, inconsistent initial liquid
velocity and insufficient floating-point headspace precision reject the step.

Seven scenarios exercise nonlinear compression beyond an acoustic timestep,
the small-amplitude midpoint acoustic limit, hydrostatic gas/liquid balance,
force transmission through liquid, body-force work, reversible gas/liquid
compression and invalid inputs. This is a reversible **material-volume
substep**, not a complete moving-mesh or fixed-grid solver. Compatible mesh
geometry and staggered momentum remapping remain unimplemented. It has no
shock dissipation, vacuum or free-surface model, immiscible-phase separation,
or phase changes; a large timestep conserves energy but does not resolve fast
acoustic oscillations. Its equations are an explicit project derivation, not
a claim to reproduce a published solver. The need to check both energy and
entropy, rather than assuming one follows from an arbitrary energy update,
is also discussed in [De Michele and Coppola's energy-discretization study](https://arxiv.org/abs/2210.01251).

A shared donor-cell remap now transports gas and liquid inventories using
caller-supplied face volumes. Every constituent, both momentum components and
total energy use the same weights. The donor's total outgoing volume must fit
its cell; all retained and incoming species masses remain nonnegative without
clipping traces. Velocity mixing turns unresolved kinetic energy into internal
energy. Condensed overfill and unsupported solid transport reject the operation
before any input changes. A divergence-free volume-flux test preserves liquid
volume-fraction bounds and uniform temperature/pressure across gas/water cells.

A separate regular-MAC transport component retains canonical face-normal
momentum. It derives component dual mass fluxes from the same primal cell fluxes
and target inertia from the updated adjacent half masses, avoiding a separate
geometric mass rebalance. Positive upwind variance and fixed-normal wall
projection have separate heat and impulse accounts. Seven Windows/Linux groups
and an exact/high-precision saved-data checker pass, including 2-D shear and
passive refinement. This [component](../native/MAC_TRANSPORT.md) takes mass fluxes
from its caller; pressure, gravity, free surfaces and world dispatch remain
required before it can replace the prototype's reconstructed face momentum.

The separate closed MAC owner now composes this transport with EOS material
pressure work. It updates canonical face J directly, forms midpoint volume
sweeps, reconstructs shared constituent/U/passive parcels, uses actual parcel
masses for primal and dual fluxes, and checks return to fixed
cell volume and the two-stage U+face-K balance. It owns the EOS identity and
prepares whole intervals atomically with bounded retries. Eight Windows/Linux
groups and independent EOS/work/Fourier checks pass, including elastic liquids
and implicit acoustic refinement. This [path](../native/MAC_CLOSED_WORLD.md)
still requires broader nonlinear controls, free surfaces, phase topology and world/game dispatch;
it does not yet replace the prototype or meet production acceptance.
Its opt-in [gravity extension](../native/MAC_GRAVITY.md) reconciles body work
with actual face mass fluxes and reports the signed internal-energy adjustment
separately from mixing heat. Conservation, gas/liquid/layered hydrostatics and
joint stratified pressure/velocity/entropy refinement now pass on both platforms.
Shared material reconstruction repairs the initial spatial error; explicit
near-roundoff nested EOS roots resolve a later Linux hydrostatic stall.
The original scientific fixture and physical bounds remain unchanged, with
earlier first failures retained. Five reconstruction groups and separate
exact/high-precision parcel/EOS checks pass. These smooth occupied-grid controls
do not establish shocks, free surfaces or general nonlinear buoyancy.
The new [strong-gas Riemann gate](../native/MAC_RIEMANN.md) remains failed:
The exponent cohort completes Sod refinement on both platforms but fails
collision velocity refinement.
The prior derived K
failure is addressed by [wide energy accounts](../native/MAC_ENERGY_TRANSPORT.md),
which pass four new Windows/Linux controls and repeat every transport/closed/
gravity prerequisite. Full energy values and legacy projection masks remain
distinct. First failures and separate saved-data diagnostics are retained.
[Scaled primary parcels](../native/MAC_PRIMARY_PARCELS.md) pass five new groups
and retain helium/passive contributions through accumulation at the original
conservation bound. [Scaled momentum contributions](../native/MAC_MOMENTUM_PARCELS.md)
pass four more groups and reach primary storage. The subsequent
[full canonical/flux representation](../native/MAC_CANONICAL_RANGE.md) passes
five more, retaining full J, velocities, sweeps, bulk/dual fluxes and wall
reactions behind explicit double projections. Constituent/U cells remain
binary64, and full vertical-gravity work is explicitly unsupported.
[Bounded large integer exponents](../native/MAC_EXPONENT_RANGE.md) now remove
the tested signed-word range failure while retaining binary64 significands.
Six new groups, all 55 native prerequisite groups, separate saved-data reviews
and Linux ASan/UBSan checks pass. The later
[compatible compression stage](../native/MAC_COMPRESSION.md) now runs inside the
owning interval before each candidate commits. Positive heat and wall impulse
remain separate from transport mixing; all 65 native groups and saved reviews
pass on Windows/Linux. The latest Windows strong retry passes Sod and collision
velocity refinement but fails pressure refinement. Linux does not repeat that
known failure. Collision accuracy still requires work before production
ownership; no full strong-flow acceptance follows.

The separate native `FluidWorld` prototype now couples nonlinear material
pressure work, common-weight gas/liquid remap, momentum/energy accounting,
diffusion, viscosity and conduction. Candidate substeps publish atomically;
failures retry smaller timesteps, and exhausting the budget rolls back the
whole requested interval. Ten integration tests include hydrostatics, gas
displacing a full-liquid slug and 10,000-step multiphase conservation.
This prototype is separate from the C ABI, managed `FluidWorld` and ONI adapter;
those still use `GasWorld`. Its 100k-cell performance fails the production gate.
See [the implemented path and limits](../native/SOLVER.md).

Production liquid flow remains incomplete. The prototype rejects vacuum and
partially filled pure-liquid free surfaces, uses one velocity and temperature
for its phases, and lacks reconstructed interface geometry,
immiscible separation and grid-coupled phase transitions. It requires a
validated free-surface treatment and phase-specific inventories.
Miscible liquids share a composition; immiscible liquids retain separate phase
fractions and separate under gravity. Liquid volume cannot exceed cell volume
by silently deleting mass. Phase density, thermal expansion, surface behavior,
and real-fluid equations of state need an explicit supported property range.
Adding a falling-cell rule to the gas reference solver would not meet this
contract.

Current closed-vessel phase helpers balance mass and energy and use vapor
partial pressure. They reject unsupported temperatures and filled containers.
They do not yet couple boiling, freezing, melting, or condensation to grid
transport. Temperature-dependent properties and validated phase tables must
replace the reference approximations before broad-temperature gameplay.
[NIST Chemistry WebBook](https://www.nist.gov/programs-projects/nist-chemistry-webbook)
and [NIST-JANAF](https://www.nist.gov/srd/chemistry) are property-data sources;
individual properties must retain their source, units, and validity range.

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
oxygen, thin air, carbon dioxide, and other contaminants. Species-specific
hazard limits are currently placeholders requiring balance review.

World generation must guarantee enough starting mixed air, inert-gas reserves,
water, and early research materials. Required gameplay additions include:

- A mixture sensor and overlay showing pressure, partial pressures, temperature,
  constituent grams, and the reason air is unbreathable.
- Accessible carbon-dioxide scrubbing with captured waste and regeneration costs.
- Ventilation fans and pressure-aware vents, bulk mixture pumps, and proper
  selective separators with electrical/thermal costs.
- Nitrogen storage and generation sources, pressure regulation, and later air
  separation. Nitrogen must never be a starting-map lottery.
- Mixed-fluid pipes/reservoirs with constituent-preserving transport, explicit
  phase compatibility, backpressure, and overpressure behavior.

These are requirements, not currently registered ONI buildings.

## Read-only mixture presentation

`MixturePresentationModel` describes explicitly supplied immutable inventories;
`CaptureFrame` projects owned world cells and containers directly from one
completed colony snapshot, with optional additional same-revision display
samples. [Owned containers](container-inventories.md) use persisted full
allocation handles; stale, foreign, retired or replaced object bindings display
unavailable and cannot adopt an allocation by its string ID. Unknown catalogue
bindings and stale container revisions reject.
`MixturePresentationRuntime` publishes one immutable view per Unity frame and
pins it across synchronous texture/overlay worker jobs. Provider callbacks run
only on the presentation thread against pinned inputs, with reentrant
publication unable to mix revisions. No provider leaves native behavior intact;
an explicit provider failure displays unavailable instead of silently inferring
a mixture from a native dominant element or packet.
Real simulator/game teardown retires the model, providers, pinned frames,
texture schedules and prefab inspection bindings before native state is freed.
Texture recreation alone does not retire providers. Session generations prevent
an old worker lease from repinning a previous colony after restart.

The shared numerical view supplies gas mole fractions, partial/total pressure,
headspace density, temperature and independent breathing-failure flags. Names
identify the leading meaningful species, mixture/traces and breathing status;
selected headers stay on one line while their state/assessment remains visible
below. Compact hover retains harmful trace contaminants; full details retain
every positive constituent, with liquid rows excluded from gas percentages and
partial pressures. Owned pipe/reservoir/storage/bottle/suit/vent bindings are
explicit. Unregistered native inventories remain labelled single-material
packets, without inventing container pressure or hidden mixed composition.
Owned-container warnings and composition precede the native description while
preserving its content and native rows' relative order; unowned fallback leaves
the native ordering intact. Declaration tests exercise repeated cached refreshes
and native link-formatting rules; actual Unity layouts need separate evidence.

Normal rendering retains `TerrainBG`'s shipped gas shader, detail/flow inputs
and Gas/GasFront passes. Only the existing gas colour, visual-density and
danger property textures receive owned-cell overrides. Colours blend familiar
constituent sRGB palettes in linear RGB; a stable world-anchored 4-by-4-cell
pattern blends continuously with composition. Gas colour alpha is 255 and
non-gas alpha is zero. The legacy gas-pressure byte texture controls opacity
from gas density; actual pressure queries and overlays use double-precision Pa.
The danger channel and warnings use numerical assessment independently of tint
or texture visibility. Owned gas channels update each frame without vanilla
temporal interpolation, preserving native fog, lighting and visibility masks.

Four overlay modes expose composition, total pressure, oxygen partial pressure
and breathability with legends and native oxygen status-item visibility. The
native F1 legend limits mixture assessment to owned cells; unowned cells keep
native colours and native gameplay breathing. Appearance settings support colour-only mode
and persisted contrast, default 0.04; replacing visual options does not change
inventories or breathing policy. Weak inventory-key caches share immutable
analysis; names, rows and warning text materialize lazily. Worker sampling and
colour lookup allocate zero after publication. The newer packed immutable mass
map keeps the public dictionary contract while internal read-only spans avoid
boxed enumeration. Initialization and update costs are reported together with
capture so allocation reductions cannot hide work moved into inventory creation.
Its standalone measurements are separate from the frozen V10 Unity evidence.
V11 independently rechecks the packed Windows assembly's automated eighteen
captures, six selected-container assertions and shutdown cleanup. It does not
repeat V10's interactive menu/resume observations or establish Linux Unity.
Entirely changed and cold frame
capture costs, sustained publication backlog and retained game-state history
remain performance acceptance requirements.

Windows fixtures have produced actual normal-view/overlay screenshots and
registered-container resolver reports in a native-owned disposable colony.
They are explicitly test-owned inventories, not production fluid transactions.
The frozen v7 run also records actual trace hover, full-table scrolling through
all three constituents, live native warning-row ordering, all four overlay
icons/legends, the native F1 legend and persisted colour-only/contrast callbacks.
Windows v10 also spawns and selects six actual native objects (pipe, reservoir,
bottle, suit, vent and machine). Live label assertions preserve their native
descriptions while placing fixture warnings/composition above them. All six
panels and the full eighteen-capture fixture sequence pass. Their inventories
remain separate conserved read-only fixtures, so production fluid transactions,
native packet flow and equipped-suit behavior remain open. V10 additionally
passes native Save And Quit/main menu/same-save resume in one process, with
idempotent world/model/frame teardown after container adapters are removed.
Post-resume screenshots record no world provider and zero inventory/binding
counts; the composition legend explicitly reports no readings and native F1
returns to its original oxygen categories. The earlier v9 scoped teardown pass
and overall container-helper failure remain separate historical evidence.
Neither establishes full-owned colony persistence. Sustained publication and
long-run retained-memory acceptance remain open.
Linux managed/IL tests run against actual Linux game
declarations; actual Linux Unity presentation remains unverified. The full
policy, implementation boundaries and evidence are in
[mixture presentation](mixture-presentation.md).

## Machines, metabolism, and energy

Transfers are transactions. A full output must leave the input unchanged.
Bulk pumps move the source mixture proportionally; filters remove selected
species while retaining the remainder. The inventory transfer helper is a
conservative bookkeeping primitive. Separate reference process models now
quote gas compression using isentropic efficiency, incompressible liquid
pumping using hydraulic efficiency, and isothermal selective gas separation.
Streams carry enthalpy `H = U + pV`. Pump inefficiency heats their output.
Separation uses the ideal-gas demixing entropy to calculate minimum work;
actual work must be rejected as heat at the feed temperature. Tests check these
energy and entropy balances, including trace-species separation. The models
do not yet reserve electrical power, outlet capacity, or a heat sink in game.

Example native reactions include water electrolysis, hydrogen/methane/carbon
combustion, glucose-based metabolism, and photosynthesis. Every reaction is
checked for elemental balance, and chemical formation energy participates in
the energy ledger. Fuel generators consume oxidizer and produce accountable
exhaust. Duplicant metabolism consumes stored food as well as O₂ and produces
CO₂, water, and heat; breathing cannot turn O₂ into CO₂ without carbon input.
All other conversions, including fictional ONI materials, need documented
compositions or surrogate compositions and balanced routes.

The current reaction API enforces the first law and available reagents. It
does not yet enforce entropy production, equilibrium, reversible-work bounds,
or machine efficiencies. It therefore cannot certify the absence of every
thermodynamic exploit. Energy includes formation energy and may be negative;
positive temperature and full-system conservation are the relevant checks.

## Native ownership and compatibility

The selected production architecture is direct **in-memory native SimDLL
integration**, specified in [the native backend contract](native-backend-integration.md).
Hooks route fluid work to the custom engine within ONI's schedule while
retaining verified nonfluid services. Native scalar fluid cells are projections,
never the physical owner. Complete native reader/writer coverage, explicit
cross-domain transfers and completed-frame publication are required before
takeover. The initial forwarding observer does not grant fluid ownership.

ONI 744825's native simulator is a process-wide singleton. `Grid` contains
pointers into arrays returned through `Sim.GameDataUpdate`. Cell mutations,
component registration, consumers, emitters, temperatures, diseases, radiation,
callbacks, saves, and conduit thermal services cross separate native paths.
Most mutation calls bypass the managed `Sim.HandleMessage` wrapper.

The game exposes no fluids-only disable switch. Full ownership therefore needs
a complete audited replacement boundary or a proven separation of domains.
Skipping all native frames also stops its other services. Updating cells after
vanilla simulation cannot undo already emitted events. Calling native
initialization a second time inside the game does not create an isolated world.

`ProbeScope` still runs only an independent gas reference world within scoped
patches around its own calls. A live colony is never bound to that reference
world. A separate opt-in validation mode copies all native snapshot buffers and
returns byte-identical owned publications while native ONI keeps physics
ownership. Windows staging validates all generated world blobs read only, but
the current custom constant-density importer rejects real compressed/overfilled
liquid cells; it does not clamp mass or invent gas headspace to proceed.

Managed colony transactions provide accountable injection/extraction, selective
transfers, heat, geometry displacement and deterministic checkpoints. An
unhooked experimental router stages a small inspected subset of fluid messages
and their callbacks, rejecting unsupported cases before publication. The
native/custom save envelope is also a helper, not a live colony save adapter.
The full adapter must publish custom merged arrays, preserve all callback
contracts, handle construction/digging/displacement atomically, and route every
consumer/producer and transport system through the mixture backend.

The main-menu probe has passed inside Windows ONI, and the same six scoped
checks passed during Linux Unity startup. Linux exited before its main menu
with no local Steam session. A separate process harness
has passed narrow import/solid-heat checks using both Windows and Linux native
libraries. Its vacuum-shadow method is historical research, superseded as the
production design by native hooks. Preserve its results and negative controls
without treating zero-fluid snapshots as hook-ownership evidence. See the
[inspected interface and historical experiments](interop-contract.md).
Current Linux main-menu/presentation setup evidence is recorded in
[the headless runner documentation](../tools/ONI_HEADLESS.md); these fixtures
and scoped probes do not establish production native ownership.

A constant-capacity thermal-network component now provides backward-Euler heat
conduction with pairwise conservative energy transfers. Conductance uses the
sum of the two half-cell thermal resistances. Vacuum has no artificial heat
capacity or conductance. The solve uses temperatures relative to each connected
component's equilibrium to avoid cancellation in stiff contacts, then verifies
the maximum principle and closed-system entropy. The game adapter must still
reserve and commit both endpoints and supply phase changes, geometry, radiation
and appropriate convective boundary conditions. These standalone results do
not themselves restore native fluid-solid or building heat exchange.

Dominant-element projections may serve vanilla rendering, but must never
become the physics source of truth. Unsupported game/mod services must fail
before a new colony starts, with a clear compatibility message. Gas colours,
inspection and overlays now have explicit read-only mixture policies. Sound,
gameplay disease/radiation/thermal behavior, material-specific effects and
production fluid transactions still require their own adapters.

## Saves and acceptance

Standalone native saves and managed colony checkpoints use stable species IDs,
material-property compatibility checks, versions and corruption detection.
The new save-envelope helper stores native and custom payloads together with
build/catalogue/frame metadata and a checksum; it rejects incompatible or
incomplete input before publication. Actual colony manual/autosave/restart and
cross-platform continuation are not connected to it. Colony-level persistence
must also save pipes, machine inventories, transport queues, breathing settings, reaction state,
and conversion-version information. It must not silently load a mixture colony
with vanilla fluid simulation or permit an old vanilla colony to be converted.

The first playable gate remains open: complete real new-colony imports and
single fluid ownership; support the missing production phase/interface physics
within budget; connect breathing, life-support machines, nitrogen/starter
resources, native solid heat exchange and construction; integrate coherent
save/reload; and run ten cycles with conservation diagnostics. Presentation
also needs final Windows and Linux Unity acceptance, sustained 100k-cell
publication/game timing, teardown and long-run memory evidence. A successful
paused visual fixture or standalone model suite does not satisfy those gates.

Release requires measured conservation, positive states, hydrostatic stability,
buoyant plumes, shock/rarefaction behavior, liquid settling and mixing,
multiphase transitions, blocked machine outputs, saved-state continuation,
all-material conversion audits, and representative colony performance on both
platforms and all supported gameplay-pack configurations. Standalone tests alone do not pass the
game integration gate.
