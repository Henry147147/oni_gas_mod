using System;

namespace MixedAtmospheres.Oni
{
    internal sealed class ColonyActivityResavePoint
    {
        internal ulong Session, Frame, Committed, Next, Head;
        internal uint Pending, Enabled;
        internal double OwnerSeconds, GameSeconds, HeadSeconds;
        internal long ActorInputs;
    }
    internal static class ColonyActivityResaveProof
    {
        internal static void ValidateImmutableOwner(ReadOnlySpan<byte> before, ReadOnlySpan<byte> after)
        {
            if (before.Length == 0 || !before.SequenceEqual(after))
                throw new InvalidOperationException("A paused owner resave changed immutable queued inputs, packet payloads or owner checkpoint bytes.");
        }
        internal static void Validate(ColonyActivityResavePoint before, ColonyActivityResavePoint after,
            ReadOnlySpan<byte> physicalBefore, ReadOnlySpan<byte> physicalAfter)
        {
            if (before.Session == 0 || before.Session != after.Session || before.Frame != after.Frame ||
                !Same(before.OwnerSeconds, after.OwnerSeconds) || !Same(before.GameSeconds, after.GameSeconds) || before.ActorInputs != after.ActorInputs)
                throw new InvalidOperationException("A paused owner resave changed its session, physical clock, frame or actor input count.");
            if (before.Committed != after.Committed || before.Next != after.Next || before.Head != after.Head || before.Pending != after.Pending ||
                before.Enabled != after.Enabled || !Same(before.HeadSeconds, after.HeadSeconds))
                throw new InvalidOperationException("A paused owner resave changed its saved activity delivery boundary.");
            if (physicalBefore.Length == 0 || !physicalBefore.SequenceEqual(physicalAfter))
                throw new InvalidOperationException("A paused owner resave changed its physical checkpoint bytes.");
        }
        private static bool Same(double a, double b) => BitConverter.DoubleToInt64Bits(a) == BitConverter.DoubleToInt64Bits(b);
    }
}
