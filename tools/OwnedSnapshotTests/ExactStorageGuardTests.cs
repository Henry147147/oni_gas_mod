using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Records the guards' calls (the production class needs the game).
    internal static class ColonyOwnedStorage
    {
        internal static int Count;
        internal static readonly List<string> Rejected = new List<string>();
        internal static void Reject(Storage storage, string operation) => Rejected.Add(operation);
        internal static void Reject(PrimaryElement item, string operation) => Rejected.Add("item " + operation);
        internal static bool RetireEmptyProjection(Storage storage, GameObject item) { Rejected.Add("retire"); return false; }
    }
}

// The typed exact-storage guards (ColonyOwnedStorage.Mutations.cs) against
// the generic guard they replaced, on the installed game's declarations:
// the same originals, each covered once, prefixes Harmony can bind, the
// guard naming its original, and nothing at all while no exact store exists.
internal static class ExactStorageGuardTests
{
    private static readonly string[] OldNames = { "Store", "AddElement", "AddGasChunk", "AddLiquid", "AddOre", "AddToPrimaryElement", "ConsumeAndGetDisease",
        "ConsumeIgnoringDisease", "ConsumeAllIgnoringDisease", "Remove", "Drop", "DropAll", "DropSome", "DropUnlessMatching", "DropUnlessHasTag", "DropHasTags",
        "ForceStore", "Transfer", "TransferMass", "TransferUnitMass", "ClearItems" };
    private static readonly string[] OldItemNames = { "SetMass", "SetTemperature", "SetMassTemperature", "SetElement", "ModifyDiseaseCount", "AddDisease" };

    internal static void Run(Action<bool> check)
    {
        var old = typeof(Storage).GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.DeclaredOnly)
            .Where(method => OldNames.Contains(method.Name)).Cast<MethodBase>().ToList();
        var oldItems = OldItemNames.Select(name => (MethodBase)AccessTools.Method(typeof(PrimaryElement), name)).ToList();
        check(old.Count >= 21 && oldItems.All(method => method != null));
        var classes = typeof(MixedAtmospheres.Oni.ColonyExactStorageMutations).Assembly.GetTypes()
            .Where(type => type.Namespace == "MixedAtmospheres.Oni" && (type.Name.StartsWith("ColonyExactStore", StringComparison.Ordinal) || type.Name.StartsWith("ColonyExactItem", StringComparison.Ordinal))).ToList();
        check(classes.Count == 31 && classes.All(type => type.GetCustomAttributes(typeof(HarmonyPatch), false).Length == 1));
        var covered = new List<MethodBase>(); var items = new List<MethodBase>();
        foreach (var type in classes)
        {
            var bulk = type.GetMethod("TargetMethods", BindingFlags.Static | BindingFlags.NonPublic);
            var single = type.GetMethod("TargetMethod", BindingFlags.Static | BindingFlags.NonPublic);
            var targets = bulk != null ? ((IEnumerable<MethodBase>)bulk.Invoke(null, null)!).ToList() : new List<MethodBase> { (MethodBase)single!.Invoke(null, null)! };
            var prepare = type.GetMethod("Prepare", BindingFlags.Static | BindingFlags.NonPublic);
            // Every typed class has targets in the inspected game; only the
            // generic fallback is empty (and so not installed).
            check(prepare == null || (bool)prepare.Invoke(null, null)! == (type.Name != "ColonyExactStoreUntyped"));
            check(type.Name != "ColonyExactStoreUntyped" ? targets.Count > 0 : targets.Count == 0);
            var prefix = type.GetMethod("Prefix", BindingFlags.Static | BindingFlags.NonPublic)!;
            foreach (var target in targets)
            {
                // Harmony binds __instance to the declaring type and __N to the
                // original's N-th parameter of the same type.
                foreach (var parameter in prefix.GetParameters())
                {
                    if (parameter.Name == "__instance") { check(parameter.ParameterType == target.DeclaringType); continue; }
                    if (parameter.Name == "__args" || parameter.Name == "__originalMethod") continue;
                    var index = int.Parse(parameter.Name!.Substring(2));
                    check(index < target.GetParameters().Length && target.GetParameters()[index].ParameterType == parameter.ParameterType);
                }
                if (target.DeclaringType == typeof(PrimaryElement)) items.Add(target); else covered.Add(target);
            }
            if (targets.Count == 0) continue;
            // The guard: nothing without stores; with one, it names its original
            // (the storage itself first; null arguments name nothing more).
            var arguments = prefix.GetParameters().Select(parameter => (object?)null).ToArray();
            MixedAtmospheres.Oni.ColonyOwnedStorage.Count = 0; MixedAtmospheres.Oni.ColonyOwnedStorage.Rejected.Clear();
            prefix.Invoke(null, arguments);
            check(MixedAtmospheres.Oni.ColonyOwnedStorage.Rejected.Count == 0);
            MixedAtmospheres.Oni.ColonyOwnedStorage.Count = 1;
            prefix.Invoke(null, arguments);
            var expected = (targets[0].DeclaringType == typeof(PrimaryElement) ? "item " : "") + targets[0].Name;
            check(targets.All(target => target.Name == targets[0].Name) && MixedAtmospheres.Oni.ColonyOwnedStorage.Rejected.SequenceEqual(new[] { expected }));
            MixedAtmospheres.Oni.ColonyOwnedStorage.Count = 0;
        }
        check(covered.Count == covered.Distinct().Count() && new HashSet<MethodBase>(covered).SetEquals(old));
        check(items.Count == 6 && new HashSet<MethodBase>(items).SetEquals(oldItems));
        check(!MixedAtmospheres.Oni.ColonyExactStorageMutations.Untyped().Any());
    }
}
