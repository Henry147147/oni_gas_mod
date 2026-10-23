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
    }
}
