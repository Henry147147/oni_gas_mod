using System;
using System.Buffers.Binary;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using MixedAtmospheres.Oni;

internal static class ActorCheckpointImageTests
{
    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check,System.Action<System.Action> reject)
    {
        test("persistent native actors have an exact 96-byte ABI and real component registration contracts",()=> {
            check(Marshal.SizeOf<OniNativeDomain.PersistentActorCommand>()==96);
            foreach(var type in new[]{typeof(ElementConsumer),typeof(ElementEmitter)}) {
                var method=type.GetMethod("OnSimRegister",BindingFlags.Instance|BindingFlags.NonPublic|BindingFlags.DeclaredOnly);
                check(method!=null && method.ReturnType==typeof(void) && method.GetParameters().Length==1 &&
                    method.GetParameters()[0].ParameterType==typeof(HandleVector<Game.ComplexCallbackInfo<int>>.Handle));
            }
            check(typeof(ElementEmitter).GetProperty(nameof(ElementEmitter.isEmitterBlocked))!.GetSetMethod(true)!=null);
        });
        test("actor image retains stable identities timing rounding and a separately owned solver section",()=> {
            var image=Fixture();var retained=(byte[])image.Clone();var value=NativeActorCheckpointImage.Parse(image);
            check(value.Actors.Count==2 && value.Actors[0].Owner==101 && value.Actors[0].Kind==1 && value.Actors[0].Remainder==1e-10 &&
                value.Actors[1].Kind==2 && value.Actors[1].Elapsed==.3 && value.Actors[1].Blocked && value.Geometry.Width==3);
            Array.Clear(image);check(value.SolverImage.AsSpan(0,8).SequenceEqual(Encoding.ASCII.GetBytes("MACCLS01")));
            check(NativeActorCheckpointImage.Parse(retained).Actors[1].Elapsed==.3);
        });
        test("the real emitter exposes the zero-time SimComponent dirty-state configuration entry",()=> {
            var method=typeof(SimComponent).GetMethod(nameof(SimComponent.Sim200ms),new[]{typeof(float)});
            check(method!=null && !method.IsStatic && method.ReturnType==typeof(void) && method.GetMethodBody()!=null &&
                method.DeclaringType==typeof(SimComponent) && typeof(ElementEmitter).IsSubclassOf(typeof(SimComponent)));
        });
        test("actor image rejects corrupt hashes lengths and unsupported extension versions",()=> {
            var image=Fixture();var bad=(byte[])image.Clone();bad[100]^=1;reject(()=>NativeActorCheckpointImage.Parse(bad));
            reject(()=>NativeActorCheckpointImage.Parse(image.AsSpan(0,image.Length-1)));
            foreach(var offset in new[]{8,12,56,60}) {
                bad=(byte[])image.Clone();BinaryPrimitives.WriteUInt32LittleEndian(bad.AsSpan(offset),offset==12 ? 0U : uint.MaxValue);Rehash(bad);
                reject(()=>NativeActorCheckpointImage.Parse(bad));
            }
        });
        test("valid checksums do not admit invalid actor identities state or ordering",()=> {
            var original=Fixture();
            foreach(var fault in new[]{0,1,2,3,4,5,6,7,8}) {
                var bad=(byte[])original.Clone();
                if(fault==0) Array.Clear(bad,64,8);
                if(fault==1) Array.Clear(bad,72,4);
                if(fault==2) BinaryPrimitives.WriteUInt32LittleEndian(bad.AsSpan(76),65536);
                if(fault==3) BinaryPrimitives.WriteUInt32LittleEndian(bad.AsSpan(88),5);
                if(fault==4) D(bad,64+32,double.NaN);
                if(fault==5) D(bad,64+72,-1);
                if(fault==6) BinaryPrimitives.WriteUInt32LittleEndian(bad.AsSpan(64+88+80),2);
                if(fault==7) D(bad,64+88+32,.1); // Injection must be exactly binary32 compatible.
                if(fault==8) {var first=bad.AsSpan(64,88).ToArray();bad.AsSpan(152,88).CopyTo(bad.AsSpan(64));first.CopyTo(bad,152);}
                Rehash(bad);reject(()=>NativeActorCheckpointImage.Parse(bad));
            }
        });
        test("actor checkpoint payload is versioned and preserves the complete image for native admission",()=> {
            var image=Fixture();var g=NativeActorCheckpointImage.Parse(image).Geometry;
            var source=new OniNativeDomain.PairStatus {Size=112,Version=1,State=2,PhysicalBytes=(uint)image.Length,
                Session=1,CompletedFrame=62,NativeEpoch=2,PhysicalTime=12.4,Configuration=g};
            var payload=NativeDomainCheckpointPayload.Create(source,image);var encoded=payload.CopyEncodedData();
            check(BinaryPrimitives.ReadInt32LittleEndian(encoded.AsSpan(8))==2 && payload.PhysicalImage.SequenceEqual(image));
            check(NativeDomainCheckpointPayload.Parse(encoded).PhysicalImage.SequenceEqual(image));
            BinaryPrimitives.WriteInt32LittleEndian(encoded.AsSpan(8),1);reject(()=>NativeDomainCheckpointPayload.Parse(encoded));
            source.Configuration.X++;reject(()=>NativeDomainCheckpointPayload.Create(source,image));
        });
    }
    // A framing fixture only. Native C++ admits the complete thermodynamic
    // checkpoint; these managed tests deliberately do not claim to do that.
    private static byte[] Fixture()
    {
        var solver=new byte[128];Encoding.ASCII.GetBytes("MACCLS01").CopyTo(solver,0);
        BinaryPrimitives.WriteUInt32LittleEndian(solver.AsSpan(8),2);BinaryPrimitives.WriteUInt32LittleEndian(solver.AsSpan(12),1);solver[16]=(byte)'x';
        D(solver,17,12.4);BinaryPrimitives.WriteUInt64LittleEndian(solver.AsSpan(25),62);
        BinaryPrimitives.WriteUInt32LittleEndian(solver.AsSpan(33),3);BinaryPrimitives.WriteUInt32LittleEndian(solver.AsSpan(37),2);
        using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);
        writer.Write(Encoding.ASCII.GetBytes("MAACTR01"));writer.Write(1U);writer.Write(2U);
        foreach(var v in new uint[]{40,2,1,64,10,10,3,2,0,0}) writer.Write(v);
        writer.Write((uint)solver.Length);writer.Write(0U);
        foreach(var kind in new[]{1U,2U}) {
            writer.Write(100UL+kind);writer.Write(12345U);writer.Write(0U);writer.Write(kind);writer.Write(650U);writer.Write(1U);writer.Write(0U);
            foreach(var v in kind==1 ? new[]{.05,0,0,0,0,1e-10} : new[]{.03125,.7,300,.01,.3,0}) writer.Write(v);
            writer.Write(kind==2 ? 1U : 0U);writer.Write(0U);
        }
        writer.Write(solver);writer.Write(new byte[64]);writer.Flush();var result=stream.ToArray();Rehash(result);return result;
    }
    private static void D(byte[] bytes,int offset,double value)=>BinaryPrimitives.WriteInt64LittleEndian(bytes.AsSpan(offset),BitConverter.DoubleToInt64Bits(value));
    private static void Rehash(byte[] bytes)
    { Encoding.ASCII.GetBytes(Convert.ToHexStringLower(SHA256.HashData(bytes.AsSpan(0,bytes.Length-64)))).CopyTo(bytes,bytes.Length-64); }
}
