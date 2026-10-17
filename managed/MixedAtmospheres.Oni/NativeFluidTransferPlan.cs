using System;
using System.Buffers.Binary;

namespace MixedAtmospheres.Oni
{
    internal readonly struct NativeFluidTransferEdit
    {
        internal readonly int Cell, Callback;
        internal readonly uint Species, Flags;
        internal readonly double MassKg, Temperature;
        internal readonly bool Injection;
        internal NativeFluidTransferEdit(int cell, int callback, uint species, uint flags, double mass, double temperature, bool injection)
        { Cell=cell;Callback=callback;Species=species;Flags=flags;MassKg=mass;Temperature=temperature;Injection=injection; }
    }
    // Pure native-packet admission. The same phase classifier serves point
    // actors and explicit transfers, including both phases of a wet cell.
    internal static class NativeFluidTransferPlan
    {
        internal static NativeFluidTransferEdit[] Decode(int message,int length,int count,ReadOnlySpan<byte> bytes,
            Func<int,bool> owned,Func<ushort,uint> classify)
        {
            var injection=message==797274363;
            if ((!injection && message!=1727657959) || length!=(injection ? 24 : 16) || count<1 || bytes.Length!=checked(length*count))
                throw new ArgumentException("Invalid native fluid point/radius transfer batch.");
            var owners=0;
            for (var index=0;index<count;++index) if (owned(BinaryPrimitives.ReadInt32LittleEndian(bytes.Slice(index*length)))) ++owners;
            if (owners==0) return Array.Empty<NativeFluidTransferEdit>();
            if (owners!=count || count>256) throw new NotSupportedException("Fluid transfer batches require one reserved owner and at most 256 requests.");
            var result=new NativeFluidTransferEdit[count];
            for (var index=0;index<count;++index)
            {
                var data=bytes.Slice(index*length,length);
                if (injection ? BinaryPrimitives.ReadInt32LittleEndian(data.Slice(16))!=0 : data[15]!=0)
                    throw new NotSupportedException("Disease injection and rectangular consumption need separate ownership contracts.");
                var mass=(double)BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(data.Slice(8)));
                var temperature=injection ? (double)BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(data.Slice(12))) : 0;
                if (double.IsNaN(mass) || double.IsInfinity(mass) || mass<0 || injection &&
                    (double.IsNaN(temperature) || double.IsInfinity(temperature) || temperature<1))
                    throw new ArgumentException("Invalid native fluid mass or injection temperature.");
                var species=classify(BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(injection ? 20 : 12)));
                result[index]=new NativeFluidTransferEdit(BinaryPrimitives.ReadInt32LittleEndian(data),
                    BinaryPrimitives.ReadInt32LittleEndian(data.Slice(4)),species,injection ? 1U : 1U|((uint)data[14]<<8),mass,temperature,injection);
            }
            return result;
        }
    }
}
