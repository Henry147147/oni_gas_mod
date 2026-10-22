# Base-game accuracy floor

User decision received 2026-10-22; definitions recorded before measurements.
This protocol applies to saved **game profiles**, including scratch feature
variants. It changes no existing physics gate. Only the user can change these
setups or metrics after measurements begin. Results below are partial; the
opt-in game profile has not passed the floor.

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
| F1 | Equalises [55.8, 56.6] s across three runs; final L/R/divider fractions about 0.47145/0.46973/0.05882 | Final fractions 64/136, 64/136, 8/136; dynamic truth unresolved | Equalises [5.6, 5.8] s; fractions 0.46793/0.47233/0.05975 | Divider error about 9.26e-4 versus vanilla below 7e-6: **fails**. Timing unresolved. |
| F2 | Closed leakage 0; open equalisation censored at 120 s | Closed leakage 0; dynamic truth unresolved | Closed leakage 0; equalises [0, 0] s after fully open | Leakage matches truth; timing unresolved. Command-to-open delay 3.0 s in both. |
| F3 | Levels [68.8, 69.0] s; final depth 2.0000003 m | Nominal depth 2 m; phase/compression correction and dynamic truth unresolved | Levels [7.4, 7.6] s; final depth 1.999846 m | Nominal depth errors 3e-7/1.54e-4 m; correction still needed. No floor pass claimed. |
| F4 | pending | pending | pending | unmeasured |
| F5 | Settling censored; oil below water; depths water/oil 4.0000004/2.000003 m | Water below oil; nominal depths 4/2 m; dynamic truth unresolved | Faults at 7 s on an endpoint range check | No arena floor result; captured-frame recovery under investigation. |

The game rows use opt-in profile 17, GPU rendering and the low CPU lock;
`1a7bcf9` restricts ownership to the fixed arena rectangle. The recorded F1
release has all 136 interior cells open; the earlier 126-owned count was
before arena construction. F1 mass/energy errors are 3.5e-17/2.5e-16, sink zero;
F2 errors are 6e-18/1.4e-16; F3 sink is 4.1e-10 kg. F1 ends with left/divider/right
pressures about 47519/48636/48152 Pa: residual sloshing, rather than mass loss,
contributes to its split error. Its temperatures are 293.804/294.383/294.947 K.

Earlier whole-asteroid-owned F1/F2/F3/F5 runs at `eb42f8e` all finished without
faults or skipped relaxations (mass/energy errors about 2e-15/5.5e-17;
whole-colony sinks about 2.3e-7 kg / 0.075 J). Their F1 divider error was still
4.76e-4, and F5 settled [9.8, 10.0] s with water below oil, depths
3.999944/2.000000 m and zero overlap. These are historical stability results;
they do not replace the arena measurements or establish floor acceptance.
Vanilla starts use the same catalogue and placed fields. F1 initially holds
42.5673 kg O2; F2 106.418 kg O2; F3 40001.84 kg water.

The strict 8× F1 reference admits its first 0.0005 s interval (six accepted
steps, seven rejections). Initial F2/F3/F5 admission probes reached a 120 s
process limit; no resolved trajectories or event times are established.
The matching reference solid heat model for F4 is still absent. These limits
leave the affected truth metrics unresolved under the original protocol.

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

The following Terra60 trials used fresh colonies, GPU rendering, fast speed
and the whole CPU lock with the same managed observer. The selected numerical
baseline was profile 17 at `3032412`: owner mean **287.2 ms**, observed mean
**366.7 ms**. Separate private tolerance/cadence trials below have not passed
the floor and are not adopted. These are single-run comparisons with timing
noise, not accepted feature configurations. Observed means include the initial
UI hand-off gap. Every row completed 60 frames without a fault or skipped
relaxation, with closed mass/energy ledgers.

| Scratch-only feature variant | Owner / observed mean ms | Owner change | Reference cost and player effect | Floor |
| --- | ---: | ---: | --- | --- |
| Barotropic pressure in full gas cells; physical EOS retained at vacuum fronts and in liquids/mixed cells | 1641.3 / 1789.1 | +471.5% | Hot bulk gas pressure is capped at its 293.15 K force law; thermal buoyancy changes. Original caloric entropy criterion is reported but disabled for this changed law. This is a scoped diagnostic, not a validated full pressure replacement. | Unresolved |
| Fixed-temperature transitions; signed latent reservoir | 790.8 / 921.8 | +175.4% | Boiling ignores pressure; latent heat leaves fluid U through a named saved account, +5.774 MJ at frame 60. Newly created minority liquid makes subsequent flow expensive. | Unresolved |
| Donor-cell liquid transport, density layering retained | 402.7 / 509.4 | +40.2% | Liquid fronts smear; gas reconstruction, momentum and density settling remain. | Unresolved |
| Liquid sound speed ×0.2 (bulk modulus ×0.04), gas EOS unchanged | 268.1 / 349.8 | −6.6% | Liquid waves slow and liquids compress more; initial arena fill masses remain identical to the baseline. | Unresolved |

The original pressure variant faulted before completing a frame; the scoped
variant above made 157141 rejected attempts. A limited-linear smeared variant
was also slower (419.8 ms owner). Restricting fixed-temperature nucleation to
bulk phases did not help (803.1 ms owner, latent reservoir +3.996 MJ).
No feature variant is pushed or selected by the game. Quantitative resolved
reference errors and F1–F5 checks remain open; none is claimed to meet the floor.

| Run | Maximum relative mass / energy error | Trace sink kg / J |
| --- | --- | --- |
| Numerical baseline | 3.95e-16 / 4.24e-17 | 3.694e-8 / 0.01304 |
| Scoped barotropic pressure | 4.71e-15 / 3.20e-17 | 1.935e-8 / 0.005709 |
| Fixed-temperature transitions | 4.43e-16 / 4.25e-17 | 3.444e-8 / 0.01301 |
| Donor-cell liquids | 1.26e-15 / 2.74e-17 | 7.167e-8 / 0.06031 |
| Reduced liquid sound speed | 4.56e-16 / 2.41e-17 | 3.422e-8 / 0.01258 |

Private numerical trials, under the same whole-lock Terra60 protocol:

| Trial | Owner / observed mean ms | Cost against the reference | Floor / adoption |
| --- | ---: | --- | --- |
| Pressure root tolerance 1e-8, geometric conservative pressure work | 271.4 / 353.7 | Root criterion loosened from 16 epsilon; quantitative field errors pending. Existing conservation/entropy checks retained. | Unresolved; scratch only |
| Paired phase relaxation at integer 1 s boundaries | 274.0 / 363.1 | Up to 0.8 s extra phase lag; fixed-U equilibrium retained. Field errors pending. | Unresolved; scratch only |
| Maximum step 0.2f s | 304.0 / 396.4 | One large trial per frame; more retries. Field errors pending. | Unresolved; scratch only |
| Maximum step 0.1f s, baseline replicate | 284.8 / 373.3 | Current game step, two aligned trials per frame | Control |
| Maximum step 0.05f s, finer control | 296.0 / 378.8 | Four aligned trials per frame | Control |

Their maximum relative mass/energy errors are below 5.9e-16 / 4.9e-17;
trace totals are 2.82e-8–3.77e-8 kg / 0.01155–0.01350 J. All complete without
faults or skipped relaxations. None reaches the fast-speed target, and these
ledger passes do not establish accuracy-floor acceptance.

An exact energy-ledger optimization (`850330d`) reuses each cell's mass and
reads binary64 momentum directly when no canonical sidecar exists. Summation
order is unchanged. Alternating high-lock audits on two captures improve
6.26–9.20 ms to 2.29–3.07 ms; full-sidecar audits improve 6.67–7.25 ms to
3.50–3.15 ms. Fresh matched whole-lock Terra60 pairs give:

| Requested speed / revision | Owner mean / p95 / max ms | Observed mean / p95 / max ms | Maximum mass / energy error | Trace sink kg / J |
| --- | ---: | ---: | --- | --- |
| Fast, parent | 292.3 / 416.0 / 755.5 | 378.6 / 546.6 / 2882.8 | 3.04e-16 / 4.47e-17 | 3.553e-8 / 0.01254 |
| Fast, optimized | 283.1 / 465.0 / 510.4 | 365.5 / 545.7 / 2863.2 | 2.86e-16 / 4.25e-17 | 3.549e-8 / 0.01290 |
| 1×, parent | 278.2 / 444.4 / 537.9 | 359.6 / 566.7 / 2903.7 | 3.54e-16 / 3.62e-17 | 3.478e-8 / 0.01238 |
| 1×, optimized | 290.5 / 520.5 / 597.8 | 379.6 / 627.0 / 2962.8 | 3.55e-16 / 4.15e-17 | 3.540e-8 / 0.01260 |

All four runs have no faults or skipped relaxations. Observed intervals include
the initial UI hand-off; Claude's managed build overlapped the parent fast
startup, before measured owner frames. Frames 10–60 at fast speed improve
owner/observed means 276.7/319.5 to 271.1/314.6 ms. The component gain is clear;
the frame gain is small and noisy, and the 1× pair regresses. The fast clock
does not hold 2×. No numerical setting changed.
