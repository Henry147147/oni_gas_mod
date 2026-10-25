using System;
using System.Collections.Generic;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // An exception escaping one of the mod's paths inside ONI's per-frame
    // update (receipt settlement, the owner's publication and messages, owned
    // pipes, their heat and germ steps, vents, sensors and readers) becomes ONE
    // owner fault, like a failed solver frame (docs/colony-owner.md): the owner
    // stops, fluids and owned pipes freeze, the notice shows and saving is
    // refused. It is never rethrown into the game's sim step, where it would
    // stop the rest of every later step without telling the player.
    internal static class ColonyFault
    {
        private static readonly object gate = new object();
        private static volatile string reason = "";
        internal static string Reason => reason;
        internal static bool Faulted => reason.Length != 0;
        // Exceptions contained (the first faults; later ones are only counted).
        internal static int Contained { get; private set; }
        internal static void Reset()
        {
            lock (gate) { reason = ""; Contained = 0; injected = ""; Rehearse = false; Rehearsed.Clear(); InjectedFault = false; }
        }

        internal static void Raise(string where, Exception error)
        {
            var injectedError = error is Injected;
            lock (gate)
            {
                ++Contained;
                if (injectedError && Rehearse) { Rehearsed.Add(((Injected)error).Path + "@" + where); return; }
                if (Faulted) return;
                reason = where + ": " + (error.Message.Length != 0 ? error.Message : error.GetType().Name);
                InjectedFault = injectedError;
            }
            // An injected control fault is expected: a warning, not a mod error.
            if (injectedError) Debug.LogWarning("[Mixed Atmospheres] Contained the injected " + where + " fault; the colony owner stops.");
            else Debug.LogError("[Mixed Atmospheres] " + where + " failed; the colony owner stops: " + error);
            try
            {
                if (ColonyOwner.Active)
                {
                    var result = OniNativeColony.Fault(reason);
                    if (result != 0) Debug.LogError("[Mixed Atmospheres] The colony owner could not record the fault (result " + result + ").");
                }
            }
            catch (Exception failure) { Debug.LogError("[Mixed Atmospheres] The colony owner could not record the fault: " + failure.Message); }
        }
        // A Harmony finalizer's body for a contained path.
        internal static Exception? Contain(Exception? error, string where)
        {
            if (error != null) Raise(where, error);
            return null;
        }

        // Fault-containment control (harness only, scenario fault-containment):
        // the next pass through the named path throws. A rehearsal records the
        // containment without faulting, so every path can be tried in one game.
        internal sealed class Injected : InvalidOperationException
        {
            internal readonly string Path;
            internal Injected(string path) : base("Injected fault (" + path + ").") { Path = path; }
        }
        private static volatile string injected = "";
        internal static bool Rehearse;
        internal static bool InjectedFault { get; private set; }
        internal static readonly List<string> Rehearsed = new List<string>();
        internal static string Armed => injected;
        // Where an injected path's throw was contained ("" before it was).
        internal static string RehearsedAt(string path)
        {
            lock (gate) foreach (var entry in Rehearsed) if (entry.StartsWith(path + "@", StringComparison.Ordinal)) return entry.Substring(path.Length + 1);
            return "";
        }
        internal static void Arm(string where) => injected = where;
        internal static void Inject(string where)
        {
            if (injected.Length == 0 || injected != where) return;
            lock (gate) { if (injected != where) return; injected = ""; }
            throw new Injected(where);
        }
    }

    // Owned gas and liquid pipes (their moves, ports, filters, valves and
    // vents' intakes all run in this step) freeze after a fault; an exception
    // escaping the step faults the owner instead of stopping the sim step.
    [HarmonyPatch(typeof(ConduitFlow), nameof(ConduitFlow.Sim200ms))]
    internal static class ColonyFaultPipeFlowPatch
    {
        private static bool Prefix(ConduitFlow __instance) => !ColonyFault.Faulted || !ColonyConduits.OwnsFlow(__instance);
        private static Exception? Finalizer(Exception? __exception, ConduitFlow __instance) =>
            __exception != null && ColonyConduits.OwnsFlow(__instance) ? ColonyFault.Contain(__exception, "pipe flow") : __exception;
    }
}
