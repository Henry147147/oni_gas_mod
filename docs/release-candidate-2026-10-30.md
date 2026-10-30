# Release-candidate review, 2026-10-30

The tested candidate is **not release-ready**. The current owned public Steam
build works with the mod's hooks and contracts, but physics, gameplay,
continuation and performance requirements still fail. Windows OS acceptance is
pending by the user's explicit choice. Nothing was published or uploaded to
Workshop.

## Version and scope

Steam public build **24423041**, ONI **744825**, was verified through the normal
signed-in Steam installation on 2026-10-29. The owned Windows base-game and
Spaced Out! depots were downloaded normally. The original 2,588 managed
contract dependencies resolve with zero differences independently against both
installed targets. This preserves the pre-update contract check; generating a
new contract alone would not establish compatibility.

The main game matrix is frozen at `69e55f8`. Its Linux libraries target Ubuntu
22.04, and independently built Linux/Windows symbol-free managed DLLs match
byte for byte. Subsequent code changes and their affected reruns are identified
separately below; earlier matrix results do not automatically qualify a newer
package. Licensed saves, memory images, installed-game inspection and raw game
logs remain in ignored private artifacts.

The allocation and verification repairs are committed and pushed in `0af5dfe`.
Their clean-package runtime checks are recorded separately from the frozen
`69e55f8` matrix. The partial storage registration guard is committed and pushed in
`3601ff5`; `602b324` corrects its disabled-contact regression. Their affected
storage reruns are separate from the earlier broad and timing cohorts.

## Results

| Area | Evidence and verdict |
| --- | --- |
| Native builds | Original Linux, Windows x64/Wine and Ubuntu 22.04 builds each pass 90 CTests. Both actual game Mono runtimes pass 2,588 contract checks with zero differences. Windows Mono also passes the 10,000-call inactive native ABI check under Wine. These are Linux-hosted preflights, not Windows OS acceptance. |
| Base-game scenarios | All 15 fixed batches complete: 62/65 scenarios and 12/15 batches pass. Freeze/melt, airflow and whole-world breathing retain their original failures. |
| Starting asteroids | All nine complete their fixed 3,000-frame generation/stability runs without owner faults and with conservation errors within the fixed bounds. These are stability fixtures with the ordinary starting reserve suppressed by validation; their zero-survivor observations do not establish ordinary-start survival failure. A normal-reserve survival control is separate. |
| Supported DLC | Ports/retirement, flight, gas/liquid cargo and warp source/cold/menu cases pass. Complete paused owner/callback images survive the tested loads. The corrected airborne-recovery prerequisite produces all four expected fluid constituents in debris and closes the ownership/pipe accounts. |
| Other controls | Correctly opted-in germ oracle, negative save guard and body-heat rounding controls pass. Spaced Out! input flush passes. Storage biology, mixture ownership and transfer-heat controls pass. |
| Storage | The explicit base-game reader rerun passes all five checks, including actual 4 kg withdrawal and closed accounts. One DLC run passes those checks and then faults on a late initial thermal callback. The partial repair and its affected reruns are recorded below. The separate closed-storage heat bound still fails. |
| Physics | Full M2/M3 pass. Full M5 grid40 fails retained-constituent storage. The extended default strong-flow gate fails density refinement; the monotone selector fails finest-grid resting velocity. Coarse tests and partial conservation reviews do not replace these failures. |
| Accuracy floors | F1/F2/F3/F5 owner and vanilla measurements complete. F1 mass splits and F3 depth point errors improve on vanilla; F2 has zero closed leakage in both. F5 has correct layer order and improved water depth, but oil point error is larger than vanilla (1.92e−8 vs 1.67e−9 m). Numeric point comparisons alone are not full floor qualification. Full F4 and independent F3/F4 timing truth remain open. |
| Active continuation | Base and DLC cold/menu smoke tests pass, but the first resumed frame already receives different thermal inputs. This does not prove deterministic continuation under identical future inputs. Capturing/restoring the existing native loop fields does not close that gap. |
| WindowsPlayer on Linux | The original 20-step audited release tier has 4 passes, 12 failures and 4 dependency blocks. Separate repaired ordinary-UI and settings source/load cohorts pass all four checks; original failures remain intact. Original retry criteria remain unchanged. Wine/Proton cannot establish actual Windows OS behavior. |
| Actual speed | Clean `0af5dfe` 900-frame WHOLE Linux probes achieve 2.948404× base / 2.134842× DLC game progress. Separate 3,000-frame normal-reserve controls achieve 0.303476× / 2.154535×. All four finish with three duplicants alive and no pauses, owner faults or dropped timing samples. Representative 3× remains unmet. |
| Fixed performance | Every 100k/250k cost and disturbance-recovery gate still fails, including with the allocation prototype. No disturbance recovers within 30 seconds; all remain unrecovered at 45 seconds. |

## Failures requiring product work

The fixed asteroid fixtures intentionally omit the normal oxylite reserve.
Their starting-air logs show 0 kg placed, while the ordinary Spaced Out! UI
start places its full 1,740.07 kg shortfall. The first four stability cases end
with no live duplicants. They do not qualify sustainable survival, and cannot
by themselves establish a normal-start defect. Keep their fixed inputs and
use a separate normal-reserve control.

Freeze/melt measures 368.3 kg owned arena water against the original >380 kg
criterion; solid debris has not been excluded as a destination, so this is not
yet demonstrated mass loss. Airflow's right-side gas falls from 4.241 to
4.052 kg instead of increasing, while its liquid barrier passes. Whole-world
breathing CO₂ changes by −0.8921 kg; the separately isolated local breathing
mechanism works. These measurements do not justify changing the gates.

The storage heat residual is −1.5818704760362332 J against a
4.9810459181527846e−6 J bound. The earlier explicitly waived
−0.70290390179257045 J measurement is a different result; its waiver has not
been extended. The original reader no-op did not recur in the explicit base-mask
control. Its cause remains unproved; a passing repeat alone does not isolate
outlet readiness. Passive dispenser details now distinguish physical inventory
from outlet eligibility. The separately observed late-registration fault needs
the transfer-boundary work below.

The first committed-package schedule and storage commands accidentally omitted
an explicit DLC mask and inherited Spaced Out!. They are additional DLC results,
not replacements for the base-game rechecks. The DLC schedule passes all six
checks. Storage withdraws all 4 kg, preserves the exact pipe accounts and
passes all five original inspection checks, but subsequently faults in
`SimTemperatureTransfer.OnSimRegistered` with “Cannot heat an empty pipe.”
Its initial thermal-registration callback reaches a store already drained.
An explicit base-mask rerun passes all five reader checks without a behavior
change. The traced DLC repeat reproduces the late-registration fault with zero
held mass and a positive initial thermal capacity.

Commit `3601ff5` defers withdrawal until native projections with positive
initial capacity have registered. This prevents withdrawal before that
registration callback; it does not prove that every native heat segment has
already been delivered. No heat is discarded or assigned to a synthetic sink;
the empty-inventory heat refusal remains in place. The diagnostic waits for
actual connection/operational/thermal readiness inside its original 80-frame
cap. Its masses, trace/germ quantities and five acceptance checks remain
unchanged. A causal regression independently accounts for −1,000 J of initial
work and checks exact 2 kg, germ and 599,000 J output transfer. Deliberately
ignoring readiness fails that regression. Native solver arithmetic and
checkpoint schemas/versions are unchanged; physical checkpoint contents can
differ because the coupling order changes.

The live DLC rerun demonstrates that this guard is **insufficient**: both
projections register while 4 kg remains, then withdrawal drains the store, and
positive-capacity heat receipts from the following native frame arrive after it
is empty. The reader passes its five inspection checks but the overall run fails
in `CreditNativeStorageHeat → PrepareHeat`. The base reader passes without an
owner fault. A complete repair needs a transfer boundary that settles earlier
work and proves the old native capacity has stopped before inventory leaves;
registration alone or copying currently available receipts does not establish
that. No heat was dropped, redirected or accepted into a massless store. The
first broader batches omitted the required schedule-writer and registered-body
trace settings for two diagnostics. Those setup failures are preserved
separately. Correctly configured `3601ff5` batches pass mixture transfer,
pending transfer heat, retirement and active-outlet save controls; they
reproduce the insulated-carrier withdrawal regression and fail the closed-heat
bound.

The current-code follow-up also checks insulated storage. The first registration
guard incorrectly waited for disabled thermal components that can never
register, blocking two biology withdrawals. Correctly configured base and DLC
batches reproduce that regression. The narrowed policy exempts disabled
contacts, with a causal full-inventory/energy/germ regression and a deliberate
mutation that restores the failure. Final clean `602b324` base-game and DLC
batches use the required native schedule writer and registered-body trace,
preserving every original fixture and bound.

Mixture storage, pending transfer heat, retirement, active-outlet saving and
biology pass in both modes. Closed heat remains failed:

| Final mode | Scenarios passed | Heat residual, J | Fixed operand bound, J |
| --- | ---: | ---: | ---: |
| Base game | 5/6 | −1.6381458381840441 | 4.9810502511920339e−6 |
| Spaced Out! | 5/6 | −1.6381458381840441 | 4.9788661104928724e−6 |

The full affected Python/managed/contract verification passes again on this
correction. Independently built Linux and Windows managed DLLs match byte for
byte, both actual game Mono contract checks resolve 2,588 dependencies with zero
differences, and the Windows native ABI preflight passes 10,000 inactive calls
under Wine. Native results are reused with identical source and tested-library
hashes. The broad 65-scenario matrix and actual-clock controls retain their
separately recorded earlier revisions; this is affected qualification, not a new
all-content matrix.

M5's exact failed-step replay preserves the source checkpoint and reproduces
the refusal. Its retained constituent is 73 minimum-subnormal quanta; all
incoming parcels contribute zero of that species. Delaying projection until
transport therefore cannot resolve this captured failure. Conservative
representation/transport work is needed; wider bounds, trace cutoffs or changed
gate inputs have not been adopted.

Continuation audits retain every activity and heat input in order. Source and
loaded branches already differ in thermal values at resumed frame 35, and the
base source has 2,992 heat records versus 2,991 after load. The native-loop
control restores its captured RNG/iteration/displacement/tick fields correctly
but still receives different thermal values. The next work is reproducible
input scheduling and complete solid/body/owner continuation, before asserting
solver nondeterminism or qualifying resumable long runs.

## Performance changes and remaining hot paths

The first-EOS allocation repair moves the first interval's EOS array into the
result instead of allocating and copying a full grid. Subsequent intervals keep
the original due-row merge. Solver arithmetic, checkpoint bytes and versions
are unchanged. Clean old/new builds preserve all 88 `ma_mac_*` command outputs
and exit codes, masking only established `ref_ms`/`prod_ms` timing fields. Known
numerical nonzero exits are preserved, not counted as test passes. Complete
checkpoint bytes also match for actual DLC and synthetic 100k replays at
1/2/4/8/12 workers, plus the existing multi-interval/dormant/wake controls.

Four interleaved WHOLE runs per implementation reduce captured fresh-DLC flow
mean from 32.5875 to 24.002 ms (26.35%). All eight final checkpoints match
bytes. This excludes future owner inputs, publication and UI. The committed
900-frame WHOLE runs achieve 2.948404× base / 2.134842× DLC, with three
duplicants alive and no pauses or dropped timing samples. These fresh runs do
not show a material whole-game gain and remain below the release target.
Separate normal-reserve controls both finish 3,000 frames with three living
duplicants and no pauses, owner faults or dropped timing samples. Base game
advances 599.608425 game seconds in 1,975.801190 wall seconds (0.303476×);
Spaced Out! advances 600.008457 in 278.486268 seconds (2.154535×). Base frames
2,551–2,700 fall to 0.063×, with 1,619 ms mean flow, 1,663 ms owner work and
3,179 ms interval time. These controls establish starting survival over the
tested interval, not sustainable progression or the qualifying long runs.

The unchanged fixed performance suite still fails with the prototype:
100k mean 868.406 ms per 0.1 simulated second against 25 ms, and 250k p95
4455.14 ms against 62.5 ms. Unpaired old/new suite measurements do not establish
a causal speedup.

The evidence identifies distinct next targets:

1. **Small accepted intervals in real colonies.** Oceania's late unpaused
   window reaches only 0.187×, with 976 ms mean native flow and a roughly 2.5 s
   longest frame. Ten flow-only steps from its private natural autosave show
   341 accepted substeps on the busiest regional paths in the first advance,
   versus 23 rejected attempts; the accepted count falls to two by advance
   eight. Optimizing work per accepted step and investigating interval
   admission matters more than merely reducing rejected trials. Preserve the
   adopted phase-volume/mass rules and numerical bounds.
2. **Pressure and transport in active/hot states.** Pressure dominates the
   fully active synthetic workload. In the late Oceania replay the pressure
   and transport stage totals are about 93.4 and 71.8 ms per nominal advance;
   regional totals may overlap and are not exclusive wall fractions. Grouped
   worker dispatch substantially reduces overhead in a dispatch-only
   microbenchmark, but no solver gain or numerical equivalence has yet been
   established for that change.
3. **Bridge conversion and publication in fresh DLC states.** Pressure is
   only about 0.9 ms of 27–37 ms flow in the fresh nine-world snapshot.
   Sparse/dense conversion, EOS merging and old-state retirement dominate its
   bridge work. Dense conversion's earlier parallel experiment regressed by
   3%, so another parallel implementation needs measurement. Native/managed
   handoff, input/heat/transfer processing and presentation remain in the
   whole-game critical path.

A separate natural base-game autosave at owner frame 2,752 provides another hot
checkpoint. Its first flow-only advance needs 232 accepted steps and 35 rejected
attempts, then 45/17, then two accepted steps. The baseline instrumented and
allocation candidate retain identical full final physical checkpoints across ten
advances. This omits future owner inputs and publication; its shared-SMT timing
is not a speedup comparison. The base slowdown starts before its natural
autosave.

The late Oceania autosave and slowdown coincide; that does not establish that
saving causes the slowdown. Its replay omits future owner inputs, census,
deliveries, heat and publication. All inspected replay variants retain complete
final checkpoint bytes, but their unpaired shared-SMT timings are not a
speedup comparison. Likewise, increasing the existing 2 s pacing debt cap
would not demonstrate more throughput.

Two original WindowsPlayer batches exit with code 5 before completing their
scenarios, without an owner fault or established cause. The exact b4 repeat
finishes normally and passes all five scenarios; this does not explain or fix
the original b4/b6 exits. Successful owner batches still fail the unchanged
zero-rejected-attempts criterion. Do not relax it silently.

## Test-driver repairs

The collector now keeps informational runner status separate from its verdict.
The main-only acceptance path inspects the clean pushed main checkout and
preserves local saves/locks. The continuation parser validates actual ordered
activity records and checks output observations independently. The pipe
schedule diagnostic copies the captured legacy/current format with port state;
the production schema and writer are unchanged.

The ordinary-UI driver previously treated an owner-start log from duplicant
selection as a running colony, clicked Begin once and silently ignored a
manual-save timeout. It now waits for a new initial save while handling the
intro/settings window, and refuses a missing manual save. The settings driver
now selects and captures all three originally required Advanced views. Original
failed logs and screenshots remain intact; fresh repaired-driver cohorts use
the same game/package and unchanged release predicates. Committed `0af5dfe`
passes the complete build: 487 Python tests (four actual-Windows-only skips),
all 90 Linux native tests, all 90 Windows/Wine native tests, the full managed
suites, 237 snapshot/persistence scenarios and two million pipe-mass controls.
Fresh Linux contract verification resolves 2,588 dependencies with zero
differences and rejects all 14 synthetic contract changes. The same complete
affected managed/Python/contract suites pass after `3601ff5`; native results are
reused with identical native/CMake source and both library hashes, rather than
counted as new native executions.

## Remaining release qualification

The candidate remains rejected until the critical physics, gameplay,
continuation and performance failures close. Sustainable life support and
endgame progression, exact storage/bottle/recipe lifecycles, full accuracy
floors, unsupported-pack live fallback, 100-cycle regressions and 1,000-cycle
all-content runs remain unqualified. Their full counts and both OS requirements
have not been replaced by shorter runs. Long-run/resumable qualification is
blocked by the current correctness and continuation failures.

Actual Windows checks, representative 3× and smooth input/camera/menus remain
pending. The user has a private Windows test kit and explicitly chose to keep
those checks pending; another permission request is unnecessary. The licence
and any public release/Workshop upload remain separate user decisions.

Private evidence: `artifacts/release-candidate-2026-10-29/`, especially
`current-progress.json`, `gate-ledger.json`, the frozen matrix plans, continuation
audits and `runtime-profile/first-eos-move/`. These captures are not tracked.
