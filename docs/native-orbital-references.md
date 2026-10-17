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
assembly contracts are checked directly. Actual Unity admission/rejection
controls for the new guard are being run; their result remains separate from
the earlier read-only observations and fluid continuation evidence.

Related contracts: [native integration](native-backend-integration.md),
[point-actor persistence](native-actor-persistence.md) and
[the remaining completion gates](completion-plan.md).
