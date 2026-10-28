using System;
using System.Collections;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using MixedAtmospheres.Core;

namespace MixedAtmospheres.Oni
{
    // Completed owner readings kept as the compact native arrays they arrive
    // in (docs/colony-owner.md#presentation-cost). A cell's heavy reading and
    // presentation objects are built only when a reader asks for that cell, so
    // a completed frame costs one array copy instead of tens of thousands of
    // objects, which is what kept the garbage collector busy.
    //
    // A buffer belongs to the published frame that holds it. It is recycled
    // only after a newer frame replaced it and RecycleAfterSeconds have passed;
    // readers hold a frame for at most a Unity frame (the status display for a
    // refresh), so no reader can see a buffer rewritten under it. Each buffer
    // carries a lease number and a reader that finds it changed treats the
    // cell as absent and counts the event (StaleReads) instead of reading
    // another frame's values.
    internal sealed class ColonyReadingBuffer
    {
        internal OniNativeColony.Reading[] Readings = Array.Empty<OniNativeColony.Reading>();
        internal OniNativeColony.Constituent[] Parts = Array.Empty<OniNativeColony.Constituent>();
        internal int Count, PartCount;
        internal ulong Frame;
        internal long Lease;
        internal long ReleasedAt;      // Stopwatch ticks when replaced; 0 while published or being filled
        internal bool Pooled;
        internal bool Sorted;          // PublicCell ascending: lookups bisect
        internal Dictionary<int, int>? Index; // otherwise, built once on first lookup
    }

    internal static class ColonyReadingPool
    {
        internal const double RecycleAfterSeconds = 2;
        private const int MaximumPooled = 48;
        private static readonly List<ColonyReadingBuffer> buffers = new List<ColonyReadingBuffer>();
        private static long leases;
        internal static long Allocated { get; private set; }
        internal static long StaleReads;

        // A buffer with room for the sizes; the caller fills it before Seal.
        internal static ColonyReadingBuffer Acquire(int readings, int parts)
        {
            var now = Stopwatch.GetTimestamp(); var age = (long)(RecycleAfterSeconds * Stopwatch.Frequency);
            ColonyReadingBuffer? chosen = null;
            lock (buffers)
            {
                foreach (var b in buffers)
                    if (b.ReleasedAt != 0 && now - b.ReleasedAt >= age && b.Readings.Length >= readings && b.Parts.Length >= parts) { chosen = b; break; }
                if (chosen == null)
                {
                    // Reuse the oldest free buffer's slot for a larger one rather than growing the pool.
                    foreach (var b in buffers) if (b.ReleasedAt != 0 && now - b.ReleasedAt >= age) { chosen = b; break; }
                    if (chosen != null) { chosen.Readings = new OniNativeColony.Reading[readings + readings / 4 + 16]; chosen.Parts = new OniNativeColony.Constituent[parts + parts / 4 + 16]; ++Allocated; }
                }
                if (chosen == null)
                {
                    chosen = new ColonyReadingBuffer
                    {
                        Readings = new OniNativeColony.Reading[readings + readings / 4 + 16], Parts = new OniNativeColony.Constituent[parts + parts / 4 + 16],
                        Pooled = buffers.Count < MaximumPooled
                    };
                    ++Allocated;
                    if (chosen.Pooled) buffers.Add(chosen);
                }
                chosen.ReleasedAt = 0; chosen.Lease = ++leases; chosen.Index = null; chosen.Count = chosen.PartCount = 0;
            }
            return chosen;
        }

        // The buffer is not published (or has been replaced): it may be reused after the grace period.
        internal static void Release(ColonyReadingBuffer? buffer)
        {
            if (buffer == null) return;
            lock (buffers) buffer.ReleasedAt = Stopwatch.GetTimestamp();
        }

        internal static void Reset() { lock (buffers) buffers.Clear(); }
    }

    // The readings of one completed frame, built on demand per cell.
    internal sealed class ColonyLazyReadings : ILazyReadings
    {
        private readonly ColonyReadingBuffer buffer;
        private readonly long lease;
        private readonly ColonyPresentation.Table table;
        private readonly long revision;
        private readonly Action<int, string> rejected;
        internal ColonyLazyReadings(ColonyReadingBuffer buffer, ColonyPresentation.Table table, long revision, Action<int, string> rejected)
        { this.buffer = buffer; lease = buffer.Lease; this.table = table; this.revision = revision; this.rejected = rejected; Count = buffer.Count; }

        public int Count { get; }
        public IReadOnlyDictionary<int, MixturePresentation> Describe(MixturePresentationModel model) => new ColonyLazyPresentations(this, model);
        internal bool Current => Volatile.Read(ref buffer.Lease) == lease;

        private int IndexOf(int cell)
        {
            var readings = buffer.Readings;
            if (buffer.Sorted)
            {
                int low = 0, high = buffer.Count - 1;
                while (low <= high)
                {
                    var mid = (low + high) >> 1; var value = readings[mid].PublicCell;
                    if (value == (uint)cell) return mid;
                    if (value < (uint)cell) low = mid + 1; else high = mid - 1;
                }
                return -1;
            }
            var index = buffer.Index;
            if (index == null)
            {
                index = new Dictionary<int, int>(buffer.Count);
                for (var i = 0; i < buffer.Count; ++i) index[(int)readings[i].PublicCell] = i;
                buffer.Index = index;
            }
            return index.TryGetValue(cell, out var found) ? found : -1;
        }

        public bool TryGetValue(int key, out MixtureThermodynamicReading value)
        {
            value = null!;
            if (!Current) { Interlocked.Increment(ref ColonyReadingPool.StaleReads); return false; }
            var i = IndexOf(key);
            if (i < 0) return false;
            try { value = ColonyPresentation.Create(table, buffer.Readings[i], revision); }
            catch (ArgumentException error) { rejected(key, error.Message); return false; }
            // The buffer was recycled while this reading was built.
            if (!Current) { Interlocked.Increment(ref ColonyReadingPool.StaleReads); value = null!; return false; }
            return true;
        }
        public bool ContainsKey(int key) => Current && IndexOf(key) >= 0;
        public MixtureThermodynamicReading this[int key] => TryGetValue(key, out var value) ? value : throw new KeyNotFoundException();
        public IEnumerable<int> Keys { get { for (var i = 0; i < Count; ++i) yield return (int)buffer.Readings[i].PublicCell; } }
        public IEnumerable<MixtureThermodynamicReading> Values { get { foreach (var pair in this) yield return pair.Value; } }
        public IEnumerator<KeyValuePair<int, MixtureThermodynamicReading>> GetEnumerator()
        {
            for (var i = 0; i < Count; ++i)
            {
                var cell = (int)buffer.Readings[i].PublicCell;
                if (TryGetValue(cell, out var reading)) yield return new KeyValuePair<int, MixtureThermodynamicReading>(cell, reading);
            }
        }
        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }

    // Presentations of those readings, described on first use and kept for the
    // frame (readers ask for the same visible cells every Unity frame).
    internal sealed class ColonyLazyPresentations : IReadOnlyDictionary<int, MixturePresentation>
    {
        private readonly ColonyLazyReadings readings;
        private readonly MixturePresentationModel model;
        private readonly ConcurrentDictionary<int, MixturePresentation?> cache = new ConcurrentDictionary<int, MixturePresentation?>();
        internal ColonyLazyPresentations(ColonyLazyReadings readings, MixturePresentationModel model) { this.readings = readings; this.model = model; }

        public int Count => readings.Count;
        public bool TryGetValue(int key, out MixturePresentation value)
        {
            if (cache.TryGetValue(key, out var hit)) { value = hit!; return hit != null; }
            var heap = ColonyFrameLog.HeapNow; var collections = ColonyFrameLog.Enabled ? GC.CollectionCount(0) : 0;
            if (!readings.TryGetValue(key, out var reading)) { if (readings.Current) cache[key] = null; value = null!; return false; }
            ColonyFrameLog.CountCell();
            var described = model.Describe(reading);
            ColonyFrameLog.CountCellBytes(heap, collections);
            if (readings.Current) cache[key] = described;
            value = described; return true;
        }
        public bool ContainsKey(int key) => TryGetValue(key, out _);
        public MixturePresentation this[int key] => TryGetValue(key, out var value) ? value : throw new KeyNotFoundException();
        public IEnumerable<int> Keys => readings.Keys;
        public IEnumerable<MixturePresentation> Values { get { foreach (var pair in this) yield return pair.Value; } }
        public IEnumerator<KeyValuePair<int, MixturePresentation>> GetEnumerator()
        {
            foreach (var cell in readings.Keys)
                if (TryGetValue(cell, out var value)) yield return new KeyValuePair<int, MixturePresentation>(cell, value);
        }
        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }
}
