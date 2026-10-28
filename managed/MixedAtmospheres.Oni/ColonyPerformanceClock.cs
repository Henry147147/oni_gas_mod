using System;

namespace MixedAtmospheres.Oni
{
    // Diagnostics only. Physical and game progress come from their absolute
    // clocks, never from how many completions the presentation happened to see.
    internal sealed class ColonyPerformanceClock
    {
        internal enum ResetReason { None, Initial, Session, FrameBackwards, OwnerClockBackwards, GameClockBackwards, WallClockBackwards, InvalidClock }

        internal readonly struct Mark
        {
            internal readonly long Generation, WallTicks, PausedObservations;
            internal readonly ulong Session, Frame;
            internal readonly double OwnerSeconds, GameSeconds;
            internal readonly bool Paused;
            internal Mark(long generation, ulong session, ulong frame, double ownerSeconds, double gameSeconds,
                long wallTicks, long pausedObservations, bool paused)
            {
                Generation = generation; Session = session; Frame = frame; OwnerSeconds = ownerSeconds;
                GameSeconds = gameSeconds; WallTicks = wallTicks; PausedObservations = pausedObservations; Paused = paused;
            }
        }

        internal readonly struct Measurement
        {
            internal readonly bool Valid, PauseObserved;
            internal readonly ulong Session, FirstFrame, LastFrame;
            internal readonly double OwnerSeconds, GameSeconds, WallSeconds;
            internal ulong Frames => LastFrame - FirstFrame;
            internal double OwnerPerReal => WallSeconds > 0 ? OwnerSeconds / WallSeconds : 0;
            internal double GamePerReal => WallSeconds > 0 ? GameSeconds / WallSeconds : 0;
            internal Measurement(Mark first, Mark last, long frequency)
            {
                Valid = first.Generation == last.Generation && last.WallTicks > first.WallTicks;
                Session = last.Session; FirstFrame = first.Frame; LastFrame = last.Frame;
                OwnerSeconds = last.OwnerSeconds - first.OwnerSeconds; GameSeconds = last.GameSeconds - first.GameSeconds;
                WallSeconds = (last.WallTicks - first.WallTicks) / (double)frequency;
                PauseObserved = first.Paused || last.Paused || last.PausedObservations > first.PausedObservations;
            }
        }

        private readonly long frequency;
        private long generation, pausedObservations;
        private Mark first, current;
        internal bool Ready { get; private set; }
        internal Mark Current => current;
        internal Measurement PreviousRun { get; private set; }

        internal ColonyPerformanceClock(long frequency)
        {
            if (frequency <= 0) throw new ArgumentOutOfRangeException(nameof(frequency));
            this.frequency = frequency;
        }

        internal ResetReason Observe(ulong session, ulong frame, double ownerSeconds, double gameSeconds, long wallTicks, bool paused)
        {
            var reason = ResetReason.None;
            if (!FiniteNonnegative(ownerSeconds) || !FiniteNonnegative(gameSeconds) || wallTicks < 0) reason = ResetReason.InvalidClock;
            else if (!Ready) reason = ResetReason.Initial;
            else if (session != current.Session) reason = ResetReason.Session;
            else if (frame < current.Frame) reason = ResetReason.FrameBackwards;
            else if (ownerSeconds < current.OwnerSeconds) reason = ResetReason.OwnerClockBackwards;
            else if (gameSeconds < current.GameSeconds) reason = ResetReason.GameClockBackwards;
            else if (wallTicks < current.WallTicks) reason = ResetReason.WallClockBackwards;
            if (reason != ResetReason.None)
            {
                PreviousRun = Ready ? Since(first) : default;
                ++generation; pausedObservations = 0;
                Ready = reason != ResetReason.InvalidClock;
                if (!Ready) { first = current = default; return reason; }
                first = current = new Mark(generation, session, frame, ownerSeconds, gameSeconds, wallTicks, 0, paused);
                return reason;
            }
            if (paused) ++pausedObservations;
            current = new Mark(generation, session, frame, ownerSeconds, gameSeconds, wallTicks, pausedObservations, paused);
            return reason;
        }

        internal Measurement Since(Mark mark) => Ready && mark.Generation == generation ? new Measurement(mark, current, frequency) : default;
        internal Measurement Total => Since(first);
        internal void Reset() { Ready = false; ++generation; first = current = default; PreviousRun = default; pausedObservations = 0; }
        private static bool FiniteNonnegative(double value) => !double.IsNaN(value) && !double.IsInfinity(value) && value >= 0;
    }
}
