using System;
using System.Buffers.Binary;
using System.IO;
using MixedAtmospheres.Oni;
using static MixedAtmospheres.Oni.OniNativeDomain;

internal static class NativeFrameInputJournalTests
{
    internal static void Run(System.Action<string,System.Action> test,System.Action<bool> check,System.Action<System.Action> reject)
    {
        var geometry=new Configuration{Size=40,Version=2,Flags=1,GridWidth=64,X=10,Y=10,Width=3,Height=2,Scenario=6};
        var source=new byte[32];source[0]=17;
        byte[] Rehash(byte[] bytes)
        {NativeFrameInputJournal.Digest(bytes.AsSpan(0,bytes.Length-64)).CopyTo(bytes,bytes.Length-64);return bytes;}
        byte[] Journal(bool commands,uint flags=0)
        {
            using var stream=new MemoryStream();using var w=new BinaryWriter(stream);
            w.Write(System.Text.Encoding.ASCII.GetBytes("MAFRIN01"));w.Write(1U);w.Write(flags);w.Write(4UL);w.Write(9UL);w.Write(10UL);w.Write(1.8);w.Write((double).2f);
            foreach(var v in new[]{geometry.Size,geometry.Version,geometry.Flags,geometry.GridWidth,geometry.X,geometry.Y,geometry.Width,geometry.Height,geometry.Scenario,geometry.Reserved}) w.Write(v);
            w.Write(commands ? 2U : 0U);w.Write(0U);w.Write(NativeFrameInputJournal.Digest(source));
            if(commands)
            {
                w.Write(1U);w.Write(1U);w.Write(48U);w.Write(1U);w.Write(4UL);w.Write(9UL);w.Write(650U);w.Write(3U);w.Write(12.0);w.Write(1000.0);
                w.Write(2U);w.Write(1U);w.Write(64U);w.Write(2U);w.Write(4UL);w.Write(9UL);w.Write(650U);w.Write(1U);w.Write(2U);w.Write(769U);w.Write(17);w.Write(0U);w.Write((double).02f);w.Write(0.0);
            }
            w.Write(new byte[64]);return Rehash(stream.ToArray());
        }
        NativeFrameInputJournal Parse(byte[] bytes)=>NativeFrameInputJournal.Parse(bytes,source,4,9,1.8,.2f,geometry);
        test("native accepted-input journal binds full source, epoch, clock, geometry and batch counts",()=> {
            var bytes=Journal(true);var held=(byte[])bytes.Clone();var j=Parse(bytes);
            check(j.ReplaySupported && j.HeatBatches==1 && j.TransferBatches==1 && j.HeatCommands==1 && j.TransferCommands==1);
            check(bytes.AsSpan().SequenceEqual(held));var empty=Parse(Journal(false));check(empty.ReplaySupported && empty.HeatCommands==0 && empty.TransferCommands==0);
            for(uint flags=1;flags<=3;++flags) check(!Parse(Journal(false,flags)).ReplaySupported);
        });
        test("valid-checksum input journals reject wrong epoch, geometry, source, reserved fields and command ABI",()=> {
            foreach(var offset in new[]{0,8,12,16,24,32,40,48,56,64,68,72,76,80,84,88,92,96,100,104,168,172,176,180,184,192,224,228,232,236,240,248})
            {var bad=Journal(true);bad[offset]^=0x80;reject(()=>Parse(Rehash(bad)));}
            var original=Journal(true);var wrong=(byte[])source.Clone();wrong[0]++;
            reject(()=>NativeFrameInputJournal.Parse(original,wrong,4,9,1.8,.2f,geometry));
            reject(()=>NativeFrameInputJournal.Parse(original,source,4,ulong.MaxValue,1.8,.2f,geometry));
        });
        test("input journals reject every truncated prefix, trailing bytes and corrupted checksum",()=> {
            var bytes=Journal(true);for(var n=0;n<bytes.Length;++n) {var prefix=bytes.AsSpan(0,n).ToArray();reject(()=>Parse(prefix));}
            bytes[^1]^=1;reject(()=>Parse(bytes));
            var extra=new byte[Journal(false).Length+1];Journal(false).CopyTo(extra,0);reject(()=>Parse(Rehash(extra)));
            reject(()=>Parse(new byte[NativeFrameInputJournal.MaximumBytes+1]));
        });
        test("physical input transcript ignores delivery identity and retains actual heat, mass, flags and timing",()=> {
            var original=Journal(true);var expected=Parse(original).PhysicalInputsSha256;
            var delivery=(byte[])original.Clone();foreach(var offset in new[]{16,184,240}) BinaryPrimitives.WriteUInt64LittleEndian(delivery.AsSpan(offset),11);
            BinaryPrimitives.WriteInt32LittleEndian(delivery.AsSpan(272),int.MinValue);
            var parsed=NativeFrameInputJournal.Parse(Rehash(delivery),source,11,9,1.8,.2f,geometry);
            check(parsed.PhysicalInputsSha256==expected);
            foreach(var offset in new[]{200,204,208,216,256,260,269,288}) {
                var changed=(byte[])original.Clone();changed[offset]^=1;
                check(Parse(Rehash(changed)).PhysicalInputsSha256!=expected);
            }
            var mass=(byte[])original.Clone();BinaryPrimitives.WriteInt64LittleEndian(mass.AsSpan(280),BitConverter.DoubleToInt64Bits((double).03f));
            check(Parse(Rehash(mass)).PhysicalInputsSha256!=expected);
            var timed=(byte[])original.Clone();BinaryPrimitives.WriteInt64LittleEndian(timed.AsSpan(48),BitConverter.DoubleToInt64Bits((double).1f));
            check(NativeFrameInputJournal.Parse(Rehash(timed),source,4,9,1.8,.1f,geometry).PhysicalInputsSha256!=expected);
        });
    }
}
