using System;
using System.IO;
using System.Linq;
using MixedAtmospheres.Oni;

internal static class BiologyStateTests
{
    private static void Check(bool condition,string message){if(!condition)throw new Exception(message);}
    private static byte[] Encode(PipeNativeCulture culture)
    {using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);culture.Write(writer);return stream.ToArray();}
    internal static void Run()
    {
        var geometry=new PipeGeometry(16,12,2,1,10,8);
        // A pipe allocated empty can later carry kilograms and germs. A cold
        // load must keep its zero allocation mass and an older cached rule,
        // independently of the current packet's mass/material.
        var growth=new PipeGrowthRule(17.25f,-300,float.PositiveInfinity,.125f,10,float.PositiveInfinity,4,3);
        var saved=new PipeNativeCulture(1,35,17,0,1,1000,.375f,growth);
        var bytes=Encode(saved);
        using(var stream=new MemoryStream(bytes))using(var reader=new BinaryReader(stream))
        {
            var restored=PipeNativeCulture.Read(reader,geometry);
            Check(restored.CapturedMass==0&&restored.CapturedElement==17&&restored.Disease==1&&restored.Count==1000&&restored.Residue==.375f,
                "Loaded packet state replaced a pipe culture's captured mass, cached element or native clock.");
            Check(restored.Growth.PopulationHalfLife==-300&&float.IsPositiveInfinity(restored.Growth.MaximumCountPerKg)&&
                restored.Growth.MinimumDiffusionCount==4&&restored.Growth.MinimumDiffusionTicks==3&&Encode(restored).SequenceEqual(bytes),
                "A cached pipe growth descriptor changed across cold continuation.");
        }
        var empty=new PipeNativeCulture(2,36,23,.5f,byte.MaxValue,0,0,default);
        using(var stream=new MemoryStream(Encode(empty)))using(var reader=new BinaryReader(stream))
            Check(PipeNativeCulture.Read(reader,geometry).CapturedMass==.5f,"A currently germ-free pipe lost its allocation context.");
        void Reject(Action action)
        {try{action();}catch(Exception error)when(error is InvalidDataException||error is EndOfStreamException){return;}throw new Exception("Invalid pipe culture context was accepted.");}
        Reject(()=>new PipeNativeCulture(1,35,17,float.PositiveInfinity,0,1,0,growth));
        Reject(()=>new PipeNativeCulture(1,35,17,0,byte.MaxValue,1,0,growth));
        Reject(()=>new PipeNativeCulture(1,35,17,0,0,1,1,growth));
        Reject(()=>new PipeGrowthRule(0,float.NaN,0,0,0,0,0,0));
        Reject(()=>{using var stream=new MemoryStream(bytes[..^1]);using var reader=new BinaryReader(stream);PipeNativeCulture.Read(reader,geometry);});
        Reject(()=>{using var stream=new MemoryStream(bytes);using var reader=new BinaryReader(stream);PipeNativeCulture.Read(reader,new PipeGeometry(16,12,8,1,4,8));});
        Console.WriteLine("PASS saved pipe biology context: allocation mass/cache and complete native growth/clock survive cold encoding, including germ-free pipes; malformed state refuses");
    }
}
