# Production substep kernel

`mac_kernel*.cpp` is a structure-of-arrays binary64 implementation of the
complete [`MacClosedWorld`](MAC_SOLVER.md) substep under the
[production contract](MAC_SOLVER.md#numerical-contracts). It keeps the owner
API, physical model, selectors, substep order, retry schedule and the
`MACCLS01` checkpoint format. The reference implementation stays the
validation oracle.

Status: phase 1 (single-threaded flux-form kernel, oracle validation,
benchmark option) and phase 3 (deterministic multithreading: bit-identical
results for 1, 2, 4, 8 and 12 threads) are done. From phase 2 the increment
pressure formulation and the shared geometric multigrid preconditioner
(`mac_pressure_multigrid.*`) are in. The opt-in
[two-moment pressure method](MAC_PRESSURE_MOMENTS.md) passes selected resolved
wave schedules. A [pressure-window component](MAC_PRESSURE_WINDOWS.md) now
streams prescribed fine-cell pressure roots through a coarse interval using
[shared physical face paths](MAC_TIME_MORTAR.md). It is not connected to the
owner. Independent sealed regions have an opt-in controller described below;
subcycling within a connected region remains open. The midpoint path covers every selector the ONI domain uses. The
real-time gate is not met; see [performance](#current-active-cell-baseline), the
[historical comparison](#historical-comparison-with-the-retired-interval-port)
and [remaining work](#remaining-work).

## Selection

- Explicit diagnostic kernel3 profiles and their saved tradeoffs are described
  in [MAC_GAME_KERNEL.md](MAC_GAME_KERNEL.md#positive-volume-profile). Profile3
  adds a positive-volume liquid law and larger linear work caps; its floor,
  resolved-reference and in-game acceptance remain open. Core20 and the
  ordinary production path retain their settings and continuation.
- `MacClosedSettings::numerical_kernel = MacNumericalKernel::production`:
  persisted selection 2, checkpoint version 6. It includes the exact-equilibrium
  certificate and the production kernel for each flow interval. The owner's
  diffusion half steps compose with it. Ordinary restore/advance needs no
  caller policy to continue this selection.
- `MacExecutionPolicy::production`: explicitly opts the returned owner into
  that saved selection, including certified equilibrium intervals.
- `MacPressureIntegration::two_moment` / `two_moment_coupled`: opt-in temporal
  pressure for kernel 2, persisted in checkpoint version 7 / 8. The latter
  solves the same moment equations as a complex symmetric linear system. Both
  retain the existing physical laws;
  unsupported/range paths requiring legacy reference execution explicitly
  reject instead of changing temporal method. It is not a general production
  default or an automatic timestep selector.
- `advance_mac_production(world, seconds, budget, options)`: flow only, without
  a certificate; also persists selection 2. It rejects enabled diffusion and
  unported selectors. The selected owner route uses an audited reference for
  unported selectors/range flags without changing the physical settings.
- The former interval solver and admission bridge are removed. Selector 1 is
  reserved and rejects; version-4 and version-5 selector-1 images explicitly
  reject as retired. They are never restored under a different kernel.

Numerical preconditioning belongs to the saved `MacClosedSettings`:
`automatic` uses geometric multigrid for pressure grids of at least 256 cells,
otherwise Jacobi. With automatic or IC pressure, conduction first tries up to
32 Jacobi-PCG iterations; if they do not reach the original tolerance, it starts
the selected pressure preconditioner's solve from
zero. This avoids hierarchy construction and pressure-cache eviction for
heat-capacity-dominated systems. Explicit `jacobi` stays Jacobi. Explicit
`incomplete_cholesky` uses independent component solves for midpoint pressure
(below); new colony owners select it. `MacKernelOptions` contains thread scheduling and diagnostics, with no
unsaved preconditioner override. Supported legacy reference images keep their
bytes and continuation; selector-1 images explicitly reject. Version 6 rejects every kernel
value except 2. The [wire contract](MAC_CHECKPOINT.md) specifies disabled and
enabled diffusion metadata.

The native ONI domain now creates selection-2 owners and explicitly selects
production on each live frame, requesting up to twelve workers, limited by
available CPU affinity and fixed block count. Loading a supported reference save preserves
its idle bytes; its first live frame migrates to selection 2/version 6. Frame
journals record this request in flag 4. Historical unflagged journals and
stateless checkpoint continuation retain the saved selection. Production gas
saves and fresh-process reloads pass Linux Unity, including paused frame-zero
resaving and 120 exact continued frames. Remaining Linux scenes and Proton
production validation are open. Fourteen historical owner images retain idle bytes
and pass same-platform continuation and migration on Linux and MinGW/Wine.
Viscous trajectories retain a pre-existing last-bit difference between compilers;
cross-platform trajectory byte identity is not claimed.

The thread count never changes a result (see [determinism](#determinism)).
`set_mac_kernel_default_options()` sets the process-wide options used by the
policy paths.

## Regional game controller

`MacStepControl::regional_predictive` (saved value 2, checkpoint 15) runs
independent timesteps on four-neighbour fluid components separated by solid
cells. Closed faces stay in the same component because they still conduct.
Each component uses the existing kernel on its bounding rectangle with the
same grid spacing, depth, inventory, primary energy corrections and canonical
momentum. Empty components and motionless isolated cells need no solve.
Components run in parallel; large connected components use the regular worker
pool. One shared attempt budget covers all components. Results publish only
after every component and the original full-world mass, passive, momentum,
U+K+G and entropy audits pass. Admission requires nonperiodic boundaries,
void-phase midpoint pressure and no kernel phase change; unsupported/range
paths reject rather than use a reference fallback. External phase relaxation
continues between game frames.

This changes adaptive schedules, floating-point reductions and trajectories;
controllers 0/1 keep their existing arithmetic and checkpoint bytes. Opening a
wall recomputes connectivity on the next interval. Regional reports sum child
steps, attempts and stage times: stage times are worker work, not frame wall
time. Workspace bytes bound concurrently retained buffers.

With unresolved-gas expansion enabled, gas-bearing cells below 16 epsilon
times their component's largest heat capacity do not conduct. For gas-only
cells driven outside material temperature ranges by rounding energy, a
conservative exchange restores an interior temperature
using the component's largest-capacity cell. Total exchanged energy is limited
to 16 epsilon of that reservoir's energy scale per conduction stage. Every
scalar heat atom is paired, primary rounding is counted, all masses remain,
and endpoint EOS, entropy and conservation checks still apply. This sacrifices
thermal detail below component energy resolution; it does not widen EOS ranges
or audit bounds. Report counters identify skipped conduction cells and energy
redistribution. Scientific controllers and gate settings do not select it.
Under the same profile, a gas-only trace below that capacity threshold may use
its positive analytic ideal-gas temperature for the intermediate gravity
entropy account. This lets the subsequent conservative heat exchange restore
an admissible endpoint rather than forcing vanishing timesteps at a material
temperature boundary. The intermediate temperature may leave the declared
range; no conserved state changes in the entropy account, and final EOS,
entropy and ledger bounds remain unchanged. Without enabled conduction, the
original strict intermediate EOS check still applies.
A captured Terra magma/headspace failure now passes its first interval with
identical one/twelve-thread checkpoint bytes, and 30 raw flow intervals pass.
The later cold-owner volume underflow is handled by the readout projection
below; other EOS domain limits remain open. Whole-lock Terra60 passes at
0.452 s mean owner time (including one checkpoint capture), with maximum
relative mass/energy errors 2.91e-16/2.07e-17 and no fault or reference fallback.
A captured life-support fault advances with this profile but still fails during
standalone continuation; trace EOS admission remains open. A captured filter
arena also still forces extreme refinement at a sharp-interface gravity EOS
boundary. Regional scheduling does not resolve these existing limits.

The reference EOS tightens the initial pressure bracket for extreme gas traces
using an upper thermal-energy bound and the liquid's minimum compliance over
the supported pressure interval. This retains the declared root budget,
tolerances and material ranges; ordinary roots keep their original bracket.
A synthetic partial/exact liquid-fill control retains the gas and recovers its
analytic pressure. A captured Oceania checkpoint now restores, though its flow
failure remains open. Checkpoint format and conserved inputs are unchanged;
previously unresolved trace roots can now produce a state.
The whole-lock Terra60 smoke passes without faults or reference fallback at
0.443 s mean owner time, with maximum relative mass/energy errors
2.11e-16/3.32e-13 under the existing ledger bounds.

Subnormal nominal liquid volumes remain wide in the reference EOS. A positive
derived phase-volume readout below double range reports the least positive
double; the total projection must fit the existing cell-volume roundoff.
An unresolved total elastic-energy readout may report zero only within the
existing conserved-energy roundoff. The pressure/energy calculations and
closure checks retain their wide values; primary masses and U stay unchanged.
Synthetic gas/liquid traces retain their inventory, and a cell too small for
the readout projection still rejects. The captured Terra continuation passes
30 intervals with exact checkpoint restoration after every interval. These
readout projections cost subnormal geometric/diagnostic detail, with no format
or API change.

## Saved game accuracy profiles

The [2026-10-22 decision](../docs/completion-plan.md) permits numerical
tradeoffs down to the measured [base-game floor](../docs/base-game-floor.md),
for saved game profiles only. Gates retain their settings and bounds. Profile
family 0 is legacy; family 1 (`base_game_floor`, checkpoint 16) saves an
explicit per-constituent trace threshold, at most 1e-6 kg. Default colonies
retain profile 0; test colonies opt in with `MA_COLONY_GAME_FLOOR=1`.
A fresh prepared base will be used for its
ten-cycle acceptance; old saves keep their existing profile.

`with_trace_sink()` removes constituents strictly below that saved threshold.
Its receipt contains registry-ordered mass, actual stored U+K+G and canonical
J changes, removed passives and counts. The same operation wraps a selected
owner's `advanced()` interval; those receipts return in `report.trace_sink`.
The game owner books and persists them separately from the space sink.
An unchanged inventory keeps the original shared state and revision. Failed
advances publish no removal. Sink edits and ordinary flow retain the existing
conservation bounds; removing material is an external account, not an entropy
production stage.

Chemical offsets follow the removed constituents. The remaining energy is
shared by heat capacity, including elastic energy; this preserves an ideal
gas mixture's temperature, while sacrificing trace-scale elastic partition
detail. Each face keeps its velocity through its retained-inertia fraction;
the sink books actual J and kinetic differences. Gas-containing cells keep
their occupied volume; gas-free liquid holds at most its zero-pressure nominal
volume or the grid volume. Empty cells retain no U, remainder or passives.

| Game-profile tradeoff | Reference cost | Floor result / adoption |
| --- | --- | --- |
| Existing regional schedules, maximum step 0.1f s, gas expansion fraction 0.001 | Different adaptive trajectory and unresolved geometry; quantitative F1–F5 errors pending | Current baseline; floor unmeasured |
| Existing tiny-capacity conduction/gravity entropy treatment | Thermal detail below component resolution; conservative heat repair and final EOS/entropy checks retained | Current baseline; floor unmeasured |
| Booked trace sink | Removes sub-threshold composition and its booked mechanics; elastic energy shared by heat capacity | Opt-in threshold 1e-11 kg; full floor acceptance pending |
| Substep trace sink (opt-in) | Books newly formed traces immediately after transport; keeps the same constituent threshold and stored-difference ledger | Reference/floor error pending; old profiles retain interval-only removal |
| Fast phase-trial EOS (opt-in) | Uses the production binary64 EOS during equilibrium root searches; uncertain trials refine with the reference, and accepted endpoints and entropy remain strict | Checkpoint 19; reference and floor measurements below; older saves retain strict trials |
| Compatible void pressure work | Uses geometric face sweeps for U work when an initial liquid void closes, matching canonical J/K; changes those trajectories | Selected by the opt-in profile; floor measurements pending |
| Mixed-EOS endpoint recovery | After the original 200 iterations fail, accepts a bracket endpoint satisfying the unchanged Newton criterion; successful old roots are unchanged | Selected by the opt-in profile; floor measurements pending |

The latter policies persist in checkpoint version 17. The void control takes
one step with no rejection instead of four with three, under unchanged audits.
A synthetic liquid with trace gas recovers its analytic pressure `2^-154 Pa`;
temperature admission stays enforced. Cold continuation across 1/12 workers
is byte-identical. These controls establish correctness of the policies,
not a Terra speed claim or a floor pass.

`MA_COLONY_FAST_PHASE_EOS=1`, together with `MA_COLONY_GAME_FLOOR=1`, selects
saved `fast_phase_trial_eos` for a new colony. It composes with the independent
substep trace option. Root tolerances, iteration caps, phase frequency and
fixed-U conversion are unchanged. The reference and all gate defaults retain
strict trials; restoring a version-18 or older save never selects this option.

On the regenerated profile-18 Terra50 state, a high-lock twelve-worker pair
measures strict/fast phase work at 22.19/7.60 ms. Stored endpoint bytes are
identical after accounting for the saved flag/version; that is a control for
this state, not a claim of universal identical trajectories. An instrumented
trial comparison finds maximum temperature/relative-pressure differences
9.1e-13 K / 5.7e-10. Mixed-pair and cold-flow controls retain fixed U/J, pair
mass, strict endpoint entropy, worker independence and exact save continuation.
Clean comparisons keep all 99 legacy MAC outputs and exits, apart from timings
and three new PASS lines.

Matched whole-lock, GPU-rendered Terra60 runs use one native bundle and pinned
managed DLLs, with fast trial EOS disabled/enabled to select profiles 18/19.
These replace the earlier trials contaminated by an abandoned F4 game process.
Each run samples all 60 frames and passes without faults or skipped relaxation.
Times are milliseconds; observed maxima include the harness UI pause.

| Profile / speed | Owner mean / p95 / max | Observed mean / p95 / max | Maximum relative mass / energy error | Trace sink kg / J |
| --- | --- | --- | --- | --- |
| 18 / fast | 162.1 / 212.5 / 354.8 | 256.0 / 383.0 / 2871.6 | 3.23e-16 / 4.89e-17 | 5.375e-08 / 0.01939 |
| 19 / fast | 169.0 / 375.2 / 462.4 | 259.6 / 457.6 / 2794.9 | 2.63e-16 / 4.92e-17 | 5.373e-08 / 0.01995 |
| 18 / normal | 160.2 / 228.6 / 321.2 | 257.2 / 386.3 / 2817.2 | 2.55e-16 / 4.59e-17 | 5.556e-08 / 0.01931 |
| 19 / normal | 150.7 / 221.2 / 397.0 | 260.0 / 420.8 / 2888.2 | 3.56e-16 / 4.10e-17 | 5.379e-08 / 0.01920 |

The cheaper phase stage does not produce a reliable whole-frame gain: fast
runs have 333/507 rejected attempts for profiles 18/19, normal runs 158/311.
Profile 18 remains the performance baseline; 19 is an optional experiment,
not a production selection. The speed target and full floor comparison remain
open; older saves and gate settings retain strict trials.
In-game profile-19 evaporation/vapour, freeze/melt, heater and generator
exhaust controls pass 201 frames without faults or skipped relaxation.
Maximum relative mass/energy errors are 7.00e-16/4.31e-17; the trace sink books
1.624e-7 kg / 0.051353 J. These controls do not establish the F1–F5 floor.

Two floor-profile failure paths retain their existing equations and audit
bounds. An isolated positive pressure scalar with exactly zero RHS returns
its exact zero solution without constructing an overflowing reciprocal.
An endpoint with a cancelled momentum remainder on an evacuated face is
rejected and retried with a smaller step, within the existing shared attempt
budget and eight audit retries. Previously successful intervals retain their
arithmetic and checkpoints; legacy and gate profiles keep their original paths.
The captured arena F5 frame passes thirty cold-restored flow intervals with
identical one/twelve-worker endpoints and a total trace sink of 1.8314e-10 kg /
0.0003705 J. The full arena run remains unverified. The ten-cycle capture gets
past its zero-RHS preconditioner error but a subsequent range failure is still
under investigation; ten-cycle acceptance is open.

Dual transport also rejects an intermediate candidate with nonzero canonical J
on a zero-inertia face, before a zero/zero share can escape as an argument
error. A newly emptied face retaining J rejects in that same substep. The
whole candidate and its accounts are discarded under the original retry
budget; momentum is never cleared to make it pass. Successful arithmetic and
checkpoint formats are unchanged. A synthetic control covers zero/positive/
negative residual J, both axes and one/twelve workers.
The captured frame-4387 fault and its later slow-onset capture each pass three
cold-restored flow frames, with identical one/twelve-worker checkpoints.
They take 7,865/7,258 substeps and 286/242 rejected attempts; maximum relative
mass/energy errors are 9.59e-17/1.08e-17. Trace receipts total 4.656e-8 kg /
0.27643 J and 4.130e-8 kg / 0.23766 J, respectively. This clears the captured
fault without removing the expensive substep/retry structure.
The whole-lock fast Terra60 smoke passes without faults or skipped relaxation:
mean owner/observed time 168.6/257.7 ms, owner p95/max 226.1/401.7 ms and
observed p95/max 403.3/2826.4 ms. Mass/energy errors are 6.11e-16/4.72e-17;
the sink books 5.355e-8 kg / 0.01942 J. This is a reliability fix, with no
whole-frame speed gain claimed over the 162.1/256.0 ms profile-18 control.

The colony owner also repairs a failed flow interval whose rarefaction reaches
the EOS temperature floor before the following phase relaxation. It bisects
the interval and applies the same fixed-U equilibrium only in vapour-bearing
cells within twice their EOS minimum temperature. Failed work and both children
share the original attempt budget; conversion and trace receipts are published
only when the whole interval succeeds. Successful old intervals and every gate
retain their original arithmetic and bytes. This also covers previously failing
legacy owner intervals; no new setting or checkpoint format is involved.
Four captured scenario failures and the matching legacy failure pass three
cold-restored frames with identical one/twelve-worker checkpoints. Condensation
is 2.663–3.052 g; profile trace totals are 1.42–1.84e-9 kg / 0.000442–0.000515 J,
and the legacy sink is zero. Maximum relative mass/energy errors are
7.02e-16 / 1.10e-17. A synthetic control checks unchanged successful flow,
the unchanged EOS domain, conversion and energy ledgers, workers, continuation
and shared-budget rollback. The ten-cycle trace capture remains unresolved.
Two in-game six-scenario batches subsequently pass 232 profile frames and 257
legacy frames where the sixth arena previously faulted; maximum mass/energy
ledger errors are 1.50e-15 / 5.64e-17.

At a provisional `1e-60 kg` threshold, five captured fault worlds complete
30 cold-restored intervals: total sink masses are `5.49e-59`, `6.63e-59`,
`2.63e-58`, `5.46e-58` and `1.53e-58 kg`. The largest booked energy magnitude
is `5.49e-65 J`. The new liquid-floor captures still fail later in continuation;
floor acceptance remains open.

The opt-in colony profile selects both policies and `1e-11 kg`, the smallest
tested decade clearing all seven captured starts (the levelling start fails
later at `1e-12 kg`). Each control below advances thirty 0.2 s flow intervals,
restoring the checkpoint after every interval; these are not full arena runs.

| Captured start | Total trace sink kg | Booked U+K+G J |
| --- | ---: | ---: |
| Terra 1071 | 1.1943e-9 | 0.002455 |
| Terra 1866 | 8.4571e-10 | 0.002419 |
| Volcanea | 1.4010e-8 | 0.004695 |
| Oceania | 1.6483e-8 | 0.008063 |
| Filter | 1.2865e-8 | 0.015318 |
| F3 levelling start | 2.1537e-8 | 0.007906 |
| F5 layering start | 2.0490e-8 | 0.008264 |

The four feature-removal variants remain scratch-only and require a user
adoption decision. [Measured Terra60 results](../docs/base-game-floor.md#performance-and-feature-variants)
are recorded for every category. Only reduced liquid sound speed improved
owner time (287.2 to 268.1 ms in whole-lock fast runs); the other tested
variants were slower. None has passed the accuracy floor. Momentum and per-cell
gas/miscible-liquid composition remain in every variant.

Private numerical trials loosened the pressure root criterion to 1e-8 with
geometric conservative U work (271.4 ms owner), delayed paired relaxation to
integer 1 s boundaries (274.0 ms), and tried a 0.2f s maximum step (304.0 ms).
They are not selected or persisted as production settings: reference field
errors and affected floor metrics are unresolved. The current maximum is
already 0.1f s; 0.05f s was a finer control, not a speedup. Full timings, ledger
errors and trace totals are in the floor report. Physics gates retain their
original settings and outputs.

The exact sterile conversion batching change at `be2a841` measured
298.6 → 291.5 ms mean owner time and 391.1 → 382.9 ms observed interval in
matched whole-lock fast Terra60 runs. The gain is small and noisy; the path
rarely joins a positive product on Terra. Parent/new mass errors were
2.32e-16/2.51e-16 and energy errors 4.69e-17/3.84e-17; trace totals were
3.536e-8/3.577e-8 kg and 0.01250/0.01283 J. Both had no faults or skips.
The new 1× control measured 281.2 ms owner / 364.5 ms observed, mass/energy
errors 7.63e-16/3.82e-17 and trace sink 3.517e-8 kg / 0.01258 J.
Fast observed p95/max were 545.6/2942.5 ms; at 1×, 586.9/2942.1 ms. The maxima
include the initial UI hand-off gap; fast frames 10–60 averaged 325.0 ms
(p95 446.5, max 545.6). No fast-speed target pass is claimed.

A version-18 opt-in saves `trace_sink_per_substep`. New colonies select it
with `MA_COLONY_GAME_FLOOR=1 MA_COLONY_SUBSTEP_TRACE=1`; the first variable
alone retains profile17. Restore uses the saved flag, so old colonies never
switch implicitly. The kernel removes traces after transport, books their
actual stored mass/U/K/G/J/passive differences and carries the entropy removed
through the interval and regional audits. Retained momentum scales with face
inertia. The threshold remains 1e-11 kg in the game selector.

The preceding private prototype clears three cold-restored intervals from
both long-run captures: sink totals 1.731e-7 kg / 1.885 J at frame1621 and
8.027e-8 kg / 0.5924 J at frame3319. Volcanea/Oceania/filter totals are
2.840e-9/6.222e-9/1.293e-8 kg and 0.001827/0.002617/0.05535 J. Relative
mass/energy errors stay below 1.7e-16/1.2e-17. The full frame1621 endpoint is
byte-identical with one/twelve workers. These are preliminary flow controls,
not a ten-cycle or floor pass; thousands of substeps still limit pace.
The focused synthetic control checks internal removal in two disconnected
regions, every ledger, cold/thread identity, old-profile identity and rollback.

The saved policy passes fresh whole-lock Terra60 runs with the same pinned
managed DLLs and GPU rendering as the preceding profile17 baseline. The
threshold stays 1e-11 kg; only the removal time changes. Profile18's frame50
dump confirms the saved selector. No faults or skipped relaxations occur.

| Speed | Owner mean / p95 / max ms, profile17 → 18 | Observed mean / p95 / max ms, profile17 → 18 |
| --- | --- | --- |
| Fast (2×) | 208.0 / 355.9 / 627.9 → 175.8 / 390.7 / 471.5 | 303.0 / 485.0 / 3030.3 → 268.9 / 484.0 / 2945.7 |
| 1× | 218.1 / 378.6 / 451.8 → 186.3 / 357.5 / 500.0 | 321.7 / 526.3 / 3071.0 → 281.5 / 403.4 / 2902.9 |

| Profile18 run | Maximum relative mass / energy error | Trace sink kg / J |
| --- | --- | --- |
| fast | 3.9e-16 / 5.15e-17 | 5.454e-08 / 0.0198 |
| normal | 3.05e-16 / 5.06e-17 | 5.467e-08 / 0.01982 |

The fast row is a rerun without a dump. The earlier 179.6/274.5 ms fast
means included a frame50 checkpoint write inside the flow timer and are
superseded here; the normal row never wrote a dump.
Observed statistics include the initial UI pause. Neither run meets the fast
clock target. Floor comparisons and the fresh-base long run remain pending;
this policy is explicitly selected, not a production-default change. Clean
Release/83 CTests pass; all 99 legacy MAC command outputs/exits and the owner
diagnostic remain identical apart from timings and the new control PASS line.
The version18 long-capture endpoints match the preliminary prototype exactly
after removing only the new saved flag. Rebuild C++ consumers, including the
private bench driver, against the new headers; package native libraries together.


## Game-kernel integration and measurements

The [accepted selector-3 design](../docs/game-kernel-spec.md) now has an
[opt-in diagnostic backend](MAC_GAME_KERNEL.md), saved as core20/profile2.
Profiles18/19 retain their existing paths and new colonies keep the prior
default. The initial dense owner bridge passed a whole-lock Terra60 with
closed trace/physical ledgers and zero faults/retries/skipped phase, but is
slower initially:210.27ms owner and266.07ms observed mean. An exact cache/copy
step now improves a clean paired run from214.42/256.71ms owner/observed to
194.11/239.11ms (9.47%/6.86%), with unchanged checkpoint bytes and closed
ledgers. Full evidence is in the backend note. It has not passed the fixed
floor or performance acceptance.
The last Codex whole-lock profile18 Terra60 measured owner168.6 ms and
observed257.7 ms mean, versus443/569 ms at the accuracy-floor brief's starting
revision. Those improvements precede selector3; the fast clock is not met.

The private prototype couples closed-form per-cell EOS, two fixed0.1 s flow
halves, one fluid heat solve, fixed-U latent phase relaxation, the1e-11 kg
trace sink, full64-bit passives, density layering and bounded liquid-interface
compression. A direct geometric liquid remap was rejected after excessive
compression and a thermal fault. The retained correction preserves J and
books its actual kinetic/gravitational work; local scalar limiting avoids
draining thermal energy. Gas and bulk material transport remain donor-cell,
so the design's limited-linear/FCT transport is not complete.

Matched whole-lock, pinned0–11, twelve-worker,60-tick offline Terra runs:

| Exact change | Before mean ms | After mean ms | Evidence |
| --- | --- | --- | --- |
| EOS/phase cleanup and removal of duplicate partial-stage audits, before interface correction |77.57 /76.98 |63.90 /63.84 | Physical bytes unchanged |
| Independent interface-correction clusters in parallel, with interfaces/passives/layering enabled |105.12 /105.46 |86.67 /87.02 | ABBA order; physical and passive bytes unchanged |
| One shared worker pool, with region audits and entropy diagnostics |98.30 /98.27 |89.95 /88.32 | Physical/passive/entropy/sink bytes unchanged |
| Reuse transport vector capacities, after heat-layout caching |88.60 /88.95 |83.23 /83.00 | Physical/passive/entropy/sink bytes unchanged |

The interface change removes17.5% of prototype time; its work falls from32.2–32.5
to12.5–12.9 ms. After-change p95 is92.2–95.1 ms, max98.6–107.1 ms. These
partial offline ticks omit owner edits/publication and do not establish a
game-frame speedup. The latest capacity-reuse change removes6.4%: transport
falls from18.7–19.4 to14.3–14.4 ms, after-change p95 is86.6–88.7 ms and
max105.8–112.1 ms. Warm/cold-object continuation also agrees. Heat-layout
caching saves about0.7 ms in that stage, with no established whole-tick gain;
pressure-layout caching was slower and rejected. The numerical changes
against the reference remain unquantified; no floor pass is claimed.

One/twelve-worker60-tick primary and passive images agree byte-for-byte.
Global species/energy/momentum/passive relative residuals are below
9.4e-17/1.6e-17/2.9e-18/2.8e-17; trace totals are1.1761e-7 kg /0.12743 J.
Once-per-tick audits also close in all1,306 connected fluid regions, with
maximum relative residual below2.5e-16. A sealed two-room wind control checks
separate wall-impact heat, trace/passive receipts and detects an artificial
cross-wall heat transfer whose global total closes. Cold-object continuation
passes six ticks; an actual saved checkpoint codec is still absent.

Entropy is diagnostic, as the design requires, and currently exposes an
unresolved flow error: Terra60 totals -70.46 MJ/K, of which flow contributes
-70.54 MJ/K. A zero-gravity diagnostic removes the loss; gravity stays enabled.
Upwind body-force and conservative velocity-filter trials did not repair it
and remain unselected. Frozen F1 starts now come directly from the protocol,
rather than from post-opening game captures. The strict8× reference advances
past its former full-range parcel fault, but resolved event times and the16×
uncertainty control remain open. A standalone finite-body heat component passes
analytic/shared-heat/worker controls; the native contact model and its coupling
are not integrated.

Open: fixed-floor/reference comparisons, limited-linear/FCT transport, saved
vacuum-front cap, sleeping, registered-solid heat, profile2/checkpoint and
batched owner edits/cached physical publication. Old saves never migrate
implicitly; new-profile ten-cycle acceptance needs a fresh prepared base.

## Coverage and admission

| Setting | Production kernel |
| --- | --- |
| Material reconstruction | 0 donor cell, 1 limited linear, 3 sharp interface |
| Momentum reconstruction | 0 donor cell, 1 limited linear |
| Compression | 0 none, 1 quadratic, 2 pressure-sensed acoustic |
| Shock heat | 0, 1, 2, 3 |
| Conduction, gravity, viscosity | yes |
| Cells | gas, filled liquid (single or several liquids), gas/liquid mixtures, exact empty cells, solids |
| Vacuum representation | 0 empty cells, 1 [void phase](MAC_VACUUM.md#void-phase) (gas and [liquid surfaces](MAC_VACUUM.md#liquid-surfaces); midpoint pressure) |
| Unresolved subcell void | 0 exact geometry (default), 1 [audited gas expansion](MAC_VACUUM.md#unresolved-gas-opt-in), persisted in version 12; supported production void-phase selectors only, no reference fallback |
| Closed faces | `MacGrid::closed_x/closed_y` between two open cells: fixed like a wall face (no flow, pressure and weight go to the wall account, no viscous slip) but still conducting; set by `MacClosedWorld::with_closed_faces`, not persisted by this production kernel (game-kernel core20–23 checkpoints do persist them), discrete-gradient pressure only, no reference fallback |

Swept (2) material reconstruction, momentum selectors 2–5, coupled
compressive pressure (3), and incomplete Cholesky with moment pressure are
rejected by the flow-only helper. A selected production owner uses the audited reference for
these settings, retaining their selectors and selection 2 in its next image.

Range: a nonfinite or overflow-range value (above `2^450`) in the input or
at the interval endpoint runs the whole interval through the reference
implementation, so full-range arithmetic is used only for flagged values.
Uncertain material-temperature or caloric boundaries also request the strict
reference, retaining consumed attempts. A temperature clearly outside a
material's range is outside the EOS domain instead: the substep retries smaller
(2026-10-20; before, it also ran the interval through the reference). A certified static state can advance
unchanged at such a boundary. A flagged intermediate that loses a representable
final pressure sweep also refines through the strict interval, retaining consumed
attempts. Subnormal cell-volume/face-area geometry uses that reference directly.
Below-range values are admitted. Roundoff-level transverse momentum on large
grids decays far below binary64's normal range within a few intervals;
falling back for it made the 160x160 wave about 100 times slower than
necessary. Full-range canonical momentum is projected, subnormal results
round, and the audit counts up to `2^-1075` of absolute rounding per operand
per substep. A momentum-correction pair whose heat budget is below the
subnormal resolution takes the always-admissible zero correction, and a
compression pair with a closing speed below `2^-500` m/s whose midpoint
rounds away is skipped; both are counted.

## Design

**Storage.** Species-major masses for present constituents only, U, one
column per present passive identity, and face J. Owner states are converted in
and out once per interval; the result owner reuses the copied per-cell vectors.

**EOS** (`mac_kernel_eos.cpp`). The reference ideal-gas/zero-expansion
elastic-liquid model in binary64: closed forms for gas-only and single-liquid
cells, safeguarded Newton roots for mixtures and several liquids. Per-cell
statistics (mass, Cv, offsets, moles, nominal liquid volume) and face
inertias are cached until transport changes the masses; between transports a
gas or liquid reading is a few operations (liquid pressure does not depend on
U at fixed volume). Where the Newton root of a
gas-bearing mixture's isentrope fails (a trace bubble compressed past its own
volume puts the root tens of log units of pressure away), a bracketed,
bisection-safeguarded solve of the same equation and tolerance takes over;
converged Newton roots are unchanged.

**Pressure** (`mac_kernel_pressure.cpp`). The reference implicit EOS work with
midpoint sweeps, solved for per-cell volume changes in the increment form
`W = p0 + dW(dV)`. Gas increments use a cancellation-free series of
`(1-(1+x)^-k)/(kx)` for `|x| < 1/4`, liquids a closed form in `s = -dV/V1`,
mixtures the reference divided differences. Node pressure differences gather
`(p0_l - p0_r) + (dW_l - dW_r)`, so there is no absolute-pressure roundoff
floor; the root reaches the reference 16-epsilon closure. A residual within 256
times that target that no line-search step can reduce (rounding noise of stiff
liquid/gas coupling) is accepted as converged rather than retried smaller, and so is
one there that a step no longer halves (at quadratic convergence one step from that
band would land far below the floor; Terra crawled 15 more steps at 1% each). Each Newton
correction is a matrix-free PCG solve honoring the saved pressure setting
(automatic geometric multigrid, or explicit Jacobi) with an inexact-Newton tolerance
(`0.1 * residual`). Canonical J, wall and body impulses and midpoint sweeps
use the reference formulas. Wall-reaction pressure components gather in
double-double before the common timestep/area product; rounding each large
opposing wall impulse separately can erase a small net reaction.

The geometric preconditioner uses [the independently reviewed hierarchy](MAC_MULTIGRID.md).
Its topology and mutable workspace are retained on the calling thread, keyed
by exact geometry and the pressure-active mask. Every solve replaces all
coefficients; vacuum arrival changes the mask and rebuilds before use.
Empty-side zero-traction terms stay in the fine diagonal. Production supplies
its fixed-block pool to the V-cycle; no interval workers execute this path.
An invalid preparation selects Jacobi before PCG; a failed application rejects
the candidate. The old aggregation implementation has been removed. Workspace
bytes include retained multigrid topology and numerical arrays.

**Independent component solves.** Saved `incomplete_cholesky` (existing value 2)
compacts the midpoint operator into connected components and solves them in
parallel with the retained `PressureSystem` IC(0) implementation. Uncoupled
boundary weights stay in the diagonal. Component cell/face order is fixed;
each job owns its factor, Krylov vectors and disjoint output cells. There is
no cached continuation history. Per-component tolerances are tightened so their
combined residual satisfies the original global linear target; each solve
checks the actual matrix residual. The nonlinear closure, physical fluxes and
interval audits are unchanged. Linear iteration counts sum component iterations;
`maximum_linear_components` and IC solve/fallback counts identify this route.
The main benefit is on disconnected asteroid caverns: a stiff pocket no longer
makes all other cells repeat its Krylov iterations. A single connected room
still uses a serial IC solve. Unsupported moment-pressure combinations retain
their previous admission rules. Checkpoint fields and versions are unchanged.

**Transport** (`mac_kernel_transport.cpp`). Limited-linear reconstruction with
the reference stencil, limiter points, positive-field and EOS admission
backtracking. Each donor writes its outgoing face parcels once; each target
gathers retained plus incoming parcels, so every constituent and passive is
moved by one shared face quantity. Dual J/K transport uses the reference
primal-to-dual edges, upwind mixing heat and wall removal, and the
limited-linear corrections with the reference pair limiter. Heat goes to cells
by new half-mass shares; gravity adds the reference body-work/potential-flux
adjustment.

Below-range primal parcel products retain their existing binary64 values.
`mac_kernel_rounding.cpp` measures each flagged product's exact error from
normalized significands and a strict split-product transform, then sums
its magnitude in full-range expansions. Exactly representable subnormals do
not count. Only the accepted reconstruction trial contributes; discarded
substeps and interval retries discard their ledgers. Fixed block reductions
give the same counts/errors for every worker count. The interval checks each
quantity's error against `rounded_transfers * 2^-1075`, publishes its full-range
error/bound in `MacKernelAudit` and includes the count in the owner's
`interval_rounded_updates`. Shared parcels add no endpoint conservation
allowance; the original interval bound is unchanged.
The saved Wine review rejected an explicit libm-FMA error transform near the
smallest-normal boundary. The split-product transform avoids that dependency.

A below-range velocity or swept volume is not itself a material transfer:
subsequent multiplication can produce a representable parcel. Flagged pressure
sweeps therefore retain their full value and sign until parcel formation. A
derived lookup is allocated only when a flagged face appears, and only its
previously flagged entries are cleared. It has no saved continuation state.
Each donor forms the full parcel before one explicit storage projection; the
receiver reads that same parcel. Ordinary sweep/product operation order stays
unchanged. If a subnormal intermediate has instead erased a normal final sweep,
the coupled pressure root refines through the strict reference.
The fraction `sweep / material_volume` has its own flag: a normal sweep divided
by a large volume can underflow before multiplication by the transported field.
That fraction also retains range until the shared parcel is projected.

Accepted primal writes that leave storage unchanged are counted for every
constituent, passive and U: donor-retained subtraction, parcel correction and
receiver gather. Reconstruction trials reset their candidate counts; discarded
substeps/intervals never publish them. These writes already enter the original
128-epsilon operand bound, which is unchanged. `unresolved_updates` combines
them with the existing pressure/heat write counts; it is not a U remainder.

Dual momentum uses canonical `J * part / inertia` when its velocity is flagged,
so a zero projected velocity cannot erase representable J. Flagged kinetic
products normalize `J^2 / (2 inertia)` before sharing, rather than underflowing
the specific kinetic energy first. Their transferred and retained projections
use the same counted half-quantum ledger. This intentionally fixes the former
large-inertia production fault and preserves representable trace arrivals.

**Pressure storage rounding.** Midpoint and both moment methods now observe
inexact canonical-J additions with deterministic [primary-write ledgers](MAC_PRESSURE_ROUNDING.md).
Their outward half-ulp charges fit inside the original interval operand bound;
stored values and refinement counters are unchanged. Accepted counts reach the
owner's rounded-update report. Incoming constituent/passive gathers now also
have [inventory-write ledgers](MAC_INVENTORY_ROUNDING.md), with unchanged stored
values and interval bounds. [Persistent primary U](MAC_PRIMARY_REMAINDERS.md)
now carries the low part through every stage, with accepted high-write and
third-part counts in the interval audit and version-10 checkpoints. Retained
constituent/passive subtractions, ordinary products and momentum writes outside
pressure still need their remaining ledgers.

**Regularization and physical stages.** Compression (modes 1/2), shock heat
(modes 1–3), conduction (backward Euler for temperature changes; with mixtures
present, Newton relinearizes each mixed cell against the complete EOS and
warm-starts each correction solve to 1e-13 of the first residual) and viscosity
(rank-one modes) use the reference pair/mode laws. With phase change and
conduction on, the kernel also converts [sharp boiling interfaces](MAC_PHASE_CHANGE.md#sharp-interfaces)
in each substep: conduction runs once before the pressure stage, and the pressure
stage carries each interface's vapour volume.

`MacKernelReport::physical` carries committed substeps, conduction contacts,
iterations, stored redistribution and entropy, viscosity modes, requested and
stored heat, wall impulse, unresolved-update counts and gravity internal work.
Published accounts contain only the audited interval's committed substeps,
so retries cannot publish rejected heat or work. These observations preserve physical
updates and the single interval entropy admission. Mixed-material conduction
uses adaptive diagnostic entropy quadrature. Its energy path interpolates the
stored endpoints, preserving the colder endpoint after a large transfer. Local
refinement may reach depth 32 within the previous depth-20 tree's sample-work
limit; tolerance is unchanged. `collect_conduction_entropy` (default true)
controls only this per-stage report. The game disables it: the field is zero,
while endpoint EOS validation, physical heat, conservation and the mandatory
whole-interval entropy audit remain active. Silent validation
controls compare heat/work reports with the oracle, reject a constrained
budget without leakage, and require identical one/twelve-worker reports.

**Exact reference order.** Every reference Gauss–Seidel sweep (compression,
shock heat, momentum corrections) chains only along grid rows or columns, and
lines are independent. The kernel runs each line sequentially in the
reference order and lines in parallel. A colour-ordered variant gave up to
2.4 K / 1% differences at a vacuum front where the momentum limiter binds;
line order reproduces the reference to roundoff. Viscosity modes and mixed
conduction contacts couple both directions: viscosity uses a fixed 9-colour
order, mixed conduction runs sequentially.

**Diffusion slot.** Gas diffusion (`mac_diffusion*.cpp`) wraps each flow
interval in half steps at the owner level. The production policy keeps that
wrapper around the kernel. Persisted production selection opts diffusion into
[counted shared-transfer rounding](MAC_DIFFUSION.md#counted-transfer-rounding);
the strict reference keeps its existing admission. A
diffusion-enabled owner gets the same composition and composed energy
account with either policy; the unit suite compares the two on the room
scene with physical diffusion (roundoff agreement). Counted transfers are
combined into the owner's interval report. `stage_diffusion` stays
reserved for a flux-form diffusion stage inside the kernel: per-face
antisymmetric species fluxes on the species-major columns, accumulated into
the same interval audit.

## Production contract

- **Conservation by construction.** Primal parcels, dual fluxes, pair
  impulses/heat and conduction fluxes are shared face quantities applied to
  both sides.
- **Interval audit.** Species, passives, both momentum components (with wall
  and body impulses) and U+K+G are reconciled once per interval with the
  rule of `mac_interval_audit.hpp`: double-double sums, an outward error
  envelope, exact `MacEnergyAccount` replay when undecidable. Operands are
  both endpoints, external impulses and every stage's operand magnitudes, so
  the 128-epsilon bound scales with the work actually done. Totals come from
  fixed row-block partials. A failed audit discards the interval and retries
  it with half the maximum step, then faults. So does an endpoint outside the
  EOS domain, such as a vacuum front's trace cooled below its material's range.
- **Rounding.** Primary storage is binary64; updates that leave a stored value
  unchanged are counted (`unresolved_updates`). Accepted below-range primal
  parcel products have per-quantity counts and exact error ledgers, independently
  checked against their counted half-quantum allowance. Flagged dual J/K
  products use the same ledgers, including retained K projections.
- **Entropy.** The interval rejects a decrease of total stored-state entropy
  beyond `128 eps sum(|U|/T)` over both endpoints, after crediting the
  gravity reconciliation by its concavity bound (below).

### Gravity reconciliation entropy

Gravity work is reconciled against the potential energy carried by actual mass
flux, and the difference goes into U. For a face with mean face mass
`(m_l + m_r) / 2` and transported strip density `rho_s`, that change is
`g h^2 v dt ((m_l + m_r) / (2 h^2) - rho_s)`. It vanishes for uniform density.
At a free surface it does not. The potential is cell-centred, so it cannot see
liquid lying below the centre of a partly filled cell. For a level surface at
fill `alpha` the reconciliation tracks the change of that hidden offset,
`-rho_l g h^3 alpha (1 - alpha) / 2` per surface cell. U falls while a surface
cell drains its upper half and returns while it drains the lower half. The
change is reversible and bounded, and it is first order in `h`. It still
lowers stored entropy over an interval. A collapsing water column at
`h = 1.25 cm` loses about `9e-6 J/K` per 0.01 s against a `6e-8 J/K`
allowance. Before this credit, every free-surface flow failed the production
audit.

Each cell with a gravity change `dU` evaluates `T` at its pre-gravity state
(same masses, `U - dU`). The kernel accumulates `sum(dU / T)`, and the reference
substep records the same sum. Entropy is concave in U, so this sum is an upper
bound on the reconciliation's actual entropy change. Subtracting it therefore
never credits more than the reconciliation itself changed. Both audits
(production interval and reference fallback) require
`change - gravity_entropy_bound >= -allowance`, where the allowance also covers
`128 eps sum(|dU| / T)`. Every other stage must still produce entropy.
`gravity_entropy_bound_j_per_k` is reported by `MacKernelReport` and
`MacClosedAdvanceReport`. `ma_mac_interface_tests` checks the bound against
the exact gravity-stage entropy change. They agree to `1e-6` of the bound;
the measured gap is EOS evaluation noise. The test also checks that the interval change alone would fail, that the
remainder passes, and that the production and reference bounds agree to `1e-6`
for limited-linear and sharp-interface reconstruction.
- **Unchanged.** Atomicity (a failed interval publishes nothing), checkpoint
  continuation, fail-closed admission and the arithmetic environment check on
  every executing thread.

## Determinism

Rows are grouped into fixed blocks (about 1,024 cells each) that depend only
on the grid shape. Each block is processed by one thread; reductions combine
per-block partials in block order; line sweeps assign whole lines to fixed
line blocks. The thread pool's exception propagation rethrows the lowest
failing block. Results are therefore bit-identical for any thread count.

The pool (`mac_kernel_parallel.cpp`) keeps its workers between parallel
regions. Threads claim blocks dynamically from one 64-bit ticket (region
generation and next block), so a descheduled worker delays only the block it
holds; which thread runs a block never changes the result. Idle workers pause
briefly, then yield, then sleep on a condition variable. Before a new region
changes the block count and job, the finished region's ticket is closed, so a
late worker cannot claim a block of the new region twice. The first pool
partitioned blocks statically; on the shared host one preempted thread then
stalled every region: at 65,536 cells 8 threads gained 1.1 times and 12 ran
at 0.56 times the one-thread speed.

Every executing thread checks the arithmetic environment before it claims a
block; a thread that fails the check records the fault on the blocks it would
have run.

## Validation

`ma_mac_kernel_validation_tests` (CTest `mac_production_kernel_oracle`)
advances each scene with the reference policy and with the production kernel
for the same intervals (identical substep schedules) and reports every
completion-plan metric. Tolerances: 0.1 K; 1% for pressure (of the
disturbance amplitude where a disturbance is defined), O2 partial pressure and
every constituent including the 1e-28 helium trace.

| Scene (cells, intervals) | Substeps | max dT (K) | max dp / disturbance | max dp / p | O2 pp | constituents |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Wave 16x16, 20 Pa (256, 2) | 21 | 1.7053e-12 | 5.09317e-11 | 1.01858e-14 | 1.08385e-14 | 1.00957e-14 |
| Wave 32x32 (1,024, 2) | 21 | 1.53477e-12 | 5.96629e-11 | 1.19324e-14 | 1.21248e-14 | 1.04868e-14 |
| Uniform translation 1 / 0.5 m/s (256, 2) | 21 | 0 | 0 | 0 | 0 | 0 |
| Hydrostatic gas, gravity 9.81, 5 Pa mode (128, 2) | 21 | 1.25056e-12 | 2.53203e-10 | 1.26811e-14 | 1.41693e-14 | 1.02873e-14 |
| Conduction, 30 K hot block (256, 2) | 21 | 1.7053e-12 | — | 1.46974e-14 | 1.43296e-14 | 1.06008e-14 |
| Gas/water mixture, solid corner, conduction (144, 2) | 21 | 3.41061e-12 | — | 0 | 9.18564e-16 | 1.91379e-14 |
| Filled water, gravity, 200 Pa mode (64, 2) | 21 | 5.17275e-12 | 3.61794e-08 | 3.22068e-11 | — | 3.29658e-15 |
| Wave with viscosity 0.5/0.25 Pa s and flow (256, 2) | 21 | 5.15286e-10 | 3.0041e-08 | 6.00786e-12 | 6.00781e-12 | 4.29144e-12 |
| Room: interior wall, 1 kPa / 10 K step, conduction (384, 2) | 21 | 1.53477e-12 | 1.1496e-12 | 1.14251e-14 | 3.2265e-14 | 2.67967e-14 |
| Gas into exact empty cells, dt 1/4096 s (128, 1) | 410 | 1.0175e-11 | 1.73168e-14 | 1.75277e-14 | 1.84091e-14 | 3.19285e-14 |

Every scene passes with at least five orders of margin. The empty-cell scene
uses a fixed small step: at the default step both implementations reject and
halve near the same volume-CFL threshold, and a one-ulp difference can choose
a different retry sequence. The viscosity scene keeps the largest difference
because its modes use a colour order rather than the reference raster order.

`ma_mac_kernel_unit_tests` (CTest `mac_production_kernel_contract`) checks
byte-identical checkpoint continuation (wave, room, liquid, mixture, empty
cells), bit-identical results for 1, 2, 4, 8 and 12 threads (odd grid sizes,
walls, gravity, conduction, viscosity), atomic budget exhaustion, rejection of
swept/coupled selectors, the production policy's equilibrium certificate, the
persisted production admission and range fallback, and that every audit
quantity passes. Both suites are run under ASan/UBSan (Debug, `-O1
-D_GLIBCXX_ASSERTIONS`, fatal findings) and ThreadSanitizer; see the commit
messages for the exact coverage of each run.

`ma_mac_kernel_multigrid_tests` adds nine explicit geometric-preconditioner
controls: odd periodic waves (including 33,389 cells), an interior wall,
filled liquid with gravity, moving gas/liquid mixtures, hydrostatic gas,
conduction, viscosity and gas arrival into empty cells. It compares the
reference within the original tolerances, checks identical checkpoints with
1/12 workers and a fresh-thread 4-worker restore, rejects exhausted budgets
atomically and compares concurrent heated/unheated branches. The largest
temperature difference is 9.36e-8 K. These controls pass GNU 15.2 Release,
Debug ASan/UBSan, Debug ThreadSanitizer and MinGW 13 under Wine 10.

`ma_mac_kernel_checkpoint_tests` checks fifteen saved-setting continuation
combinations (gas, liquid, mixtures, empty cells; automatic/Jacobi; diffusion
off/physical/100× where admitted), including ordinary restore/advance with
changed worker counts. It checks the equilibrium certificate, resealed wrong
versions/selectors, malformed diffusion settings, every truncation and an
explicit-IC reference fallback. A mid-interval range flag must retain selection
2, consume its discarded attempt, pass exact interval accounts and leave the
source unchanged when its budget is exhausted.

`ma_mac_kernel_saved_tests` (CTest `mac_production_saved_accounts`) carries
all seven original interval-port scientific scenes into production: periodic gas,
walls, a one-dimensional strip, 10,000 steps, 81 species/82 passives, periodic
multigrid and disconnected multigrid. `review_mac_kernel.py` independently
recomputes their original strict endpoint mass, passive, impulse, U+K and entropy
bounds, then checks the production report's stage magnitudes and below-range
operand allowance. It passes 105,985 checks and rejects seven altered captures.
The reviewer exposed wall-reaction cancellation errors; the pressure-component
gather fixes them without changing physical state or gate bounds.

The saved controls also cover changed geometry, heat/mass edits, changed
constituent/passive schemas and caloric models, source lifetime and concurrent
owners. Four formerly declined inputs retain their scientific setup and now
exercise production admission; unported selectors use the audited reference.
Material-boundary uncertainty refines through the reference, while exact static
states can take their certificate. Mid-interval range flags retain consumed
attempts and atomic rollback. The former interval solver/cache is removed;
selector-1 construction and its historical checkpoints explicitly reject.

The Release build enables tests, the ONI backend and warnings as errors; all
52 CTests pass. Twelve pressure/kernel suites pass Debug ASan/UBSan and MinGW 13 under
Wine 10; eleven pass ThreadSanitizer, using the moment suites’ threaded checkpoint
controls there. All 22 saved reviews pass: both temporal formats on Release,
ASan/UBSan and Wine, and the other four reviewers on all four routes. The temporal
controls and endpoint review are described in [pressure moments](MAC_PRESSURE_MOMENTS.md).
The shared-face time suite additionally passes all four routes, with 1,057
independent saved checks and fourteen altered captures; see
[time integration](MAC_TIME_MORTAR.md).
The pressure-window and affected moment/multigrid controls also pass
ASan/UBSan, ThreadSanitizer and MinGW/Wine, with targeted threaded checkpoint
controls for the older moment methods. Its four saved reviews pass 3,307
checks/nineteen altered captures. All 66 preceding component executables retain
their physical records and exits against `5737fb6`; 64 outputs are byte-identical
and two differ only in timing. Prior checkpoint bytes and continuation remain
unchanged. See [pressure windows](MAC_PRESSURE_WINDOWS.md) for the component's
normal-range, fixed-mass scope and open production work.
Current accepted dual-projection counts (`TTHREAD`: `4/554/88`) include
flagged J/K work; product and endpoint mass records are unchanged.
The new diffusion-rounding suite independently checks quantized Heun transfers,
shared-budget rollback, tiny ternary retry and spreading-front continuation.
Its saved reader passes 2,690 checks/six corruptions on all four routes; the
four 400-frame 8,192-cell captures pass 131,154 checks/three corruptions. Bulk
metrics remain unchanged and the strict aggregate helium bound is retained.

`ma_mac_kernel_rounding_tests` (CTest `mac_production_transfer_rounding`)
exports 1,080 exact product controls, including zero, exact subnormals,
ties, signs and the smallest-normal boundary. The saved-only
`review_mac_kernel_rounding.py` checks their errors and merged expansion with
exact fractions, then checks an 8,192-cell advected trace front against the
original 128-epsilon aggregate inventory bound. It passes 50,825 checks and
rejects seven altered captures on Release, ASan/UBSan, TSan and MinGW/Wine.
The front completes 160 intervals with 44,420
rounded products and helium in 7,028 cells. A 33,389-cell control executes twelve
workers, preserves counts and bytes against one worker, restores ordinary
production continuation and proves budget exhaustion publishes neither state
nor a partial ledger. These are functional controls, not performance acceptance.

Reproduce the saved review:

```sh
build-production/ma_mac_kernel_rounding_tests > /tmp/mac-parcel-rounding.txt
python3 native/tests/review_mac_kernel_rounding.py /tmp/mac-parcel-rounding.txt
```

`ma_mac_kernel_range_tests` (CTest `mac_production_flagged_transport`) scales
inventories by `2^420` and uses canonical J `±2^-700`. The former production
implementation exhausted 32 attempts after its derived velocity rounded to
zero; the strict reference advanced in one. The flagged path now advances in
one attempt and matches the reference endpoint, including a
`3.5355459982430231e-205` tracer arrival. Signed x/y, zero/one/two-quantum
arrivals and per-quantity J/K error ledgers pass. A 33,389-cell control retains
exact one/twelve-worker state and counts (100,167 projections), clears the
cache before ordinary owners, and restores ordinary production continuation.
The saved-only `review_mac_kernel_range.py` derives the physical transfer and
J/K projection errors from independent exact fractions, including a normal sweep
divided by material volume `2^450`: 1,318 checks and ten
altered captures rejected on Release, ASan/UBSan, TSan and MinGW/Wine. A wide-area control requires a strict interval
restart and proves one discarded attempt remains charged; concurrent owners
retain separate caches. A flagged tracer also crosses a fixed row-block boundary
with the same shared transfer on one and twelve workers. All four saved reviewers
pass on Release, ASan/UBSan, TSan and Wine. These controls do not certify
active-cell performance.
The large-volume control retains the reference's `3.9806709100596318e-190`
arrival and zero/one/two-quantum arrivals. The reader independently counts all
accepted unresolved writes: 36/37 in six cells, and 200,335 in the 33,389-cell
controls. Saved format 2 requires these records; `--legacy` explicitly reviews
the earlier format. Failed interval budgets publish neither count nor state.

The default reference wave checkpoint SHA-256 remains
`4d250703f98fd1b00af3a7da505aaac1e7a7aa5405f24644e9b299893e8cf763`.
Against `cb8ecb1`, all 65 preceding MAC component executables retain their
exit results: 63 outputs are byte-identical and two differ only in timing.
All physical records are unchanged; the shared-face time test makes 66.
Five-interval 256- and 4,096-cell production wave checkpoints also retain every
version-6 byte against the preceding binary. Fresh-process gas and multigrid
images from Linux and Wine continue byte-identically on both, in both source
directions. Subnormal helium/passive and flagged-sweep images also continue exactly
in fresh Linux/Wine processes, in both source directions. New version-7 moment
images and new version-8 coupled images also continue exactly on both platforms
in both source directions. Version-7 bytes and cross-commit continuation remain
unchanged. These are Wine controls,
separate from actual Windows OS evidence.
Six synthetic version-2/3/4/5 images retain every byte of the previous writer;
reference images restore unchanged and selector-1 images explicitly reject.
The original strong-flow, curved-nodal and vacuum-front failures remain open.
The frozen production-owner/pressure-ledger Ubuntu 22.04 snapshot passes 55
CTests; both shared libraries meet the glibc 2.34 ceiling after the shared
interface geometry replaces `std::hypot`. Production Linux source/save and
fresh-process reload cases pass in the partial game cohort. Water-heat fails
its unchanged endpoint energy gate (−0.0177002 J against 0.00594580 J after
123 frames). With [persisted U](MAC_PRIMARY_REMAINDERS.md), the unchanged standalone
reproducer passes (+3.81470e-6 J against 0.00594585 J). The sharp-interface control
passes MinGW/Wine with the authorized translation amendment and the U-inclusive
gravity observation. Fresh U-enabled Linux/Proton game packages and active-cell
acceptance remain open; no actual Windows OS evidence is claimed here.

The playtest benchmark compares against the reference automatically:
`tools/run_mac_playtest_benchmark.py --kernel production --profile reference
--scene wave --continuation` gave 8.2e-11 of the disturbance after three
consecutive intervals at 4,096 cells and 8.0e-11 at 16,384.

## Terra colony frame

Matched Linux Release new-colony runs complete 60 frames each, advancing
0.2 game seconds per frame with sixteen workers under the whole-machine CPU
lock. New owners select component IC; physical selectors, timestep, closure
and audit bounds are unchanged.

| Owner advance | Parent | Component IC + cheap conduction |
| --- | ---: | ---: |
| Median | 3.784 s | 0.961 s |
| Mean | 4.430 s | 0.998 s |
| p95 | 10.873 s | 1.526 s |
| Maximum | 16.238 s | 1.679 s |

Both runs close their ledgers. Candidate maximum relative mass/energy errors
are `5.18e-16` / `2.77e-17`, with zero IC fallbacks. Matched high-half dump
advances take 4.30–4.59 s → 0.65–0.97 s at frame 50; the severe pocket-opening
dump takes 43.45 s → 3.48 s. These measure the evolving colony owner. Connected active-cell workloads
follow below.

Release/83 CTests and the focused ASan/UBSan/TSan control pass. Clean-build
comparisons preserve all 83 short MAC executable and sixteen variant exits;
the coarse Stefan worst error moves from 2.745% to 2.773% and still passes its
unchanged bound. Other differences are counters, timing and the new controls.
Checkpoint serialization is unchanged; new owner images store existing IC
selector 2. The following exact changes continue that work:

- [Cell-local phase relaxation](MAC_PHASE_CHANGE.md) uses the existing pool:
  matched Terra60 mean owner advance 1.029 s → 0.764 s, closed ledgers.
- Component geometry is prepared once per nonlinear pressure root. Face order,
  component row order and boundary-diagonal addition order stay fixed; every
  Newton iteration still rebuilds its current diagonal and RHS. Void active-set
  changes rebuild the geometry. There is no cache across roots or worlds.
  A frame-50 profile spends 132 ms preparing component graphs and 78 ms solving
  them; reuse reduces a matched high-half dump advance from 599 ms to 425 ms
  (pressure 226 ms → 75 ms), with identical steps and linear iterations.
  Four dump endpoints keep every checkpoint byte; the existing focused room
  control exercises nonlinear updates, workers, cold continuation and rollback.
  Terra60 under the same whole-machine lock improves owner mean
  0.764 s → 0.665 s, median 0.711 s → 0.637 s, and p95 1.290 s → 1.054 s.
  Observed completed-frame intervals average 0.996 s → 0.904 s, including
  publication and game-side waiting. The candidate passes with relative
  mass/energy ledger errors `5.61e-16` / `9.06e-14`. Release/83 CTests,
  ASan/UBSan/TSan and clean 83-executable/sixteen-variant comparisons pass;
  only timing fields change.

EOS input validation and the colony's read-only view projection also use the
same pool in fixed 1,024-cell blocks. Cell calculations and view fields are
unchanged. The execution library owns the existing pool/options once, shared
by transport and the kernel. This trades concurrent CPU use for elapsed time.
A fresh whole-lock Terra60 pair measures owner mean 0.665 s → 0.652 s
and observed frame interval 0.904 s → 0.858 s. The last frame's ledger/view
stage takes 98 ms → 35 ms. The candidate passes with maximum relative
mass/energy errors `4.62e-16` / `2.93e-17`. Release/83 CTests and both
focused ASan/UBSan/TSan controls pass. Clean MAC comparisons preserve every
existing result; only timings and the new validation-control PASS line differ.
The cold owner control also covers zero-length checkpoint reads/writes, now
guarded without changing their bytes or format.

World copies share immutable state; an edit or advance publishes a new state
after its checks. Clocks, revisions and rollback remain independent. A matched
high-half frame-50 copy takes 9.5 ms → less than 0.1 ms; flow-only time is
unchanged. Four dump endpoint checkpoints remain byte-identical. Clean
83-executable/sixteen-variant comparisons retain existing outputs, and the
branch/lifetime/continuation control passes ASan/UBSan/TSan.

Untouched positive-zero constituent arrays share immutable storage. Mutable
access materializes an independent array; owned arrays still copy deeply, so
retained mutable references cannot change a copied inventory. Checkpoint
reading compacts only positive-zero arrays and preserves signed-zero bits.
The kernel retains unchanged wall arrays when storing its endpoint. Solver
arithmetic, C ABI records and checkpoint bytes/format are unchanged; the C++
inventory layout changes, so all native libraries must be rebuilt together.
Clean Release/83 CTests and 99 MAC command comparisons pass, with only timings
and the new inventory-control PASS line differing. The native colony diagnostic
is timing-only; focused ASan/UBSan/TSan value controls pass. Heat, settling,
phase and flow checkpoints match on two captured Terra worlds.

High-lock same-state probes on the newer Terra capture give these medians
(six samples after three warmups). Earlier operation results remain alive
during each probe; these are component costs. Whole-lock frame measurements
follow separately.

| Operation | Dense arrays ms | Shared zero arrays ms |
| --- | ---: | ---: |
| Copy cell inventories | 9.63 | 2.58 |
| Heat edit | 9.48 | 3.50 |
| Liquid settling | 17.64 | 10.42 |
| Phase relaxation | 40.89 | 46.36 |
| Flow | 90.06 | 83.40 |

The older Terra dump's flow probe improves 4.30 to 3.51 s. The phase probe's
regression on the newer capture remains visible in the table; no numerical
setting or accuracy tradeoff accompanies the storage change.

A follow-up skips constituent presence/range scans for implicit positive-zero
inventories and initializes the SoA mass array contiguously before copying
owned arrays. Shape, energy, passive and momentum validation still runs;
owned signed-zero arrays retain their original path. Phase pair reads use a
const view, avoiding empty-array materialization. Arithmetic and checkpoint
bytes remain unchanged. Clean Release/83 CTests and all 99 MAC command
comparisons pass with timing-only differences; the native owner diagnostic
also matches apart from timing. High-lock newer-dump flow medians improve 72.31 to
66.13 ms; the older dump measures 2.865 to 3.052 s. Phase-only gains in a
quieter repeat are 0.7–1.2%. These component probes retain prior results and
exclude their first of nine samples. Flow and phase checkpoints match both
dumps; the phase-only probe also matches heat and settling checkpoints.

The follow-up's matched whole-lock Terra60 runs use GPU rendering, profile
17 and identical managed DLLs; no own validation overlaps the runs:

| Speed | Owner mean / p95 / max ms, before → after | Observed mean / p95 / max ms, before → after |
| --- | --- | --- |
| Fast (2×) | 215.2 / 293.7 / 530.7 → 208.0 / 355.9 / 627.9 | 302.7 / 423.4 / 2966.8 → 303.0 / 485.0 / 3030.3 |
| 1× | 229.1 / 430.9 / 489.6 → 218.1 / 378.6 / 451.8 | 329.8 / 505.2 / 3022.5 → 321.7 / 526.3 / 3071.0 |

These single pairs have varying early spikes. Observed means include the
initial UI pause. Frames 10–60 observed means are 258.2→242.6 ms at fast speed
and 269.6→268.8 ms at 1×; those 50-interval subsets do not establish acceptance.
Every run completes without faults or skipped relaxations:

| Follow-up run | Maximum relative mass / energy error | Trace sink kg / J |
| --- | --- | --- |
| parent-fast | 3.22e-16 / 4.22e-17 | 3.384e-08 / 0.0115 |
| new-fast | 2.55e-16 / 4.75e-17 | 3.549e-08 / 0.01322 |
| parent-normal | 2.36e-16 / 3.78e-17 | 3.58e-08 / 0.01304 |
| new-normal | 2.17e-16 / 3.28e-17 | 3.484e-08 / 0.01218 |

The fast-clock target remains open; no numerical or profile setting changes.

Fresh matched whole-lock Terra60 runs, identical managed DLLs, GPU rendering
and opt-in profile 17 measure the complete storage change:

| Requested speed | Owner mean / p95 / max ms, before → after | Observed mean / p95 / max ms, before → after |
| --- | --- | --- |
| Fast (2×) | 289.0 / 421.0 / 741.3 → 211.9 / 367.1 / 463.8 | 375.0 / 727.6 / 2963.1 → 303.4 / 445.6 / 2902.2 |
| 1× | 273.0 / 408.6 / 724.6 → 204.3 / 279.0 / 431.1 | 355.2 / 527.2 / 2863.6 → 294.6 / 405.0 / 2922.2 |

Observed means/maxima include the initial UI pause. Frames 10–60 observed
means improve 308.9→249.0 ms (fast) and 295.9→242.6 ms (1×); these 50-interval
subsets do not establish the required 60-frame fast-clock acceptance. Every
run has zero faults/skipped relaxations and closed ledgers:

| Run | Maximum relative mass / energy error | Trace sink kg / J |
| --- | --- | --- |
| Parent fast | 2.94e-16 / 4.44e-17 | 3.561e-8 / 0.01239 |
| Storage fast | 7.63e-16 / 3.76e-17 | 3.474e-8 / 0.01238 |
| Parent 1× | 3.22e-16 / 3.38e-17 | 3.473e-8 / 0.01220 |
| Storage 1× | 2.76e-16 / 4.05e-17 | 3.701e-8 / 0.01250 |

The 2× target remains open. The separate base-v6 check faults at frame 1621
after slowing to about 10 s/frame and exhausting its attempt budget; life
support and ledgers held before the fault. Its captured flow is under study.
Whole-lock Terra60 owner mean is 0.649 s → 0.638 s; observed frame interval
is 0.848 s → 0.830 s. Both pass; candidate relative mass/energy errors are
`5.25e-16` / `3.35e-17`.

The optimizations above keep the physical model, tolerances and checkpoint format.
Real-time 0.2 s frames remain open.

New colony owners also select the saved gas-expansion volume fraction `0.001`
([subcell gas](MAC_VACUUM.md#unresolved-gas-opt-in)). Eligible input cells expand
before pressure as well as after substeps. This resolves a door-arena case whose
tiny occupied gas volumes otherwise reach the clock's resolution while cooling.
The trade is earlier filling of thin gas fronts; every species, primary U and
momentum is retained, and free-expansion entropy is accounted. The legacy
fraction and all gate settings retain their behavior. Configured owners write
checkpoint version 14.
Matched high-half dump timing is 2.125 s → 0.874 s for the severe pocket
(23 substeps/22 rejections → 13/6); the door fault dump completes in 5.496 s
with 105 substeps/31 rejections. Whole-lock Terra60 passes: owner mean
0.638 s → 0.623 s, observed interval 0.830 s → 0.817 s. Candidate p95 is
1.035 s; relative mass/energy errors are `5.32e-16` / `3.75e-17`.
Release/83 CTests, the focused ASan/UBSan control and clean 83-executable/
sixteen-variant comparisons pass. Legacy outputs retain their values except
timing and the new configured-control PASS line.

## Current active-cell baseline

Unchanged closed-face masks can share the existing state while incrementing
the revision. The full original inventory/EOS admission still runs. Guards
retain negative-zero/full-field normalization and closure of moving faces.
Three alternating high-lock pairs measure median calls at 27.8 → 14.3 ms,
with identical checkpoint hashes. The in-game before/after pair is pending.

External edit batches now avoid repeated full-field work. Independent heat
cells run on the existing pool, retaining each cell's edit sequence and the
original global audit/error order. Fluid transfers decode canonical momentum
once per batch and encode it once before the endpoint audit; injections keep J
and extractions only reduce it. Empty passive batches share their unchanged
state while retaining the revision increment. Admission checks stay enabled.

Matched high-half warm edit controls measure 3,753 heat edits at about
42 → 15 ms, and 40/80 fluid edits at 74.7/135.7 → 20.2/21.6 ms. Every paired
checkpoint and rounding count matches. Whole-lock GPU Terra60 at fast speed
measures mean owner advance 0.3498 → 0.2586 s, p95 0.3960 → 0.3050 s;
observed intervals average 0.4284 → 0.3411 s including startup, or
0.3909 → 0.2963 s over frames 10–60. Relative mass/energy errors are
9.00e-16/4.61e-17 and 4.96e-16/4.61e-17; both pass without faults.
The sink is disabled (0 kg). Release/83 CTests and clean 99-command MAC
comparisons pass; only two new PASS lines and timings differ. Solver arithmetic,
saved settings and checkpoint bytes are unchanged. Fast-speed acceptance remains open.

With NVIDIA harness rendering and fast speed selected, a matched whole-lock
Terra60 pair caps the regional owner at 12 rather than 16 workers. Mean owner
time is 0.4505 → 0.4486 s (no clear throughput gain); p95 is 0.5027 → 0.4894 s.
Mean observed interval is 0.5481 → 0.5326 s, including startup, or
0.5025 → 0.4899 s over frames 10–60. Maximum relative mass/energy errors are
2.38e-16/3.45e-17 and 2.21e-16/3.09e-17; neither run faults or falls back.
The trace sink is disabled. This exact scheduling change keeps checkpoint
bytes and solver arithmetic; a 12-CPU affinity still selects 10 workers.
The new fast-speed target remains unmet. Vanilla fast in the same GPU harness
measures 0.1081 s per tick; older software-rendered results retain their
original measurement context.

At `06e88fd`, including persisted primary U and the pressure-storage rounding
ledger, the production kernel with automatic geometric multigrid, limited-linear
material/momentum reconstruction and the unchanged 0.01 s maximum step remains
over budget. Each capture advances 21 consecutive 0.1-second intervals of the
20 Pa wave; every cell changes, no equilibrium certificate is used, and there
are 212 substeps with no retries. Two captures per size use GNU 15.2 Release/IPO
on the shared Ryzen 9 5900X, pinned to logical CPUs 0–11 (one per physical core).
One-minute host loads sampled every five seconds range from 2.35 to 8.69.
These are current-kernel measurements, not a controlled comparison with earlier
commits under different host load.

| Active cells (shape) | Workers used | p50 ms, runs 1 / 2 | p95 ms, runs 1 / 2 | Workspace B/cell | Peak process RSS B/cell, runs 1 / 2 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 4,096 (64×64) | 4 | 28.75 / 35.55 | 36.70 / 41.03 | 1,642 | 5,765 / 5,959 |
| 65,536 (256×256) | 12 | 261.58 / 303.61 | 277.89 / 329.49 | 1,665 | 3,466 / 3,457 |
| 100,000 (400×250) | 12 | 489.69 / 469.06 | 545.72 / 516.42 | 1,704 | 3,410 / 3,408 |
| 250,000 (500×500) | 12 | 1,312.20 / 1,334.42 | 1,390.68 / 1,388.24 | 1,651 | 3,375 / 3,379 |

Reproduce each shape twice with:

```sh
taskset -c 0-11 python3 tools/run_mac_playtest_benchmark.py \
  --executable build-production/ma_mac_playtest_benchmark --output CAPTURE \
  --width WIDTH --height HEIGHT --samples 21 --profile reference \
  --scene wave --kernel production --threads 12 --no-reference --continuation \
  --timeout 600
```

The paired endpoint checkpoints are byte-identical. Captures, executable/source
hashes, sampled loads and `/usr/bin/time` peak-RSS receipts are in
`local/active-scaling-primary-u-v1/`.
These are solver-only timings: neither 25 ms at 100,000 cells nor 62.5 ms at
250,000 passes, before adapter cost. Workspace is 393.7 MiB at 250,000;
it includes cached multigrid/topology. Peak command-process RSS at 250,000 is
805.6 MiB, including standalone owner copies and the benchmark wrapper. Neither
measurement establishes additional game/adapter memory. Resolved-reference accuracy, colony
rendering and the 32-duplicant workload are not measured by these captures.

**Preconditioner comparison (`cc9e9a0`).** On this wave, automatic multigrid
is slower than the saved `PressurePreconditioner::jacobi` setting. A benchmark
copy that only sets that field ran 8-interval captures, alternating
preconditioners, pinned to CPUs 0–11 with twelve workers (host load 20–23):

| Active cells | Jacobi p50 ms (PCG iterations) | Automatic multigrid p50 ms (PCG iterations) |
| ---: | ---: | ---: |
| 100,000 | 430 / 653 (3,193) | 682 / 601 (694) |
| 250,000 | 1,076 / 1,107 (3,095) | 1,697 / 1,694 (577) |

At 250,000 cells the pressure stage takes about 300–325 ms per 0.1 s with
Jacobi against 910–1,370 ms with multigrid; the other stages are similar.
Each Newton solve needs about 19 Jacobi iterations (about 0.75 ms each).
Serially at 250,000 cells, one V-cycle takes about 13 ms and one `prepare`
about 5–6.5 ms, with no gain from four interval workers. A topology build,
needed whenever the pressure-active mask changes, takes about 161 ms, and the
hierarchy retains about 130 MiB. Multigrid therefore cannot pay for itself on
compliance-dominated gas systems; it needs a cheaper, parallel V-cycle, or an
automatic choice that keeps Jacobi until Jacobi iteration counts grow (as
they do in stiff liquid systems).

The unselected [pressure-window component](MAC_PRESSURE_WINDOWS.md) now uses
its cached macro matrix and a macro preconditioner over all occupied cells.
Its fixed Krylov dot blocks use eight independent double-double chains.
At 250,000 cells, two pinned fixed-source diagnostic runs measure warm
pressure-only costs of 813–905 ms for an all-coarse window and
1,407–2,031 ms for a 15,500-cell stripe taking two local intervals (sampled
host loads 17.26–21.88). The latter needs 16 outer iterations instead of 60.
All prior MAC physical captures and exits remain unchanged by this reduction.
These exclude
remap/audits/adapters and do not replace the evolving-owner baseline above.

## Earlier Jacobi performance

These measurements precede persisted selection 2 and automatic geometric
multigrid. They remain historical diagnostics; current active-cell acceptance
is open.


Method: `ma_mac_playtest_benchmark WIDTH HEIGHT 3 reference wave --continuation
--no-reference --kernel production --threads N` (what
`tools/run_mac_playtest_benchmark.py --kernel production` runs), GNU 15.2
Release/IPO, default production selectors (limited-linear material and
momentum, pressure-sensed compression and shock heat), 0.01 s maximum step,
three consecutive 0.1 s samples per run (10.3 substeps per 0.1 s, the
reference schedule), three runs per size and thread count. ms per 0.1 s is the
median sample of a run; ns per cell-substep divides it by cells times
substeps. The host (Ryzen 9 5900X, 12 cores, 24 logical CPUs) was shared with
a video encoder using about 11 cores and other builds: the 1-minute load was
21-30 for phase 1 and 24-51 for the current sweep. Processes were pinned with
`taskset` (one logical CPU for phase 1; logical CPUs 0-11, one per core, for
the current sweep). Process CPU time is recorded beside wall time.

Phase 1 (commit 08337ed), one thread:

| Cells (shape) | ms per 0.1 s (three runs) | ns per cell-substep | Reference ms per 0.1 s |
| ---: | --- | ---: | ---: |
| 4,096 (64x64) | 60, 65, 79 | 1,420–1,870 | 7,560 |
| 16,384 (128x128) | 310, 332, 349 | 1,830–2,060 | 36,231 |
| 65,536 (256x256) | 1,359, 1,411, 1,418 | 2,010–2,090 | 145,569 (2048x32) |
| 250,000 (500x500) | 6,014, 6,187 | 2,330–2,400 | aborted (scaling analysis) |

Reference figures are from the [scaling analysis](../docs/solver-scaling-analysis.md)
(same host, thread CPU time, four pinned cores). The production kernel is
about 100–120 times faster at the same physics, selectors and schedule.

Current kernel, one thread. Since phase 1: no integer division in face and
neighbour addressing (per-cell face table, column/row-carrying helpers),
stored inverse face inertias, the PCG direction update fused into the
operator application, and parallel load, store, field copies and audit
partials.

| Cells | ms per 0.1 s (three runs) | ns per cell-substep |
| ---: | --- | ---: |
| 4,096 | 61, 70, 88 | 1,450–2,070 |
| 16,384 | 231, 257, 269 | 1,360–1,590 |
| 65,536 | 900, 1,018, 1,064 | 1,330–1,570 |
| 250,000 | 3,563, 3,630, 3,803 | 1,380–1,470 |

Threads (median of three runs, speedup against the one-thread median of the
same sweep; every endpoint checkpoint of a size is byte-identical across
thread counts). The 4,096- and 16,384-cell rows are from a second sweep
(load 43–48) after the thread cap below changed; the 100,000-cell rows from a
third (load 18):

| Cells | Threads used | ms per 0.1 s | ns per cell-substep | Speedup | Process CPU ms per 0.1 s |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 4,096 | 1 | 52 | 1,233 | 1 | 52 |
| 4,096 | 2 | 47 | 1,118 | 1.10 | 94 |
| 4,096 | 4 (4, 8 or 12 requested) | 36–43 | 850–1,014 | 1.22–1.45 | 135–142 |
| 16,384 | 1 | 276 | 1,632 | 1 | 273 |
| 16,384 | 2 | 200 | 1,181 | 1.38 | 393 |
| 16,384 | 4 | 129 | 760 | 2.15 | 448 |
| 16,384 | 8 | 92 | 543 | 3.01 | 540 |
| 16,384 | 12 | 83 | 492 | 3.31 | 561 |
| 65,536 | 1 | 1,018 | 1,503 | 1 | 1,017 |
| 65,536 | 2 | 664 | 981 | 1.53 | 1,246 |
| 65,536 | 4 | 439 | 649 | 2.32 | 1,581 |
| 65,536 | 8 | 313 | 462 | 3.25 | 1,868 |
| 65,536 | 12 | 277 | 410 | 3.67 | 2,006 |
| 100,000 (400x250) | 1 | 1,477 | 1,430 | 1 | 1,465 |
| 100,000 | 12 | 397 | 384 | 3.72 | 2,989 |
| 250,000 | 1 | 3,630 | 1,405 | 1 | 3,618 |
| 250,000 | 2 | 2,445 | 946 | 1.48 | 4,723 |
| 250,000 | 4 | 1,510 | 584 | 2.40 | 5,546 |
| 250,000 | 8 | 1,226 | 474 | 2.96 | 7,394 |
| 250,000 | 12 | 1,164 (1,106–1,267) | 451 | 3.12 | 7,797 |

The kernel never runs more threads than row blocks
(`MacKernelOptions::minimum_blocks_per_thread`, default 1): idle workers
woken for every parallel region only cost time. At 4,096 cells (four blocks,
load 18) four threads gave 2.0 times one thread and twelve uncapped threads
1.6 times. An earlier build with the static-partition pool collapsed
instead (12 threads on 4,096 cells: 1.9–9.8 s per 0.1 s, 25–150 times slower
than one thread, load 30). Scaling beyond four threads is limited by the number of parallel
regions (two per PCG iteration, several per sweep stage) and by the shared
host; the spin-wait shows as process CPU time above the wall time.

Memory: retained kernel workspace (`MacKernelReport::workspace_bytes`: field
banks and scratch) is 4.1, 16.5, 66.2, 101.0 and 252.5 MiB at 4,096, 16,384,
65,536, 100,000 and 250,000 cells, about 1,060 bytes per cell. The whole
benchmark process (source and result owners, checkpoint, kernel workspace)
peaks at 609–611 MB RSS at 250,000 cells, 246 MB at 100,000 and 163 MB at
65,536, independent of the thread count.

The comparison below predates the geometric preconditioner port and measures the Jacobi default.

Phase 1 stage breakdown (4,096 cells, default selectors, one thread, ns per
cell-substep): pressure root and PCG 410, momentum transport 275, shock heat
265, reconstruction 225, compression 215, primal gather/heat/gravity 70,
EOS anchors 40, audit 28.

## Historical comparison with the retired interval port

The narrower gas port used numerical selection 1 and a separate pool. Production
covers the game's current physical selectors and deterministic fixed-block pool.
The accepted consolidation now includes the same geometric multigrid, version-6
saved selection and independent endpoint reviewers. Selector 1 is retired rather
than silently migrated; the strict reference remains the oracle.

Earlier Jacobi comparisons measured about 1.16 s per 0.1 s at 250,000 cells on
twelve workers. Different physical selectors, pressure settings, host load and
range fallbacks affected the retired port's figures. These historical comparisons
do not certify current automatic-multigrid active-cell performance.

## Remaining work

- Real-time target: open. The last measured automatic-multigrid baseline has p95
  1,388–1,391 ms per 0.1 s at 250,000 active cells and 516–546 ms at
  100,000, against 62.5/25 ms, before adapter cost. This `06e88fd` baseline
  includes persisted primary U and the pressure-storage ledger. Colony FPS,
  additional game/adapter memory and the 32-duplicant workload remain unmeasured.
- Persisted production selection and automatic geometric multigrid are
  implemented; measured active-cell timing with these defaults fails the targets.
  The saved reviewers are ported and the narrower interval kernel is retired.
  Larger calm-region steps with local subcycling remain open.
- Thread scaling stops near 3–3.7 times on 12 threads on the shared host.
  Fewer, larger parallel regions (fused sweep stages, a PCG variant with one
  reduction per iteration) and a measurement on an idle host are the next
  steps.
- Diffusion is composed around the flow kernel by the production owner.
  Material selectors 0/1/3 are supported; swept material reconstruction,
  momentum selectors 2–5 and coupled compressive pressure are not ported.
- Below-range primal parcel products and composed diffusion transfers are counted
  and audited. Flagged sweeps preserve range until parcel formation, and dual
  J/K products retain canonical momentum and count their projections. Other
  pressure, regularization and heat-deposition transients still lack complete
  per-value rounding ledgers; interval endpoints and inputs retain range checks.

The conduction diagnostic control uses two 60-frame Terra runs under the whole
CPU lock: report enabled/disabled mean owner advance 1.071/0.960 s, observed
frame spacing 1.350/1.203 s. Both pass without faults; disabled-report maximum
relative mass/energy errors are 1.06e-15/2.57e-16. The game omits only the
per-stage conduction entropy report. Six offline on/off endpoints and four
parent/candidate endpoints are byte-identical. Pipes and terrain fault replays
now complete through production with the report enabled too. These runs use
the owner inputs preceding the later managed cell-announcement change.

New colonies use a 0.1 s maximum timestep; saved colonies retain their saved
value. Two whole-lock Terra60 runs at 0.05/0.1 s measured mean owner advance
1.100/1.025 s, observed spacing 1.378/1.313 s and maximum advance 11.21/5.81 s.
The larger limit trades temporal resolution for fewer solves; the existing CFL
control still shortens fast steps. After-run relative mass/energy errors are
1.02e-15/3.59e-17. An older 0.05 s save loads and advances with exactly that
saved limit under the new default; native config/image checks remain strict.

Regional controller whole-lock Terra60 control at the same 0.1 s limit:
mean owner advance 0.872/0.436 s, observed spacing 1.099/0.566 s and maximum
advance 2.647/0.491 s. All 60 frames pass without faults or reference fallback;
after-run relative mass/energy errors are 3.07e-16/2.46e-17. Three alternating
high-lock offline pairs measure production flow at 0.434/0.103 s on Terra50
and 0.755/0.099 s on the vacuum-expansion dump. Legacy-controller diagnostics
retain their numerical output; regional schedules intentionally change trajectories.

New colonies use the binary32 value `0.1f` promoted to double, so two calm
steps exactly cover the game's promoted `0.2f` frame instead of leaving a tiny
third step. The owner admission cap allows this value; saved limits stay exact.
Matched whole-lock Terra60 runs at double/float 0.1 s measured mean owner
advance 0.457/0.449 s and observed spacing 0.575/0.565 s. Component substeps
fell from 154,779 to 103,477; after-run relative mass/energy errors were
2.91e-16/1.92e-17, without faults or reference fallback. A focused owner control
checks two-step advancement and cap rejection; an old 0.05 s save retains its
limit and closed ledgers. Solver formulas and checkpoint format are unchanged;
new-colony schedules and saved timestep bits change.
