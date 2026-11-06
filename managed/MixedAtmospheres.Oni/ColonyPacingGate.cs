using System;

namespace MixedAtmospheres.Oni
{
    // Pacing decisions (docs/colony-owner.md#pacing), kept free of the game so
    // tools/ColonyPacingTests can drive them against a model of SimEveryTick.
    internal static class ColonyPacingGate
    {
        // Game.SimEveryTick's sub tick and its 0.2 s argument cap.
        internal const float SubTickSeconds = 1f / 60f;
        internal const float MaximumStepSeconds = .2f;

        // While the owner is busy the game runs the 1/60 s sub ticks before its
        // next 200 ms tick and holds there. With overlapped pacing it also holds
        // a Unity frame too short for a sub tick: SimEveryTick would post a
        // zero-time native frame, and PrepareGameData would then wait for the
        // posted positive frame, which waits for the owner.
        // Returns whether SimEveryTick runs; `dt` may be shortened. `held` is
        // true when the call is skipped or shortened.
        internal static bool Admit(bool overlapped, int subTick, float simDt, ref float dt, out bool held)
        {
            var allowed = (12 - subTick) / 60f - simDt - 1e-4f;
            var offered = Min(dt, MaximumStepSeconds);
            if (overlapped && (float)(simDt + Min(offered, allowed > 0 ? allowed : 0f)) < SubTickSeconds) { held = true; return false; }
            if (offered < allowed) { held = false; return true; }
            held = true;
            if (allowed <= 0) return false;
            dt = allowed;
            return true;
        }
        // UnityEngine.Mathf.Min, as the game's own cap uses.
        private static float Min(float a, float b) => a < b ? a : b;
    }

    // The activity frames the game posted, for overlapped pacing
    // (docs/colony-owner.md#pacing). Posted: the frame the last positive native
    // frame delivers; the game holds its next 200 ms tick until the owner has
    // been handed it. Shown: the frame posted before it. The game's readers see
    // the owner state Shown started from, which is what that native frame
    // wrote into ONI's cells: owner frame T-2 at the tick that posts frame T.
    internal sealed class ColonyPostedFrames
    {
        internal ulong Posted { get; private set; }
        internal ulong Shown { get; private set; }
        internal void Post(ulong sequence)
        {
            if (sequence == 0 || sequence <= Posted) throw new InvalidOperationException("Activity frames must be posted in sequence order.");
            Shown = Posted; Posted = sequence;
        }
        internal void Reset() { Posted = Shown = 0; }
    }
}
