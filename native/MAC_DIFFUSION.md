# Conservative gas diffusion

`MacClosedWorld` can exchange constituents across open gas faces after bulk
pressure flow has settled. The implementation is in `mac_diffusion.cpp` and
`mac_diffusion_normal.cpp`. Liquid diffusion, dissolution, Soret diffusion and
pressure-driven species separation are outside this implementation's validated
scope. Resolved circulation and buoyancy are still needed for fast room mixing.

## Equations and integration

Each face uses the union of gases present on either side, including species
absent on one side and every positive trace. At arithmetic face T, p, X and Y,

```
D'_k = sum(j != k, Y_j) / sum(j != k, X_j / D_kj)
J*_k = -rho (W_k / W_mean) D'_k grad(X_k)
J_k  = J*_k - Y_k sum(J*_j)
```

This is the mole-gradient, mixture-averaged formulation in the
[Cantera governing equations](https://cantera.org/3.2/reference/onedim/governing-equations.html#diffusive-fluxes).
It approximates the Maxwell–Stefan coupling. The largest mass fraction's flux
is evaluated as the negative sum of all other corrected fluxes, imposing zero
net diffusive mass algebraically. No inventory is clipped or normalized.

One face flux gives equal and opposite constituent transfers. The energy flux
is `sum(J_k h_k(T_face))`; it changes stored internal energy U consistently with
the registry's caloric model. Canonical bulk momentum is retained. The existing
pressure solver then responds to the changed composition and U.

Each interval of at most 0.1 simulated seconds executes diffusion for half the
interval, the existing flow solver for the interval, then diffusion for the
other half. Flow keeps its existing maximum 0.01-second substep. This symmetric
composition is not a claim of second-order accuracy for the full nonlinear
solver. Diffusion itself uses explicit Heun prediction/correction, with
`dt max(D_kj) / dx² <= 0.2` in both stages. Positivity, admissible thermodynamic
states and entropy are checked; invalid trials backtrack. A shared attempt
budget bounds retries. A failed advance publishes no candidate state or clock.

Per-cell total mass, every species' global mass, global U, and endpoint entropy
are checked against the existing operand-scaled accounting bounds. Changes too
small to resolve in stored inventories are counted. Positive unrepresentable
inventories reject rather than becoming zero. Exact pressure equilibrium is
insufficient to skip an active concentration gradient. The zero-flux shortcut
requires identical neighbouring constituent inventories without a tolerance.

## Transport catalogue

`GasTransportProfile::standard()` is immutable profile `ma-gas-transport-1`.
The coefficients and their provenance are stored together in the profile.
Binary diffusivity is in m²/s:

```
D(T,p) = 1e-4 exp(a + b/T + c ln(T)) 101325/p
```

The factor `1e-4` converts cm²/s. T is kelvin, p is pascals. The common admitted
pressure envelope, 10–200 kPa, is a chosen low-density model envelope for the
inverse-pressure approximation, not a measured pressure range from NIST.

| Pair | Source/model | T range (K) | Estimated |
| --- | --- | --- | --- |
| N₂–He | NIST TN 2279 Table 1a, a=-10.266, b=27.449, c=1.724 | 298–723, including the tabulated 298 K reference; fitted range starts at 300 K | No, evaluated fit |
| N₂–CO₂ | NIST TN 2279 Table 1a, a=-7.317, b=-139.909, c=1.103 | 253–473 | No, evaluated fit |
| N₂–O₂, O₂–He, O₂–CO₂, He–CO₂ | Fuller–Schettler–Giddings correlation | 253–473, selected validation envelope | Yes |

NIST's reference values and correlations come from
[Burgess, NIST TN 2279 (2024)](https://doi.org/10.6028/NIST.TN.2279).
The Fuller estimates use molecular weights in g/mol and diffusion volumes
N₂=18.5, O₂=16.3, He=2.67, CO₂=26.9, with the coefficient `0.001 T^1.75`
atmospheric-pressure formulation, as documented in
[COMSOL's gas diffusion model](https://doc.comsol.com/6.4/doc/com.comsol.help.lgp/lgp_ug_thermo.5.27.html).
Property uncertainty is separate from numerical error and from the
mixture-averaged model error. Unsupported pairs or states outside their profile
range reject; the solver does not silently extrapolate.

## Modes, configuration and persistence

`MacDiffusionSettings` supports disabled (0), physical (1, multiplier 1), and
accelerated (2, finite multiplier >=1). `MacDiffusionSettings{}` selects
physical. `MacClosedSettings` retains disabled as its low-level compatibility
default so existing reference trajectories keep their physics. The game adapter
explicitly selects physical for new gas-only development regions.

The status panel displays the effective mode and multiplier. **Shift+F6** cycles
physical â†’ accelerated â†’ off â†’ physical, including while paused. The initial
accelerated multiplier is **100**. Its preference is configurable in
`mods/config/MixedAtmospheres/diffusion.json` under ONI's user-data directory:

```json
{"Mode":"physical","AcceleratedMultiplier":100}
```

Mode changes use the completed native session/frame fence. A pending request is
retried at the next completed frame; it never changes a running trial. Changing
configuration invalidates replay across that boundary until a new checkpoint.
The option file selects new regions; a restored owner uses its saved mode.
Liquid and mixed gas/liquid regions reject enabling gas diffusion.

Enabled checkpoints use `MACCLS01` version 4. They contain the effective mode,
multiplier, profile identity, every coefficient, validity bound, estimated flag
and source. A changed property under the same profile ID rejects. Disabled
owners retain version 2/3 bytes. Old checkpoints restore with diffusion disabled
until explicitly upgraded through the mode control.

`MacClosedAdvanceReport.diffusion` reports accepted substeps, rejected trials,
processed faces, exchanged mass (half the sum of absolute constituent face
transfers), unresolved updates, execution time, species mass residuals/bounds,
U residual/bound, and entropy change/allowance. The additive native diffusion ABI
reports cumulative work and timing, with conservation residuals for the most
recent completed advance. Managed telemetry records the same data.

## Execution and validation

The common ordinary-range path specializes storage for up to four globally
present gases. It retains every positive species; more species, extreme
operands or uncertified arithmetic use the general range-safe path. Scratch
capacity is reused on the solver thread. Pair coefficients are cached only for
exactly equal temperatures within a predictor stage; pressure and validity
checks use each actual face. Parallel face evaluation and fixed-order cell
gathers keep accumulation deterministic. There is no trace cutoff or approximate
equilibrium threshold.

Run `ctest -R gas_diffusion --output-on-failure` after building, or run
`ma_mac_diffusion_tests` directly. Tests cover 2-, 6- and 64-cell analytical
binary modes and refined numerical solutions (<=1% of the initial perturbation),
conservation, absent gases, helium and CO₂ traces, pure gases, walls, unequal
temperature, moving mixtures, accelerated time scaling, rollback, more than
four species, subnormals, profile identity and deterministic checkpoint
continuation. Native tests cover the ABI, worker dispatch, stale-frame rejection
and mode persistence.

`tools/review_gas_diffusion.py` independently solves dense Maxwell–Stefan face
systems using the same exported transport/caloric data. It reports mixing-time
model error separately from 10/5-second refinement error for both solvers. Its
declared ordinary-air envelope is 300 K, approximately one bar, N₂/O₂ with
0–0.1% CO₂ by mass. The model target is <=5% mixing-time difference; the
numerical target is <=1%. This narrow envelope does not validate arbitrary
helium-rich mixtures, temperature extremes or room-scale circulation.

Performance command: `ma_mac_diffusion_tests --benchmark 250000`. It measures
wall time for both half-stages including exact stationarity checks, validation,
state construction and replacement, with five warmup advances and 31 samples
each for uniform and actively mixing grids. The proposed
allocation is 6.25 ms p95 per 0.1 simulated seconds on a Ryzen 5900X. It remains
a measurement target, not an achieved guarantee. The complete colony gate is
still solver plus adapter <=62.5 ms p95, sustained 1× and >=30 FPS; the flow
solver also needs substantial optimization. Native Linux tests do not establish
actual Linux ONI runtime validation.

Measured on 2026-10-18 after pulling `fa8df8e`, Windows Release/MSVC with strict floating point, Ryzen
9950X (16 cores/32 threads), eight diffusion workers at 250,000 cells:

| Scene | p50 (ms) | p95 (ms) | Proposed allocation |
| --- | --- | --- | --- |
| Uniform, exact zero-flux certificate | 4.80 | 5.06 | Met on this host |
| Actively mixing | 179.87 | 188.32 | **Missed** |

These are isolated synthetic diffusion measurements, without the flow solver or
ONI adapter. They do not establish colony real-time performance or the 5900X
gate. Authoritative state copies, remaining serial work and worker launch costs
still need optimization; the exact-temperature cache does not remove those
costs. No runtime work is dropped to satisfy a wall-clock budget.

The binary analytical perturbation error is 0.00110%, with 0.00109% difference
from a 16-times-refined time integration. Independent ordinary-air mixing-time
model errors are 0% (binary), 0.04084% and 0.04081% (two ternary cases); reported
time-step refinement errors are at most 0.00817%. These are errors against the
declared mathematical/reference models, not experimental accuracy guarantees.

WindowsPlayer 744825 was exercised through Steam on 2026-10-18 in disposable
Spaced Out six-cell mixed-air rooms. Physical diffusion passed 121 native frames;
accelerated 100× diffusion continued through frame 1,268. A diffusion-enabled
paired colony save passed file readback and restored its exact physical image,
then matched 120 continuation frames against native input replay. The saved
100× mode survived a physical default preference. Shift+F6 cycled all three
modes while preserving the gas-composition overlay; physical play continued
past the completed audit without a fault. Numerical results and package hashes
are in [the validation record](../docs/gas-diffusion-validation.json).

These Windows checks establish the bounded development room only. They do not
establish whole-colony performance, normal gameplay FPS or actual Linux ONI.
