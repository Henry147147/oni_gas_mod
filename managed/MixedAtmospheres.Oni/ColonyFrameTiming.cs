using System;
using System.Collections.Generic;

namespace MixedAtmospheres.Oni
{
    // Diagnostic bookkeeping only: a submit may precede publication of the
    // previous completion, so the last observed frame is not its identity.
    internal sealed class ColonyFrameTiming
    {
        private readonly Queue<(ulong Frame, long At)> pending = new Queue<(ulong, long)>();
        private readonly int maximumPending;
        private ulong submitted;
        internal long DroppedSubmissions { get; private set; }
        internal int PendingCount => pending.Count;

        internal ColonyFrameTiming(int maximumPending = 256)
        {
            if (maximumPending <= 0) throw new ArgumentOutOfRangeException(nameof(maximumPending));
            this.maximumPending = maximumPending;
        }

        internal ulong Submit(ulong observed, long at)
        {
            submitted = Math.Max(submitted, observed) + 1;
            if (pending.Count == maximumPending) { pending.Dequeue(); ++DroppedSubmissions; }
            pending.Enqueue((submitted, at));
            return submitted;
        }

        internal bool Observe(ulong completed, out long submittedAt)
        {
            while (pending.Count != 0 && pending.Peek().Frame < completed) pending.Dequeue();
            var matched = pending.Count != 0 && pending.Peek().Frame == completed;
            submittedAt = matched ? pending.Dequeue().At : 0;
            return matched;
        }

        internal void Reset() { pending.Clear(); submitted = 0; DroppedSubmissions = 0; }
    }
}
