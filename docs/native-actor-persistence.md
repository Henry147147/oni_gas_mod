# Saving registered native fluid actors

Actor persistence is part of the native backend's save contract. A saved
consumer or emitter needs its remaining timing and a stable game object
identity as well as the fluid state. Native handles and callback indices belong
to one running session; they are not persistent object identities.
The [native wire-format contract](../native/ONI_ACTOR_CHECKPOINT.md) specifies
the versioned bytes and public extension ABI.

The new bounded extension implements that contract for admitted, disease-free,
single-cell `ElementConsumer` and `ElementEmitter` objects. The component must
share a root with a registered `KPrefabID` and `SaveLoadRoot`. Its identity is
the saved prefab instance ID, prefab tag fingerprint, actor kind and component
index. Unsupported or unsaved registrations continue to prevent a complete
checkpoint. This does not add powered pumps, metabolism or general machines.

## Capture and restore

Persistent configurations reserve a complete batch atomically. Duplicate
identities, stale frames, changed prefab identities and unsupported phases
reject the batch. Stable identities order persistent actor operations, so a
different native handle allocation after loading cannot reorder transfers.
Refreshing an unchanged persistent configuration preserves its timing.

An ordinary colony save drains edits and completed callback receipts, then
captures native cells, the solver and durable actor records at the same worker
fence. The `MAACTR01` section retains emitter elapsed time, consumer binary32
rounding remainder and blocked state, with a SHA-256 digest. It excludes native
handles, callbacks, thread IDs and session IDs. The enclosing native-domain
payload is version 2; actor-free payloads retain their original version and
physical bytes. The legacy physical-only export still refuses live actors.

Before native load, both managed and native admission validate the section and
the complete native solver model. Restored records remain pending until normal
managed scene spawning creates their actual consumer/emitter components. Their
fresh native registrations must match every saved identity, location, material
and configuration. Inactive spawn bootstrap messages cannot reset saved timing.
ONI's emitter waits for a `SimComponent` dirty-state update after registration.
During the initial restore pause, the adapter invokes that component's real
`Sim200ms(0)` configuration pass after its genuine handle callback, verifies
that it matches the saved record and retains physical time. It does not spend
a managed simulation subtick to make an emitter ready.
The managed game holds positive time while bindings are pending; the native
worker also faults if an incomplete restore attempts to advance. Native fluid
exclusion remains installed after a fault.

The emitter's nonserialized blocked property is restored without publishing
another blocked edge. Positive consumer parcels are checked against the real
`OnElementConsumed` callbacks, including count and exact reported mass. Complete
snapshot continuation includes actor timing and fluid state; a fluid-only match
does not satisfy this check.

## Verification and current limits

The native controls pass gas, compressed water and water with gravity. They
reverse registration order, change native handles, preserve a positive rounding
remainder and blocked/timing state, reject malformed images with recomputed
checksums, and compare ten resumed worker frames exactly in each case.
Premature positive advancement faults without advancing or releasing ownership.
All 13 native tests pass in the Linux compatibility build, with Linux
address/undefined-behavior sanitizers, and as Windows binaries under Wine. Both
platforms' managed declaration suites pass 132 groups, and 221 Python checks
pass with four optional/platform skips.

The actual Linux gas source uses frozen `6368468` and native compatibility build
v12. It saves three live actors after 121 physical frames, including 114 real
consumer callbacks carrying exactly 1.1400000331923366 kg. Both emitter timing
remainders and the blocked state are present. The complete source image is
`86af26c9e96ff4e701863e180947202c245b16c0321d39167eabd480dbe2e56b`.
The ordinary save body contains 5,125,525 bytes; its file SHA-256 is
`9144b42247ca0980291881854ef6cdda03cb99d88b0ced5303f3dd11543124b7`.

The first load correctly fails at physical frame zero with its consumer bound
and two emitters pending. It exposes the deferred emitter configuration above;
the original failed report is retained. The repaired `6216773` load uses that
same unchanged source file. Both native sessions 1 and 2 bind all three actual
components, flush two real zero-time emitter configurations, complete the
paused frame-zero autosave/body/header readback and compare 120 complete
actor-and-fluid images exactly. Each receives 120 matching consumer callbacks.
Both final images equal
`abf65e2b3cd5bd4d0c7a31774e5e1e7256d18aa7276ef6833ea9d762c80aa0c3`.
The same-process shutdown/menu lifecycle, restored pressure/breathing readers,
calibrated GPU bytes and private-display cleanup also pass.

Independent gas review SHA-256:
`dd96ff6067c0d333df57e942136ef6fa8084a5f0aff1f99977d8457ef65ae309`.
Raw source/load results are
`814d7fed55f1e97ec0ba6866142f3911da42410e51831714f9f53632b78126b7`
and `e5147ea624b8455a1e0beabc6c22bb1a659b9c56176a3b737b2b6ce5baf5184b`.
This accepts the bounded Linux gas actor save/reload.

The subsequent water source and both load sessions also pass with `6216773`
and native v12. The source saves all three live water actors after 121 frames;
its complete body is 5,134,605 bytes. The original file remains unchanged with
SHA-256 `869e84d4aab3853dc2c1aaa1e136db233fbbe0817e6388c808d4b7b32f53d839`.
Both load sessions flush two genuine zero-time emitter configurations, finish
the paused autosave/readback, receive 120 matching consumer callbacks and
compare every one of 120 complete resumed frames. Their final actor-and-fluid
image is `67804efcdd6332085905330a90f1823cedb488cd9d79935e2e2032cc724251bd`.
Restored readers, calibrated GPU bytes, shutdown/menu reload and desktop
cleanup pass. The second save body contains 5,134,810 bytes.

Independent water review SHA-256:
`134ee78fe958e39d739fd670fb96e41e00fbdc1ad8ba9161535a9a930626324b`.
Raw water source/load results are
`3d33fd6b4719b540cc5a9ff551390c8e3ae219353a3b2d88e76404f72d9fa0d3`
and `5efe7a88e7044c8a5ad0204d049fa5bce28cdd87bf040524669ef46e2ccc3826`.
Windows Unity actor execution and a natural day-rollover save with live actors
remain under validation. These actor cohorts have no separate manual image
review; their scoped appearance check uses actual calibrated GPU readback.

Fresh native Windows execution of the merged source passes the ABI, detour and
four domain groups, including durable actor identity remapping and exact
continuation. The same six groups pass on Linux and with Linux ASan/UBSan.
The Windows installed-assembly suite passes 131 groups; four dependent Python
suites pass 119 tests without skips. The adapter has only its two existing
MSB3277 reference conflicts. Private evidence is
`local/windows-actor-persistence-merge-r1`, terminal review SHA-256
`b0fa5ccfa04a48b97ff568476392d5b7ef893ea9636c14e413534369c818dd5e`.
The attempted Windows Unity launch was blocked by Steam reporting another
computer already playing ONI. The runner restored the original profile and
generated-world files byte for byte. No Unity actor result is claimed.

After merging the zero-time emitter configuration repair, the Windows suite
passes 132 groups and the adapter builds with the same two reference conflicts.
All 246 tracked native inputs and the three previously verified Windows native
libraries remain unchanged. The six PowerShell option checks pass again on the
merged runner. Private evidence is `local/windows-actor-emitter-merge-r2`,
terminal SHA-256
`6805173ff923a9b4a8217d7e5c11c60389a71baf2a5f34528aa456fcd1fc17d9`.
This is declaration/build verification; Unity save/reload remains required.

The private `--verify-persistent-actors` source fixture uses three real,
saveable prefab objects: a consumer, an emitter and a blocked emitter. It
requires `native-domain` or `native-liquid-domain`, `--save-colony` and at least
80 frames. A separate natural-day control combines `--verify-persistent-actors`
with `--verify-natural-autosave --frames 3302`, using `native-domain` and no
manual source save. It retains the actual day scheduler and independently
replays every subsequent solver/actor interval before comparing the final
complete image. Its current actor report identifies the final image separately
from the earlier saved image; local oxygen exhaustion is a valid selective
consumer result. The fixed short control retains its positive saved-remainder
challenge. Natural-day execution with live actors is still under validation.
`native-load-domain --resave-before-resume --reload-in-process`
then exercises paused autosave and two actual managed scene loads.
On Windows, `tools/run_colony_validation.ps1 -VerifyPersistentActors` exposes
the same source check: use `-NativeDomain gas` or `water`, `-SaveColony` and
`-Frames 80` or more. The runner requests the strict persistent-actor evidence
review explicitly. Load the resulting save with `-NativeDomain load`,
`-LoadSave`, `-ResaveBeforeResume` and `-ReloadInProcess`; the load reviewer
also requires restored actor evidence whenever the save contains actors.
Six actual PowerShell invocations verify rejection of missing/wrong source
modes, a short run and a missing save request, and admission of both gas/water
options through to package validation. None creates a staging directory or
launches the game. Private review:
`local/windows-actor-runner-options-r1/REVIEW.json`, SHA-256
`f89a3f223ca282e733760a6b37d6b29f95b590454b24da7d8c4d777725d932da`.

This remains bounded point-boundary persistence with drained callbacks.
Crash recovery of pending outboxes, object destruction during callback delivery,
moving actors across owners, arbitrary prefab hierarchies and complete process
reservations remain open. They must not be inferred from a successful static
actor round trip. The [completion plan](completion-plan.md) retains the broader
playable-colony gates.
