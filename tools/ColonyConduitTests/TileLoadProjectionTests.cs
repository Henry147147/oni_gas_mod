using System;
using System.IO;
using System.Linq;
using MixedAtmospheres.Oni;

internal static class TileLoadProjectionTests
{
    internal static void Run()
    {
        void Check(bool condition,string why){if(!condition)throw new Exception(why);}
        byte[] Message(int cell=7,ushort element=68,float mass=100,byte replacement=2)
        {
            using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);
            writer.Write(cell);writer.Write(0x12345678);writer.Write(350.25f);writer.Write(mass);writer.Write(9);
            writer.Write(element);writer.Write(replacement);writer.Write((byte)3);writer.Write(0x56789abc);return stream.ToArray();
        }
        byte[] Expected(int cell,float temperature,byte disease,int germs)
        {
            using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);
            writer.Write(cell);writer.Write(0x12345678);writer.Write(temperature);writer.Write(100f);writer.Write(germs);
            writer.Write((ushort)68);writer.Write((byte)2);writer.Write(disease);writer.Write(0x56789abc);return stream.ToArray();
        }
        var saved=new ColonyTileLoadProjection(7,68,100,MathF.BitIncrement(296.5f),2,1234567);
        var packet=Message();Check(saved.Rewrite(packet),"Saved tile recreation was not recognized.");
        Check(packet.SequenceEqual(Expected(7,MathF.BitIncrement(296.5f),2,1234567)),"Tile state changed a callback, material, mass, displacement/padding byte or lost exact thermal/culture state.");
        var other=new ColonyTileLoadProjection(8,68,100,MathF.BitDecrement(499.5f),255,0);
        packet=Message(8);Check(other.Rewrite(packet)&&packet.SequenceEqual(Expected(8,MathF.BitDecrement(499.5f),255,0)),"A multi-cell tile lost its independent cell temperature or empty culture.");
        foreach(var rejected in new[]{Message(8),Message(element:69),Message(mass:MathF.BitIncrement(100f)),Message(replacement:0),Message(replacement:1),Message().Take(27).ToArray(),Message().Concat(new byte[]{0}).ToArray()})
        {var original=rejected.ToArray();Check(!saved.Rewrite(rejected)&&rejected.SequenceEqual(original),"A different construction/edit was partially rewritten.");}
        void Reject(Action action)
        {try{action();}catch(ArgumentException){return;}throw new Exception("Invalid saved tile state accepted.");}
        foreach(var value in new[]{0f,-1f,float.NaN,float.PositiveInfinity})
        {Reject(()=>new ColonyTileLoadProjection(7,68,value,300,255,0));Reject(()=>new ColonyTileLoadProjection(7,68,100,value,255,0));}
        Reject(()=>new ColonyTileLoadProjection(-1,68,100,300,255,0));
        Reject(()=>new ColonyTileLoadProjection(7,68,100,300,255,1));
        Reject(()=>new ColonyTileLoadProjection(7,68,100,300,2,-1));
        Console.WriteLine("PASS saved tile recreation: exact independent cell temperature/culture, intact callback/material/mass/padding, unchanged unrelated edits and malformed-state refusal");
    }
}
