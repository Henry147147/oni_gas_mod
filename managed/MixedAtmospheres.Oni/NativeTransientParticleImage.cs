using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    // Mod-owned effect identity; this never substitutes a native/game object ID.
    internal readonly struct NativeTransientParticle
    {
        internal readonly ulong Identity;
        internal readonly uint OriginCell,Flags;
        internal readonly float X,Y,Z,VelocityX,VelocityY,VelocityZ,MassKg,Temperature,LifetimeRemaining,Rotation;
        internal NativeTransientParticle(ulong identity,uint origin,uint flags,ReadOnlySpan<float> values)
        {
            if(values.Length!=10) throw new InvalidDataException("Incomplete transient CO2 particle fields.");
            Identity=identity;OriginCell=origin;Flags=flags;X=values[0];Y=values[1];Z=values[2];
            VelocityX=values[3];VelocityY=values[4];VelocityZ=values[5];MassKg=values[6];Temperature=values[7];
            LifetimeRemaining=values[8];Rotation=values[9];
        }
        internal float[] Values()=>new[]{X,Y,Z,VelocityX,VelocityY,VelocityZ,MassKg,Temperature,LifetimeRemaining,Rotation};
    }

    // Bounded original serialization for the native game's unsaved CO2 effect
    // pool. Position/velocity/time are stored as their exact binary32 bits.
    // The enclosing paired save binds these records to its physical epoch.
    internal sealed class NativeTransientParticleImage
    {
        internal const int MaximumParticles=4096,RecordBytes=64;
        private const int HeaderBytes=24,DigestBytes=64;
        private static readonly byte[] Magic=Encoding.ASCII.GetBytes("MACO2P01");
        private readonly byte[] bytes;
        private readonly IReadOnlyList<NativeTransientParticle> particles;
        internal IReadOnlyList<NativeTransientParticle> Particles=>particles;
        private NativeTransientParticleImage(byte[] data,NativeTransientParticle[] values)
        {bytes=data;particles=Array.AsReadOnly(values);}
        private static void Require(bool valid,string message) {if(!valid) throw new InvalidDataException(message);}
        private static void Validate(NativeTransientParticle value,Configuration geometry)
        {
            Require(value.Identity>0 && value.Identity<ulong.MaxValue && value.Flags<=1,"Invalid transient CO2 identity or flags.");
            Require(geometry.GridWidth>0 && value.OriginCell%geometry.GridWidth>=geometry.X &&
                (ulong)(value.OriginCell%geometry.GridWidth)<(ulong)geometry.X+geometry.Width &&
                value.OriginCell/geometry.GridWidth>=geometry.Y &&
                (ulong)(value.OriginCell/geometry.GridWidth)<(ulong)geometry.Y+geometry.Height,
                "Transient CO2 origin is outside its physical owner.");
            foreach(var item in value.Values())
                Require(((uint)BitConverter.SingleToInt32Bits(item)&0x7f800000U)!=0x7f800000U,"Transient CO2 field is nonfinite.");
            // Integer classification retains positive subnormal particle mass
            // even when a Unity caller has DAZ/FTZ enabled.
            var mass=(uint)BitConverter.SingleToInt32Bits(value.MassKg);
            Require(mass>0 && mass<0x7f800000U && value.Temperature>=1 && value.Temperature<=1e6 &&
                value.X>=0 && (double)value.X<geometry.GridWidth && value.Y>=0 && (double)value.Y<uint.MaxValue &&
                value.LifetimeRemaining<=3,"Invalid transient CO2 mass, temperature, position or lifetime.");
        }
        internal static NativeTransientParticleImage Create(IReadOnlyList<NativeTransientParticle> values,Configuration geometry)
        {
            if(values==null) throw new ArgumentNullException(nameof(values));
            Require(values.Count<=MaximumParticles,"Transient CO2 particle limit exceeded.");
            var records=values.OrderBy(value=>value.Identity).ToArray();ulong previous=0;
            using(var stream=new MemoryStream()) {
                using(var writer=new BinaryWriter(stream,Encoding.UTF8,true)) {
                    writer.Write(Magic);writer.Write(1U);writer.Write((uint)RecordBytes);writer.Write((uint)records.Length);writer.Write(0U);
                    foreach(var item in records) {
                        Validate(item,geometry);Require(item.Identity>previous,"Duplicate transient CO2 identity.");previous=item.Identity;
                        writer.Write(item.Identity);writer.Write(item.OriginCell);writer.Write(item.Flags);
                        foreach(var value in item.Values()) writer.Write(BitConverter.SingleToInt32Bits(value));
                        writer.Write(0UL);
                    }
                }
                var body=stream.ToArray();stream.Write(NativeFrameInputJournal.Digest(body),0,DigestBytes);
                return new NativeTransientParticleImage(stream.ToArray(),records);
            }
        }
        internal static NativeTransientParticleImage Parse(ReadOnlySpan<byte> data,Configuration geometry)
        {
            Require(data.Length>=HeaderBytes+DigestBytes && data.Length<=HeaderBytes+RecordBytes*MaximumParticles+DigestBytes &&
                data.Slice(0,8).SequenceEqual(Magic) && U(data,8)==1 && U(data,12)==RecordBytes && U(data,20)==0,
                "Unsupported transient CO2 image framing.");
            var count=U(data,16);Require(count<=MaximumParticles && data.Length==HeaderBytes+(long)RecordBytes*count+DigestBytes,
                "Transient CO2 image length differs.");
            Require(data.Slice(data.Length-DigestBytes).SequenceEqual(NativeFrameInputJournal.Digest(data.Slice(0,data.Length-DigestBytes))),
                "Transient CO2 image checksum differs.");
            var records=new NativeTransientParticle[checked((int)count)];ulong previous=0;
            for(var index=0;index<records.Length;++index) {
                var offset=HeaderBytes+RecordBytes*index;var fields=new float[10];
                for(var i=0;i<fields.Length;++i) fields[i]=NativeBinary32.FromBits((int)U(data,offset+16+4*i));
                var item=new NativeTransientParticle(BinaryPrimitives.ReadUInt64LittleEndian(data.Slice(offset)),U(data,offset+8),U(data,offset+12),fields);
                Require(BinaryPrimitives.ReadUInt64LittleEndian(data.Slice(offset+56))==0 && item.Identity>previous,
                    "Transient CO2 image has reserved data or noncanonical identities.");
                Validate(item,geometry);records[index]=item;previous=item.Identity;
            }
            return new NativeTransientParticleImage(data.ToArray(),records);
        }
        internal void ValidateGrid(uint width,uint height)
        {
            Require(width>0 && height>0,"Missing transient CO2 native grid.");
            foreach(var item in particles) Require((double)item.X<width && (double)item.Y<height &&
                item.OriginCell/(ulong)width<height,"Transient CO2 particle escaped its saved native world.");
        }
        internal byte[] CopyEncodedData()=>(byte[])bytes.Clone();
        private static uint U(ReadOnlySpan<byte> data,int offset)=>BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(offset,4));
    }
}
