using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using MixedAtmospheres.Core.Interop;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    // Inner physical-domain section of MACOLONY. Native C++ validation still
    // must check the complete physical image before native Load or activation.
    internal sealed class NativeDomainCheckpointPayload
    {
        private const int HeaderBytes=88;
        private static readonly byte[] Magic=System.Text.Encoding.ASCII.GetBytes("MAONID01");
        private readonly byte[] bytes;
        private readonly int physicalBytes;
        internal ulong Session { get; }
        internal ulong CompletedFrame { get; }
        internal ulong NativeEpoch { get; }
        internal double PhysicalTime { get; }
        internal Configuration Geometry { get; }
        internal ReadOnlySpan<byte> PhysicalImage=>bytes.AsSpan(HeaderBytes,physicalBytes);
        internal NativeTransientParticleImage? TransientParticles { get; }
        private NativeDomainCheckpointPayload(byte[] image,ulong session,ulong frame,ulong epoch,double time,Configuration geometry,
            NativeTransientParticleImage? particles=null)
        { bytes=image;physicalBytes=BinaryPrimitives.ReadInt32LittleEndian(image.AsSpan(84));Session=session;CompletedFrame=frame;NativeEpoch=epoch;PhysicalTime=time;Geometry=geometry;TransientParticles=particles; }
        private static void Require(bool value,string message) { if (!value) throw new InvalidDataException(message); }
        private static void Validate(ulong session,ulong frame,ulong epoch,double time,Configuration geometry,ReadOnlySpan<byte> physical)
        {
            Require(session>0 && epoch>0 && !double.IsNaN(time) && !double.IsInfinity(time) && time>0,"Invalid physical checkpoint source epoch/clock.");
            Require(geometry.Size==40 && geometry.Version==2 && geometry.Flags==1 && geometry.Reserved==0 && geometry.Scenario<=6 &&
                geometry.Width>=2 && geometry.Width<=16 && geometry.Height>=2 && geometry.Height<=16 && geometry.X>0 && geometry.Y>0 &&
                (ulong)geometry.X+geometry.Width<geometry.GridWidth,"Invalid saved native domain geometry.");
            NativeActorCheckpointImage? actorImage=null;
            if(NativeActorCheckpointImage.HasMagic(physical))
            {
                actorImage=NativeActorCheckpointImage.Parse(physical);
                var g=actorImage.Geometry;
                Require(g.Size==geometry.Size && g.Version==geometry.Version && g.Flags==geometry.Flags && g.GridWidth==geometry.GridWidth &&
                    g.X==geometry.X && g.Y==geometry.Y && g.Width==geometry.Width && g.Height==geometry.Height && g.Scenario==geometry.Scenario && g.Reserved==0,
                    "Saved actor geometry differs from its physical domain.");
                physical=actorImage.SolverImage;
            }
            Require(physical.Length>=32 && physical.Slice(0,8).SequenceEqual(System.Text.Encoding.ASCII.GetBytes("MACCLS01")) &&
                BinaryPrimitives.ReadUInt32LittleEndian(physical.Slice(8))==(geometry.Scenario==6 ? 3U : 2U),"Unsupported physical checkpoint format for this scenario.");
            var identity=BinaryPrimitives.ReadUInt32LittleEndian(physical.Slice(12)); var clock=16L+identity;
            Require(clock+42<=physical.Length-8,"Physical checkpoint metadata is truncated.");
            var offset=checked((int)clock);
            Require(BitConverter.Int64BitsToDouble(BinaryPrimitives.ReadInt64LittleEndian(physical.Slice(offset)))==time &&
                BinaryPrimitives.ReadUInt64LittleEndian(physical.Slice(offset+8))>0 &&
                BinaryPrimitives.ReadUInt32LittleEndian(physical.Slice(offset+16))==geometry.Width &&
                BinaryPrimitives.ReadUInt32LittleEndian(physical.Slice(offset+20))==geometry.Height,
                "Physical checkpoint clock or grid differs from its saved domain metadata.");
        }
        internal static NativeDomainCheckpointPayload Create(PairStatus source,ReadOnlySpan<byte> physical,NativeTransientParticleImage? particles=null)
        {
            Require(source.Size==112 && source.Version==1 && source.State==2 && source.PhysicalBytes==physical.Length,"Paired source is incomplete.");
            Validate(source.Session,source.CompletedFrame,source.NativeEpoch,source.PhysicalTime,source.Configuration,physical);
            Require(physical.Length<=ColonySaveEnvelope.MaximumPayloadBytes-HeaderBytes,"Physical domain section exceeds the envelope limit.");
            var transient=particles!=null && particles.Particles.Count>0 ? particles.CopyEncodedData() : Array.Empty<byte>();
            if(transient.Length>0) particles=NativeTransientParticleImage.Parse(transient,source.Configuration);else particles=null;
            Require((long)physical.Length+HeaderBytes+(transient.Length>0 ? 4+transient.Length : 0)<=ColonySaveEnvelope.MaximumPayloadBytes,"Transient domain section exceeds the envelope limit.");
            var data=new byte[HeaderBytes+physical.Length+(transient.Length>0 ? 4+transient.Length : 0)]; Magic.CopyTo(data,0);
            BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(8),transient.Length>0 ? 3 : NativeActorCheckpointImage.HasMagic(physical) ? 2 : 1);
            BinaryPrimitives.WriteUInt64LittleEndian(data.AsSpan(12),source.Session);
            BinaryPrimitives.WriteUInt64LittleEndian(data.AsSpan(20),source.CompletedFrame);
            BinaryPrimitives.WriteUInt64LittleEndian(data.AsSpan(28),source.NativeEpoch);
            BinaryPrimitives.WriteInt64LittleEndian(data.AsSpan(36),BitConverter.DoubleToInt64Bits(source.PhysicalTime));
            var g=source.Configuration;var values=new[]{g.Size,g.Version,g.Flags,g.GridWidth,g.X,g.Y,g.Width,g.Height,g.Scenario,g.Reserved};
            for(var i=0;i<values.Length;++i) BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(44+4*i),values[i]);
            BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(84),physical.Length); physical.CopyTo(data.AsSpan(HeaderBytes));
            if(transient.Length>0) {
                BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(HeaderBytes+physical.Length),transient.Length);
                transient.CopyTo(data,HeaderBytes+physical.Length+4);
            }
            return new NativeDomainCheckpointPayload(data,source.Session,source.CompletedFrame,source.NativeEpoch,source.PhysicalTime,g,particles);
        }
        internal static NativeDomainCheckpointPayload Parse(ReadOnlySpan<byte> data)
        {
            Require(data.Length>=HeaderBytes+32 && data.Length<=ColonySaveEnvelope.MaximumPayloadBytes && data.Slice(0,8).SequenceEqual(Magic) &&
                (U(data,8)==1 || U(data,8)==2 || U(data,8)==3),"Unsupported native-domain checkpoint section.");
            var version=U(data,8);var size=BinaryPrimitives.ReadInt32LittleEndian(data.Slice(84));
            Require(size>=32 && size<=data.Length-HeaderBytes,"Native-domain physical section length differs.");
            Require(version==3 || (size==data.Length-HeaderBytes && version==(NativeActorCheckpointImage.HasMagic(data.Slice(HeaderBytes,size)) ? 2U : 1U)),
                "Saved actor extension differs from its enclosing format version.");
            var session=BinaryPrimitives.ReadUInt64LittleEndian(data.Slice(12));var frame=BinaryPrimitives.ReadUInt64LittleEndian(data.Slice(20));
            var epoch=BinaryPrimitives.ReadUInt64LittleEndian(data.Slice(28));var time=BitConverter.Int64BitsToDouble(BinaryPrimitives.ReadInt64LittleEndian(data.Slice(36)));
            var g=new Configuration{Size=U(data,44),Version=U(data,48),Flags=U(data,52),GridWidth=U(data,56),X=U(data,60),Y=U(data,64),Width=U(data,68),Height=U(data,72),Scenario=U(data,76),Reserved=U(data,80)};
            Validate(session,frame,epoch,time,g,data.Slice(HeaderBytes,size));
            NativeTransientParticleImage? particles=null;
            if(version==3) {
                var offset=HeaderBytes+size;Require(data.Length-offset>=4,"Missing transient CO2 section length.");
                var length=BinaryPrimitives.ReadInt32LittleEndian(data.Slice(offset));
                Require(length==data.Length-offset-4 && length>0,"Transient CO2 section length differs.");
                particles=NativeTransientParticleImage.Parse(data.Slice(offset+4,length),g);
                Require(particles.Particles.Count>0,"Empty transient CO2 section must use the legacy domain format.");
            }
            return new NativeDomainCheckpointPayload(data.ToArray(),session,frame,epoch,time,g,particles);
        }
        internal void ValidateNativeSection(ReadOnlySpan<byte> native)
        {
            Require(native.Length>=29 && native.Slice(0,8).SequenceEqual(System.Text.Encoding.ASCII.GetBytes("SIMSAVE\0")) &&
                BinaryPrimitives.ReadInt32LittleEndian(native.Slice(8))==15,"Unsupported native world in the paired checkpoint.");
            var width=BinaryPrimitives.ReadInt32LittleEndian(native.Slice(12));var height=BinaryPrimitives.ReadInt32LittleEndian(native.Slice(16));
            Require(width>2 && height>2 && native.Length==29L+36L*width*height && (ulong)width==(ulong)Geometry.GridWidth+2 &&
                (ulong)Geometry.Y+Geometry.Height<(ulong)height-2,"Native paired world does not contain its physical domain.");
            Require(BinaryPrimitives.ReadInt32LittleEndian(native.Slice(20))==0 && BinaryPrimitives.ReadInt32LittleEndian(native.Slice(24))==0,
                "Nonzero native world placement requires cluster restore staging.");
            TransientParticles?.ValidateGrid(Geometry.GridWidth,checked((uint)height-2));
        }
        internal void ValidateNativeInventory(ReadOnlySpan<byte> native,IReadOnlyDictionary<int,NativeMatterPhase> phases,
            int vacuumHash,int gasHash,int liquidHash,int wallHash)
        {
            ValidateNativeSection(native);
            // Validate every native material, inventory, backwall and guard
            // before allowing the original loader to see the saved bytes.
            NativeWorldBlob.ValidatePreserved(native,phases,vacuumHash);
            Require(Geometry.Width==3 && Geometry.Height==2,"Only the six-cell sealed checkpoint has a native restore adapter.");
            var width=checked((int)Geometry.GridWidth+2);var count=checked(width*BinaryPrimitives.ReadInt32LittleEndian(native.Slice(16)));
            for(var y=(int)Geometry.Y-1;y<=(int)Geometry.Y+Geometry.Height;++y)
                for(var x=(int)Geometry.X-1;x<=(int)Geometry.X+Geometry.Width;++x)
                {
                    var index=x+1+(y+1)*width;var offset=29+16*index;
                    var boundary=x<Geometry.X || y<Geometry.Y || x>=Geometry.X+Geometry.Width || y>=Geometry.Y+Geometry.Height;
                    var hash=BinaryPrimitives.ReadInt32LittleEndian(native.Slice(offset));
                    var mass=BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(native.Slice(offset+8)));
                    Require(hash==(boundary ? wallHash : (Geometry.Scenario==1 || Geometry.Scenario==3) ? liquidHash : gasHash),"Saved sealed native material/boundary differs.");
                    if(boundary) Require(mass>0,"Saved sealed native boundary is empty.");
                    else
                    {
                        var column=x-(int)Geometry.X;
                        var expectedMass=(Geometry.Scenario==1 || Geometry.Scenario==3) ? column==0 ? 1000f : column==1 ? 1200f : 1100f : column==0 ? .6f : column==1 ? 1.2f : 2.4f;
                        Require(mass==expectedMass && BinaryPrimitives.ReadInt32LittleEndian(native.Slice(offset+4))==BitConverter.SingleToInt32Bits(280+20*column) &&
                            BinaryPrimitives.ReadInt32LittleEndian(native.Slice(offset+12))==0 &&
                            BinaryPrimitives.ReadInt32LittleEndian(native.Slice(29+16*count+8*index))==0 &&
                            BinaryPrimitives.ReadInt32LittleEndian(native.Slice(29+16*count+8*index+4))==0,
                            "Saved native compatibility cell contains unsupported inventory or metadata.");
                    }
                }
        }
        internal byte[] CopyEncodedData()=>(byte[])bytes.Clone();
        private static uint U(ReadOnlySpan<byte> data,int offset)=>BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(offset));
    }
}
