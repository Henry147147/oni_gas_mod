using System;

namespace MixedAtmospheres.Oni
{
    // Domain inputs differ: ordinary pipes retain an allocated culture's mass
    // and cached descriptor, whereas a world cell uses its current material/mass.
    // Both adapters share the game's effects and the same scalar rounding step.
    internal static class ColonyNativeGermEffects
    {
        internal static bool TryCapturedCultureStep(int population,float rounding,byte disease,
            Klei.AI.DiseaseGrowthRules.ElemGrowthInfo growth,float capturedMass,float temperature,float seconds,out NativeGermStep next)
        {
            if(capturedMass<0||float.IsNaN(capturedMass)||float.IsInfinity(capturedMass)||!(temperature>0)||float.IsInfinity(temperature)||!(seconds>0)||float.IsInfinity(seconds)||disease==byte.MaxValue)
                throw new ArgumentOutOfRangeException("Invalid native germ effect context.");
            var materialChange=growth.CalculateDiseaseCountDelta(population,capturedMass,seconds);
            var definition=Db.Get().Diseases[disease];
            var multiplier=Klei.AI.Disease.HalfLifeToGrowthRate(definition.CalculateTemperatureHalfLife(temperature),seconds);
            return ColonyGermEvolution.TryNativeStep(population,rounding,materialChange,multiplier,out next);
        }
        internal static bool TryWorldCellStep(int population,float rounding,byte disease,ushort elementOrdinal,
            float currentMass,float temperature,float seconds,out NativeGermStep next)
        {
            if(!(currentMass>0)||float.IsInfinity(currentMass)||disease==byte.MaxValue)
                throw new ArgumentOutOfRangeException("Invalid native world germ context.");
            var growth=Db.Get().Diseases[disease].elemGrowthInfo[elementOrdinal];
            return TryCapturedCultureStep(population,rounding,disease,growth,currentMass,temperature,seconds,out next);
        }
        internal static PipeAmount ExpectedMaterialChange(PipeAmount population,Klei.AI.DiseaseGrowthRules.ElemGrowthInfo growth,float mass,float seconds)
        {
            if(population.Sign<=0||mass<0||float.IsNaN(mass)||float.IsInfinity(mass)||!(seconds>0)||float.IsInfinity(seconds))
                throw new ArgumentOutOfRangeException("Invalid expected germ effect context.");
            // Public descriptors express density cutoffs and two proportional
            // half-life regimes. Below the minimum, death is an absolute rate;
            // scaling the population and mass together would change that rate.
            var minimum=growth.minCountPerKG*mass;var maximum=growth.maxCountPerKG*mass;
            var below=!float.IsNaN(minimum)&&minimum>0&&(float.IsPositiveInfinity(minimum)||population.Minus(new PipeAmount(minimum)).Sign<0);
            if(below)return new PipeAmount(-growth.underPopulationDeathRate*seconds);
            // Native unordered density comparisons choose the over-population
            // branch, including a public infinite cutoff multiplied by zero kg.
            var over=float.IsNaN(minimum)||float.IsNaN(maximum)||maximum<0||!float.IsPositiveInfinity(maximum)&&population.Minus(new PipeAmount(maximum)).Sign>0;
            var multiplier=Klei.AI.Disease.HalfLifeToGrowthRate(over?growth.overPopulationHalfLife:growth.populationHalfLife,seconds);
            return population.Times((double)(multiplier-1f));
        }
    }
}
