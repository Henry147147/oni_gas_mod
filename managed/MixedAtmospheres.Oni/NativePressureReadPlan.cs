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

        // Exact-build callers, independently enumerated from both assemblies.
        // Rewriting each call site also covers the tiny inlinable indexer.
        internal static IReadOnlyList<MethodInfo> CallSites(Assembly game)
        {
            var names = new[] { ("DevToolSimDebug", "RenderTo"), ("PropertyTextures", "UpdatePressure"),
                ("SimDebugView", "GetPressureMapColour"), ("SimDebugView", "GetDangerColour"),
                ("SimDebugView", "GetSimCheckErrorMapColour") };
            return names.Select(item => game.GetType(item.Item1, true)!.GetMethods(BindingFlags.Static | BindingFlags.Instance |
                BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly).Single(method => method.Name == item.Item2)).ToArray();
        }
    }
}
