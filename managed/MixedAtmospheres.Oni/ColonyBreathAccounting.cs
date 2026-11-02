using System;

namespace MixedAtmospheres.Oni
{
    // Diagnostic accounting for one declared breath parcel deposited in a
    // sealed arena. The arena's carbon dioxide is sampled on every completed
    // owner frame and every loss is paired with the whole-owner trace-sink
    // increment of the SAME interval. The profile's trace sink is the only
    // booked way the arena's fluid can leave, so a loss in an interval where
    // the sink booked less than the loss, or any rise after the deposit, is a
    // fault. The owner books the sink as one whole-owner scalar and exposes
    // no per-region or per-species term, so the pairing is an interval bound,
    // not an attribution; it is still far tighter than comparing the endpoints
    // against the colony's cumulative sink increment.
    internal sealed class ColonyBreathAccounting
    {
        // Binary64 summation noise of ~50 sampled constituents and of the
        // owner's long-double sink ledger: far below any booked sink event.
        internal const double IntervalTolerance = 1e-15;
        private readonly ulong session;
        private readonly double expected;
        private ulong lastFrame;
        private double lastTrace, last;
        private bool deposited;
        private string error = "";
        internal ColonyBreathAccounting(ulong session, ulong frame, double expectedKg, double arenaCarbonKg, double traceSinkKg)
        {
            this.session = session; lastFrame = frame; expected = expectedKg; lastTrace = traceSinkKg; last = arenaCarbonKg;
            if (session == 0 || !Finite(expectedKg) || expectedKg <= 0) error = "Invalid breath parcel declaration.";
            else if (!Finite(arenaCarbonKg) || arenaCarbonKg != 0) error = "The arena already contained carbon dioxide.";
            else if (!Finite(traceSinkKg) || traceSinkKg < 0) error = "Invalid trace sink baseline.";
        }
        private static bool Finite(double value) => !double.IsNaN(value) && !double.IsInfinity(value);
        internal bool Valid => error.Length == 0;
        internal string Error => error;
        internal bool Deposited => deposited;
        internal int Intervals { get; private set; }
        internal int LossIntervals { get; private set; }
        internal double ArenaKg => last;
        internal double LostKg { get; private set; }
        // Sum of the whole-owner sink increments of exactly the intervals in
        // which the arena lost carbon dioxide.
        internal double PairedTraceKg { get; private set; }
        internal double TotalTraceKg { get; private set; }
        private void Fail(string reason) { if (error.Length == 0) error = reason; }
        // One completed owner frame (or the first completed frame after the
        // last observation). A repeated frame is ignored; time never runs back.
        internal void Observe(ulong observedSession, ulong frame, double arenaCarbonKg, double traceSinkKg)
        {
            if (error.Length != 0) return;
            if (observedSession != session) { Fail("The owner session changed."); return; }
            if (frame < lastFrame) { Fail("The owner frame ran backwards."); return; }
            if (frame == lastFrame) return;
            if (!Finite(arenaCarbonKg) || arenaCarbonKg < 0) { Fail("Invalid sampled carbon dioxide."); return; }
            if (!Finite(traceSinkKg) || traceSinkKg < lastTrace) { Fail("The trace sink decreased or is invalid."); return; }
            var sink = traceSinkKg - lastTrace;
            ++Intervals; TotalTraceKg += sink;
            if (arenaCarbonKg > expected + IntervalTolerance) { Fail("The arena holds more carbon dioxide than the declared parcel."); return; }
            if (!deposited)
            {
                if (arenaCarbonKg > 0) { deposited = true; last = expected; } // the one deposit lands in this interval
                else { lastFrame = frame; lastTrace = traceSinkKg; return; }
            }
            if (arenaCarbonKg > last + IntervalTolerance) { Fail("The arena gained carbon dioxide after the deposit."); return; }
            var loss = last - arenaCarbonKg;
            if (loss > sink + IntervalTolerance) { Fail("The arena lost more carbon dioxide than the trace sink booked in the same interval."); return; }
            if (loss > IntervalTolerance) { ++LossIntervals; LostKg += loss; PairedTraceKg += sink; }
            last = arenaCarbonKg; lastFrame = frame; lastTrace = traceSinkKg;
        }
        // The deposited parcel must be present, its whole loss must be booked
        // trace, and the booked trace of the loss intervals must be
        // informative (smaller than the parcel). A loss of zero is exact.
        internal bool Compatible =>
            Valid && deposited && last > 0 && last <= expected + IntervalTolerance &&
            LostKg <= PairedTraceKg + IntervalTolerance * Math.Max(1, Intervals) &&
            PairedTraceKg < expected;
    }
}
