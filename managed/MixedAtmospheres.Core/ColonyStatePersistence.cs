using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Core
{
    public sealed partial class ColonyFluidState
    {
        private static readonly byte[] SaveMagic = Encoding.ASCII.GetBytes("MAFLUIDS");
        public const int SaveFormatVersion = 2;
        private const int MaximumSaveBytes = 640 * 1024 * 1024;
        private static readonly Encoding StrictUtf8 = new UTF8Encoding(false, true);

        /// <summary>Custom inventory checkpoint only. ONI must atomically pair this with its matching completed native frame.</summary>
        public byte[] Save()
            => SaveSnapshot(CaptureSnapshot(), SaveMagic);

        internal static byte[] SaveSnapshot(ColonyFrameSnapshot snapshot, byte[] magic)
        {
            using var stream = new MemoryStream();
            using (var writer = new BinaryWriter(stream, StrictUtf8, true))
            {
                writer.Write(magic); writer.Write(SaveFormatVersion);
                WriteIdentity(writer, snapshot.Catalog.Version); writer.Write(snapshot.Catalog.FingerprintBytes);
                writer.Write(snapshot.Width); writer.Write(snapshot.Height); writer.Write(snapshot.Geometry.VacuumHash);
                writer.Write(snapshot.CellVolumeCubicMetres); writer.Write(snapshot.Revision);
                writer.Write(snapshot.CompletedFrame); writer.Write(snapshot.SimulationTimeSeconds);
                writer.Write(snapshot.Geometry.SolidHashes.Count);
                foreach (var hash in snapshot.Geometry.SolidHashes) writer.Write(hash);
                writer.Write(snapshot.Worlds.Count);
                for (var i = 0; i < snapshot.Worlds.Count; ++i)
                {
                    var p = snapshot.Worlds[i]; writer.Write(p.WorldId); writer.Write(p.Width); writer.Write(p.Height);
                    writer.Write(p.OffsetX); writer.Write(p.OffsetY);
                    foreach (var element in snapshot.Geometry.Elements[i]) writer.Write(element);
                }
                WriteInventory(writer, snapshot.InitialInventory); WriteInventory(writer, snapshot.SuppliedInventory);
                WriteInventory(writer, snapshot.RemovedInventory); writer.Write(snapshot.ExternalHeatJoules);
                writer.Write(snapshot.Inventories.Count);
                foreach (var entry in snapshot.Inventories.OrderBy(entry => entry.Key))
                { writer.Write(entry.Key); WriteInventory(writer, entry.Value); }
                if (stream.Length + 32 > MaximumSaveBytes) throw new InvalidOperationException("Custom colony checkpoint exceeds the size limit.");
            }
            var payload = stream.ToArray(); using var sha = SHA256.Create(); var checksum = sha.ComputeHash(payload);
            var result = new byte[checked(payload.Length + checksum.Length)];
            payload.CopyTo(result, 0); checksum.CopyTo(result, payload.Length); return result;
        }

        /// <summary>Validates the complete custom checkpoint before returning a publishable authority.</summary>
        public static ColonyFluidState Restore(ReadOnlySpan<byte> bytes, FluidMaterialCatalog catalog)
            => new ColonyFluidState(RestoreSnapshot(bytes, catalog, SaveMagic, false));

        internal static ColonyFrameSnapshot RestoreSnapshot(ReadOnlySpan<byte> bytes, FluidMaterialCatalog catalog, byte[] magic, bool allowUnplaced)
        {
            if (catalog == null) throw new ArgumentNullException(nameof(catalog));
            if (bytes.Length < 80 || bytes.Length > MaximumSaveBytes) throw new InvalidDataException("Invalid custom colony checkpoint length.");
            var payload = bytes.Slice(0, bytes.Length - 32).ToArray();
            using var sha = SHA256.Create();
            if (!bytes.Slice(bytes.Length - 32).SequenceEqual(sha.ComputeHash(payload)))
                throw new InvalidDataException("Custom colony checkpoint checksum mismatch.");
            try
            {
                using var stream = new MemoryStream(payload, false); using var reader = new BinaryReader(stream, StrictUtf8, true);
                if (!reader.ReadBytes(magic.Length).SequenceEqual(magic) || reader.ReadInt32() != SaveFormatVersion)
                    throw new InvalidDataException("Unsupported custom colony checkpoint signature or version.");
                if (ReadIdentity(reader) != catalog.Version || !reader.ReadBytes(32).SequenceEqual(catalog.FingerprintBytes))
                    throw new InvalidDataException("Custom colony checkpoint requires a different material model.");
                var width = reader.ReadInt32(); var height = reader.ReadInt32(); var vacuumHash = reader.ReadInt32();
                if (width <= 0 || height <= 0 || (long)width * height > ColonyGeometry.MaximumCells)
                    throw new InvalidDataException("Invalid checkpoint cluster dimensions.");
                var volume = reader.ReadDouble(); var revision = reader.ReadInt64(); var frame = reader.ReadInt64(); var time = reader.ReadDouble();
                if (revision < 0 || frame < 0) throw new InvalidDataException("Invalid checkpoint revision or completed frame.");
                FluidChecks.Nonnegative(time, "simulation time");
                var solidCount = ReadCount(reader, 65536, "native solid identities");
                var solids = new int[solidCount]; for (var i = 0; i < solidCount; ++i) solids[i] = reader.ReadInt32();
                var count = ReadCount(reader, ColonyGeometry.MaximumWorlds, "worlds");
                var placements = new List<NativeWorldPlacement>(); var elements = new List<int[]>();
                long totalWorldCells = 0;
                for (var i = 0; i < count; ++i)
                {
                    var p = new NativeWorldPlacement(reader.ReadInt32(), reader.ReadInt32(), reader.ReadInt32(), reader.ReadInt32(), reader.ReadInt32());
                    var cells = (long)p.Width * p.Height; totalWorldCells += cells;
                    if (p.Width <= 0 || p.Height <= 0 || cells > ColonyGeometry.MaximumCells || totalWorldCells > (long)width * height ||
                        cells * 4 > stream.Length - stream.Position) throw new InvalidDataException("Invalid checkpoint world payload.");
                    var data = new int[(int)cells]; for (var cell = 0; cell < data.Length; ++cell) data[cell] = reader.ReadInt32();
                    placements.Add(p); elements.Add(data);
                }
                var geometry = new ColonyGeometry(width, height, vacuumHash, volume, placements, elements, solids);
                var initial = ReadInventory(reader, catalog); var supplied = ReadInventory(reader, catalog); var removed = ReadInventory(reader, catalog);
                var heat = reader.ReadDouble(); FluidChecks.Finite(heat, "external heat");
                var inventories = new Dictionary<int, FluidInventory>();
                var occupied = ReadCount(reader, (int)totalWorldCells, "fluid cells");
                for (var i = 0; i < occupied; ++i)
                {
                    var cell = reader.ReadInt32(); geometry.RequireFluidCell(cell);
                    if (inventories.ContainsKey(cell)) throw new InvalidDataException("Duplicate checkpoint fluid cell.");
                    var inventory = ReadInventory(reader, catalog);
                    if (inventory.IsEmpty) throw new InvalidDataException("Redundant empty checkpoint inventory.");
                    catalog.Describe(inventory, allowUnplaced ? double.MaxValue : geometry.CellVolume); inventories.Add(cell, inventory);
                }
                if (stream.Position != stream.Length) throw new InvalidDataException("Trailing custom colony checkpoint payload.");
                var snapshot = new ColonyFrameSnapshot(geometry, catalog, revision, frame, time, inventories, initial, supplied, removed, heat);
                ValidateBalance(snapshot);
                return snapshot;
            }
            catch (Exception exception) when (exception is EndOfStreamException || exception is ArgumentException ||
                exception is InvalidOperationException || exception is OverflowException || exception is DecoderFallbackException)
            { throw new InvalidDataException("Invalid custom colony checkpoint state.", exception); }
        }

        private static void WriteIdentity(BinaryWriter writer, string value)
        {
            FluidChecks.Identity(value, nameof(value)); var bytes = StrictUtf8.GetBytes(value); writer.Write(bytes.Length); writer.Write(bytes);
        }

        private static string ReadIdentity(BinaryReader reader)
        {
            var count = ReadCount(reader, 1024, "identity bytes");
            var data = reader.ReadBytes(count); if (data.Length != count) throw new EndOfStreamException();
            var result = StrictUtf8.GetString(data); FluidChecks.Identity(result, "checkpoint identity"); return result;
        }

        private static int ReadCount(BinaryReader reader, int maximum, string name)
        {
            var count = reader.ReadInt32();
            if (count < 0 || count > maximum) throw new InvalidDataException("Invalid checkpoint count for " + name);
            return count;
        }

        private static void WriteInventory(BinaryWriter writer, FluidInventory inventory)
        {
            writer.Write(inventory.MassesKg.Count);
            foreach (var entry in inventory.MassesKg) { WriteIdentity(writer, entry.Key); writer.Write(entry.Value); }
            writer.Write(inventory.TotalEnergyJoules); writer.Write(inventory.MomentumX); writer.Write(inventory.MomentumY);
            writer.Write(inventory.RadiationQuantity); writer.Write(inventory.DiseaseCounts.Count);
            foreach (var entry in inventory.DiseaseCounts) { writer.Write(entry.Key); writer.Write(entry.Value); }
        }

        private static FluidInventory ReadInventory(BinaryReader reader, FluidMaterialCatalog catalog)
        {
            var count = ReadCount(reader, catalog.Materials.Count, "constituents");
            var masses = new Dictionary<string, double>(StringComparer.Ordinal);
            for (var i = 0; i < count; ++i)
            {
                var id = ReadIdentity(reader); catalog.Get(id); var mass = reader.ReadDouble();
                FluidChecks.Positive(mass, "checkpoint constituent mass");
                if (masses.ContainsKey(id)) throw new InvalidDataException("Duplicate checkpoint constituent."); masses.Add(id, mass);
            }
            var energy = reader.ReadDouble(); var momentumX = reader.ReadDouble(); var momentumY = reader.ReadDouble();
            var radiation = reader.ReadDouble(); var diseases = ReadCount(reader, FluidChecks.MaximumDiseaseKinds, "disease kinds");
            var diseaseCounts = new Dictionary<int, double>();
            for (var i = 0; i < diseases; ++i)
            {
                var id = reader.ReadInt32(); var value = reader.ReadDouble(); FluidChecks.Positive(value, "checkpoint disease quantity");
                if (diseaseCounts.ContainsKey(id)) throw new InvalidDataException("Duplicate checkpoint disease identity."); diseaseCounts.Add(id, value);
            }
            return new FluidInventory(masses, energy, momentumX, momentumY, diseaseCounts, radiation);
        }

        private static void ValidateBalance(ColonyFrameSnapshot snapshot)
        {
            // Compensating large external source/sink ledgers permits floating-point cancellation;
            // tolerance is relative to all participating terms, never a reason to delete traces.
            var total = snapshot.Totals();
            void Equal(double actual, double initial, double supplied, double removed, double extra = 0)
            {
                var expected = initial + supplied - removed + extra;
                var scale = Math.Abs(initial) + Math.Abs(supplied) + Math.Abs(removed) + Math.Abs(extra);
                var tolerance = Math.Max(double.Epsilon, scale * 1e-10);
                if (double.IsNaN(expected) || double.IsInfinity(expected) || Math.Abs(actual - expected) > tolerance)
                    throw new InvalidDataException("Checkpoint conservation ledger does not balance.");
            }
            var ids = total.MassesKg.Keys.Concat(snapshot.InitialInventory.MassesKg.Keys).Concat(snapshot.SuppliedInventory.MassesKg.Keys)
                .Concat(snapshot.RemovedInventory.MassesKg.Keys).Distinct(StringComparer.Ordinal);
            double Mass(FluidInventory inventory, string id) => inventory.MassesKg.TryGetValue(id, out var value) ? value : 0;
            foreach (var id in ids) Equal(Mass(total, id), Mass(snapshot.InitialInventory, id), Mass(snapshot.SuppliedInventory, id), Mass(snapshot.RemovedInventory, id));
            var diseaseIds = total.DiseaseCounts.Keys.Concat(snapshot.InitialInventory.DiseaseCounts.Keys).Concat(snapshot.SuppliedInventory.DiseaseCounts.Keys)
                .Concat(snapshot.RemovedInventory.DiseaseCounts.Keys).Distinct();
            double Disease(FluidInventory inventory, int id) => inventory.DiseaseCounts.TryGetValue(id, out var value) ? value : 0;
            foreach (var id in diseaseIds) Equal(Disease(total, id), Disease(snapshot.InitialInventory, id), Disease(snapshot.SuppliedInventory, id), Disease(snapshot.RemovedInventory, id));
            Equal(total.TotalEnergyJoules, snapshot.InitialInventory.TotalEnergyJoules, snapshot.SuppliedInventory.TotalEnergyJoules,
                snapshot.RemovedInventory.TotalEnergyJoules, snapshot.ExternalHeatJoules);
            Equal(total.MomentumX, snapshot.InitialInventory.MomentumX, snapshot.SuppliedInventory.MomentumX, snapshot.RemovedInventory.MomentumX);
            Equal(total.MomentumY, snapshot.InitialInventory.MomentumY, snapshot.SuppliedInventory.MomentumY, snapshot.RemovedInventory.MomentumY);
            Equal(total.RadiationQuantity, snapshot.InitialInventory.RadiationQuantity, snapshot.SuppliedInventory.RadiationQuantity, snapshot.RemovedInventory.RadiationQuantity);
        }
    }
}
