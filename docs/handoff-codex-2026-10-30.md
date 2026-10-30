# Handoff to Codex, 2026-10-30

The latest task is the release-candidate test process against the owned public
Steam build, with Linux work continuing while **actual Windows OS tests remain
pending by the user's explicit choice**. Do not ask for those tests again or
publish/upload a Workshop release. Read [AGENTS.md](../AGENTS.md),
[the candidate report](release-candidate-2026-10-30.md) and
[the accepted amendments](completion-plan.md#amendments).

## Current result

The candidate is rejected, not release-ready. ONI public build 24423041 / game
744825 retains all 2,588 managed dependencies on both targets; no update-hook
repair was required. Current numerical, gameplay, continuation and performance
failures remain. Keep the fixed inputs/grids/bounds.

Current tested code is `602b324a8d3528523a0c32101a9ff120bf0cb423`, pushed on
main. The first-EOS allocation/verification fixes are `0af5dfe`; the partial
reservoir registration guard is `3601ff5`, and its insulated-carrier correction
is `602b324`. Native solver arithmetic and versions are unchanged. The guard
affects withdrawal ordering, so physical checkpoint contents can differ. It does
not close later queued native heat receipts.

All affected managed/Python/contract suites pass: 487 Python tests (four
actual-Windows-only skips), 237 snapshot/persistence scenarios, two million mass
controls and 2,588 dependencies with zero differences. Both game-target managed
DLLs match byte for byte. Linux, Windows/Wine and Ubuntu 22.04 each pass 90
native tests for `0af5dfe`; the C# repairs reuse those results with identical
native/CMake inputs and library hashes. All 88 old/new MAC outputs and exit
codes match for the EOS move, masking only existing timing fields.

The frozen `69e55f8` gameplay matrix finishes 15 batches (62/65 scenarios pass)
and all nine 3,000-frame asteroid stability runs. Twenty-two follow-ups finish;
their runner passes do not qualify every accuracy floor. The original audited
Proton tier has 4 passes, 12 failures and 4 dependency blocks. Separate repaired
ordinary-UI/settings cohorts pass all four source/load checks. Current-source
storage reruns are detailed in the candidate report.

## Resume product work

1. Address the severe normal-reserve base-game slowdown, then qualify
   sustainable life support and progression. Separate base/DLC controls complete
   3,000 frames with three healthy duplicants and no pauses or owner faults.
   Actual game progress is 0.303476× base / 2.154535× DLC; a late base window is
   0.063×. The original fixed asteroid stability fixtures suppress the normal
   reserve, so their zero survivors do not establish an ordinary-start defect.
   These separate short controls do not qualify 100/1,000-cycle survival.
2. Continue exact storage/bottle/recipe lifecycle work. The reader no-op does
   not recur in the explicit base-mask control; its cause is still unproved. A
   separate DLC run reproduces late initial heat registration after complete
   withdrawal. `3601ff5` waits for native thermal handles before withdrawal and
   for actual outlet readiness in the original bounded reader fixture. Its
   independent −1,000 J transfer regression and deliberate broken-readiness
   mutation are verified, but the live DLC reader still faults on a later
   positive-capacity heat receipt after drain. Registration readiness is
   insufficient. Implement a transactional native heat/transfer boundary that
   settles earlier work and acknowledges the old capacity endpoint before
   inventory leaves; preserve the massless-heat refusal. The initial guard also
   blocked insulated biological carriers that have no active thermal callback;
   the subsequent correction exempts disabled contacts and tests exact insulated
   withdrawal. Keep the separate closed-storage heat residual as an unwaived
   failure.
3. Preserve exact future input scheduling through active cold/menu load.
   Current heat inputs already differ at resumed frame 35. Native-loop RNG,
   iteration/displacement/tick restoration alone does not close it. Do not
   attribute different-input trajectories to solver nondeterminism.
4. Repair fixed accuracy/physics failures: freeze/melt, airflow, whole-world
   breathing; M5 retained-constituent storage and strong-flow density/velocity.
   The exact M5 replay shows incoming parcels contain zero of the failing
   subnormal constituent, so projection deferral alone is insufficient.
5. Continue measured speed work. The exact first-EOS allocation move is safe,
   but does not establish 3×. Late Oceania's 341 accepted substeps versus 23 rejected
   trials make cost per accepted step and conservative interval admission a
   priority. The separate base hot autosave (actual owner frame 2,752) takes 232
accepted steps/35 rejected trials initially, then relaxes to two accepted steps.
Neither replay includes future live owner inputs. Pressure/transport are hot in
those states and the synthetic
   all-active state; conversion/merging/publication dominate fresh DLC bridge
   work. Grouped dispatch is only a microbenchmark result until solver/game
   equivalence and speed are measured.
6. Complete F1–F5 qualification, progression, unsupported-pack live refusal,
   exact storage/bottle/recipe lifecycles and the long runs after critical
   correctness/continuation/speed gates close. Full F4 and independent F3/F4
   dynamic truth are still open. Do not substitute shorter or coarser runs.

## Private evidence and tools

Evidence lives in ignored `artifacts/release-candidate-2026-10-29/`:
`current-progress.json`, `gate-ledger.json`, `current-summary.json`, frozen
matrix/follow-up plans, the original and audited Proton results, corrected UI
cohorts, `floor-comparisons-69e55f8.json`, continuation audits,
`slow-oceania-69e55f8/`, and `runtime-profile/first-eos-move/`.

The actual signed-in Steam Linux install is under
`~/.local/share/oni-headless/steam-home/.local/share/Steam/steamapps/common/OxygenNotIncluded`;
use it instead of the incomplete shared Linux payload. The owned Windows
payload is `local/steam-windows-public-24423041`. GPU display `:99` is available.
Never relocate the Steam home or touch credentials. Each game must use
`tools/game_slot.sh`; builds/tests use `tools/run_locked.sh`. LOW/HIGH are SMT
siblings, so shared runs establish functionality only; use WHOLE without other
CPU jobs for timing. Keep local saves/locks.

The full 88-command native old/new comparison uses fresh builds and masks only
existing `ref_ms`/`prod_ms` timing fields. Identical known nonzero diagnostic
exits are preserved, not counted as passed physics gates. Actual-DLC and synthetic
Linux replay checkpoint comparisons span 1/2/4/8/12 workers in the private replay suite.

The final clean private probes are `linux-probe-602b324` and
`windows-probe-602b324`; see `rc-insulation-probes/summary.json` for their file
manifests and actual-Mono preflights. The final affected storage batches are
`linux-storage-final-602b324/summary.json`; their exact verdicts are in the
report. The original and corrected-prerequisite `3601ff5` runs remain separate
evidence. No background acceptance controllers remain after this handoff.

The current private Windows kit is
`/mnt/data/share/oni/playtest/MixedAtmospheres-RC-602b324`
(`README-Windows.txt`). Actual Windows OS tests remain pending; do not ask again
or mistake Wine for OS acceptance. Completed one-off native build folders are
removed after retaining exact tested libraries and test/config logs in
`native-build-retained/cleanup.json`; current probes keep their packaged
libraries. Regenerate build folders when needed.

All changes are on main and pushed. A subsequent documentation-only commit does
not change the tested code revision above. Do not rerun the old frozen/probe
controllers into existing output directories. The older worktrees are
historical, not the current work queue.
