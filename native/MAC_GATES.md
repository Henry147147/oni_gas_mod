# Gravity and multiphase gates

Milestone B's gravity and multiphase exit is defined by eight gates
([completion plan](../docs/completion-plan.md#gravity-and-multiphase-gates)).
Each gate is a standalone executable that prints `GATE <id> PASS|FAIL|OPEN` with
its measurements and exits nonzero unless it passes. All except V run the
production kernel (numerical kernel 2) with default settings. The free-surface gates
(M1–M3) also select sharp interfaces (material selector 3). V runs the reference
path's void phase. Passing gates also run as CTests.

| Gate | Command | Status | CTest |
| --- | --- | --- | --- |
| G1 hydrostatic gas column | `ma_mac_gravity_gates G1` | pass | `mac_gate_g1_hydrostatic_column` |
| G2 gravitational species separation | `ma_mac_gravity_gates G2` | pass | `mac_gate_g2_species_separation` |
| M1 still water | `ma_mac_free_surface_gates M1` | pass | `mac_gate_m1_still_water` |
| M2 dam break | `ma_mac_free_surface_gates M2` | pass | `mac_gate_m2_dam_break_coarse` (16 cells across a only) |
| M3 standing wave | `ma_mac_free_surface_gates M3` | pass | `mac_gate_m3_standing_wave_coarse` (32 cells; period and amplitude within 5%) |
| M4 two liquids | `ma_mac_multiphase_gates M4` | pass | `mac_gate_m4_two_liquids` |
| M5 phase change | `ma_mac_multiphase_gates M5 --run` | pass (sharp-interface boiling) | `mac_gate_m5_stefan_coarse` (5 cells; worst error within 4%) |
| V vacuum front | `ma_mac_vacuum_front_diagnostic --gate` | pass (void phase, reference path) | `mac_gate_v_vacuum_front` |

## Passing gates

**G1.** N2 at 300 K starts from the solver's own discrete hydrostatic state, where
each face balances `A (p_below - p_above) = g (m_below + m_above) / 2`. It stays at
rest over 1,000 intervals of 0.1 s:

| Cells | Profile error vs exponential | Max speed | Pressure drift |
| ---: | ---: | ---: | ---: |
| 64 | 2.7e-11 | 2e-11 m/s | 7e-14 |
| 128 | 6.7e-12 | 2e-11 m/s | 7e-14 |
| 256 | 1.7e-12 | 2e-11 m/s | 6e-14 |

The successive error ratios are 0.252 and 0.248, so convergence is second order.

**G2.** CO2/N2, 50/50 by mole, in a 1 m column of 64 cells, with accelerated
diffusion (multiplier 1000) run for ten diffusion times. Over 1 m the barometric
law changes partial pressures by only 1.1e-4 (N2) and 1.7e-4 (CO2), so "within 1%"
of the law is applied to each species' exponent: ln(p_k(z)/p_k(z0)) must be within
1% of −M_k g (z − z0)/(RT). The settling comes from the Maxwell–Stefan
pressure-diffusion term ([diffusion](MAC_DIFFUSION.md)). Results:
- the worst exponent error is 2.2e-5 (2.9e-7 at multiplier 100, which runs about six
  times longer);
- the CO2 mole-fraction difference between the bottom and top cells is 1.5482e-5,
  against 1.5482e-5 predicted;
- species drift is 1.4e-15.

**M1.** A 1 m tank (32 × 32 cells) holds water 0.5 m deep under air, built in discrete
hydrostatic balance with the surface on a face. After 10 s:
- the maximum speed is 1.6e-11 m/s (3.8e-9 before the
  [sharp-interface gravity weight](MAC_PRESSURE.md#gravity-with-sharp-interfaces));
- the surface has drifted 3e-15 m;
- the water above the surface cell is exactly zero;
- water mass has drifted 1.9e-17.

This gate found two sharp-interface defects that leaked trace water upward through
the air ([transport](MAC_TRANSPORT.md#sharp-gasliquid-interfaces-selector-3)).

**M2.** The front Z = x/a on the floor is compared with Martin & Moyce (1952)
at simulated T = T_data − 0.175. The experiment's front lags its gate release;
this is the time-origin shift of Lethe's validation, adopted by user decision on
2026-10-19. Without it the front leads the data by up to 18.8% early on.

| T (data) | 0.84 | 1.19 | 1.43 | 1.82 | 2.20 | 2.50 | 2.82 | 2.96 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Data | 1.23 | 1.44 | 1.67 | 2.11 | 2.56 | 3.00 | 3.44 | 3.67 |
| 64 cells | 1.27 | 1.55 | 1.78 | 2.21 | 2.68 | 3.09 | 3.55 | 3.76 |
| 32 cells | 1.27 | 1.55 | 1.77 | 2.17 | 2.63 | 3.02 | 3.48 | 3.69 |

- **Front:** the worst error is 7.6% (bound 10%).
- **Grid agreement:** the two finest grids agree within 2.1% (bound 3%).
- **Interface:** the band, counting cells 1–99% full across the interface, is at most
  3 cells wide.
- **Mist:** liquid in cells under 1% full is at most 7e-5 of the water. A patch of
  0.1–0.5% full cells briefly forms near the column's upper corner around T ≈ 1.3.
- **Conservation:** water mass drifts by at most 6e-18.

**M3.** The period converges to linear theory, and the amplitude
change is within ±2% per period on the finest grid:

| Cells per wavelength | 32 | 64 | 128 |
| --- | ---: | ---: | ---: |
| Period error | 0.38% | 0.17% | 0.12% |
| Amplitude change per period | −3.6% | −0.71% | −0.23% |

Before the [sharp-interface gravity weight](MAC_PRESSURE.md#gravity-with-sharp-interfaces),
the wave grew +0.9%, +6.6% and +8.8% per period. The growth was proportional to the
time step (6.6%, 3.3% and 1.4% at 64 cells with limits of 0.004, 0.002 and 0.0005 s):
a start-of-substep face weight is first order and anti-damping. It was also larger
with mean-density face weights, which put half of a thin liquid layer's weight on
the gas face above it. Time-centring a geometric weight removes both effects.

**V.** The front with 0.1% of the gas mass beyond it is compared with the exact fan.
At N512 it is within 4.2% of its exact travel and moves within 0.32% of its exact
displacement over the last 0.2t; both errors decrease over N128 and N256. The last
gas-bearing cell stays behind the exact front. The gate first used that last cell
as the front, which no grid method can bring within 5% at N512. Details, and the
remaining fast-front error, are in [vacuum](MAC_VACUUM.md#gate-v-vacuum-front).

**M4.** Water-like (1000 kg/m³) over lighter (800 kg/m³) liquid, 64 cells per
0.1 m wavelength, perturbation 1e-5 m, with sharp interfaces: a cell holding exactly
two liquids and no gas places the denser below a PLIC line
([transport](MAC_TRANSPORT.md#sharp-gasliquid-interfaces-selector-3)).
- The unstable growth rate is 8.01/s, against √(A g k) = 8.28/s (3.2% low).
- A flat stable interface stays at rest within 1.5e-12 m/s.
- At an amplitude of 5% of the width, the band of cells holding 1–99% heavy
  liquid is at most 6 cells deep per column. Each liquid's share in cells where it
  is under 1% is 2.0e-5. When the liquids blended within a cell, these were 9 cells
  and 6.9e-5, with a growth rate 4.0% low.

**M5.** A Stefan vapour film at a wall 10 K above saturation, started from the exact
solution at t0 = 1 ms, with [sharp-interface boiling](MAC_PHASE_CHANGE.md#sharp-interfaces).
The film's steam volume per unit area follows 2λ√(αt) over [t0, 10 t0]:

| Cells across δ(t0) | Worst error | Water mass drift |
| ---: | ---: | ---: |
| 10 | 1.36% | 1.0e-16 |
| 20 | 0.87% | 6.4e-17 |
| 40 | 0.73% | 8.9e-17 |

Every interval passes the kernel's energy and species audits, latent heat included.
The errors approach an offset of about 0.5–1% rather than zero: the analytic
solution keeps steam at its saturation density, while the model's hotter steam
near the wall fills more volume ([details](MAC_PHASE_CHANGE.md#gate-m5)).

## Reference oracle on liquid traces

Trace constituents can decay to subnormal amounts. In still water, helium and air
traces in the water do so within a few intervals. The strict reference path rejects
storing them ("MAC primary storage rounding exceeds its operand bound"), while the
production kernel counts that rounding. Separately, a liquid trace below about 1e-320
kg has a nominal volume below binary64 range, and the reference EOS rejects it
("Positive thermodynamic quantity underflowed double range"). Blended liquids reach
this; the M4 measurement therefore uses nominal volumes, not EOS evaluations. Other
code relies on "positive mass, positive partial volume", so this boundary is left
unchanged and recorded here.
