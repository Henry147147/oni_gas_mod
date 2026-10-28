using System;

namespace MixedAtmospheres.Oni
{
    // Delivery is independent of sampled completion counters and diagnostics.
    internal static class ColonyActivityDelivery
    {
        internal static bool NeedsRefresh(ColonyActivitySnapshot current, ColonyActivitySnapshot? previous) =>
            current.Touched.Length != 0 || previous == null || !current.Actors.SequenceEqual(previous.Actors);
        internal static void Validate(ulong session, double seconds, ulong observedSession, uint restored, ulong head, double savedSeconds)
        {
            if (session == 0 || observedSession != session) throw new InvalidOperationException("Region activity belongs to a different colony session.");
            if (!(seconds > 0) || double.IsNaN(seconds) || double.IsInfinity(seconds)) throw new ArgumentOutOfRangeException(nameof(seconds));
            if (restored == 0) return;
            if (head == 0 || BitConverter.DoubleToInt64Bits(seconds) != BitConverter.DoubleToInt64Bits(savedSeconds))
                throw new InvalidOperationException("The restored region activity frame has a different timestep or sequence. Saved inputs cannot be regenerated.");
        }
    }
}
