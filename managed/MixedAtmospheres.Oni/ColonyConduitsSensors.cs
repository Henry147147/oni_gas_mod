using System;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    internal static class ColonyPipeSensors
    {
        private static readonly FieldInfo Type=AccessTools.Field(typeof(ConduitElementSensor),"conduitType");
        internal static bool Read(KMonoBehaviour sensor,out PipeInventory inventory)
        {
            inventory=PipeInventory.Empty;if(!ColonyConduits.Active)return false;
            var type=(ConduitType)Type.GetValue(sensor);
            if(type!=ConduitType.Gas&&type!=ConduitType.Liquid)return false;
            return ColonyConduits.TryRead(Conduit.GetFlowManager(type),Grid.PosToCell(sensor.gameObject),out inventory);
        }
    }
    [HarmonyPatch(typeof(ConduitElementSensor),"GetContentsElement")]
    internal static class ColonyMixedPipeElementSensorPatch
    {
        private static readonly FieldInfo Filter=AccessTools.Field(typeof(ConduitElementSensor),"filterable");
        private static bool Prefix(ConduitElementSensor __instance,ref Tag __0,ref bool __1)
        {
            if(!ColonyPipeSensors.Read(__instance,out var inventory))return true;
            __0=GameTags.Void;__1=inventory.Count>0;
            var wanted=((Filterable)Filter.GetValue(__instance)).SelectedTag;
            foreach(var part in inventory.Parts)
                if(ElementLoader.FindElementByHash(ColonyConduits.ElementHash(part.Id)).tag==wanted&&inventory.ContainsFraction(part.Id,ColonySensors.TraceFraction))
                {__0=wanted;break;}
            return false;
        }
    }
    [HarmonyPatch(typeof(ConduitDiseaseSensor),"GetContentsDisease")]
    internal static class ColonyMixedPipeDiseaseSensorPatch
    {
        private static bool Prefix(ConduitDiseaseSensor __instance,ref int __0,ref int __1,ref bool __2)
        {
            if(!ColonyPipeSensors.Read(__instance,out var inventory))return true;
            var total=PipeAmount.Zero;var largest=0.0;__0=byte.MaxValue;__2=inventory.Count>0;
            foreach(var germ in inventory.Germs)
            {
                total=total.Plus(germ.Amount);
                if(germ.Mass>largest&&germ.Id<byte.MaxValue){largest=germ.Mass;__0=germ.Id;}
            }
            __1=(int)Math.Min(int.MaxValue,Math.Floor(total.Value));
            while(__1>0&&new PipeAmount(__1).Minus(total).Sign>0)--__1;
            return false;
        }
    }
}
