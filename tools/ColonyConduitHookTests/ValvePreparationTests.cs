using System.Reflection;

internal static class ValvePreparationTests
{
    internal static void Run(Assembly assembly)
    {
        var method=assembly.GetType("MixedAtmospheres.Oni.ColonyValvePreparation")!.GetMethod("TryCallbackPhase",BindingFlags.Static|BindingFlags.NonPublic)!;
        (bool Fits,ulong Next) Probe(ulong seed,ulong previous,ulong last)
        {
            object?[] args={seed,previous,last,null};var fits=(bool)method.Invoke(null,args)!;return(fits,(ulong)args[3]!);
        }
        void Check(bool condition,string why){if(!condition)throw new Exception(why);}
        // Actual negative control: callback151 ran closed before the arm;
        // callback156 is outside the unchanged151..155 physical window.
        Check(Probe(127,121,126)==(false,156UL),"The captured missed-callback phase was accepted.");
        // Actual final b10: liquid callbacks tagged141/146/151 for the rate
        // window opened at141 and checked at151; only146 ran while open.
        Check(Probe(131,126,131)==(false,156UL),"The captured liquid phase with callbacks on the rate window's edges was accepted.");
        Check(Probe(131,123,128)==(true,158UL),"The captured gas phase with interior callbacks was refused.");
        Check(Probe(134,126,131)==(true,161UL),"The captured liquid phase's own interior epoch was refused.");
        var shared=false;
        for(var seed=131UL;seed<136;++seed)shared|=Probe(seed,123,128).Fits&&Probe(seed,126,131).Fits;
        Check(!shared,"The captured gas and liquid phases share an epoch, so one shared timeline would suffice.");
        foreach(var bad in new[]{(120UL,0UL,115UL),(120UL,115UL,115UL),(120UL,115UL,121UL),(120UL,113UL,119UL),(ulong.MaxValue,100UL,105UL)})
            Check(!Probe(bad.Item1,bad.Item2,bad.Item3).Fits,"Missing, future, unsupported or overflowing cadence was accepted.");
        // Independent event timeline, not a duplicate of the phase formula.
        bool Interior(ulong seed,ulong last)
        {
            var rate=0;var partial=false;
            for(var frame=last+5;frame<=seed+28;frame+=5)
            {
                if(frame==seed+10||frame==seed+20)return false;
                if(frame>seed+10&&frame<seed+20)++rate;
                if(frame>seed+24&&frame<seed+28)partial=true;
            }
            return rate==2&&partial;
        }
        for(var callback=0UL;callback<5;++callback)for(var initial=120UL;initial<125;++initial)
        {
            var found=false;
            for(var wait=0UL;wait<5;++wait)
            {
                var seed=initial+wait;var probe=Probe(seed,100+callback,105+callback);
                Check(probe.Fits==Interior(seed,105+callback),"Preparation differs from the independent native callback timeline.");
                if(!probe.Fits)continue;
                Check(probe.Next>seed+24&&probe.Next<seed+28,"A prepared phase extends the fixed physical window.");
                found=true;break;
            }
            Check(found,"A legal closed preparation phase needs more than four frames of waiting.");
        }
        Console.WriteLine("PASS valve preparation: captured zero-callback and rate-edge regressions, invalid cadence refusal, every callback phase prepares strictly interior callbacks for both unchanged windows within four frames, without native clock reset");
    }
}
