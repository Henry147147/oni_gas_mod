using System;
using System.Buffers.Binary;
using System.IO;
using System.Numerics;
using System.Text;
using MixedAtmospheres.Oni;

internal static class RetirementInventoryTests
{
    internal static void Run()
    {
        static void Check(bool ok,string reason){if(!ok)throw new Exception(reason);}
        static PipeMaterial Material(int id)=>new PipeMaterial(id,1000,0);
        static byte Phase(int id)=>id==1||id==2?(byte)1:id==3?(byte)2:throw new ArgumentException("unknown material");
        static byte[] EncodeInventory(PipeInventory value)
        {using var s=new MemoryStream();using var w=new BinaryWriter(s);value.Write(w);return s.ToArray();}
        var gas=PipeInventory.Single(Material(1),1,300,0,.5).Joined(PipeInventory.Single(Material(1),1e-240,300)).Joined(PipeInventory.Single(Material(2),.25,300));
        var liquid=PipeInventory.Single(Material(3),2,300,1,12.25);
        byte[] Wire(bool badMechanical=false)
        {
            using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);
            writer.Write(Encoding.ASCII.GetBytes("MARET001"));writer.Write(7UL);writer.Write(19UL);writer.Write(42U);writer.Write(32U);writer.Write(32U);
            foreach(var inventory in new[]{gas,liquid})
            {
                inventory.Write(writer);
                writer.Write(1U);writer.Write(badMechanical?0:.75);PipeScaledValue.WriteExponent(writer,new BigInteger(100000));
                writer.Write(0U);writer.Write(0U);writer.Write(0U);new PipeAmount(-1e-240).Write(writer);
            }
            return stream.ToArray();
        }
        ColonyRetirementInventory Read(byte[] bytes,ulong session=7,ulong frame=19,uint origin=42)
            =>ColonyRetirementInventory.Read(bytes,session,frame,origin,32,32,Phase,Material);
        static void Reject(System.Action action)
        {
            try{action();}catch(Exception e)when(e is InvalidDataException||e is ArgumentException||e is EndOfStreamException){return;}
            throw new Exception("Malformed retirement inventory was accepted.");
        }
        var wire=Wire();var before=(byte[])wire.Clone();var parsed=Read(wire);
        Check(EncodeInventory(parsed.Phases[0].Inventory).AsSpan().SequenceEqual(EncodeInventory(gas)),"Retirement gas lost trace mass, exact U or fractional germs.");
        Check(EncodeInventory(parsed.Phases[1].Inventory).AsSpan().SequenceEqual(EncodeInventory(liquid)),"Retirement liquid lost a constituent or germ identity.");
        Check(!parsed.Phases[0].Kinetic.IsZero&&parsed.Phases[0].Remainder.Minus(new PipeAmount(-1e-240)).Sign==0,
            "Retirement projected full-range mechanics or dropped a precision energy remainder.");
        Reject(()=>Read(wire,session:8));Reject(()=>Read(wire,frame:20));Reject(()=>Read(wire,origin:43));
        Reject(()=>Read(Wire(true)));
        for(var length=0;length<wire.Length;++length)Reject(()=>Read(wire.AsSpan(0,length).ToArray()));
        var trailing=new byte[wire.Length+1];wire.CopyTo(trailing,0);Reject(()=>Read(trailing));
        var changed=(byte[])wire.Clone();BinaryPrimitives.WriteUInt32LittleEndian(changed.AsSpan(40),3);Reject(()=>Read(changed));
        changed=(byte[])wire.Clone();BinaryPrimitives.WriteUInt32LittleEndian(changed.AsSpan(44),uint.MaxValue);Reject(()=>Read(changed));
        Check(wire.AsSpan().SequenceEqual(before),"Retirement decode or refusal mutated the native source bytes.");
        Console.WriteLine("PASS retirement wire: exact two-phase/trace/germ inventories, wide mechanics, precision remainder, source identity and every truncation/malformed refusal");
    }
}
