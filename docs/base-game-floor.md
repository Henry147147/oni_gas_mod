# Base-game accuracy floor

User decision received 2026-10-22; definitions recorded before measurements.
This protocol applies to saved **game profiles**, including scratch feature
variants. It changes no existing physics gate. Only the user can change these
setups or metrics after measurements begin. No floor results are available yet.

## Common setup and measurements

Coordinates below are local, zero-based cell coordinates; rectangles include
both endpoints. Cells are 1 m square, with 1 m depth. The outermost row and
column are solid Unobtanium, initially 293.15 K. Unspecified interior cells are
vacuum. There are no duplicants, debris, buildings or sources except the F2
door. Isolate the arena from the colony. Use the same runtime material
catalogue for both simulations and the offline oracle; do not commit catalogue
values or private starting worlds. Set zero initial velocity and 293.15 K fluid
temperature except where stated. Mod gravity is 9.81 m/s² downward. Vanilla
retains its own simulation. No manual exchanges occur after release.

For a gas region, the pressure metric is its volume-average ideal pressure
`sum(m_k R T / M_k) / V`, including empty cells in V. It is computable in
vanilla without a pressure field. Initial oxygen mass is chosen from this
equation at the stated pressure; 1 atm means 101325 Pa. For liquid fills,
one full cell means the mass occupying 1 m³ at 293.15 K and 1 atm in the
catalogue/reference EOS. Use exactly those masses in vanilla too. Record actual
initial mass and heat totals as numbers with each result.

Sample every 0.2 simulation seconds, including time zero. Report threshold
crossings as brackets between samples, not more precise interpolated times.
An event must persist for the next five samples (1 s); its time is the first
sample in that sequence. Gas, door, levelling and layering runs last 120 s
after release; F4 lasts 20000 s. Report an unreached event as censored, never
as a passing result. Final metrics use the last 1 s average. Also record mass,
energy and momentum ledgers, trace-sink totals, faults and fallbacks.

Measure vanilla once; repeat three times if a repeat changes an event's sample
bracket. Run the mod and vanilla from the specified initial fields, not from
each other's endpoint. Keep only vanilla measurement numbers; do not retain
vanilla saves, screenshots or captures. The owner is disabled throughout
vanilla runs: ONI's native simulation is the measured subject, never the mod's
solver. Timed runs take the shared CPU/game locks.

## Fixed scenarios

| ID | Initial geometry and release | Metrics |
| --- | --- | --- |
| F1 gas into vacuum | 19 × 10 grid. Left room x=1..8, y=1..8 contains 1 atm O2; right room x=10..17, y=1..8 is empty. The solid divider x=9, y=1..8 is removed at t=0, adding eight empty cells. | Time both original rooms' mean pressures stay within 5% of the analytic final pressure; final mass fraction in each original room and in the divider. |
| F2 door | Same grid/rooms as F1, with left 2 atm and right 0.5 atm O2. Divider remains solid except a closed **manual airlock** at x=9, y=4..5. Hold closed for 10 s, then command fully open; t=0 is the first simulation sample after it is fully open. | Net mass crossing into the right room during the closed hold, divided by initial total mass; time after opening both room pressures stay within 5% of the analytic final pressure. Report command-to-open delay separately. |
| F3 levelling | 22 × 14 grid. Water fills x=1..5, y=1..8, initially held by a solid gate x=6, y=1..8. At t=0 remove the gate. The basin spans x=1..20. | Time the highest and lowest column heights differ by at most 1 m; final mean depth and its absolute error from total nominal water volume / 20 m². Column height is the sum of water volume fractions, so mist cannot disappear from the metric. |
| F4 conduction | 7 × 4 grid. Left O2 room x=1..2, y=1..2 at 353.15 K; right room x=4..5, y=1..2 at 283.15 K. Each starts at 1 atm. Granite divider x=3, y=1..2 starts at 318.15 K, with the mass of a normal constructed granite tile. No opening; t=0 enables heat exchange. | Time the difference between mass-weighted room temperatures falls to 35 K; final mass-weighted temperature and error against the common temperature obtained from the initial total heat, including the divider. Record boundary heat exchange, if any, in that truth calculation. |
| F5 layering | 10 × 14 grid. Water fills x=1..8, y=1..4. Crude oil fills x=1..8, y=7..8, initially supported by a solid shelf x=1..8, y=6. At t=0 remove the shelf. | Final bottom-to-top layer order; each liquid's mean depth from nominal volume / 8 m²; settling time until the height below which 99% of water lies is no greater than the height below which 1% of oil lies, and the total liquid column heights differ by at most 1 m, persisting for 1 s. Report immiscible interface width as an additional, non-vanilla metric. |

For F1 the final uniform mass fractions are 64/136, 64/136 and 8/136.
For F2 use total gas moles and conserved heat over the 130 open cells, including
any booked heat exchanged with the door. F3's nominal final water depth is 2 m;
F5's nominal depths are 4 m water and 2 m oil. Compressibility/thermal-expansion
corrections are reported separately using the oracle, rather than hidden by
changing these volume metrics.

## Truth and comparison

Analytic inventory, sealed-door zero leakage, final gas split and caloric
equilibrium are the truth where applicable. Dynamic event times and liquid
layering use the resolved reference: unchanged gate physical/settings choices,
with the scenario geometry refined 8× in each spatial direction, maximum step
0.0005 s, exact subcell geometry, no trace sink and no game-profile shortcuts.
Run a 16× / 0.00025 s control as well and report their difference as reference
uncertainty; if the strict reference cannot advance the setup or the event is
censored, that metric remains unresolved. Do not silently substitute a game
profile as truth. Include the same solid/door heat exchange model in F4's
offline reference; absence of that model leaves its dynamic truth unresolved.

For each analogous metric report vanilla, game profile, truth and absolute
errors (seconds for timing, mass fraction for split/leakage, metres for depth,
kelvins for temperature). A game profile passes only if its error is no larger
than vanilla's, accounting explicitly for sampling/reference uncertainty.
Layer order is categorical and must match truth. Censoring or overlapping
uncertainty alone does not establish a pass. Gas/miscible-liquid mixing,
partial pressures and latent heat have no vanilla analogue: retain them and
report reference error for the user's judgment.

| Scenario | Vanilla | Truth | Game profile | Errors / floor result |
| --- | --- | --- | --- | --- |
| F1 | pending | pending | pending | unmeasured |
| F2 | pending | pending | pending | unmeasured |
| F3 | pending | pending | pending | unmeasured |
| F4 | pending | pending | pending | unmeasured |
| F5 | pending | pending | pending | unmeasured |

## Performance and feature variants

Each numerical step needs same-lock Terra60 before/after, affected floor
scenarios, ledgers and sink totals. Final acceptance uses a new Terra colony
for at least 60 frames at fast (2×) speed under the whole lock: mean observed
interval ≤0.1 s, closed ledgers, no faults/fallbacks; report owner mean and
observed mean/p95/max. Measure vanilla Terra at fast speed too. If it cannot
hold 2×, use its measured rate as the target. Confirm ten-cycle pace with the
same saved profile; old prepared saves retain their original profile unless
an explicit audited migration is separately implemented.

Measure all four variants below outside the repository, without pushing or
adopting them. Compare Terra60 against the best numerical profile under the
same lock, and check F1–F5. Retain momentum and inertia in every variant.

| Scratch-only feature variant | Terra60 gain | Floor / reference cost | Player effect |
| --- | --- | --- | --- |
| Simplified physical pressure | pending | pending | pending |
| Fixed-temperature transitions, latent energy booked | pending | pending | pending |
| Smeared liquid transport, density layering retained | pending | pending | pending |
| Low-Mach / reduced acoustic speed | pending | pending | pending |
