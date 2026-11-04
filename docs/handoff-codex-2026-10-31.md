# Release continuation, 2026-10-31

The active goal is release readiness. Continue on `main`; the mod is still not
release-ready. Actual Windows OS testing is explicitly deferred: do not ask
again or publish a Workshop release. Fixed inputs, grids and bounds remain
unchanged except by the decisions below.
The [previous handoff](handoff-codex-2026-10-30.md) records the broader failures.

## Review and fixes, 2026-11-02 (Claude)

The user asked Claude to review Codex's 10-28–10-31 work, confirmed **3× as
the release gate**, and delegated the pending decisions; they are recorded in
the [completion plan amendments](completion-plan.md). Breathing: the
per-breather production criterion is adopted and passes in game; the
breath-particle diagnostic pairs each loss with the trace sink booked in the
same interval instead of the whole colony's cumulative sink. Freeze/melt: the
any-frame observable proposal is rejected; the >380 kg deadline check stays.
Retained pacing credit is kept. Licence and Workshop upload stay the user's.

An independent clean rebuild of `51e8879` reproduced 91/91 CTests, every
managed suite, 239 snapshot scenarios, the 2589-entry game contract, the
`patch.hpp` hash and identical all-88 outputs for the remap change. Fixes now
on main (no solver arithmetic or checkpoint byte changes):

- Saves no longer race an activity frame popped between settle and the locks
  (reproduced ~300 unrestorable saves per control run before the fix); the
  activity back-pressure wait exits on stop.
- The per-Prepare native Step wait runs only under exact storage; ordinary
  games keep main/native overlap (A-B-B-A 900-frame base: 2.935× → 2.969×,
  weak evidence, still under 3×).
- Frame-fence waits are bounded and a stale Windows frame lock is released at
  the next BeginFrame; a paired-heat fault that cannot be recorded fails closed.
- Saves retry (bounded) while exact-storage thermal publication is pending;
  spawned byproducts are acknowledged on surviving early exits (no duplicate
  debris); the regional coordinator thread checks the FP environment; phase
  relaxation reads pair masses through the cell after copy-on-write detach.
- Pacing and receipt-fence tests now run production code; result-collection
  Python tests no longer depend on the enclosing repository's HEAD.

**Runtime continuation (closed 2026-11-03, Claude):** the record-6 WIP was
cut, never committed. Every one of its fourteen in-game source captures failed
before a load, and it could not become exact in any case: two cold loads of
one save diverge from each other once the game runs freely, because duplicant
movement follows real-time render frames. The rebased patch is kept privately
in `artifacts/continuation/`. MACOLP03 tag 6 / `MACGRT01` stay retired
unwritten ([checkpoint formats](../native/MAC_CHECKPOINT.md)). What is
established instead is continuation of the mod's own state under identical
inputs, in actual colonies ([evidence](colony-owner.md#continuation-evidence)).

## Current integration

The exact remap-plan optimization passes a clean Linux Release full build and
all 91 CTests, using 1228 pinned source files with runtime WIP excluded. It
reuses four call-local buffers and emits dense
affected-cell IDs from the existing stamps in their original ascending order.
The production header SHA256 is
`559a6715d32d70f071c76f6348cca8d19103dcfd8ea7da97ee6dae73816dfd34`.
FP expressions/reductions/order and identical-input checkpoint bytes are
unchanged; allocation-failure positions can differ. No version is reserved.

Both clean Linux and Windows/Wine comparisons retain all 88 numerical outputs
and exits, including eight known nonzero diagnostics; only existing
`ref_ms`/`prod_ms` fields differ. Targeted6182 Release/ASan/UBSan/errno controls,
full owner/receipt/pre-flow/status replay and independent cold/menu future
images pass. Windows also uses the selected owner math/allocator runtime.
The permanent synthetic regression pins original outputs, analytic two-cell
flux, changing prefixes and lowest-cell error/recovery. Stale-prefix and
descending-order mutants fail behaviorally; valid canonical sums can be
order-independent. The measured math header is unchanged by adding the test.

Seven isolated hot-replay pairs improve median steady native advance
1508.38→1442.00 ms (ratio 1.04657, all seven faster). This repeats the original
saved CO2 parcel, not the complete live trajectory. Balanced fresh8-pair
results are base cold/menu0.98623/0.99890 and DLC1.01060/0.99504, with mixed
signs and broad ranges. A small base-cold cost remains unresolved; do not
claim fresh speedup, equivalence or actual 3× from this change.

## Verified and open

- Clean committed `3d8c20f` passes 91 native tests, every managed suite and 239
  snapshot scenarios. Explicit PowerShell/backend Python coverage passes 485
  with four actual-Windows-only skips; the initial run's other seven skips are
  closed separately. All1227 source hashes remain fixed. Actual Linux game
  Mono checks 2589 dependencies with zero differences. The one-off native build
  is removed after retaining libraries, CTest logs/config and executable hashes.
- Committed compact pressure/hazard channels retain exact bytes. Actual Mono
  method benchmarks improve 2.32877×/7.21132×; these are not game speedups.
  The frozen exact3d900-frame base/DLC WHOLE checks now pass functionally at
  actual game 2.913118×/2.339865× (owner 2.912785×/2.339596×), with three
  duplicants, no faults/observed pauses/dropped samples. These full 0–900 rates
  are close to the earlier 2.91709×/2.33570× controls; different cohorts do not
  establish a causal comparison. Neither meets 3×. The package predates the
  remap integration; normal-reserve/smoothness/Windows remain separate.
- A separate private64-entry, worker-block-local velocity cache passes 6182
  literal-original Release and targeted ASan/UBSan controls plus 24 complete
  hot owner/receipt/pre-flow/status frames and 348 fresh cold/menu frames.
  Independent default-OFF future images remain exact. Isolated hot7-pair
  steady ratio 1.014323× is mixed; first-advance ratio 0.967028× is slower.
  Balanced fresh8-pair base cold/menu ratios 0.999331/0.994748 and DLC
  0.965330/1.016068 do not establish a useful general gain. It is not adopted;
  broader all 88/Windows qualification stays unrun. CPU HOLD ended 00:55:22 UTC.
- The pool probe's fresh replays have zero inner/residual selections or serial
  probes (six unknown/ten too-small decisions each). Alternating slow frames
  still occur on the outer-only route. Do not attribute these new observations
  to inner selection, or infer choices in earlier unlogged runs. Registered
  pool CPU-time data is being reviewed; no parking/selector change is adopted.
- The unchanged freeze/melt batch has 43 coherent observations. At check 42,
  liquid 368.293808 kg plus visible ice 111.706198 kg accounts for 480 kg within
  5.4 mg; no product is pending. Water loss matches the conversion ledger to
  about1e-11 kg. Hidden-liquid owner conversion produces the ice; no native
  freeze input occurs in that window. Local post-source heat is−12.146388 MJ.
  The original >380 kg liquid gate still fails. Joint native/owner energy and
  thermal/phase behavior remain open; do not tune the rate or add fusion heat
  merely to pass the fixture.
- Report-writer serialization is committed and passes Linux controls. Actual
  Windows game Mono on Wine fails concurrent reader stress: `File.Replace`
  briefly loses the destination while writes finish successfully. Private
  MoveFileEx fails open-reader sharing; FileRenameInfoEx is unsupported there.
  Neither alternative is adopted. Resolved 2026-11-02: the contract is a whole
  document for every read, with a missing name retried; the stress control
  counts those retries, and the harness's live report polling now tolerates
  them. Actual Windows OS acceptance stays separate.
- Runtime continuation: superseded by the 2026-11-03 note above.
- The private airflow core reaches 22 optimized/sanitized component groups,
  retaining historical no-mask outputs. A missing aperture/inertia view in
  recursive positive-pressure correction is fixed; the unchanged high-speed
  fixture now passes. Typed heat/mask/trace receipts separate physical packet
  predictions from exact grid credits. Global geometry/halo, regional/owner
  execution and channel consumers remain unfinished/refused. Experimental
  checkpoint encoding is refused; no new law/version is claimed.

## Resume

1. The remap integration verification passes; preserve its arithmetic/checkpoint
   statement and frozen targeted cross-platform proofs. Keep unrelated runtime
   WIP intact. Do not expand diagnostic masks or claim the known nonzero
   physics diagnostics passed.
2. The uninstrumented clean3d whole-game900-frame checks are complete; the
   separate cache is not adopted and all worker CPU holds have ended.
   Future timing requires explicit worker holds; each game uses its own
   `game_slot.sh whole` call without an outer CPU lock.
3. Continue the separate cache replay/cost work and evidence-led pool analysis;
   no speculative scheduler tuning. Runtime continuation is closed (above).
4. Complete the conservative airflow owner path, then run the original gate.
   Continue the freeze/melt energy audit; its material custody is now measured.
5. Remaining physics/accuracy, powered/mixed heat, default activation, exact
   storage/bottle/recipe teardown and sustainable progression gates remain as
   recorded in the previous handoff. Full F4 is 20000 simulated seconds; do not
   substitute a shorter duration. Actual sustained 3× remains unproven.
6. Exact storage scope (assessed 2026-11-03, Claude). **Decided 2026-11-04:** storage
   beyond the reservoirs stays ONI's, crossings booked; see
   [storage crossings](colony-owner.md#storage-crossings). The assessment follows. Ordinary colonies already store fluids as ONI items, one per
   species, and book every mass, energy and germ-identity difference at that
   boundary to the machine ports' `StorageBoundary` ledger, which saves with
   the pipes. The exact adapter keeps a mixture's heat and germs inside a
   store, but covers only gas and liquid reservoirs, under a development
   control, and refuses nonempty teardown. Extending it to the rest means an
   adapter, a save record and in-game qualification for each of: 91 base-game
   prefabs that combine `Storage` with fluid input or output, 74 fabricator
   recipes with fluid inputs or outputs, 32 element converters, loose bottles
   and canisters that duplicants carry, mop and pitcher-pump pickups, and the
   demolition of every one of them.

## Private evidence and ownership

Evidence remains under `artifacts/release-ready-2026-10-30/` even though this
continuation crosses midnight:

- `remap-plan-adoption-v1/` — clean integration source/build/CTest.
- `late-flow-audit-v1/ADOPTION-READY.json`, `remap-plan-adoption.patch`,
  `all88-86e5eb3/current-replay/whole{,-fresh-v2}/` and `windows-v1/`.
- `face-cache-prototype-v1/` — root's separate cache prototype and completed
  hot/fresh measurements; not adopted. `face-cache-broad-v1/` stays unrun.
- `actual-speed-3d8c20f-v1/` — completed whole-game plan, results and analysis.
- `full-committed-3d8c20f/{verification,python-expanded,cleanup}.json`.
- `freeze-custody-v1/{analysis.json,NEXT-AUDIT.md}` and original case output;
  `freeze-energy-v1/` prepares exact carried-parcel energy replay/audit.
- `report-json-fix-v1/windows-mono-qualification*.json` — retain failures.
- `continuation-runtime/`, `airflow-core-port-v1/` and `current-progress.json`.

The runtime worker owns its managed continuation/save files, native backend
records/tests and focused suites. The airflow worker owns private core-port
artifacts. The native performance worker owns private remap qualification,
regression proposal and pool probe. Root owns remap main integration, docs,
the separate velocity-cache prototype and actual whole-game speed checks.
All CPU work uses the shared locks; LOW/HIGH share SMT cores and are only
functional concurrency. Source edits may continue during coordinated timing
holds, but no other project build/test/game may run until END.
