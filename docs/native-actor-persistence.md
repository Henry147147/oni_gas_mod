# Saving registered native fluid actors

Actor persistence is part of the native backend's save contract. A saved
consumer or emitter needs its remaining timing and a stable game object
identity as well as the fluid state. Native handles and callback indices belong
to one running session; they are not persistent object identities.

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
platforms' managed declaration suites pass 131 groups, and 220 Python checks
pass with four optional/platform skips. Real Unity source/save/reload controls
are in progress; this page does not yet record an accepted Unity actor save.

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

The private `--verify-persistent-actors` source fixture uses three real,
saveable prefab objects: a consumer, an emitter and a blocked emitter. It
requires `native-domain` or `native-liquid-domain`, `--save-colony` and at least
80 frames. `native-load-domain --resave-before-resume --reload-in-process`
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
