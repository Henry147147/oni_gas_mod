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
    // The version follows the content; older versions keep their exact bytes:
    // 1 MACCLS01 image; 2 MAACTR01 actor wrapper; 3 a length-delimited version
    // 1 or 2 image followed by a nonempty MACO2P01 particle image (scenarios
    // 0..6). 4 a configured scenario-7 image: MADCFG01 (geometry, physics and
    // solid mask around MACCLS01), or MAACTR01 enclosing MADCFG01; 5 a
    // length-delimited version 4 image followed by a nonempty MACO2P01.
    internal sealed class NativeDomainCheckpointPayload
    {
        private const int HeaderBytes=88;
        internal const int ConfiguredFormatVersion=4,ConfiguredParticleFormatVersion=5;
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
        // Saved configured geometry, physics and mask (scenario 7 only).
        internal NativeConfiguredDomainImage? Configured { get; }
        internal int FormatVersion=>BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(8));
        private NativeDomainCheckpointPayload(byte[] image,ulong session,ulong frame,ulong epoch,double time,Configuration geometry,
            NativeTransientParticleImage? particles=null,NativeConfiguredDomainImage? configured=null)
        { bytes=image;physicalBytes=BinaryPrimitives.ReadInt32LittleEndian(image.AsSpan(84));Session=session;CompletedFrame=frame;NativeEpoch=epoch;PhysicalTime=time;Geometry=geometry;TransientParticles=particles;Configured=configured; }
        private static void Require(bool value,string message) { if (!value) throw new InvalidDataException(message); }
        private static NativeConfiguredDomainImage? Validate(ulong session,ulong frame,ulong epoch,double time,Configuration geometry,ReadOnlySpan<byte> physical)
        {
            Require(session>0 && epoch>0 && !double.IsNaN(time) && !double.IsInfinity(time) && time>0,"Invalid physical checkpoint source epoch/clock.");
            var configured=geometry.Scenario==ConfiguredScenario;
            if(configured) Require(NativeConfiguredDomainImage.ValidGeometry(geometry),"Invalid saved configured native domain geometry.");
            else Require(geometry.Size==40 && geometry.Version==2 && geometry.Flags==1 && geometry.Reserved==0 && geometry.Scenario<=6 &&
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
            NativeConfiguredDomainImage? image=null;
            if(configured)
            {
                // The wrapper is the saved configuration; it must name exactly
                // this domain, and no saved actor may sit on one of its solids.
                Require(NativeConfiguredDomainImage.HasMagic(physical),"A configured physical checkpoint requires its MADCFG01 configuration wrapper.");
                image=NativeConfiguredDomainImage.Parse(physical);
                Require(NativeConfiguredDomainImage.SameGeometry(image.Geometry,geometry),"Saved configured wrapper geometry differs from its physical domain.");
                if(actorImage!=null) foreach(var actor in actorImage.Actors) Require(image.IsFluidCell(actor.Cell),"A saved actor targets a configured solid or a cell outside its domain.");
                physical=image.SolverImage;
            }
            else Require(!NativeConfiguredDomainImage.HasMagic(physical),"A configured physical image requires the configured scenario.");
            Require(physical.Length>=32 && physical.Slice(0,8).SequenceEqual(System.Text.Encoding.ASCII.GetBytes("MACCLS01")),
                "Unsupported physical checkpoint format for this scenario.");
            var solverVersion=BinaryPrimitives.ReadUInt32LittleEndian(physical.Slice(8));
            var legacyVersion=configured ? (image!.Physics.Flags&PhysicsViscosity)!=0 ? 3U : 2U : geometry.Scenario==6 ? 3U : 2U;
            // Version 5 (gas diffusion) retains this metadata prefix and stores
            // both viscosity values, the numerical kernel, diffusion mode and the
            // transport profile. Native admission checks those complete settings
            // against the saved scenario/wrapper. Version 4 (interval kernel) is
            // never selected by the game and stays rejected here.
            Require(solverVersion==legacyVersion || solverVersion==5U,"Unsupported physical checkpoint format for this scenario.");
            var identity=BinaryPrimitives.ReadUInt32LittleEndian(physical.Slice(12)); var clock=16L+identity;
            Require(clock+42<=physical.Length-8,"Physical checkpoint metadata is truncated.");
            var offset=checked((int)clock);
            Require(BitConverter.Int64BitsToDouble(BinaryPrimitives.ReadInt64LittleEndian(physical.Slice(offset)))==time &&
                BinaryPrimitives.ReadUInt64LittleEndian(physical.Slice(offset+8))>0 &&
                BinaryPrimitives.ReadUInt32LittleEndian(physical.Slice(offset+16))==geometry.Width &&
                BinaryPrimitives.ReadUInt32LittleEndian(physical.Slice(offset+20))==geometry.Height,
                "Physical checkpoint clock or grid differs from its saved domain metadata.");
            return image;
        }
        // A saved CO2 parcel must originate in a configured fluid cell.
        private static void RequireFluidOrigins(NativeTransientParticleImage? particles,NativeConfiguredDomainImage? configured)
        {
            if(particles==null || configured==null) return;
            foreach(var particle in particles.Particles)
                Require(configured.IsFluidCell(particle.OriginCell),"A saved CO2 parcel originates in a configured solid.");
        }
        internal static NativeDomainCheckpointPayload Create(PairStatus source,ReadOnlySpan<byte> physical,NativeTransientParticleImage? particles=null)
        {
            Require(source.Size==112 && source.Version==1 && source.State==2 && source.PhysicalBytes==physical.Length,"Paired source is incomplete.");
            var configured=Validate(source.Session,source.CompletedFrame,source.NativeEpoch,source.PhysicalTime,source.Configuration,physical);
            Require(physical.Length<=ColonySaveEnvelope.MaximumPayloadBytes-HeaderBytes,"Physical domain section exceeds the envelope limit.");
            var transient=particles!=null && particles.Particles.Count>0 ? particles.CopyEncodedData() : Array.Empty<byte>();
            if(transient.Length>0) particles=NativeTransientParticleImage.Parse(transient,source.Configuration);else particles=null;
            RequireFluidOrigins(particles,configured);
            Require((long)physical.Length+HeaderBytes+(transient.Length>0 ? 4+transient.Length : 0)<=ColonySaveEnvelope.MaximumPayloadBytes,"Transient domain section exceeds the envelope limit.");
            var data=new byte[HeaderBytes+physical.Length+(transient.Length>0 ? 4+transient.Length : 0)]; Magic.CopyTo(data,0);
            BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(8),configured!=null ? transient.Length>0 ? ConfiguredParticleFormatVersion : ConfiguredFormatVersion :
                transient.Length>0 ? 3 : NativeActorCheckpointImage.HasMagic(physical) ? 2 : 1);
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
            return new NativeDomainCheckpointPayload(data,source.Session,source.CompletedFrame,source.NativeEpoch,source.PhysicalTime,g,particles,configured);
        }
        internal static NativeDomainCheckpointPayload Parse(ReadOnlySpan<byte> data)
        {
            Require(data.Length>=HeaderBytes+32 && data.Length<=ColonySaveEnvelope.MaximumPayloadBytes && data.Slice(0,8).SequenceEqual(Magic) &&
                U(data,8)>=1 && U(data,8)<=ConfiguredParticleFormatVersion,"Unsupported native-domain checkpoint section.");
            var version=U(data,8);var size=BinaryPrimitives.ReadInt32LittleEndian(data.Slice(84));
            var delimited=version==3 || version==ConfiguredParticleFormatVersion;var configuredFormat=version>=ConfiguredFormatVersion;
            Require(size>=32 && size<=data.Length-HeaderBytes,"Native-domain physical section length differs.");
            Require(delimited || (size==data.Length-HeaderBytes && (configuredFormat || version==(NativeActorCheckpointImage.HasMagic(data.Slice(HeaderBytes,size)) ? 2U : 1U))),
                "Saved actor extension differs from its enclosing format version.");
            var session=BinaryPrimitives.ReadUInt64LittleEndian(data.Slice(12));var frame=BinaryPrimitives.ReadUInt64LittleEndian(data.Slice(20));
            var epoch=BinaryPrimitives.ReadUInt64LittleEndian(data.Slice(28));var time=BitConverter.Int64BitsToDouble(BinaryPrimitives.ReadInt64LittleEndian(data.Slice(36)));
            var g=new Configuration{Size=U(data,44),Version=U(data,48),Flags=U(data,52),GridWidth=U(data,56),X=U(data,60),Y=U(data,64),Width=U(data,68),Height=U(data,72),Scenario=U(data,76),Reserved=U(data,80)};
            Require(configuredFormat==(g.Scenario==ConfiguredScenario),"Saved configured domain differs from its enclosing format version.");
            var configured=Validate(session,frame,epoch,time,g,data.Slice(HeaderBytes,size));
            NativeTransientParticleImage? particles=null;
            if(delimited) {
                var offset=HeaderBytes+size;Require(data.Length-offset>=4,"Missing transient CO2 section length.");
                var length=BinaryPrimitives.ReadInt32LittleEndian(data.Slice(offset));
                Require(length==data.Length-offset-4 && length>0,"Transient CO2 section length differs.");
                particles=NativeTransientParticleImage.Parse(data.Slice(offset+4,length),g);
                Require(particles.Particles.Count>0,"Empty transient CO2 section must use the legacy domain format.");
                RequireFluidOrigins(particles,configured);
            }
            return new NativeDomainCheckpointPayload(data.ToArray(),session,frame,epoch,time,g,particles,configured);
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
            int vacuumHash,int gasHash,int liquidHash,int wallHash,int? solidContactHash=null)
        {
            ValidateNativeSection(native);
            // Validate every native material, inventory, backwall and guard
            // before allowing the original loader to see the saved bytes.
            NativeWorldBlob.ValidatePreserved(native,phases,vacuumHash);
            if(Configured!=null) {ValidateConfiguredNativeInventory(native,phases,gasHash,liquidHash,wallHash);return;}
            Require(Geometry.Width==3 && Geometry.Height==2,"Only the six-cell sealed checkpoint has a native restore adapter.");
            var width=checked((int)Geometry.GridWidth+2);var count=checked(width*BinaryPrimitives.ReadInt32LittleEndian(native.Slice(16)));
            for(var y=(int)Geometry.Y-1;y<=(int)Geometry.Y+Geometry.Height;++y)
                for(var x=(int)Geometry.X-1;x<=(int)Geometry.X+Geometry.Width;++x)
                {
                    var index=x+1+(y+1)*width;var offset=29+16*index;
                    var boundary=x<Geometry.X || y<Geometry.Y || x>=Geometry.X+Geometry.Width || y>=Geometry.Y+Geometry.Height;
                    var hash=BinaryPrimitives.ReadInt32LittleEndian(native.Slice(offset));
                    var mass=BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(native.Slice(offset+8)));
                    var thermal=boundary && solidContactHash.HasValue && hash==solidContactHash.Value && x==(int)Geometry.X-1 && y==Geometry.Y;
                    Require(hash==(boundary ? thermal ? solidContactHash!.Value : wallHash : (Geometry.Scenario==1 || Geometry.Scenario==3) ? liquidHash : gasHash),"Saved sealed native material/boundary differs.");
                    if(boundary) {
                        Require(mass>0,"Saved sealed native boundary is empty.");
                        if(thermal) {
                            var temperature=BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(native.Slice(offset+4)));
                            Require(Geometry.X>=3 && mass==10f && temperature>=1 && temperature<=1e6 &&
                                phases.TryGetValue(hash,out var phase) && phase==NativeMatterPhase.Solid,
                                "Saved native solid contact has unsupported geometry, mass, temperature or phase.");
                            var back=index-1;
                            Require(BinaryPrimitives.ReadInt32LittleEndian(native.Slice(29+16*back))==wallHash &&
                                BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(native.Slice(29+16*back+8)))==20000f,
                                "Saved native solid contact lost its insulating backer.");
                        }
                    }
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
        // The saved native world must reproduce the saved configured room: its
        // solid mask is recomputed from the native cells' phases and must equal
        // the saved mask exactly. Border and interior solids are filled
        // Unobtanium with cleared native metadata (their insulating element
        // properties are checked against the live element table in the game);
        // fluid cells hold exactly the scene's compatibility projection.
        private void ValidateConfiguredNativeInventory(ReadOnlySpan<byte> native,IReadOnlyDictionary<int,NativeMatterPhase> phases,
            int gasHash,int liquidHash,int wallHash)
        {
            var room=Configured!;var physics=room.Physics;var liquid=(physics.Flags&PhysicsLiquid)!=0;
            Require((physics.Flags&PhysicsMixture)==0,"Saved configured mixtures have no native compatibility restore adapter.");
            Require(phases.TryGetValue(wallHash,out var wallPhase) && wallPhase==NativeMatterPhase.Solid,"The configured wall element is not a native solid.");
            var width=checked((int)Geometry.GridWidth+2);var count=checked(width*I(native,16));
            var w=(int)Geometry.Width;var h=(int)Geometry.Height;var loaded=new byte[w*h];
            for(var dy=0;dy<h;++dy) for(var dx=0;dx<w;++dx)
            {
                var index=(int)Geometry.X+dx+1+((int)Geometry.Y+dy+1)*width;
                loaded[dx+dy*w]=phases.TryGetValue(I(native,29+16*index),out var phase) && phase==NativeMatterPhase.Solid ? (byte)1 : (byte)0;
            }
            Require(room.SameMask(loaded),"Saved native world solids differ from the saved configured solid mask.");
            for(var y=(int)Geometry.Y-1;y<=(int)Geometry.Y+h;++y)
                for(var x=(int)Geometry.X-1;x<=(int)Geometry.X+w;++x)
                {
                    var index=x+1+(y+1)*width;var offset=29+16*index;var disease=29+16*count+8*index;
                    var boundary=x<Geometry.X || y<Geometry.Y || x>=Geometry.X+w || y>=Geometry.Y+h;
                    var hash=I(native,offset);var temperature=BitConverter.Int32BitsToSingle(I(native,offset+4));
                    var mass=BitConverter.Int32BitsToSingle(I(native,offset+8));
                    if(boundary) {Require(hash==wallHash && mass>0 && !float.IsInfinity(mass),"Saved configured native boundary is not a filled wall.");continue;}
                    var dx=x-(int)Geometry.X;var cleared=I(native,offset+12)==0 && I(native,disease)==0 && I(native,disease+4)==0;
                    if(loaded[dx+(y-(int)Geometry.Y)*w]!=0)
                        Require(hash==wallHash && mass>0 && !float.IsInfinity(mass) && temperature>0 && !float.IsInfinity(temperature) && cleared,
                            "Saved configured interior solid is not a filled wall cell with cleared native metadata.");
                    else
                        Require(hash==(liquid ? liquidHash : gasHash) && mass==ConfiguredDomainLayout.ProjectionMass(liquid,dx) &&
                            I(native,offset+4)==BitConverter.SingleToInt32Bits(ConfiguredDomainLayout.ProjectionTemperature(dx)) && cleared,
                            "Saved configured native compatibility cell contains unsupported inventory or metadata.");
                }
        }
        private static int I(ReadOnlySpan<byte> data,int offset)=>BinaryPrimitives.ReadInt32LittleEndian(data.Slice(offset));
        internal byte[] CopyEncodedData()=>(byte[])bytes.Clone();
        private static uint U(ReadOnlySpan<byte> data,int offset)=>BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(offset));
    }
}
