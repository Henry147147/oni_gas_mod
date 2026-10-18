# Heat regularization, conduction and entropy checks

[`MacClosedWorld`](MAC_SOLVER.md) has two separate heat operators: a numerical
shock-heat regularization that spreads heat away from compression fronts, and
physical material conduction. They have separate settings, reports and energy
accounts. Both run as symmetric half steps around pressure/transport
([order](MAC_SOLVER.md#substep-structure)). Physical viscosity, which also heats, is
documented in [native viscosity](../docs/native-domain-physics.md#physical-viscosity).

Code: `mac_shock_heat.hpp`/`.cpp`, `mac_material_entropy_detail.hpp`,
`mac_thermal.hpp`/`.cpp`.

## Shock-heat regularization

Strong compression over-heats the material at a shock front
([Noh (1987)](https://www.sciencedirect.com/science/article/pii/002199918790074X)
compares artificial viscosity with an added artificial heat flux). This operator moves
internal energy between neighbouring cells only where the flow compresses. It is not
physical conductivity or gas diffusion: every species including traces, every passive
quantity, canonical J and the geometry are unchanged, and no heat crosses a wall.

| Owner setting | Value | Component law | Face conductance |
| --- | --- | --- | --- |
| `none` | 0 | — | — |
| `compression_activated` | 1 | `closing_velocity` | max adjacent closing speed × harmonic equilibrium capacity / h |
| `pressure_sensed_acoustic` (default) | 2 | `pressure_sensed_viscosity` | max adjacent (v + a) × harmonic Cp / h |
| `pressure_sensed_series` | 3 | `pressure_sensed_series` | series of half-cell Cp·d/h conductances |

The persisted owner enum and the component enum have different meanings and are never
cast into each other.

### Pair relaxation

At fixed masses and volumes, let C_L and C_R be the EOS equilibrium heat capacities
and G·dt the integrated nonnegative conductance. Find the signed heat Q_eq that brings
the two actual internal energies to a common temperature. Constant-capacity gas or
liquid endpoints use the exact capacity-weighted partition; gas/liquid endpoints use
a bounded scalar root with the full EOS, keeping elastic and reference energies. With
`x = G dt (1/C_L + 1/C_R)` and `f = 1 − exp(−x)`, transfer `Q = f Q_eq` from left to
right, applying −Q and +Q to the stored U fields. For constant capacities this is
exact two-body conduction with the semigroup property; the mixed-EOS extension uses
the initial capacities for its rate and is a first-order nonlinear regularization.

`expm1` handles ordinary x; below 2^-53 f = x with the full exponent kept; at x ≥ 64
f = 1. No positive contribution becomes its zero binary64 projection, and U updates too
small to store are flagged.

The mixed-EOS root never evaluates one mixture at the other's starting temperature
(that can leave a material's compression domain). For the zero-expansion elastic EOS
at fixed volume, `Cv ≤ C_eff ≤ Cv + nR`, so Q_eq is bounded by
`min(Cv_hot + nR_hot, Cv_cold + nR_cold)` times the initial temperature difference,
with an outward 128-epsilon allowance. The root bisects heat directly for at most 128
iterations; a trial outside a domain narrows the upper bound. Each root exports its
equilibrium temperatures, residual, rejected-trial count and a storage certificate:
128 epsilons of the two temperatures plus, per cell,
`(|U_before| + |Q| + |U_after|) / Cv`. That certificate is accepted only on the cooling
side of equilibrium; crossing equilibrium still requires the 128-epsilon temperature
bound. Temperatures must stay within their original interval, energy must close at
128 epsilons and actual entropy must not decrease beyond the stored-U allowance.

### Face conductance laws

For each normal cell pair, a cell is active only while it compresses (positive normal
closing speed). Rest, expansion, stationary contacts and pure tangential shear are
inactive, and uniform temperature transfers nothing. Coefficients are frozen from the
source of each call; each active face receives a forward half step and a reverse half
step, reversing the entire axis order too. For constant capacities this symmetric
composition is second order in time.

- **Mode 1** uses the larger adjacent closing speed times the harmonic equilibrium
  capacity divided by h, with coefficient one and no sound-speed term.
- **Mode 2** matches the conductivity to the compression stress
  `q = ρ v (v + a)` with `a = 0.5 · sound_speed · pressure_sensor`, the frozen
  half-sound speed and sensor of [mode 2 compression](MAC_PRESSURE.md#pressure-sensed-acoustic-compression-mode-2),
  built by the same function. [Cook's derivation](https://www.osti.gov/servlets/purl/1224395)
  (equations 6–9) gives `κ = c_p β` for bulk viscosity alone; reading v/h as the
  compressive strain rate gives `β = ρ h (v + a)`. Each cell uses
  `Cp = C_intrinsic + nR` (the actual constant-pressure capacity of the
  zero-expansion surrogate EOS, independent of reference energies) and diffusion
  speed `v + a`; the face uses the greater adjacent speed times harmonic Cp over h.
  The pair relaxation still uses fixed-volume capacities.
- **Mode 3** assigns each cell `G_i = Cp_i d_i / h` and combines the two half-cell
  resistances, `G_face = 2 G_L G_R / (G_L + G_R)`; an inactive cell gives zero flux.
  `diffusion_speed` then reports the equivalent face speed `G_face h / harmonic(Cp)`.

None of these coefficients was fitted to a benchmark. Mode 3 made the strong-flow
collision result worse than mode 2 (density/pressure ratios 0.919/0.898 against
0.856/0.796), so mode 2 stays the default ([results](MAC_STRONG_FLOW.md#results-by-method)).

### Reusing completed EOS readings

Each heat stage retains the current validated EOS reading for every fluid cell.
Each accepted pair replaces its two cached readings together with the corresponding
stored-U updates. A bit-identical U reuses its existing reading; changed U receives
a complete evaluation before the pair can pass its interval, energy and entropy
checks. Coefficients remain frozen from the original source. Masses, volumes and the
immutable model stay fixed during the stage; the cache is destroyed on return or
failure, so an edit between calls always receives fresh readings. No wall-clock or
approximate-state criterion controls reuse.

Source and final entropy checks keep only temperature and gas-volume readings,
instead of retaining two complete EOS result vectors. Pair input evaluations and
the final whole-grid evaluation pass reuse the corresponding validated states.
The report counts these as `reused_pair_eos_readings` and
`reused_endpoint_eos_readings`; they do not include the source-admission, coefficient
or mixed-EOS bracket evaluations, which still execute.

`ma_mac_shock_heat_cache_tests` compares every pair receipt and the complete endpoint
against ordered replay through the public, freshly evaluated pair operator. Its 72
stages cover both boundary types, an interior solid, all three laws, wet mixtures,
traces, below-range velocities/heat and edits between calls. There are 2,064 exact
receipt comparisons and 1,440 witnesses where an earlier pair changed a later pair's
input inventory. These are cache-correctness controls, not a strong-flow pass.

## Entropy measurement

Heat and compressive-work stages admit a candidate only if actual stored-state entropy
does not fall beyond 128 epsilons of the stored-U/temperature operands. The change is
computed per fixed-mass, fixed-volume endpoint with the relative helper in
`mac_material_entropy_detail.hpp`: `dS = Cv log(T1/T0) + nR log(Vg1/Vg0)`. Pure gas
uses the conserved caloric increment, `Cv log((U1 − offset)/(U0 − offset))`; mixed and
liquid states use the EOS temperature and gas headspace. Identical stored U and
constituents give exactly zero. Small ratios keep the full-exponent leading term.

Subtracting two absolute EOS entropy readings fails when thermal energy nearly cancels
a negative caloric reference. In a quadratic-reconstruction separation run, a heat pair
(1/65536 m³ cells, nitrogen with a helium trace, about 416.940 K) exchanged exactly
±61/576460752303423488 J. The true entropy increment is about +1.38e-26 J/K and the
allowance about 9.03e-19 J/K, but the absolute difference was −1.73e-18 J/K and
falsely rejected. The pair and global checks now use the relative calculation; the
bound, its 128-epsilon coefficient and the rejection sign are unchanged.
`ma_mac_shock_heat_tests --entropy-regression` covers forward/reflected transfer,
unchanged and below-range states, and a genuine cooling state that must still reject.

## Physical conduction and stored-energy rounding

`advance_mac_conduction` (`thermal_conduction = true`) is closed finite-volume,
backward-Euler conduction with harmonic half-cell face resistance and conductivity
frozen per stage. Single-phase cells use the constant-capacity graph solve. Gas/liquid
cells use complete-EOS pairwise contact solves, described in
[gas/liquid conduction](../docs/native-domain-physics.md#mixed-cell-conduction). Gas conductivity is a
declared mole-weighted reference and liquid conductivity is volume-weighted. No heat
crosses walls or the exterior; species, passives and J are retained.

Independent nearest rounding of each cell's new U can repeatedly cool a hot cell while
every small incoming update rounds away, losing energy from the closed system. For each
connected conductive component, the operator forms the exact additive expansion of
`old U + solved heat` per cell and finds its two neighbouring binary64 values. It then
chooses among those endpoints to keep the component's exact original U sum when that
sum is representable there: spacings are powers of two, so a greedy selection by
descending spacing works, preferring the larger fractional request and breaking ties
by cell index. No cell leaves its rounding bracket, and an insulating wall cannot carry
a correction. If the spacings are too far apart to admit the exact sum, the component
keeps nearest rounding and the ordinary accounts, and the report counts it as
unavailable; no minimum flux is forced.

Entropy for this operator is `Cv log1p(ΔU / (Cv T))` with ΔU formed exactly.
`redistributed_energy` (and the native ABI's `RedistributedEnergy`) counts only
represented U changes; the requested redistribution is a separate field. A sub-ULP
request therefore shows positive requested heat, zero redistribution and zero entropy
change, is counted unresolved, and cannot pass a conduction-progress check.

Licensed-native controls: `sealed-water-small-heat` (+2,000 J / −500 J) must conserve
energy and report unresolved updates (`internalConductionPassed: false` by design);
`sealed-water-heat` (+2 MJ / −0.5 MJ) must show positive redistribution and entropy and
warm both the source and a conductive neighbour. On Linux and Windows-under-Wine the
latter gives 4.375 J represented redistribution, 1.582595547e-5 J/K entropy increase
and zero measured U+K drift; the small control reports 11,536 unresolved updates. The
rendered `native-liquid-heat-domain` (Windows `-NativeDomain water-heat`) scene uses the
resolvable input with save/reload checks; game-level results are in
[sealed native domain](../docs/sealed-native-domain.md). Fluid/solid exchange,
variable material properties and phase change are not covered.

## Verification

- `ma_mac_shock_heat_tests` (eight groups): exact unequal-capacity pairs, reflection,
  stiff relaxation and semigroup; mixed gas/liquid partitions, below-range heat and a
  full-range stiff relaxation number; periodic and solid-corner wet grids; stationary
  contacts, shear and uniform temperature; a fixed three-body constant-capacity network
  against its exponential solution; full-range activation and eight invalid inputs;
  asymmetric mixed brackets including a 100,000 K inventory; wet contacts unchanged
  bit for bit. The network's N1/2/4/8/16 maximum temperature errors are 2.146985,
  0.536746, 0.134136, 0.033530 and 0.008382 K, meeting the successive-ratio limit of
  0.3 and the final 0.05 K limit.
- `ma_mac_matched_heat_tests` and `ma_mac_series_heat_tests` (eight groups each): periodic
  gas, wet solid corner, compressed liquid, inactive cases, subrange heat, two-body
  semigroup (and, for mode 3, unequal-capacity resistance on both axes) and invalid
  requests. A weak-wave control gives heat / amplitude² between 0.57282181 and
  0.57282197 for amplitudes 1e-3, 1e-5 and 1e-7.
- `ma_mac_shock_heat_owner_tests` (six groups per law): moving periodic wet mixture
  with exact component-stage replay, wet solid corner, physical conduction with both
  regularizers active, two accepted substeps in one revision, below-range initial
  heat, and disabled/invalid/late-failure/recovery controls. Flags select the
  reference combinations.
- `ma_mac_thermal_tests` (CTest `mac_occupied_conduction`): the two-node
  backward-Euler formula and time refinement, 2,400 small-water stages with exact
  component U sums, separate insulated components, and a gas/water pair whose spacings
  cannot admit conservative rounding.

## Reproduce

```sh
cmake --build build --config Release --target ma_mac_shock_heat_tests ma_mac_matched_heat_tests \
  ma_mac_series_heat_tests ma_mac_shock_heat_owner_tests
build/ma_mac_shock_heat_tests > heat.stdout
python3 native/tests/review_mac_shock_heat.py heat.stdout
build/ma_mac_shock_heat_tests --entropy-regression > heat-entropy.stdout
python3 native/tests/review_mac_heat_entropy.py heat-entropy.stdout heat-entropy.json
build/ma_mac_shock_heat_owner_tests > heat-owner.stdout
python3 native/tests/review_mac_shock_heat_owner.py heat-owner.stdout heat-owner.json --matched-heat
```

The owner test and its reviewer use different flags for the same configuration:

| Configuration (compression / heat) | Test flag | Reviewer flag |
| --- | --- | --- |
| 2 / 2 (defaults) | none or `--matched-heat` | `--matched-heat` |
| 2 / 3 | `--series-heat` | `--series-heat` |
| 2 / 1 | `--legacy-heat` | `--acoustic-compression` |
| 1 / 1 | `--quadratic-compression` | none |

The matched and series components pair with `review_mac_matched_heat.py` and
`review_mac_series_heat.py`. The reviewers rebuild the EOS and thermal partitions at
80 digits, the ordered face geometry from source J and masses, the conductances and
every signed U update, and check trace preservation, entropy and energy accounts.
