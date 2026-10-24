using System;
using System.Diagnostics;
using System.IO;
using MixedAtmospheres.Oni;

static class BiologyContinuation
{
    sealed class Case
    {
        internal readonly string Name;internal readonly PipeAmount Seed;
        internal readonly Func<int,float> Native;internal readonly Func<PipeAmount,PipeAmount> Expected;
        internal readonly float Temperature;
        internal Case(string name,PipeAmount seed,float material,float temperature)
        {Name=name;Seed=seed;Native=n=>(float)n*material;Expected=n=>n.Times(material);Temperature=temperature;}
        internal Case(string name,PipeAmount seed,Func<int,float> native,Func<PipeAmount,PipeAmount> expected,float temperature)
        {Name=name;Seed=seed;Native=native;Expected=expected;Temperature=temperature;}
    }
    static byte[] Image(PipeAmount population,GermPopulationClock clock,PipeAmount births,PipeAmount deaths)
    {
        using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true))
        {population.Write(writer);clock.Write(writer);births.Write(writer);deaths.Write(writer);}return stream.ToArray();
    }
    static int Atoms(PipeAmount amount)
    {using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true))amount.Write(writer);return BitConverter.ToInt32(stream.ToArray());}
    static void Check(bool okay,string message){if(!okay)throw new Exception(message);}
    internal static void Run()
    {
        const int ticks=30000,window=1000;
        var cases=new[]{
            new Case("native-integer",new PipeAmount(2000000),0,.99999f),
            new Case("fractional",new PipeAmount(.25),.0002f,.9997f),
            new Case("wide-plus-tiny",new PipeAmount(1e30).Plus(new PipeAmount(1e-240)),-.00005f,1),
            new Case("tiny-decay",new PipeAmount(1e-240),0,.999f),
            new Case("tiny-growth",new PipeAmount(1e-240),0,1.0001f),
            new Case("fractional-absolute-death",new PipeAmount(10.5),n=>-.0001f,n=>new PipeAmount(-.0001f),1),
            new Case("mixed-material",new PipeAmount(1000.25),n=>(float)(.6*((float)n*.0001f)+.4*((float)n*-.0004f)),
                n=>n.Times(.0001f).Times(.6).Plus(n.Times(-.0004f).Times(.4)),.9999f)
        };
        // Warm code paths on throwaway state. Reported first/last windows then
        // include real evolving representation cost, rather than first-use JIT.
        foreach(var item in cases)
        {
            var pop=item.Seed;var mode=new GermPopulationClock(false,0);
            for(var i=0;i<200;++i)
            {Check(ColonyGermPopulation.TryStep(pop,mode,item.Native,item.Expected,item.Temperature,out var step),"Warm culture refused");pop=step.Population;mode=step.Clock;}
        }
        Console.WriteLine("BIOLOGY CONTINUATION: 30000 fixed 0.2s ticks (6000 game seconds), product/clock model only; explicit supported proportional/absolute material fluxes and f32 temperature factors, no native DB lookup or conduit/oracle overhead.");
        foreach(var item in cases)
        {
            var pop=item.Seed;var mode=new GermPopulationClock(false,0);var born=PipeAmount.Zero;var died=PipeAmount.Zero;
            var timer=Stopwatch.StartNew();var started=Stopwatch.GetTimestamp();long allocated=GC.GetAllocatedBytesForCurrentThread();
            double early=0,late=0;long earlyBytes=0,lateBytes=0;var maxAtoms=0;var maxImage=0;
            for(var tick=1;tick<=ticks;++tick)
            {
                Check(pop.Sign>0,"Stress case unexpectedly retired before the full cadence bound");
                if(!ColonyGermPopulation.TryStep(pop,mode,item.Native,item.Expected,item.Temperature,out var step))
                    throw new Exception("Continuation refused "+item.Name+" at "+tick);
                pop=step.Population;mode=step.Clock;
                if(step.Change.Sign>0)born=born.Plus(step.Change);else died=died.Minus(step.Change);
                if(tick==window)
                {early=1e6*(Stopwatch.GetTimestamp()-started)/Stopwatch.Frequency/window;earlyBytes=(GC.GetAllocatedBytesForCurrentThread()-allocated)/window;}
                if(tick==ticks)
                {late=1e6*(Stopwatch.GetTimestamp()-started)/Stopwatch.Frequency/window;lateBytes=(GC.GetAllocatedBytesForCurrentThread()-allocated)/window;}
                if(tick%window==0)
                {
                    Check(item.Seed.Plus(born).Minus(died).Minus(pop).Sign==0,"Biological audit drift "+item.Name+" at "+tick);
                    var image=Image(pop,mode,born,died);var atoms=Atoms(pop);maxAtoms=Math.Max(maxAtoms,atoms);maxImage=Math.Max(maxImage,image.Length);
                    // Resume the entire exact population/clock/journal image;
                    // compare its next flux, not just its rounded readout.
                    using var stream=new MemoryStream(image);using var reader=new BinaryReader(stream);
                    var restored=PipeAmount.Read(reader);var restoredMode=GermPopulationClock.Read(reader);var restoredBorn=PipeAmount.Read(reader);var restoredDied=PipeAmount.Read(reader);
                    var coldOkay=ColonyGermPopulation.TryStep(restored,restoredMode,item.Native,item.Expected,item.Temperature,out var cold);
                    var liveOkay=ColonyGermPopulation.TryStep(pop,mode,item.Native,item.Expected,item.Temperature,out var live);
                    Check(coldOkay&&liveOkay&&cold.Population.Minus(live.Population).Sign==0&&cold.Change.Minus(live.Change).Sign==0&&
                        restoredBorn.Minus(born).Sign==0&&restoredDied.Minus(died).Sign==0,"Cold culture continuation changed "+item.Name);
                    PipeAmount NextBorn(PipeAmount previous,GermPopulationStep next)=>next.Change.Sign>0?previous.Plus(next.Change):previous;
                    PipeAmount NextDied(PipeAmount previous,GermPopulationStep next)=>next.Change.Sign<=0?previous.Minus(next.Change):previous;
                    Check(Image(cold.Population,cold.Clock,NextBorn(restoredBorn,cold),NextDied(restoredDied,cold)).AsSpan().SequenceEqual(
                        Image(live.Population,live.Clock,NextBorn(born,live),NextDied(died,live))),"Cold next culture image differs byte-for-byte "+item.Name);
                    if(tick==1000||tick==10000||tick==20000||tick==30000)
                        Console.WriteLine($"BIOLOGY {item.Name} tick={tick} population={pop.Value:R} atoms={atoms}/{Atoms(born)}/{Atoms(died)} model-image={image.Length}B elapsed={timer.Elapsed.TotalSeconds:F3}s");
                }
                if(tick==ticks-window){started=Stopwatch.GetTimestamp();allocated=GC.GetAllocatedBytesForCurrentThread();}
            }
            Console.WriteLine($"BIOLOGY RESULT {item.Name}: early/late {early:F6}/{late:F6} us/tick; alloc {earlyBytes}/{lateBytes} B/tick; max population atoms {maxAtoms}, max population+clock+journals image {maxImage} B; exact audit/cold checks pass; total {timer.Elapsed.TotalSeconds:F3}s");
        }
    }
}
