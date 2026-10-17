using System;
using System.Buffers.Binary;
using MixedAtmospheres.Oni;
using static MixedAtmospheres.Oni.OniNativeDomain;

internal static class NativeTransientParticleImageTests
{
    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check,System.Action<System.Action> reject)
    {
        var geometry=new Configuration{Size=40,Version=2,Flags=1,GridWidth=64,X=10,Y=10,Width=3,Height=2};
        NativeTransientParticle Particle(ulong id=7,float mass=.03125f)=>new NativeTransientParticle(id,650,1,
            new[]{10.25f,10.5f,-.5f,.5f,-2.25f,0f,mass,333.25f,-.125f,-15.5f});
        byte[] Original()=>NativeTransientParticleImage.Create(new[]{Particle(9),Particle()},geometry).CopyEncodedData();
        byte[] Rehash(byte[] data) {NativeFrameInputJournal.Digest(data.AsSpan(0,data.Length-64)).CopyTo(data,data.Length-64);return data;}
        test("transient CO2 image retains exact mass motion and expired lifetime with stable owned effect IDs",()=> {
            var data=Original();var image=NativeTransientParticleImage.Parse(data,geometry);var held=image.CopyEncodedData();
            check(image.Particles.Count==2 && image.Particles[0].Identity==7 && image.Particles[1].Identity==9 &&
                image.Particles[0].VelocityY==-2.25f && image.Particles[0].LifetimeRemaining==-.125f && image.Particles[0].MassKg==.03125f);
            image.ValidateGrid(64,64);Array.Clear(data);check(image.CopyEncodedData().AsSpan().SequenceEqual(held));
            var empty=NativeTransientParticleImage.Create(Array.Empty<NativeTransientParticle>(),geometry);
            check(NativeTransientParticleImage.Parse(empty.CopyEncodedData(),geometry).Particles.Count==0);
        });
        test("transient CO2 serialization retains a positive binary32 subnormal without a mass floor",()=> {
            var image=NativeTransientParticleImage.Create(new[]{Particle(mass:float.Epsilon)},geometry);
            var value=NativeTransientParticleImage.Parse(image.CopyEncodedData(),geometry).Particles[0];
            check(BitConverter.SingleToInt32Bits(value.MassKg)==1);
        });
        test("transient CO2 images reject truncation invalid checksums identities records and coordinates",()=> {
            var original=Original();for(var size=0;size<original.Length;++size) {var data=original.AsSpan(0,size).ToArray();reject(()=>NativeTransientParticleImage.Parse(data,geometry));}
            var corrupt=(byte[])original.Clone();corrupt[^1]^=1;reject(()=>NativeTransientParticleImage.Parse(corrupt,geometry));
            foreach(var offset in new[]{8,12,16,20,32,36,80}) {
                var data=(byte[])original.Clone();BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(offset),uint.MaxValue);Rehash(data);
                reject(()=>NativeTransientParticleImage.Parse(data,geometry));
            }
            foreach(var item in new[]{(64,0f),(64,-1f),(68,float.NaN),(72,3.5f),(40,-1f),(44,float.PositiveInfinity)}) {
                var data=(byte[])original.Clone();BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(item.Item1),BitConverter.SingleToInt32Bits(item.Item2));Rehash(data);
                reject(()=>NativeTransientParticleImage.Parse(data,geometry));
            }
            var duplicate=(byte[])original.Clone();duplicate.AsSpan(24,8).CopyTo(duplicate.AsSpan(88));Rehash(duplicate);
            reject(()=>NativeTransientParticleImage.Parse(duplicate,geometry));
            reject(()=>NativeTransientParticleImage.Parse(original,geometry).ValidateGrid(64,10));
        });
        test("transient CO2 creation bounds the pool and rejects duplicate or invalid source identities",()=> {
            reject(()=>NativeTransientParticleImage.Create(new[]{Particle(0)},geometry));
            reject(()=>NativeTransientParticleImage.Create(new[]{Particle(ulong.MaxValue)},geometry));
            reject(()=>NativeTransientParticleImage.Create(new[]{Particle(),Particle()},geometry));
            reject(()=>NativeTransientParticleImage.Create(new NativeTransientParticle[NativeTransientParticleImage.MaximumParticles+1],geometry));
            var outside=geometry;outside.X++;
            reject(()=>NativeTransientParticleImage.Create(new[]{Particle()},outside));
        });
    }
}
