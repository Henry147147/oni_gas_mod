using System;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Explicit fixture only: independently run the game's original updater on
    // one copied culture, reading the same temperature handle. Its constructor
    // does not register with any world or scheduler, and no native list changes.
    internal static class ColonyPipeBiologyOracle
    {
        private static readonly FieldInfo Rows=AccessTools.Field(typeof(ConduitDiseaseManager),"data"),
            Temperature=AccessTools.Field(typeof(ConduitDiseaseManager),"temperatureManager");
        private static ConduitFlow? flow;
        private static int cell;
        private static ConduitDiseaseManager? reference;
        internal static int Comparisons { get; private set; }
        internal static int Mismatches { get; private set; }
        internal static string Last { get; private set; }="";
        internal static void Start(ConduitFlow target,int targetCell){Reset();flow=target;cell=targetCell;}
        internal static void Reset(){flow=null;reference=null;cell=Comparisons=Mismatches=0;Last="";}
        internal static void Advance(ConduitDiseaseManager manager,ConduitFlow target,int targetCell,ConduitDiseaseManager.Data data,float seconds)
        {
            if(!ReferenceEquals(flow,target)||cell!=targetCell)return;
            if(reference==null)
            {
                reference=(ConduitDiseaseManager)Activator.CreateInstance(typeof(ConduitDiseaseManager),BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic,null,
                    new[]{Temperature.GetValue(manager)},null);
                ((List<ConduitDiseaseManager.Data>)Rows.GetValue(reference)).Add(data);
            }
            reference.Sim200ms(seconds);
        }
        internal static void Compare(ConduitFlow target,int targetCell,PipeInventory actual,ConduitDiseaseManager.Data projected)
        {
            if(reference==null||!ReferenceEquals(flow,target)||cell!=targetCell)return;
            var original=((List<ConduitDiseaseManager.Data>)Rows.GetValue(reference))[0];++Comparisons;
            var okay=original.diseaseCount==0?actual.Germs.Length==0:
                actual.Germs.Length==1&&actual.Germs[0].Id==original.diseaseIdx&&actual.Germs[0].Amount.Minus(new PipeAmount(original.diseaseCount)).Sign==0;
            if(!okay||projected.diseaseIdx!=original.diseaseIdx||
                BitConverter.SingleToInt32Bits(projected.accumulatedError)!=BitConverter.SingleToInt32Bits(original.accumulatedError))
            {++Mismatches;Last=$"native {original.diseaseIdx}/{original.diseaseCount}/{original.accumulatedError:R}; projection {projected.diseaseIdx}/{projected.diseaseCount}/{projected.accumulatedError:R}";}
        }
    }
}
