using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;

namespace MixedAtmospheres.Core.Interop
{
    public readonly struct NativeWorldPlacement
    {
        public int WorldId { get; }
        public int Width { get; }
        public int Height { get; }
        public int OffsetX { get; }
        public int OffsetY { get; }
        public NativeWorldPlacement(int worldId, int width, int height, int offsetX, int offsetY)
        { WorldId = worldId; Width = width; Height = height; OffsetX = offsetX; OffsetY = offsetY; }
    }

    public sealed class NativeWorldSource
    {
        private readonly byte[] data;
        public int WorldId { get; }
        public ReadOnlySpan<byte> LengthPrefixedData => data;
        public NativeWorldSource(int worldId, ReadOnlySpan<byte> lengthPrefixedData)
        { WorldId = worldId; data = lengthPrefixedData.ToArray(); }
    }

    public readonly struct NativeFluidBinding
    {
        public string MaterialId { get; }
        public NativeMatterPhase Phase { get; }
        public NativeFluidBinding(string materialId, NativeMatterPhase phase)
        {
            if (string.IsNullOrWhiteSpace(materialId)) throw new ArgumentException("A stable material identity is required.", nameof(materialId));
            if (phase != NativeMatterPhase.Gas && phase != NativeMatterPhase.Liquid)
                throw new ArgumentOutOfRangeException(nameof(phase));
            MaterialId = materialId; Phase = phase;
        }
    }

    public readonly struct ImportedFluidCell
    {
        public int WorldId { get; }
        public string MaterialId { get; }
        public CapturedFluidCell Inventory { get; }
        internal ImportedFluidCell(int worldId, string materialId, CapturedFluidCell inventory)
        { WorldId = worldId; MaterialId = materialId; Inventory = inventory; }
    }

    public sealed class StagedNativeWorld
    {
        private readonly byte[] lengthPrefixedData;
        public NativeWorldPlacement Placement { get; }
        public NativeFluidMigration Migration { get; }
        public ReadOnlySpan<byte> LengthPrefixedData => lengthPrefixedData;
        internal StagedNativeWorld(NativeWorldPlacement placement, NativeFluidMigration migration)
        {
            Placement = placement; Migration = migration;
            lengthPrefixedData = new byte[checked(4 + migration.NativeData.Length)];
            BinaryPrimitives.WriteInt32LittleEndian(lengthPrefixedData, migration.NativeData.Length);
            migration.NativeData.CopyTo(lengthPrefixedData.AsSpan(4));
        }
    }

    // Staging is pure: no native calls, no callbacks, and no caller-owned writes.
    // Only a successful result may be handed to a new-colony loader. Existing
    // colony save conversion is deliberately not exposed by this API.
    public sealed class NativeColonyImport
    {
        private const int MaximumCells = 16_777_216;
        private const int MaximumWorlds = 256;
        private const long MaximumPayloadBytes = 640L * 1024 * 1024;
        public int Width { get; }
        public int Height { get; }
        public int VacuumHash { get; }
        public ReadOnlyCollection<int> SolidElementHashes { get; }
        public ReadOnlyCollection<StagedNativeWorld> Worlds { get; }
        public ReadOnlyCollection<ImportedFluidCell> Fluids { get; }
        private NativeColonyImport(int width, int height, int vacuumHash, List<int> solidHashes, List<StagedNativeWorld> worlds, List<ImportedFluidCell> fluids)
        {
            Width = width; Height = height; VacuumHash = vacuumHash; SolidElementHashes = solidHashes.AsReadOnly();
            Worlds = worlds.AsReadOnly(); Fluids = fluids.AsReadOnly();
        }

        public static NativeColonyImport StageNewColony(int width, int height,
            IReadOnlyList<NativeWorldPlacement> expectedWorlds, IReadOnlyList<NativeWorldSource> sources,
            IReadOnlyDictionary<int, NativeMatterPhase> phases,
            IReadOnlyDictionary<int, NativeFluidBinding> fluidBindings, int vacuumHash)
        {
            if (expectedWorlds == null) throw new ArgumentNullException(nameof(expectedWorlds));
            if (sources == null) throw new ArgumentNullException(nameof(sources));
            if (phases == null) throw new ArgumentNullException(nameof(phases));
            if (fluidBindings == null) throw new ArgumentNullException(nameof(fluidBindings));
            if (width <= 0 || height <= 0 || (long)width * height > MaximumCells)
                throw new InvalidDataException("Invalid cluster dimensions.");
            if (expectedWorlds.Count == 0 || expectedWorlds.Count > MaximumWorlds || expectedWorlds.Count != sources.Count)
                throw new InvalidDataException("Every expected world must have exactly one source blob.");

            var placements = new Dictionary<int, NativeWorldPlacement>();
            foreach (var world in expectedWorlds)
            {
                if (world.WorldId < 0 || world.Width <= 0 || world.Height <= 0 || world.OffsetX < 0 || world.OffsetY < 0 ||
                    (long)world.OffsetX + world.Width > width || (long)world.OffsetY + world.Height > height ||
                    placements.ContainsKey(world.WorldId))
                    throw new InvalidDataException("Invalid, duplicate or out-of-cluster world placement.");
                foreach (var other in placements.Values)
                    if (world.OffsetX < (long)other.OffsetX + other.Width && other.OffsetX < (long)world.OffsetX + world.Width &&
                        world.OffsetY < (long)other.OffsetY + other.Height && other.OffsetY < (long)world.OffsetY + world.Height)
                        throw new InvalidDataException("World interiors overlap.");
                placements.Add(world.WorldId, world);
            }

            var byId = new Dictionary<int, NativeWorldSource>();
            long payloadBytes = 0;
            foreach (var source in sources)
            {
                if (source == null || !placements.ContainsKey(source.WorldId) || byId.ContainsKey(source.WorldId))
                    throw new InvalidDataException("Unknown, null or duplicate world source.");
                var data = source.LengthPrefixedData;
                if (data.Length < 4 || BinaryPrimitives.ReadInt32LittleEndian(data) != data.Length - 4)
                    throw new InvalidDataException("Native world length prefix does not match its complete payload.");
                payloadBytes += data.Length;
                if (payloadBytes > MaximumPayloadBytes) throw new InvalidDataException("Cluster payload exceeds the import limit.");
                byId.Add(source.WorldId, source);
            }

            // Build only local results until the LAST world and binding passes.
            var staged = new List<StagedNativeWorld>();
            var fluids = new List<ImportedFluidCell>();
            foreach (var placement in expectedWorlds)
            {
                var migration = NativeWorldBlob.Split(byId[placement.WorldId].LengthPrefixedData.Slice(4), phases, vacuumHash);
                if (migration.Width != placement.Width || migration.Height != placement.Height ||
                    migration.OffsetX != placement.OffsetX || migration.OffsetY != placement.OffsetY)
                    throw new InvalidDataException("Native payload disagrees with expected world placement: " + placement.WorldId);
                foreach (var fluid in migration.Fluids)
                {
                    if (!fluidBindings.TryGetValue(fluid.ElementHash, out var binding) || binding.Phase != fluid.Phase ||
                        string.IsNullOrWhiteSpace(binding.MaterialId))
                        throw new InvalidDataException("No matching custom fluid material for native element " + fluid.ElementHash);
                    fluids.Add(new ImportedFluidCell(placement.WorldId, binding.MaterialId, fluid));
                }
                staged.Add(new StagedNativeWorld(placement, migration));
            }
            var solidHashes = new List<int>();
            foreach (var entry in phases) if (entry.Value == NativeMatterPhase.Solid) solidHashes.Add(entry.Key);
            solidHashes.Sort();
            return new NativeColonyImport(width, height, vacuumHash, solidHashes, staged, fluids);
        }
    }
}
