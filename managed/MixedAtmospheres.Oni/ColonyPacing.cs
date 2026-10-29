using System;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Pacing (docs/colony-owner.md#pacing). The owner steps on its own solver
    // thread, so the game thread never waits for it. While the owner is busy
    // the game runs its sub ticks up to the next 200 ms tick and holds there,
    // and the game clock runs at the player's speed or the owner's measured
    // rate, whichever is slower, so play is slow motion rather than stop-go.
    // The camera, menus and pause use unscaled time and stay responsive.
    internal static class ColonyPacing
    {
        internal static bool Enabled = true;
        internal static long HeldTicks { get; private set; }
        internal static long UnityFrames { get; private set; }
        internal static double OwnerRate { get; private set; } // game seconds per real second, smoothed
        internal static float TimeScale => Time.timeScale;
        private static ulong seenFrames;
        private static bool scaled;
        private static float playerScale = 1;
        private static readonly bool publicationLog = Environment.GetEnvironmentVariable("MA_COLONY_PUBLICATION_LOG") == "1";
        private static long updateTicks, tickTicks, updateCalls, tickCalls, maximumUpdateTicks, loggedHeldTicks;
        private static ulong loggedFrame = ulong.MaxValue;
        private static readonly AccessTools.FieldRef<Game, int> subTick = AccessTools.FieldRefAccess<Game, int>("simSubTick");
        // The player's speed settings, read without per-frame reflection.
        private static readonly AccessTools.FieldRef<SpeedControlScreen, float> normalSpeed = AccessTools.FieldRefAccess<SpeedControlScreen, float>("normalSpeed");
        private static readonly AccessTools.FieldRef<SpeedControlScreen, float> fastSpeed = AccessTools.FieldRefAccess<SpeedControlScreen, float>("fastSpeed");
        private static readonly AccessTools.FieldRef<SpeedControlScreen, float> ultraSpeed = AccessTools.FieldRefAccess<SpeedControlScreen, float>("ultraSpeed");
        private static readonly AccessTools.FieldRef<Game, float> simDt = AccessTools.FieldRefAccess<Game, float>("simDt");

        internal static void Reset()
        {
            ColonyPacingLossLog.Reset();
            ColonyPerformanceLog.FinishRun();
            HeldTicks = UnityFrames = 0; OwnerRate = 0; seenFrames = 0;
            updateTicks = tickTicks = updateCalls = tickCalls = maximumUpdateTicks = loggedHeldTicks = 0;
            loggedFrame = ulong.MaxValue;
        }
        internal static bool Busy => Enabled && ColonyOwner.Active && OniNativeColony.Busy() != 0;

        // Game.SimEveryTick runs one 1/60 s sub tick per 1/60 s of game time
        // and posts the 200 ms simulation tick on every twelfth. While the
        // owner is busy, the sub ticks before that one still run.
        internal static bool BeforeSimEveryTick(Game game, ref float dt)
        {
            if (ColonyFrameLog.Enabled)
            {
                var framed = ColonyFrameLog.Now;
                try { return BeforeSimEveryTickTimed(game, ref dt); } finally { ColonyFrameLog.Add(ColonyFrameLog.Part.Tick, framed); }
            }
            return BeforeSimEveryTickTimed(game, ref dt);
        }
        private static bool BeforeSimEveryTickTimed(Game game, ref float dt)
        {
            if (!publicationLog) return BeforeTick(game, ref dt);
            var begun = System.Diagnostics.Stopwatch.GetTimestamp();
            var result = BeforeTick(game, ref dt);
            tickTicks += System.Diagnostics.Stopwatch.GetTimestamp() - begun; ++tickCalls;
            return result;
        }

        private static bool BeforeTick(Game game, ref float dt)
        {
            if (!Busy)
            {
                if (ColonyPacingLossLog.Enabled) ColonyPacingLossLog.Pacing(dt, dt, true, false);
                return true;
            }
            var before = dt;
            var allowed = (12 - subTick(game)) / 60f - simDt(game) - 1e-4f;
            if (Mathf.Min(dt, .2f) < allowed)
            {
                if (ColonyPacingLossLog.Enabled) ColonyPacingLossLog.Pacing(before, dt, true, true);
                return true;
            }
            ++HeldTicks;
            if (allowed <= 0)
            {
                if (ColonyPacingLossLog.Enabled) ColonyPacingLossLog.Pacing(before, 0, false, true);
                return false;
            }
            dt = allowed;
            if (ColonyPacingLossLog.Enabled) ColonyPacingLossLog.Pacing(before, dt, true, true);
            return true;
        }

        internal static void Update()
        {
            var framed = ColonyFrameLog.Now;
            try { UpdateTimed(); } finally { ColonyPacingLossLog.AfterUpdate(); ColonyFrameLog.Add(ColonyFrameLog.Part.Pacing, framed); }
        }
        private static void UpdateTimed()
        {
            if (!publicationLog) { UpdateClock(); return; }
            var begun = System.Diagnostics.Stopwatch.GetTimestamp();
            UpdateClock();
            var elapsed = System.Diagnostics.Stopwatch.GetTimestamp() - begun;
            updateTicks += elapsed; ++updateCalls; maximumUpdateTicks = Math.Max(maximumUpdateTicks, elapsed);
            var frame = ColonyOwner.Status.CompletedFrames;
            if (!ColonyOwner.Active || frame == loggedFrame) return;
            var factor = 1000.0 / System.Diagnostics.Stopwatch.Frequency;
            Debug.Log(FormattableString.Invariant($"[Mixed Atmospheres] pacing frame {frame} update_calls {updateCalls} update_ms {updateTicks * factor:F3} update_max_ms {maximumUpdateTicks * factor:F3} tick_calls {tickCalls} tick_ms {tickTicks * factor:F3} held {HeldTicks - loggedHeldTicks} owner_rate {OwnerRate:F3} scale {Time.timeScale:F3}"));
            loggedFrame = frame; loggedHeldTicks = HeldTicks;
            updateTicks = tickTicks = updateCalls = tickCalls = maximumUpdateTicks = 0;
        }

        private static void UpdateClock()
        {
            if (!Enabled || !ColonyOwner.Active)
            {
                // The owner stopped: return the player's own speed.
                if (scaled && SpeedControlScreen.Instance != null && !SpeedControlScreen.Instance.IsPaused) Time.timeScale = playerScale;
                scaled = false; return;
            }
            ++UnityFrames;
            ColonyPerformanceLog.UnityFrame(Time.unscaledDeltaTime);
            var status = ColonyOwner.Status;
            if (status.CompletedFrames != seenFrames && status.LastAdvanceMilliseconds > 0)
            {
                seenFrames = status.CompletedFrames;
                var measured = status.LastFrameSeconds / (status.LastAdvanceMilliseconds / 1000);
                OwnerRate = OwnerRate == 0 ? measured : .7 * OwnerRate + .3 * measured;
                ColonyPerformanceLog.OwnerFrame(status, OwnerRate, HeldTicks);
            }
            var speed = SpeedControlScreen.Instance;
            if (speed == null || speed.IsPaused || OwnerRate == 0) return;
            playerScale = speed.GetSpeed() switch { 0 => normalSpeed(speed), 1 => fastSpeed(speed), _ => ultraSpeed(speed) };
            var target = Mathf.Clamp((float)(.95 * OwnerRate), .01f, playerScale);
            if (Mathf.Abs(Time.timeScale - target) > 1e-3f * target) { Time.timeScale = target; scaled = true; }
        }
    }
    // Off by default. Entry observes the caller's dt before the existing
    // Priority.First load/continuation guards; final admission observes whether
    // the original actually ran. All strings are emitted at bounded windows.
    internal static class ColonyPacingLossLog
    {
        internal static readonly bool Enabled = Environment.GetEnvironmentVariable("MA_COLONY_PACING_LOSS_LOG") == "1";
        private const ulong WindowFrames = 50;
        private static readonly ColonyPacingLoss totals = new ColonyPacingLoss();
        private static ulong session, bucket;
        private static bool haveWindow;
        internal static ColonyPacingLossToken Begin(float dt)
        {
            var active = ColonyOwner.Active;
            var status = active ? ColonyOwner.Status : default;
            var nextFrame = status.CompletedFrames + 1;
            var nextBucket = status.CompletedFrames / WindowFrames;
            if (active && (status.Session != session || !haveWindow || (bucket != nextBucket && totals.Depth == 0)))
            {
                Emit();
                if (status.Session != session) totals.Reset();
                session = status.Session; bucket = nextBucket; haveWindow = true;
            }
            return totals.Begin(dt, active, nextFrame, status.Session);
        }
        internal static void Pacing(float before, float after, bool returned, bool busy) => totals.Pacing(before, after, returned, busy);
        internal static void Complete(ColonyPacingLossToken token, float dt, bool originalRan)
        {
            if (!ColonyOwner.Active || ColonyOwner.Status.Session != token.Session) totals.Cancel(token);
            else totals.Complete(token, dt, originalRan);
        }
        internal static void Exception(ColonyPacingLossToken token) => totals.Exception(token);
        internal static void End(ColonyPacingLossToken token) => totals.Cancel(token);
        internal static void AfterUpdate()
        {
            if (!Enabled || !haveWindow || totals.Depth != 0 || !ColonyOwner.Active) return;
            if (ColonyOwner.Status.CompletedFrames >= (bucket + 1) * WindowFrames) { Emit(); haveWindow = false; }
        }
        internal static void Reset()
        {
            if (!Enabled) return;
            Emit(); totals.Reset(); session = bucket = 0; haveWindow = false;
        }
        private static void Emit()
        {
            var value = totals.Drain();
            if (value.Calls == 0 && value.Exceptions == 0 && value.Overflow == 0 && value.ScopeErrors == 0) return;
            Debug.Log(FormattableString.Invariant($"[Mixed Atmospheres] pacing loss session {session} from_frame {value.FirstFrame} through_frame {value.LastFrame} calls {value.Calls} original {value.OriginalRuns} skipped {value.OriginalSkipped} raw_offered_s {value.RawOfferedSeconds:R} offered_capped_s {value.OfferedSeconds:R} original_cap_lost_s {value.OriginalCapLostSeconds:R} admitted_s {value.AdmittedSeconds:R} lost_s {value.LostSeconds:R} raw_lost_s {value.OriginalCapLostSeconds + value.LostSeconds:R} pacing_lost_s {value.PacingLostSeconds:R} other_lost_s {value.OtherLostSeconds:R} gained_s {value.GainedSeconds:R} pacing_calls {value.PacingCalls} busy {value.BusyCalls} clipped {value.ClippedCalls} fully_held {value.FullyHeldCalls} earlier_zero {value.EarlierZeroCalls} other_veto {value.OtherVetoCalls} exceptions {value.Exceptions} overflow {value.Overflow} scope_errors {value.ScopeErrors} invalid_dt {value.InvalidDt} attribution_errors {value.AttributionErrors}"));
        }
    }
    [HarmonyPatch(typeof(Game), "SimEveryTick")]
    internal static class ColonyPacingLossPatch
    {
        private static bool Prepare() => ColonyPacingLossLog.Enabled;
        [HarmonyPriority(Priority.First + 1)]
        private static void Prefix(float __0, out ColonyPacingLossToken __state) => __state = ColonyPacingLossLog.Begin(__0);
        [HarmonyPriority(Priority.Last)]
        private static void Postfix(float __0, bool __runOriginal, ColonyPacingLossToken __state) => ColonyPacingLossLog.Complete(__state, __0, __runOriginal);
        private static Exception? Finalizer(Exception? __exception, ColonyPacingLossToken __state)
        {
            if (__exception != null) ColonyPacingLossLog.Exception(__state);
            else ColonyPacingLossLog.End(__state);
            return __exception;
        }
    }
    public sealed class ColonyPacingDriver : MonoBehaviour
    {
        // A zero-time native publication can run game scheduler callbacks
        // recursively. Free a retiring grid only after its resource callback
        // has returned to Unity, never from inside that publication.
        private void LateUpdate(){ColonyClusterRetirement.PumpDeletes();ColonyOwner.PumpDeferredByproducts();}
        internal static void Ensure()
        {
            if (FindFirstObjectByType<ColonyPacingDriver>() != null) return;
            var holder = new GameObject("Mixed Atmospheres colony pacing");
            DontDestroyOnLoad(holder); holder.AddComponent<ColonyPacingDriver>();
        }
        private void Update() => ColonyPacing.Update();
    }
    [HarmonyPatch(typeof(Game), "SimEveryTick")]
    internal static class ColonyPacingTickPatch
    {
        private static bool Prefix(Game __instance, ref float __0) => ColonyPacing.BeforeSimEveryTick(__instance, ref __0);
    }
}
