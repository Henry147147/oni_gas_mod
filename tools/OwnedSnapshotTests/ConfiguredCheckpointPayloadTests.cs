using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using MixedAtmospheres.Core.Interop;
using MixedAtmospheres.Oni;
using static MixedAtmospheres.Oni.OniNativeDomain;

// Paired-save framing of configured (scenario 7) rooms and the unchanged legacy
// framing. These are framing and admission fixtures; native C++ still admits
// the complete thermodynamic image and compares it with the wrapper.
internal static class ConfiguredCheckpointPayloadTests
{
    // SHA-256 of the legacy payloads below as produced by the unmodified
    // version 1..3 codec (git 596c17b). Configured support must not move them.
    private const string LegacyVersion1Sha256="15152846f58500eaec7517a661a70d7961f016d4a9df763f693a614cf2b93e85";
    private const string LegacyVersion2Sha256="e5ef1e60bb7a2eda2fc975197c97112801176d81536782987f5502bde9143242";
    private const string LegacyVersion3Sha256="01db54637b25a93f0f7426097962bf31f72be79c8fde0ce6107727aa6844ad7b";
    private const string LegacyViscousSha256="4e56f2b5385a4a9f63f6747778b65757ad575721c413d6231eb5e192847f7014";

    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check,System.Action<System.Action> reject)
    {
        test("legacy paired-save payload bytes are unchanged by the configured extension",()=> {
            var legacy=LegacyPayloads();
            check(Hash(legacy[0])==LegacyVersion1Sha256 && Hash(legacy[1])==LegacyVersion2Sha256 &&
                Hash(legacy[2])==LegacyVersion3Sha256 && Hash(legacy[3])==LegacyViscousSha256);
            // The documented version 1 layout, written independently.
            var solver=Solver(3,2,2);var expected=new byte[88+solver.Length];
            Encoding.ASCII.GetBytes("MAONID01").CopyTo(expected,0);BinaryPrimitives.WriteInt32LittleEndian(expected.AsSpan(8),1);
            BinaryPrimitives.WriteUInt64LittleEndian(expected.AsSpan(12),1);BinaryPrimitives.WriteUInt64LittleEndian(expected.AsSpan(20),62);
            BinaryPrimitives.WriteUInt64LittleEndian(expected.AsSpan(28),2);BinaryPrimitives.WriteInt64LittleEndian(expected.AsSpan(36),BitConverter.DoubleToInt64Bits(12.4));
            var g=LegacyGeometry(0);var values=new[]{g.Size,g.Version,g.Flags,g.GridWidth,g.X,g.Y,g.Width,g.Height,g.Scenario,g.Reserved};
            for(var i=0;i<values.Length;++i) BinaryPrimitives.WriteUInt32LittleEndian(expected.AsSpan(44+4*i),values[i]);
            BinaryPrimitives.WriteInt32LittleEndian(expected.AsSpan(84),solver.Length);solver.CopyTo(expected,88);
            check(legacy[0].AsSpan().SequenceEqual(expected));
            foreach(var (bytes,version) in legacy.Zip(new[]{1,2,3,1})) {
                var parsed=NativeDomainCheckpointPayload.Parse(bytes);
                check(parsed.Configured==null && parsed.FormatVersion==version && parsed.CopyEncodedData().AsSpan().SequenceEqual(bytes));
            }
        });
        test("diffusion, production and primary-U solver images survive paired-save framing with legacy configured actor and particle wrappers",()=> {
            foreach(var scenario in new[]{0U,6U,7U}) foreach(var withActor in new[]{false,true}) {
                var geometry=scenario==7 ? ConfiguredGeometry() : LegacyGeometry(scenario);
                foreach(var version in new[]{4U,5U,6U,7U,8U,9U,10U,11U,uint.MaxValue}) {
                    var solver=Solver(geometry.Width,geometry.Height,version);
                    var image=scenario==7 ? Wrapper(ConfiguredDomainLayout.PillaredRoom().Mask,ConfiguredDomainLayout.ScenePhysics(false),solverVersion:version) : solver;
                    if(withActor) image=ActorImage(image,geometry,10+10*64);
                    var pair=Pair(image,geometry);
                    if(version!=5 && version!=6 && version!=10) {reject(()=>NativeDomainCheckpointPayload.Create(pair,image));continue;}
                    foreach(var withParticle in new[]{false,true}) {
                        var particle=new NativeTransientParticle(9,10+10*64,0,new[]{10.5f,10.5f,-.5f,0f,-1f,0f,.03125f,333.25f,3f,0f});
                        var particles=withParticle ? NativeTransientParticleImage.Create(new[]{particle},geometry) : null;
                        var payload=NativeDomainCheckpointPayload.Create(pair,image,particles);
                        var encoded=payload.CopyEncodedData();var restored=NativeDomainCheckpointPayload.Parse(encoded);
                        var expectedVersion=scenario==7 ? withParticle ? 5 : 4 : withParticle ? 3 : withActor ? 2 : 1;
                        check(restored.Geometry.Scenario==scenario && restored.FormatVersion==expectedVersion &&
                            restored.PhysicalImage.SequenceEqual(image) && restored.CopyEncodedData().AsSpan().SequenceEqual(encoded) &&
                            (withParticle ? restored.TransientParticles!.Particles.Count==1 : restored.TransientParticles==null));
                    }
                }
            }
        });
        test("void-phase configured images retain their flag and admit solver versions 9 and 10 without enabling phase change",()=> {
            var room=ConfiguredDomainLayout.PillaredRoom();
            foreach(var version in new[]{9U,10U}) {
                var image=Wrapper(room.Mask,Physics.Create(PhysicsVacuum|PhysicsMixture),solverVersion:version);
                var payload=NativeDomainCheckpointPayload.Create(Pair(image,ConfiguredGeometry()),image);
                var restored=NativeDomainCheckpointPayload.Parse(payload.CopyEncodedData());
                check(restored.Configured!.Physics.Flags==(PhysicsVacuum|PhysicsMixture) &&
                    restored.PhysicalImage.SequenceEqual(image) && restored.CopyEncodedData().SequenceEqual(payload.CopyEncodedData()));
            }
            var phase=Wrapper(room.Mask,Physics.Create(PhysicsVacuum|PhysicsMixture),solverVersion:11);
            reject(()=>NativeDomainCheckpointPayload.Create(Pair(phase,ConfiguredGeometry()),phase));
        });
        test("configured wrapper exposes the exact saved geometry physics record mask and solver image",()=> {
            var room=ConfiguredDomainLayout.PillaredRoom();var image=Wrapper(room.Mask,ConfiguredDomainLayout.ScenePhysics(false));
            var value=NativeConfiguredDomainImage.Parse(image);
            check(NativeConfiguredDomainImage.SameGeometry(value.Geometry,ConfiguredGeometry()) && value.SameMask(room.Mask) &&
                NativeConfiguredDomainImage.SamePhysics(value.Physics,ConfiguredDomainLayout.ScenePhysics(false)) &&
                value.Physics.Size==40 && value.Physics.Version==1 && value.Physics.Flags==3 && value.Physics.GravityMetresPerSecondSquared==9.81);
            check(value.SolverImage.SequenceEqual(Solver(7,5,2)) && value.Mask.SequenceEqual(room.Mask));
            var copy=value.Mask;copy[0]=1;check(value.SameMask(room.Mask) && !value.SameMask(copy));
            // Public cells: the room starts at (10,10) in a 64-cell-wide grid.
            check(value.IsFluidCell(10+10*64) && !value.IsFluidCell(13+10*64) && !value.IsFluidCell(15+13*64) && value.IsFluidCell(16+14*64) &&
                !value.IsFluidCell(9+10*64) && !value.IsFluidCell(17+10*64) && !value.IsFluidCell(10+15*64));
            check(ConfiguredDomainLayout.IsSavedScene(value) && ConfiguredDomainLayout.IsSavedScene(
                NativeConfiguredDomainImage.Parse(Wrapper(room.Mask,ConfiguredDomainLayout.ScenePhysics(true)))));
        });
        test("configured wrapper rejects damaged and resealed geometry physics mask and sizes",()=> {
            var room=ConfiguredDomainLayout.PillaredRoom();var original=Wrapper(room.Mask,ConfiguredDomainLayout.ScenePhysics(false));
            var damaged=(byte[])original.Clone();damaged[200]^=1;reject(()=>NativeConfiguredDomainImage.Parse(damaged));
            reject(()=>NativeConfiguredDomainImage.Parse(original.AsSpan(0,original.Length-1)));
            reject(()=>NativeConfiguredDomainImage.Parse(original.Concat(new byte[]{0}).ToArray()));
            var faults=new List<System.Action<byte[]>> {
                bad=>U(bad,8,2),bad=>U(bad,12,34),bad=>U(bad,16+32,6),bad=>U(bad,16+24,8),bad=>U(bad,16+4,1),bad=>U(bad,16+36,1),
                bad=>U(bad,16+16,0),bad=>U(bad,16+12,15),bad=>U(bad,56,3|64),bad=>U(bad,56,3|8|16),bad=>U(bad,60,1),bad=>U(bad,92,1),
                bad=>D(bad,64,0),bad=>D(bad,64,100.5),bad=>D(bad,64,double.NaN),bad=>{U(bad,56,2);D(bad,64,-0.0);},
                bad=>D(bad,72,.5),bad=>{U(bad,56,3|4);},bad=>{U(bad,56,3|4);D(bad,72,1001);},bad=>D(bad,80,-0.0),
                bad=>U(bad,88,1000),bad=>bad[96+3]=2,bad=>Array.Fill(bad,(byte)1,96,35),bad=>bad[96+35]^=1,
            };
            foreach(var fault in faults) {var bad=(byte[])original.Clone();fault(bad);Rehash(bad);reject(()=>NativeConfiguredDomainImage.Parse(bad));}
            // Enabled viscosity with admitted values parses; the scene check still rejects it.
            var viscous=(byte[])original.Clone();U(viscous,56,3|4);D(viscous,72,.5);D(viscous,80,.25);Rehash(viscous);
            var parsed=NativeConfiguredDomainImage.Parse(viscous);check(parsed.Physics.Flags==7 && !ConfiguredDomainLayout.IsSavedScene(parsed));
        });
        test("only the configured scenes this adapter builds are admitted for restore, compared bitwise",()=> {
            var room=ConfiguredDomainLayout.PillaredRoom();
            bool Admits(byte[] mask,Physics physics)=>ConfiguredDomainLayout.IsSavedScene(NativeConfiguredDomainImage.Parse(Wrapper(mask,physics)));
            check(Admits(room.Mask,ConfiguredDomainLayout.ScenePhysics(false)) && Admits(room.Mask,ConfiguredDomainLayout.ScenePhysics(true)));
            var moved=room.Mask;moved[3]=0;moved[4]=1;check(!Admits(moved,ConfiguredDomainLayout.ScenePhysics(false)));
            var open=room.Mask;open[26]=0;check(!Admits(open,ConfiguredDomainLayout.ScenePhysics(true)));
            foreach(var physics in new[]{Physics.Create(PhysicsGravity|PhysicsConduction,9.8),Physics.Create(PhysicsGravity,9.81),
                Physics.Create(PhysicsConduction),Physics.Create(PhysicsGravity|PhysicsConduction|PhysicsMixture,9.81),
                Physics.Create(PhysicsGravity|PhysicsConduction|PhysicsViscosity,9.81,.5,.25),
                Physics.Create(PhysicsGravity|PhysicsConduction,BitConverter.Int64BitsToDouble(BitConverter.DoubleToInt64Bits(9.81)+1))})
                check(!Admits(room.Mask,physics));
            var wide=ConfiguredDomainLayout.FromSolids(5,7,(dx,dy)=>dx==2 && dy<3).Mask;
            check(!ConfiguredDomainLayout.IsSavedScene(NativeConfiguredDomainImage.Parse(Wrapper(wide,ConfiguredDomainLayout.ScenePhysics(false),5,7))));
        });
        test("configured paired payloads use additive versions four and five and retain the wrapper",()=> {
            var room=ConfiguredDomainLayout.PillaredRoom();var image=Wrapper(room.Mask,ConfiguredDomainLayout.ScenePhysics(true));
            var source=Pair(image,ConfiguredGeometry());
            var payload=NativeDomainCheckpointPayload.Create(source,image);var encoded=payload.CopyEncodedData();
            check(payload.FormatVersion==4 && BinaryPrimitives.ReadInt32LittleEndian(encoded.AsSpan(8))==4 && payload.PhysicalImage.SequenceEqual(image) &&
                encoded.Length==88+image.Length && payload.Configured!=null && payload.Configured.SameMask(room.Mask) &&
                (payload.Configured.Physics.Flags&PhysicsLiquid)!=0);
            var parsed=NativeDomainCheckpointPayload.Parse(encoded);
            check(parsed.FormatVersion==4 && parsed.Geometry.Scenario==7 && parsed.PhysicalImage.SequenceEqual(image) &&
                NativeConfiguredDomainImage.SamePhysics(parsed.Configured!.Physics,ConfiguredDomainLayout.ScenePhysics(true)) &&
                parsed.Configured.SameMask(room.Mask) && parsed.CopyEncodedData().AsSpan().SequenceEqual(encoded));
            // An empty particle image keeps the exact version 4 framing.
            var empty=NativeTransientParticleImage.Create(Array.Empty<NativeTransientParticle>(),ConfiguredGeometry());
            check(NativeDomainCheckpointPayload.Create(source,image,empty).CopyEncodedData().AsSpan().SequenceEqual(encoded));
            var fluid=new NativeTransientParticle(9,10+11*64,0,new[]{10.5f,11.5f,-.5f,0f,-1f,0f,.03125f,333.25f,3f,0f});
            var withParticle=NativeDomainCheckpointPayload.Create(source,image,NativeTransientParticleImage.Create(new[]{fluid},ConfiguredGeometry())).CopyEncodedData();
            var restored=NativeDomainCheckpointPayload.Parse(withParticle);
            check(BinaryPrimitives.ReadInt32LittleEndian(withParticle.AsSpan(8))==5 && restored.FormatVersion==5 && restored.PhysicalImage.SequenceEqual(image) &&
                restored.TransientParticles!.Particles.Count==1 && restored.Configured!.SameMask(room.Mask));
            foreach(var size in new[]{withParticle.Length-1,encoded.Length,encoded.Length+3}) reject(()=>NativeDomainCheckpointPayload.Parse(withParticle.AsSpan(0,size)));
            var solid=new NativeTransientParticle(9,13+10*64,0,new[]{13.5f,10.5f,-.5f,0f,-1f,0f,.03125f,333.25f,3f,0f});
            reject(()=>NativeDomainCheckpointPayload.Create(source,image,NativeTransientParticleImage.Create(new[]{solid},ConfiguredGeometry())));
            // Versions follow content: no legacy version names a configured
            // image, and no configured version names a legacy one.
            foreach(var version in new[]{1,2,3,5,6,0}) {
                var bad=(byte[])encoded.Clone();BinaryPrimitives.WriteInt32LittleEndian(bad.AsSpan(8),version);reject(()=>NativeDomainCheckpointPayload.Parse(bad));
            }
            var legacy=LegacyPayloads()[0];
            foreach(var version in new[]{4,5}) {
                var bad=(byte[])legacy.Clone();BinaryPrimitives.WriteInt32LittleEndian(bad.AsSpan(8),version);reject(()=>NativeDomainCheckpointPayload.Parse(bad));
            }
        });
        test("configured payloads reject a header that differs from the wrapper and unwrapped or legacy images",()=> {
            var room=ConfiguredDomainLayout.PillaredRoom();var image=Wrapper(room.Mask,ConfiguredDomainLayout.ScenePhysics(false));
            var source=Pair(image,ConfiguredGeometry());
            foreach(var change in new System.Action<Configuration[]>[]{g=>g[0].X++,g=>g[0].Y++,g=>g[0].GridWidth++,g=>g[0].Width=6,g=>g[0].Flags=0,g=>g[0].Scenario=6}) {
                var g=new[]{source.Configuration};change(g);var moved=source;moved.Configuration=g[0];
                reject(()=>NativeDomainCheckpointPayload.Create(moved,image));
            }
            // Scenario 7 requires the wrapper; a wrapper requires scenario 7.
            var solver=Solver(7,5,2);reject(()=>NativeDomainCheckpointPayload.Create(Pair(solver,ConfiguredGeometry()),solver));
            var legacyGeometry=LegacyGeometry(0);legacyGeometry.Width=7;legacyGeometry.Height=5;
            reject(()=>NativeDomainCheckpointPayload.Create(Pair(image,legacyGeometry),image));
            // The enclosed solver version follows the saved viscosity flag.
            var viscous=Wrapper(room.Mask,Physics.Create(PhysicsGravity|PhysicsConduction|PhysicsViscosity,9.81,.5,.25));
            reject(()=>NativeDomainCheckpointPayload.Create(Pair(viscous,ConfiguredGeometry()),viscous));
            var viscous3=Wrapper(room.Mask,Physics.Create(PhysicsGravity|PhysicsConduction|PhysicsViscosity,9.81,.5,.25),solverVersion:3);
            check(NativeDomainCheckpointPayload.Create(Pair(viscous3,ConfiguredGeometry()),viscous3).FormatVersion==4);
            // Clock and grid of the enclosed solver must match the header.
            var late=Pair(image,ConfiguredGeometry());late.PhysicalTime=12.6;reject(()=>NativeDomainCheckpointPayload.Create(late,image));
            var narrow=Wrapper(room.Mask,ConfiguredDomainLayout.ScenePhysics(false),solverWidth:6);
            reject(()=>NativeDomainCheckpointPayload.Create(Pair(narrow,ConfiguredGeometry()),narrow));
        });
        test("configured durable actor images wrap the configuration and cannot target a saved solid",()=> {
            var room=ConfiguredDomainLayout.PillaredRoom();var wrapper=Wrapper(room.Mask,ConfiguredDomainLayout.ScenePhysics(false));
            var actors=ActorImage(wrapper,ConfiguredGeometry(),10+10*64);var source=Pair(actors,ConfiguredGeometry());
            var payload=NativeDomainCheckpointPayload.Create(source,actors);
            check(payload.FormatVersion==4 && payload.PhysicalImage.SequenceEqual(actors) && payload.Configured!.SameMask(room.Mask) &&
                NativeDomainCheckpointPayload.Parse(payload.CopyEncodedData()).Configured!.SameMask(room.Mask));
            var onSolid=ActorImage(wrapper,ConfiguredGeometry(),13+10*64);reject(()=>NativeDomainCheckpointPayload.Create(Pair(onSolid,ConfiguredGeometry()),onSolid));
            var outside=ActorImage(wrapper,ConfiguredGeometry(),9+10*64);reject(()=>NativeDomainCheckpointPayload.Create(Pair(outside,ConfiguredGeometry()),outside));
            var otherHeader=ConfiguredGeometry();otherHeader.X=11;var moved=ActorImage(wrapper,otherHeader,11+10*64);
            reject(()=>NativeDomainCheckpointPayload.Create(Pair(moved,otherHeader),moved));
            var legacyInner=ActorImage(Solver(7,5,2),ConfiguredGeometry(),10+10*64);
            reject(()=>NativeDomainCheckpointPayload.Create(Pair(legacyInner,ConfiguredGeometry()),legacyInner));
        });
        test("configured native world admission recomputes the saved mask and requires exact compatibility tuples",()=> {
            foreach(var liquid in new[]{false,true}) {
                var room=ConfiguredDomainLayout.PillaredRoom();var image=Wrapper(room.Mask,ConfiguredDomainLayout.ScenePhysics(liquid));
                var payload=NativeDomainCheckpointPayload.Create(Pair(image,ConfiguredGeometry()),image);
                var native=World(room,liquid);var retained=(byte[])native.Clone();
                payload.ValidateNativeInventory(native,Phases,1,2,3,4,5);check(native.AsSpan().SequenceEqual(retained));
                var faults=new List<System.Action<byte[]>> {
                    bad=>Set(bad,13,10,liquid ? 3 : 2,ConfiguredDomainLayout.ProjectionMass(liquid,3),ConfiguredDomainLayout.ProjectionTemperature(3)), // a solid became fluid
                    bad=>Set(bad,11,10,4,20000,300),                       // a fluid cell became a solid
                    bad=>Set(bad,12,12,5,20000,300),                       // a different native solid in a fluid cell
                    bad=>Set(bad,15,13,2,1,300),                           // the ledge became gas
                    bad=>Set(bad,10,10,liquid ? 3 : 2,BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(ConfiguredDomainLayout.ProjectionMass(liquid,0))+1),280),
                    bad=>Set(bad,16,14,liquid ? 3 : 2,ConfiguredDomainLayout.ProjectionMass(liquid,6),311),
                    bad=>Set(bad,10,10,liquid ? 2 : 3,ConfiguredDomainLayout.ProjectionMass(liquid,0),280),
                    bad=>Set(bad,9,12,1,0,0),                              // an empty border cell
                    bad=>Set(bad,13,11,4,0,300),                           // an empty interior solid
                    bad=>Set(bad,13,11,4,20000,0),                         // an interior solid without temperature
                    bad=>Disease(bad,13,10,0,24),bad=>Disease(bad,12,12,0,1),bad=>Disease(bad,15,13,7,0),
                    bad=>Radiation(bad,15,13,1),bad=>Radiation(bad,11,11,.5f),
                };
                foreach(var fault in faults) {var bad=(byte[])retained.Clone();fault(bad);reject(()=>payload.ValidateNativeInventory(bad,Phases,1,2,3,4,5));}
                // A wrapper resealed with another mask no longer matches the world.
                var moved=room.Mask;moved[26]=0;moved[25]=1;var other=Wrapper(moved,ConfiguredDomainLayout.ScenePhysics(liquid));
                var otherPayload=NativeDomainCheckpointPayload.Create(Pair(other,ConfiguredGeometry()),other);
                reject(()=>otherPayload.ValidateNativeInventory(retained,Phases,1,2,3,4,5));
                // The other phase's physics names other compatibility cells.
                var swapped=Wrapper(room.Mask,ConfiguredDomainLayout.ScenePhysics(!liquid));
                reject(()=>NativeDomainCheckpointPayload.Create(Pair(swapped,ConfiguredGeometry()),swapped).ValidateNativeInventory(retained,Phases,1,2,3,4,5));
                // A mixture has no compatibility projection adapter.
                var mixture=Wrapper(room.Mask,Physics.Create(PhysicsGravity|PhysicsConduction|PhysicsMixture,9.81));
                reject(()=>NativeDomainCheckpointPayload.Create(Pair(mixture,ConfiguredGeometry()),mixture).ValidateNativeInventory(retained,Phases,1,2,3,4,5));
            }
        });
    }

    internal static readonly Dictionary<int,NativeMatterPhase> Phases=new Dictionary<int,NativeMatterPhase> {
        [1]=NativeMatterPhase.Vacuum,[2]=NativeMatterPhase.Gas,[3]=NativeMatterPhase.Liquid,[4]=NativeMatterPhase.Solid,[5]=NativeMatterPhase.Solid};
    private const int WorldWidth=66,WorldHeight=34,WorldCells=WorldWidth*WorldHeight;

    internal static Configuration LegacyGeometry(uint scenario)=>new Configuration{Size=40,Version=2,Flags=1,GridWidth=64,X=10,Y=10,Width=3,Height=2,Scenario=scenario};
    internal static Configuration ConfiguredGeometry()=>new Configuration{Size=40,Version=2,Flags=1,GridWidth=64,X=10,Y=10,Width=7,Height=5,Scenario=7};
    internal static PairStatus Pair(byte[] image,Configuration geometry)=>new PairStatus{Size=112,Version=1,State=2,PhysicalBytes=(uint)image.Length,
        Session=1,CompletedFrame=62,NativeEpoch=2,PhysicalTime=12.4,Configuration=geometry};
    // A framing-only MACCLS01 image: version, identity, clock, revision and grid.
    internal static byte[] Solver(uint width,uint height,uint version)
    {
        var solver=new byte[128];Encoding.ASCII.GetBytes("MACCLS01").CopyTo(solver,0);
        BinaryPrimitives.WriteUInt32LittleEndian(solver.AsSpan(8),version);BinaryPrimitives.WriteUInt32LittleEndian(solver.AsSpan(12),1);solver[16]=(byte)'x';
        D(solver,17,12.4);BinaryPrimitives.WriteUInt64LittleEndian(solver.AsSpan(25),62);
        BinaryPrimitives.WriteUInt32LittleEndian(solver.AsSpan(33),width);BinaryPrimitives.WriteUInt32LittleEndian(solver.AsSpan(37),height);
        return solver;
    }
    // The native MADCFG01 layout (oni_configured_domain.hpp).
    internal static byte[] Wrapper(byte[] mask,Physics physics,int width=7,int height=5,uint solverVersion=2,uint solverWidth=0)
    {
        var geometry=ConfiguredGeometry();geometry.Width=(uint)width;geometry.Height=(uint)height;
        var solver=Solver(solverWidth==0 ? (uint)width : solverWidth,(uint)height,solverVersion);
        using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);
        writer.Write(Encoding.ASCII.GetBytes("MADCFG01"));writer.Write(1U);writer.Write((uint)mask.Length);
        foreach(var v in new[]{geometry.Size,geometry.Version,geometry.Flags,geometry.GridWidth,geometry.X,geometry.Y,geometry.Width,geometry.Height,geometry.Scenario,geometry.Reserved}) writer.Write(v);
        writer.Write(physics.Flags);writer.Write(physics.Reserved);writer.Write(physics.GravityMetresPerSecondSquared);
        writer.Write(physics.ShearViscosityPaSeconds);writer.Write(physics.BulkViscosityPaSeconds);writer.Write((uint)solver.Length);writer.Write(0U);
        writer.Write(mask);writer.Write(solver);writer.Write(new byte[64]);writer.Flush();var result=stream.ToArray();Rehash(result);return result;
    }
    // A one-consumer MAACTR01 image enclosing the given physical section.
    internal static byte[] ActorImage(byte[] physical,Configuration geometry,uint cell)
    {
        using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);
        writer.Write(Encoding.ASCII.GetBytes("MAACTR01"));writer.Write(1U);writer.Write(1U);
        foreach(var v in new[]{geometry.Size,geometry.Version,geometry.Flags,geometry.GridWidth,geometry.X,geometry.Y,geometry.Width,geometry.Height,geometry.Scenario,geometry.Reserved}) writer.Write(v);
        writer.Write((uint)physical.Length);writer.Write(0U);
        writer.Write(101UL);writer.Write(12345U);writer.Write(0U);writer.Write(1U);writer.Write(cell);writer.Write(1U);writer.Write(0U);
        foreach(var v in new[]{.05,0,0,0,0,1e-10}) writer.Write(v);
        writer.Write(0U);writer.Write(0U);writer.Write(physical);writer.Write(new byte[64]);writer.Flush();var result=stream.ToArray();Rehash(result);return result;
    }
    // Deterministic legacy payloads for the byte-identity pin: version 1 (gas),
    // version 2 (actor wrapper), version 3 (actor wrapper and one CO2 parcel)
    // and version 1 of the viscous scenario 6.
    internal static byte[][] LegacyPayloads()
    {
        var g=LegacyGeometry(0);var solver=Solver(3,2,2);var actors=ActorImage(solver,g,10+10*64);
        var particle=new NativeTransientParticle(7,650,0,new[]{10.5f,10.5f,-.5f,0f,-1f,0f,.03125f,333.25f,3f,0f});
        var viscous=Solver(3,2,3);
        return new[]{
            NativeDomainCheckpointPayload.Create(Pair(solver,g),solver).CopyEncodedData(),
            NativeDomainCheckpointPayload.Create(Pair(actors,g),actors).CopyEncodedData(),
            NativeDomainCheckpointPayload.Create(Pair(actors,g),actors,NativeTransientParticleImage.Create(new[]{particle},g)).CopyEncodedData(),
            NativeDomainCheckpointPayload.Create(Pair(viscous,LegacyGeometry(6)),viscous).CopyEncodedData()};
    }
    // A 64x32 public world with the configured room at (10,10), its border and
    // interior solids filled Unobtanium (4) and fluid cells at their projection.
    private static byte[] World(ConfiguredDomainLayout room,bool liquid)
    {
        var native=new byte[29+36*WorldCells];
        Encoding.ASCII.GetBytes("SIMSAVE\0").CopyTo(native,0);BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(8),15);
        BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(12),WorldWidth);BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(16),WorldHeight);
        for(var i=0;i<WorldCells;++i) {
            BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(29+16*i),1);
            BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(29+24*WorldCells+12*i),1);
        }
        for(var y=9;y<=15;++y) for(var x=9;x<=17;++x) {
            var dx=x-10;var dy=y-10;var wall=dx<0 || dy<0 || dx>=7 || dy>=5 || room.IsSolid(dx,dy);
            Set(native,x,y,wall ? 4 : liquid ? 3 : 2,wall ? 20000 : ConfiguredDomainLayout.ProjectionMass(liquid,dx),
                wall ? 300 : ConfiguredDomainLayout.ProjectionTemperature(dx));
        }
        return native;
    }
    private static int Cell(int x,int y)=>x+1+(y+1)*WorldWidth;
    private static void Set(byte[] native,int x,int y,int hash,float mass,float temperature)
    {
        var offset=29+16*Cell(x,y);BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(offset),hash);
        BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(offset+4),BitConverter.SingleToInt32Bits(temperature));
        BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(offset+8),BitConverter.SingleToInt32Bits(mass));
    }
    private static void Disease(byte[] native,int x,int y,int hash,int count)
    {
        var offset=29+16*WorldCells+8*Cell(x,y);
        BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(offset),hash);BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(offset+4),count);
    }
    private static void Radiation(byte[] native,int x,int y,float value)=>
        BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(29+16*Cell(x,y)+12),BitConverter.SingleToInt32Bits(value));
    private static void U(byte[] bytes,int offset,uint value)=>BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(offset),value);
    private static void D(byte[] bytes,int offset,double value)=>BinaryPrimitives.WriteInt64LittleEndian(bytes.AsSpan(offset),BitConverter.DoubleToInt64Bits(value));
    private static string Hash(byte[] bytes)=>Convert.ToHexStringLower(SHA256.HashData(bytes));
    private static void Rehash(byte[] bytes)
    { Encoding.ASCII.GetBytes(Convert.ToHexStringLower(SHA256.HashData(bytes.AsSpan(0,bytes.Length-64)))).CopyTo(bytes,bytes.Length-64); }
}
