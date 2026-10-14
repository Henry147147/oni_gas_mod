using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;

namespace MixedAtmospheres.Core.Interop
{
    public enum NativeProductionBranch { LowTemperature, HighTemperature, SublimationOrConversion }

    public readonly struct SuppressedNativeProduction
    {
        public int ElementHash { get; }
        public NativeProductionBranch Branch { get; }
        internal SuppressedNativeProduction(int elementHash, NativeProductionBranch branch)
        { ElementHash = elementHash; Branch = branch; }
    }

    public sealed class FilteredNativeElementTable
    {
        private readonly byte[] data;
        public ReadOnlySpan<byte> Data => data;
        public ReadOnlyCollection<SuppressedNativeProduction> Suppressed { get; }
        internal FilteredNativeElementTable(byte[] data, List<SuppressedNativeProduction> suppressed)
        { this.data = data; Suppressed = suppressed.AsReadOnly(); }
    }

    // The inspected 744825 Windows/Linux ABI has 164-byte element records.
    // This filters definitions only, not emitters, chunks, buildings, callbacks,
    // interactions or managed conversions. It is not an ownership certificate.
    public static class NativeElementTable
    {
        public const int RecordBytes = 164;
        private static int Int(ReadOnlySpan<byte> data, int offset) => BinaryPrimitives.ReadInt32LittleEndian(data.Slice(offset, 4));
        private static ushort Index(ReadOnlySpan<byte> row, int offset) => BinaryPrimitives.ReadUInt16LittleEndian(row.Slice(offset, 2));
        private static float Float(ReadOnlySpan<byte> row, int offset) => BitConverter.Int32BitsToSingle(Int(row, offset));
        private static void SetFloat(Span<byte> row, int offset, float value) =>
            BinaryPrimitives.WriteInt32LittleEndian(row.Slice(offset, 4), BitConverter.SingleToInt32Bits(value));
        private static bool Finite(float value) => !float.IsNaN(value) && !float.IsInfinity(value);
        private static bool IsFluid(NativeMatterPhase phase) => phase == NativeMatterPhase.Gas || phase == NativeMatterPhase.Liquid;

        public static FilteredNativeElementTable SuppressFluidProduction(ReadOnlySpan<byte> source,
            IReadOnlyDictionary<int, NativeMatterPhase> expectedPhases)
        {
            if (expectedPhases == null) throw new ArgumentNullException(nameof(expectedPhases));
            if (source.Length < 4 || source.Length > 64 * 1024 * 1024) throw new InvalidDataException("Invalid element table length.");
            var count = Int(source, 0);
            if (count < 1 || count > ushort.MaxValue || source.Length < 4L + (long)count * RecordBytes || expectedPhases.Count != count)
                throw new InvalidDataException("Element table does not match the complete phase contract.");
            var hashes = new Dictionary<int, int>();
            var phases = new NativeMatterPhase[count];
            for (var i = 0; i < count; ++i)
            {
                var row = source.Slice(4 + i * RecordBytes, RecordBytes);
                var hash = Int(row, 0); var phase = (NativeMatterPhase)(row[10] & 3);
                if (Index(row, 8) != i || hashes.ContainsKey(hash) ||
                    !expectedPhases.TryGetValue(hash, out var expected) || phase != expected)
                    throw new InvalidDataException("Element identity, index or phase differs from the inspected contract.");
                hashes.Add(hash, i); phases[i] = phase;
            }
            // Validate every record before generating any transformed table.
            for (var i = 0; i < count; ++i)
            {
                var row = source.Slice(4 + i * RecordBytes, RecordBytes);
                foreach (var offset in new[] { 4, 6, 84, 86 })
                    if (Index(row, offset) >= count && Index(row, offset) != ushort.MaxValue)
                        throw new InvalidDataException("Unknown element transition index.");
                foreach (var offset in new[] { 68, 76 })
                {
                    var fraction = Float(row, offset + 4);
                    if (!Finite(fraction) || fraction < 0 || fraction > 1 || (fraction > 0 && !hashes.ContainsKey(Int(row, offset))))
                        throw new InvalidDataException("Invalid transition byproduct.");
                }
                foreach (var offset in new[] { 56, 60, 124, 128, 132, 136 })
                    if (!Finite(Float(row, offset))) throw new InvalidDataException("Nonfinite production parameter.");
            }
            var data = source.ToArray(); // retain native names/opaque tail byte-for-byte
            var suppressed = new List<SuppressedNativeProduction>();
            bool FluidIndex(int index) => index < count && IsFluid(phases[index]);
            for (var i = 0; i < count; ++i)
            {
                var row = data.AsSpan(4 + i * RecordBytes, RecordBytes);
                foreach (var low in new[] { true, false })
                {
                    var targetOffset = low ? 4 : 6; var oreOffset = low ? 68 : 76;
                    var fluidByproduct = Float(row, oreOffset + 4) > 0 && IsFluid(phases[hashes[Int(row, oreOffset)]]);
                    if (!FluidIndex(Index(row, targetOffset)) && !fluidByproduct) continue;
                    // Disable the entire branch, including when only its
                    // byproduct is fluid. Dropping just that fraction would
                    // allow an unaccounted solid transition to proceed.
                    BinaryPrimitives.WriteUInt16LittleEndian(row.Slice(targetOffset, 2), ushort.MaxValue);
                    SetFloat(row, low ? 56 : 60, low ? -float.MaxValue : float.MaxValue);
                    SetFloat(row, oreOffset + 4, 0);
                    suppressed.Add(new SuppressedNativeProduction(Int(row, 0), low ? NativeProductionBranch.LowTemperature : NativeProductionBranch.HighTemperature));
                }
                if (FluidIndex(Index(row, 84)) || FluidIndex(Index(row, 86)))
                {
                    BinaryPrimitives.WriteUInt16LittleEndian(row.Slice(84, 2), ushort.MaxValue);
                    BinaryPrimitives.WriteUInt16LittleEndian(row.Slice(86, 2), ushort.MaxValue);
                    foreach (var offset in new[] { 124, 128, 132, 136 }) SetFloat(row, offset, 0);
                    suppressed.Add(new SuppressedNativeProduction(Int(row, 0), NativeProductionBranch.SublimationOrConversion));
                }
            }
            return new FilteredNativeElementTable(data, suppressed);
        }
    }
}
