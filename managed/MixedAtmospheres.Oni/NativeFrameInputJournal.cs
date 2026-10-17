using System;
using System.Buffers.Binary;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    // Independently check framing and source identity before native replay.
    internal sealed class NativeFrameInputJournal
    {
        internal const uint MaximumBytes=1024*1024;
        internal uint Flags { get; private set; }
        internal int HeatBatches { get; private set; }
        internal int TransferBatches { get; private set; }
        internal int HeatCommands { get; private set; }
        internal int TransferCommands { get; private set; }
        internal bool ReplaySupported=>Flags==0;
        private static void Require(bool valid,string message) {if(!valid) throw new InvalidDataException(message);}
        private static uint U(ReadOnlySpan<byte> b,int p)=>BinaryPrimitives.ReadUInt32LittleEndian(b.Slice(p,4));
        private static ulong L(ReadOnlySpan<byte> b,int p)=>BinaryPrimitives.ReadUInt64LittleEndian(b.Slice(p,8));
        private static double D(ReadOnlySpan<byte> b,int p)=>BitConverter.Int64BitsToDouble((long)L(b,p));
        internal static byte[] Digest(ReadOnlySpan<byte> bytes)
        {
            using(var hash=SHA256.Create()) return Encoding.ASCII.GetBytes(BitConverter.ToString(hash.ComputeHash(bytes.ToArray())).Replace("-","").ToLowerInvariant());
        }
        internal static NativeFrameInputJournal Parse(ReadOnlySpan<byte> bytes,ReadOnlySpan<byte> source,
            ulong session,ulong sourceFrame,double sourceTime,double seconds,Configuration geometry)
        {
            Require(bytes.Length>=232 && bytes.Length<=MaximumBytes && bytes.Slice(0,8).SequenceEqual(Encoding.ASCII.GetBytes("MAFRIN01")),"Invalid frame input journal framing.");
            var body=bytes.Slice(0,bytes.Length-64);
            Require(bytes.Slice(bytes.Length-64).SequenceEqual(Digest(body)),"Frame input journal checksum differs.");
            Require(U(body,8)==1 && (U(body,12)&~3U)==0 && session>0 && sourceFrame<ulong.MaxValue &&
                L(body,16)==session && L(body,24)==sourceFrame && L(body,32)==sourceFrame+1 &&
                !double.IsNaN(sourceTime) && !double.IsInfinity(sourceTime) && sourceTime>=0 &&
                !double.IsNaN(seconds) && !double.IsInfinity(seconds) && seconds>0 && seconds<=1 &&
                D(body,40)==sourceTime && D(body,48)==seconds,"Frame input journal epoch, clock or version differs.");
            var g=geometry;var values=new[]{g.Size,g.Version,g.Flags,g.GridWidth,g.X,g.Y,g.Width,g.Height,g.Scenario,g.Reserved};
            for(var i=0;i<values.Length;++i) Require(U(body,56+4*i)==values[i],"Frame input geometry differs from the restored owner.");
            Require(body.Slice(104,64).SequenceEqual(Digest(source)),"Frame input journal belongs to another complete source snapshot.");
            var count=U(body,96);Require(count<=2048 && U(body,100)==0,"Invalid frame input batch count or reserved word.");
            var result=new NativeFrameInputJournal{Flags=U(body,12)};var offset=168;
            for(uint i=0;i<count;++i)
            {
                Require(offset<=body.Length-8,"Truncated frame input batch header.");
                var kind=U(body,offset);var commands=U(body,offset+4);offset+=8;
                Require((kind==1 || kind==2) && commands>0 && commands<=256,"Invalid frame input command kind or count.");
                var stride=kind==1 ? 48 : 64;
                Require(commands<=(body.Length-offset)/stride,"Truncated frame input commands.");
                if(kind==1) {++result.HeatBatches;result.HeatCommands+=checked((int)commands);}
                else {++result.TransferBatches;result.TransferCommands+=checked((int)commands);}
                Require(result.HeatCommands<=4096 && result.TransferCommands<=4096,"Frame input commands exceed live reservation bounds.");
                for(uint j=0;j<commands;++j,offset+=stride)
                    Require(U(body,offset)==stride && U(body,offset+4)==(kind==1 ? 1U : 2U) && L(body,offset+8)==session && L(body,offset+16)==sourceFrame,
                        "Frame input command differs from its source epoch or ABI.");
            }
            Require(offset==body.Length,"Frame input journal has trailing bytes.");return result;
        }
    }
}
