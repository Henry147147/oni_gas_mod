# Durable ONI point-actor checkpoint extension

This extends the physical `MACCLS01` image for the bounded native ONI owner
with durable point actors. Capability `ma_oni_domain_actor_checkpoint_abi_version()`
returns 1. Supported scope, game binding and verification are in
[durable point actors](../docs/native-domain-persistence.md#durable-point-actors).

Actor-free snapshots keep the existing physical bytes. With persistent actors,
`ma_oni_domain_snapshot_copy` returns a `MAACTR01` wrapper containing the same
solver image and the actors' complete operational state. The paired worker
capture uses `ma_oni_domain_pair_snapshot_request` and the existing
poll/copy/release token lifecycle. The physical-only export still rejects live
actors.

All integers are little-endian. Doubles keep their binary64 bits. No native
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

For a configured (scenario 7) owner, the enclosed solver section is the
checksummed `MADCFG01` wrapper, which carries the exact geometry, physics and
solid mask around the `MACCLS01` image; see the
[configured domain](../docs/native-configured-domain.md). Restore compares the
actor header geometry and that wrapper with the caller's configuration.
Legacy images are unchanged.

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

## Validation

Records have a unique, strictly increasing `(kind, owner, prefab, component)`
key. The native codec permits positive uint64 owner IDs; the ONI adapter uses
`KPrefabID.InstanceID + 1`, bounded to `1 .. 2^31`, a nonzero prefab-tag hash
and component indices `0 .. 65535`. The hash is not a native allocation handle.
No callback index, handle, session ID, frame ID or thread ID enters a record.

All numbers must be finite; mass, elapsed time and remainder are nonnegative.
Consumers have zero elapsed time and no emitter blocked state. Emitters have
zero consumer remainder. Enabled emitters need positive interval and density,
a temperature of at least 1 K and an exactly binary32-representable parcel
mass. Native admission also checks cell ownership, phase, complete geometry,
clock, solver checksum and material/EOS identity. The whole wrapper is bounded
to 256 MiB. The enclosing `MAONID01` section uses payload version 2 for this
extension; its actor-free variant stays version 1.

## Restore

Restored records stay pending by stable identity. The game's newly allocated
registrations supply current native handles through atomic
`ma_oni_domain_configure_persistent_actors` batches of 96-byte
`MAOniDomainPersistentActor` commands. A matching configuration restores
cadence, remainder and blocked state without transferring material. A
zero-rate spawn bootstrap cannot overwrite a pending record. Identity or
configuration mismatches and duplicate bindings reject the whole batch.
Positive worker advancement with pending records faults while keeping native
fluid exclusion.

`ma_oni_domain_snapshot_advance` independently restores and advances solver
and actor state. Its transfer ordering uses the stable keys, so allocator
changes cannot change the result; native controls reverse registration and
handle order and match ten complete resumed images for gas, water and water
with gravity. The extension admits drained point boundaries only. It does not
serialize undelivered callback outboxes or implement complete machine
reservations.
