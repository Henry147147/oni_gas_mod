using System;
using System.IO;
using System.Runtime.CompilerServices;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Check native enum-to-tag helpers before and after Harmony composition.
    // No aliases or additional elements are registered.
    internal static class NativeElementTagChecks
    {
        internal static bool Verified { get; private set; }
        internal static void ResetAfterFailedLoad() => Verified = false;
        private static bool Same(Tag tag, string name) => tag.Name == name && tag.GetHash() == Hash.SDBMLower(name);
        [MethodImpl(MethodImplOptions.NoInlining)]
        internal static void WarmNativeCalls()
        {
            Verified = false;
            foreach (var name in new[] { "Create", "CreateTag" })
            {
                var method = AccessTools.Method(typeof(GameTagExtensions), name, new[] { typeof(SimHashes) })
                    ?? throw new MissingMethodException("Native tag helper unavailable: " + name);
                if (!(method.Invoke(null, new object[] { SimHashes.Oxygen }) is Tag tag) || !Same(tag, "Oxygen"))
                    throw new InvalidDataException("Native tag helper differs before patching: " + name);
            }
        }
        [MethodImpl(MethodImplOptions.NoInlining)]
        internal static void VerifyCalls()
        {
            Verified = false;
            foreach (var id in new[] { SimHashes.Oxygen, SimHashes.Water, SimHashes.Unobtanium, SimHashes.Vacuum, (SimHashes)int.MaxValue })
            {
                var text = id.ToString(); var assigned = default(Tag); assigned.Name = text;
                if (!Same(GameTagExtensions.Create(id), text) || !Same(id.CreateTag(), text) || !Same(TagManager.Create(text), text) || !Same(assigned, text))
                    throw new InvalidDataException("Native tag helper changed its identity: " + (int)id);
            }
            Verified = true;
        }
    }
}
