using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    // Admission of the durable actor extension. Native C++ still validates the
    // enclosed solver image and its model before the original game loader runs.
    internal sealed class NativeActorCheckpointImage
    {
        internal sealed class Actor
        {
            internal ulong Owner;
            internal uint Prefab, Component, Kind, Cell, Species;
            internal double Mass, Interval, Temperature, MaximumDensity, Elapsed, Remainder;
            internal bool Blocked;
        }
        private static readonly byte[] Magic=Encoding.ASCII.GetBytes("MAACTR01");
        internal readonly Configuration Geometry;
        internal readonly IReadOnlyList<Actor> Actors;
        internal readonly byte[] SolverImage;
        private NativeActorCheckpointImage(Configuration geometry,Actor[] actors,byte[] solver)
        { Geometry=geometry;Actors=Array.AsReadOnly(actors);SolverImage=solver; }
        internal static bool HasMagic(ReadOnlySpan<byte> value)=>value.Length>=8 && value.Slice(0,8).SequenceEqual(Magic);
        private static void Require(bool valid,string message) { if(!valid) throw new InvalidDataException(message); }
        private static uint U(ReadOnlySpan<byte> value,int offset)=>BinaryPrimitives.ReadUInt32LittleEndian(value.Slice(offset,4));
        private static double D(ReadOnlySpan<byte> value,int offset)=>BitConverter.Int64BitsToDouble(BinaryPrimitives.ReadInt64LittleEndian(value.Slice(offset,8)));
        private static bool Finite(double value)=>!double.IsNaN(value) && !double.IsInfinity(value);
        internal static NativeActorCheckpointImage Parse(ReadOnlySpan<byte> value)
        {
            Require(value.Length>=64+88+32+64 && value.Length<=256<<20 && HasMagic(value) && U(value,8)==1,
                "Unsupported durable actor image.");
            var count=U(value,12);var physical=U(value,56);
            Require(count>0 && count<=256 && physical>=32 && U(value,60)==0 && 64L+88L*count+physical+64==value.Length,
                "Durable actor image sizes differ.");
            using(var hash=SHA256.Create())
            {
                var body=value.Slice(0,value.Length-64).ToArray();
                var digest=BitConverter.ToString(hash.ComputeHash(body)).Replace("-", "").ToLowerInvariant();
                Require(value.Slice(value.Length-64).SequenceEqual(Encoding.ASCII.GetBytes(digest)),"Durable actor image checksum differs.");
            }
            var g=new Configuration {Size=U(value,16),Version=U(value,20),Flags=U(value,24),GridWidth=U(value,28),X=U(value,32),Y=U(value,36),
                Width=U(value,40),Height=U(value,44),Scenario=U(value,48),Reserved=U(value,52)};
            var actors=new Actor[count];
            (uint kind,ulong owner,uint prefab,uint component) previous=default;
            for(var i=0;i<actors.Length;++i)
            {
                var start=64+88*i;
                var a=new Actor {Owner=BinaryPrimitives.ReadUInt64LittleEndian(value.Slice(start,8)),Prefab=U(value,start+8),Component=U(value,start+12),
                    Kind=U(value,start+16),Cell=U(value,start+20),Species=U(value,start+24),Mass=D(value,start+32),Interval=D(value,start+40),
                    Temperature=D(value,start+48),MaximumDensity=D(value,start+56),Elapsed=D(value,start+64),Remainder=D(value,start+72),Blocked=U(value,start+80)==1};
                Require(a.Owner>0 && a.Owner<=1UL+int.MaxValue && a.Prefab!=0 && a.Component<=65535 && (a.Kind==1 || a.Kind==2) && a.Species<5 &&
                    U(value,start+28)==0 && U(value,start+80)<=1 && U(value,start+84)==0,"Invalid saved actor identity, kind or flags.");
                Require(Finite(a.Mass) && Finite(a.Interval) && Finite(a.Temperature) && Finite(a.MaximumDensity) && Finite(a.Elapsed) && Finite(a.Remainder) &&
                    a.Mass>=0 && a.Elapsed>=0 && a.Remainder>=0,"Invalid saved actor timing or rate.");
                Require(a.Kind==1 ? a.Elapsed==0 && !a.Blocked : a.Remainder==0 && (a.Mass==0 ||
                    a.Interval>0 && a.Temperature>=1 && a.MaximumDensity>0 &&
                    (double)BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits((float)a.Mass))==a.Mass),"Invalid saved actor configuration.");
                var key=(a.Kind,a.Owner,a.Prefab,a.Component);
                Require(i==0 || previous.CompareTo(key)<0,"Saved actor identities are repeated or unordered.");
                previous=key;actors[i]=a;
            }
            return new NativeActorCheckpointImage(g,actors,value.Slice(64+88*actors.Length,checked((int)physical)).ToArray());
        }
    }
}
