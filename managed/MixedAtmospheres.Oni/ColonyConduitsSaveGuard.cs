using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Temporary early refusal while actual mixed runtime persistence is gated.
    // The string entrypoint is before opening/truncating the destination;
    // writer overloads can protect only writes after the caller supplied them.
    [HarmonyPatch]
    internal static class ColonyMixedPipeSaveGuard
    {
        internal const string Reason="The mixed-pipe control cannot save until its runtime inventory, scheduler and quiescent save barrier are validated.";
        internal static IEnumerable<MethodBase> TargetMethods()
        {
            MethodInfo Require(Type type,string name,params Type[] args)
            {
                var method=AccessTools.Method(type,name,args);
                if(method==null||method.GetMethodBody()==null)throw new NotSupportedException("Mixed pipe save ingress changed: "+type.Name+"."+name);
                return method;
            }
            yield return Require(typeof(SaveLoader),"Save",typeof(string),typeof(bool),typeof(bool));
            yield return Require(typeof(SaveLoader),"Save",typeof(BinaryWriter));
            yield return Require(typeof(SaveLoader),"InitialSave");
            yield return Require(typeof(Game),"StartDelayedInitialSave");
            yield return Require(typeof(Game),"StartDelayedSave",typeof(string),typeof(bool),typeof(bool));
            yield return Require(typeof(Game),"Save",typeof(BinaryWriter));
        }
        [HarmonyPriority(Priority.First)]
        internal static void Prefix()
        {if(ColonyConduits.Active)throw new NotSupportedException(Reason);}
    }
}
