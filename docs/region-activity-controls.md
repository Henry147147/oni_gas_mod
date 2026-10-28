# Region activity integration controls

`activity-lifecycle` is a separate private game fixture. It does not change the
inputs or bounds of an existing scenario. Run it against the integrated profile9
bundle with `MA_COLONY_ACTIVITY_CONTROL=1`, through `tools/game_slot.sh`, after the
native and managed checks pass. Saves and runtime evidence remain private under
`artifacts/` or `local/`.

The fixture uses a real Puft, an unpowered base-game filter with a secondary conduit output,
and a solid Tile in a sealed polluted-oxygen arena. It checks:

- An inactive prefab starts before its first spawn; activating it publishes actor
  presence. An already spawned actor remains in the census while inactive.
- Moving the inactive actor includes its new position and touches its previous
  footprint. Destroying it clears both positions after game cleanup.
- Building placement cells and secondary ports enter the actor mask. A solid
  building marks its adjacent fluid contacts, while the solid cell stays unmarked.
- Paused lifecycle flushes and policy disable/enable preserve the exact owner time,
  game clock and completed-frame count. Re-enabling tiers clears pending evolution
  instead of catching up.
- The supported dev life-support prefab supplies a real native consumer and
  emitter beside a second live Puft. The control prepares one upcoming physical
  frame, including actual callback-owned actor transfers and its full census. The ordinary game save writes `activity-pending.sav`
  with that positive packet still unbound. The uninterrupted next `NewGameFrame`
  delivers the prepared packet once, which the scenario also checks. A cold load claims the saved packet and skips
  actor transfer/census generation for that frame.

The last control is scoped to this named scenario. It does not stage frames during
normal gameplay. Cold-load bookkeeping still migrates any native-registered
pre-owner consumers and emitters out of ONI before claiming a saved packet; it does
not regenerate their already-saved transfers or jobs.

For a source run, use `--mode colony-owner --scenario activity-lifecycle
--save-colony --frames 450 --speed 3 --dlc-mask 0 --gpu-display :99`. A named scenario
finishes when its checks finish; the frame target is not a 450-frame performance
window in this case. Retain its private `activity-pending.sav`, then run
`--mode colony-owner-load --load-save <that save> --resave-before-resume --frames 80`
with the same DLC mask. The scenario also supports the existing
`MA_COLONY_CONTINUATION_SOURCE=1`, `MA_COLONY_CONTINUATION_FRAMES=N` and
`MA_COLONY_CONTINUATION_DUMP=1` controls for the uninterrupted branch. Use the
same continuation-frame count for the cold load. The load must report exact frame-zero owner image
re-encoding, at least one `RegionActivity.ClaimedFrames`, no owner fault, and closed
mass/energy ledgers. Record `ReplayMigratedActors` to establish whether that load
actually exercised pre-owner native actor migration during a paused resave or a
positive claim. A zero value does not prove
that migration path. Repeat the lifecycle and saved-load controls with the DLC
selection used by the playtest.

The paused owner-load resave waits for actual active saved components to finish
initialization and `OnSpawn`, then uses `Game.StartDelayedSave`. It keeps its
baseline and pause while the deferred save completes. It requires exact full
owner bytes before/after, matching physical bytes and game/owner clocks, unchanged
pending packet watermarks/head/timestep/count/policy, and canonical callback
requests. `WriterOwnerSha256` identifies the image supplied to the envelope writer;
it is not disk readback. Cold-load the generated `owner-frame-zero-resave.sav`
without `--resave-before-resume` and require `RestoredOwner.ImageSha256` to match
that writer hash, with `Reencoded=true`, to prove actual file readback.

Saved supported callbacks wait for their restored component and native handle,
even before registration has begun. All requests are preflighted before creating
any consumed-callback handle; a later unresolved actor cannot leak handles on
retries. Failed construction/native submission rolls back newly created handles.
Explicit unsupported records retain their release policy; the existing final
fallback is separate from readiness. Zero-time native tail work preserves
callback-owned inputs protected by pending positive packets. A successful known
actor control requires `Rebound=1`, `Released=0`, `NativeReleased=0` and one saved
packet claim, with no creature/state-machine deserialization errors.

`MA_COLONY_ACTIVITY_CONTROL=1` also checks real registered actors' occupied cells,
building placement caches and conduit utility cells. For observed completed
packets it compares the submitted actor census, projected through the same
publication's fluid/wall topology, against the native sampled mask. `MaskChecks`,
`MaskCellsChecked`, `MaskBusy` and `DroppedChecks` describe this sampled coverage;
not every submitted frame is necessarily observed. Static world-generation debris
is excluded from persistent actor presence. Its actual inputs and thermal contacts
wake regions at the native boundary.

Run a separate scenario-free 450-frame session for performance. Enable
`MA_FRAME_LOG=1 MA_COLONY_NATIVE_WAIT_LOG=1`, use 12 native workers, and compare two
warmed 150-frame windows with the frozen profile8 baseline. Report actual owner and
game clock advance per monotonic real second, pause/reset coverage, full
submit-to-publication latency, partial owner time, GC intervals and native waits.
The speed button, Unity time scale and the capacity estimate do not establish
actual 3×. A physical 0.2-second frame has a 66.7 ms end-to-end budget at 3×.

The engine-free `tools/ColonyActivityTests` cover rotated/clipped footprints,
external cardinal contacts at an owner's boundary, immutable census transitions,
zero-time refresh settlement, exact restored timestep/session/sequence admission,
wrapper14 metadata including callback wrapper13-over14, exact paused-resave
boundaries, immutable packet/input bytes, and all-or-nothing callback readiness
with handle rollback. The optional directory
argument reads the three native synthetic owner images emitted by the focused
native control; those images stay ignored. The saved policy is
`simulation.regionActivity`: `tiers` (Activity tiers) or `always` (Always simulate).
It is chosen for new colonies and read-only while playing. Older profiles restore
as Always simulate. Existing diagnostic profile6/7 selectors retain their meaning;
`MA_COLONY_KERNEL3_PROFILE8=1` requests the legacy profile8 game owner for comparison.

## Private Linux evidence (2026-10-28)

The frozen r7 bundle passes all 16 base-game lifecycle checks. Its original pending
save cold-loads with one rebound actor request, no released request and one packet
claim. The deferred paused resave keeps the complete owner image byte-identical;
a second ordinary cold load reproduces the exact writer hash from the actual
resaved file. Both loads complete 80 resumed frames with clean creature startup.
Migration counters are zero in these runs, so they do not establish coverage of a
pre-owner native registration being migrated during replay.

A separate default 450-frame Terra run measures about 2.80× across the two warmed
150-frame windows, versus the frozen profile8 baseline's 1.94×. This is an
improvement, not a sustained 3× result or Windows gameplay acceptance. An opt-in
pacing trace measures 2.5586 seconds lost to the original cap and 1.2815 seconds
lost to pacing across 60 simulated seconds. The observer changes no timestep,
debt or fence behavior; any dt-retention experiment remains separate.
