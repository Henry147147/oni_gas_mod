# Feature completion work

**Current goal, 2026-10-28:** a Steam-ready first release covering the base
game and Spaced Out! ([amendment](completion-plan.md#amendments)). The user's
0.85× Windows playtest with stuttering input failed acceptance; the latest
release goal is sustained actual 3× with smooth input. Profile8/core27 carries
liquid-correction momentum and the adopted 1 mg interval floor. Profile9/core28
now integrates region activity tiers through the game owner, with wrapper14
for immutable pending inputs. Actual speed, native frame waits, exact storage
and the accuracy/persistence proofs remain open.

This tracks work toward the full [completion plan](completion-plan.md), beyond
the narrower first private base-game playtest. A private candidate is already
available; it is not evidence that these requirements have passed. The earlier
playtest deferrals remain recorded until their own controls pass.

## Current repairs

- **3× instrumentation and activity integration, 2026-10-28:** performance diagnostics
  measure independent absolute owner/game clock deltas against monotonic wall
  time, retain pauses and report matched submit-through-managed-publication
  latency. Profile9/core28 adds persisted per-cell activity history, deterministic
  regional remote cadence, no catch-up and an Always simulate control. New
  colonies use it with the actor census, native contact/input provenance,
  immutable queued-frame save state and ordered owner germs/conversions/sinks.
  Controls cover multiple queued cuts, cold claims, lifecycle refresh, passive
  versus contact heat, terrain isolation and exact Always simulate equality.
  Base-game and ordinary nine-world Spaced Out! lifecycle, pending cold claim,
  paused full-owner resave and actual-file readback cohorts pass on Linux,
  preserving one supported actor binding and one packet claim without release.
  These do not certify the broader gameplay matrix or full active continuation.
  The earlier profile8 Linux Terra baseline reaches 1.935×; integrated tiers
  reach about 2.80× warmed actual owner/game progress. Owner work falls from
  about 65 to 30 ms. One clean 15 s window has 3 GCs and frame p99 95 ms, versus 5
  GCs and 120–123 ms in the earlier baseline. An opt-in trace attributes almost
  all remaining throughput loss to discarded game-update time (original cap
  plus pacing clipping); no admission policy change is adopted from that
  measurement alone. Full-game 3× and smooth-input acceptance remain open;
  see [measurements and remaining optimizations](performance-acceptance.md#requested-3-release-goal-2026-10-28).
- **Exact passive edits, 2026-10-28:** germ evolution now avoids cloning a
  uniquely held physical state, retaining full checkpoint/receipt bytes in
  matched replays. Captured native owner means improve 15–20%; all native,
  managed and output-comparison checks pass. Longer 900-frame Linux probes still
  slow from about 2.827× early to about 2.6× overall. Private reporting and
  report-cadence experiments do not establish a whole-game gain. The adopted
  bounded texture cache passes both standard test modes and halves measured
  GC frames; its private Linux run reaches 2.667× warmed speed, still below 3×.
- **Scenario baseline, 2026-10-28:** immutable `16da60c` also fails the unchanged
  freeze/melt check (357.9 kg, required >380 kg) and airflow gas check
  (4.278 → 4.052 kg, required increase >0.05 kg; liquid barrier passes).
  Both failures can predate boil-only and profile7; their causes remain open.
  Freeze/melt used llvmpipe, airflow NVIDIA, so these are functional results,
  not a performance comparison. No scenario inputs or bounds were changed.
- **Freeze/melt handoff:** the delivered `dde2a09` failed its unchanged
  arena-water check (337.9 kg; required greater than 380 kg). A diagnostic
  repeat admitted the full 400 kg ice source at 290 K and passed with 387.3 kg
  water. The immutable `d252486` Linux `linux-b1` repeat now passes the original
  terrain, heat, phase, freeze/melt and door controls, with unchanged inputs and
  bounds (`release-resume-v1`). Keep the older failed candidate as history;
  this focused repeat does not certify the complete acceptance suite.
- **Native frame handoff:** a focused asynchronous control proves that native
  shadow publication can return before the preceding owner frame finishes.
  Fence the next native frame before its messages are applied, and make shadow
  publication wait for a completed owner image. The focused test now passes,
  as do all 86 native tests. The short in-game repeat still fails freeze/melt
  (338.2 kg), so this is an independent synchronization repair, not proof of
  that scenario's cause or acceptance.
- **Storage transfers and heat:** settle completed native contact receipts
  before preparing reservoir admissions or withdrawals. The new
  `mixed-storage-transfer-heat` control exposes the old ordering: 453.252 J of
  cooling was pending at withdrawal, none was credited, and a later credit
  reached an empty store and faulted. The repaired game control passes all
  nine checks: it credits the same 453.252 J before discharging, and the first
  2 kg draw carries exactly its quarter of the cooled 8 kg inventory's energy.
  This separate regression waits at most 20 owner frames with its outlet
  closed for an observable completed cooling segment; the existing fixture's
  inputs and stages remain unchanged. An earlier repaired run observed no
  pending work and is retained as inconclusive, not a passing regression.
  The exact-storage prototype remains gated on the other lifecycle controls.
- **Filter fixture readiness:** the same candidate also failed batch10 because
  its filter and sensor inputs stayed empty; the later germ check then threw
  on an empty sequence. Wait for all actual native filter/sensor conduits to
  register before seeding, then retain the original 8-frame check intervals,
  mixture quantities and bounds. The repaired five-scenario batch passes all
  76 checks, including boundary transfers, output, demolition and valve rates.
- **Pump-rate observation:** the old candidate's batch11 moves the expected
  mixtures and passes its mass/account checks, but reads zero for both native
  pump rate accumulators. Sampling now waits for the native accumulator window.
  Immutable `d252486` Linux `linux-b11` passes vents, biology, all germs, pumps
  and thermal controls in `release-resume-v1`.
- **Nonempty storage and projection cleanup:** the development-only writer
  restores exact reservoir inventories by checking and rebinding every native
  projection. Schema6 preserves native consumer/dispenser controls after
  prefab startup. Native heat ABI2 retains preceding heat receipts until an
  exhausted body's zero-capacity removal is verified and acknowledged. The
  new fixed `owned-storage-retirement` control passes all six checks, including
  native cleanup after complete withdrawal. Cold and menu loading also pass;
  general storage mutations and full active continuation remain open.
- **Scheduled storage discharge:** the new `owned-storage-active-outlet`
  control exposes native cooling that finishes after an asynchronous managed
  withdrawal. Waiting at native GameData preparation closes that overlap;
  saves and the continuation probe also allow deferred Unity destruction to
  complete before capture. All five source checks and cold/menu reloads pass
  in the same frozen Linux bundle, with 8 kg discharged, two native bodies
  acknowledged and closed accounts. Full continued state still differs on
  the first resumed frame.
- **Drained-store history:** a second save after native cleanup passes seven
  source checks and cold/menu loading. All 24 source/cold/menu storage sections
  retain identical heat, transfer and culture history. The new control exposed
  a temperature-equality false positive in pipe-body save validation; a
  read-only native message completion observer now proves the queued work was
  processed. The complete world/pipe prefix still differs after loading.
- **Pipe continuation counter:** restore's extra zero-time callback changed
  only the saved contact diagnostic. Counting timed contacts while retaining
  all zero-time heat settlement yields byte-identical complete pipe images
  through eight source/cold/menu frames. The world image and native inputs
  still diverge on the first resumed frame; full colony continuation is open.
- **Empty reservoir demolition:** gated schema7 keeps completed histories in
  at most two exact phase aggregates after their native hosts are removed.
  The fixed Linux and WindowsPlayer/Proton demolition/save controls pass eight
  checks each; each platform's 24 source/cold/menu storage sections are
  byte-identical, with no live host.
  Nonempty demolition, containers and recipe mutations remain required.
- **World continuation context:** read-only source/cold/menu diagnostics show
  native RNG/tick state resets before resuming, different managed breather
  bucket phase and partial-step offsets, and five differing projected solid
  temperatures. Preserve native loop control first; managed scheduling and
  temperature provenance remain separate open repairs. Zero-time frames do
  not advance the observed native counters.
  Gated MACNRC01/section5 now preserves native loop state before positive time;
  host Linux and WindowsPlayer/Proton source/cold/menu controls pass, including
  exact counter readback and unchanged eight-frame pipe prefixes. Full world
  continuation still fails with different breathing/solid-heat inputs.
- **Saved projection loading:** a scoped native load guard prevents ONI from
  applying its load-time phase law before the mixture owner restores. The
  sampled Rock Gas cells now retain their exact saved identity and temperature;
  Linux and WindowsPlayer/Proton cold/menu checks pass. A separate tile-startup
  correction preserves native temperature/cultures when deserialized prefabs
  recreate matching cells. The final Linux cold/menu pair now matches all
  98,304 projected cells and the 180-cell native sample before resuming; thermal
  and breather scheduling continuation remain open. The final WindowsPlayer/Proton
  build also passes ordinary Spaced Out! source/cold/menu loading with nine
  worlds and both experimental controls off.

## Remaining implementation and proofs

| Work | Completion condition |
| --- | --- |
| [Exact component storage](mixed-pipes.md) | Transfers with outstanding thermal work, demolition, bottles and machine recipes preserve every constituent, energy and germ identity. Extend the verified reservoir binding/cleanup and schema7 empty-host retirement to the remaining lifecycle controls, then enable exact storage through the normal player path. |
| [Active continuation](mixed-pipes.md) | Pending activity requests now wait for supported actor startup, bind transactionally and survive zero-time flushes. Base-game paused resave and second cold file readback retain the full owner image and deliver the saved packet once with no released callback. Still compare identical future inputs and complete owner, pipe/scheduling and particle states after source, cold and menu loads; broader first-resumed-frame differences remain failures. |
| [Game-profile accuracy](base-game-floor.md) | Establish the frozen initial fields and independent timing truths for F3 and F4, including solid heat exchange. Run the unchanged floor comparisons. The accepted 0.703 J storage residual does not waive F4 or other accuracy bounds. |
| [Strong-flow accuracy](../native/MAC_STRONG_FLOW.md) | Repair the recorded collision-refinement and separation-velocity failures against their fixed grids and bounds. The separately recorded [gravity, free-surface, multiphase and vacuum gates](../native/MAC_GATES.md) already pass; preserve them. Game-profile floor results do not substitute for the strong-flow gate. |
| [DLC gameplay](dlc-compatibility.md) | First release: base game and Spaced Out!. Multi-asteroid ownership, rocket interiors, ports, flight, retirement, teleporters and cargo pass targeted controls; verify generation, gameplay and persistence for both selections on both operating systems. Colonies with Frosty, Bionic, Prehistoric or Aquatic active must stay on ONI's simulation with a notice. Those packs (bionic oxygen, aquatic systems) follow later; their content is not installed here. |
| [Starting air and progression](starting-air.md) | Nitrogen removed, default CO₂ opt-out, live switch, Advanced controls and finite starting oxylite reserve. Prove representative starts reach sustainable life support and endgame without developer intervention. |
| [Mixture presentation](mixture-presentation.md) | Finish exact container coverage and the required real-game hover, detail, overlay and contrast checks on both operating systems. Prove the 100,000-cell publication and allocation requirements as well as functional display. |
| [Performance](performance-acceptance.md#requested-3-release-goal-2026-10-28) | Latest release goal: sustained actual 3× with smooth input, camera and menus on the user's PC in representative base-game and Spaced Out! colonies. Fixed active-cell/duplicant, memory, disturbance-recovery and presentation bounds remain unchanged; current smaller-colony measurements do not certify them. Features may be removed for speed only by the user's choice. |
| Boil-only phase change | Implemented in `1058d0d` (options version 2); profile9/core28 retains either saved phase rule and defaults to boil-only. The native control, saved-profile support and the earlier Linux in-game phase scenario pass in both modes. Open: Windows in-game check. |
| Speed work, 2026-10-28 | Profile9/core28 is the new-colony default with integrated paused/1 s remote regions; wrapper14 preserves queued census/input delivery. Profile8's momentum-carrying sharpening/layering and 1 mg interval floor remain. The earlier matched Spaced Out! replay drops mean owner time from 0.764 to 0.583 s and the defective magma face from −2,081 to −25 m/s; the twelve-dig replay passes. These contended profile8 numbers do not measure tiers or establish real time. Open: actual integrated throughput, regional critical paths, space-sink and owner overhead. |
| Smooth input and UI | Readings allocate 64–68% less in fixed-JIT .NET controls and match the parent across 60,000 cells plus 528 edge cases. Event appends/prepends now omit temporary arrays; native reading copies use pinned publications. The integrated ultra Terra probe has one clean 15 s window with 3 GCs and frame p99 95 ms; native UpdateData remains about 14 ms. Advanced → Performance → Log frame timing enables Windows diagnostics. The one-GC/15 s goal and actual Windows smoothness remain open. |
| [Compatibility and packaging](windows-validation.md) | Complete current-bundle gameplay, natural autosave, cold/menu load and UI checks; resolve portable Linux ABI validation; retain separate evidence for Proton and actual Windows OS. |
| [Long regressions and release](completion-plan.md#release-acceptance) | Complete the base-game and Spaced Out! mode and all-content colony regressions, player documentation and human playtesting. Workshop publication remains a separate authorized action. |
| Game-update resilience | The mod and its saves are pinned to ONI build 744825 (whole-file hashes, fixed hook addresses, save envelopes). Verify each build by what the mod actually depends on, fail closed with a clear message, load saves across supported builds, and provide a re-targeting tool for new builds. |
| Release package and identity | One reproducible Workshop-shaped folder with both platforms' native libraries and no installer; final static ID and version that keep existing private saves and presets working; nothing written into the mod folder in ordinary play; clean refusal on unsupported operating systems; an accurate Steam description. |
| Licence and publication | The user chooses the licence later. Workshop upload needs the user's authorization. |

The existing immutable `dde2a09` verification continues independently of new
source changes. Its passed 30,000-frame/ten-cycle survival run is retained with
that revision, and its failed freeze/melt result is not erased by a successful
diagnostic repeat. No release-readiness row is marked passed by this tracker.

## Linux work while Windows playtesting is pending

Most engineering and the Linux half of acceptance can proceed now. Keep
evidence tied to one immutable candidate, covering the base game and Spaced
Out! through ordinary player paths and unchanged physical checks.

| Work to finish | Linux completion evidence | Windows/user work still needed |
| --- | --- | --- |
| 3× and smoothness | Activity tiers, full-cycle profiling, pressure/transport/owner optimization, actual clock throughput, input/frame logs and bounded memory/backlog | Actual Windows 9950X/5070 Ti speed and input/camera/menu response |
| Gameplay and coverage | Diagnose freeze/melt and airflow; current-profile scenario/starting-asteroid matrix, machines, life, pipes, heat and Spaced Out! travel/cargo | WindowsPlayer prechecks under Proton here, then actual Windows OS checks |
| Exact component storage | Bottles/canisters, recipes, thermal transfers and nonempty demolition preserve all constituents, energy and germs through normal play | Windows lifecycle/save/reader checks |
| Active save continuation | Identical future inputs give exact complete source/cold/menu owner, pipe and particle states; repair breathing/solid-heat scheduling | Windows same-platform continuation and bounded cross-platform save-transfer differences |
| Published-save compatibility | Define and control loading every published profile across future updates; retain corruption/missing-mod refusal | Windows loader/path/error checks; earlier R&D saves keep their existing policy |
| Accuracy | Complete F3/F4 comparisons and fixed collision-refinement/separation-velocity controls | Required compiler/platform coverage |
| Progression and long runs | Base-game/Spaced Out! starts reach sustainable life support and endgame without developer help; current-candidate autosaves, 100/1,000-cycle runs and inter-asteroid transfers | Windows half of long-run/save/DLC evidence and human playtesting |
| Presentation and automation | Real-game hover/overlays, containers, sensors and contrast; 100k-cell allocation/sampling proof | Windows rendering/scaling/controls and equivalent performance evidence |
| Compatibility/distribution | Ubuntu 22.04-compatible native build and game checks, Windows cross-build/Wine/Proton prechecks, reproducible mod-only folder, negative admission controls, retargeting, tested mod-compatibility policy and accurate player/minimum-hardware docs | Windows install/update behavior and hardware evidence; licence selection and separate Workshop upload authorization |

Proton pretests the Windows player on Linux; it does not certify Windows OS
behavior or the user's performance. Other gameplay packs remain outside this
first release and must be refused cleanly. The harness also needs the long-run changes in
[performance acceptance](performance-acceptance.md#measurement-and-test-harness-first).
