using System;

namespace MixedAtmospheres.Oni
{
    // Diagnostic accounting only: never changes the offered/admitted timestep.
    internal readonly struct ColonyPacingLossToken
    {
        internal readonly long Epoch, Sequence;
        internal readonly int Depth;
        internal readonly ulong Session;
        internal ColonyPacingLossToken(long epoch, long sequence, int depth, ulong session)
        { Epoch = epoch; Sequence = sequence; Depth = depth; Session = session; }
    }
    internal struct ColonyPacingLossTotals
    {
        internal long Calls, OriginalRuns, OriginalSkipped, PacingCalls, BusyCalls, ClippedCalls, FullyHeldCalls;
        internal long EarlierZeroCalls, OtherVetoCalls, Exceptions, Overflow, ScopeErrors, InvalidDt, AttributionErrors;
        internal ulong FirstFrame, LastFrame;
        internal double RawOfferedSeconds, OriginalCapLostSeconds, OfferedSeconds, AdmittedSeconds, LostSeconds, PacingLostSeconds, OtherLostSeconds, GainedSeconds;
    }
    internal sealed class ColonyPacingLoss
    {
        internal const int Capacity = 8;
        private struct Scope
        {
            internal long Sequence;
            internal bool Open, Track, Valid, PacingSeen, Busy, PacingReturned;
            internal ulong Frame;
            internal float RawOffered, Offered, BeforePacing, AfterPacing;
        }
        private readonly Scope[] scopes = new Scope[Capacity];
        private long epoch = 1, sequence;
        private int depth;
        private ColonyPacingLossTotals totals;
        internal int Depth => depth;
        internal ColonyPacingLossTotals Snapshot => totals;
        internal static float Capped(float value) => float.IsNaN(value) || float.IsInfinity(value) || value < 0 ? 0 : Math.Min(value, .2f);
        private static bool Valid(float value) => !float.IsNaN(value) && !float.IsInfinity(value) && value >= 0;
        internal ColonyPacingLossToken Begin(float offered, bool track, ulong frame, ulong session)
        {
            var token = new ColonyPacingLossToken(epoch, ++sequence, ++depth, session);
            if (depth > Capacity) { if (track) ++totals.Overflow; return token; }
            scopes[depth - 1] = new Scope { Sequence = token.Sequence, Open = true, Track = track,
                Valid = Valid(offered), Frame = frame, RawOffered = Valid(offered) ? offered : 0, Offered = Capped(offered) };
            return token;
        }
        internal void Pacing(float before, float after, bool returned, bool busy)
        {
            if (depth <= 0 || depth > Capacity) return;
            ref var scope = ref scopes[depth - 1];
            scope.PacingSeen = true; scope.Busy = busy; scope.PacingReturned = returned;
            scope.BeforePacing = Capped(before); scope.AfterPacing = returned ? Capped(after) : 0;
            scope.Valid &= Valid(before) && (!returned || Valid(after));
        }
        private bool Take(ColonyPacingLossToken token, out Scope scope)
        {
            scope = default;
            if (token.Epoch != epoch || token.Depth <= 0) return false;
            // A normal finalizer follows an already-accounted postfix.
            if (token.Depth > depth) return false;
            if (token.Depth != depth) { ++totals.ScopeErrors; ++epoch; depth = 0; Array.Clear(scopes, 0, scopes.Length); return false; }
            if (depth > Capacity) { --depth; return false; }
            if (!scopes[depth - 1].Open || scopes[depth - 1].Sequence != token.Sequence) return false;
            scope = scopes[depth - 1]; scopes[depth - 1] = default; --depth; return scope.Track;
        }
        internal void Complete(ColonyPacingLossToken token, float actual, bool originalRan)
        {
            if (!Take(token, out var scope)) return;
            if (totals.Calls++ == 0) totals.FirstFrame = totals.LastFrame = scope.Frame;
            else { totals.FirstFrame = Math.Min(totals.FirstFrame, scope.Frame); totals.LastFrame = Math.Max(totals.LastFrame, scope.Frame); }
            if (!scope.Valid || !Valid(actual)) ++totals.InvalidDt;
            var admitted = originalRan ? Capped(actual) : 0;
            var loss = Math.Max(0, (double)scope.Offered - admitted);
            var pacingLoss = scope.PacingSeen && scope.Busy ? Math.Max(0, (double)scope.BeforePacing - scope.AfterPacing) : 0;
            totals.RawOfferedSeconds += scope.RawOffered;
            totals.OriginalCapLostSeconds += Math.Max(0, (double)scope.RawOffered - scope.Offered);
            totals.OfferedSeconds += scope.Offered; totals.AdmittedSeconds += admitted; totals.LostSeconds += loss;
            totals.PacingLostSeconds += pacingLoss; totals.OtherLostSeconds += Math.Max(0, loss - pacingLoss);
            totals.GainedSeconds += Math.Max(0, (double)admitted - scope.Offered);
            if (pacingLoss > loss) ++totals.AttributionErrors;
            if (originalRan) ++totals.OriginalRuns; else ++totals.OriginalSkipped;
            if (scope.PacingSeen) ++totals.PacingCalls;
            if (scope.Busy) ++totals.BusyCalls;
            if (scope.PacingSeen && scope.Busy && scope.PacingReturned && pacingLoss > 0) ++totals.ClippedCalls;
            var fullHold = scope.PacingSeen && scope.Busy && !scope.PacingReturned && scope.BeforePacing > 0;
            if (fullHold) ++totals.FullyHeldCalls;
            if (scope.Offered > 0 && originalRan && admitted == 0 && (!scope.PacingSeen || scope.BeforePacing == 0)) ++totals.EarlierZeroCalls;
            if (scope.Offered > 0 && !originalRan && !fullHold) ++totals.OtherVetoCalls;
        }
        internal void Exception(ColonyPacingLossToken token)
        { if (Take(token, out _)) ++totals.Exceptions; }
        internal void Cancel(ColonyPacingLossToken token) { Take(token, out _); }
        internal ColonyPacingLossTotals Drain() { var result = totals; totals = default; return result; }
        internal void Reset() { ++epoch; depth = 0; totals = default; Array.Clear(scopes, 0, scopes.Length); }
    }
}
