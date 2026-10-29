using System;

namespace MixedAtmospheres.Oni
{
    // Diagnostic interval only: a whole-owner sink is an upper bound on the
    // arena's lost trace, not an attribution or an exact regional mass ledger.
    internal static class ColonyBreathAccounting
    {
        internal const double AbsoluteTolerance = 1e-10;
        private static bool Finite(double value) => !double.IsNaN(value) && !double.IsInfinity(value);
        internal static bool Compatible(double expected, double observed, double traceUpperBound) =>
            Finite(expected) && expected > AbsoluteTolerance && Finite(observed) && observed > 0 &&
            Finite(traceUpperBound) && traceUpperBound >= 0 && traceUpperBound < expected &&
            observed <= expected + AbsoluteTolerance && observed + traceUpperBound >= expected - AbsoluteTolerance;
    }
}
