using System;

namespace MixedAtmospheres.Oni
{
    internal sealed class ColonyFrameWindow
    {
        private ulong? baseline;
        internal ulong Baseline => baseline ?? throw new InvalidOperationException("Owner frame window has not started.");
        internal void Start(ulong frame) { if (!baseline.HasValue) baseline = frame; }
        internal bool HasReached(ulong frame, ulong advancedFrames)
            => frame >= Baseline && frame - Baseline >= advancedFrames;
        internal void Reset() => baseline = null;
    }
}
