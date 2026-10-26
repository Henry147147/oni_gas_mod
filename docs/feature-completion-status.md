# Feature completion work

This tracks work toward the full [completion plan](completion-plan.md), beyond
the narrower first private base-game playtest. A private candidate is already
available; it is not evidence that these requirements have passed. The earlier
playtest deferrals remain recorded until their own controls pass.

## Current repairs

- **Freeze/melt handoff:** the delivered `dde2a09` failed its unchanged
  arena-water check (337.9 kg; required greater than 380 kg). A diagnostic
  repeat admitted the full 400 kg ice source at 290 K and passed with 387.3 kg
  water. The original failure remains open. Identify the native/owner timing
  difference and verify the repair without changing the inputs or bound.
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
  pump rate accumulators. Diagnose the accumulator update and sampling epochs;
  neither a positive rate nor a repair is established yet.

## Remaining implementation and proofs

| Work | Completion condition |
| --- | --- |
| [Exact component storage](mixed-pipes.md) | Transfers with outstanding thermal work, demolition, bottles and machine recipes preserve every constituent, energy and germ identity. Nonempty stores restore native projections and biology after a cold or menu load. Then enable the reserved schema5 writer through the normal player path. |
| [Active continuation](mixed-pipes.md) | Preserve callback-owned requests and rebind actors to stable identities; compare identical future inputs and complete owner, pipe/scheduling and particle states after source, cold and menu loads. Current first-resumed-frame differences remain failures. |
| [Game-profile accuracy](base-game-floor.md) | Establish the frozen initial fields and independent timing truths for F3 and F4, including solid heat exchange. Run the unchanged floor comparisons. The accepted 0.703 J storage residual does not waive F4 or other accuracy bounds. |
| [Strong-flow accuracy](../native/MAC_STRONG_FLOW.md) | Repair the recorded collision-refinement and separation-velocity failures against their fixed grids and bounds. The separately recorded [gravity, free-surface, multiphase and vacuum gates](../native/MAC_GATES.md) already pass; preserve them. Game-profile floor results do not substitute for the strong-flow gate. |
| [DLC gameplay](dlc-compatibility.md) | Implement multi-asteroid ownership, rocket interiors/cargo and transfers, bionic oxygen and aquatic systems. Verify generation, gameplay and persistence for the supported content combinations. The existing matrix runner is infrastructure; Spaced Out! remains explicitly inactive. Linux content bundles are currently missing. |
| [Starting air and progression](nitrogen.md) | Complete nitrogen thermodynamics and world generation with finite reserves, the hard/easy breathing starts, and the population's life-support bridge. Prove representative starts can reach sustainable life support and endgame without developer intervention; element registration alone does not satisfy this. |
| [Mixture presentation](mixture-presentation.md) | Finish exact container coverage and the required real-game hover, detail, overlay and contrast checks on both operating systems. Prove the 100,000-cell publication and allocation requirements as well as functional display. |
| [Performance](completion-plan.md#release-acceptance) | Measure the active-cell/duplicant, memory, disturbance-recovery and presentation targets on the specified hardware. The current smaller-colony measurements do not certify them. Preserve the accepted physical model while optimizing. |
| [Compatibility and packaging](windows-validation.md) | Complete current-bundle gameplay, natural autosave, cold/menu load and UI checks; resolve portable Linux ABI validation; retain separate evidence for Proton and actual Windows OS. |
| [Long regressions and release](completion-plan.md#release-acceptance) | Complete the plan's later pack/mode and all-content colony regressions, player documentation and human playtesting. Workshop publication remains a separate authorized action. |

The existing immutable `dde2a09` verification continues independently of new
source changes. Its passed 30,000-frame/ten-cycle survival run is retained with
that revision, and its failed freeze/melt result is not erased by a successful
diagnostic repeat. No release-readiness row is marked passed by this tracker.
