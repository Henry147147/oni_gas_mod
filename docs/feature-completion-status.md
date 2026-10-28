# Feature completion work

**Current goal, 2026-10-27:** a Steam-ready first release covering the base
game and Spaced Out! ([amendment](completion-plan.md#amendments)). A private
Windows candidate is with the user ([current instructions](spaced-out-private-playtest.md));
actual Windows feedback remains pending. Revision `d252486` removes
nitrogen, makes CO₂ breathing limits optional (off by default) and moves
technical controls and solver presets under Advanced. Release engineering now
continues with exact storage and continuation. Performance and the
accuracy/persistence proofs below remain required.

This tracks work toward the full [completion plan](completion-plan.md), beyond
the narrower first private base-game playtest. A private candidate is already
available; it is not evidence that these requirements have passed. The earlier
playtest deferrals remain recorded until their own controls pass.

## Current repairs

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
| [Active continuation](mixed-pipes.md) | Preserve callback-owned requests and rebind actors to stable identities; compare identical future inputs and complete owner, pipe/scheduling and particle states after source, cold and menu loads. Current first-resumed-frame differences remain failures. |
| [Game-profile accuracy](base-game-floor.md) | Establish the frozen initial fields and independent timing truths for F3 and F4, including solid heat exchange. Run the unchanged floor comparisons. The accepted 0.703 J storage residual does not waive F4 or other accuracy bounds. |
| [Strong-flow accuracy](../native/MAC_STRONG_FLOW.md) | Repair the recorded collision-refinement and separation-velocity failures against their fixed grids and bounds. The separately recorded [gravity, free-surface, multiphase and vacuum gates](../native/MAC_GATES.md) already pass; preserve them. Game-profile floor results do not substitute for the strong-flow gate. |
| [DLC gameplay](dlc-compatibility.md) | First release: base game and Spaced Out!. Multi-asteroid ownership, rocket interiors, ports, flight, retirement, teleporters and cargo pass targeted controls; verify generation, gameplay and persistence for both selections on both operating systems. Colonies with Frosty, Bionic, Prehistoric or Aquatic active must stay on ONI's simulation with a notice. Those packs (bionic oxygen, aquatic systems) follow later; their content is not installed here. |
| [Starting air and progression](starting-air.md) | Nitrogen removed, default CO₂ opt-out, live switch, Advanced controls and finite starting oxylite reserve. Prove representative starts reach sustainable life support and endgame without developer intervention. |
| [Mixture presentation](mixture-presentation.md) | Finish exact container coverage and the required real-game hover, detail, overlay and contrast checks on both operating systems. Prove the 100,000-cell publication and allocation requirements as well as functional display. |
| [Performance](completion-plan.md#release-acceptance) | Measure the active-cell/duplicant, memory, disturbance-recovery and presentation targets on the specified hardware. The current smaller-colony measurements do not certify them. Preserve the accepted physical model while optimizing. |
| [Compatibility and packaging](windows-validation.md) | Complete current-bundle gameplay, natural autosave, cold/menu load and UI checks; resolve portable Linux ABI validation; retain separate evidence for Proton and actual Windows OS. |
| [Long regressions and release](completion-plan.md#release-acceptance) | Complete the base-game and Spaced Out! mode and all-content colony regressions, player documentation and human playtesting. Workshop publication remains a separate authorized action. |
| Game-update resilience | The mod and its saves are pinned to ONI build 744825 (whole-file hashes, fixed hook addresses, save envelopes). Verify each build by what the mod actually depends on, fail closed with a clear message, load saves across supported builds, and provide a re-targeting tool for new builds. |
| Release package and identity | One reproducible Workshop-shaped folder with both platforms' native libraries and no installer; final static ID and version that keep existing private saves and presets working; nothing written into the mod folder in ordinary play; clean refusal on unsupported operating systems; an accurate Steam description. |
| Licence and publication | The user chooses the licence later. Workshop upload needs the user's authorization. |

The existing immutable `dde2a09` verification continues independently of new
source changes. Its passed 30,000-frame/ten-cycle survival run is retained with
that revision, and its failed freeze/melt result is not erased by a successful
diagnostic repeat. No release-readiness row is marked passed by this tracker.
