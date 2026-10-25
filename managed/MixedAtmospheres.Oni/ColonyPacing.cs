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
        private static readonly AccessTools.FieldRef<Game, float> simDt = AccessTools.FieldRefAccess<Game, float>("simDt");

        internal static void Reset()
        {
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
            if (!publicationLog) return BeforeTick(game, ref dt);
            var begun = System.Diagnostics.Stopwatch.GetTimestamp();
            var result = BeforeTick(game, ref dt);
            tickTicks += System.Diagnostics.Stopwatch.GetTimestamp() - begun; ++tickCalls;
            return result;
        }

        private static bool BeforeTick(Game game, ref float dt)
        {
            if (!Busy) return true;
            var allowed = (12 - subTick(game)) / 60f - simDt(game) - 1e-4f;
            if (Mathf.Min(dt, .2f) < allowed) return true;
            ++HeldTicks;
            if (allowed <= 0) return false;
            dt = allowed; return true;
        }

        internal static void Update()
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
            var t = Traverse.Create(speed);
            playerScale = speed.GetSpeed() switch
            {
                0 => t.Field<float>("normalSpeed").Value, 1 => t.Field<float>("fastSpeed").Value, _ => t.Field<float>("ultraSpeed").Value
            };
            var target = Mathf.Clamp((float)(.95 * OwnerRate), .01f, playerScale);
            if (Mathf.Abs(Time.timeScale - target) > 1e-3f * target) { Time.timeScale = target; scaled = true; }
        }
    }
    public sealed class ColonyPacingDriver : MonoBehaviour
    {
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
