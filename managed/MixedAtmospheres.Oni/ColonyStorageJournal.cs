using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Numerics;
using MixedAtmospheres.Core;
namespace MixedAtmospheres.Oni
{
    // Cumulative bookkeeping only; never a physical inventory or a source of
    // values for withdrawal, temperature or a native compatibility carrier.
    internal readonly struct StorageJournalAmount
    {
        internal readonly BigInteger Units;
        private StorageJournalAmount(BigInteger units){Units=units;}
        internal int Sign=>Units.Sign;
        internal static StorageJournalAmount From(PipeAmount value)
        {
            // Decode the unchanged physical expansion into exact journal units.
            using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true))value.Write(writer);
            stream.Position=0;using var reader=new BinaryReader(stream);var units=BigInteger.Zero;
            for(int i=0,n=reader.ReadInt32();i<n;++i)units+=CoupledAccountQuantity.FromDouble(reader.ReadDouble()).Binary64Units;
            return new StorageJournalAmount(units);
        }
        internal StorageJournalAmount Plus(StorageJournalAmount other)=>new StorageJournalAmount(Units+other.Units);
        internal StorageJournalAmount Plus(PipeAmount other)=>Plus(From(other));
        internal StorageJournalAmount Minus(StorageJournalAmount other)=>new StorageJournalAmount(Units-other.Units);
        internal StorageJournalAmount Negated()=>new StorageJournalAmount(-Units);
        internal double Value
        {
            get
            {
                if(Units.IsZero)return 0;
                var positive=BigInteger.Abs(Units);var bytes=positive.ToByteArray();var last=bytes.Length-1;while(bytes[last]==0)--last;
                var bits=last*8;for(var top=bytes[last];top!=0;top>>=1)++bits;
                var shift=Math.Max(0,bits-53);var rounded=positive>>shift;
                if(shift!=0){var tail=positive-(rounded<<shift);var half=BigInteger.One<<(shift-1);if(tail>half||tail==half&&!rounded.IsEven)++rounded;}
                if(rounded==BigInteger.One<<53){rounded>>=1;++shift;}
                var exponent=rounded>=BigInteger.One<<52?shift+1:0;
                if(exponent>=2047)throw new InvalidOperationException("Cumulative storage journal has no finite binary64 display value.");
                var encoded=(ulong)rounded;if(exponent!=0)encoded=((ulong)exponent<<52)|(encoded&0xfffffffffffffUL);if(Units.Sign<0)encoded|=1UL<<63;
                return BitConverter.Int64BitsToDouble(unchecked((long)encoded));
            }
        }
        internal void Write(BinaryWriter writer)
        {
            var bytes=Units.IsZero?Array.Empty<byte>():Units.ToByteArray();writer.Write(bytes.Length);writer.Write(bytes);
        }
        internal static StorageJournalAmount Read(BinaryReader reader)
        {
            var length=reader.ReadInt32();
            // Finite binary64 fluxes accumulated through a uint64 revision
            // domain need at most271 bytes; this also bounds corrupt inputs.
            if(length<0||length>4096||length>reader.BaseStream.Length-reader.BaseStream.Position)throw new InvalidDataException("Invalid storage journal integer length.");
            if(length==0)return default;
            var bytes=reader.ReadBytes(length);var value=new BigInteger(bytes);
            if(value.IsZero||!value.ToByteArray().SequenceEqual(bytes))throw new InvalidDataException("Noncanonical storage journal integer.");
            return new StorageJournalAmount(value);
        }
    }
    internal sealed class StorageJournalInventory
    {
        internal static readonly StorageJournalInventory Empty=new StorageJournalInventory(new SortedDictionary<int,StorageJournalAmount>(),default,new SortedDictionary<int,StorageJournalAmount>());
        private readonly SortedDictionary<int,StorageJournalAmount> masses,germs;
        internal readonly IReadOnlyDictionary<int,StorageJournalAmount> Masses,Germs;
        internal readonly StorageJournalAmount Energy;
        private StorageJournalInventory(SortedDictionary<int,StorageJournalAmount> masses,StorageJournalAmount energy,SortedDictionary<int,StorageJournalAmount> germs)
        {this.masses=masses;this.germs=germs;Masses=new ReadOnlyDictionary<int,StorageJournalAmount>(masses);Energy=energy;Germs=new ReadOnlyDictionary<int,StorageJournalAmount>(germs);}
        internal StorageJournalInventory Joined(PipeInventory parcel)
        {
            var masses=new SortedDictionary<int,StorageJournalAmount>(this.masses);var germs=new SortedDictionary<int,StorageJournalAmount>(this.germs);
            foreach(var part in parcel.Parts){masses.TryGetValue(part.Id,out var before);masses[part.Id]=before.Plus(part.Amount);}
            foreach(var germ in parcel.Germs){germs.TryGetValue(germ.Id,out var before);germs[germ.Id]=before.Plus(germ.Amount);}
            return new StorageJournalInventory(masses,Energy.Plus(parcel.Energy),germs);
        }
        internal void Write(BinaryWriter writer){WriteMap(writer,Masses);Energy.Write(writer);WriteMap(writer,Germs);}
        internal static StorageJournalInventory Read(BinaryReader reader)=>new StorageJournalInventory(ReadMap(reader,65536),StorageJournalAmount.Read(reader),ReadMap(reader,255));
        private static void WriteMap(BinaryWriter writer,IReadOnlyDictionary<int,StorageJournalAmount> map){writer.Write(map.Count);foreach(var pair in map){writer.Write(pair.Key);pair.Value.Write(writer);}}
        private static SortedDictionary<int,StorageJournalAmount> ReadMap(BinaryReader reader,int maximum)
        {
            var count=reader.ReadInt32();if(count<0||count>maximum||count>(reader.BaseStream.Length-reader.BaseStream.Position)/9)throw new InvalidDataException("Invalid storage journal identity count.");
            var result=new SortedDictionary<int,StorageJournalAmount>();var previous=-1;
            for(var i=0;i<count;++i){var id=reader.ReadInt32();var value=StorageJournalAmount.Read(reader);if(id<=previous||id>=maximum||value.Sign<=0)throw new InvalidDataException("Invalid storage journal identity or amount.");result.Add(id,value);previous=id;}
            return result;
        }
    }
}
