# Durable ONI point-actor checkpoint extension

This supplements the physical `MACCLS01` image for the bounded native ONI
owner. Capability `ma_oni_domain_actor_checkpoint_abi_version()` returns 1.
The [game integration and actual acceptance](../docs/native-actor-persistence.md)
describe its supported scope.

Actor-free snapshots retain the existing physical bytes. With persistent
actors, `ma_oni_domain_snapshot_copy` returns a `MAACTR01` wrapper containing
the same solver image and the actors' complete operational state. The paired
worker capture uses `ma_oni_domain_pair_snapshot_request` and the existing
poll/copy/release token lifecycle. The original physical-only export continues
to reject live actors.

All integers are little-endian. Doubles retain their binary64 bits. No native
structure padding is serialized.

| Header offset | Bytes | Value |
| --- | ---: | --- |
| 0 | 8 | ASCII `MAACTR01` |
| 8 | 4 | Format version 1 |
| 12 | 4 | Actor count, 1 through 256 |
| 16 | 40 | Ten `MAOniDomainConfig` uint32 fields, in declaration order |
| 56 | 4 | Enclosed solver image byte count |
| 60 | 4 | Reserved zero |
| 64 | 88 × count | Actor records |
| Following records | Solver byte count | Complete `MACCLS01` image |
| Final 64 bytes | 64 | Lowercase ASCII SHA-256 of all preceding bytes |

Each actor record is:

| Record offset | Bytes | Value |
| --- | ---: | --- |
| 0 | 8 | Stable owner ID |
| 8 | 4 | Prefab fingerprint |
| 12 | 4 | Same-kind component index |
| 16 | 4 | Kind: 1 consumer, 2 emitter |
| 20 | 4 | Public, unpadded game cell index |
| 24 | 4 | Domain species index |
| 28 | 4 | Reserved zero |
| 32 | 8 | Consumer kg/s or emitter kg/interval |
| 40 | 8 | Emitter interval seconds |
| 48 | 8 | Emission temperature kelvin |
| 56 | 8 | Maximum total fluid density kg/m³ |
| 64 | 8 | Emitter elapsed seconds |
| 72 | 8 | Consumer binary32 reporting remainder kg |
| 80 | 4 | Blocked flag, 0 or 1 |
| 84 | 4 | Reserved zero |

Records have a unique, strictly increasing `(kind, owner, prefab, component)`
key. The native codec permits positive uint64 owner IDs; the ONI adapter uses
`KPrefabID.InstanceID + 1`, bounded to `1 .. 2^31`, a nonzero prefab-tag hash and
component indices `0 .. 65535`. The hash is not a native allocation handle.
No callback index, handle, session ID, frame ID or thread ID enters a record.

All numbers must be finite; mass, elapsed and remainder are nonnegative.
Consumers have zero elapsed time and no emitter blocked state. Emitters have
zero consumer remainder. Enabled emitters require positive interval/density,
temperature at least 1 K and exactly binary32-representable parcel mass. Native
admission also checks cell ownership, phase, complete geometry, clock, solver
checksum and material/EOS identity. The total wrapper is bounded to 256 MiB.
The enclosing `MAONID01` section uses payload version 2 for this extension;
its actor-free variant remains version 1.

Restoration retains records pending by stable identity. The real game's newly
allocated registrations supply current native handles through atomic
`ma_oni_domain_configure_persistent_actors` batches of 96-byte commands. A
matching configuration restores cadence, remainder and blocked state without
transferring material. A zero-rate spawn bootstrap cannot overwrite a pending
record. Identity/configuration mismatches and duplicate bindings reject the
whole batch. Positive worker advancement with pending records faults while
retaining native fluid exclusion.

`ma_oni_domain_snapshot_advance` independently restores and advances solver and
actor state. Its transfer ordering uses stable keys, so allocator changes
cannot change the result. Native controls reverse registration and handle
order and match ten complete resumed images for gas, water and water with
gravity. This extension admits drained point boundaries; it does not serialize
undelivered callback outboxes or implement complete machine reservations.
