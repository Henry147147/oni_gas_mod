using System;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // The colony owner's save barrier (docs/colony-owner.md#saves). The owner
    // steps on its own thread one frame ahead of what the game has seen, so at
    // a save its last completed frame has receipts the game has not received
    // (consumer and breath mass, emitter callbacks, byproducts, pipe outcomes).
    // Before ONI opens or writes anything, the barrier waits for the worker and
    // publishes that frame through ONI's own zero-time sim step, so every
    // callback is dispatched by the game's normal code before the native
    // section is captured. Nothing advances while the synchronous save runs.
    // A save requested inside an unfinished Game.StepTheSim is deferred through
    // the game's delayed-save entry instead.
    internal static class ColonySaveBarrier
    {
        private static int stepping;
        private static readonly MethodInfo SimTick = AccessTools.Method(typeof(Game), "UnsafeSim200ms") ??
            throw new MissingMethodException(nameof(Game), "UnsafeSim200ms");
        private static readonly MethodInfo Publish = AccessTools.Method(typeof(Game), "StepTheSim") ??
            throw new MissingMethodException(nameof(Game), "StepTheSim");
        internal static int Settled { get; private set; } // barriers that had to publish a pending frame
        internal static int Deferred { get; private set; }
        internal static int BodyHeatChecks { get; private set; }
        internal static int NativePublications { get; private set; }
        internal static long ReceiptsAtSave { get; private set; }
        internal static ulong LastSettledFrame { get; private set; }
        internal static ulong LastSettledSession { get; private set; }
        internal static void Reset() { Settled = Deferred = BodyHeatChecks = NativePublications = 0; ReceiptsAtSave = 0; LastSettledFrame = LastSettledSession = 0; }
        internal static object Report() => new { Settled, Deferred, ReceiptsAtSave, BodyHeatChecks, NativePublications, LastSettledFrame, LastSettledSession, LastSynchronization };

        internal static void EnterStep() => ++stepping;
        internal static void ExitStep() { if (stepping > 0) --stepping; }

        // SaveLoader.Save(string, bool, bool) entry. False: deferred.
        internal static bool BeforeFileSave(string filename, bool isAutoSave, bool updateSavePointer)
        {
            if (!ColonyOwner.Active) return true;
            if (stepping > 0)
            {
                ++Deferred;
                Game.Instance.StartDelayedSave(filename, isAutoSave, updateSavePointer);
                return false;
            }
            _ = Settle();
            return true;
        }

        // Quiescent: worker idle, every receipt delivered, owner running.
        internal static bool Quiescent(out OniNativeColony.Status status)
        {
            OniNativeColony.Check(OniNativeColony.Wait());
            status = OniNativeColony.Status.Empty;
            OniNativeColony.Check(OniNativeColony.CopyStatus(ref status));
            return status.ReceiptsPending == 0 && OniNativeColony.Busy() == 0;
        }

        // ONI's sim thread runs native frames in order (next-frame .. end-frame).
        private const int NextFrameHook = 8, EndFrameHook = 19;
        private static (ulong Started, ulong Ended) NativeFrames()
        { var reading = OniNativeObserver.CaptureStatus(); return (reading.Hooks[NextFrameHook].Entries, reading.Hooks[EndFrameHook].Exits); }
        internal static string LastSynchronization { get; private set; } = "";
        // ONI posts a native frame and its sim thread may run it later: wait
        // until, counted from `before` (read just before posting), at least one
        // frame has started and every frame started since has ended. Frames run
        // in order, so every earlier posted frame (and any positive frame's
        // hand-off to the owner) is then complete. Counters are compared as
        // deltas: their absolute values need not match.
        internal static (ulong Started, ulong Ended) NativeFrameCounters() => NativeFrames();
        internal static (ulong Started, ulong Ended) WaitNativeFrameAfter((ulong Started, ulong Ended) before, int timeoutMilliseconds = 10000)
        {
            var timer = System.Diagnostics.Stopwatch.StartNew();
            var frames = NativeFrames();
            while (!(frames.Started > before.Started && frames.Ended - before.Ended >= frames.Started - before.Started))
            {
                if (timer.ElapsedMilliseconds > timeoutMilliseconds)
                    throw new TimeoutException($"ONI's native frame did not end (started/ended {before.Started}/{before.Ended} -> {frames.Started}/{frames.Ended}).");
                System.Threading.Thread.Sleep(1); frames = NativeFrames();
            }
            return frames;
        }

        // Returns the settled owner frame.
        internal static ulong Settle()
        {
            if (stepping > 0) throw new InvalidOperationException("A colony save cannot settle inside an unfinished simulation step.");
            ColonySavedParticles.RestorePending();
            ColonyPipeSave.TryRestore();
            if (ColonyPipeRuntime.Active) ColonyConduitsSchedule.FlushTopology();
            Quiescent(out var entry);
            if (entry.State != 1) throw new InvalidOperationException("A faulted colony owner cannot be saved: " + entry.Error);
            var before = NativeFrames(); var timer = System.Diagnostics.Stopwatch.StartNew();
            // Zero time: no owner frame is submitted and no native physics
            // advances; the game dispatches the owner's completed frame.
            SimTick.Invoke(Game.Instance, new object[] { 0f });
            var after = NativeFrames(); var waited = WaitNativeFrameAfter(before);
            LastSynchronization = $"native frames started/ended before {before.Started}/{before.Ended}, after zero tick {after.Started}/{after.Ended}, settled {waited.Started}/{waited.Ended} in {timer.Elapsed.TotalMilliseconds:F1} ms";
            UnityEngine.Debug.Log("[Mixed Atmospheres] save barrier: " + LastSynchronization);
            var pending = entry.ReceiptsPending;
            // UnsafeSim200ms publishes while its newly posted native frame
            // can still be running. After waiting for completion, publish
            // again even when the owner has no receipts: native body work and
            // element chunks have their own GameData/callback publication.
            Publish.Invoke(Game.Instance, new object[] { 0f }); ++NativePublications;
            for (var round = 0; round < 64 && !Quiescent(out var _); ++round)
            { Publish.Invoke(Game.Instance, new object[] { 0f }); ++NativePublications; }
            if (!Quiescent(out var status))
                throw new InvalidOperationException($"The colony owner did not settle before saving: {status.ReceiptsPending} receipts pending, busy {OniNativeColony.Busy()}.");
            ReceiptsAtSave += (long)pending; ++Settled; settledForSave = true;
            // Validation: pipe contact work the native sim applies to bodies
            // one step later must be published before the capture.
            if (ColonyRuntimeValidation.Settings != null && ColonyPipeContactHeat.BodyHeatMismatch() is string mismatch)
                throw new InvalidOperationException("Pipe body heat is still pending at the save barrier: " + mismatch);
            if (ColonyRuntimeValidation.Settings != null && ColonyPipeRuntime.Active) ++BodyHeatChecks;
            return Record(status);
        }
        // A world is about to disappear. Settle native work first, then drain
        // follow-up owner inputs and precise outcomes at the SAME fluid time.
        // Only lifecycle adapters call this; ordinary saves retain their
        // existing queued-input checkpoint semantics.
        internal static ulong FlushInputs()
        {
            if(OniNativeColony.InputFlushVersion()!=1)
                throw new NotSupportedException("World teardown requires matching native input-boundary support.");
            _=Settle();settledForSave=false;
            for(var round=0;round<64;++round)
            {
                OniNativeColony.Check(OniNativeColony.FlushInputs(out var submitted,out var deferred));
                if(submitted==0&&deferred==0&&Quiescent(out var status))return Record(status);
                ColonyOwner.InvalidateLifecycleReadings();
                // Install the edited owner's vacuum/mass in native buffers as
                // well as managed Grid. Otherwise cleanup could refund an old
                // scalar shadow after its complete mixture had already left.
                var before=NativeFrames();
                SimTick.Invoke(Game.Instance,new object[]{0f});
                _=WaitNativeFrameAfter(before);
                Publish.Invoke(Game.Instance,new object[]{0f});++NativePublications;
            }
            throw new InvalidOperationException("World teardown could not settle all owner inputs and callbacks without advancing time.");
        }
        // Sim.Save: reuse this save's barrier when it ran at the file entry
        // (nothing has advanced since); any other entry settles here.
        internal static ulong ForCapture()
        {
            if (!settledForSave) return Settle();
            if (!Quiescent(out var status)) throw new InvalidOperationException("The colony owner advanced after its save barrier.");
            return Record(status);
        }
        private static bool settledForSave;
        internal static void SaveFinished() => settledForSave = false;
        private static ulong Record(OniNativeColony.Status status)
        {
            if (status.State != 1) throw new InvalidOperationException("A faulted colony owner cannot be saved: " + status.Error);
            LastSettledFrame = status.CompletedFrames; LastSettledSession = status.Session;
            return status.CompletedFrames;
        }
    }

    [HarmonyPatch(typeof(SaveLoader), nameof(SaveLoader.Save), new[] { typeof(string), typeof(bool), typeof(bool) })]
    internal static class ColonySaveBarrierPatch
    {
        private static bool Prefix(string __0, bool __1, bool __2, ref string __result)
        {
            if (ColonySaveBarrier.BeforeFileSave(__0, __1, __2)) return true;
            __result = __0; return false;
        }
        private static Exception? Finalizer(Exception? __exception) { ColonySaveBarrier.SaveFinished(); return __exception; }
    }

    [HarmonyPatch(typeof(Game), "StepTheSim")]
    internal static class ColonySaveStepPatch
    {
        private static void Prefix() { ColonyPipeSave.TryRestore(); ColonySaveBarrier.EnterStep(); ColonyFault.Inject("sim-step"); }
        // The last net: any exception escaping the game's sim step while the
        // owner runs faults it (ColonyFault) instead of stopping every step.
        private static Exception? Finalizer(Exception? __exception)
        {
            ColonySaveBarrier.ExitStep();
            return __exception != null && ColonyOwner.Active ? ColonyFault.Contain(__exception, "game sim step") : __exception;
        }
    }
}
