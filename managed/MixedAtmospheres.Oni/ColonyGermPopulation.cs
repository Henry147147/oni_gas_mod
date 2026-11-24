using System;
using System.IO;

namespace MixedAtmospheres.Oni
{
    // Native event rounding remains a separate mode. Fractional/wide cultures
    // use expected population, and never switch back merely because a later
    // rounded value happens to be integral. Extinction retires the culture.
    internal readonly struct GermPopulationClock
    {
        internal readonly bool Expected;
        internal readonly float NativeResidue;
        internal GermPopulationClock(bool expected,float residue)
        {
            if(float.IsNaN(residue)||float.IsInfinity(residue)||Math.Abs(residue)>=1||expected&&residue!=0)
                throw new ArgumentOutOfRangeException(nameof(residue));
            Expected=expected;NativeResidue=residue;
        }
        internal void Write(BinaryWriter writer){writer.Write(Expected);writer.Write(NativeResidue);}
        internal static GermPopulationClock Read(BinaryReader reader)
        {
            var mode=reader.ReadByte();if(mode>1)throw new InvalidDataException("Invalid germ evolution mode.");
            try{return new GermPopulationClock(mode!=0,reader.ReadSingle());}
            catch(ArgumentOutOfRangeException error){throw new InvalidDataException("Invalid germ evolution clock.",error);}
        }
    }
    internal readonly struct GermPopulationStep
    {
        internal readonly PipeAmount Population,Change;
        internal readonly GermPopulationClock Clock;
        internal GermPopulationStep(PipeAmount population,PipeAmount change,GermPopulationClock clock)
        {Population=population;Change=change;Clock=clock;}
    }
    internal static class ColonyGermPopulation
    {
        // An expected population is kept as one binary64 value (user decision
        // 2026-11-24: germ counts need not be exact). The exact result of a
        // step is rounded to the nearest double, and the step's booked change
        // is the exact difference between that and the population before, so
        // population + change = next population still holds exactly and the
        // birth and death accounts close. Unrounded, a count grew by a word
        // with every step until it reached the smallest double (about twenty
        // words), and each step's exact sums cost in proportion: the pipes'
        // germ steps were the largest part of the mod's time in a tick frame
        // (docs/playtest-1.13.5.md). A count that came from a move or a
        // mixture can still be a few words long until its next step.
        internal static PipeAmount Rounded(PipeAmount exact)
        {
            if(exact.Words<=1)return exact;
            var value=exact.Value;
            if(double.IsInfinity(value))throw new InvalidOperationException("Pipe account overflow.");
            return new PipeAmount(value);
        }
        // Integer cultures use the native public material equation. Fractional
        // and wide cultures use a public-descriptor expected-population flux:
        // absolute under-population death, proportional normal/over-population
        // growth. The latter cannot be obtained by scaling the former: absolute
        // death is not homogeneous in count and mass.
        internal static bool TryStep(PipeAmount population,GermPopulationClock clock,
            Func<int,float> nativeMaterialChange,Func<PipeAmount,PipeAmount> expectedMaterialChange,
            float temperatureMultiplier,out GermPopulationStep next)
        {
            next=default;
            if(population.Sign<=0||temperatureMultiplier<0||float.IsNaN(temperatureMultiplier)||float.IsInfinity(temperatureMultiplier))
                throw new ArgumentOutOfRangeException("Invalid exact germ population context.");
            var value=population.Value;
            if(!clock.Expected&&value<=int.MaxValue&&value==Math.Floor(value)&&population.Minus(new PipeAmount(value)).Sign==0)
            {
                var material=nativeMaterialChange((int)value);
                if(float.IsNaN(material)||float.IsInfinity(material))return false;
                if(ColonyGermEvolution.TryNativeStep((int)value,clock.NativeResidue,material,temperatureMultiplier,out var native))
                {
                    var change=new PipeAmount(native.Change);
                    next=new GermPopulationStep(population.Plus(change),change,new GermPopulationClock(false,native.Rounding));return true;
                }
            }
            try
            {
                var change=expectedMaterialChange(population).Plus(population.Times((double)temperatureMultiplier-1))
                    .Plus(new PipeAmount(clock.NativeResidue));
                var after=population.Plus(change);
                if(change.Sign<0&&after.Sign<=0){change=population.Negated();after=population.Plus(change);}
                if(after.Sign<0)throw new InvalidOperationException("Germ death exceeded exact population.");
                if(after.Words>1){after=Rounded(after);change=after.Minus(population);}
                next=new GermPopulationStep(after,change,new GermPopulationClock(true,0));return true;
            }
            catch(InvalidOperationException){return false;}
            catch(ArgumentOutOfRangeException){return false;}
        }
    }
}
