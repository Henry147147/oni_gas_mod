using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;

namespace MixedAtmospheres.Core.Interop
{
    public enum NativeMatterPhase { Vacuum, Gas, Liquid, Solid }

    public readonly struct CapturedFluidCell
    {
        public int X { get; }
        public int Y { get; }
        public int ElementHash { get; }
        public NativeMatterPhase Phase { get; }
        public float MassKg { get; }
        public float TemperatureKelvin { get; }
        public float Radiation { get; }
        public int DiseaseHash { get; }
        public int DiseaseCount { get; }
        internal CapturedFluidCell(int x, int y, int element, NativeMatterPhase phase,
            float mass, float temperature, float radiation, int disease, int count)
        {
            X = x; Y = y; ElementHash = element; Phase = phase; MassKg = mass;
            TemperatureKelvin = temperature; Radiation = radiation; DiseaseHash = disease; DiseaseCount = count;
        }
    }

    public sealed class NativeFluidMigration
    {
        private readonly byte[] data;
        // A span does not expose a mutable backing ArraySegment through
        // MemoryMarshal.TryGetArray, unlike ReadOnlyMemory<byte>.
        public ReadOnlySpan<byte> NativeData => data;
        public ReadOnlyCollection<CapturedFluidCell> Fluids { get; }
        public int Width { get; }
        public int Height { get; }
        public int OffsetX { get; }
        public int OffsetY { get; }
        internal NativeFluidMigration(byte[] data, List<CapturedFluidCell> fluids, int width, int height, int x, int y)
        { this.data = data; Fluids = fluids.AsReadOnly(); Width = width; Height = height; OffsetX = x; OffsetY = y; }
    }

    // Original parser for the privately inspected Windows/Linux 744825 native save
    // contract. Callers must verify the game/native build before use. This is
    // NOT a complete ONI save parser or automatic existing-colony migration.
    public static class NativeWorldBlob
    {
        private static readonly byte[] Magic = { 83, 73, 77, 83, 65, 86, 69, 0 };
        private static int Int(ReadOnlySpan<byte> data, int offset) => BinaryPrimitives.ReadInt32LittleEndian(data.Slice(offset, 4));
        private static float Float(ReadOnlySpan<byte> data, int offset) => BitConverter.Int32BitsToSingle(Int(data, offset));
        private static bool Nonnegative(float value) => !float.IsInfinity(value) && !float.IsNaN(value) && value >= 0;
        private static NativeMatterPhase Phase(IReadOnlyDictionary<int, NativeMatterPhase> phases, int hash)
        {
            if (!phases.TryGetValue(hash, out var phase) || phase < NativeMatterPhase.Vacuum || phase > NativeMatterPhase.Solid)
                throw new InvalidDataException("Unknown native element or phase: " + hash);
            return phase;
        }

        public static NativeFluidMigration Split(ReadOnlySpan<byte> source,
            IReadOnlyDictionary<int, NativeMatterPhase> phases, int vacuumHash)
        {
            if (phases == null) throw new ArgumentNullException(nameof(phases));
            if (Phase(phases, vacuumHash) != NativeMatterPhase.Vacuum) throw new InvalidDataException("Vacuum element is not vacuum.");
            if (source.Length < 29 || !source.Slice(0, 8).SequenceEqual(Magic) || Int(source, 8) != 15)
                throw new InvalidDataException("Unsupported native save signature or version.");
            var width = Int(source, 12); var height = Int(source, 16);
            var x = Int(source, 20); var y = Int(source, 24);
            var count64 = (long)width * height;
            if (width < 3 || height < 3 || x < 0 || y < 0 || count64 > 16_777_216 ||
                29 + 36 * count64 != source.Length || (long)x + width > int.MaxValue || (long)y + height > int.MaxValue)
                throw new InvalidDataException("Invalid native save dimensions, placement or payload length.");
            var count = (int)count64;
            var native = source.ToArray();
            var fluids = new List<CapturedFluidCell>();
            for (var i = 0; i < count; ++i)
            {
                var cell = 29 + 16 * i; var disease = 29 + 16 * count + 8 * i; var wall = 29 + 24 * count + 12 * i;
                var element = Int(source, cell); var phase = Phase(phases, element);
                var temperature = Float(source, cell + 4); var mass = Float(source, cell + 8); var radiation = Float(source, cell + 12);
                var diseaseHash = Int(source, disease); var diseaseCount = Int(source, disease + 4);
                var wallPhase = Phase(phases, Int(source, wall));
                if (!Nonnegative(temperature) || !Nonnegative(mass) || !Nonnegative(radiation) || diseaseCount < 0 ||
                    (diseaseHash == 0 && diseaseCount != 0) || !Nonnegative(Float(source, wall + 4)) || !Nonnegative(Float(source, wall + 8)))
                    throw new InvalidDataException("Invalid native cell inventory or disease data.");
                if (wallPhase == NativeMatterPhase.Gas || wallPhase == NativeMatterPhase.Liquid)
                    throw new InvalidDataException("Fluid backwalls require a separate owner.");
                if (phase != NativeMatterPhase.Gas && phase != NativeMatterPhase.Liquid) continue;
                var localX = i % width - 1; var localY = i / width - 1;
                if (localX < 0 || localY < 0 || localX >= width - 2 || localY >= height - 2)
                    throw new InvalidDataException("Fluid in the native guard border cannot be migrated.");
                if (mass > 0 && temperature <= 0) throw new InvalidDataException("Fluid has mass without a positive temperature.");
                fluids.Add(new CapturedFluidCell(x + localX, y + localY, element, phase, mass, temperature, radiation, diseaseHash, diseaseCount));
                BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(cell, 4), vacuumHash);
                native.AsSpan(cell + 4, 12).Clear(); // no native fluid temperature/mass/radiation
                native.AsSpan(disease, 8).Clear(); // fluid disease moves with its owner
            }
            return new NativeFluidMigration(native, fluids, width - 2, height - 2, x, y);
        }
    }
}
