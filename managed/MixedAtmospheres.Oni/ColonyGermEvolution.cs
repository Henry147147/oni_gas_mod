using System;

namespace MixedAtmospheres.Oni
{
    // Common scalar step for pipe and world adapters. Material and temperature
    // effects come from the game's public disease APIs; this class only owns
    // their native binary32 accumulation/truncation and exact integer change.
    internal readonly struct NativeGermStep
    {
        internal readonly int Population,Change;
        internal readonly float Rounding;
        internal NativeGermStep(int population,float rounding,int change)
        {Population=population;Rounding=rounding;Change=change;}
    }
    internal static class ColonyGermEvolution
    {
        internal static bool TryNativeStep(int population,float rounding,float materialChange,float temperatureMultiplier,out NativeGermStep next)
        {
            if(population<0||!Finite(rounding)||Math.Abs(rounding)>=1||!Finite(materialChange)||!Finite(temperatureMultiplier)||temperatureMultiplier<0)
                throw new ArgumentOutOfRangeException("Invalid native germ evolution inputs.");
            next=default;
            var accumulated=rounding+materialChange;
            var temperaturePopulation=(float)population*temperatureMultiplier;
            var temperatureChange=temperaturePopulation-(float)population;
            accumulated+=temperatureChange;
            // Native float->int conversion and int addition cease to represent
            // the population outside this domain. Retain the original exact
            // population for the caller's explicit wide/fractional extension.
            if(!Finite(accumulated)||accumulated < -2147483648f||accumulated >= 2147483648f)return false;
            var change=(int)accumulated;var count=(long)population+change;
            if(count>int.MaxValue)return false;
            if(count<=0)next=new NativeGermStep(0,0,-population);
            else next=new NativeGermStep((int)count,accumulated-(float)change,change);
            return true;
        }
        private static bool Finite(float value)=>!float.IsNaN(value)&&!float.IsInfinity(value);
    }
}
