# Gravity and multiphase gates

Milestone B's gravity and multiphase exit is defined by eight gates
([completion plan](../docs/completion-plan.md#gravity-and-multiphase-gates)).
Each gate is a standalone executable that prints `GATE <id> PASS|FAIL|OPEN` with
its measurements and exits nonzero unless it passes. All run the production
kernel (numerical kernel 2) with default settings. The free-surface gates (M1–M3)
also select sharp interfaces (material selector 3). Passing gates also run as
CTests.

| Gate | Command | Status | CTest |
| --- | --- | --- | --- |
| G1 hydrostatic gas column | `ma_mac_gravity_gates G1` | pass | `mac_gate_g1_hydrostatic_column` |
| G2 gravitational species separation | `ma_mac_gravity_gates G2` | pass | `mac_gate_g2_species_separation` |
| M1 still water | `ma_mac_free_surface_gates M1` | pass | `mac_gate_m1_still_water` |
| M2 dam break | `ma_mac_free_surface_gates M2` | fail: front vs data | — |
| M3 standing wave | `ma_mac_free_surface_gates M3` | fail: amplitude grows | — |
| M4 two liquids | `ma_mac_multiphase_gates M4` | open: no immiscible interfaces | — |
| M5 phase change | `ma_mac_multiphase_gates M5` | open: no phase change | — |
| V vacuum front | `ma_mac_vacuum_front_diagnostic --gate` | fail | — |

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
- the maximum speed is 3.8e-9 m/s;
- the surface has drifted 3e-15 m;
- the water above the surface cell is exactly zero;
- water mass has drifted 1.2e-17.

This gate found two sharp-interface defects that leaked trace water upward through
the air ([transport](MAC_TRANSPORT.md#sharp-gasliquid-interfaces-selector-3)).

## Failing gates

**M2 dam break.** Front Z = x/a on the floor, finest grid (64 cells across a), against
Martin & Moyce (1952) at the same T = t√(2g/a):

| T | 0.84 | 1.19 | 1.43 | 1.82 | 2.20 | 2.50 | 2.82 | 2.96 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Data | 1.23 | 1.44 | 1.67 | 2.11 | 2.56 | 3.00 | 3.44 | 3.67 |
| 64 cells | 1.40 | 1.71 | 1.96 | 2.42 | 2.91 | 3.33 | 3.81 | 4.02 |
| 32 cells | 1.39 | 1.69 | 1.93 | 2.37 | 2.85 | 3.27 | 3.73 | 3.94 |

The worst error is 18.8% (T = 1.19), against a 10% bound.
- **Passing criteria:** the two finest grids agree within 2.1% (bound 3%), the
  interface band is 3 cells wide (bound 3), and water mass drifts by at most 2e-17.
- **Why the front leads:** the simulation is inviscid with free-slip walls, while the
  experiment started by removing a gate. Front data from this experiment are commonly
  compared with a time-origin shift; Lethe's validation adds 0.175 to T. With that
  shift the finest grid is within 2.1–6.4% of the data wherever the shifted time
  stays inside the measurements (T ≤ 2.64).
- **Open decision:** the gate as defined applies no shift.

**M3 standing wave.** The period converges to linear theory:

| Cells per wavelength | 32 | 64 | 128 |
| --- | ---: | ---: | ---: |
| Period error | 2.4% | 0.61% | 0.052% |
| Amplitude change per period | +0.9% | +6.6% | +8.8% |

The amplitude grows, so the run fails.
- **Not the regularisation:** with shock heat and compression viscosity off, the
  amplitude changes by −1.2%, +5.4% and +10.7% per period.
- **Not only face crossing:** moving the mean surface to mid-cell (no face crossing)
  still grows the wave 4.9% per period at 128 cells.
- **Where the energy comes from:** the energy budget shows the gravity
  reconciliation removing about 0.01 J/m of U every half period. That is about four
  times this wave's energy (2.5e-3 J/m). It returns as mechanical energy, most of
  which is dissipated again as mixing heat (0.047 J/m in three periods).
- **Cause:** sharp mass fluxes combined with mean-density face inertia and weight.
  The face above a thin liquid layer carries half of that layer's weight and
  inertia, although the liquid rests on the face below. Raising and lowering the
  surface therefore do not balance, and each cycle feeds the wave.
- **Fix:** this is the known consistency requirement between mass and momentum
  transport for VOF methods at large density ratios. Free surfaces need face mass
  and weight consistent with the reconstructed interface.

**V vacuum front.** The N128 run stops with "Compressive actual stored-state
entropy falls below the original stored-U allowance" ([vacuum](MAC_VACUUM.md)).
Front tracking into vacuum needs a void phase.

## Open gates

**M4.** Water-like (1000 kg/m³) over lighter (800 kg/m³) liquid, 64 cells per
0.1 m wavelength, perturbation 1e-5 m. The liquids still blend within a cell, so
the gate stays open even though the measured numbers meet the criteria:
- the unstable growth rate is 7.81/s, against √(A g k) = 8.28/s (5.6% low);
- a flat stable interface stays at rest within 1.1e-12 m/s.

**M5.** It reports open until phase change exists.

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
