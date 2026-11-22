using System;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    internal static class WindowsHarmonyPageAllocation
    {
        // The three supported games ship the same Harmony/MonoMod binary.
        private static readonly Guid BundledHarmony = new Guid("f14b4638-aa14-40a9-ae99-a46147fec4b9");

        internal static MethodInfo? Target()
        {
            var assembly = typeof(Harmony).Assembly;
            if (assembly.ManifestModule.ModuleVersionId != BundledHarmony) return null;

            var allocator = assembly.GetType("MonoMod.Core.Platforms.Systems.WindowsSystem+PageAllocator", true)!;
            var signature = new[] { typeof(IntPtr), typeof(IntPtr), typeof(bool), typeof(IntPtr).MakeByRefType() };
            return allocator.GetMethod("TryAllocatePage", signature)
                ?? throw new MissingMethodException(allocator.FullName, "TryAllocatePage");
        }

        internal static void Install(Harmony harmony)
        {
            var target = Target();
            if (target == null) return;
            var prefix = AccessTools.DeclaredMethod(typeof(WindowsHarmonyPageAllocation), nameof(RequireAddress));
            harmony.Patch(target, prefix: new HarmonyMethod(prefix));
        }

        // Zero asks VirtualAlloc to choose any address. In this overload MonoMod
        // needs the exact page it requested so its patch can reach that page.
        private static bool RequireAddress(IntPtr __0, ref IntPtr __3, ref bool __result)
        {
            if (__0 != IntPtr.Zero) return true;
            __3 = IntPtr.Zero;
            __result = false;
            return false;
        }
    }
}
