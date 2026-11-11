using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Exact storage (development only: ColonyConduitsRuntime starts it for its
    // development switch or a saved schema 5-7 pipe image)
    // refuses every native mutation of a store it owns and of an item that
    // projects one. A saved colony can start it in any game, so the guards are
    // always installed; they are typed per overload shape and per method, and
    // return at once while no exact store exists (ColonyOwnedStorage.Count is
    // 0 in every player's game). ONI's Storage and PrimaryElement calls thus
    // pay no argument array, boxing or reflection. With stores they reject
    // exactly as the single generic guard they replace did: the storage
    // itself, then each item argument (its source storage) and storage
    // argument (a destination), in argument order; Remove first retires an
    // exhausted display carrier.
    internal static class ColonyExactStorageMutations
    {
        internal static readonly string[] Names = { "Store", "AddElement", "AddGasChunk", "AddLiquid", "AddOre", "AddToPrimaryElement", "ConsumeAndGetDisease",
            "ConsumeIgnoringDisease", "ConsumeAllIgnoringDisease", "Remove", "Drop", "DropAll", "DropSome", "DropUnlessMatching", "DropUnlessHasTag", "DropHasTags",
            "ForceStore", "Transfer", "TransferMass", "TransferUnitMass", "ClearItems" };
        internal static bool Idle => ColonyOwnedStorage.Count == 0;

        private static IEnumerable<MethodInfo> Overloads(string name) =>
            typeof(Storage).GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.DeclaredOnly).Where(method => method.Name == name);
        private static bool Carried(Type type) => type == typeof(GameObject) || type == typeof(Storage);
        // The overloads of `name` whose leading parameters are exactly `leading`
        // and whose other parameters carry no item or storage.
        internal static IEnumerable<MethodBase> Shaped(string name, params Type[] leading) => Overloads(name).Where(method =>
        {
            var parameters = method.GetParameters();
            if (parameters.Length < leading.Length) return false;
            for (var k = 0; k < parameters.Length; ++k)
                if (k < leading.Length ? parameters[k].ParameterType != leading[k] : Carried(parameters[k].ParameterType.IsByRef ? parameters[k].ParameterType.GetElementType()! : parameters[k].ParameterType))
                    return false;
            return true;
        }).Cast<MethodBase>().ToList();
        internal static IEnumerable<MethodBase> Plain(string name) => Shaped(name);
        // Every guarded overload the typed patches below cover.
        private static IEnumerable<MethodBase> Typed() =>
            new[] { "AddElement", "AddGasChunk", "AddLiquid", "AddOre", "AddToPrimaryElement", "ClearItems", "ConsumeAllIgnoringDisease", "ConsumeAndGetDisease",
                "ConsumeIgnoringDisease", "Drop", "DropAll", "DropHasTags", "DropSome", "DropUnlessHasTag", "DropUnlessMatching", "ForceStore" }.SelectMany(Plain)
            .Concat(new[] { "Store", "Remove", "ConsumeIgnoringDisease", "Drop" }.SelectMany(name => Shaped(name, typeof(GameObject))))
            .Concat(new[] { "Transfer", "TransferMass", "TransferUnitMass" }.SelectMany(name => Shaped(name, typeof(Storage))))
            .Concat(Shaped("Transfer", typeof(GameObject), typeof(Storage)));
        // Overloads of the guarded names no typed patch covers (none in the
        // inspected game; a later game may add one): the generic guard.
        internal static IEnumerable<MethodBase> Untyped()
        {
            var typed = new HashSet<MethodBase>(Typed());
            return Names.SelectMany(Overloads).Where(method => !typed.Contains(method)).Cast<MethodBase>().ToList();
        }

        internal static void Guard(Storage storage, string operation) => ColonyOwnedStorage.Reject(storage, operation);
        internal static void Source(GameObject? item, string operation)
        {
            if (item == null) return;
            var pickup = item.GetComponent<Pickupable>();
            if (pickup != null && pickup.storage != null) ColonyOwnedStorage.Reject(pickup.storage, operation + " source");
        }
        internal static void Destination(Storage? storage, string operation)
        {
            if (!ReferenceEquals(storage, null)) ColonyOwnedStorage.Reject(storage!, operation + " destination");
        }
    }

    // Storage mutations without an item or storage argument.
    [HarmonyPatch] internal static class ColonyExactStoreAddElement { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("AddElement"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "AddElement"); } }
    [HarmonyPatch] internal static class ColonyExactStoreAddGasChunk { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("AddGasChunk"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "AddGasChunk"); } }
    [HarmonyPatch] internal static class ColonyExactStoreAddLiquid { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("AddLiquid"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "AddLiquid"); } }
    [HarmonyPatch] internal static class ColonyExactStoreAddOre { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("AddOre"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "AddOre"); } }
    [HarmonyPatch] internal static class ColonyExactStoreAddToPrimaryElement { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("AddToPrimaryElement"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "AddToPrimaryElement"); } }
    [HarmonyPatch] internal static class ColonyExactStoreClearItems { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("ClearItems"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "ClearItems"); } }
    [HarmonyPatch] internal static class ColonyExactStoreConsumeAllIgnoringDisease { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("ConsumeAllIgnoringDisease"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "ConsumeAllIgnoringDisease"); } }
    [HarmonyPatch] internal static class ColonyExactStoreConsumeAndGetDisease { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("ConsumeAndGetDisease"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "ConsumeAndGetDisease"); } }
    [HarmonyPatch] internal static class ColonyExactStoreConsumeIgnoringDisease { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("ConsumeIgnoringDisease"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "ConsumeIgnoringDisease"); } }
    [HarmonyPatch] internal static class ColonyExactStoreDrop { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("Drop"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "Drop"); } }
    [HarmonyPatch] internal static class ColonyExactStoreDropAll { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("DropAll"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "DropAll"); } }
    [HarmonyPatch] internal static class ColonyExactStoreDropHasTags { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("DropHasTags"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "DropHasTags"); } }
    [HarmonyPatch] internal static class ColonyExactStoreDropSome { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("DropSome"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "DropSome"); } }
    [HarmonyPatch] internal static class ColonyExactStoreDropUnlessHasTag { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("DropUnlessHasTag"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "DropUnlessHasTag"); } }
    [HarmonyPatch] internal static class ColonyExactStoreDropUnlessMatching { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("DropUnlessMatching"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "DropUnlessMatching"); } }
    [HarmonyPatch] internal static class ColonyExactStoreForceStore { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Plain("ForceStore"); static void Prefix(Storage __instance) { if (!ColonyExactStorageMutations.Idle) ColonyExactStorageMutations.Guard(__instance, "ForceStore"); } }

    // Storage mutations of an item (its source storage is refused too).
    [HarmonyPatch] internal static class ColonyExactStoreStore { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Shaped("Store", typeof(GameObject)); static void Prefix(Storage __instance, GameObject? __0) { if (ColonyExactStorageMutations.Idle) return; ColonyExactStorageMutations.Guard(__instance, "Store"); ColonyExactStorageMutations.Source(__0, "Store"); } }
    [HarmonyPatch] internal static class ColonyExactStoreConsumeItem { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Shaped("ConsumeIgnoringDisease", typeof(GameObject)); static void Prefix(Storage __instance, GameObject? __0) { if (ColonyExactStorageMutations.Idle) return; ColonyExactStorageMutations.Guard(__instance, "ConsumeIgnoringDisease"); ColonyExactStorageMutations.Source(__0, "ConsumeIgnoringDisease"); } }
    [HarmonyPatch] internal static class ColonyExactStoreDropItem { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Shaped("Drop", typeof(GameObject)); static void Prefix(Storage __instance, GameObject? __0) { if (ColonyExactStorageMutations.Idle) return; ColonyExactStorageMutations.Guard(__instance, "Drop"); ColonyExactStorageMutations.Source(__0, "Drop"); } }
    [HarmonyPatch]
    internal static class ColonyExactStoreRemove
    {
        static bool Prepare() => TargetMethods().Any();
        static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Shaped("Remove", typeof(GameObject));
        static void Prefix(Storage __instance, GameObject? __0)
        {
            if (ColonyExactStorageMutations.Idle) return;
            // An exhausted display carrier leaves without a refusal.
            if (!ReferenceEquals(__0, null) && ColonyOwnedStorage.RetireEmptyProjection(__instance, __0!)) return;
            ColonyExactStorageMutations.Guard(__instance, "Remove"); ColonyExactStorageMutations.Source(__0, "Remove");
        }
    }

    // Transfers to another storage (refused as a destination too).
    [HarmonyPatch] internal static class ColonyExactStoreTransfer { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Shaped("Transfer", typeof(Storage)); static void Prefix(Storage __instance, Storage? __0) { if (ColonyExactStorageMutations.Idle) return; ColonyExactStorageMutations.Guard(__instance, "Transfer"); ColonyExactStorageMutations.Destination(__0, "Transfer"); } }
    [HarmonyPatch] internal static class ColonyExactStoreTransferMass { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Shaped("TransferMass", typeof(Storage)); static void Prefix(Storage __instance, Storage? __0) { if (ColonyExactStorageMutations.Idle) return; ColonyExactStorageMutations.Guard(__instance, "TransferMass"); ColonyExactStorageMutations.Destination(__0, "TransferMass"); } }
    [HarmonyPatch] internal static class ColonyExactStoreTransferUnitMass { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Shaped("TransferUnitMass", typeof(Storage)); static void Prefix(Storage __instance, Storage? __0) { if (ColonyExactStorageMutations.Idle) return; ColonyExactStorageMutations.Guard(__instance, "TransferUnitMass"); ColonyExactStorageMutations.Destination(__0, "TransferUnitMass"); } }
    [HarmonyPatch] internal static class ColonyExactStoreTransferItem { static bool Prepare() => TargetMethods().Any(); static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Shaped("Transfer", typeof(GameObject), typeof(Storage)); static void Prefix(Storage __instance, GameObject? __0, Storage? __1) { if (ColonyExactStorageMutations.Idle) return; ColonyExactStorageMutations.Guard(__instance, "Transfer"); ColonyExactStorageMutations.Source(__0, "Transfer"); ColonyExactStorageMutations.Destination(__1, "Transfer"); } }

    // Any other overload of a guarded name keeps the generic guard.
    [HarmonyPatch]
    internal static class ColonyExactStoreUntyped
    {
        static bool Prepare() => TargetMethods().Any();
        static IEnumerable<MethodBase> TargetMethods() => ColonyExactStorageMutations.Untyped();
        static void Prefix(Storage __instance, object[] __args, MethodBase __originalMethod)
        {
            if (ColonyExactStorageMutations.Idle) return;
            if (__originalMethod.Name == "Remove" && __args.Length > 0 && __args[0] is GameObject exhausted && ColonyOwnedStorage.RetireEmptyProjection(__instance, exhausted)) return;
            ColonyOwnedStorage.Reject(__instance, __originalMethod.Name);
            foreach (var argument in __args)
                if (argument is Storage storage) ColonyOwnedStorage.Reject(storage, __originalMethod.Name + " destination");
                else if (argument is GameObject item) ColonyExactStorageMutations.Source(item, __originalMethod.Name);
        }
    }

    // Item mutations of a projection's PrimaryElement.
    [HarmonyPatch] internal static class ColonyExactItemSetMass { static MethodBase TargetMethod() => AccessTools.Method(typeof(PrimaryElement), "SetMass"); static void Prefix(PrimaryElement __instance) { if (!ColonyExactStorageMutations.Idle) ColonyOwnedStorage.Reject(__instance, "SetMass"); } }
    [HarmonyPatch] internal static class ColonyExactItemSetTemperature { static MethodBase TargetMethod() => AccessTools.Method(typeof(PrimaryElement), "SetTemperature"); static void Prefix(PrimaryElement __instance) { if (!ColonyExactStorageMutations.Idle) ColonyOwnedStorage.Reject(__instance, "SetTemperature"); } }
    [HarmonyPatch] internal static class ColonyExactItemSetMassTemperature { static MethodBase TargetMethod() => AccessTools.Method(typeof(PrimaryElement), "SetMassTemperature"); static void Prefix(PrimaryElement __instance) { if (!ColonyExactStorageMutations.Idle) ColonyOwnedStorage.Reject(__instance, "SetMassTemperature"); } }
    [HarmonyPatch] internal static class ColonyExactItemSetElement { static MethodBase TargetMethod() => AccessTools.Method(typeof(PrimaryElement), "SetElement"); static void Prefix(PrimaryElement __instance) { if (!ColonyExactStorageMutations.Idle) ColonyOwnedStorage.Reject(__instance, "SetElement"); } }
    [HarmonyPatch] internal static class ColonyExactItemModifyDiseaseCount { static MethodBase TargetMethod() => AccessTools.Method(typeof(PrimaryElement), "ModifyDiseaseCount"); static void Prefix(PrimaryElement __instance) { if (!ColonyExactStorageMutations.Idle) ColonyOwnedStorage.Reject(__instance, "ModifyDiseaseCount"); } }
    [HarmonyPatch] internal static class ColonyExactItemAddDisease { static MethodBase TargetMethod() => AccessTools.Method(typeof(PrimaryElement), "AddDisease"); static void Prefix(PrimaryElement __instance) { if (!ColonyExactStorageMutations.Idle) ColonyOwnedStorage.Reject(__instance, "AddDisease"); } }
}
