# Private Linux and Windows playtest completion

Spaced Out! update, 2026-10-27: ordinary DLC activation and the cluster lifecycle
adapters are implemented after targeted multi-world, rocket, teleporter and
cargo controls. The replacement Windows package is undergoing ordinary New
Game and source/cold/menu checks. Keep Spaced Out! enabled for that package;
the older `dde2a09` ZIP remains base-only. Current evidence is in
[DLC compatibility](dlc-compatibility.md). Full release acceptance remains open.

Candidate update, 2026-10-26: `dde2a09` was delivered early at the user's
request; installer startup was repaired separately in `4e069af`. That
candidate passed the prepared 30,000-frame/ten-cycle survival run, but failed
freeze/melt, mixed-filter setup and pump-rate accumulator checks. Full release acceptance remains
open. Remaining candidate checks continue separately from the
[new feature-completion repairs](feature-completion-status.md); neither a
diagnostic repeat nor a later source repair relabels the failed candidate.

Status reviewed 2026-10-25 against the accepted [base-game plan](playtest-plan.md)
and its overriding [user amendments](completion-plan.md#amendments).
The mod is **not ready for a full private colony playtest**. Catalogue support
exists for the loaded 29 gases and 49 liquids; that does not establish every
material's gameplay, phase change or machine integration.

The current target includes Windows x64. WindowsPlayer is exercised under
Proton here; Windows OS behaviour and speed require the user's PC. Configuration
and WindowsPlayer acceptance are now explicit checklist items. The final default
gameplay sweep, asteroid sweep, ten-cycle autosave run and immutable Windows
bundle are still pending. Exact owned-storage heat/biology, byte-exact active
continuation and the remaining accuracy floor are deferred by the 2026-10-25
user amendment; they remain public-release work.

`python3 tools/playtest_checklist.py status` lists historical adapter coverage.
Its 2,584 implemented entries are system-level implementation records, not
2,584 independent acceptance results. `gate` also checks the completion items
in [private-playtest-status.json](private-playtest-status.json) and fails while
any remains open. These items record existing requirements; they do not change
physics gates, frozen arenas, bounds or the user's decisions.

| Completion item | Current evidence and remaining work |
| --- | --- |
| Mixed gas/liquid pipes and ports | On by default with profile5/core23 (flags13), including saves. Pumps, vents, full-inventory filtering, boundaries, valves, machine ports, thermal contacts, cold pipes and readers have current Linux/WindowsPlayer coverage. The final complete same-bundle sweep remains open. |
| Exact owned-storage heat and biology | Deferred for the first playtest under the user amendment. This concerns schema5; current world and mixed-pipe heat/biology are still required. WindowsPlayer all-ID biology passes9/9, the native germ oracle makes247 exact comparisons, and conduit heat replay matches1,032 actual Windows carrier results with zero residual. |
| Conserved gameplay transactions | The current default sweep is in progress. Known fixture defects include unconnected refinery output, cumulative pipe counters used as case-local values, and a warm-ice source staged before its measurement baseline. Corrections must retain the original physical inputs and bounds; none of these observations waives final acceptance. |
| Byte-exact active continuation | Deferred by the user; ordinary saves, natural autosaves and cold/menu loading remain required. WindowsPlayer cold load restores Precise + Strict settings despite a global Default preset. Same-process two-load lifecycle checks pass with restored owner images; these do not prove byte-exact active continuation. |
| Whole-asteroid reliability | Historical imports cover all nine base-game starting asteroids. The final profile5 nine-asteroid sweep and prepared ten-cycle run with three living duplicants, closed ledgers and natural autosaves remain pending. |
| Remaining accuracy floor | Deferred for the first playtest, including F3 levelling and F4 conduction. The strict reference, frozen physics inputs/grids and acceptance bounds remain unchanged. See [the floor document](base-game-floor.md). |
| Ordinary player build and presentation | Windows managed/native builds and86/86 Wine CTests pass. Native fault containment prevents repeated game-loop exceptions and refuses unsafe saving. README captures show actual WindowsPlayer gameplay, mixture inspection and settings under official Proton; Windows OS remains unverified here. |
| Ten-cycle acceptance and package | Pending the final immutable source/bundle after fixture corrections. The prepared colony must advance ten cycles without invincibility, keeping every duplicant alive, no owner fault and closed ledgers, with actual natural autosave files. |
| Configuration and presets | Mods-row settings open; creating/saving/loading a user preset, resetting Default, new Precise + Strict colony creation and cold restoration pass in WindowsPlayer. Loaded colony values are read-only. Native Default bytes remain identical; see [settings](settings.md). |
| WindowsPlayer acceptance | Selected gameplay groups, contact and biology controls, ordinary/cold non-default saves, same-process reload and both quit paths pass bounded runs. The final candidate still needs its default cycle, natural autosave/cold reload, inactive Spaced Out! control and final selected gameplay checks; see [Windows validation](windows-validation.md). |

Source through `f5a6dde` passes the full build:354 Python tests,86 native CTests
and all managed suites. The Windows lifecycle change also preserves every
comparable `ma_mac_*` physical diagnostic result and exit status against clean
parent builds; only validation timing fields differ. These checks do not
substitute for the in-game acceptance rows above.

The separate Terra 2× target remains open. The latest same-bundle WHOLE-lock
Terra60 profile2/core20 versus profile3/core21 pair at requested2× measures
199.543/250.684 versus199.880/252.286 ms owner/observed means: no clear speed
change. The GPU is shared with the user's workload, so these are diagnostic
timings. Offline kernel timings exclude owner/publication work and establish
no game-speed acceptance. Report mean/p95/max, completed frames,
faults/fallback availability, mass/energy errors and trace totals for final
runs, with instrumentation off and explicit CPU/game windows. The accepted
private-playtest scope does not make real-time speed a feature-cutting gate.

DLC, Workshop publication and the broader public-release matrix follow this
private base-game playtest. No public release is authorized here. The detailed
records below describe earlier bundles; the current checklist and the amendment
above determine first-playtest readiness.

## Current-bundle reliability checks

Immutable `codex-resume-oceania-profile5-v1` creates an actual Oceania prepared
base with core23, liquid law1, heat mixing8 and pressure caps400/800. The durable
frame32 source save is reused without alteration. Its physics-only continuation
`codex-resume-oceania-physics1200-v1` reaches frame1232 with all three duplicants
alive, 562.850302 kg of phase conversions, zero owner faults/rejections/skipped
relaxations and no missed samples (1200 owner frames,1199 adjacent intervals).
Maximum relative mass/energy errors are1.46020e-15/1.08465e-14; trace sinks are
2.18949e-6 kg/1.54053 J. LOW-lock owner mean/p95/max is
217.942/250.658/334.282 ms; observed spacing260.233/366.789/533.142 ms.
The shared GPU and CPU diagnostic window establishes no speed acceptance.
Fallback count remains unavailable; package hashes and process cleanup pass.

The separate `codex-resume-oceania-1200-v1` requests ordinary saves and fails
at frame632, its first save after600 new frames. The owner itself remains
healthy with three duplicants and357.106 kg of phase conversions. The barrier
reports a pipe body still at299.99942 K while contact replay expects299.9994 K.
The physics-only result does not waive this save failure. Investigation must
establish actual native work settlement without changing the thermal bounds.

The immutable-package diagnostic `codex-resume-oceania-save-thermal-v1`
repeats the same frame32 source through1200 new frames and both ordinary writes
successfully, retaining a private contact trace. All1200 samples are present,
with no fault/skipped relaxation; maximum relative mass/energy errors are
1.51940e-15/1.10120e-14. This repeat does not establish the cause or resolution
of the earlier one-ULP body-heat refusal. A subsequent isolated control reproduces
that refusal with applied native body work and stale GameData. Republishing after
native completion passes the unchanged control5/5:20 validated contacts transfer
67.993164993822575 J from the body to1 kg of liquid with exact0 J residual and a
later, unchanged zero-energy probe confirming actual capacity and publication.
The longer Oceania saving repeat with this fix now passes, as recorded below.

`codex-resume-oceania-publish1200-v1` uses an immutable package with the actual
save-publication fix and diagnostic instrumentation disabled. The unchanged
frame32 source advances1200 sampled frames to1232 with three duplicants alive,
zero faults/rejections/skipped relaxations and no sample gaps. Both ordinary
saves pass; the barrier records two native publications and two body-heat
checks. The final durable owner image is frame1233 because the worker is one
frame ahead of the sampled endpoint. Maximum relative mass/energy errors are
1.3060620601475641e-15/1.0762250083960142e-14, with trace sinks
2.214235923029314e-6 kg/1.5520861572407771 J. LOW-lock owner mean/p95/max is
214.570684/229.777469/315.412347 ms; observed spacing is
257.097545/354.961260/1084.145968 ms (1200 samples/1199 adjacent intervals).
Source/package hashes and process cleanup pass. This covers schema4's existing
native-item storage path; it does not establish exact owned-storage schema5,
whole-storage heat closure, natural autosaves or ten-cycle acceptance.

The exact-storage thermal fixture has a complete sealed-region and body census
at both zero-time endpoints. With hidden loose actors removed before the source
is established, `codex-resume-storage-heat-control-v3` has unchanged world mass
but gains5488.7846115393422 J against the fixed4.9810592011375575e-6 J bound.
The unchanged-input v4 repeat gains5947.813901577032 J. Its managed trace shows
paired old/new absolute projection-temperature messages and delayed publication
oscillations. Native before/after measurements are required to establish the
cause; no compensating ledger, changed physical input or relaxed bound is used.

The opt-in `pipe-schedule` diagnostic passes6/6 on core23. It captures two gas
cells/two networks/three callbacks and five liquid cells/five networks/four
callbacks, including empty pipes and a state-machine callback. At the same
physical boundary it changes and restores clocks, native staging, routing,
network traversal and callback order, reproduces the original1067-byte image,
then continues positive time with exact physical accounts. This verifies the
runtime rebind primitive, not complete schema5 cold/menu save continuation;
the new writer remains gated on complete storage and native context capture.

Immutable `package-private-readiness-v1` uses the unchanged `79e0545` native
pair and `cda2aa1` managed code. New colonies explicitly select profile18
(flags3), with fast phase off and diagnostic instrumentation off. LOW-lock
measurements may overlap HIGH work and establish behavior, not performance.

| Asteroid | Completed owner / observed intervals | Owner mean / p95 / max, ms | Observed mean / p95 / max, ms | Maximum relative mass / energy error | Trace sink kg / J | Result |
| --- | --- | --- | --- | --- | --- | --- |
| Oceania | 30 / 29, no missed samples | 972.360 / 2831.906 / 5904.672 | 1044.371 / 2376.298 / 2866.135 | 6.462e-16 / 5.499e-17 | 8.811e-7 / 0.275235 | No fault/skipped relaxation; process cleanup verified. The earlier frame-8 startup failure is absent in this bound. |
| Volcanea | 30 / 29, no missed samples | 417.899 / 583.470 / 694.893 | 479.716 / 682.616 / 699.427 | 1.008e-16 / 4.124e-17 | 2.060e-8 / 0.008868 | No fault/skipped relaxation; process cleanup verified. The earlier frame-12 startup failure is absent in this bound. |

No separate fallback counter is exposed. The 30-frame result establishes no
ten-cycle or speed acceptance, and does not erase earlier failing profiles.

## Diagnostic game-kernel checks

The new opt-in positive-volume profile3/core21 (`d0b79bb`) completes60 frames
each on Terra, Oceania and Volcanea with zero faults/rejections/skipped phase
relaxations. It fixes the earlier core20 startup failures on the latter two
worlds. The [game-kernel report](../native/MAC_GAME_KERNEL.md#positive-volume-profile)
records mean/p95/max, mass/energy errors, trace totals and same-bundle normal1×
and requested2× comparisons. Fallback count remains unavailable. The normal
profile20→21 owner means are199.855→201.837 ms and observed means
249.029→244.796 ms; neither speed pair establishes an isolated improvement.
Old saves retain their law, and the production default is unchanged. Its phase,
floor/reference and long-run checks remain separate acceptance work.

The same immutable core21 bundle subsequently passes all ten unchanged
evaporation/vapour, door and freeze/melt checks in139 frames. All139 owner
samples and138 adjacent intervals are retained, with no gaps. LOW-lock
owner mean/p95/max is222.231/270.107/306.018 ms; observed spacing is
269.025/349.752/839.975 ms. Maximum relative mass/energy errors are
3.600e-16/1.292e-15 and the trace sink is2.48880e-7 kg/0.263928 J.
There are zero faults/rejections/skipped relaxations; fallback count remains
unavailable. Package hashes are unchanged and process cleanup is verified.
This is focused phase/terrain coverage, with shared GPU load; it establishes
neither full mixed-pipe behavior, floor acceptance nor sustained speed.

A separate immutable diagnostic bundle from `d9645ea` creates a fresh
profile3/core21 base, passes all four base checks with three duplicants and
writes an ordinary save at durable frame32. Its frame1 pre-flow image verifies
physical checkpoint21. The read-only all-passive control passes32 observations
with32 rows and zero stale reads. The sampled physical window is31 frames/30
adjacent intervals, with no gaps; maximum mass/energy errors are
3.944e-16/4.294e-16 and trace totals6.30185e-8 kg/0.0589007 J. There are zero
faults/rejections/skipped relaxations, unchanged package hashes and verified
cleanup. Instrumented LOW-lock owner mean/p95/max is392.584/460.855/461.664 ms;
observed spacing547.699/945.063/1300.416 ms. These are diagnostic timings.
Cold/menu restore and saved-law readback remain pending at the user's stop.

That run pairs258 native view calls with258 managed publications. At frame10
onward (152 publications), native mutex wait mean/max is0.0000654/0.000830 ms;
native work mean/max0.095795/0.881882 ms; the managed view lap mean/max is
0.148526/0.906 ms. No collection-count changes are observed. The earlier153 ms
view outlier is absent, so its cause remains unresolved. Logging and passive
controls are enabled here; these numbers establish no final timing acceptance.

The opt-in kernel3/core20 corrected bundle runs in the actual game, with no
default or old-save migration. Its whole-lock Terra60 result is
210.27/226.40/275.22 ms owner mean/p95/max and
266.07/404.87/766.01 ms observed spacing. Mass/energy errors are
1.673e-16/7.099e-16, trace sink 1.183e-7 kg / 0.12784 J; 120 half steps, no
rejection, fault or skipped phase work. The adapter trace metadata defect in
the first diagnostic was corrected before this result. The new bridge remains
slower than profile18; these results establish neither speed nor floor acceptance.

A hash-identical corrected bundle then passes all ten existing evaporation,
door and freeze/melt checks in 139 frames. Mass/energy errors are
3.891e-16/1.472e-15, trace sink 2.508e-7 kg / 0.262600 J, with no
fault/rejection/skipped relaxation and verified cleanup. LOW-lock diagnostic
owner mean/p95/max is 222.214/338.487/364.019 ms; observed
269.668/390.172/844.487 ms (139/138 samples, none missed). This is phase and
terrain smoke coverage, not all-machine, mixed-pipe, save or long-run acceptance.

The same immutable corrected kernel3 bundle also builds a fresh prepared base
and writes its ordinary managed save, with all three duplicants
alive and all four base checks passing. Saved flags are 5; no legacy save is
migrated. Maximum mass/energy errors are 4.034e-16/4.083e-16, trace sink
6.353e-8 kg / 0.058867 J, with no fault/skipped relaxation and verified
cleanup. The old cached report said frame 31; cold load established that the
durable native image was frame 32. Save metadata now reads the frame from the
returned image rather than the asynchronously cached status; native bytes and
solver arithmetic are unchanged.

Cold load of that same save restores flags 5 even with both new-colony profile
selectors set, and advances frame 32 to 92 with three duplicants alive. There
is one load, no save, no fault/skipped relaxation, and verified cleanup. Maximum
mass/energy errors are 5.001e-16/7.530e-16, trace sink 1.743e-7 kg / 0.183282 J.
LOW-lock owner mean/p95/max is 380.768/426.751/495.950 ms and observed spacing
484.173/635.552/689.296 ms; all 60 newly advanced intervals were sampled. The
restored 32-frame prefix is not missing measurement data.

The harness admits explicit loaded-owner overwrite and same-process menu
reload controls. The latter checks owner shutdown, a new session, source-save
hash, colony identity and saved flags across two loads; it does not establish
byte-exact physical continuation without an input transcript.

The loaded-owner overwrite run completes two ordinary writes to the same
path, at durable frames 63 and 93 after restoring frame 32. All three duplicants
survive; game validation passes and process cleanup is verified. The last
sampled physical frame is 92, with mass/energy errors 5.311e-16/9.283e-16,
trace sink 1.744e-7 kg / 0.182721 J, and no fault/rejection/skipped relaxation.
LOW-lock owner mean/p95/max is 224.681/240.183/343.756 ms; observed spacing
843.777/1505.081/18627.540 ms includes save pauses. All 60 newly advanced
intervals are sampled; these timings are not performance acceptance.

The sampler rejects the stale final save count: the physical status JSON was
written before the second write and said one save, while the game log and
durable-frame metadata record both. Completion reporting now refreshes save
counters and adds a separately labelled native completion status, preserving
the sampled physical frame. Cold/menu readback of the overwritten file,
in-flight callbacks and full mixed-pipe saves remain open.

The first frame-93 menu control fails: the old endpoint code compares the
restored absolute frame with a 30-frame request before initializing its
baseline, completes immediately, and attempts menu teardown during startup.
ONI reports `Game.StopBE` null errors; the owned process group is stopped and
verified gone. The file's frame-93 admission is established, but it has no
continuation/menu acceptance. The frame window now initializes before the
comparison and counts new frames using subtraction, with explicit controls
for zero, large saved frames and the integer limit. A repeat remains required.

The corrected window repeats frame 93→123 with all 30 new intervals sampled,
three duplicants alive, no owner fault/rejection/skipped relaxation, mass/energy
errors 5.311e-16/1.227e-15 and trace sink 2.276e-7 kg / 0.237336 J. Ordinary
menu teardown still fails independently at `Game.StopBE`; no second session
starts. Cleanup is verified after explicitly stopping that bounded failure.
Inspection identifies two unguarded Unity `Component.gameObject` reads of
destroyed save roots in the native managed cleanup path. A narrow quit guard
preserves every original instruction except those two getters, using Unity's
existing null check before access. Installed-layout and unknown-layout controls
pass; actual menu acceptance still requires its game repeat.

With that guard, the overwritten frame-93 save passes cold load and actual
same-process menu reload. Both sessions advance to frame 123 (30 new frames,
31 samples each, no gaps). Session 1 retires through shutdown/main menu and
session 2 restores the same source hash, colony GUID and saved flags 5.
Loads are 2, saves 0; all three duplicants survive. No owner fault, rejection,
skipped relaxation, presentation error or renderer error occurs; cleanup is
verified. Mass error is 5.311e-16 in both legs; energy errors are
1.218e-15/1.216e-15. Trace sink totals are 2.27547e-7 kg / 0.237334 J and
2.27599e-7 kg / 0.237344 J.

LOW-lock owner mean/p95/max is 202.548/226.834/305.944 ms then
202.870/218.786/232.084 ms; observed spacing is
916.629/2073.797/17455.309 ms then 905.596/1336.330/18511.905 ms. These
include startup/pauses and are lifecycle diagnostics, not speed acceptance.
The two trajectories have no identical input transcript and are not claimed
byte-identical. In-flight callbacks and mixed-pipe manual/autosaves remain open.

## Paired native pipe heat trace

The unchanged legacy thermal package reproduces the existing failure at its
fixed 601-frame endpoint: 4/5 checks pass. A private observer pairs all 587
managed contacts with 587 actual native flow/clamp/temperature call triplets.
The observer preserves native return bits, rounding/flush modes, exception
flags and errno in 240,000 direct-versus-detoured controls. The installed-game
hash and actual game-process load are verified; initial attempts hidden by the
isolated home mount are excluded from evidence.

| Quantity | Measured joules |
| --- | ---: |
| Native contact heat delivered to fluid | 90,673.992929773 |
| Heat inferred from the rounded carrier temperature | 90,674.229153523 |
| Actual registered-body heat loss | 90,675.287708655 |
| Native heat minus inferred carrier heat | -0.236223750 |
| Body heat loss minus native contact heat | 1.294778882 |
| Closed body-plus-mixture residual | -1.058555132 |
| Fixed endpoint error bound | 0.245883836 |

The registered body capacity is 1,940.000057 J/K (5 kg), and the two-species
mixture capacity is 1,702.5 J/K (1 kg). Final body/mixture temperatures are
353.260162354/353.259457704 K against the analytic 353.260123613 K equilibrium.
Mass and exact pipe-inventory accounts pass, but heat conservation does not.
Using the native contact heat alone would still exceed the fixed bound.
Replaying each represented body update as
`f32(Tbody + f32(Qnative / Cbody))` reproduces the complete actual body loss
exactly: 90,675.287708655 J, with zero difference. All 587 pre-call managed
snapshots match the actual native dt, fluid/body temperatures and capacities
bit-for-bit after binary32 projection. The remaining work is a scoped contact
adapter that credits represented body work to its physical mixture. No guessed
endpoint residual ledger or relaxed bound is adopted.

The run completes the fixed scenario with no owner fault, no skipped phase
relaxation and 11,065 legacy rejected attempts. Owner maximum relative
mass/energy errors are 1.771e-15/5.865e-17; the trace sink is zero. Process
cleanup is verified. The owner world audit does not include the independent
pipe/body contact account and cannot erase that failed thermal check.

The explicit represented-body-work adapter subsequently passes the unchanged
601-frame thermal check **5/5**. Across 586 contacts, the body loses
90,674.695667622 J and the mixture/booked account gains exactly that amount:
residual 0 J against the original 0.245883883 J bound. Body/mixture temperatures
353.26046753/353.25973172 K agree with analytic 353.26012361 K; the full 1 kg,
two-species inventory and exact U account pass. Actual carrier validation
succeeds throughout; no private native detours are used for this acceptance.
Maximum owner mass/energy errors are 2.964e-15/5.533e-17, with no fault or trace
sink, 13,704 legacy rejected attempts and zero skipped relaxations. All
601 samples/600 intervals are present. Diagnostic LOW-lock owner
mean/p95/max is 359.812/454.501/718.199 ms and observed spacing is
456.684/630.994/4540.225 ms; cleanup is verified. The new behavior is explicitly
selected for the control; full mixed saves and production activation remain
gated. Gas/liquid phase and capacity-range checks remain open.
