# Native orbital reference admission

The fluid owner retains ONI's managed worlds and nonfluid objects. A successful
fluid checkpoint alone cannot establish that their saved references are intact.
Native orbital references therefore have a separate admission check before a
restored domain advances and before a paired colony save starts serialization.

The adapter observes each native world's `OrbitalMechanics` reference list,
saved object ID and cached target directly. It does not call `Ref.Get()` or
`GetId()`: those native methods can clear a missing reference. Each populated
reference must identify a tracked `OrbitalBG` in the correct world, with one
initialized, spawned `SaveLoadRoot` registration. Worlds and reference slots
must be unique. Empty optional references and newly created cached references
retain their ordinary native behavior.

Restored physics stays at frame zero until this check passes. A missing or
mismatched target faults the bounded load while keeping fluid exclusion and
the pause in place. A save with invalid references is declined before opening
its destination, using the existing save-problem path. The adapter does not
delete the reference, invent a replacement object or assign a different ID.
An already damaged development save needs an intact source save; this check
does not reconstruct lost native objects.

`NativeOrbitalReferences` records the observed world/object identities, save
registrations, stage, inspection count, error and the exact frame/time of
restored admission. It explicitly reports zero rewritten references and leaves
general managed-reference integrity unverified. The portable save/load reviewer
requires this evidence; earlier frozen reports do not establish this new check.
Session shutdown clears the observations and admission state before another
colony loads.

## Finishing the existing background save root

A later fresh WindowsPlayer water source reached physical frame 122 with all
three actors and exact consumer callbacks, then correctly declined its first
paired save: orbital ID 2 was tracked and initialized, but its save root had
never spawned or registered. This is a fresh-colony lifecycle problem as well
as an input-admission concern.

The installed Windows/Linux bodies show that `OrbitalObject.OnSpawn` deactivates
its own object. A read-only Windows lifecycle trace observes that deactivation
while the later `SaveLoadRoot` remains unspawned. In that traced control,
rendering reactivates the object roughly 0.7 seconds later and the root then
registers. The failed source retains an unspawned root instead. This evidence
supports completing the root before the orbital component can hide the object;
it does not establish the cause of every historical damaged save.

In native-domain modes, the adapter now calls the existing root's normal
`Spawn()` before `OrbitalObject.OnSpawn`. It requires the original tracked
`OrbitalBG` identity, an initialized root, an available native save manager and
zero prior registrations. Completion must leave exactly one registration.
An already spawned root keeps its ordinary lifecycle. The native orbital body
still controls visibility and rendering. No object or reference is created or
reassigned, and an already missing saved object still fails admission.
`NativeOrbitalReferences.SaveRootLifecycle` records the completed root IDs and
any error, and resets between sessions. The portable reviewer rejects errors,
duplicate/invalid root IDs and any created object or rewritten reference in
this receipt; older audits without it do not establish the new spawn ordering.
The default observer modes do not install this lifecycle patch.

Frozen `9a1a894` and native v13 now pass a fresh nine-world WindowsPlayer/Proton
OpenGL water source. Orbital ID 2 completes its original save root once before
native deactivation; the later Unity root start remains idempotent. At physical
frame 121 all three point actors save, with 114 matching consumer callbacks
carrying 1.1400000331923366 kg. The complete 10,593,603-byte body and header pass
readback and retain the serialized background. The physical image is unchanged
from the successful unpatched lifecycle-observation control. Independent source
review SHA-256:
`eaf8705bbb79ab17d9858d35fd4480e0482048bf206b3496e7d5743a084a2d91`.

Both WindowsPlayer and native Linux then load that same intact source twice.
Every session admits the original references at physical frame zero, completes
its paused autosave/readback, binds the three actors and compares 120 complete
frames exactly against its local continuation. An additional load of the older
missing-object input still rejects at frame zero, without rewriting its raw
reference or producing a save. The combined saved-receipt review for the water
loads and later precision/day controls passes 88 checks, SHA-256
`9b378dc21153abfa82994b79c0d94995e0860c2cfcdc9e882cff86597d5fba95`.
The local exact-continuation result does not accept the separate cross-platform
water numerical comparison; see [arithmetic controls](native-floating-point-environment.md).

## Waiting for the restored native Start lifecycle

The first `51d5ce9`/v15 mixed gas/liquid WindowsPlayer source saves correctly,
and its first load completes a paused autosave and 120 exact physical frames.
The second load in the same process reaches the admission check before Unity
has called the native save roots' `Start`: all nine world roots and the existing
orbital object are tracked and initialized, with `RootSpawned=false` and zero
registrations. The strict check correctly refuses advancement, but treating
this transient lifecycle state as a damaged save makes ordinary reloads race.
The failed second-session result is retained; it does not invalidate the
completed first session or accept the failed two-session control.

Restored admission now waits only for that exact initialized/unspawned/zero-
registration combination. It inspects every world and reference before waiting,
so a missing target, changed identity, duplicate registration or inconsistent
root state still rejects immediately. Ordinary save admission remains strict.
The existing restored pause keeps physical frame zero while Unity completes
its normal lifecycle; the adapter does not spawn or register world roots itself.
Every waiting observation and the final admission compare the entire native
physical checkpoint byte for byte with the restored source and require its
original physical time. A bounded lifecycle timeout starts at the first actual
waiting observation, after loading and shader warmup.

`RestoreLifecycleHold` records waiting inspections, current/maximum pending
root counts and the checked frame/time/full-image hash. Session cleanup clears
it. The portable reviewer rejects an outstanding wait or a claimed witness
without the original restored image. Four new declaration groups distinguish
legitimate native startup from dangling references and inconsistent registry
states. Frozen `2df8e7b` with the unchanged v15 native libraries now loads the
same mixed-state source twice in WindowsPlayer/Proton and twice in native Linux.
Both second sessions naturally wait once for all ten roots while preserving the
complete source image and physical time at frame zero. All four sessions finish
paused autosaves/readbacks and compare 120 complete physical frames exactly;
the Windows final image also equals the earlier successful pre-wait session.
The combined saved-receipt review passes 36 checks, SHA-256
`aaed03f27ffa1f2ac611e462d8227d2adcba295532db0178475c9c44fa103846`.

## Reproduced input and current verification

An older nine-world input with SHA-256
`988207f97176d668fd138f9d7a44526bd87b9efe6598431db448c9a6abebbb04`
previously logged `Missing KPrefabID reference: 2` during resaving. A read-only
serialization observer identified asteroid instance 1's `Ref<OrbitalObject>` as
the owner. Its complete inflated body has 10,601,770 bytes, SHA-256
`89c4e7dba5b36814e65d31204270de80e7b33ad53217e80de59b76a5106d1618`,
and no serialized `OrbitalBG` entry. The warning is not evidence of a fluid
continuation mismatch; it exposes a separate missing native object.

Later read-only native and fluid-owned source observations find orbital
instance 2 tracked and its save root registered. The newer Linux gas, water
and natural-day actor saves contain the background entry. These observations
do not establish why the older source omitted it. The later lifecycle repair
above follows a separately reproduced fresh-source failure and native spawn
ordering; it does not reconstruct the older missing object.

The pure admission controls cover missing targets, changed IDs, wrong worlds,
duplicate slots/worlds, uninitialized or duplicate save registration, valid
empty references and unchanged observations after rejection. Both installed
assembly contracts are checked directly. Both platform builds and all 138
declaration/admission groups per platform pass; 225 Python tests pass with four
optional/platform skips.

Frozen `74c9ec1` and native v12 pass the actual Linux controls. The intact
live-actor day autosave (`929f2f49a3cc1c26f6567d045e3a27371f8f6e325f9fae3e2a780cd7d9fc935d`)
loads twice in one Unity process. Both native sessions admit orbital ID 2 in
world 0 before positive time, finish paused autosaves and compare 120 complete
actor-and-fluid frames exactly. A separate fresh load with no requested
initial resave also admits at frame zero and matches 120 frames. All three
final images remain
`6a3809d5bfc9e93fd997729ade7657c77a1027ef2341e4b96ae2872b5062251e`.
The live actor callbacks, restored readers, calibrated GPU readback, original
input hashes and private desktop cleanup pass.

The older nine-world input now rejects before restored advancement. All nine
world save roots are registered, but owner instance 1 still refers to missing
ID 2. The observed raw ID remains 2; physical frame stays zero and physical
time stays 24.400000363588333 seconds. There are zero save admissions, sim-save
writes or exact continuation frames, and no resaved file is created. The input
is unchanged. This is an expected rejection of an already missing object,
not recovery of that object or acceptance of this input's native integrity.

Independent review SHA-256:
`a88f430fe049c676105f8ed17c7fce3dfcac68952674913d2d036faa0dc821e8`.
Raw results for the two-load control, missing-object rejection and ordinary
fresh load respectively are
`fbebe7912a00fa0501da5789fe9de874f63e36c9ab6d88970fccc6f45e49d05d`,
`9de158709acae102e84b72810c9ea67728679712bda14df20c2c990f9a074020`
and `d93f855c3ff035c9550fc0d884be196a2cb11f50d2fd8ad26eb6c822558362a8`.
The later fresh nine-world WindowsPlayer/Proton OpenGL gas source and its two
reload sessions also admit the native references. Native Linux reloads the same
Windows source twice with admission at frame zero. See
[the platform evidence](native-actor-persistence.md#windowsplayer-and-native-linux-spaced-out-gas).
These controls do not establish execution on the Windows OS or general managed
reference integrity.

Related contracts: [native integration](native-backend-integration.md),
[point-actor persistence](native-actor-persistence.md) and
[the remaining completion gates](completion-plan.md).
