using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace MixedAtmospheres.Oni
{
    internal static class NativePressureReadPlan
    {
        internal static float Kilopascals(double physicalPascals)
        {
            if (double.IsNaN(physicalPascals) || double.IsInfinity(physicalPascals) || physicalPascals < 0)
                throw new ArgumentException("Physical pressure must be finite and nonnegative.");
            var value = (float)(physicalPascals / 1000);
            if (float.IsInfinity(value) || physicalPascals > 0 && value == 0)
                throw new NotSupportedException("Physical pressure is outside the native reader's scalar range.");
            return value;
        }

        // Every caller of the pressure indexer in the three game types that
        // read it, found by its call token so that the list follows the game:
        // build 763796 turned PropertyTextures.UpdatePressure from a method
        // into a lambda held in a static field, whose body lives in a
        // compiler-generated nested type. Rewriting each call site also covers
        // the tiny inlinable indexer. The complete assembly is checked against
        // this plan by tools/OwnedSnapshotTests.
        internal const int ExpectedCallSites = 5;
        internal static IReadOnlyList<MethodInfo> CallSites(Assembly game)
        {
            var getter = game.GetType("Grid+PressureIndexer", true)!.GetProperty("Item")!.GetMethod!;
            var token = BitConverter.GetBytes(getter.MetadataToken);
            const BindingFlags declared = BindingFlags.Static | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
            IEnumerable<Type> family(Type type) => new[] { type }.Concat(
                type.GetNestedTypes(BindingFlags.Public | BindingFlags.NonPublic).SelectMany(family));
            var found = new[] { "DevToolSimDebug", "PropertyTextures", "SimDebugView" }
                .SelectMany(name => family(game.GetType(name, true)!))
                .SelectMany(type => type.GetMethods(declared))
                .Where(method => Calls(method, token)).OrderBy(method => method.MetadataToken).ToArray();
            if (found.Length != ExpectedCallSites)
                throw new InvalidOperationException("Native pressure call sites differ from the inspected builds: " +
                    string.Join(", ", found.Select(method => method.DeclaringType!.FullName + "::" + method.Name)));
            return found;
        }
        // A call or callvirt of the token. A byte scan can only find too many
        // (operand bytes that happen to match), which the count then refuses.
        private static bool Calls(MethodInfo method, byte[] token)
        {
            var il = method.GetMethodBody()?.GetILAsByteArray();
            if (il == null) return false;
            for (var offset = 0; offset + 4 < il.Length; ++offset)
                if ((il[offset] == 0x28 || il[offset] == 0x6f) && il[offset + 1] == token[0] && il[offset + 2] == token[1] &&
                    il[offset + 3] == token[2] && il[offset + 4] == token[3]) return true;
            return false;
        }
    }
}
