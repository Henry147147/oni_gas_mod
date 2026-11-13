# Feature completion work

**Update, 2026-11-13, 0.1.7:** the steam of the 0.1.4 recording is the
humidity of "Evaporation below boiling", which the user's own logs show on
in that installation; the start dialog now says when it is on. A pool that
is still levelling when its region reaches the activity pause keeps its
remote steps until it is level
([game kernel](../native/MAC_GAME_KERNEL.md#levelling-keeps-a-region-remote)).
0.1.6 was withdrawn: its acceptance run found that it did not load a
colony saved by 0.1.4 or 0.1.5 (it re-keyed the germs while restoring);
0.1.7 corrects that. Open items are in [0.1.7](playtest-0.1.7.md#open).

**Update, 2026-11-12, 0.1.6:** a probe of everyday liquid situations on the
solver alone found that water would not drain through a one-tile opening
into a closed space (it stood on the trapped air), that interface
sharpening held a tilted water and oil interface and could stir a resting
one, and that wide pools level slowly. The first two are fixed
([liquid over gas](../native/MAC_GAME_KERNEL.md#liquid-over-gas-profile-12-2026-11-12)).
The germ check open since 0.1.2 is traced to a pool tile's germs diffusing
into the air over it; germs are now carried by the gas or the liquid of
their tile and the germ rules run for each
([carried passives](../native/MAC_GAME_KERNEL.md#carried-passives-profile-12-2026-11-12)).
What stays open is in [0.1.6](playtest-0.1.6.md#open). The 0.1.5 acceptance
tier: 44 of 46 steps, the misses being the germ checks and a load fault
that did not recur in twelve reloads.

**Update, 2026-11-12, 0.1.5:** the user's recording of 0.1.4 shows a
35-tile pool sloshing with a half-tile step for over a minute after a shaft
was dug under it. Liquid had no damping; profile 12 now has a liquid drag
with a one-second time constant, and the same pool is flat in ten to
fifteen seconds without swinging over
([game kernel](../native/MAC_GAME_KERNEL.md#liquid-drag-profile-12-2026-11-12)).
The user raised going back to ONI's own liquid simulation; that would be a
large rework and is the user's decision
([0.1.5](playtest-0.1.5.md#open)). The 0.1.4 acceptance tier: 40 of 46
steps, the misses being the known germ check, a Proton new-colony exit
that passed when re-run, and a test click at the start dialog's old button
position.

**Update, 2026-11-11, 0.1.4:** by user decision oxygen, polluted oxygen,
hydrogen and chlorine have the masses of their molecules
([material catalogue](material-catalogue.md#molecular-masses-014-2026-11-11)):
normal base air reads about 125 kPa instead of 250, natural gas and steam
rise above oxygen and chlorine sinks below carbon dioxide. Earlier colonies
are refused at the menu (save material model 4), so the frozen acceptance
save and both full-colony benchmark saves were made again. Candidate
[0.1.4](playtest-0.1.4.md); its acceptance is recorded there.

**Update, 2026-11-11:** private candidate [0.1.3](playtest-0.1.3.md) carries
out a project review of speed, bugs and playability. The review found that
0.1.2 had slowed the Spaced Out! full colony by a third (170 to 228 ms a
tick on its replay, probably below 1× in game): thin-gas cells holding a
mist droplet were entered into the pressure solve as full liquid cells.
That is fixed (172 ms); large connected regions use the multigrid pressure
solve (industrial base replay 78 to 63 ms a tick); idle solver workers no
longer spin for 2 ms; paused frames, publications and overlays cost the game
thread less; the fault path no longer frees the snapshot the game reads.
Play: a rescaled pressure overlay, an oxygen margin overlay, a toxic gas
limit (5 % chlorine, sour gas and fallout), playable CO₂ limits when on, a
start dialog that lists what plays differently, and a slowdown sign
([measurements](performance-acceptance.md#review-measurements-and-013-2026-11-11)).
Its acceptance run also found that 0.1.2 stopped the simulation at frame 14
of a new Forest colony (a hydrogen draw beside a droplet; fixed) and that the
liquid stability change of 2026-11-10 lets pool germs reach the air above
(open). Delivered as `d9dd945`
([acceptance](playtest-0.1.3.md#acceptance)): in game on Linux the
industrial base runs 2.19 game seconds per real second (0.1.2: 1.73) and the
Spaced Out! full colony 1.02 (0.1.2: 0.875, below the beta bar). Still to
measure: all of it on Windows.

**Update, 2026-11-10:** the latest Windows recording confirms liquid
churn in a young Spaced Out! colony. Two independent causes are repaired:
sharpening gathered water sideways into taller columns, and remote activity
used coarse liquid-flow steps that fed waves on repeated wakes. Profile 12
now sharpens along the reconstructed normal and retains the saved active
flow resolution in wet remote regions
([cause and controls](../native/MAC_GAME_KERNEL.md#quiet-liquid-surfaces-profile-12-2026-11-10)).
The new actual-game `liquid-settling` control fails on the old Linux solver
at 421.57 kg late column variation and passes at 70.93 kg on Linux (base game)
and WindowsPlayer under Proton (Spaced Out!); all 17 liquid/flood/airflow/
freeze-melt checks pass in each with closed ledgers. The
recording confirms the earlier candidate on actual Windows, not the new
repair or representative full-colony throughput. The detailed timestamped
review is saved beside the user's recording. The private
[0.1.2 rebuild `b75d712`](playtest-0.1.2.md#liquid-stability-rebuild-2026-11-10)
is delivered beside it.
[Steam Offline Mode](steam-offline-playtesting.md) launches and advances
the Linux owner after the user's cached sign-in. The user confirms Steam
running on Windows concurrently; simultaneous Windows ONI simulation was
not independently observed.

**Update, 2026-11-09:** private candidate [0.1.2](playtest-0.1.2.md)
(rebuilt as `d7eb0e3`) answers the [0.1.1 playtest review](playtest-0.1.1-review.md):
the starting oxygen reserve is no longer skipped by a paused start, natural
tiles and polluted water sublimate by ONI's rule, and flooded pools no longer
spike to megapascals or liquefy CO₂ at room temperature. The cold gas at
strong sources is answered by the **Gas source energy** setting the user
chose (Compensated by default; Real physics and Internal energy offered).
Still open: a Spaced Out! colony in game and the Windows OS itself.

**Update, 2026-11-08:** private candidate [0.1.1](playtest-0.1.1.md)
(`a914a00`) makes colony settings changeable on an existing save, fixes the
Windows owner DLL's per-frame memory growth (52 GB in the user's Spaced Out!
colony) and makes the status panel movable, collapsible and hideable with a
top-bar button. A Spaced Out! colony and the Windows OS itself remain to be
confirmed by the user.

**Update, 2026-11-06:** a beta for wider playtesting needs sustained 1× on
the full-colony saves; 3× stays the speed goal
([amendment](completion-plan.md#amendments)). The base full colony runs about
2.2× on the Linux harness with overlapped pacing, and the Spaced Out! full
colony, rebuilt on profile 12 (`full-colony-spacedout-v2`), 1.05×; both
still need measuring on the user's Windows PC
([numbers](performance-acceptance.md#spaced-out-full-colony-on-profile-12-2026-11-06)).
The two-platform release folder builds reproducibly and passes in-game smoke
runs ([dry run](distribution.md)); the licence, a preview image and the
upload remain the user's.

**Update, 2026-11-04:** RC1 `258134c` passes the full private release tier,
all 47 steps including the ten-cycle run, and is delivered privately
([record](private-playtest-release.md#rc1-258134c-2026-11-04)). On the Linux
harness, profile 11 holds the 3× cap in steady windows. That covers the pinned
Spaced Out! world V9 (2.99–3.00×), new base colonies and the frozen save; the
cycle-2 save measures 2.88–3.09× ([numbers](performance-acceptance.md)).
Long-running-world speed work continues past 3×. Still open:

- Windows OS acceptance on the user's PC.
- Exact owned storage beyond reservoirs (a feature-scope decision for the user).
- Licence and Workshop publication.

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

- **Idle delivery and pacing, 2026-10-29:** production now pumps already
  published byproduct prefixes in idle Unity frames with a separate budget,
  sequence/session fences and lifecycle/reentry protection. Callback/job
  receipts remain on normal publication. Permanent actual-body and game-Mono
  checks pass. The combined private pump/credit experiment reaches nominal
  3× in fresh base game and 1.955× in Spaced Out!; save/readback, queued actor,
  controlled Lead merging and precise-pipe custody checks pass. Pacing credit
  now retains at most 2 s and repays under the original cap/busy guards, with
  default/on/off and actual game-Mono patch-order controls. Sustained representative 3× and smooth
  input remain open. The selected native held-vector representation improves
  captured DLC mean/p95 and real DLC progress to 2.090×, while the base-game
  clock stays at nominal 2.997×. Its standalone base p95 regression does not
  produce a meaningful clock regression in that bounded game probe. Broad
  release performance still needs representative acceptance.
- **DLC texture fallback, 2026-10-29:** all 5,543 diagnostic rejections were
  modulo collisions despite spare bounded capacity. The production preflight
  now permits conflicts as misses, preserving slot locks and full cache keys.
  Scalar/renderer, actual game-Mono and 6,144 sampled CPU destination-byte
  controls pass. An ordered DLC comparison improves 1.906→1.948× and lowers
  heap-growth estimates; native work and representative 3× remain open.
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
- **Filter fixture readiness:** the same candidate also failed batch 10 because
  its filter and sensor inputs stayed empty; the later germ check then threw
  on an empty sequence. Wait for all actual native filter/sensor conduits to
  register before seeding, then retain the original 8-frame check intervals,
  mixture quantities and bounds. The repaired five-scenario batch passes all
  76 checks, including boundary transfers, output, demolition and valve rates.
- **Pump-rate observation:** the old candidate's batch 11 moves the expected
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
  Nonempty demolition, containers and recipe mutations stay ONI's own since 2026-11-04, with their crossings booked ([storage crossings](colony-owner.md#storage-crossings)).
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
| Game-update resilience | Implemented ([design](game-update-resilience.md)): packages carry a `game-contract.json` of what the mod actually depends on (2589 dependencies, verified against public build 744825 on 2026-10-29), fail closed with a clear message, load saves across supported builds, and `tools/retarget_oni.py` re-targets new builds. Each new Klei build still needs a contract check before release. |
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
