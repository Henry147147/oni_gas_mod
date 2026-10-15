# Idle reservoir checkpoint contract checks

This original pure-managed test host links `OwnedReservoirCheckpoint.cs` and
Core only. It requires no licensed game assemblies, native library or running
game. It installs no save/load hooks and never permits a native save.

```powershell
& C:/Users/henry/.dotnet/sdk10/dotnet.exe build tools/OwnedReservoirCheckpointTests -c Release --artifacts-path local/reservoir-checkpoint-build
& C:/Users/henry/.dotnet/sdk10/dotnet.exe local/reservoir-checkpoint-build/bin/OwnedReservoirCheckpointTests/release/OwnedReservoirCheckpointTests.dll
```

`ColonyFluidState.ReservePublication(expected)` separately provides an exclusive
read/save lease for the exact immutable publication, without a new transaction,
revision or companion. `Source` and `Snapshot` retain that exact source;
`EnsureCurrent` checks active lease identity; `SaveCheckpoint` serializes that
source and checks the lease both before and after serialization. Reads and
optimistic staging remain possible, while commit and writer reservation fail.
Disposal is idempotent and cannot release a successor lease. No external
delegate runs under its Core publication lock. An opaque companion's resource
lifetime remains its caller's responsibility; this lease does not prevent that
caller disposing native memory independently. Reservation tests are in the
existing `MixedAtmospheres.Tests` host.

The reservoir codec is a bounded data contract for exactly two declared native
owners, including an empty owned allocation. It retains exact canonical Core
v3 bytes and every empty unbound container. Every nonempty container, including
trace mass or massless metadata, must have one exact owner. World-cell mass and
metadata are rejected. Full container handles, native colony identity, pinned
build and two managed contract hashes, catalog identity, declared volume and
gas admission, native instance IDs, persistent owner tokens and construction
values are validated. Native instance ID zero is valid. Construction mass is
stored as an exact promoted native float. Capacity is an independent 1000 kg
gameplay limit, never an inferred volume or pressure limit.

The binary layout is original: `MARESVR1`, int32 format version, int64 complete
encoded length, build and two SHA256 contract identities, native colony GUID,
bounded UTF-8 catalog version and SHA256 fingerprint, explicit idle protocol
version/zero flags and counts, int64 length plus canonical Core v3 bytes,
int32 owner count and two records sorted by saved native instance ID, followed
by SHA256 over all preceding bytes. All values use BinaryWriter little-endian
primitive encoding; GUIDs use .NET `Guid.ToByteArray`. Strings use explicit
int32 UTF-8 byte lengths, not BinaryWriter's seven-bit string prefix. The
maximum complete encoded size is 640 MiB. Unknown versions, trailing bytes,
bad lengths, corruption, identity conflicts and nonidle data fail closed.

`IdleState` is a caller-supplied declaration, not proof of live actor/native
eligibility. The codec cannot inspect actual prefab instances, packet lists,
ports, footprint overlap, pending external callbacks or save coroutines. Native
construction element identity is retained as an opaque declared hash; the
future live adapter must validate the actual construction component. Saving
the codec alone is not integrated ONI persistence. Actual scoped serialization,
atomic file promotion, quarantined restore, all-owner activation, callback
outcomes, ordinary saves/autosaves and crash recovery remain separate work.
