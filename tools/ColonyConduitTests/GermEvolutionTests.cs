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
        var clock=new GermPopulationClock(false,0);
        var exact=new PipeAmount(7);var scalar=new NativeGermStep(7,0,0);
        for(var frame=0;frame<100;++frame)
        {
            scalar=Step(scalar.Population,scalar.Rounding,.25f,1);
            Check(ColonyGermPopulation.TryStep(exact,clock,n=>.25f,n=>new PipeAmount(.25),1,out var extended),"Integer population wrapper refused native domain");
            Check(extended.Population.Value==scalar.Population&&!extended.Clock.Expected&&extended.Clock.NativeResidue==scalar.Rounding,"Expected population replaced the native integer clock");
            exact=extended.Population;clock=extended.Clock;
        }
        foreach(var quantity in new[]{.25,1e-240,1e30})
        {
            var population=new PipeAmount(quantity).Plus(new PipeAmount(1e-260));var before=population;
            var expectedClock=new GermPopulationClock(true,0);var births=PipeAmount.Zero;var deaths=PipeAmount.Zero;
            for(var tick=0;tick<20;++tick)
            {
                Check(ColonyGermPopulation.TryStep(population,expectedClock,n=>throw new Exception("Expected culture called native integer callback"),
                    n=>n.Times(tick%2==0?.25:-.25),1,out var update),"Fractional/wide population update refused");
                if(update.Change.Sign>0)births=births.Plus(update.Change);else deaths=deaths.Minus(update.Change);
                Check(update.Population.Sign>0&&update.Clock.Expected&&update.Clock.NativeResidue==0,"Expected-population mode or trace was lost");
                Check(population.Plus(update.Change).Minus(update.Population).Sign==0,"Biological change differs from exact population");
                population=update.Population;expectedClock=update.Clock;
                using var encoded=new MemoryStream();using(var writer=new BinaryWriter(encoded,System.Text.Encoding.UTF8,true)){population.Write(writer);expectedClock.Write(writer);}
                encoded.Position=0;using var reader=new BinaryReader(encoded);var resumedPopulation=PipeAmount.Read(reader);var resumed=GermPopulationClock.Read(reader);
                Check(resumed.Expected&&resumed.NativeResidue==0,"Expected-population mode changed across cold clock roundtrip");
                Check(ColonyGermPopulation.TryStep(resumedPopulation,resumed,n=>0,n=>n.Times(.125),.99f,out var cold)&&
                    ColonyGermPopulation.TryStep(population,expectedClock,n=>0,n=>n.Times(.125),.99f,out var uninterrupted)&&
                    cold.Population.Minus(uninterrupted.Population).Sign==0&&cold.Change.Minus(uninterrupted.Change).Sign==0,
                    "Cold exact population/mode changed the next biological flux");
            }
            Check(before.Plus(births).Minus(deaths).Minus(population).Sign==0,"Per-identity biological audit lost a fractional/wide tail");
            Check(ColonyGermPopulation.TryStep(population,expectedClock,n=>0,n=>new PipeAmount(-1e100),1,out var extinct)&&
                extinct.Population.Sign==0&&extinct.Change.Plus(population).Sign==0,"Absolute death did not stop at the entire exact available population");
        }
        Check(ColonyGermPopulation.TryStep(new PipeAmount(.5),new GermPopulationClock(false,.75f),n=>0,n=>PipeAmount.Zero,1,out var transitioned)&&
            transitioned.Population.Value==1.25&&transitioned.Change.Value==.75&&transitioned.Clock.Expected,"Pending native residue was lost or left unbooked at expected-mode transition");
        Check(!ColonyGermPopulation.TryStep(new PipeAmount(double.MaxValue),new GermPopulationClock(true,0),n=>0,n=>n,2,out _),"Unrepresentable biological birth overflow was silently clipped");
        try{_ = Step(1,float.NaN,0,1);throw new Exception("Malformed rounding accepted");}catch(ArgumentOutOfRangeException){}
        Console.WriteLine("PASS common germ evolution: native integer parity, expected fractional/wide/tiny populations, per-ID birth/death closure, extinction limiting, explicit overflow refusal and cold mode/rounding continuation (runtime inactive)");
    }
}
