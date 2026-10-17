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
Windows water-actor and Windows OS execution remain under validation; the later
WindowsPlayer gas cohort is accepted below. These Linux actor cohorts
have no separate manual image review; their scoped appearance check uses actual
calibrated GPU readback.

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

After the natural-autosave runner changes and pressure-work refactor, fresh
Windows source `e181a18` passes 132 installed-assembly groups, 120 Python runner
tests without skips and 12 actual PowerShell option checks. Its rebuilt native
libraries pass the ABI, detour and four domain groups. Separate licensed
SimDLL worker controls pass gas and water checkpoint restores, each with 12
exact continuation frames. These isolated controls use the default backend
and do not execute Unity or prove Windows actor save/reload. The managed/runner
terminal is `546ad352dfb7196e0ced460389da673905487d9cf9e791565bfb916dcf094ce2`;
the licensed-worker terminal is
`4089728c3182940eaee2eef8e5247e78df605fb1144ee27ae1d6fec653f936cf`.

The subsequent orbital-reference admission merge (`4e7cf03`) passes 138
installed-game declaration groups and 125 Python runner tests without skips.
The adapter builds with the same two reference conflicts. All 170 native
source/build inputs match the preceding verified binaries; no native rerun is
claimed for this managed-only merge. Private evidence is
`local/windows-orbital-merge-r6`, terminal SHA-256
`f7a59d3fe43431b00628380599aa1403c2cdde07569b1c136e3868cf9571965e`.
Its fresh Windows hover launch again encounters Steam's other-computer session
warning and is canceled. This supplies no new Windows Unity or pixel result.

## WindowsPlayer and native Linux Spaced Out gas

The later `74c9ec1` mod and native v12 pass real WindowsPlayer gas actor saving
and both reload sessions under official Proton 10/OpenGL on Linux. The public
runner is frozen `4255c60`. Its fresh Spaced Out cluster has nine retained
native worlds and six owned gas cells. At physical frame 122/time
24.400000363588333 seconds, all three actors save with timing/remainder and
blocked state; 114 matching consumer callbacks carry 1.1400000331923366 kg.
The complete 10,680,044-byte body/header pass readback, and pause depth returns
from zero to zero. The input file SHA-256 is
`9b1c6cad9a1abb482d64514ad7f14e723c7a749326aa6887301cc70cf6483bf4`;
its complete physical image is
`5ef75748419ceba04a1d25cddfad3f4785ebe9c6f46dbcd6d743b99bd7d2003b`.

Both WindowsPlayer native sessions, separated by shutdown/menu in one process,
bind all three real actors, flush two genuine zero-time emitter configurations,
admit all nine world save roots and orbital object 2 before positive time, and
complete paused frame-zero autosaves/readback. Each receives 120 matching
consumer callbacks and compares all 120 complete actor-and-fluid frames exactly.
Their final image is
`5413689412358fd63f7ce1002688e8c8ec912f6f3116c03a65442ede3243e11c`.
Both resaved bodies contain 10,680,222 bytes. Restored readers, calibrated GPU
readback and original-input preservation pass.

The installed native Linux player then loads that same unchanged Windows save.
Its two native sessions also restore the exact initial image, preserve the
native references, complete the paused autosaves and compare 120 complete
frames individually with matching callbacks/readers/GPU checks. Both Linux
final images equal
`a3c40c74784258a5bd73f68566dea143183cf80c00b7ffec539aa87658a5e436`.
The private displays are retired.

The Windows/Linux final images differ numerically. An independent complete
checkpoint comparison retains exact actor records, material/registry identity,
geometry, settings, walls, physical time/revision and passive identities. It
compares all 119 stored physical quantities, including every constituent mass
and both face-momentum representations. Thirty-one differ; the largest
relative difference is 6.731267162903596e-12, below the explicit 1e-8 comparison
bound. Zeros retain an exact comparison and traces receive no absolute-mass
floor. This accepts bounded cross-platform gas continuation, while the
same-platform controls retain their individually exact continuation checks.

Independent review SHA-256 values: Windows gas
`d935e842a426a0f7e4d4631a6a9d15a5da7aad7a9639e597000c4d15fac5c1e8`,
native Linux cross-load
`1e8870a0556a09201066be16a4556dc1afc68ebdb8c16c4575f17263fb2583b5`,
complete checkpoint comparison
`b3040d956c288a293562819c8d9516332017a347a31fabca3139105b78c19853`.
The actual WindowsPlayer source image was also viewed; its mixture hover facts
match completed cell 103106/revision 122. That scoped image review is
`bf343e82ec8c365e217497c80f32bb620bf9f1a0e09d37800a6c6497fbe4fcaf`.
It does not establish four-gas/trace-cutoff presentation or load-image review.
These controls do not establish execution on the Windows OS, arbitrary native
references, production devices or whole-colony fluid ownership.

## Natural day autosaves with live actors

Frozen `6c0f9a8` and native compatibility build v12 pass the actual Linux day
scheduler and subsequent actor continuation. The source uses no manual save
request: `GameClock.DoAutoSave` requests cycle one, and the ordinary autosave
finishes at native game time 602.0255126953125 seconds. It captures all three
live actors at physical frame 2,731/time 546.2000081390142 seconds. The complete
5,151,577-byte body and current header pass readback, and pause depth returns
from zero to zero. The unchanged autosave file SHA-256 is
`929f2f49a3cc1c26f6567d045e3a27371f8f6e325f9fae3e2a780cd7d9fc935d`.

The source continues to physical frame 3,304 with all three actors live and
3,296 matching consumer callbacks carrying exactly 30.362300756853074 kg.
Replaying all 573 intervals after the save independently produces the same
final complete actor-and-fluid image:
`87a2382d917a783d0a76c696088c989bb4138513c87c338377e8191510934e7b`.
This compares the final source image after replaying those intervals; it does
not claim an individual source comparison at every intermediate frame.

A fresh Unity process then loads that actual autosave. Native sessions one and
two, separated by shutdown and the main menu in the same process, each bind all
three actors, flush two genuine zero-time emitter configurations, finish the
paused frame-zero autosave/readback and compare all 120 resumed complete
frames individually. Each receives 120 matching consumer callbacks. Both final
images equal
`6a3809d5bfc9e93fd997729ade7657c77a1027ef2341e4b96ae2872b5062251e`.
Restored readers, calibrated GPU readback, input preservation and private
desktop cleanup also pass. Both resaved bodies contain 5,151,764 bytes.

Independent review SHA-256:
`3df81c88b6a111796fe27fa9de678d62f0c0a522cff02bfc845831c2a0f1e24c`.
Raw source/load result SHA-256 values are
`77dd0e865266c9520439ad5b0a70d52d5e44adbe602fe59d986a581e2547de24`
and `438e8ea6d019026d4117d85b5c795b03160f84f1299497acd89f500c0eb717eb`.
This accepts the bounded Linux live-actor day autosave and reload; it does not
establish Windows execution or whole-colony ownership. It has no separate
manual pixel review.

## Running the control

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
challenge. The actual Linux natural-day execution and reload are accepted above.
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
