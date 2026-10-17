using System;
using System.Buffers.Binary;

namespace MixedAtmospheres.Oni
{
    internal readonly struct NativeCellMassEdit
    {
        internal readonly int Cell;
        internal readonly uint Species;
        internal readonly float MassKg, TemperatureK;
        internal NativeCellMassEdit(int cell, uint species, float massKg, float temperatureK)
        { Cell = cell; Species = species; MassKg = massKg; TemperatureK = temperatureK; }
    }

    // Complete preflight for additive ModifyCell/ModifyMass packets. Native
    // callback-less producers include the game's CO2 particle manager.
    internal static class NativeCellMassEditPlan
    {
        internal static NativeCellMassEdit[] Decode(int length, int count, ReadOnlySpan<byte> payload,
            Func<int, bool> ownsCell, Func<ushort, uint> species)
        {
            if (length != 28 || count < 1 || count > int.MaxValue / 28 || payload.Length != length * count)
                throw new ArgumentException("Invalid native cell mass-edit packet.");
            var owned = 0;
            for (var index = 0; index < count; ++index)
                if (ownsCell(BinaryPrimitives.ReadInt32LittleEndian(payload.Slice(index * length)))) ++owned;
            if (owned == 0) return Array.Empty<NativeCellMassEdit>();
            if (owned != count || count > 256)
                throw new NotSupportedException("Cell mass-edit batches require one reserved owner and at most 256 requests.");
            var result = new NativeCellMassEdit[count];
            for (var index = 0; index < count; ++index)
            {
                var data = payload.Slice(index * length, length);
                if (BinaryPrimitives.ReadInt32LittleEndian(data.Slice(4)) != -1 || data[22] != 0 || data[24] != 1 ||
                    BinaryPrimitives.ReadInt32LittleEndian(data.Slice(16)) != 0)
                    throw new NotSupportedException("Owned cell mass additions require no callback, replacement, displacement or disease.");
                var temperature = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(data.Slice(8)));
                var mass = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(data.Slice(12)));
                if (float.IsNaN(mass) || float.IsInfinity(mass) || mass <= 0 ||
                    float.IsNaN(temperature) || float.IsInfinity(temperature) || temperature <= 0)
                    throw new NotSupportedException("Owned cell mass additions require positive finite mass and temperature.");
                result[index] = new NativeCellMassEdit(BinaryPrimitives.ReadInt32LittleEndian(data),
                    species(BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(20))), mass, temperature);
            }
            return result;
        }
    }
}
