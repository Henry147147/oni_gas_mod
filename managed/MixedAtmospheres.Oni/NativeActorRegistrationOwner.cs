using System;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Balanced observation of the real component issuing a registration. No
    // callback index or allocator handle is used as a persistent identity.
    internal sealed class NativeActorRegistrationOwner : IDisposable
    {
        [ThreadStatic] internal static NativeActorRegistrationOwner? Current;
        private readonly NativeActorRegistrationOwner? previous;
        internal readonly SimComponent Owner;
        internal readonly int Callback;
        internal NativeActorRegistrationOwner(SimComponent owner,int callback)
        { previous=Current;Owner=owner;Callback=callback;Current=this; }
        public void Dispose() { Current=previous; }
    }

    [HarmonyPatch]
    internal static class NativeActorRegistrationOwnerPatch
    {
        private static IEnumerable<MethodBase> TargetMethods()
        {
            var signature=new[]{typeof(HandleVector<Game.ComplexCallbackInfo<int>>.Handle)};
            foreach(var type in new[]{typeof(ElementConsumer),typeof(ElementEmitter)})
            {
                var method=AccessTools.DeclaredMethod(type,"OnSimRegister",signature);
                if(method==null || method.IsStatic || method.ReturnType!=typeof(void))
                    throw new MissingMethodException(type.FullName,"OnSimRegister");
                yield return method;
            }
        }
        private static void Prefix(SimComponent __instance,HandleVector<Game.ComplexCallbackInfo<int>>.Handle cb_handle,
            out NativeActorRegistrationOwner? __state)
        { __state=NativeSealedDomainValidation.Active ? new NativeActorRegistrationOwner(__instance,cb_handle.index) : null; }
        private static Exception? Finalizer(Exception? __exception,NativeActorRegistrationOwner? __state)
        { __state?.Dispose();return __exception; }
    }
}
