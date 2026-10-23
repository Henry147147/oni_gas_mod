# Private Linux playtest completion

Status reviewed 2026-10-23 against the accepted [base-game plan](playtest-plan.md)
and its overriding [user amendments](completion-plan.md#amendments).
The mod is **not ready for a full private colony playtest**. Catalogue support
exists for the loaded 29 gases and 49 liquids; that does not establish every
material's gameplay, phase change or machine integration.

`python3 tools/playtest_checklist.py status` lists historical adapter coverage.
Its 2,584 implemented entries are system-level implementation records, not
2,584 independent acceptance results. `gate` also checks the completion items
in [private-playtest-status.json](private-playtest-status.json) and fails while
any remains open. These items record existing requirements; they do not change
physics gates, frozen arenas, bounds or the user's decisions.

| Completion item | Current evidence and remaining work | Owner |
| --- | --- | --- |
| Full mixed gas/liquid pipes and ports | Explicit pump/storage 27/27, atomic world output 10/10 and real teardown 10/10 pass. Normal mixed ownership remains off. Finish vents, recipe-specific consumers/storage/reservoirs, phase damage/returns, all readers and activation. | Pipe worker |
| Heat and germs | Correct pipe body capacity uses registered thermal mass, not construction mass. Corrected thermal equilibrium agrees, but the approximately 1.21 J energy residual exceeds the fixed endpoint bound and remains unresolved. Every germ identity must grow/die by medium; minority retention alone is insufficient, in pipes and world cells. | Pipe worker / gameplay owner |
| Conserved gameplay transactions | Existing life-support, plant, critter, source and machine scenarios pass on the packet path. Verify recipe-only consumption, blocked input/output and power/heat accounting on the final mixed path, including storage, bottles, suits and rocket tanks/cargo. A generic converter preflight is not a per-recipe atomicity proof. | Gameplay owner / pipe worker |
| Exact saves with mixed activity | Historical owner saves/load/overwrite and missing-mod rejection pass. Mixed saves are explicitly refused. Finish the pre-header quiescence barrier, durable ports/retries/component scheduling, in-flight manual/autosaves, cold/menu reload, interrupted-save checks and byte-exact continuation. Old saves retain their recorded profile. | Pipe worker / gameplay owner |
| Whole-asteroid reliability | Every base-game asteroid imports; older Oceania/Volcanea checks faulted. Oceania and Volcanea now pass 30 frames on profile 18; longer reliability remains open. The prepared profile-18 late colony stalls after frame 4,383; pass that region before resuming ten cycles. One legacy transfer fault at frame 456 did not recur in a 601-frame retry and is not claimed fixed. | Solver / gameplay owners |
| Numerical acceptance for the selected profile | F1 static split currently fails the vanilla comparison. F2 requires accepted external-heat/kinetic correction; F3/F5 dynamic references and compression/phase corrections remain open; F4 lacks a completed aligned solid/fluid measurement. Finish affected evaporation, steam and freeze/melt checks. New kernel integration is diagnostic until accepted. | Solver / gameplay owners |
| Ordinary player build and presentation | Select only an accepted profile for new colonies; no development fixture required. Verify normal startup, full world/pipe/storage/spill inspection and sensors, responsive pause/menu, clean shutdown, clear faults and missing/incompatible-mod saves. | Gameplay owner |
| Ten-cycle colony acceptance and package | After the shorter failure-region check passes, run the prepared colony ten cycles with every duplicant alive, no owner fault/fallback and closed ledgers, including autosaves. Validate one immutable final bundle, then provide private Linux install/run/known-behaviour instructions and hashes. | Gameplay owner |

The separate Terra 2× target remains open. Latest clean whole-game measurements
are approximately 169 ms owner advance / 258 ms observed frame spacing; the
private approximately 83 ms kernel prototype excludes owner/publication work
and has no game-speed acceptance. Report mean/p95/max, completed frames,
faults/fallback availability, mass/energy errors and trace totals for final
runs, with instrumentation off and explicit CPU/game windows. The accepted
private-playtest scope does not make real-time speed a feature-cutting gate.

Windows, DLC, Workshop publication and the broader public-release matrix follow
this private Linux base-game playtest. No public release is authorized here.

## Current-bundle reliability checks

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
