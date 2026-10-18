# Pressure work, gravity and compression regularization

This covers the mechanical stages of a [`MacClosedWorld`](MAC_SOLVER.md) substep: the
implicit EOS pressure step with optional gravity, and the compression regularization
that damps shocks. The pressure step produces the swept volumes that
[transport](MAC_TRANSPORT.md) remaps.

Code: `advance_mac_closed_substep` in `mac_closed_world.cpp`,
`advance_occupied_material_pressure` in `volume_work.hpp`, `mac_compression.hpp`/`.cpp`,
`mac_compressive_work.hpp`, `mac_compressive_work.cpp` and `mac_compressive_pressure.cpp`.

## Pressure and transport substep

Each occupied material cell starts at nominal grid volume. The reversible EOS
pressure-work solver (`advance_occupied_material_pressure`) moves the face nodes: its
work pressures update the canonical face J directly. The midpoint of the represented
face velocities defines each face's swept volume. Material reconstruction samples
the constituent masses, actual U and passives in each swept parcel, and the parcels'
actual mass defines the flux handed to MAC transport. The same sweeps must return
every moving material volume to the nominal grid volume within 128 epsilons of the
actual local operands.

Each stage checks actual U plus resolved face K; the composed energy bound is the sum
of the stage bounds. Wall pressure impulse, transport projection heat and mixing heat
have separate accounts, and stored U/J rounding is explicit. No scalar total energy is
used to rebuild U or J.

Root tolerances: the outer material-volume solve uses 16 binary64 epsilons (the
helper's own default is 5e-13; stricter targets down to 8 epsilons are admitted). The
nested isentropic inversion is asked for ε/4 relative pressure accuracy, against the
helper default of 2e-13, so inner work-pressure noise cannot stall the
near-roundoff outer solve. This tightens the inner root without changing any outer
geometry or energy bound. With the ordinary inner tolerance a Linux hydrostatic
control left a volume residual of 6.4e-14 against the 3.6e-15 target.

Wall reactions accumulate signed boundary pressures before multiplying by the shared
timestep and area, which avoids cancelling separately rounded large wall products.
Range handling of face velocities and sweeps is described in
[range arithmetic](MAC_RANGE.md#canonical-momentum-and-fluxes).

The [pressure-increment restart](MAC_PRESSURE_INCREMENTS.md) additionally repairs
absolute-pressure cancellation when an ordinary occupied root stalls. It retains
the original closure/account bounds and publishes the same compensated node
differences to canonical momentum. Default timesteps and selectors are unchanged;
the larger-step wave accuracy gate still fails.

## Gravity

Constant downward gravity (`gravity_m_per_s2`, default 0) applies a body force to the
same face inertia as the pressure force. Body work on a vertical face is
`W_f = −dt g I_f u_mid` with the represented midpoint velocity. Transport moves
potential energy across that face as `D_f = F_f g h` with the actual constituent mass
flux `F_f`. The difference `−(W_f + D_f)` is a signed internal-energy adjustment,
shared between the adjacent target cells by half-mass, and reported separately from
boundary, mixing and wall heat. U and J stay primary.

Pressure U+K minus body work, transport U+K, the stored internal adjustment and the
potential transport each have their own 128-epsilon account; their bounds sum to the
final U+K+PE allowance. Unstored signed U changes are counted. Entropy change is
reported, not clamped. Body impulse, body work, potential flux, signed internal work
and stage bounds retain their full significand/exponent operands, including when
their diagnostic projections are zero. Adjacent-cell work is accumulated in full
range before one primary-U projection; unresolved U updates are counted. These
rounding remainders are not yet persisted. Nonzero gravity rejects vertical
periodicity. The relation
between gravity work and face mass flux follows
[Springel (2010), sections 5.2–5.4](https://academic.oup.com/mnras/article/401/2/791/1147356);
the split and face distribution here are this implementation's choices.

## Compression regularization

`compression_viscosity` selects a numerical shock regularization, separate from
physical viscosity:

| Value | Name | Placement | Law |
| --- | --- | --- | --- |
| 0 | `none` | — | — |
| 1 | `quadratic` | Split half steps around pressure/transport | `q = ρ v²` |
| 2 | `acoustic_quadratic` (default) | Split half steps | `q = ρ (v² + c v)`, pressure-sensed |
| 3 | `coupled_pressure` | Inside the material pressure root | Same stress, coupled to volume and U |

Here `v` is the positive closing speed `u_left − u_right` across a cell's two normal
faces. Expansion, uniform normal velocity and tangential shear produce no force.

The split modes run compression(dt/2), pressure/transport(dt), compression(dt/2),
following [Strang](https://epubs.siam.org/doi/10.1137/0705041); both halves, their heat
and their wall impulse commit only with the accepted substep. A manufactured forced
pair verifies the ordering: inertias 2/4 kg, initial J 6/−8 kg m/s, opposing force
4 N, coefficient b = ρ·area = 0.4 and duration 0.5 s, with closing velocity
`c' = a F − a b c²`, `a = 1/I_left + 1/I_right`. On N8/16/32 time intervals each
successive error must be below 0.30 of its predecessor and N32 below 1e-4 and one
tenth of the after-only error. Measured errors are 1.8988874e-4, 4.7494979e-5 and
1.1875170e-5 (ratios 0.250); the after-only N32 error is 0.003367796. This is temporal
evidence for the pair only, not second-order accuracy of the full solver.

### Quadratic pair relaxation (mode 1)

For integrated coefficient `B = dt · area · ρ = dt · cell_mass / spacing` and
`a = 1/I_left + 1/I_right` (omitting a fixed endpoint), the pair ODE
`dc/dt = −(area ρ) a c²` has the exact relaxation `c_new = c / (1 + B a c)`. The
equal and opposite impulse is `d = B c² / (1 + B a c)` and the kinetic loss
`Q = d (c − a d/2) ≥ 0` is computed from velocity differences, never by subtracting
large kinetic energies. The impulse is kept even when the denominator rounds to one.
Fixed endpoints keep zero J and report their impulse on the fluid separately.

Forward and reverse half sweeps compose the pair solves symmetrically; the x and y
operators act on different momentum fields. Every positive Q heats its cell at fixed
masses and volume; no trace, passive or mass moves. Each pair checks represented J/K
and wall/heat accounts at 128 epsilons of its operands; full-range values are kept
behind explicit projections. The general approach is discussed by
[De Vuyst](https://arxiv.org/html/1703.00235); the pair ODE and its split use are
this implementation's derivation.

### Pressure-sensed acoustic compression (mode 2)

For positive closing speed v, `q = ρ (v² + c v)` with `c = (c_s / 2) ψ`. The sound
speed comes from the occupied isentropic mixed-EOS tangent, `c_s² = V² / (mass C)`
with `C = (−dV/dp)_S = C_liquid + Cv/(Cv + nR) · V_gas/p`; it reduces to the ideal-gas
and elastic-liquid speeds. The coefficient 0.5 is the linearized symmetric Riemann
pressure increment `ρ c_s (u_left − u_right)/2` for equal impedance; linear-plus-quadratic
viscosity is described in the
[Sandia ALEGRA report, section 7.2](https://www.osti.gov/servlets/purl/934850) and the
same 0.5 appears in [LULESH](https://github.com/LLNL/LULESH/blob/master/lulesh-init.cc).
The quadratic coefficient stays one grid spacing. No coefficient was fitted to a
benchmark.

The sensor for each cell and axis is

`ν_i = |p_left − 2 p_i + p_right| / (p_left + 2 p_i + p_right)`,

with zero for a zero denominator, and `ψ_i = max(ν_left, ν_i, ν_right)`. Boundaries
and solids supply the cell's own pressure, so the stencil never crosses a solid. ψ is
in [0, 1], zero for uniform or affine pressure, and small in smooth weak flow, so the
linear term adds no first-order damping to a weak acoustic wave. This follows
[Swanson and Turkel, section II, equations 9–10](https://ntrs.nasa.gov/api/citations/19870012739/downloads/19870012739.pdf);
the staggered stencil and pair relaxation are this implementation's adaptation. An
unsensed half-sound-speed term damps the wave itself: it failed the pointwise
Fourier check by up to 0.114 of amplitude at N16 against 5e-5. The unsensed law
remains only as component law `MacCompressionLaw::acoustic_quadratic`; owner mode 2
always uses the sensed law.

With `L = B a c` and `φ(L) = (1 − exp(−L))/L` (φ(0) = 1), the frozen pair ODE
`dv/dt = −(area ρ) a v (v + c)` gives impulse

`d = B v (v + c) φ(L) / (1 + B a v φ(L))`

and the same compatible loss `Q = d (v − a d/2) ≥ 0`. For L < 2^-53 φ = 1; for
L ≥ 64 φ = 1/L; otherwise `expm1` is used. All sums, ratios and products use
full-range arithmetic, so a large pressure is never projected and a curvature below
binary64 range stays positive. With c = 0 the quadratic arithmetic is reproduced
exactly. Sound speeds, pressures and sensors are frozen from each call's incoming
state, a first-order treatment of their thermal dependence.

### Coupled compressive pressure work (mode 3)

Mode 3 moves the compressive stress into the material pressure step instead of
splitting it, so the same force changes momentum and internal energy, as in compatible
staggered methods ([Caramana et al. (1998)](https://www.sciencedirect.com/science/article/abs/pii/S0021999198960296)).

For source mass M, volume V0, spacing h and timestep dt, with `s = max(−dV/V0, 0)`,
`a = h/dt` and `ρ = M/V0`, a frozen nonnegative linear speed L gives
`q = ρ (L a s + a² s²)` (quadratic coefficient one). The response adds
`Q = −q dV` to stored U and q to the isentropic work pressure, so
`dU = −(p_isentropic_work + q) dV` within the stored-operand allowance. Q is
nonnegative and zero in expansion. At fixed V0, h and dt,
`dq/ddV = −ρ (L a + 2 a² s) / V0` on the compressive branch; the zero-step tangent is
the compressive one-sided tangent. The returned EOS state reads the actual stored U
and endpoint volume; constituents, including traces, are unchanged; reference energies
may be negative. Invalid EOS states, internal liquid voids or invalid inputs return no
candidate.

`advance_mac_compressive_pressure` uses the same total stress in the nonlinear
volume root, the nodal force and stored U, with one frozen linear speed per cell.
Rejected Newton trials are not published. In the owner, mode 3 skips both split
compression halves. It freezes the incoming half-sound speed and pressure sensors once
per candidate and uses the larger directional value for one isotropic bulk stress.
The accepted work pressure supplies the face impulse and midpoint sweep; heat is
counted once. Incoming velocity projections retain their full canonical correction,
and full midpoint sweeps feed material transport. Each cell also checks its actual
stored-U change against `-p_work * sum(full midpoint sweeps)`, at the original
128-epsilon operand bound. This supplements the constitutive root's binary64 volume
parameter and the existing full geometry, momentum and total-energy checks. A range
projection alone no longer rejects a coupled candidate; a failed actual-work check
still rejects atomically.

The substep exposes `full_compressive_volume_changes_m3`,
`full_compressive_internal_work_j`, and their full residual/bound. Changes not resolved
by stored U increment `unresolved_compressive_work_updates` in both substep and owner
reports. These are diagnostic accounts, not additional heat or another energy-budget
allowance. Primary-U/volume rounding remainders remain unpersisted; this does not
complete the production conservation contract.

The constitutive callback reuses the pressure root's immutable source anchor and
completed isentropic response/tangent. It adds the same compressive stress, actual
stored-U update and admission checks without solving the isentrope again. The public
standalone response entry constructs its own validated anchor and uses the combined
isentropic work/tangent query. Publication supplies the completed query too; no
borrowed reference survives the callback. This changes evaluation cost, with the
same physical coefficients, root options, source lifetime and saved settings.

`ma_mac_compressive_eos_reuse_tests` compares 56 callback queries against freshly
anchored responses through nonlinear gas, wet-mixture and liquid solves. The accepted
pressure/velocity/volume/U arrays, iteration path and source inventories remain exact.

Entropy admission measures constant-composition relative entropy: pure gas uses
actual conserved caloric increments and cell volumes; mixed and liquid states use
stable log ratios of the EOS temperature and gas headspace. Subtracting rounded
absolute entropies is wrong when U crosses the arbitrary caloric reference. In the
case that exposed this, U went from 0.2881186565689019 to −0.23594879742432628 J with
about 52.27 J of thermal energy; the absolute difference gave −4.16e-17 J/K against an
allowance of 3.57e-17 J/K, while the true change is −2.54e-18 J/K.

## Verification

**Closed pressure/transport (`ma_mac_closed_world_tests`, eight groups).**
Pressure-driven gas flow, an elastic gas/liquid solid corner, constant-p/T
variable-density contacts, a 2.2e8 Pa compressed-liquid identity (no force or flux,
and a changed caller model alias cannot alter the owner), adaptive CFL retry and late
rollback, acoustic refinement, transpose symmetry, ten invalid domains, three
accuracy bounds and three invalid nested-EOS options. The acoustic control is a
travelling ideal-gas wave at 100000 Pa with a 0.1 Pa perturbation,
c = 352.9876569479 m/s, on a periodic unit square, elapsed time 0.25/c and acoustic
Courant 2. Errors as last recorded (with limited linear reconstruction, before the
pressure-sensed compression default):

| Grid | Normalized pressure L1 | Normalized velocity L1 |
| --- | --- | --- |
| 16 × 16 | 0.05262 | 0.05203 |
| 32 × 32 | 0.01411 | 0.01405 |
| 64 × 64 | 0.003592 | 0.003588 |

The joint ratio bound is 0.7 (measured about 0.27 and 0.25). The reviewer also
requires the profiles to match an independent midpoint Fourier recurrence within
5e-5 of the wave amplitude; the current owner is within 6.8e-7. This is a small
smooth wave, not shock or nonlinear refinement.

**Gravity (`ma_mac_gravity_tests`, four groups).** Two exported gas and wet-corner
cases; gas, liquid and layered hydrostatic rest with maximum speed below 1e-7 m/s
(measured below 3e-11); a stratified vertical acoustic mode; invalid gravity/topology
and whole-interval rollback. The mode uses isothermal scale height one,
constant-capacity nitrogen with a helium trace, rigid ends and 0.05 m/s initial
velocity. Its linear displacement is proportional to
`exp(y/2) sin(π y) sin(ω t)` with `ω = c sqrt(π² + 1/4)`. At a quarter period,
cell-average pressure and face velocity are compared on N32/64/128 with N/4 nominal
steps and no retries. Joint pressure/velocity ratios must be below 0.7,
specific-entropy-change ratios below 0.8, finest normalized errors below 0.02 and
specific entropy change below 1e-6 J/(kg K):

| Grid | Pressure L1 | Velocity L1 | Specific entropy change (J/(kg K)) |
| --- | --- | --- | --- |
| 32 | 0.0002468565114 | 0.004793925538 | 8.484297298e-7 |
| 64 | 0.00007272638404 | 0.001201710434 | 2.121502573e-7 |
| 128 | 0.00004637729934 | 0.0002964012499 | 5.304184976e-8 |

With donor-cell material transport this gate failed (velocity ratio 1.11457):
spatial and temporal errors cancelled partly on the coarse grid. Limited linear
reconstruction fixed it. `native/tests/analyze_mac_gravity_linearization.py` repeated
the tridiagonal linearized-column calculation used for that diagnosis. It reads only a
capture that stopped after N32/N64, like the original failed run, and runs no native
code.

**Range-extended gravity (`ma_mac_gravity_range_tests`, five groups).** Full incoming
vertical momentum with ordinary gravity; body impulse, work and actual-mass potential
flux below binary64 range; minimum-subnormal gravity with below-range velocity and
volume sweeps; source/endpoint checkpoint continuation and late-budget rollback;
10,000 closed steps with unchanged primary inventories and retained accumulated
canonical impulse. The saved reader reconstructs each face's body work and potential
flux and every target-cell work share using exact fractions. Removing a tiny work,
flux, cell update or owner impulse, or hiding the unresolved-U count, fails review.
This is a range/accounting control, not a strong-flow or general endurance gate.

**Compression.** `ma_mac_compression_tests` (six groups) covers analytic free/free and
fixed/free pairs, stiff relaxation and semigroup checks, below-binary64 J and heat,
constant velocity/expansion/shear/Galilean shift, rigid-wall and periodic gas/liquid
grids and invalid inputs. `ma_mac_compression_owner_tests` covers the owner with both
halves, a mixed-liquid solid corner, full-range heat, the disabled reference,
rollback and the manufactured pair. `ma_mac_acoustic_compression_tests` (seven
groups) adds acoustic pair, range, inactive, grid and transpose controls; gas, wet and
liquid sound speeds agree with isentropic finite differences to 6.68e-11, 8.20e-11
and 5.05e-11, below the 1e-5 limit. `ma_mac_pressure_sensor_tests` (three groups)
covers scalar triples including exact cancellation below binary64 range, periodic and
wet-corner pressure jumps, transposition, quadratic agreement at uniform pressure, a
new below-range face J, and invalid pressures.

**Coupled compressive work.** `ma_mac_compressive_work_tests` covers the local
response for dry gas, wet gas/liquid, compressed water, expansion and zero change,
negative reference U, subrange heat, geometric scaling, monotone stress/tangent,
reference-crossing admission, identical-state zero entropy and real cooling rejection.
`ma_mac_compressive_pressure_tests` (six groups) covers dry reflection, wet pressure,
filled water, gravity/body work, uniform periodic translation and early/late
rejection. `ma_mac_compressive_owner_tests` (seven groups) covers dry 2-D flow, wet
gravity with walls, filled water, uniform translation, unsplit clock and heat
accounting, 24 checkpoint-setting continuations and invalid-configuration,
stale-projection and late rejection.

`ma_mac_compressive_range_tests` adds eight captured gas/wet/liquid controls with
both motion axes/signs, below-range and rounded-subnormal velocities, actual full
midpoint work, positive constituent/passive traces and source/endpoint checkpoint
continuation. A separate active-pressure regression combines a projected incoming
velocity with nonzero compressive heating. A zero-U control whose actual work cannot
be stored still rejects, and the shared-budget owner rolls back. The saved reader
reconstructs actual sweeps and cell work with exact fractions and the EOS at 100
digits, and rejects seven corruptions. Its pure-liquid oracle is checked against
analytic cold-curve pressure and elastic-work differences in compression/expansion;
internal void still rejects.

Strong-shock behaviour of each mode is in [strong flow](MAC_STRONG_FLOW.md#results-by-method):
mode 3 is the only mode that passes the collision refinement gate.

## Reproduce

```sh
cmake --build build --config Release --target ma_mac_closed_world_tests ma_mac_gravity_tests ma_mac_gravity_range_tests \
  ma_mac_compression_tests ma_mac_compression_owner_tests ma_mac_acoustic_compression_tests \
  ma_mac_pressure_sensor_tests ma_mac_compressive_work_tests ma_mac_compressive_pressure_tests \
  ma_mac_compressive_owner_tests
build/ma_mac_closed_world_tests > closed.stdout
python3 native/tests/review_mac_closed_world.py closed.stdout
build/ma_mac_gravity_tests > gravity.stdout
python3 native/tests/review_mac_gravity.py gravity.stdout
build/ma_mac_compressive_owner_tests > compressive-owner.stdout
python3 native/tests/review_mac_compressive_owner.py compressive-owner.stdout compressive-owner.json
```

The other targets pair with `review_mac_compression.py`,
`review_mac_compression_owner.py`, `review_mac_acoustic_compression.py`,
`review_mac_pressure_sensor.py`, `review_mac_compressive_work.py` and
`review_mac_compressive_pressure.py` (the last two require a new report path).
`review_mac_closed_world.py` reconstructs exact-fraction transport balances and
recomputes equilibrium, isentropic work and the acoustic Fourier modes at 80 digits.
`review_mac_gravity.py` reconstructs mass, impulse, body work, the gravity adjustment
and U+K+PE accounts, solves the EOS and isentropes at 80 digits and integrates the
continuous stratified mode. The compression reviewers solve each pair ODE
independently, replay every event in order and reconstruct the mixed-EOS sound speed,
sensors, U+K, entropy and wall momentum.
