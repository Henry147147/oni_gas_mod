using System.Reflection;

internal static class ValvePreparationTests
{
    internal static void Run(Assembly assembly)
    {
        var method=assembly.GetType("MixedAtmospheres.Oni.ColonyValvePreparation")!.GetMethod("TryPartialCallback",BindingFlags.Static|BindingFlags.NonPublic)!;
        (bool Fits,ulong Next) Probe(ulong seed,ulong previous,ulong last)
        {
            object?[] args={seed,previous,last,null};var fits=(bool)method.Invoke(null,args)!;return(fits,(ulong)args[3]!);
        }
        void Check(bool condition,string why){if(!condition)throw new Exception(why);}
        // Actual negative control: callback151 ran closed before the arm;
        // callback156 is outside the unchanged151..155 physical window.
        Check(Probe(127,121,126)==(false,156UL),"The captured missed-callback phase was accepted.");
        Check(Probe(126,121,126)==(true,151UL),"A captured callback strictly inside the fixed window was refused.");
        foreach(var bad in new[]{(120UL,0UL,115UL),(120UL,115UL,115UL),(120UL,115UL,121UL),(120UL,113UL,119UL),(ulong.MaxValue,100UL,105UL)})
            Check(!Probe(bad.Item1,bad.Item2,bad.Item3).Fits,"Missing, future, unsupported or overflowing cadence was accepted.");
        // Independent event timeline, not a duplicate of the phase formula.
        bool EventInside(ulong seed,ulong last)
        {
            for(var frame=last+5;frame<seed+28;frame+=5)if(frame>seed+24)return true;
            return false;
        }
        for(var gas=0UL;gas<5;++gas)for(var liquid=0UL;liquid<5;++liquid)for(var initial=120UL;initial<125;++initial)
        {
            var found=false;
            for(var wait=0UL;wait<5;++wait)
            {
                var seed=initial+wait;var g=Probe(seed,100+gas,105+gas);var l=Probe(seed,100+liquid,105+liquid);
                Check(g.Fits==EventInside(seed,105+gas)&&l.Fits==EventInside(seed,105+liquid),"Preparation differs from the independent native callback timeline.");
                if(!g.Fits||!l.Fits)continue;
                Check(g.Next>seed+24&&g.Next<seed+28&&l.Next>seed+24&&l.Next<seed+28,"A prepared phase extends the fixed physical window.");
                found=true;break;
            }
            Check(found,"A legal closed preparation phase needs more than four frames of waiting.");
        }
        Console.WriteLine("PASS valve preparation: captured zero-callback regression, invalid cadence refusal, all125 phase pairs fit the unchanged window without native clock reset");
    }
}
