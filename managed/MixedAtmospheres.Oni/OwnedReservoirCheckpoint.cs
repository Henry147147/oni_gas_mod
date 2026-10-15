using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using MixedAtmospheres.Core;

namespace MixedAtmospheres.Oni
{
    // Original bounded data contract only. No native save/load hooks, ownership
    // adoption, live eligibility attestation or callback crash recovery.
    internal sealed class OwnedReservoirCheckpointCompatibility
    {
        internal int OniBuild { get; }
        internal string AssemblyCSharpSha256 { get; }
        internal string AssemblyCSharpFirstpassSha256 { get; }
        internal OwnedReservoirCheckpointCompatibility(int oniBuild, string assemblyCSharpSha256, string assemblyCSharpFirstpassSha256)
        {
            if (oniBuild <= 0) throw new ArgumentOutOfRangeException(nameof(oniBuild));
            OniBuild = oniBuild;
            AssemblyCSharpSha256 = OwnedReservoirCheckpointV1.ValidateHash(assemblyCSharpSha256);
            AssemblyCSharpFirstpassSha256 = OwnedReservoirCheckpointV1.ValidateHash(assemblyCSharpFirstpassSha256);
        }
    }

    // A caller-supplied declaration to validate and persist, not evidence that
    // native owners or an actor session actually meet these requirements.
    internal sealed class OwnedReservoirCheckpointIdleState
    {
        internal int PendingBatchCount { get; }
        internal int OutboxCount { get; }
        internal int SnapshotReaderCount { get; }
        internal int OrphanCount { get; }
        internal bool Preparing { get; }
        internal bool Notifying { get; }
        internal bool Faulted { get; }
        internal bool WorldRecoveryRequired { get; }
        internal bool ConsumptionAcknowledged { get; }
        internal OwnedReservoirCheckpointIdleState(int pendingBatchCount = 0, int outboxCount = 0, int snapshotReaderCount = 0,
            int orphanCount = 0, bool preparing = false, bool notifying = false, bool faulted = false,
            bool worldRecoveryRequired = false, bool consumptionAcknowledged = true)
        {
            if (pendingBatchCount < 0 || outboxCount < 0 || snapshotReaderCount < 0 || orphanCount < 0)
                throw new ArgumentOutOfRangeException(nameof(pendingBatchCount));
            PendingBatchCount = pendingBatchCount; OutboxCount = outboxCount; SnapshotReaderCount = snapshotReaderCount;
            OrphanCount = orphanCount; Preparing = preparing; Notifying = notifying; Faulted = faulted;
            WorldRecoveryRequired = worldRecoveryRequired; ConsumptionAcknowledged = consumptionAcknowledged;
        }
        internal void RequireIdle()
        {
            if (PendingBatchCount != 0 || OutboxCount != 0 || SnapshotReaderCount != 0 || OrphanCount != 0 ||
                Preparing || Notifying || Faulted || WorldRecoveryRequired || !ConsumptionAcknowledged)
                throw new InvalidDataException("Reservoir checkpoint v1 supports only declared idle, acknowledged, zero-outbox state.");
        }
    }

    internal sealed class OwnedReservoirCheckpointOwner
    {
        internal int SavedInstanceId { get; }
        internal string PrefabId { get; }
        internal Guid PersistentOwnerToken { get; }
        internal FluidContainerHandle Container { get; }
        internal int WorldId { get; }
        internal int AnchorCell { get; }
        internal int Orientation { get; }
        internal int ConstructionElementId { get; }
        // Exact promoted native float, never rounded through text or narrowed
        // from an arbitrary higher precision mass during save/restore.
        internal double ConstructionMassKg { get; }
        internal double VolumeCubicMetres { get; }
        internal double MaximumMassKg { get; }
        internal FluidContainerPhases AllowedPhases { get; }
        internal OwnedReservoirCheckpointOwner(int savedInstanceId, string prefabId, Guid persistentOwnerToken,
            FluidContainerHandle container, int worldId, int anchorCell, int orientation, int constructionElementId,
            double constructionMassKg, double volumeCubicMetres, double maximumMassKg = 1000,
            FluidContainerPhases allowedPhases = FluidContainerPhases.Gas)
        {
            if (savedInstanceId < 0) throw new ArgumentOutOfRangeException(nameof(savedInstanceId));
            if (prefabId != "GasReservoir") throw new ArgumentException("Only the explicit GasReservoir prefab is supported.", nameof(prefabId));
            if (persistentOwnerToken == Guid.Empty || container.ColonyId == Guid.Empty || container.AllocationToken == Guid.Empty || container.Generation <= 0)
                throw new ArgumentException("Complete nonempty native owner and Core allocation identities are required.");
            OwnedReservoirCheckpointV1.ValidateIdentity(container.Id);
            if (worldId < 0 || anchorCell < 0 || (orientation != 0 && orientation != 1 && orientation != 2 && orientation != 3 && orientation != 5 && orientation != 6))
                throw new ArgumentException("Invalid world anchor or pinned ONI orientation value.");
            if (double.IsNaN(constructionMassKg) || double.IsInfinity(constructionMassKg) || constructionMassKg < 0 ||
                (double)(float)constructionMassKg != constructionMassKg)
                throw new ArgumentException("Construction mass must be an exact finite nonnegative promoted native float.");
            if (double.IsNaN(volumeCubicMetres) || double.IsInfinity(volumeCubicMetres) || volumeCubicMetres <= 0 ||
                maximumMassKg != 1000 || allowedPhases != FluidContainerPhases.Gas)
                throw new ArgumentException("Explicit positive volume, 1000 kg gameplay capacity and gas-only admission are required.");
            SavedInstanceId = savedInstanceId; PrefabId = prefabId; PersistentOwnerToken = persistentOwnerToken; Container = container;
            WorldId = worldId; AnchorCell = anchorCell; Orientation = orientation; ConstructionElementId = constructionElementId;
            ConstructionMassKg = constructionMassKg; VolumeCubicMetres = volumeCubicMetres;
            MaximumMassKg = maximumMassKg; AllowedPhases = allowedPhases;
        }
    }

    internal sealed class OwnedReservoirCheckpointV1
    {
        private static readonly byte[] Magic = Encoding.ASCII.GetBytes("MARESVR1");
        private static readonly Encoding StrictUtf8 = new UTF8Encoding(false, true);
        internal const int FormatVersion = 1;
        internal const int MaximumPayloadBytes = 640 * 1024 * 1024;
        private const int ChecksumBytes = 32;
        private readonly byte[] encoded, coreData;
        private readonly FluidMaterialCatalog catalog;
        internal OwnedReservoirCheckpointCompatibility Compatibility { get; }
        internal Guid NativeColonyId { get; }
        internal ColonyFrameSnapshot Source { get; }
        internal OwnedReservoirCheckpointIdleState IdleState { get; }
        internal IReadOnlyList<OwnedReservoirCheckpointOwner> Owners { get; }
        internal ReadOnlySpan<byte> CoreData => coreData;
        internal int EncodedLength => encoded.Length;

        private OwnedReservoirCheckpointV1(byte[] encoded, byte[] coreData, FluidMaterialCatalog catalog,
            OwnedReservoirCheckpointCompatibility compatibility, Guid nativeColonyId, ColonyFrameSnapshot source,
            OwnedReservoirCheckpointIdleState idleState, OwnedReservoirCheckpointOwner[] owners)
        {
            this.encoded = encoded; this.coreData = coreData; this.catalog = catalog; Compatibility = compatibility;
            NativeColonyId = nativeColonyId; Source = source; IdleState = idleState;
            Owners = new ReadOnlyCollection<OwnedReservoirCheckpointOwner>(owners);
        }

        internal static OwnedReservoirCheckpointV1 Create(ReadOnlySpan<byte> coreData, FluidMaterialCatalog catalog,
            OwnedReservoirCheckpointCompatibility compatibility, Guid nativeColonyId,
            IEnumerable<OwnedReservoirCheckpointOwner> owners, OwnedReservoirCheckpointIdleState idleState)
        {
            if (catalog == null) throw new ArgumentNullException(nameof(catalog));
            if (compatibility == null) throw new ArgumentNullException(nameof(compatibility));
            if (owners == null) throw new ArgumentNullException(nameof(owners));
            if (idleState == null) throw new ArgumentNullException(nameof(idleState));
            if (nativeColonyId == Guid.Empty) throw new ArgumentException("A native colony identity is required.", nameof(nativeColonyId));
            idleState.RequireIdle();
            if (coreData.Length < 80 || coreData.Length > MaximumPayloadBytes - 512) throw new InvalidDataException("Invalid Core checkpoint length.");
            // Take ownership before invoking a caller-controlled enumerator.
            var core = coreData.ToArray(); var source = RestoreCore(core, catalog);
            var list = new List<OwnedReservoirCheckpointOwner>(2);
            foreach (var owner in owners)
            {
                if (owner == null || list.Count == 2) throw new InvalidDataException("Exactly two nonnull reservoir owners are required.");
                list.Add(owner);
            }
            if (list.Count != 2) throw new InvalidDataException("Exactly two reservoir owners are required.");
            var rows = list.ToArray(); Array.Sort(rows, (a, b) => a.SavedInstanceId.CompareTo(b.SavedInstanceId));
            ValidateDomain(source, rows);
            var data = Encode(core, source, compatibility, nativeColonyId, rows);
            return new OwnedReservoirCheckpointV1(data, core, catalog, compatibility, nativeColonyId, source, idleState, rows);
        }

        internal static OwnedReservoirCheckpointV1 Parse(ReadOnlySpan<byte> data, FluidMaterialCatalog catalog,
            OwnedReservoirCheckpointCompatibility expectedCompatibility, Guid expectedNativeColonyId)
        {
            if (catalog == null) throw new ArgumentNullException(nameof(catalog));
            if (expectedCompatibility == null) throw new ArgumentNullException(nameof(expectedCompatibility));
            if (expectedNativeColonyId == Guid.Empty) throw new ArgumentException("A native colony identity is required.", nameof(expectedNativeColonyId));
            if (data.Length < 200 || data.Length > MaximumPayloadBytes) throw new InvalidDataException("Invalid reservoir checkpoint length.");
            var owned = data.ToArray();
            using var sha = SHA256.Create();
            if (!CryptographicOperations.FixedTimeEquals(sha.ComputeHash(owned, 0, owned.Length - ChecksumBytes), owned.AsSpan(owned.Length - ChecksumBytes)))
                throw new InvalidDataException("Reservoir checkpoint checksum mismatch.");
            try
            {
                using var stream = new MemoryStream(owned, 0, owned.Length - ChecksumBytes, false, true);
                using var reader = new BinaryReader(stream, StrictUtf8, true);
                if (!ReadBytes(reader, Magic.Length).AsSpan().SequenceEqual(Magic) || reader.ReadInt32() != FormatVersion)
                    throw new InvalidDataException("Unsupported reservoir checkpoint signature or version.");
                if (reader.ReadInt64() != owned.Length) throw new InvalidDataException("Reservoir checkpoint declared length differs from its complete payload.");
                var compatibility = new OwnedReservoirCheckpointCompatibility(reader.ReadInt32(), ReadHash(reader), ReadHash(reader));
                if (compatibility.OniBuild != expectedCompatibility.OniBuild || compatibility.AssemblyCSharpSha256 != expectedCompatibility.AssemblyCSharpSha256 ||
                    compatibility.AssemblyCSharpFirstpassSha256 != expectedCompatibility.AssemblyCSharpFirstpassSha256)
                    throw new InvalidDataException("Reservoir checkpoint requires a different ONI build or managed contract.");
                var colony = ReadGuid(reader);
                if (colony != expectedNativeColonyId) throw new InvalidDataException("Reservoir checkpoint belongs to a different native colony.");
                if (ReadIdentity(reader) != catalog.Version || ReadHash(reader) != catalog.Fingerprint)
                    throw new InvalidDataException("Reservoir checkpoint requires a different material catalog.");
                // V1 has one exact idle representation. Unknown flags, counts
                // and protocol versions cannot become a resumable outbox.
                if (reader.ReadInt32() != 1 || reader.ReadInt32() != 0 || reader.ReadInt32() != 0 ||
                    reader.ReadInt32() != 0 || reader.ReadInt32() != 0 || reader.ReadInt32() != 0)
                    throw new InvalidDataException("Unsupported nonidle reservoir protocol state.");
                var coreLength = reader.ReadInt64();
                if (coreLength < 80 || coreLength > MaximumPayloadBytes - 512 || coreLength > stream.Length - stream.Position - 4)
                    throw new InvalidDataException("Invalid nested Core checkpoint length.");
                var core = ReadBytes(reader, (int)coreLength); var source = RestoreCore(core, catalog);
                if (reader.ReadInt32() != 2) throw new InvalidDataException("Exactly two reservoir owner records are required.");
                var rows = new[] { ReadOwner(reader, source), ReadOwner(reader, source) };
                if (stream.Position != stream.Length) throw new InvalidDataException("Trailing reservoir checkpoint data.");
                if (rows[0].SavedInstanceId >= rows[1].SavedInstanceId) throw new InvalidDataException("Reservoir owner records must be uniquely and canonically ordered.");
                ValidateDomain(source, rows);
                return new OwnedReservoirCheckpointV1(owned, core, catalog, compatibility, colony, source, new OwnedReservoirCheckpointIdleState(), rows);
            }
            catch (Exception error) when (error is EndOfStreamException || error is ArgumentException || error is InvalidOperationException || error is OverflowException)
            { throw new InvalidDataException("Invalid reservoir checkpoint data.", error); }
        }

        internal byte[] CopyEncodedData() => (byte[])encoded.Clone();
        internal ColonyFluidState RestoreAuthority() => ColonyFluidState.Restore(coreData, catalog);

        private static ColonyFrameSnapshot RestoreCore(byte[] data, FluidMaterialCatalog catalog)
        {
            var restored = ColonyFluidState.Restore(data, catalog);
            if (!data.AsSpan().SequenceEqual(restored.Save())) throw new InvalidDataException("Nested Core checkpoint is not canonical v3 data.");
            return restored.CaptureSnapshot();
        }

        private static void ValidateDomain(ColonyFrameSnapshot source, OwnedReservoirCheckpointOwner[] rows)
        {
            if (source.Inventories.Count != 0) throw new InvalidDataException("Reservoir checkpoint v1 owns containers only; world mass and metadata must be empty.");
            var ids = new HashSet<int>(); var tokens = new HashSet<Guid>(); var handles = new HashSet<FluidContainerHandle>(); var anchors = new HashSet<int>();
            foreach (var owner in rows)
            {
                if (!ids.Add(owner.SavedInstanceId) || !tokens.Add(owner.PersistentOwnerToken) || !handles.Add(owner.Container) || !anchors.Add(owner.AnchorCell))
                    throw new InvalidDataException("Duplicate native owner, owner token, Core allocation or native anchor.");
                var allocation = source.SampleContainer(owner.Container);
                if (allocation.Definition.MaximumMassKg != owner.MaximumMassKg || allocation.Definition.VolumeCubicMetres != owner.VolumeCubicMetres ||
                    allocation.Definition.AllowedPhases != owner.AllowedPhases)
                    throw new InvalidDataException("Native owner declaration differs from the exact Core container definition.");
                if (!source.TrySampleCell(owner.AnchorCell, out var cell) || cell!.WorldId != owner.WorldId)
                    throw new InvalidDataException("Native owner anchor lies outside its declared Core world geometry.");
            }
            foreach (var allocation in source.Containers.Values)
                if ((allocation.Inventory.MassesKg.Count != 0 || allocation.Inventory.DiseaseCounts.Count != 0 || allocation.Inventory.RadiationQuantity != 0) &&
                    !handles.Contains(allocation.Handle))
                    throw new InvalidDataException("A nonempty Core allocation has no exact saved native owner.");
        }

        private static byte[] Encode(byte[] core, ColonyFrameSnapshot source, OwnedReservoirCheckpointCompatibility compatibility,
            Guid nativeColonyId, OwnedReservoirCheckpointOwner[] rows)
        {
            using var stream = new MemoryStream();
            using (var writer = new BinaryWriter(stream, StrictUtf8, true))
            {
                writer.Write(Magic); writer.Write(FormatVersion); writer.Write(0L);
                writer.Write(compatibility.OniBuild); WriteHash(writer, compatibility.AssemblyCSharpSha256); WriteHash(writer, compatibility.AssemblyCSharpFirstpassSha256);
                writer.Write(nativeColonyId.ToByteArray()); WriteIdentity(writer, source.MaterialModelVersion); WriteHash(writer, source.MaterialModelFingerprint);
                writer.Write(1); for (var i = 0; i < 5; ++i) writer.Write(0); // Explicit idle/zero-outbox protocol.
                writer.Write((long)core.Length); writer.Write(core); writer.Write(rows.Length);
                foreach (var owner in rows)
                {
                    writer.Write(owner.SavedInstanceId); WriteIdentity(writer, owner.PrefabId); writer.Write(owner.PersistentOwnerToken.ToByteArray());
                    writer.Write(owner.Container.ColonyId.ToByteArray()); WriteIdentity(writer, owner.Container.Id);
                    writer.Write(owner.Container.Generation); writer.Write(owner.Container.AllocationToken.ToByteArray());
                    writer.Write(owner.WorldId); writer.Write(owner.AnchorCell); writer.Write(owner.Orientation); writer.Write(owner.ConstructionElementId);
                    writer.Write(owner.ConstructionMassKg); writer.Write(owner.VolumeCubicMetres); writer.Write(owner.MaximumMassKg); writer.Write((int)owner.AllowedPhases);
                }
                var length = checked(stream.Length + ChecksumBytes);
                if (length > MaximumPayloadBytes) throw new InvalidDataException("Reservoir checkpoint exceeds its size limit.");
                writer.Flush(); stream.Position = 12; writer.Write(length); writer.Flush();
            }
            var payload = stream.ToArray(); using var sha = SHA256.Create(); var checksum = sha.ComputeHash(payload);
            var result = new byte[checked(payload.Length + ChecksumBytes)]; payload.CopyTo(result, 0); checksum.CopyTo(result, payload.Length); return result;
        }

        private static OwnedReservoirCheckpointOwner ReadOwner(BinaryReader reader, ColonyFrameSnapshot source)
        {
            var instance = reader.ReadInt32(); var prefab = ReadIdentity(reader); var ownerToken = ReadGuid(reader);
            var colony = ReadGuid(reader); var id = ReadIdentity(reader); var generation = reader.ReadInt64(); var token = ReadGuid(reader);
            if (!source.Containers.TryGetValue(id, out var allocation) || allocation.Handle.ColonyId != colony ||
                allocation.Handle.Generation != generation || allocation.Handle.AllocationToken != token)
                throw new InvalidDataException("Saved owner requires a different full Core allocation handle.");
            return new OwnedReservoirCheckpointOwner(instance, prefab, ownerToken, allocation.Handle,
                reader.ReadInt32(), reader.ReadInt32(), reader.ReadInt32(), reader.ReadInt32(), reader.ReadDouble(),
                reader.ReadDouble(), reader.ReadDouble(), (FluidContainerPhases)reader.ReadInt32());
        }
        private static byte[] ReadBytes(BinaryReader reader, int count)
        { var data = reader.ReadBytes(count); if (data.Length != count) throw new EndOfStreamException(); return data; }
        private static Guid ReadGuid(BinaryReader reader)
        { var id = new Guid(ReadBytes(reader, 16)); if (id == Guid.Empty) throw new InvalidDataException("Empty saved allocation identity."); return id; }
        private static string ReadIdentity(BinaryReader reader)
        {
            var length = reader.ReadInt32(); if (length <= 0 || length > 1024) throw new InvalidDataException("Invalid identity length.");
            var identity = StrictUtf8.GetString(ReadBytes(reader, length)); ValidateIdentity(identity); return identity;
        }
        private static void WriteIdentity(BinaryWriter writer, string identity)
        { ValidateIdentity(identity); var data = StrictUtf8.GetBytes(identity); writer.Write(data.Length); writer.Write(data); }
        internal static void ValidateIdentity(string value)
        {
            if (string.IsNullOrWhiteSpace(value) || value.Length > 256 || StrictUtf8.GetByteCount(value) > 1024)
                throw new ArgumentException("A bounded explicit identity is required.");
            foreach (var character in value) if (char.IsControl(character)) throw new ArgumentException("Identity contains a control character.");
        }
        internal static string ValidateHash(string value)
        {
            if (value == null || value.Length != 64) throw new ArgumentException("A complete SHA256 contract identity is required.");
            var positive = false;
            foreach (var character in value)
            {
                if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F')))
                    throw new ArgumentException("Invalid SHA256 contract identity.");
                if (character != '0') positive = true;
            }
            if (!positive) throw new ArgumentException("An unknown zero contract identity is not supported.");
            return value.ToLowerInvariant();
        }
        private static void WriteHash(BinaryWriter writer, string value)
        {
            for (var i = 0; i < 32; ++i) writer.Write(Convert.ToByte(value.Substring(i * 2, 2), 16));
        }
        private static string ReadHash(BinaryReader reader) => BitConverter.ToString(ReadBytes(reader, 32)).Replace("-", "").ToLowerInvariant();
    }
}
