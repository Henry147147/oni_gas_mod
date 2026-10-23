using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Read-only, explicitly selected oracle control. Original native-game pipe
    // disease evolution still runs; compare its count and binary32 residue.
    internal static class ColonyGermEvolutionProbe
    {
        internal sealed class Expected
        {
            internal readonly int Index,Population;internal readonly byte Disease;internal readonly NativeGermStep Step;
            internal Expected(int index,int population,byte disease,NativeGermStep step){Index=index;Population=population;Disease=disease;Step=step;}
        }
        internal sealed class Scope
        {
            internal readonly long Epoch;internal readonly bool Reference;internal readonly IList Rows;internal readonly List<Expected> Values=new List<Expected>();
            internal Scope(long epoch,IList rows,bool reference){Epoch=epoch;Rows=rows;Reference=reference;}
        }
        private static readonly FieldInfo Rows=AccessTools.Field(typeof(ConduitDiseaseManager),"data"),
            Temperature=AccessTools.Field(typeof(ConduitDiseaseManager),"temperatureManager");
        private static readonly MethodInfo GetTemperature=AccessTools.Method(AccessTools.TypeByName("ConduitTemperatureManager"),"GetTemperature");
        private static readonly MethodInfo AllocateCulture=typeof(ConduitDiseaseManager).GetMethod("Allocate",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.DeclaredOnly)!;
        private static long epoch;
        private static ConduitDiseaseManager? reference;
        private static readonly HashSet<byte> diseases=new HashSet<byte>();
        internal static bool Enabled { get; private set; }
        internal static long Samples { get; private set; }
        internal static long Extinctions { get; private set; }
        internal static long Unsupported { get; private set; }
        internal static long Mismatches { get; private set; }
        internal static long Births { get; private set; }
        internal static long Deaths { get; private set; }
        internal static long PipeSamples { get; private set; }
        internal static long ReferenceSamples { get; private set; }
        internal static long ReferenceBirths { get; private set; }
        internal static float MinimumPipeCultureMass { get; private set; }
        internal static float MaximumPipeCultureMass { get; private set; }
        internal static int Diseases=>diseases.Count;
        internal static string Last { get; private set; }="";
        internal static void StartControl()
        {
            Reset();if(Rows==null||Temperature==null||GetTemperature==null||AllocateCulture==null)throw new NotSupportedException("Native pipe disease oracle layout is unavailable.");Enabled=true;
        }
        internal static void Reset(){Enabled=false;++epoch;reference=null;Samples=Extinctions=Unsupported=Mismatches=Births=Deaths=PipeSamples=ReferenceSamples=ReferenceBirths=0;MinimumPipeCultureMass=float.PositiveInfinity;MaximumPipeCultureMass=0;diseases.Clear();Last="";}
        internal static void Stop()=>Enabled=false;
        internal static string Report=>$"{Samples} comparisons ({PipeSamples} actual pipe/{ReferenceSamples} isolated positive-mass), {Diseases} identities, births/deaths {Births}/{Deaths}, reference births {ReferenceBirths}, pipe culture mass {MinimumPipeCultureMass:R}..{MaximumPipeCultureMass:R}, {Extinctions} extinction resets, {Unsupported} unsupported, {Mismatches} mismatches; {Last}";
        private static object Field(object row,string name)=>AccessTools.Field(row.GetType(),name).GetValue(row);
        internal static Scope? Begin(ConduitDiseaseManager manager,float dt)
        {
            if(!Enabled)return null;
            var rows=(IList)Rows.GetValue(manager);var scope=new Scope(epoch,rows,ReferenceEquals(manager,reference));
            for(var index=0;index<rows.Count;++index)
            {
                var row=rows[index]!;var id=(byte)Field(row,"diseaseIdx");if(id==byte.MaxValue)continue;
                var population=(int)Field(row,"diseaseCount");var rounding=(float)Field(row,"accumulatedError");
                var mass=(float)Field(row,"mass");var growth=(Klei.AI.DiseaseGrowthRules.ElemGrowthInfo)Field(row,"growthInfo");
                var temperature=(float)GetTemperature.Invoke(Temperature.GetValue(manager),new[]{Field(row,"temperatureHandle")});
                if(!scope.Reference){MinimumPipeCultureMass=Math.Min(MinimumPipeCultureMass,mass);MaximumPipeCultureMass=Math.Max(MaximumPipeCultureMass,mass);}
                if(ColonyNativeGermEffects.TryCapturedCultureStep(population,rounding,id,growth,mass,temperature,dt,out var step)){scope.Values.Add(new Expected(index,population,id,step));diseases.Add(id);}
                else{++Unsupported;Last=$"unsupported native integer domain at {index}, disease {id}, population {population}";}
            }
            return scope;
        }
        internal static void End(Scope? scope)
        {
            if(!Enabled||scope==null||scope.Epoch!=epoch)return;
            foreach(var expected in scope.Values)
            {
                var row=scope.Rows[expected.Index]!;var population=(int)Field(row,"diseaseCount");var rounding=(float)Field(row,"accumulatedError");var id=(byte)Field(row,"diseaseIdx");
                ++Samples;if(expected.Step.Population==0)++Extinctions;
                if(scope.Reference)++ReferenceSamples;else ++PipeSamples;
                var actual=(long)population-expected.Population;if(actual>0){Births+=actual;if(scope.Reference)ReferenceBirths+=actual;}else Deaths-=actual;
                if(population!=expected.Step.Population||BitConverter.SingleToInt32Bits(rounding)!=BitConverter.SingleToInt32Bits(expected.Step.Rounding)||id!=(population==0?byte.MaxValue:expected.Disease))
                {++Mismatches;Last=$"row {expected.Index}: native {id}/{population}/{rounding:R}, predicted {expected.Disease}/{expected.Step.Population}/{expected.Step.Rounding:R}";}
            }
        }
        internal static bool SeedPositiveMassReference(out string detail)
        {
            if(!Enabled)throw new InvalidOperationException("Native germ oracle is disabled.");
            var manager=Game.Instance.conduitDiseaseManager;var rows=(IList)Rows.GetValue(manager);var temperatures=Temperature.GetValue(manager);
            var food=Db.Get().Diseases.GetIndex(Db.Get().Diseases.FoodGerms.id);var slime=Db.Get().Diseases.GetIndex(Db.Get().Diseases.SlimeGerms.id);
            var best=float.NegativeInfinity;var bestCase="none";
            foreach(var material in new[]{SimHashes.ContaminatedOxygen,SimHashes.DirtyWater,SimHashes.Water,SimHashes.Oxygen})
            foreach(var id in new[]{food,slime})foreach(var count in new[]{1,10,100,1000,10000,100000,1000000,10000000})
            {
                var element=ElementLoader.FindElementByHash(material);var mass=element.IsLiquid?5f:.5f;
                for(var index=0;index<rows.Count;++index)
                {
                    var handle=Field(rows[index]!,"temperatureHandle");var t=(float)GetTemperature.Invoke(temperatures,new[]{handle});
                    if(!ColonyNativeGermEffects.TryWorldCellStep(count,0,id,element.idx,mass,t,.2f,out var step))continue;
                    var growth=step.Change+step.Rounding;
                    if(growth>best){best=growth;bestCase=$"{material}, id {id}, mass {mass:R}, T {t:R}, count {count}, change+residue {growth:R}";}
                    if(step.Change<0||step.Change==0&&step.Rounding<1f/60)continue;
                    reference=(ConduitDiseaseManager)Activator.CreateInstance(typeof(ConduitDiseaseManager),BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic,null,new[]{temperatures},null);
                    object contents=new ConduitFlow.ConduitContents(material,mass,t,id,count);
                    AllocateCulture.Invoke(reference,new[]{handle,contents});
                    detail=$"isolated native equation manager: {material}, disease {id}, allocated mass {mass:R}, T {t:R}, count {count}; actual pipe table unchanged ({rows.Count} rows)";return true;
                }
            }
            detail="No native current-mass fluid culture reached the fixed birth window; best "+bestCase;return false;
        }
        internal static void TickReference(){if(Enabled&&reference!=null)reference.Sim200ms(.2f);}
    }
    [HarmonyPatch(typeof(ConduitDiseaseManager),"Sim200ms")]
    internal static class ColonyGermEvolutionOraclePatch
    {
        private static void Prefix(ConduitDiseaseManager __instance,float __0,ref ColonyGermEvolutionProbe.Scope? __state)=>__state=ColonyGermEvolutionProbe.Begin(__instance,__0);
        private static void Postfix(ColonyGermEvolutionProbe.Scope? __state)=>ColonyGermEvolutionProbe.End(__state);
    }
}
