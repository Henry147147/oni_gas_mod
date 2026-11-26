using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Moono (github.com/tigin-backwards/oni-moono) runs the game's own DLLs on
    // CoreCLR with its own reimplementation of Unity
    // (docs/game-update-resilience.md#moono). It has no mod API; these are its
    // markers. Never throws: a marker that cannot be read reads as unknown.
    internal static class MoonoHost
    {
        private const string EngineAssembly = "OniMoono.UnityEngine", HostAssembly = "OniMoono";
        private const string TweakOwnerPrefix = "OniMoono.Host.GameTweaks.", HostOwnerPrefix = "OniMoono.Host.";

        private static bool? isMoono;

        // Unity's types come from Moono's engine assembly.
        internal static bool IsMoono
        {
            get
            {
                if (isMoono is bool known) return known;
                bool found;
                try { found = typeof(UnityEngine.Object).Assembly.GetName().Name == EngineAssembly; }
                catch (Exception) { found = false; }
                isMoono = found;
                return found;
            }
        }

        private static Assembly? Host()
        {
            try { return AppDomain.CurrentDomain.GetAssemblies().FirstOrDefault(a => a.GetName().Name == HostAssembly); }
            catch (Exception) { return null; }
        }

        // The release tag (vYYYY.MM.DD.NN) the host was built as; "unknown
        // release" for a build without one.
        internal static string Release
        {
            get
            {
                try
                {
                    var host = Host();
                    var tag = host?.GetCustomAttributesData().Where(a => a.AttributeType.FullName == "System.Reflection.AssemblyMetadataAttribute" &&
                        a.ConstructorArguments.Count == 2 && (a.ConstructorArguments[0].Value as string) == "MoonoRelease")
                        .Select(a => a.ConstructorArguments[1].Value as string).FirstOrDefault(v => !string.IsNullOrEmpty(v));
                    return tag ?? "(unknown release)";
                }
                catch (Exception) { return "(unknown release)"; }
            }
        }

        // The source commit in the host's informational version ("1.0.0+<commit>").
        internal static string Commit
        {
            get
            {
                try
                {
                    var version = Host()?.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "";
                    var plus = version.IndexOf('+');
                    return plus >= 0 && plus + 1 < version.Length ? version.Substring(plus + 1) : "unknown";
                }
                catch (Exception) { return "unknown"; }
            }
        }

        // " on Moono <release>" for refusal and verified lines; empty on Unity.
        internal static string OnHost => IsMoono ? " on Moono " + Release : "";

        // A Harmony owner that is Moono's host (its tweaks and fixes), not a mod.
        internal static bool IsHostOwner(string owner) => owner != null && owner.StartsWith(HostOwnerPrefix, StringComparison.Ordinal);

        // Moono's game tweaks that have installed Harmony patches so far, by
        // name. Tweaks may install after the mod loads: read when needed.
        internal static IReadOnlyList<string> ActiveTweaks()
        {
            if (!IsMoono) return Array.Empty<string>();
            try
            {
                var names = new SortedSet<string>(StringComparer.Ordinal);
                foreach (var method in Harmony.GetAllPatchedMethods())
                {
                    var info = Harmony.GetPatchInfo(method);
                    if (info == null) continue;
                    foreach (var owner in info.Owners)
                        if (owner.StartsWith(TweakOwnerPrefix, StringComparison.Ordinal)) names.Add(owner.Substring(TweakOwnerPrefix.Length));
                }
                return names.ToArray();
            }
            catch (Exception) { return Array.Empty<string>(); }
        }

        // One Player.log line at Mod.OnLoad on Moono.
        internal static string LoadLine() =>
            "[Mixed Atmospheres] Running on Moono " + Release + " (commit " + Commit + ", .NET " + Environment.Version + "): the game contract is checked by member keys " +
            "and the game's files (docs/game-update-resilience.md#moono).";
    }
}
