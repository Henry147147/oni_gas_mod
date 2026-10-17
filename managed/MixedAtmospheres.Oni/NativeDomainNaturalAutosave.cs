using System;
using System.Linq;
using HarmonyLib;
using Klei;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool NaturalAutosaveRequired => ColonyRuntimeValidation.Settings?.VerifyNaturalAutosave == true;
        private static bool naturalAutosaveConfigured, naturalAutosaveVerified;
        private static int naturalAutosaveOriginalInterval, naturalAutosaveRequests, naturalAutosaveRequestedCycle;
        private static int savedNativeCycle;
        private static double savedNativeGameSeconds;
        private static byte[]? naturalAutosavePhysical;
        private static ulong naturalAutosaveContinuationFrames;
        private static string naturalAutosaveExpectedHash = "", naturalAutosaveCurrentHash = "";
        private static void ConfigureNaturalAutosaveControl()
        {
            if (!NaturalAutosaveRequired) return;
            naturalAutosaveOriginalInterval = SaveGame.Instance.AutoSaveCycleInterval;
            SaveGame.Instance.AutoSaveCycleInterval = 1;
            Require(!GenericGameSettings.instance.disableAutosave, "Natural autosaving is disabled in this disposable game.");
            naturalAutosaveConfigured = true;
        }
        internal static void ObserveNaturalAutosaveRequest(int cycle)
        {
            if (!NaturalAutosaveRequired || !active) return;
            ++naturalAutosaveRequests; naturalAutosaveRequestedCycle = cycle;
        }
        private static void ObserveNaturalAutosaveCompleted(byte[] physical)
        {
            if (!NaturalAutosaveRequired || !savedAsAutoSave) return;
            Require(naturalAutosaveConfigured && naturalAutosaveRequests == 1 && naturalAutosaveRequestedCycle >= 1,
                "An autosave bypassed the actual day-rollover request.");
            naturalAutosavePhysical = (byte[])physical.Clone();
        }
        private static void VerifyNaturalAutosaveControl()
        {
            Require(naturalAutosaveConfigured && naturalAutosavePhysical != null && colonyFileSaved && savedAsAutoSave &&
                naturalAutosaveRequests == 1 && naturalAutosaveRequestedCycle >= 1 && savedNativeCycle == naturalAutosaveRequestedCycle &&
                savedNativeGameSeconds >= 600 && GameClock.Instance.GetCycle() >= savedNativeCycle &&
                automaticSaveAdmissions == 1 && stagedSaveWrites == 1 && savedFileReadbackVerified && savePauseStateRestored &&
                stagedColonySave == null && savedSourceFrame > 0 && savedSourceFrame < latest.CompletedFrames,
                "Natural day rollover did not complete exactly one paired autosave and resume the game.");
            naturalAutosaveContinuationFrames = latest.CompletedFrames - savedSourceFrame;
            var expected = naturalAutosavePhysical!;
            for (ulong frame = 0; frame < naturalAutosaveContinuationFrames; ++frame)
                expected = ResumePhysicalCheckpoint(expected, latest.LastTimestepSeconds);
            Require(TryCapturePhysicalCheckpoint(out var current), "Natural-autosave continuation has no completed physical image.");
            naturalAutosaveExpectedHash = CheckpointHash(expected); naturalAutosaveCurrentHash = CheckpointHash(current);
            naturalAutosaveVerified = expected.SequenceEqual(current);
            Require(naturalAutosaveVerified, "Physical continuation after the natural autosave repeated or lost a worker interval.");
        }
        private static object ColonyClockReport() => new { Cycle = GameClock.Instance?.GetCycle(),
            NativeGameSeconds = GameClock.Instance?.GetTime(), AutoSaveCycleInterval = SaveGame.Instance?.AutoSaveCycleInterval };
        private static object NaturalAutosaveReport() => new { Required = NaturalAutosaveRequired, Configured = naturalAutosaveConfigured,
            Verified = naturalAutosaveVerified, Source = "GameClock.DoAutoSave", Requests = naturalAutosaveRequests,
            RequestedCycle = naturalAutosaveRequestedCycle, OriginalCycleInterval = naturalAutosaveOriginalInterval,
            SavedNativeCycle = savedNativeCycle, SavedNativeGameSeconds = savedNativeGameSeconds,
            ExactContinuationFrames = naturalAutosaveContinuationFrames,
            ExpectedPhysicalSha256 = naturalAutosaveExpectedHash, CurrentPhysicalSha256 = naturalAutosaveCurrentHash };
    }

    [HarmonyPatch(typeof(GameClock), "DoAutoSave")]
    internal static class NativeNaturalAutosaveRequestPatch
    {
        private static void Prefix(int __0) => NativeSealedDomainValidation.ObserveNaturalAutosaveRequest(__0);
    }
}
