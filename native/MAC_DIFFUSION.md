# Conservative gas diffusion

`MacClosedWorld` can exchange constituents across open gas faces after bulk
pressure flow has settled. The implementation is in `mac_diffusion.cpp` and
`mac_diffusion_normal.cpp`. The driving force is the Maxwell–Stefan force with
pressure diffusion. Thermal (Soret) diffusion, liquid diffusion and dissolution
are outside this implementation's validated scope. Resolved circulation and
buoyancy are still needed for fast room mixing.

The current discrete scheme is **formulation 2**. Formulation 1, the first
merge candidate, used a mole-fraction gradient alone. It rejected empty cells
and states outside the correlations' ranges, and it could decrease entropy
when a pressure step opposed a small composition gradient. It was never
released. Its checkpoints reject (see [persistence](#modes-configuration-and-persistence)).

## Equations

Each face uses the union of gases present on either side, including species
absent on one side and every positive trace. In the continuum the flux of
species k, relative to the mass-average velocity, is the corrected
mixture-averaged (Hirschfelder–Curtiss) flux:

```
D'_k = sum(j != k, Y_j) / sum(j != k, X_j / D_kj)
d_k  = grad(X_k) + (X_k - Y_k) grad(ln p)
J*_k = -rho (W_k / W_mean) D'_k d_k
J_k  = J*_k - Y_k sum(J*_j)
```

`d_k` is the ideal-gas Maxwell–Stefan driving force with the
pressure-diffusion term. A uniform body force such as gravity cancels from it,
so heavier gases settle only at their barometric equilibrium. The scale height
is kilometres, so this is negligible on room scales. At uniform pressure the
term vanishes, leaving the familiar mole-gradient form of the
[Cantera governing equations](https://cantera.org/3.2/reference/onedim/governing-equations.html#diffusive-fluxes).
The fluxes sum to zero mass, so diffusion never moves bulk mass. Each cell's
total mass is unchanged, and pressure flow is left to the flow solver.

This flux equals `J = -P A Pᵀ ψ`, with the following terms:

- `ψ_k` is the species' mass-specific chemical-potential gradient over T.
- `A = diag(ρ_k W_k D'_k / R)`.
- `P = I − Y 1ᵀ`.

The matrix is symmetric positive semidefinite, so the entropy production
`ψᵀ P A Pᵀ ψ` is non-negative. The mole-gradient force alone lacks this form.
With a pressure gradient it can drive species against their chemical
potential, which is the formulation-1 fault.

## Discrete face flux

The discretization keeps that structure with each face's **exact** discrete
force, so the first-order entropy production is non-negative by construction.
The construction for one face between cells L and R is:

1. **Exact force.** It uses the partial pressures `p_k = n_k R T / V`, the
   arithmetic face temperature `T_f`, and the enthalpy flux carried at
   `h_k(T_f)`:
   `Φ_k = ln(p_k^L / p_k^R) + (c_p,k / R) [T_f (1/T_R − 1/T_L) + ln(T_R / T_L)]`
   (molar `c_p`). Then `R Σ δn_k Φ_k` is exactly the first-order change of the
   stored ideal-gas entropy for the transfer `δn` with its enthalpy. The
   temperature term is third order in `T_R − T_L` and is zero at equal
   temperatures.
2. **Log-mean mobility.** Each two-sided species uses the logarithmic-mean
   partial pressure `Λ_k = (p_k^L − p_k^R) / ln(p_k^L / p_k^R)`. Its mass
   mobility is `a_k = W_k² D'_k Λ_k (A/Δx) / (R² T_f)`, so
   `a_k ψ_k = W_k D'_k (A/Δx) F_k / (R T_f)` with the finite potential
   `F_k = Δp_k + Λ_k G_k`. `log1p` keeps the near-equal case accurate.
3. **Projection.** The weights are `w_k = W_k Λ_k / Σ W_j Λ_j`, the log-mean
   partial densities, which tend to `Y_k`. The flux is
   `J = P_w A P_wᵀ ψ` with `P_w = I − w 1ᵀ`. In terms of F, the raw flux is
   `J*_k = (F_k − w_k Σ F_j) W_k D'_k (A/Δx) / (R T_f)` and the corrected flux
   is `J_k = J*_k − w_k Σ J*_j`. This matrix is symmetric positive
   semidefinite in the exact force and has zero net mass for any state. Its
   continuum limit is the corrected flux driven by `d_k` above.
4. **One-sided species.** A species present on only one side has an
   infinite exact force and zero log-mean, weight and correction. It keeps its
   finite downhill Fick flux `W_k D'_k (A/Δx) Δp_k / (R T_f)`, so it never
   leaves a cell where it is absent. Its first-order entropy production is
   unbounded and positive.
5. **Disjoint inventories.** When no species is present on both sides (for
   example pure O₂ beside pure CO₂), the downhill Fick fluxes of each side's
   species are scaled to the corrected group totals `Y_R S_L + Y_L S_R`, where
   `S` is each side's raw total and `Y` the face mass fraction of that side's
   species. This gives zero net mass with no sign reversal and reproduces the
   corrected binary flux for pure A beside pure B.

The largest-weight species' flux is evaluated as the negative sum of the
others, imposing zero net mass algebraically. No inventory is clipped or
normalized. Positivity, admissible thermodynamic states and the stored-state
entropy of every trial are checked, and failed trials backtrack.

One face flux gives equal and opposite constituent transfers. The energy flux
is `sum(J_k h_k(T_f))`, which changes stored U consistently with the
registry's caloric model. Canonical bulk momentum is retained. The pressure
solver then responds to the changed composition and U.

**Empty cells.** Exact empty cells (main's
[empty-cell support](MAC_VACUUM.md)) are admitted. A face touching one
carries no diffusive flux. Because diffusion moves zero net mass across every
face, emptiness cannot change during a diffusion advance. Empty cells are
excluded from temperature, pressure and entropy evaluation, and they must
leave the advance bit-identical. Gas enters them only through the flow solver.

## Integration

Each interval of at most 0.1 simulated seconds executes diffusion for half the
interval, the existing flow solver for the interval, then diffusion for the
other half. Flow keeps its existing maximum 0.01-second substep. This symmetric
composition is not a claim of second-order accuracy for the full nonlinear
solver.

Diffusion uses explicit Heun prediction and correction, accepting
`dt max(D_kj) / dx² <= 0.2` in both stages. Each step is selected at 0.18,
10% inside that bound. Near the Knudsen limit the predictor's coefficient is
marginally larger, so a step chosen exactly at the bound would always be
rejected once. A shared attempt budget bounds retries. A failed advance
publishes no candidate state or clock.

Per-cell total mass, every species' global mass, global U, and endpoint
entropy are checked against the existing operand-scaled accounting bounds;
the entropy allowance is unchanged. Changes too small to resolve in stored
inventories are counted. Positive unrepresentable inventories reject rather
than becoming zero.

The zero-flux shortcut requires no tolerance. Every face must join identical
inventories and identical U, or touch an empty cell. With pressure diffusion,
equal masses at different temperatures are not stationary.

## Transport catalogue

`GasTransportProfile::standard()` is immutable profile `ma-gas-transport-1`.
The coefficients and their provenance are stored together in the profile. The
molecular binary diffusivity is in m²/s:

```
D_mol(T,p) = 1e-4 exp(a + b/T + c ln(T)) 101325/p
```

The factor `1e-4` converts cm²/s. T is in kelvin and p in pascals. The
**validated ranges** are:

| Pair | Source/model | Validated T (K) | Validated p (kPa) | Estimated |
| --- | --- | --- | --- | --- |
| N₂–He | NIST TN 2279 Table 1a, a=-10.266, b=27.449, c=1.724 | 298–723 (fit from 300 K, plus tabulated 298 K reference) | 10–200 | No, evaluated fit |
| N₂–CO₂ | NIST TN 2279 Table 1a, a=-7.317, b=-139.909, c=1.103 | 253–473 | 10–200 | No, evaluated fit |
| N₂–O₂, O₂–He, O₂–CO₂, He–CO₂ | Fuller–Schettler–Giddings correlation | 253–473, selected validation envelope | 10–200 | Yes |

The 10–200 kPa envelope is a chosen low-density envelope for the
inverse-pressure law, not a measured pressure range from NIST. NIST's
reference values and correlations come from
[Burgess, NIST TN 2279 (2024)](https://doi.org/10.6028/NIST.TN.2279). The
Fuller estimates use molecular weights in g/mol and the diffusion volumes
N₂=18.5, O₂=16.3, He=2.67 and CO₂=26.9, with the `0.001 T^1.75`
atmospheric-pressure formulation in
[COMSOL's gas diffusion model](https://doc.comsol.com/6.4/doc/com.comsol.help.lgp/lgp_ug_thermo.5.27.html).
Property uncertainty is separate from numerical error and from the
mixture-averaged model error.

**Outside the validated range** each correlation is evaluated in its own
functional form: the NIST `a + b/T + c ln T` fit, the Fuller `T^1.75` law,
and `1/p` scaling. The state is admitted, not rejected. These values are
extrapolations with unquantified error. The Fuller form is a kinetic-theory
estimate with reasonable behaviour well outside 253–473 K. The NIST fits are
empirical and become less trustworthy the further T leaves their fitted
interval, especially below it. Above about 200 kPa the ideal-gas `1/p` law
overstates the coefficient as the gas departs from the dilute limit. Every
pair evaluation outside the validated range is counted and its extreme face
T and p are recorded (see the [report](#reports-and-diagnostics)). Only a
pair missing from the profile rejects.

**Knudsen limit.** As p → 0, `D_mol` grows without bound and the explicit
step `0.18 dx²/D` would vanish. Each binary coefficient is therefore combined
with a free-molecular limit by the Bosanquet formula:

```
1/D = 1/(m D_mol) + 1/D_Kn,   D_Kn = (L/3) sqrt(8 R T / (π M_ij)),   M_ij = M_i M_j / (M_i + M_j)
```

Here L is the cell spacing and m the mode multiplier. The form is a
regularization at the grid scale. An ONI cell is not a pore, and the
transition regime is not modelled in detail. The choices are deliberate:

- The mixture-averaged closure and the Onsager-symmetric form need a
  symmetric pair coefficient `D_kj = D_jk`.
- `sqrt(8RT/(π M_ij))` is the mean relative speed of i–j molecule pairs.
- The pair limit exceeds the per-species Knudsen coefficient
  `(L/3) sqrt(8RT/(π M_i))` by at most `sqrt(1 + M_i/M_j)`, which is 3.4 for
  CO₂ in He. It therefore never binds earlier than a per-species limit.

At L = 1 m and 300 K, `D_Kn` is about 195–450 m²/s, against
`D_mol ≈ 2×10⁻⁵ m²/s` at one bar. It changes ordinary coefficients by about
1e-7 relative and dominates only below about 0.01 Pa. The resistance form
`1/D = p/(m D_atm) + 1/D_Kn` stays finite even at 1e-300 Pa.

The multiplier scales the molecular coefficient only. The Knudsen limit is
kinematic (molecules cannot diffuse faster than they fly across the cell), so
accelerated mode saturates at the same bound. Near vacuum the substep count
is then bounded by about `0.05 s × D_Kn / (0.18 dx²)` per half interval in
both modes: about 125 at 1e-4 Pa and 1 m cells. At one bar, the
unscaled limit makes 100× mode differ from exact 100× time scaling by about
1e-5 in the coefficient.

## Reports and diagnostics

`MacClosedAdvanceReport.diffusion` reports:

- accepted substeps, rejected trials and processed faces;
- exchanged mass (half the sum of absolute constituent face transfers);
- unresolved updates and execution time;
- species mass residuals and bounds, and the U residual and bound;
- entropy change and allowance;
- `out_of_range_evaluations`: the (face, stage, pair) coefficient evaluations
  of accepted substeps whose face T or p lies outside that pair's validated
  range;
- the minimum and maximum face temperature and pressure among those
  evaluations (zero when there are none).

The native diffusion status ABI is version 2 (224 bytes). It reports
cumulative work, timing and out-of-range counts and extremes, with
conservation residuals for the most recent completed advance. The unreleased
184-byte version-1 layout rejects. Managed telemetry and the development
report record the same fields.

## Modes, configuration and persistence

`MacDiffusionSettings` supports disabled (0), physical (1, multiplier 1), and
accelerated (2, finite multiplier >=1). `MacDiffusionSettings{}` selects
physical. `MacClosedSettings` retains disabled as its low-level compatibility
default so existing reference trajectories keep their physics. The game adapter
explicitly selects physical for new gas-only development regions.

The status panel displays the effective mode and multiplier. **Shift+F6**
cycles physical → accelerated → off → physical, including while paused. The
initial accelerated multiplier is **100**. The preference is configurable in
`mods/config/MixedAtmospheres/diffusion.json` under ONI's user-data directory:

```json
{"Mode":"physical","AcceleratedMultiplier":100}
```

Mode changes use the completed native session/frame fence. A pending request is
retried at the next completed frame; it never changes a running trial. Changing
configuration invalidates replay across that boundary until a new checkpoint.
The option file selects new regions; a restored owner uses its saved mode.
Liquid and mixed gas/liquid regions reject enabling gas diffusion.

Enabled checkpoints use `MACCLS01` version 5 with either numerical kernel (see
[version assignment](MAC_CHECKPOINT.md#version-assignment)). After the kernel
selector they store the mode and multiplier, then the discrete formulation
number (`mac_diffusion_formulation`, 2), the profile identity and every
coefficient, validated range, estimated flag and source. A changed property
under the same profile ID rejects. So does any other formulation.

On its development branch diffusion used version 4, first without and then
with the formulation field. Those images were never released. Main's interval
kernel took version 4, and diffusion moved to version 5. Both earlier diffusion
layouts now reject instead of restoring under different physics.

Disabled owners keep their version 2/3 (reference kernel) or 4 (interval
kernel) bytes. Old checkpoints restore with diffusion disabled until explicitly
upgraded through the mode control. The managed paired-save wrapper reads only
the unchanged metadata prefix of version 5.

Diffusion composes with either numerical kernel: it wraps each flow interval,
whether the interval kernel publishes it or falls back to the reference.
Composed reports accumulate every flow counter, including main's interval and
pressure-restart counters.

## Execution and validation

The common ordinary-range path specializes storage for up to four globally
present gases and retains every positive species. More species, extreme
operands or uncertified arithmetic use the general range-safe path. The
general path computes the log-mean and logarithms of range-extended values
without projecting them.

Scratch capacity is reused on the solver thread. Pair coefficients and
Knudsen limits are cached only for exactly equal temperatures within a
predictor stage; pressure scaling and range checks use each actual face.
Parallel face evaluation and fixed-order cell gathers keep accumulation
deterministic. There is no trace cutoff or approximate equilibrium threshold.

Run `ctest -R gas_diffusion --output-on-failure` after building, or run
`ma_mac_diffusion_tests` directly. The tests cover:

- 2-, 6- and 64-cell analytical binary modes and refined numerical solutions
  (<=1% of the initial perturbation, isobaric equal-mass gases);
- conservation, absent gases, helium and CO₂ traces, pure gases and walls;
- unequal temperature, moving mixtures and accelerated time scaling (within
  the bound set by the unscaled Knudsen limit);
- rollback, more than four species, subnormals, profile identity and
  deterministic checkpoint continuation.

Formulation-2 controls cover:

- **Empty cells.** Diffusion beside empty cells is bit-identical to the same
  cells as walls, in both modes and both arithmetic paths, and empty cells
  stay exactly empty. A face touching vacuum keeps the zero-flux certificate.
  Physical and accelerated owners expand gas into an empty cell for 20 frames
  with every constituent conserved. A single gas beside vacuum follows the
  diffusion-off trajectory bit for bit for 20 frames.
- **Opposing pressure and composition gradients.** A 16-cell row (40 frames)
  and a 3×2 room (150 frames) with 1–20% density steps and O₂ gradients of
  1e-4 and 1e-5 run in both modes, plus general-path variants. Every composed
  advance satisfies the unchanged entropy check. Pure-diffusion N₂/CO₂ and
  helium-rich checkerboards (He mole fraction 0–0.95) at the stability bound
  run for 6 × 10 s; formulation 1 failed these.
- **Out-of-range states.** Composed rooms at 200 K (CO₂-free, see below),
  250, 480 and 600 K, and at 1 kPa, 500 kPa and 1 Pa, run in both modes; an
  in-range 300 K room records no out-of-range evaluations. The diffusion stage
  alone runs at 1e-4 and 1e-9 Pa. The controls check counts, recorded
  extremes, conservation and Knudsen-bounded substep counts.
- **Persistence.** Version 5 round-trips and continues exactly with both
  kernels in both modes. Rebuilt version-4 diffusion layouts, an unknown kernel,
  formulation 1 and a diffusion-disabled version 5 reject. Diffusion-disabled
  owners still write version 2 (reference) or 4 (interval).
- **Composed reports.** A zero-flux single-gas wave reports exactly the flow
  counters of the diffusion-off owner, including a pressure-increment restart.

`ma_oni_diffusion_tests` covers the ABI, worker dispatch, stale-frame
rejection and mode persistence. It also runs the review's scenes through the
domain ABI:

- the 3×2 room with 1% and 5% density steps and O₂ gradients 1e-4/1e-5, 150
  frames each in both modes;
- a cooled generated scene (about 297 K);
- a 297 K helium trace;
- O₂/CO₂ rooms above 200 kPa, below 10 kPa, at 250 K and at 480 K.

All complete with diffusion off, physical and accelerated, and record
out-of-range counts.

## Known limits

These were found while verifying formulation 2:

- **Trace fronts in large rooms.** A constituent absent from part of a room
  spreads one cell per Heun stage, two per substep. Its amount falls by about
  `dt D/dx²` per cell, roughly 1e-6 at one bar and 1 m cells. Positive
  inventories below binary64 reject rather than becoming zero, so physical
  diffusion faults once the tip is about 36 cells from the source. In a
  64-cell row with a 1e-28 kg helium trace in one end cell, that happens at
  frame 4 (about 1 s). A 40-cell row runs 400 frames because the front reaches
  the far wall first. Accelerated 100× has a per-cell factor about 100 times
  larger (about 1e-4) and completes 400 frames of the 64-cell row.
  Formulation 1 had the same limit. The generated development scenes carry the
  trace in every cell, and the 3×2 and 7×5 rooms are too small to reach it.
  Larger configured rooms with a locally introduced gas will reach it.
- **Strong flow beside diffused traces.** One kilogram of helium in one cell
  of an air row (about 6 bar) runs with diffusion disabled. Physical diffusion
  first carries a 2e-12 kg trace into the neighbours, and the flow solver then
  rejects with "MAC primary storage rounding exceeds its operand bound". The
  flow solver rejects the same state with diffusion disabled when such traces
  are present initially, so this is the existing strong-flow limit made
  reachable.
- **Near vacuum.** A composed owner at 1e-4 Pa exhausts its attempt budget in
  the flow solver even with diffusion disabled.
- **Cold CO₂.** A CO₂-bearing room at 200 K trips the EOS guard "Material
  energy reference erases resolved thermal or elastic energy", also with
  diffusion disabled. CO₂'s large energy offset loses the resolved thermal
  energy there.

## Maxwell–Stefan review

`tools/review_gas_diffusion.py` independently solves dense Maxwell–Stefan face
systems using the same exported transport and caloric data. Like production,
its driving force includes pressure diffusion. The reference cases are not
exactly isobaric (equal cell masses, different compositions), so the term is
exercised. The oracle reports mixing-time model error separately from
10/5-second refinement error for both solvers.

Its declared ordinary-air envelope is 300 K, approximately one bar, N₂/O₂
with 0–0.1% CO₂ by mass. The model target is <=5% mixing-time difference and
the numerical target is <=1%. This narrow envelope does not validate
arbitrary helium-rich mixtures, temperature extremes or room-scale
circulation.

## Performance

Performance command: `ma_mac_diffusion_tests --benchmark 250000`. It measures
wall time for both half-stages, including exact stationarity checks,
validation, state construction and replacement. It takes five warmup advances
and 31 samples each for uniform and actively mixing grids. The proposed
allocation is 6.25 ms p95 per 0.1 simulated seconds on a Ryzen 5900X. It
remains a measurement target, not an achieved guarantee.

The complete colony gate is still solver plus adapter <=62.5 ms p95,
sustained 1× and >=30 FPS. The flow solver also needs substantial
optimization.

Measured on 2026-10-18 for formulation 1, after pulling `fa8df8e`. The host
was Windows Release/MSVC with strict floating point on a Ryzen 9950X (16
cores, 32 threads), with eight diffusion workers at 250,000 cells:

| Scene | p50 (ms) | p95 (ms) | Proposed allocation |
| --- | --- | --- | --- |
| Uniform, exact zero-flux certificate | 4.80 | 5.06 | Met on this host |
| Actively mixing | 179.87 | 188.32 | **Missed** |

Formulation 2 adds one `log1p` or logarithm per present species per face. On
2026-10-18 both formulations ran alternately on the shared 24-thread Linux
development host (load average about 25, GCC Release): four runs each, with
formulation 2's uniform scene counted only from its two runs after the
certificate change. Their times overlapped:

| Scene | Formulation 1 p50 (ms) | Formulation 2 p50 (ms) |
| --- | --- | --- |
| Uniform | 1.9–3.4 | 2.0–3.4 |
| Actively mixing | 249–272 | 260–301 |

The uniform figures for formulation 2 are after the certificate examines
emptiness only for differing inventories. Before that change it summed both
cells' masses on every face, which cost about 1.7× in the uniform scene. These are
isolated synthetic diffusion measurements, without the flow solver or the ONI
adapter. They do not establish colony real-time performance or the 5900X
gate. Authoritative state copies, remaining serial work and worker launch
costs still need optimization; the exact-temperature cache does not remove
those costs. No runtime work is dropped to satisfy a wall-clock budget.

Formulation-2 accuracy on Linux is as follows. The binary analytical
perturbation error is 0.00110%, with 0.00109% difference from a
16-times-refined time integration. Independent ordinary-air Maxwell–Stefan
model errors are 0% (binary) and 0.0407% for both ternary cases, both with
pressure diffusion. Refinement errors are at most 0.00814%.

Against the earlier pressure-diffusion-free oracle, the same production
mixing times differ by 0.32–0.43%. That is the effect of the reference cases'
0.4% pressure difference. These are errors against the declared mathematical
and reference models, not experimental accuracy guarantees.

## Game validation

WindowsPlayer 744825 was exercised through Steam on 2026-10-18 with
formulation 1, in disposable Spaced Out six-cell mixed-air rooms. Physical
diffusion passed 121 native frames, and accelerated 100× diffusion continued
through frame 1,268. A diffusion-enabled paired colony save passed file
readback and restored its exact physical image. It then matched 120
continuation frames against native input replay. The saved 100× mode survived
a physical default preference. Shift+F6 cycled all three modes while
preserving the gas-composition overlay; physical play continued past the
completed audit without a fault. Numerical results and package hashes are in
[the validation record](../docs/gas-diffusion-validation.json), which describes
formulation 1.

These Windows checks establish the bounded development room only. They do not
establish whole-colony performance, normal gameplay FPS or actual Linux ONI.
Linux game runs of formulation 2 are recorded with the merge verification in
[implementation status](../docs/implementation-status.md).
