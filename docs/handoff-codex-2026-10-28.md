# Handoff to Codex, 2026-10-28

Claude's session on Steam readiness and real-time speed stopped here at the
user's request. This page is enough to resume. Read [AGENTS.md](../AGENTS.md)
first, then the 2026-10-28 entries in the
[completion plan amendments](completion-plan.md#amendments) and the rows
"Performance", "Boil-only phase change", "Speed work, 2026-10-28" and "Smooth
input and UI" in [feature completion status](feature-completion-status.md).

## Codex continuation, 2026-10-28

The preserved work below has now been recovered onto `main`:

- **A is implemented.** New colonies use profile8/core27 with boil-only.
  Sharpening and layering carry both momentum components, book closed-face
  impulses and retain the fixed conservation bounds. Controls cover separate
  correction clusters, shared layering faces, both gravity signs, the exact
  1 mg threshold, massive inflows, worker equality and cold continuation.
  The fresh Spaced Out! replay (frames 4–43) limits the watched magma face to
  −24.84..+5.04 m/s, versus −2,080.92 m/s on profile7. Matched-script mean
  owner time falls from 0.764 to 0.583 s (23.7%) on the contended 5900X.
  The twelve-dig replay (505–584) stays within 32.346–34.370°C,
  1.64923–1.68182 kg and 249.755–254.394 kPa, without an owner fault.
- **B's allocation and logging changes are implemented.** Original/current
  readings and presentations match across 60,000 generated cells and 528
  boundary cases, including all public properties and rejection messages.
  Fixed-JIT .NET construction allocates 64–68% less for 1–5 constituents;
  this is not a Unity GC acceptance result. Advanced → Performance →
  **Log frame timing** works without environment variables; native phase
  timing is available with `MA_COLONY_NATIVE_WAIT_LOG=1`. The stopped
  scheduler-niceness experiment was omitted. The one-GC/15 s target and the
  main thread's native wait remain open.
  A fresh GPU Linux Terra run passes 180 normal-speed frames with no owner
  fault or presentation rejection. Its single clean 15 s window has two GCs,
  108.8 ms mean GC-containing frame time and frame p95/p99 of 55.1/103.4 ms.
  Main-thread blocking p99 is 51.4 ms; native phase logs show about 9–10 ms
  mean next-frame owner waits (up to 113 ms) and 13–14 ms in UpdateData.
  Startup windows are excluded. This is not a paired GC comparison or a
  Windows acceptance run.
- **Native verification:** both clean parent and candidate pass 89 CTests.
  All 87 short MAC programs retain their exit codes and numeric output;
  only new/updated profile8 PASS lines differ after masking the existing
  `ref_ms`/`prod_ms` timing fields. Long gates were not included.
  The Python and managed suites pass too. The extended managed run exposed
  an already-stale checkpoint26 metadata expectation; it now checks saved
  law/caps for 26/27 and keeps the unknown28 control, and all remaining checks
  pass after that repair.
- **D predates today's changes.** Immutable `16da60c` (profile6,
  equilibrium phase rule) also fails freeze/melt at 357.9 kg and airflow at
  4.278→4.052 kg. Airflow's liquid barrier still passes. Inputs and bounds
  are unchanged; the causes remain open. Freeze/melt used llvmpipe,
  airflow NVIDIA. Possible airflow diagnostics are coherent bulk sampling
  and wet-face closures; neither is a confirmed cause.

### 3× work started after the recovery

- **C now has a verified experimental solver core.** Profile9/core28 persists
  per-cell activity, pending time, deadlines and actors. It controls pristine
  pause, staggered remote stepping, wake without catch-up, splits/merges,
  worker equality and exact cold continuation. Always simulate retains profile8
  physical fields bit for bit. Malformed clocks and non-progressing or excessive
  schedules reject. New colonies remain profile8; normal game loading refuses
  experimental28 saves until the full owner pipeline is integrated.
- **Throughput measurement is repaired.** Independent absolute owner/game clock
  deltas use monotonic wall time, including pauses and skipped completions.
  Matched submit-through-managed-publication latency is separate from the
  partial native work timer. Final partial windows and
  `colony_owner.Performance` retain short-run results. Diagnostic queues are
  bounded and report dropped samples. The focused suite passes 81 checks.
- **Exact transfer work is reduced.** Reuse the batch's EOS table and omit
  unused full-reading arrays/entropy, retaining rare validation through a
  conservative fallback. Two private actual 78-material states, two withdrawal
  fractions and two repeats give 771,324 withdrawals per implementation in
  192 batches, with identical checkpoint bytes and serialized receipt fields.
  Isolated batch work falls 25–36% under the whole-machine lock; this is not a
  whole-game gain. See [transfer details](../native/MAC_GAME_KERNEL.md#exact-transfer-reading-reuse).
- **Verification:** all 89 Linux Release CTests, 437 Python tests and the full
  managed/game-contract suites pass. All 87 short MAC programs retain their
  exit codes and numeric output from clean parent/current builds; only two new
  PASS lines differ after masking existing `ref_ms`/`prod_ms` fields. Long
  gates were not run. Evidence: `artifacts/region-tiers-2026-10-28/`.
- **Actual Linux speed is measured, still below 3×.** The frozen private
  profile8 Terra run passes at frame450. Warmed frames151–450 achieve
  1.938× owner / 1.935× game progress, versus partial capacity estimates near
  3.2×. Owner work averages 65 ms, submit-through-publication 109–115 ms.
  Two clean 15 s windows each have five GCs and frame p99 123/120 ms. There
  are no dropped timing samples, pauses, owner faults or presentation
  rejections. See [full measurement scope](performance-acceptance.md#linux-ultra-probe-after-the-first-implementation)
  and private `linux-3x-run/measurement-summary.json` under the evidence folder.

### Full activity integration

The owner pipeline now selects profile9/core28 for new colonies (flags29).
Owner wrapper14, reserved in `3a855ee`, saves immutable census packets, exact
input cuts, FIFO sequence watermarks and fractional biology clocks; outer
callback wrapper13 can contain it. Restored packets are claimed once without
regenerating actor inputs, including migration of pre-owner native actors.
The managed census includes inactive actors, occupied/placement cells, rotated
primary/secondary ports and old/new motion/removal contacts. Native body
contacts and accepted external inputs wake regions; passive solid conduction
alone does not. Zero-time lifecycle refresh and policy changes preserve both
physical clocks. Germs, conversions, trace and space sinks follow the region's
due cells. Older profiles retain Always simulate. Gameplay → Region activity
chooses Tiers or Always simulate for new colonies.

Held regions now skip local extraction/EOS; wall connectivity is cached and
dormant dense fields retain even signed-zero and partial-volume bits. Global
audits remain. Native readings copy directly from a pinned immutable
publication; event appends/prepends copy directly into their final unmanaged
buffer. Controls cover byte order, aliasing, rejection and zero managed
allocation for the event-copy path. These are exact optimizations, not a Unity
GC acceptance result.

Clean Linux and Windows x64/Wine builds pass all 89 native CTests. The final
Python suite passes 439 checks, with all managed/game-contract suites passing.
All 87 short MAC programs retain parent exit codes and existing numeric output
(existing timing fields masked); only the new regional execution PASS line is
added. This includes identical baseline nonzero/usage exits, not a claim that
all diagnostics pass. Replay covers two saved pending packets and rejects a
deliberately dropped callback. Managed activity/readiness/resave checks cover 209
synthetic cases plus 12 actual native wrapper images, with 26 pacing-loss checks.
Focused owner controls passed ASan/UBSan, as does the isolated regional scheduler
control. The complete instrumented game-kernel test hits an unchanged zero-add
control failure; the old HEAD test reproduces it with the same header/compiler
flags, without a sanitizer memory diagnostic. Its finite-input diagnostic sweep
has no mismatch. Keep this limitation rather than claiming a full ASan pass. See
[activity controls](region-activity-controls.md). Evidence lives in
`artifacts/tier-integration-2026-10-28/` and
`artifacts/region-tiers-2026-10-28/native-owner/`.

The first actual startup exposed an OccupyArea cache read before initialization;
it is guarded. The native fault path retains the originating adapter failure.
The pending-save control then exposed premature release of a supported actor
before OnSpawn and a zero-time native flush releasing protected requests.
Managed binding now preflights all requests, waits for supported late actors
and rolls back partial callback creation on failure. Native zero-time flushes
preserve requests within saved packet cuts and refuse an unresolved tail.
The deferred paused-save control waits for actual component startup and compares
the complete before/after owner bytes. Base-game r7 passes all 16 lifecycle checks;
its cold load/resave retains pending 1 at saved frame 34, rebinds the saved actor
once and releases none. A second normal cold load reads the exact writer owner
hash from the produced file, reencodes it exactly and claims the packet once.
Both final loads have no creature/SMI serializer errors or owner fault.
Pre-owner native migration remains controlled in the implementation but its
nonzero live branch was not observed (the real runs report 0 migrations).

The diagnostic-free r6 Terra probe reaches about 2.80× warmed owner/game progress
versus 1.938× in the profile8 baseline (about 44.5% faster), still below 3×.
Partial owner work falls from about 65 to 30 ms; native UpdateData remains about 14
ms. A clean 15 s window has 3 GCs and frame p99 95 ms. Its loader uses the earlier
callback path, so this is throughput evidence only. The general regional
scheduler cuts the short digging prefix 18% and keeps the fresh Spaced Out!
path serial when hot regions compete. All 80 frames of the full twelve-dig
trajectory retain the original physical bytes and owner hashes.

The opt-in pacing trace explains nearly all the remaining warmed Terra gap:
over 300 owner frames, raw offered time 63.846806 s loses 2.558634 s to the game's
original 0.2 s argument cap and 1.281474 s to pacing, admitting 60.006697 s.
Other-guard loss and all diagnostic errors are 0. The corresponding wall span
is 21.289978 s. A bounded time-retention experiment remains private/off by
default. It passes 17,138 accounting and 81 real Mono/Harmony controls but
reaches only 2.862× owner / 2.861× game, a small gain with saturated debt and
dropped time. Do not adopt it or claim 3× from the traced losses. The original
diagnostic changes no dt, cap or fence.

Exact heat validation reuse is now implemented for batches of at least 32
edits; smaller calls keep the original path because table setup regresses them.
It reuses transfer EOS evaluation and its absent-liquid rejection fallback,
with lazy setup after the original argument and representability checks.
The private differential passes 6057 comparisons over 2019 cases, including
refusals, revisions, rounding and complete checkpoint bytes. Two actual
79-species journal cohorts and 96 owner-frame executions preserve physical,
full-owner and receipt/job hashes. Heat-stage replay means improve about 30–33%;
complete fresh owner time is unchanged within noise. All 80 frames/all twelve
digs retain the original physical and owner hashes. Final Linux and Windows
x64/Wine each pass 89 CTests; the full managed/game-contract suites and 439
Python tests pass. All 87 short MAC outputs and exit codes are retained except
the added heat-control PASS line. Checkpoint formats and solver arithmetic are
unchanged. Evidence: `artifacts/tier-integration-2026-10-28/heat-exact/`,
`heat-native.log`, `heat-managed.log` and `mac-heat-final/comparison-reviewed.json`.
The heat-only actual Linux Terra450 run passes at 2.7469× warmed owner /
2.7470× game speed. Heat falls slightly to 8.2/8.1 ms, while transfers rise to
10.3/10.5 ms against the earlier run's 4.8/6.8 ms; runtime inputs differ, so this
does not prove an overall speedup or explain that increase. A clean 15 s window
has two GCs averaging 146.3 ms, frame p99 94.74 ms and 166,368 lazy cell reads.
See `linux-heat-only-3x-r7/compact-summary.json` under the same evidence folder.

The Spaced Out! cohort is now complete on Linux using the original frozen r7
bundle: nine worlds/79 species, all 16 lifecycle checks, a pending packet saved
at owner frame 33 for sequence 34, cold paused resave and a second ordinary
cold file readback. Both loads resume 80 frames; each rebinds the actor once,
releases none and claims one packet. The readback restores the exact resave
writer owner hash `005c76cc502e9627c4e29d33dc91ceb9a0ac59225341ce1f98bd00bc6b42a48d`.
Mask checks are 27/59/59 for source/load/readback, with no dropped checks, owner
faults or creature/serializer errors. Migration stays 0 and full resumed-world
equality against the uncontrolled source branch remains open.

The user completed normal Steam sign-in. A second pre-world refusal then exposed
the real content issue: the shared game copy lacked 422 installed DLC assets.
The official Steam installation has them; all 132 managed DLLs and eight
native/code binaries match the shared copy. The successful cohort uses that
official directory and ordinary content-mask checks, without an ownership
override. Evidence: `linux-activity-dlc1-{source,load,readback}-r7-installed/`
under the same artifact folder. Do not repeat the old sign-in question.

### Passive-edit optimization and longer speed probes

The higher “transfers” time is chiefly biology in the actual base-game
frames151–200 capture: 7.410 ms, including 4.268 ms cloning the passive-edit
state. Census/scheduling cost 0.978/0.927 ms; transfer application only 0.252 ms.
The owner now releases its previous activity snapshot after its last history
read, then consumes the unique game state through an expiring passive-edit
operation. Shared/non-game states retain the old copying path. All validation,
allocation and result construction precede nonthrowing commit. Empty/nonempty
cache policy, arithmetic and checkpoint bytes remain unchanged.

Two reversed 16-frame runs per scene retain every physical/full-owner byte and
canonical receipt record. With two warmup frames excluded per run, native owner
means improve 31.836→27.057 ms in base game and 68.518→54.736 ms in Spaced Out!
(15.0%/20.1%, 28 warm samples/mode). This excludes ONI/managed/rendering.
All 89 Linux Release and 89 Windows x64/Wine tests pass, along with 439 Python
and the full managed/game-contract suites. All 87 short MAC programs retain
existing output/exit codes, apart from the exact new passive-control PASS line
and the two usual timing masks. Seven nonzero diagnostic/usage exits are
unchanged. Full-dependency ASan/UBSan focused controls pass 977 checks and 42
allocation-refusal positions plus owner lifecycle tests. See
`passive-expiring/`, `passive-{native,managed}.log` and
`mac-passive-final/comparison-reviewed.json` under the tier-integration artifacts.

Two actual 900-frame games with the new native code both pass. A private
observer experiment holds binaries fixed and reduces full harness report
cadence from every update to every 30 updates, preserving per-update clock and
all lifecycle/target/fault checks. Report cost falls 5.679→0.311 s; warmed
owner/game throughput is 2.641/2.638× versus 2.588/2.590×. Both reach about
2.827× early and slow later; each has 3/4/5 GC frames in three clean 15 s
windows. This single ordered pair has differing gameplay inputs and proves no
whole-game speedup. Ordinary play skips this configured harness report path.
The observer remains private. See `harness-overhead/README.md`.

The bounded compact texture cache is now integrated, enabled by default with
`MA_COLONY_COMPACT_TEXTURES=0` retaining the original renderer. Viewport preflight
and guarded optional getter binding preserve fallback for unsupported/oversized
batches and lifecycle context failures. The original rich constructor and full
color/off loops stay independent. Tracked `tools/CompactTextureTests` runs in
both default and opt-out processes through `tools/build.py`: each passes
569,496 scalar/cache/error/lifetime/viewport controls, including source/value
oracles and deliberate mutation negatives. Existing original-value goldens stay
separate. The full official managed/game-contract suites and 439 Python tests
pass; Windows managed contract checks 2,587 dependencies with zero differences.
Native sources/DLLs are unchanged from the 89-test passive candidate.

The private compact-on 900-frame run passes at 2.667× warmed owner / 2.668×
game speed versus cadence30's 2.588/2.590×. Three clean-window GC counts fall
3/4/5→1/2/3, approximate heap growth 188/269/345→60/127/155 KB per Unity frame
and p99 90/104/146→32/101/108 ms. This is one ordered comparison with differing
runtime inputs, not a precise causal speedup estimate. The cache admits about
95% of batches with about 95% hits; 126,883 retained numeric slots are below
the 196,464 bound. .NET warmed allocation controls pass; actual Mono allocation
counting is unsupported. Same-runtime original/current captures match
118,750,539 bytes; the separate actual CPU destination proof matches 6,144
texture samples without mismatch, not GPU readback. See `compact-observer/`,
`linux-compact-observer900/` and [performance scope](performance-acceptance.md#cached-texture-scalars).

**Receipt-budget investigation:** completed, with the original admission policy retained. Existing publication
logging in a separate diagnostic run attributes 3.543 of 3.554 s of dispatch
to 35,970 byproduct calls. Copy/append/ack costs are only 1.75/2.80/3.06 ms
in total. Five ready counts exceed 1,500, but unbounded long batches also occur
below that threshold. Game API inspection confirms ordinary low-dt polling
calls `StepTheSim(0)`; the old `StepSeconds > 0` budget gate therefore also
bypasses ordinary polls. New optional logs capture step duration, admission,
budget, maximum single spawn and collections, without changing policy.
A private explicit settlement-scope candidate passes 102,350 controls and an
old-predicate negative control fails as expected. Its actual 900-frame run
regresses warmed owner speed 2.667→2.579×; GC frames stay six and late p99 rises
108→148 ms. Budgeted 920 / deferred 537 / backlog override 21 / maximum copied 1,600 shows the
per-publication capacity problem; the published pending-count sample 0 is
ineffective, not an empty-queue proof. It is parked, with no actual save or
lifecycle acceptance claim. The updated diagnostic separately confirms 92
zero-time unlimited batches below the backlog threshold, 15 stalls over 50 ms,
and a 129 ms maximum. Actual final frames are 906 in that diagnostic and 908 in
the candidate. Keep the original production policy. The replacement design drains only already-published, sequenced byproduct
prefixes in idle Unity frames without extra physics/publication calls. Its
production integration and custody proofs are recorded below.
Byproduct counts are receipt deliveries, not newly created objects; the game
merges before instantiating. See `linux-compact-publication900/` and
`receipt-budget/` under the artifacts.

A second private pacing experiment retains a configurable maximum of 2 s,
leaving the original 0.4 s default, 0.2f argument cap and 1f/60f repayment cap.
The original 17,138 controls plus 102,908 added checks pass; real Mono/Harmony
passes 81 original plus five configured checks in each separate bound process.
No production pacing changes are adopted. The actual 900-frame run reaches
2.928× warmed owner/game progress; windows are 3.014/2.994/2.785/3.018/2.845×,
so sustained 3× remains unmet. Debt reaches 2 s and drops 1.782 s of offered
warm time; accounting stays clean. GC frames rise from six to eight. The
original loss logger is disabled in favor of retained accounting, so record
that observer difference. See `pacing-retention2/`, `retention2-run/` and
`linux-retention2-900/`; preserve the old baseline and limitations. A further
private repayment-cap variant tests 1f/30f with original defaults/hard cap/fence
retained; all prior controls plus 101,886 new assertions and Mono/Harmony
controls pass. Actual-game and save/lifecycle gates are still pending.

The later combined private pump/retention3 run reaches approximately 3× in
fresh base game: warmed 2.998744× owner / 3.000498× game, with zero dropped warm
credit and five clean-window GC frames. Subsequent real merge/dying pickupable,
pipe-callback, paused-save/cold-load and pending-packet lifecycle checks pass.
Production integration is split into receipt pumping and then pacing credit.
Scheduling receipt delivery changes Unity-time behavior; byte/order controls
do not establish unchanged world trajectories. The fresh completion
sample still has 381 pending receipts at owner frame 900.

The SAME frozen package passes fresh Spaced Out! 900 with Expansion1 active,
nine worlds/79 species, but reaches only 1.95478× owner / 1.95592× game. The 2 s
credit bound saturates and drops 78.474 s of offered warm time. Flow 36.5–38.7 ms
is the largest broad native stage; heat 11.7–13.3, stage2 8.3–9.8 and separate
ledger/projection 6.2–6.4 ms need finer attribution. All 5,856 texture batches
fall back for an unrecorded reason; no compact samples run. The native species
count is 79 despite the unsampled diagnostic table count 0. Pump delivers 0,
maximum observed pending 12, fresh frame 900 pending 0 only at that instant.
There is one GC in each of five clean 15 s windows, no owner fault or dropped
timing samples. The private native split attributes most flow time to dense
state construction, including 40.54 MB of held-vector copies per frame. An
immutable-vector prototype preserves complete replay bytes and lowers warm
means 11–13%, but repeatedly worsens p95. It remains private while memory,
sanitizers and a layout-preserving variant are investigated. See
[combined/base/DLC scope](performance-acceptance.md#idle-frame-byproducts-and-combined-pacing-2026-10-29).


### Production receipt pump, 2026-10-29

The default-on idle receipt pump (`MA_COLONY_BYPRODUCT_PUMP=0` to opt out)
uses a successful publication's session/frame/observed-sequence fence, a shared
reentry lease and a separate 2 ms idle budget. It stops at callbacks, jobs and
precise/moving sidecars. Pause, continuation, readiness, retirement and explicit
settlement scopes prevent idle delivery; resource callbacks enter the step
barrier so reentrant saves defer. Ordinary publication admission and its
1,500-receipt override remain unchanged. Pump telemetry is opt-in with
`MA_COLONY_BYPRODUCT_PUMP_LOG=1`; frame timing counts its work separately.
Solver arithmetic and checkpoint layouts/bytes do not change for identical
operations. Receipt scheduling changes Unity-time behavior.

Permanent tests link the actual state/save-barrier sources and compile the
actual receipt-delivery/pump bodies against deterministic fake endpoints.
Default/on each pass 1,753 assertions, opt-out passes seven, and the disabled
negative oracle rejects. Actual Linux game Mono passes 1,751 functional
assertions in each enabled mode; its unsupported allocation checks are omitted.
.NET's native ABI loop passes 10,000 iterations with zero warmed allocations.
Actual Linux and Windows game Mono each pass the raw/legacy 600-byte status,
48-byte receipt and inactive-clock ABI checks; Windows Mono runs under Wine,
not Windows OS. The Windows adapter compiles with two existing assembly
warnings and verifies 2,588 contract dependencies with zero differences.
The complete managed/game-contract suite and 439 Python tests pass. Native
sources are unchanged, retaining the previously verified 89-test binaries.
Evidence: `receipt-pump/production-port/`, `windows-game-mono-abi/`,
`windows-pump-managed/` and `pump-production-full-managed.log` under the tier
artifacts. The delivered fc0fcc4 Windows ZIP still excludes the new pump and
credit integration; a replacement package follows final validation.

The separate pacing-credit production port is now integrated. Its wrapper
selects 2 s capacity and `1f/30f` repayment, leaving the original 0.2f argument
cap, busy fence/epsilon and guard priorities unchanged. The pure helper keeps
its original defaults and arithmetic from the sealed combined experiment.
Default-on `MA_COLONY_PACING_CREDIT` has a `0` opt-out; full telemetry requires
`MA_COLONY_PACING_CREDIT_LOG=1`, while faults always report. The older
`MA_COLONY_PACING_LOSS_LOG` runs only with credit disabled because its
loss-only accounting cannot attribute repayment. Session, speed, pause,
readiness and save boundaries reset credit; it is not checkpoint state.

Permanent pure controls pass the original 17,138 plus 102,908 capacity and
101,886 repayment assertions. Actual Linux game Mono passes the original 81
Harmony checks plus 11 configured/production checks in default and explicit-on
modes, opt-out eight and the disabled negative oracle. Flag combinations
exercise quiet defaults, explicit credit telemetry and baseline loss telemetry.
The complete managed/game-contract suite and 439 Python tests pass. Evidence:
`credit-production-port/` and `credit-production-full-managed.log`. Neither
solver arithmetic nor checkpoint format/bytes change for identical operations.
The final Windows adapter verifies 2,588 dependencies with zero differences;
the actual Windows game Mono/Harmony matrix also passes under Wine, including
default/on/off, baseline loss logging and the deliberate disabled negative.
This uses fake game endpoints, not the Windows OS or an active game owner.
Evidence: `windows-credit-harmony/results.json`.

The production-port base-game repeat passes at 2.99675× warmed game progress;
five windows range 2.960–3.027× and clean-window GC counts are 1/2/4. The
latest clean Windows private ZIP is `MixedAtmospheres-private-windows-05d0c73.zip`
in the playtest share, with pump/credit and the bounded collision-cache repair
included. Windows OS and representative sustained 3× remain open.

The DLC texture diagnostic identifies all 5,543 fallbacks as modulo collisions
with only 1,606 visible readings in one 4,093-slot cache. A same-binary clean
OFF/ON pair improves warmed game progress 1.90644→1.94790× with lower heap
growth estimates, 5,445 admitted batches and no fallback in ON. Runtime inputs
vary, so the observed 2.17% is not a precise causal speedup. Separate actual
DLC CPU destination proof matches all 6,144 samples with zero mismatch.
The production repair permits bounded conflicts as misses while preserving
slot locks and full keys; large/unsupported views retain rich fallback.
The full managed/game-contract suite and 439 Python tests pass; Windows
contract verifies 2,588 dependencies with zero differences. Permanent cache
default/opt-out .NET controls pass 569,506 each, and actual Linux game Mono
passes 569,496 each with unsupported allocation assertions omitted.
Evidence: `compact-collision-final/` and the detailed
[performance record](performance-acceptance.md#idle-frame-byproducts-and-combined-pacing-2026-10-29).

Native dense-state construction copies about 40.54 MB of held mass vectors
per captured DLC frame. Two immutable-sharing prototypes retain replay bytes
but worsen tails. The third uses a 32-byte tagged storage / 96-byte cell and
passes 339 storage controls plus native/cold/reference tests and 256 exact
replay outputs. DLC mean/p95 improves 55.258/62.819→45.110/48.859 ms; base
mean improves 28.120→26.668 but p95 rises 29.674→33.682 ms. It remains private
while a clean full backend is prepared for actual game tests, followed by
sanitizers and broad platform/output checks if supported. No native sharing
prototype is included in the delivered ZIP. Preserve the failed performance
variants and fixed acceptance bounds. Evidence: `dense-frozen-{copy,memory,descriptor,variant}/`.

The private combined real-game controls pass ordinary overwrite/cold resave,
independent file readback and menu reload. A separate activity cohort passes
all 16 original checks, preserves a pending frame, rebinds one actor without
release and reads back the exact resave writer owner bytes. The Lead fixture
checks real normal merging and dying-target survival with disclosed setup
interventions; the pipe fixture protects a genuine precise job behind a
byproduct backlog and observes normal publication delivering it exactly once.
See the detailed scope and limitations in [performance acceptance](performance-acceptance.md#idle-frame-byproducts-and-combined-pacing-2026-10-29).

Nearby searches cost about 0.06 ms in measured replays, all reservoir anchors
are open and saved precision inventories are empty. The cold first DLC transfer
spends about 8.1 ms obtaining missing audit totals; the next reuses them in
0.0005 ms. A parallel fixed-order audit prototype and hook-counter sharding
remain private/unadopted, with no default throughput claim. Keep large active
pressure/transport work in scope: active-cell benchmarks cannot benefit from
sleeping their active cells. The clean private Windows package `MixedAtmospheres-private-windows-fc0fcc4.zip`
is delivered in `/mnt/data/share/oni/playtest`, with the tested native DLLs and
2,587/0 Windows managed contract. See [provenance and install](spaced-out-private-playtest.md). Actual Windows OS acceptance,
3×, smooth input, full active save continuation, exact storage and the broader
release matrix remain open. Historical worktree patches below are superseded
by main.

Private compact evidence is in `local/profile8/replay-summary.json`,
`artifacts/smoothness-baseline-driver/control-summary.json` and
`artifacts/codex-resume-2026-10-28/` (comparison, verification and scenario
triage, plus `smoothness-linux-run/smoothness-summary.json`). The original
handoff follows.

## User decisions of 2026-10-28

1. **Real time blocks release.** The private `5d11aea` (profile 5) Windows
   playtest ran at about 0.85× with stuttering mouse, camera and UI, and the
   user found it "very unenjoyable". Before release the game must sustain at
   least 1× with smooth input on the user's PC (Ryzen 9 9950X, 16 cores, RTX
   5070 Ti, Windows). The fixed performance targets of the completion plan
   stay unchanged.
2. **Features may be removed for speed**, but only by the user's choice.
   Measure each option, state what the player sees, and ask.
3. **Chosen for speed:**
   - Momentum-carrying interface sharpening and layering (a defect fix).
   - A 1 mg floor on the net-volume interval bound.
   - Pausing untouched regions until something touches them, with no
     catch-up.
   - Remote regions without actors stepping every 1 s instead of 0.2 s.
4. **Not chosen:** turning off liquid-surface sharpening, softer liquids
   (stiffness 0.01) or a larger trace threshold. Leave them as they are.
5. **Boil-only phase change**, on by default for new colonies (done). A liquid
   vaporises only when it boils at the cell's total gas pressure, and vapour
   below that boiling point condenses, so the air holds no humidity. The user
   then confirmed total pressure for every gas, even though minor gases in
   strongly compressed mixtures condense (Terra's ~19 bar hot hydrogen pocket
   liquefies about 214 kg of chlorine at ~110 °C).

## What is on main

Today's series, oldest first (`16da60c` was main before it):

| Commit | What |
| --- | --- |
| `058683a`, `3f18696` | Decisions and tracker updates |
| `3549185`, `1058d0d` | Checkpoint 25 and the boil-only rule. Settings: Gameplay → Evaporation below boiling, off by default; options struct version 2. Control: `ma_mac_phase_change_tests --boil-only` |
| `d223793`, `2b46613` | Stutter fix 1: lazy owner readings, a 2 ms receipt budget per publication, `MA_FRAME_LOG=1` frame log. The worker default stays at physical cores |
| `a19d6ba`, `23882ca` | Exact speedups: only changed cells' EOS is re-read; the pressure solve's component structure is cached. No change to results or bytes |
| `94c42f6`, `e1615ba`, `ab6eea3`, `eb40d9b` | Profile 7 / checkpoint 26: pressure intervals sized from the measured swept volume. Root-cause notes are in [MAC_GAME_KERNEL.md](../native/MAC_GAME_KERNEL.md#retry-storms-and-speed-profile-7) |
| `f7dd8f3` | **Reserves checkpoint 27 for profile 8** (momentum plus the 1 mg floor). Nothing is implemented on main yet |
| `7d3d09b` | New colonies use profile 7 / core 26 with boil-only (selector 1). The legacy liquid-boundary control is pinned to profile 6, and a new profile-7 airflow-tile control was added |

All 89 CTests passed at each pushed solver commit, and the `ma_mac_*`
outputs were compared with the parent from clean builds.

## Unfinished work, preserved and not committed

Copies of both diffs are in the ignored folder
`/home/henry/projects/oni_gas_mod/artifacts/handoff-2026-10-28/`. The
worktrees themselves were left as they were.

### A. Profile 8: momentum-carrying sharpening and layering, plus the 1 mg floor

- **Location:** `worktrees/perf` at `7d3d09b`, 10 files changed (+315/−58).
  Copy: `perf-profile8-momentum-wip.patch`.
- **Already present:** checkpoint 27 read/write and
  `MacGameProfile::game_kernel_carried_momentum = 8`; the
  `bound_mass_floor` threading in `flow_local.hpp`/`tick.hpp`; a
  `carry_momentum` path in `surface.hpp` and `layering.hpp` (parcels keep the
  velocities of the faces they leave; momentum reaching a closed face becomes
  a wall impulse audited by `momentum_receipt`); managed
  `ColonyCheckpointMetadata` accepting 27.
- **Status:** stopped mid-implementation of the sharpening stage. It has not
  been built or tested.
- **Done when:**
  - momentum closes exactly in the audits;
  - the magma face no longer forms in the fresh Spaced Out! owner replay
    (frames 4–43; it reached about −2,081 m/s on profile 7);
  - the twelve-dig overfill regression replay and the expansion /
    temporal-reference controls still pass;
  - full ctest and the `ma_mac_*` comparison pass;
  - profile 8 is then the new-colony default, with boil-only.
- **Expected effect:** the no-sharpening replay suggests about 23% less frame
  time in fresh Spaced Out!. The 1 mg floor took fresh replay frame 60 from
  0.353/0.821 s to 0.283/0.434 s (mean/p95).

### B. Smoothness, round 2

- **Location:** `worktrees/distribution` at `eb40d9b`, which is behind main,
  so rebase first. 11 files changed plus the untracked
  `tools/ReadingEquivalenceTests/`. Copies: `distribution-smoothness-wip.patch`
  and `distribution-untracked.tgz`.
- **Contents:**
  - fewer allocations in Core readings (`MixtureThermodynamicReading`,
    `FluidMaterialCatalog`), `ColonyLazyReadings` and `ColonyPresentation`;
  - a preference, Advanced → Performance → "Log frame timing", for frame logs
    on the user's Windows PC (documented in `docs/settings.md`);
  - `MA_COLONY_NATIVE_WAIT_LOG=1` phase timing in `native/src/oni_backend.cpp`
    and `oni_colony.cpp`;
  - the equivalence driver, still being written when the agent stopped.
- **Status:** unverified.
- **Goal:** vanilla-like garbage-collection frequency, about one collection
  per 15 s. After `d223793` the base-game harness still showed a ~200 ms
  pause about every 3 s. Readings must stay byte-identical, so prove it with
  the equivalence driver.
- **Then:** scope the main thread's wait in ONI's `PrepareGameData` (p99
  40–50 ms, much worse when the solver is behind) and move the end-of-frame
  projection off the critical path if that can be done within `oni_*.cpp`.

### C. Region activity tiers

- **Status:** not started.
- **Intent:** the user's choices 3c and 3d above.
- **Proposed design:**
  - Classify each solid-separated region every tick, deterministically:
    - **Active (0.2 s steps):** any owner input this tick or within a short
      hold time, or containing a duplicant, critter, building or port.
      Inputs are digs, builds, tile changes, transfers, geyser output,
      building or body heat, debris and merging with an active region.
    - **Remote (1 s steps):** touched since load but idle past the hold time.
    - **Paused:** pristine world-generation regions, and remote regions idle
      past a cooling-off time (about 60 s of game time, tunable).
  - Passive solid-tile conduction alone must not count as activity: the
    Terra input journal showed "heat inputs" in 82–94% of regions.
  - Save the activity state, so a new profile and checkpoint are needed;
    claim the version after a fetch.
  - Make it a colony setting, on by default, with an "Always simulate"
    choice.
  - The tiers must not depend on the camera, worker count or frame rate.
    Conservation stays exact, and a region that wakes resumes from its stored
    state.
- **Scope:** in fresh Spaced Out!, untouched regions hold 87–98% of
  single-thread solver time; freezing them cut those regions' cost 2.6–4.4×,
  and 1 s steps cut their CPU about 44%.
- **Implementation constraints found during resumption:** region IDs change
  on topology edits, so save activity per cell or remap splits/merges by
  overlap. `ColonyActors` alone is not a complete actor census; idle buildings,
  ports, bodies, plants and debris need coverage, including inactive or
  burrowed entities. Native heat records combine passive solid conduction
  with actor heat. The owner also evolves germs before flow and runs identity
  conversions, trace removal and the space sink afterwards: define consistent
  tier semantics for these stages, not just `RegionalTick`. Persist remote
  fractional-step state and deterministic wake rules.

### D. Failing in-game scenarios

Partial results on the package built from `eb40d9b` (Linux harness), from
`worktrees/accuracy/artifacts/bis/`:

| Scenario | `eb40d9b` | Message |
| --- | --- | --- |
| freeze-melt | fail | ice melted into an owned cell; owned water in the arena 356 kg (bound > 380) |
| airflow | fail | air passed the airflow tiles (4.25 -> 4.052 kg) |
| starting-air | pass | — |

- **Packages built:** `16da60c`, `058683a`, `1058d0d`, `d223793` and
  `eb40d9b` (`bis/pkg-*`).
- **Baseline runs at `16da60c`:** killed by the stop, so they have no result.
- **Next steps:** finish the bisect, check whether either failure predates
  today, and check whether boil-only or profile 7 changed what the scenarios
  assume. Never change scenario inputs or bounds.
- **History:** freeze-melt failed earlier too, at 337.9 kg on `dde2a09`; see
  "Freeze/melt handoff" in the tracker.

## Speed: where it stands

The dev machine is a Ryzen 9 5900X; timings are under
`tools/run_locked.sh high` (12 workers) and often contended. Calibration: the
profile 5 fresh Spaced Out! owner frame here, about 0.48 s, matches the
user's 0.85×, so **the user's PC is about 2× this machine**. Real time on
their PC therefore needs an owner frame of about 0.4 s here; aim for
0.2–0.25 s to leave headroom for UI and larger colonies.

**Profile 7, owner frames:**
- fresh Spaced Out!, frames 4–43: about 1.16 s;
- the user's colony with the twelve-dig inputs: 1.49 s (2.94 s on profile 6).

**Physical advance only, same states:**

| State | Profile 6 mean/p95 (s) | Profile 7 mean/p95 (s) |
| --- | --- | --- |
| `writer32` | 1.905 / 2.978 | 1.171 / 1.724 |
| Fresh replay frame 60 | 0.591 / 1.498 | 0.337 / 0.750 |

`writer32` is the fresh Spaced Out! frame-32 state with digs, not the user's
colony.

**Findings:**
- **Workers:** wall time is flat beyond 4–6 workers, because the largest
  active region is the critical path. The 9950X's gain is per-core speed, not
  core count.
- **Where the time goes:** pressure 40–49%, interface sharpening 14–16%,
  phase change 7–14% (it visits most gas cells, because O₂, CO₂, H₂ and Cl₂
  are vapours of liquid pairs), transport 9–28%, audits 6–12%, heat about 2%,
  layering under 1%.
- **Owner overhead:** the owner frame is about 1.35× the physical advance.
  The space-sink step alone costs about 65 ms per frame in fresh Spaced Out!
  (12,913 space cells).

**Exact speedups still open (no decision needed):**
- the space sink;
- owner overhead;
- per-interval transport cost (1–2 ms per interval on 5,000–9,000 cells);
- a multigrid or aggregation preconditioner for large, calm, connected bases
  (270–440 IC(0) iterations; an offline prototype converged in 26–33).

**Options measured and not adopted:**
- flow parts 1, gas heat mixing 0 and phase relaxation 10 s were all slower;
- phase change off triggers retry storms;
- sleeping regions at rest gain little.

**Data:**
- `worktrees/accuracy/artifacts/perf/`: probes, per-region CSVs and owner
  timings;
- `worktrees/perf/artifacts/mac-compare`;
- `artifacts/temperature-*`.

**Private states** (never commit them):
- `local/temperature-release-regression/` (`writer32.owner`,
  `flux-core24-private.sav`, the user's colony);
- `local/windows-temperature-video/` (`flux.owner`, the probe helpers);
- `/mnt/data/share/oni/playtest/flux/` (the user's upload and its
  `config.json`).

## Steam release gaps

Release logistics are not yet done: no licence (the user's choice), no
Workshop authorization, and the README and Steam draft still describe a
private playtest. Engineering gaps, in rough order:

1. **Speed and smoothness:** A–C above plus the exact speedups. Then a
   private Windows package with the frame log for the user's PC
   (`tools/package_windows.py`, delivered to `/mnt/data/share/oni/playtest`;
   ask for Player.log). The user's machine is the acceptance hardware.
2. **Save compatibility after release:** ordinary loading refuses earlier
   R&D profiles today. Once published, every released profile must keep
   loading after mod updates; this needs a policy and controls.
3. **Exact component storage** (bottles, canisters, recipes, nonempty
   demolition) through the normal player path, and **active continuation**
   (the world diverges on the first resumed frame).
4. **Accuracy proofs:** F3/F4 floor; strong-flow collision refinement (0.856
   against 0.85) and separation velocity.
5. **Progression:** representative base-game and Spaced Out! starts reaching
   sustainable life support and the endgame without developer help.
6. **Mixture presentation:** container coverage and real-game
   hover/overlay checks on both operating systems.
7. **Release tests:** the compatibility matrix (base game and Spaced Out! on
   Windows and Linux, plus refusal of the other packs); disturbance
   recovery; memory; 100-cycle and 1,000-cycle long runs (the latter is about
   167 h of game time at 1×, so the user may want to revisit it).
8. **Player-facing gaps:** a minimum-hardware statement, popular-mod
   compatibility, a re-targeting routine for Klei game updates (the mod is
   pinned to build 744825 and fails closed), README and Steam text, and the
   user's own Windows OS check (only Proton is tested here).

## Housekeeping

- **Worktrees:**
  - `perf` holds A;
  - `distribution` holds B;
  - `accuracy` has clean code, with bisect and perf data in its
    `artifacts/`;
  - `progression` is clean at `7d3d09b` (C was not started);
  - `acceptance` holds older uncommitted work from 2026-10-27, not from this
    session. Inspect it before use.
- **Build folders left by the stop:** `worktrees/perf/build-speed` and
  `worktrees/distribution/build-smooth*`. Reuse or delete them.
- **Processes:** every harness run started by this session was stopped.
  `OxygenNotIncluded.exe` PID 832594 (Proton, an archived
  `claude-team/spacedout-cargo` run) has been running for over 31 hours from
  an earlier session. It is probably stale; check before killing it.
- **Shell trap:** `pkill -f PATTERN` matches the shell running it. Kill by
  PID.
- **Checkpoint numbering:** 27 is reserved for profile 8. Region tiers need
  the next free number, claimed after a fetch
  ([MAC_CHECKPOINT.md](../native/MAC_CHECKPOINT.md#version-assignment)).
