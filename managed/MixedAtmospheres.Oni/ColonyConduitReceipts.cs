using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Numerics;
using System.Text;

namespace MixedAtmospheres.Oni
{
    internal readonly struct PipeScaledValue
    {
        internal readonly double Mantissa;
        internal readonly BigInteger Exponent;
        internal PipeScaledValue(double mantissa,BigInteger exponent)
        {
            if(double.IsNaN(mantissa)||double.IsInfinity(mantissa))throw new InvalidDataException("Nonfinite pipe mechanical quantity.");
            Mantissa=mantissa;Exponent=exponent;
        }
        internal static BigInteger ReadExponent(BinaryReader reader,int maximumBytes)
        {
            var length=reader.ReadUInt32();
            if(length==0||length>maximumBytes||length>reader.BaseStream.Length-reader.BaseStream.Position)
                throw new InvalidDataException("Invalid mechanical exponent length.");
            var bytes=reader.ReadBytes((int)length);
            foreach(var item in bytes)if(item!='-'&&(item<'0'||item>'9'))throw new InvalidDataException("Non-ASCII mechanical exponent.");
            var text=Encoding.ASCII.GetString(bytes);
            if(!BigInteger.TryParse(text,NumberStyles.AllowLeadingSign,CultureInfo.InvariantCulture,out var value)||
                value.ToString(CultureInfo.InvariantCulture)!=text)throw new InvalidDataException("Noncanonical mechanical exponent.");
            return value;
        }
        internal static void WriteExponent(BinaryWriter writer,BigInteger exponent)
        {
            var bytes=Encoding.ASCII.GetBytes(exponent.ToString(CultureInfo.InvariantCulture));writer.Write(bytes.Length);writer.Write(bytes);
        }
        internal static PipeScaledValue Read(BinaryReader reader,int maximumBytes)
        {var mantissa=reader.ReadDouble();return new PipeScaledValue(mantissa,ReadExponent(reader,maximumBytes));}
    }

    // Exact sparse radix-2^32 account. The bin coordinate itself is arbitrary
    // precision: a finite native mechanical quantity is never projected to a
    // binary64 value or an int64 exponent. Balanced signed digits permit exact
    // cancellation without an unbounded negative sign-extension chain.
    internal sealed class PipeScaledAccount
    {
        private readonly SortedDictionary<BigInteger,int> bins=new SortedDictionary<BigInteger,int>();
        private static readonly BigInteger Radix=BigInteger.One<<32,Half=BigInteger.One<<31;
        internal bool IsZero=>bins.Count==0;
        internal int Bins=>bins.Count;
        internal void Add(PipeScaledValue value)
        {
            if(value.Mantissa==0)return;
            var bits=(ulong)BitConverter.DoubleToInt64Bits(value.Mantissa);
            var biased=(int)((bits>>52)&2047);var significand=new BigInteger(bits&0xfffffffffffffUL);
            if(biased!=0)significand+=BigInteger.One<<52;
            if((bits>>63)!=0)significand=-significand;
            var exponent=value.Exponent+(biased==0?-1074:biased-1075);
            var bin=BigInteger.DivRem(exponent,32,out var remainder);
            if(remainder.Sign<0){--bin;remainder+=32;}
            AddBin(bin,significand<<(int)remainder);
        }
        internal PipeScaledAccount WithAdded(PipeScaledAccount other)
        {
            var result=new PipeScaledAccount();
            foreach(var bin in bins)result.bins.Add(bin.Key,bin.Value);
            foreach(var bin in other.bins)result.AddBin(bin.Key,bin.Value);
            return result;
        }
        private void AddBin(BigInteger index,BigInteger value)
        {
            while(!value.IsZero)
            {
                bins.TryGetValue(index,out var before);value+=before;
                var carry=BigInteger.DivRem(value,Radix,out var digit);
                if(digit>=Half){digit-=Radix;++carry;}else if(digit< -Half){digit+=Radix;--carry;}
                if(digit.IsZero)bins.Remove(index);else bins[index]=(int)digit;
                value=carry;++index;
            }
        }
        internal void Write(BinaryWriter writer)
        {
            writer.Write(bins.Count);foreach(var entry in bins){PipeScaledValue.WriteExponent(writer,entry.Key);writer.Write(entry.Value);}
        }
        internal static PipeScaledAccount Read(BinaryReader reader,int maximumBytes)
        {
            var count=reader.ReadInt32();
            if(count<0||(long)count*9>reader.BaseStream.Length-reader.BaseStream.Position)throw new InvalidDataException("Invalid pipe mechanical account count.");
            var result=new PipeScaledAccount();BigInteger? previous=null;
            for(var i=0;i<count;++i)
            {
                var index=PipeScaledValue.ReadExponent(reader,maximumBytes);var digit=reader.ReadInt32();
                if(digit==0||previous.HasValue&&index<=previous.Value)throw new InvalidDataException("Noncanonical pipe mechanical account.");
                result.bins.Add(index,digit);previous=index;
            }
            return result;
        }
    }

    internal readonly struct PipeReceiptPassive
    {
        internal readonly ulong Identity;internal readonly double Quantity;
        internal PipeReceiptPassive(ulong identity,double quantity){Identity=identity;Quantity=quantity;}
    }
    internal sealed class PipeReceiptParcel
    {
        internal uint NativeIndex,PublicCell;internal bool Injected;
        internal double Mass,Temperature,InternalEnergy;
        internal PipeScaledValue Kinetic,Potential,MomentumX,MomentumY;
        internal PipeReceiptPassive[] Passives=Array.Empty<PipeReceiptPassive>();
        internal PipeReceiptPassiveObservation[] Observations=Array.Empty<PipeReceiptPassiveObservation>();
    }
    internal readonly struct PipeReceiptPassiveObservation
    {
        internal readonly ulong Identity;internal readonly double Before,After;
        internal PipeReceiptPassiveObservation(ulong identity,double before,double after){Identity=identity;Before=before;After=after;}
    }
    internal sealed class PipeReceipt
    {
        internal ulong Sequence,CompletedFrame;
        internal int WireVersion;
        internal PipeReceiptParcel[] Parcels=Array.Empty<PipeReceiptParcel>();
        internal static PipeReceipt Decode(byte[] image,ulong expectedSequence)
        {
            if(image.Length<28||image.Length>ColonyConduitCheckpoint.MaximumBytes)throw new InvalidDataException("Invalid precise pipe receipt length.");
            try
            {
                using var stream=new MemoryStream(image,false);using var reader=new BinaryReader(stream);
                var magic=Encoding.ASCII.GetString(reader.ReadBytes(8));
                var version=magic=="MARECP01"?1:magic=="MARECP02"?2:throw new InvalidDataException("Unknown precise pipe receipt.");
                var result=new PipeReceipt{WireVersion=version,Sequence=reader.ReadUInt64(),CompletedFrame=reader.ReadUInt64()};
                if(result.Sequence!=expectedSequence)throw new InvalidDataException("Precise pipe receipt sequence mismatch.");
                var count=reader.ReadUInt32();
                if(count>(stream.Length-stream.Position)/92)throw new InvalidDataException("Invalid precise pipe parcel count.");
                result.Parcels=new PipeReceiptParcel[count];
                for(var i=0;i<count;++i)
                {
                    var parcel=new PipeReceiptParcel{NativeIndex=reader.ReadUInt32(),PublicCell=reader.ReadUInt32()};
                    var injected=reader.ReadUInt32();if(injected>1)throw new InvalidDataException("Invalid precise pipe parcel direction.");parcel.Injected=injected!=0;
                    parcel.Mass=reader.ReadDouble();parcel.Temperature=reader.ReadDouble();parcel.InternalEnergy=reader.ReadDouble();
                    if(!Finite(parcel.Mass)||parcel.Mass<0||!Finite(parcel.Temperature)||parcel.Mass>0&&parcel.Temperature<=0||!Finite(parcel.InternalEnergy))
                        throw new InvalidDataException("Invalid precise pipe parcel thermodynamics.");
                    parcel.Kinetic=PipeScaledValue.Read(reader,image.Length);parcel.Potential=PipeScaledValue.Read(reader,image.Length);
                    parcel.MomentumX=PipeScaledValue.Read(reader,image.Length);parcel.MomentumY=PipeScaledValue.Read(reader,image.Length);
                    var passives=reader.ReadUInt32();if(passives>(stream.Length-stream.Position)/16)throw new InvalidDataException("Invalid precise pipe passive count.");
                    parcel.Passives=new PipeReceiptPassive[passives];
                    var identities=new HashSet<ulong>();
                    for(var p=0;p<passives;++p)
                    {
                        var identity=reader.ReadUInt64();var quantity=reader.ReadDouble();
                        if(identity==0||!Finite(quantity)||quantity<0||!identities.Add(identity))throw new InvalidDataException("Invalid precise pipe passive.");
                        parcel.Passives[p]=new PipeReceiptPassive(identity,quantity);
                    }
                    if(version==2)
                    {
                        var observations=reader.ReadUInt32();
                        if(observations>(stream.Length-stream.Position)/24)throw new InvalidDataException("Invalid precise passive observation count.");
                        parcel.Observations=new PipeReceiptPassiveObservation[observations];ulong previous=0;
                        for(var p=0;p<observations;++p)
                        {
                            var identity=reader.ReadUInt64();var before=reader.ReadDouble();var after=reader.ReadDouble();
                            if(identity<=previous||!Finite(before)||!Finite(after)||before<0||after<0)throw new InvalidDataException("Invalid precise passive observation.");
                            parcel.Observations[p]=new PipeReceiptPassiveObservation(identity,before,after);previous=identity;
                        }
                    }
                    result.Parcels[i]=parcel;
                }
                if(stream.Position!=stream.Length)throw new InvalidDataException("Trailing precise pipe receipt bytes.");
                return result;
            }
            catch(EndOfStreamException error){throw new InvalidDataException("Truncated precise pipe receipt.",error);}
        }
        private static bool Finite(double value)=>!double.IsNaN(value)&&!double.IsInfinity(value);
    }
}
