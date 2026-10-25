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

**Amendment 2026-10-24** (user decisions, [completion plan](completion-plan.md)):
the strict reference cannot advance these setups at practical cost (an 8× F1
run refuses after 2.5 ms; F3/F5 at the first interval). A timing metric passes
when the game profile's error is no larger than vanilla's for every truth in a
documented, physically justified range; where the range does not decide it, an
independent second-order Godunov Euler solver (gas) or incompressible VOF
solver (liquid) at 8× and 16× is that metric's truth. F3/F5 depth truth is the
nominal depth minus the saturated water vapour in the vacuum headspace, from
the catalogue at the final temperature of the closed caloric balance: F3
1.999779687 m (4.406 kg vapour, 293.095 K), F5 water 3.999867527 m (1.060 kg);
crude oil has no vapour, so its truth stays 2 m. Setups, metrics and other
bounds are unchanged.

The optional game release capture is taken after terrain edits, before flow.
In F1 the owner's opening helper has already moved 6.25% of the gas into the
divider; F3/F5 captures likewise include pre-release evolution. These are
intermediate replay states, not the frozen initial world. Reconstruct oracle
initial fields directly from this protocol and the private runtime catalogue,
including the actual binary32 placed masses/temperatures and zero initial
velocity. Do not carry the game opening prefix into the truth. Earlier
capture-based admission probes establish execution only, not a resolved floor
trajectory. No such trajectory has been accepted.

For each analogous metric report vanilla, game profile, truth and absolute
errors (seconds for timing, mass fraction for split/leakage, metres for depth,
kelvins for temperature). A game profile passes only if its error is no larger
than vanilla's, accounting explicitly for sampling/reference uncertainty.
Layer order is categorical and must match truth. Censoring or overlapping
uncertainty alone does not establish a pass. Gas/miscible-liquid mixing,
partial pressures and latent heat have no vanilla analogue: retain them and
report reference error for the user's judgment.

The event reader now derives a lower bound for an unreached vanilla event,
including any final true samples which still lack their five-sample persistence
confirmation. The bound can precede the observation endpoint. Given an
independently justified truth range, comparison may prove that the candidate's
worst error is no larger than vanilla's minimum possible error throughout that
range. Vanilla remains censored; no event time is assigned to it. A censored
candidate, or a legacy censored record without this explicit bound, remains
unresolved.

The historical arena rows below are **invalid as floor evidence**. The harness
left plants, seeds and critters inside the arenas, contrary to the specified
empty setup. Their native heat exchange supplied 37–47 kJ in F1. Both vanilla
and owner measurements must be repeated after the fixture removes those bodies;
grids, fluid fills, metrics and bounds stay fixed. F4 has not been measured.

| Scenario | Vanilla | Truth | Game profile | Errors / floor result |
| --- | --- | --- | --- | --- |
| F1 | Equalises [55.8, 56.6] s across three runs; final L/R/divider fractions about 0.47145/0.46973/0.05882 | Final fractions 64/136, 64/136, 8/136; dynamic truth unresolved | Equalises [5.6, 5.8] s; fractions 0.46793/0.47233/0.05975 | Historical divider errors about 9.26e-4 / below 7e-6; invalid fixture. |
| F2 | Closed leakage 0; open equalisation censored at 120 s | Closed leakage 0; dynamic truth unresolved | Closed leakage 0; equalises [0, 0] s after fully open | Historical leakage zero and command-to-open delay 3.0 s in both; invalid fixture. |
| F3 | Levels [68.8, 69.0] s; final depth 2.0000003 m | Nominal depth 2 m; phase/compression correction and dynamic truth unresolved | Levels [7.4, 7.6] s; final depth 1.999846 m | Historical nominal errors 3e-7/1.54e-4 m; invalid fixture and unresolved correction. |
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
they do not establish floor acceptance and also require the fixture correction.
Vanilla starts use the same catalogue and placed fields. F1 initially holds
42.5673 kg O2; F2 106.418 kg O2; F3 40001.84 kg water.

The strict 8× F1 reference admits its first 0.0005 s interval (six accepted
steps, seven rejections). Initial F2/F3/F5 admission probes reached a 120 s
process limit; no resolved trajectories or event times are established.
The matching reference solid heat model for F4 is still absent. These limits
leave the affected truth metrics unresolved under the original protocol.

### F4 isolated native completion probe

The explicit `NativeShadowProbe floor-f4` mode runs the licensed native
simulation with no mod owner or observer loaded. It copies all 28 binary32
material/mass/temperature tuples from the Unity-proved `MAFLRQ01` admission
request, verifies the actual initial native arrays, and samples 100,001 times
over 20,000 nominal seconds. Its disposable 64×64 embedding differs from
Unity's world coordinates and partitions: this is numerical comparison
evidence, **not a completed aligned Unity floor measurement**. The frozen
7×4 arena, inputs, sampling and metrics are unchanged. No world snapshots,
saves or screenshots are written.

The initial oxygen and granite masses are 4.962862968444824 kg and 400 kg;
the boundary has 360,000 kg Unobtanium with zero runtime heat capacity.
Initial heat is 102,103,025.81511155 J and total heat capacity is
320,987.68584269116 J/K, giving a common caloric temperature of
318.0901645714532 K. The native timestep is binary32 0.2 s; nominal sample
times retain the fixed 0.2 s protocol.

| Metric | Isolated native result | Limit |
| --- | --- | --- |
| Persistent 35 K room-difference crossing | [1.4, 1.6] s | Needs Unity alignment and resolved dynamic truth |
| Final hot/cold mass-weighted room temperatures | 318.9563369751 / 317.1917190552 K | Last 1 s average |
| Final fluid mass-weighted temperature | 317.9769643298 K | Absolute common-temperature error 0.1132002417 K |
| Final / maximum absolute heat drift | +18.7065483 / 20.9790360 J | Native rounding is measured, not booked away |
| Maximum mass drift | 0 kg | Across the complete arena |

The run completes all steps with no native diagnostic errors. It does not
supply a mod-profile result or fill F4's missing resolved solid/fluid model.

### Corrected fixture measurements

The rectangle-opening and repeated object removal in `0c347d9` remove the
buried hatch that supplied the earlier F1 heat. The following runs use that
corrected fixture (`0c347d9`/`13ebb2c`), the same frozen setups, the low CPU
lock and GPU rendering. Owner rows use saved profile 17. F1 vanilla was
repeated three times; the event bracket was identical. These are baseline
measurements, not acceptance of profiles 18 or 19.

| Scenario | Vanilla | Profile 17 | Truth comparison / status |
| --- | --- | --- | --- |
| F1 | Equalises [56.4, 56.6] s; final L/R/divider fractions about 0.47148–0.47152 / 0.46966–0.46969 / 0.0588233–0.0588264 | Equalises [9.0, 9.2] s; fractions 0.470084 / 0.470615 / 0.059330 | Against the analytic final fractions, owner divider error 5.06e-4 exceeds all three vanilla errors (1.07e-8–2.91e-6). This split metric does not meet the floor. Dynamic truth remains unresolved. |
| F2 | No closed leakage; opens in 2.8 s; equalisation censored at 120 s | No closed leakage; opens in 3.0 s; equalises at the first open sample | Locked initial room masses and temperatures match. Heat at release differs because the hold and opening exchange heat with the door; include that exchange in dynamic truth. |
| F3 | Levels [68.8, 69.0] s; mean depth 2.0000003 m | Levels [7.6, 7.8] s; mean depth 1.999848 m | Nominal depth errors 3.02e-7 / 1.52e-4 m; compression/thermal correction and dynamic truth remain unresolved. No floor pass. |
| F4 | Interrupted by display loss before its fixed 20000 s duration | Unmeasured | No complete conduction measurement; rerun both modes. |
| F5 | Settling censored at 120 s; crude oil below water; depths water/oil 4.000000 / 2.000000 m | Settles at the first sample; water below crude oil; depths 3.999946 / 2.000000 m; interface overlap zero | Owner layer order matches physical truth. Depth corrections and resolved settling time remain unresolved. |

F2's initial fields at the start of the closed hold are identical; the
release-sample heat is not the initial setup. Owner F1/F2 sinks are zero;
F3 books 5.16e-10 kg / 8.71e-4 J and F5 2.15e-9 kg / 1.74e-3 J.
Maximum relative mass/energy errors across these owner runs are
2.03e-15 / 8.25e-17, with no faults or skipped phase relaxation.

Temperature samples come from binary32 game fields. The metrics reader
reconstructs that original value for legacy short float-round-trip text;
new exports print the exact widened value and declare `temperature_storage`.
This fixes an apparent sensible-heat offset without changing the arena or
metric. In the fresh profile-18 F1 run, initial O2 mass agrees with vanilla
within 7.1e-15 kg and first-sample heat within 0.00048 J of 12.541 MJ. At
release, after the fixed closed hold, the sensible-heat difference is about
+0.141 J. Account for kinetic/gravitational energy and publication precision
before treating a release-sample sensible-heat difference as a starting-input
mismatch. F1's divider split still fails: error 5.059e-4, versus vanilla's
at most 2.91e-6. Profile 18 equalises at [9.0, 9.2] s with no fault or skipped
relaxation; mass/energy errors 1.16e-17 / 1.99e-17 and trace sink
1.393e-10 kg / 0.003834 J. F2–F5 profile-18 measurements remain open.

### Sampler integrity audit, 2026-10-23

Earlier owner rows above used one composition request per cell; a completion
could occur during the row. Treat those dynamic measurements as provisional.
The presentation reading table is also unsuitable for a complete inventory:
a refused reference-EOS projection keeps the previous visual value and omits
its reading, while its material remains in the physical world.

The owner exporter now samples every requested composition, the binary32
projected temperatures and source audit counters from one immutable completed
world. It records tick/frame/time, refused projection cells and the full
ledger's expected fluid-plus-reservoir mass. Solid temperatures still come from
the native published update. `floor_metrics.py` reports repeated/skipped owner
frames, refusal counts and the maximum sampled inventory residual against that
ledger. Geometry, placed inputs, event definitions and acceptance bounds are
unchanged; no floor pass follows merely from sampler integrity.

The public queued-input mass counters exclude identity-conversion transactions.
Do not infer a mass loss from their partial balance, or add the gross
`ConvertedKg` counter as though every conversion removed mass. The F3 audit
found about 14 micrograms of conversion activity; the complete ledger includes
the corresponding extraction/product entries. The synthetic solid-byproduct
control verifies this accounting separately from the scenario measurements.

Fresh profile-18 F3 with the complete sampler records 605 samples, no refused
projections, and maximum sampled fluid-plus-reservoir residual **7.276e-12 kg**
against the full ledger. Levelling is bracketed at **[7.2, 7.4] s**, final nominal
depth **1.999841353 m**, trace sink **6.019e-9 kg / 0.007721 J**, and maximum
relative mass/energy errors **5.542e-16 / 5.432e-18**. Three repeated and three
skipped owner-frame observations are explicit in the record. There is no fault
or skipped phase relaxation. Dynamic resolved truth remains pending.

Fresh profile-18 F1 with the same complete sampler records 605 samples, no
refused projections, and maximum full-ledger inventory residual **7.105e-15 kg**.
Initial O2 is **42.567310333251946 kg**, with sensible heat
**12540999.879670504 J**, matching the clean vanilla inputs at the precision
described above. Final L/R/divider mass fractions are
**0.470143180159 / 0.470527356652 / 0.059329463190**. Divider error
**5.05934e-4** still exceeds vanilla's **2.91e-6** maximum: this analytic split
metric **fails**. Maximum mass/energy errors are **1.224e-17 / 1.883e-17**,
trace sink **2.698e-12 kg / 3.167e-6 J**, with no fault or skipped relaxation.
The observed row-time equalisation bracket remains [9.0, 9.2] s. Five repeated
and five skipped owner observations are recorded; source-aligned cadence and
resolved dynamic truth remain open, so this bracket is provisional. Complete
inventories establish mass accounting, not complete dynamic floor acceptance.

The floor-only owner sampler now waits for already submitted owner work before
reading that tick's atomic snapshot. A fixed F1 rerun records **605 consecutive
owner frames**, with **zero repeats, skips or refused projections**. Initial
gas mass and sensible heat, the [9.0, 9.2] s equalisation bracket and all three
final mass fractions match the preceding run exactly. Maximum inventory
residual remains **7.105e-15 kg**. Maximum mass/energy errors are
**1.126e-17 / 1.724e-17**, trace sink **2.686e-12 kg / 3.153e-6 J**, with no
fault or skipped relaxation. This corrects F1's sampled cadence; its analytic
split failure and unresolved dynamic reference remain. Other owner scenarios
still require the barrier rerun. Vanilla sampling and all fixed setups,
physical settings and metric definitions are unchanged.

The wait-only F2 rerun still recorded four repeat/skip pairs. The completed
sampler therefore retains up to 64 immutable owner frames for these small
arenas and drains each frame exactly once on its source clock. Missing history
or a non-0.2 s interval fails the diagnostic. Gate/door release is stamped from
the sampled owner geometry. Normal games retain no history; saves and physics
are unchanged. F4 kept its existing path for those measurements because its
native solid temperatures lacked an aligned source; they establish no conduction
result.

The first retained F2 run has **669 consecutive samples**, zero repeats, skips
or refused projections, and maximum full-ledger inventory residual
**1.421e-14 kg**. Closed leakage is **zero**, command-to-open delay **3.0 s**,
and the release-sample pressure test holds at the first open sample. Maximum
mass/energy errors are **1.413e-17 / 8.347e-18**, trace sink
**2.697e-12 kg / 3.165e-6 J**, with no fault or skipped relaxation.

Retained samples also expose cumulative **accepted external heat** from the
frame ledger. The old published `native_heat_j` field was not that ledger and
remained zero. F2 accepts **159654.733 J after release** through the endpoint;
this must enter the door-heat truth calculation. The existing pressure target
uses the release snapshot, so its post-release heat/kinetic correction remains
unresolved along with the dynamic oracle. No dynamic floor pass follows.

New retained-frame exports also include U, K, G, formation energy, Cv capacity
and the accepted heat/trace accounts from the composition's exact session/frame.
Primary-U remainders and canonical face momentum enter these sums before their
final binary64 observations. F2 analysis uses one final energy budget, including
heat accepted after release and kinetic energy released into heat. It bounds
equilibrium G by the fixed arena heights and reports event brackets valid for
every final pressure in that range; ambiguity stays unresolved. The target is
constant through the event search. Older records lack this energy observation
and retain their explicit unresolved release-sample estimate. The profile5
measurement below supplies the first live capture with this energy account.

The retained F1 release-stamp control also records **605 consecutive frames**,
zero repeats/skips/refusals and residual **7.105e-15 kg**. Initial mass/heat,
the [9.0, 9.2] s bracket and all final fractions match the earlier F1 runs.
The release stamp now identifies the interval before the first fully open
completed owner frame, rather than the submitted game clock. Maximum
mass/energy errors are **1.210e-17 / 1.758e-17**, trace sink
**1.697e-11 kg / 0.000213 J**, with no fault or skipped relaxation. F1's static
split still fails; the F4 source model remains open.

The retained F3 rerun records **605 consecutive samples**, zero gaps or
refused projections, and full inventory residual **1.455e-11 kg**. Levelling
is bracketed at **[7.0, 7.2] s**; final nominal depth is **1.9998392342 m**
(error **0.0001607658 m**). Maximum mass/energy errors are
**5.266e-16 / 5.120e-18**, trace sink **6.9998e-9 kg / 0.01002245 J**,
with no fault or skipped relaxation. Accepted external heat spans **1.285 J**.
Dynamic truth and the compression/heat correction remain unresolved; this
establishes reliable cadence and accounting, not a floor pass.

The retained F5 rerun also records **605 consecutive samples**, zero repeats,
skips or refused projections, full inventory residual **7.276e-12 kg** and
accepted external heat span **zero**. Water settles below crude oil in
**[6.8, 7.0] s**, with depths **3.9999357643 / 2.0000000192 m** and no interface
overlap. Maximum mass/energy errors are **3.299e-16 / 4.897e-18**, trace sink
**2.218e-9 kg / 0.00080854 J**, with no fault or skipped relaxation and verified
process cleanup. Native/owner phase and compression corrections plus resolved
dynamic truth remain open; the reliable sampler is not a floor acceptance.

### Profile 4 measurements, 2026-10-24

The later immutable `solver-p4-bfd8-v1` bundle selects core22 with saved gas
heat mixing8. Its `p4a` Linux runs complete the frozen 120 s arenas with no
sample gaps, refused projections, owner faults or skipped relaxations.
These supersede profile18's measurements for the candidate profile; they do
not establish the complete floor.

| Arena | Profile4 measurement | Comparison still needed |
| --- | --- | --- |
| F1 | Equalisation [1.4, 1.6] s; L/R/divider fractions 0.470600362761 / 0.470576115197 / 0.058823522042 | All three analytic split errors improve on the recorded clean vanilla measurements. Independent gas timing probes reach [0.4, 0.6] s at both 8× and16×, but ran only20 s; a justified timing range and provenance remain to be recorded. |
| F2 | Closed leakage zero; command-to-open3.0 s | Pressure timing still uses the release-sample heat. Accepted external heat and kinetic/gravitational energy must enter its truth before acceptance. |
| F3 | Levelling [5.8, 6.0] s; depth1.999787384350 m | Depth error7.697e-6 m against the amended1.999779687 m truth improves on vanilla. Independent VOF8×/16× events [48.4, 48.6] / [25.8, 26.0] s disagree enough to leave the timing comparison unresolved. |
| F5 | Settling [0, 0] s; water below oil; depths3.999909813768 / 2.000000019206 m; overlap zero | Both depth errors improve on clean vanilla: water4.229e-5 vs1.320e-4 m, oil1.921e-8 vs2.209e-7 m. The fixed predicate already holds at release; this is not a measured fall duration. The analytic timing comparison below applies to profile4; selected core23 still needs its own run. |

The F3 VOF probes run the full120 s from the released, stationary field and
report their clipped-volume accounts (5.78e-7 / 7.92e-7 m³). Their broad timing
range crosses the point where owner and vanilla have equal error; choosing
only its16× endpoint would manufacture a pass. They remain exploratory
reference evidence. Source, numerical methods and independent controls still
need a reproducible review before reference acceptance.

### Profile 5 F2 energy measurement, 2026-10-25

The immutable `codex-resume-floor-energy-f631-v1` package ran the frozen F2
arena to120 s after release. Its creation dump selects core23, liquid law1,
caps400/800 and gas heat mixing8; all18 package file hashes stayed unchanged.
The669 retained observations have no repeats, skips or refused projections.
Closed leakage is zero and command-to-open delay is3.0 s. Maximum owner
mass/energy errors are9.006e-17/4.034e-16, with zero trace sink, fault or skipped
relaxation. The player exited0 and the game-slot wrapper verified cleanup.

The fixed FINAL pressure truth is **[124848.6998,124877.7670] Pa**, including
**146243.590 J** release kinetic energy and **39104.430 J** heat accepted
after release through the final averaging window. The geometry bound on
equilibrium gravitational energy gives final temperature
**[293.4814613,293.5497896] K**. Both room pressures satisfy the fixed5% test
at **[0.2,0.4] s** for every pressure in that range. Their final means are
124860.0828/124866.2520 Pa. A physically justified timing range or resolved
independent door oracle and the corresponding vanilla truth calculation are
still needed; this measured event does not establish the F2 timing floor.

This capture exposed a read-only inventory export bug: the field labeled
`expected_fluid_and_reservoir_kg` actually returned the fluid-only ledger.
The original record remains unchanged. Analysis explicitly uses
`--legacy-fluid-only-booking`, reports that interpretation, and obtains a
maximum inventory residual4.263e-14 kg. The native reader now adds the
reservoir from the same immutable publication; new records explicitly name
their `fluid+reservoir` scope. A held-reservoir regression also checks that
the observation leaves checkpoint bytes unchanged. Solver arithmetic and
checkpoint formats are unchanged.

The **0.6498948433 kg** held reservoir stays isolated throughout the
post-release interval: fluid mass106.4182758331 kg, reservoir, sink and mass
input/output counters are constant. Every frame's whole-fluid U+K+G closes
against accepted heat/trace accounts within **2.201e-8 J**, at the binary64
observations' roundoff. This explicit closure permits the fixed held reservoir
outside the fluid energy budget; constant reservoir mass alone would not
establish an energy account.

The new F4 diagnostic retains native cell endpoints together with the heat-input
batch drained by the owner. A read succeeds only for one native update with the
publication's exact ticket and one owner interval; missing, failed, coalesced or
mismatched captures are refused. The history reader supplies both native granite
and owner fluids from that pairing and records U/K/G and accepted heat. Native
values remain binary32 observations. History-disabled games capture nothing;
checkpoint bytes and simulation order are unchanged. Synthetic controls cover
retained temperature epochs, heat pairing and rejection without output mutation.
Actual F4 admission/cadence and the full20000 s result remain to be measured.
The legacy vanilla t=0 row already has a61.25 K room difference after conduction;
its dynamic timing needs the initial-admission ticket before it can be compared
with the prescribed70 K input.

The additive88-byte region-energy reader also observes a sealed subset of a
larger owner at an explicitly requested latest session/frame. It includes
carried U and canonical internal-face K once, refusing open boundaries and
nonzero boundary J. It needs no history and leaves checkpoints unchanged.
Controls include an8192-cell owner with a separate excluded gas pocket and
opposed formation offsets with carried U remainders. Heat exchanged with native
bodies and stored-fluid energy remain separate accounts; this reader alone does
not establish their coupled conservation.

The recovered independent VOF reference fails a small-amplitude gravity-wave
control: its8×/16× zero crossings are both[3.2,3.4] s against the analytic
2.294215826 s from `omega^2 = g*k*tanh(k*h)` (20 m box,2 m depth,1 mm cosine;
see the [Basilisk dispersion control](https://basilisk.fr/src/test/dispersion.c)).
Using volume fraction minus0.5 as interface distance halves a small subcell
height perturbation in its pressure boundary condition. Scratch repairs use
the reconstructed PLIC planes and reflective fraction ghosts at walls. Both
controls then cross in[2.2,2.4] s; the flat pool retains exact reported depth
and volume, with K below3.4e-30 J. Original sources and failed results are
preserved. The repaired F3 full120 s8×/16× trajectories still need to run; the
old divergent events are not accepted timing truth.

The repaired8× trajectory subsequently completed120 s/601 records and240103
steps, with levelling[40.6,40.8] s and maximum volume-plus-clipping residual
2.361e-10 m³. The16× trajectory stalls beyond its1.8 s sample while its
reported mechanical energy rises to2.64 MJ from1.57 MJ initially. Its partial
output is preserved and rejected as reference evidence. Linear-wave success
does not establish strong-impact accuracy; an established independent VOF
implementation and bounded failure replay are being investigated.

The bounded F4 owner probe produced293 consecutive paired fluid/solid/energy
observations through owner frame300, with no gaps, refused projections or
inventory residual above2.665e-15 kg. It exposed an admission error: granite
was set at Build, two ticks before Fill, and had already cooled to
318.145111/318.146820 K when the prescribed gas arrived. The fixture now
resets it to the existing318.15 K at Fill. Native admission capture runs after
the owner writes its shadow, before native heat, and waits for both native
buffers to match the exact prescribed fields. The next bounded proof must
establish that origin before the full run. Earlier vanilla F4w1 numbers remain
legacy setup evidence pending a matched admission repeat.

F5's prescribed horizontal layers and reflective walls reduce the inviscid
sharp-interface truth to one dimension: water stays below oil and total column
heights agree. The amended1.060 kg water-vapour budget is far below1% of the
approximately32000 kg water, so it cannot reverse the bulk99%/1% ordering.
The fixed predicate therefore holds from release through its first1 s
confirmation: timing truth[0,0] s. Clean vanilla013848 has no final true tail
and supplies a120 s event lower bound; profile4's[0,0] s error is zero.
This proves that timing comparison without replacing vanilla's censoring by
an event. New selected-profile measurements remain necessary for acceptance.

`tools/floor_timing.py` compares event sample brackets over an explicitly
documented truth range, preserving both sampling and truth uncertainty. It
reports `unresolved` when admissible times reverse the comparison or either
event is censored. It does not establish a physical basis for a supplied
range or certify the full floor. For example, F3's exploratory range above
leaves its [5.8,6.0] s owner / [68.8,69.0] s vanilla comparison unresolved.

The complete Unity vanilla F4 `w1` measurement also now reaches its fixed
20000 s endpoint: 35 K crossing [0.8,1.0] s and final fluid temperature
318.028901253 K. Reconstructing its frozen binary32 placements gives initial
heat102103025.81511156 J, common temperature318.090164571 K and error0.061263319 K.
The exporter’s release row follows heat exchange; `floor_metrics.py` reports
its heat separately rather than treating it as the frozen input. Reconstruction
specifies truth; the admission probe below establishes native starting fields.
No complete profile4 F4 or aligned solid/fluid dynamic reference is established.

### F4 admission audit

The interrupted vanilla journal starts with an empty-room row, then its first
fluid row already has hot-room cells at 353.15/344.4 K and cold-room cells at
283.15/291.9 K. Thus its first-fluid `t=0` label follows a conduction interval;
it does not prove the frozen initial heat or starting temperatures. Do not
seed truth from that row or promote the interrupted run to acceptance. The
fixed geometry, fills, temperatures, duration and metrics remain unchanged.
The opt-in pre-UpdateData capture now proves vanilla admission in the actual
Linux game. Both arrays match all 28 requested tuples (mask 3) before native
ticket 509, with binary32 step 0.20000000298023224 s. Each contains
4.962862968444824 kg O2 and 400 kg granite, plus 360000 kg of zero-capacity
border material; total initial heat is 102103025.81511155 J from the runtime
capacities. The complete 840-byte capture and 484-byte request agree exactly.
The bounded diagnostic stops immediately after capture (expected harness exit
143), with process cleanup verified. This proves the vanilla starting state,
not the full 20000 s result, the owner's source alignment or dynamic truth.

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
