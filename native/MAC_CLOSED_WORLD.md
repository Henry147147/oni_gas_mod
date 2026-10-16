# Closed MAC pressure and transport

This closed, occupied regular-grid owner composes the existing reversible EOS
pressure-work solver with retained
MAC face momentum and compatible primal/dual transport. It remains excluded
from default builds, the C ABI and game dispatch.

Each material pressure cell starts at nominal grid volume. EOS work pressures
update canonical normal face J directly. The midpoint of represented face
velocities defines a swept volume; outgoing material density converts it to the
mass flux passed to MAC transport. The same sweeps must return each moving
material volume to nominal grid volume within 128 binary64 epsilons of actual
local operands. Each stage checks actual U plus resolved face K; the composed
energy bound is the sum of the two stage bounds. No scalar total energy is used
to reconstruct U or J. Wall pressure impulse, transport projection heat and
mixing heat have separate accounts. Stored U/J rounding remains explicit.

The EOS-aware transport overload evaluates source inventories at material
volumes and targets at nominal volume. It does not apply legacy caloric
temperature or constant-density liquid tests. Empty fluid cells and internal
liquid voids reject. The pressure helper's original 5e-13 root tolerance stays
the default; this caller requests 16 epsilons. Only stricter tolerances down to
8 epsilons are admitted by the added optional accuracy argument.

The owner copies its registry, state and EOS, including when the caller holds a
mutable model alias. A requested interval prepares a new owner. Domain/CFL
rejections halve the attempted step within a shared attempt budget; a later
failure returns no partial state, clock or revision. Fatal topology/precision
errors remain fatal. The completed interval increments the owner revision once.

Eight passing control groups cover pressure-driven gas flow, an elastic gas/liquid solid
corner, constant-p/T variable-density contacts, a compressed-liquid identity
and mutable-model alias, adaptive CFL retry and late rollback, joint N16/32/64
pressure/velocity acoustic refinement at acoustic Courant 2, transpose symmetry,
ten invalid domains and three accuracy bounds. The joint refinement ratio is
predeclared below 0.7. A separate saved-data reviewer reconstructs Fraction
transport balances and 80-digit EOS, isentropic work and acoustic Fourier modes.

## Current-source evidence

Fresh Windows MSVC and WSL Linux GCC15 Release builds pass all eight groups.
The actual compiler settings and executable/archive identities were audited
before runtime. All seven current transport groups, 57 core groups and 24
occupied-fluid groups also pass on each platform. The transport regression now
requires a zero-flux operation to preserve every stored value bit for bit.

The data-only reviewer passes 4,873 saved-operand/profile checks per platform,
including both exported pressure/transport cases. It reconstructs actual U+K,
wall impulse, donor-density fluxes and target volume, independently solves the
elastic-mixture EOS and isentrope, and checks entropy against the stored-U
roundoff allowance. Work/EOS comparisons use the predeclared 5e-10 relative
bound. Acoustic profiles match an independent midpoint Fourier recurrence
within 5e-5 of normalized wave amplitude; row/report error agreement uses 2e-7.
Three deliberate work-pressure/trace/profile corruptions reject. The separate
transport reviewer also passes 3,884 checks and rejects three corruptions.
These are independent arithmetic checks in separate code, not an independent
agent review or an entropy proof for arbitrary flows.

The small travelling ideal-gas wave uses pressure 100,000 Pa, perturbation
0.1 Pa, sound speed 352.9876569479 m/s, a unit periodic square, and elapsed time
`0.25/c`. Each nominal timestep has acoustic Courant 2. The predeclared joint
refinement bound is 0.7:

| Grid | Normalized pressure L1 | Normalized velocity L1 | Completed steps |
| --- | --- | --- | --- |
| 16 × 16 | 0.0526169287 | 0.0520308622 | 2 |
| 32 × 32 | 0.0141066645 | 0.0140540653 | 4 |
| 64 × 64 | 0.0035920052 | 0.0035881773 | 9 |

There are no rejected acoustic attempts. The finest run includes a final
represented-clock remainder step. Successive pressure ratios are 0.26810 and
0.25463; velocity ratios are 0.27011 and 0.25531. Windows/Linux normalized errors
differ by at most 8.25e-11. This is a small smooth wave check; it does not prove
shock, general all-Mach, discontinuous-interface or nonlinear refinement.

The independent interval control takes three attempts: one material-volume CFL
rejection and two accepted steps. A separate late-budget failure follows two
completed candidates and leaves the original owner, clock and revision intact.
The 2.2e8-Pa compressed-liquid control has nominal-density volume above one cell
and a legacy caloric temperature differing by more than 1 K. It retains its
actual EOS state exactly without a force or flux, and a changed caller-owned
model alias cannot alter the owner's thermodynamic identity.

The gas pressure-driven case has represented composed energy residual
`8.64e-12 J` within `2.2891e-8 J` on Windows. The wet-corner case has residual
`1.2071e-7 J` within `0.004091 J`. The latter bound includes large stored formation
energies; it is not a statement that every pressure-work increment or heat
addition is stored exactly. Independent work/entropy checks supplement it.

Private evidence is `local/occupied-staggered-validation/mac-closed-{windows,linux}-r1`.
Completed-review SHA256 values are
`894ee378f4a483df661ea6110274a73c53bb17c3a88fea029a79ba0ee4f9ed4b`
(Windows) and
`400c68e0f2c6039576855c36b58179fc945211a2caf5d114fc66c112502c7a0a`
(Linux). Source freezes, EOF-checked raw pipes, compiler approvals and first
results are retained. Bounds were fixed before launching: aggregate build
180 seconds, new native/review 60 seconds each, each regression/review 30 seconds,
and 128 KiB per raw pipe. Runtime durations are not performance acceptance.
The default game DLL remains unchanged; no Unity or previous failed physical
gate was run for this change.

The opt-in [gravity extension](MAC_GRAVITY.md) now closes finite work/potential
accounts and retains hydrostatic rest in its initial controls, but its joint
stratified-wave refinement gate fails on Windows/Linux. Zero gravity remains
the default and the accepted scope above. Spatial reconstruction is required
before retrying that gate; no gravity or production acceptance follows.

Validated gravity, viscosity, diffusion, conduction, shocks, vacuum/free surfaces,
immiscible interfaces, phase transitions, persistence, colony ownership and
production performance remain required. The four-species test profile uses a
zero-expansion elastic water surrogate; it is not a calibrated material catalog.
Earlier remap, vacuum and performance failures remain separate and unchanged.
