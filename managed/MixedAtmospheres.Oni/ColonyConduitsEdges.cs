using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Reuse native rate/operation/topology decisions. AddElement reserves the
    // chosen destination; its matching RemoveElement moves the whole mixture
    // atomically. No dominant-element scalar is ever imported as that mixture.
    [HarmonyPatch]
    internal static class ColonyMixedPipeEdgePatch
    {
        private sealed class Fields
        {
            internal readonly FieldInfo Input,Type;
            internal readonly FieldInfo? Port,Secondary;
            internal Fields(System.Type type)
            {
                Input=AccessTools.Field(type,"inputCell");Secondary=AccessTools.Field(type,"secondaryInput");
                Type=AccessTools.Field(type,"conduitType")??AccessTools.Field(type,"type");
                if(Type==null){Port=AccessTools.Field(type,"portInfo");Type=AccessTools.Field(Port.FieldType,"conduitType");}
            }
        }
        private static readonly ConcurrentDictionary<Type,Fields> Cached=new ConcurrentDictionary<Type,Fields>();
        private static IEnumerable<MethodBase> TargetMethods()
        {
            yield return AccessTools.Method(typeof(ConduitBridge),"ConduitUpdate");
            yield return AccessTools.Method(typeof(ValveBase),"ConduitUpdate");
            yield return AccessTools.Method(typeof(ConduitOverflow),"ConduitUpdate");
            yield return AccessTools.Method(typeof(ConduitPreferentialFlow),"ConduitUpdate");
        }
        private static void Prefix(object __instance,ref ColonyConduits.TransferScope? __state)
        {
            if(!ColonyConduits.Active)return;
            var fields=Cached.GetOrAdd(__instance.GetType(),type=>new Fields(type));
            var type=(ConduitType)fields.Type.GetValue(fields.Port==null?__instance:fields.Port.GetValue(__instance));
            if(type!=ConduitType.Gas&&type!=ConduitType.Liquid)return;
            var input=(int)fields.Input.GetValue(__instance);var secondary=-1;
            if(fields.Secondary?.GetValue(__instance) is object item)secondary=(int)AccessTools.Property(item.GetType(),"Cell").GetValue(item);
            __state=ColonyConduits.BeginTransfer(Conduit.GetFlowManager(type),input,secondary);
        }
        private static Exception? Finalizer(Exception? __exception,ColonyConduits.TransferScope? __state)
        {ColonyConduits.EndTransfer(__state,__exception);return __exception;}
    }
}
