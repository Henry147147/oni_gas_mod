using System;

namespace MixedAtmospheres.Oni
{
    // One detection window of UnityFrameTiming, free of Unity so tools/
    // ColonyPerformanceTests can drive it: frames whose engine-reported delta
    // falls well short of the real time between frames.
    internal sealed class FrameClockWindow
    {
        internal const int WindowFrames = 240;
        internal const double ShortFrame = .75, ShortShare = .5, ReportedShare = .8, MaximumFrameSeconds = .25;
        internal int Frames { get; private set; }
        internal int ShortFrames { get; private set; }
        internal double RealSeconds { get; private set; }
        internal double ReportedSeconds { get; private set; }
        // Adds one frame; true when the window is complete. Loading screens
        // and hitches (over 0.25 s of real time) say nothing about the clock.
        internal bool Add(double real, double reported)
        {
            if (!(real > 0) || real > MaximumFrameSeconds || double.IsNaN(reported)) return false;
            ++Frames; RealSeconds += real; ReportedSeconds += Math.Min(Math.Max(reported, 0), MaximumFrameSeconds);
            if (reported < ShortFrame * real) ++ShortFrames;
            return Frames >= WindowFrames;
        }
        internal bool Mismatched => Frames > 0 && ShortFrames >= ShortShare * Frames && ReportedSeconds < ReportedShare * RealSeconds;
        internal void Clear() { Frames = ShortFrames = 0; RealSeconds = ReportedSeconds = 0; }
    }
}
