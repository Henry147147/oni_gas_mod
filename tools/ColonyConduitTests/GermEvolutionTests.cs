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
        // An expected population is one binary64 value after every step (user decision 2026-11-24):
        // the exact result correctly rounded, with the rounding inside the booked change, so the
        // accounts still close exactly; a longer count that arrives (a move, an older save) is
        // rounded by its next step.
        {
            var arrived=PipeAmount.Zero;foreach(var part in new[]{1e6,3e-10,7e-40,5e-120,9e-250})arrived=arrived.Plus(new PipeAmount(part));
            Check(arrived.Words==5,"Long germ count fixture is not five words");
            var factor=Math.PI*1e-3;var mode=new GermPopulationClock(true,0);
            Check(ColonyGermPopulation.TryStep(arrived,mode,n=>throw new Exception("Expected culture called native integer callback"),n=>n.Times(factor),.999f,out var first),"Long expected population refused");
            var unrounded=arrived.Plus(arrived.Times(factor).Plus(arrived.Times((double).999f-1)));
            Check(first.Population.Words==1&&BitConverter.DoubleToInt64Bits(first.Population.Value)==BitConverter.DoubleToInt64Bits(unrounded.Value),"Expected population is not the exact result correctly rounded");
            Check(arrived.Plus(first.Change).Minus(first.Population).Sign==0,"Rounded population differs from population plus booked change");
            var culture=first.Population;var born=PipeAmount.Zero;var dead=PipeAmount.Zero;var longest=0;var random=new Random(11);
            for(var tick=0;tick<5000;++tick)
            {
                var rate=(random.NextDouble()-.5)*1e-2;var heat=(float)(1-random.NextDouble()*1e-3);
                Check(ColonyGermPopulation.TryStep(culture,mode,n=>throw new Exception("Expected culture called native integer callback"),n=>n.Times(rate),heat,out var next),"Expected population step refused");
                Check(next.Population.Words<=1&&culture.Plus(next.Change).Minus(next.Population).Sign==0,"Expected population grew beyond one word or left its account");
                if(next.Change.Sign>0)born=born.Plus(next.Change);else dead=dead.Minus(next.Change);
                longest=Math.Max(longest,Math.Max(born.Words,dead.Words));culture=next.Population;
                if(culture.Sign==0)break;
            }
            Check(first.Population.Plus(born).Minus(dead).Minus(culture).Sign==0,"Birth and death accounts of a rounded culture do not close exactly");
            Check(longest<=4,"Birth and death accounts of a rounded culture grow without bound: "+longest+" words");
            Check(ColonyGermPopulation.Rounded(arrived).Words==1&&ColonyGermPopulation.Rounded(new PipeAmount(2.5)).Value==2.5&&ColonyGermPopulation.Rounded(PipeAmount.Zero).Sign==0,"Rounding changed a one-word or empty count");
        }
        Check(ColonyGermPopulation.TryStep(new PipeAmount(.5),new GermPopulationClock(false,.75f),n=>0,n=>PipeAmount.Zero,1,out var transitioned)&&
            transitioned.Population.Value==1.25&&transitioned.Change.Value==.75&&transitioned.Clock.Expected,"Pending native residue was lost or left unbooked at expected-mode transition");
        Check(!ColonyGermPopulation.TryStep(new PipeAmount(double.MaxValue),new GermPopulationClock(true,0),n=>0,n=>n,2,out _),"Unrepresentable biological birth overflow was silently clipped");
        try{_ = Step(1,float.NaN,0,1);throw new Exception("Malformed rounding accepted");}catch(ArgumentOutOfRangeException){}
        Console.WriteLine("PASS common germ evolution: native integer parity, expected fractional/wide/tiny populations of one correctly rounded word, per-ID birth/death closure, extinction limiting, explicit overflow refusal and cold mode/rounding continuation (runtime inactive)");
    }
}
