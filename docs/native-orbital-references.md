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
do not establish why the older source omitted it, and no spawn-order repair is
shipped on that basis.

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
