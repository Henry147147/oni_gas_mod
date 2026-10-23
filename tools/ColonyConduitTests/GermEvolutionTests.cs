using System;
using System.IO;
using MixedAtmospheres.Oni;

static class GermEvolutionTests
{
    static void Check(bool okay,string why){if(!okay)throw new Exception(why);}
    static NativeGermStep Step(int count,float rounding,float material,float temperature)
    {Check(ColonyGermEvolution.TryNativeStep(count,rounding,material,temperature,out var result),"Supported native germ step refused");return result;}
    internal static void Run()
    {
        foreach(var direction in new[]{-1,1})
        {
            var state=new NativeGermStep(7,0,0);var change=0;
            for(var frame=0;frame<4;++frame){state=Step(state.Population,state.Rounding,direction*.25f,1);change+=state.Change;}
            Check(state.Population==7+direction&&state.Rounding==0&&change==direction,"Fractional native birth/death accumulation lost an integer event");
        }
        var half=new NativeGermStep(8,0,0);var died=0;
        foreach(var expected in new[]{4,2,1,1,0})
        {half=Step(half.Population,half.Rounding,0,.5f);died-=half.Change;Check(half.Population==expected,"Temperature death trajectory changed");}
        Check(died==8&&half.Rounding==0,"Extinction retained rounding debt or deleted unbooked population");
        var overkill=Step(3,-.75f,-100,0);Check(overkill.Population==0&&overkill.Change==-3&&overkill.Rounding==0,"Native extinction was not limited to existing population");
        var large=Step(16777219,0,0,1);Check(large.Population==16777219&&large.Change==0,"Binary32 population conversion replaced the exact integer count");
        var maximum=Step(int.MaxValue,0,0,1);Check(maximum.Population==int.MaxValue,"Maximum native population changed at equilibrium");
        Check(!ColonyGermEvolution.TryNativeStep(int.MaxValue,0,1,1,out _)&&
            !ColonyGermEvolution.TryNativeStep(100,0,float.MaxValue,float.MaxValue,out _),"Unsupported native overflow was silently wrapped or clipped");
        var live=new NativeGermStep(10000,0,0);long ledger=0;
        for(var frame=0;frame<1000;++frame)
        {
            var previous=live.Population;live=Step(live.Population,live.Rounding,frame%3==0?.17f:-.35f,.999f);ledger+=live.Change;
            Check((long)previous+live.Change==live.Population,"Birth/death account differs from actual population change");
            using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true)){writer.Write(live.Population);writer.Write(live.Rounding);}
            stream.Position=0;using var reader=new BinaryReader(stream);var cold=new NativeGermStep(reader.ReadInt32(),reader.ReadSingle(),0);
            var resumed=Step(cold.Population,cold.Rounding,.17f,.999f);var uninterrupted=Step(live.Population,live.Rounding,.17f,.999f);
            Check(resumed.Population==uninterrupted.Population&&BitConverter.SingleToInt32Bits(resumed.Rounding)==BitConverter.SingleToInt32Bits(uninterrupted.Rounding),"Native rounding continuation changed after cold serialization");
        }
        Check(10000+ledger==live.Population,"Accumulated biological ledger failed");
        try{_ = Step(1,float.NaN,0,1);throw new Exception("Malformed rounding accepted");}catch(ArgumentOutOfRangeException){}
        Console.WriteLine("PASS common native germ step: integer birth/death, temperature extinction, exact count changes, overflow refusal and binary32 cold continuation (runtime inactive)");
    }
}
