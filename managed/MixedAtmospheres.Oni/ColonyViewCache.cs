using System;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    // The native publication is immutable. Only the fluid-view memcpy can be
    // reused; all native body snapshots, events, receipts and actors still run.
    internal static class ColonyViewCache
    {
        private static readonly bool disabled = Environment.GetEnvironmentVariable("MA_COLONY_UNCACHED_VIEWS") == "1";
        private static readonly bool control = Environment.GetEnvironmentVariable("MA_COLONY_VIEW_CACHE_CONTROL") == "1";
        private static bool supported = true;
        private static ulong session = ulong.MaxValue, frame = ulong.MaxValue;
        private static OniNativeColony.CellView[]? buffer;
        private static OniNativeColony.CellView[] expected = Array.Empty<OniNativeColony.CellView>();
        internal static int Copies { get; private set; }
        internal static int Hits { get; private set; }
        internal static int Comparisons { get; private set; }
        internal static int ChangedDuringComparison { get; private set; }

        internal static void Reset()
        {
            session = frame = ulong.MaxValue; buffer = null;
            Copies = Hits = Comparisons = ChangedDuringComparison = 0;
        }
        internal static void Copy(ref OniNativeColony.Status status, OniNativeColony.CellView[] views)
        {
            if (!ReferenceEquals(buffer, views)) { Reset(); buffer = views; }
            int result; bool copied;
            if (disabled || !supported || ColonyCopyValidation.Enabled || ColonyCopyValidation.UseMarshalledOutputs)
            {
                result = OniNativeColony.Copy(ref status, views, (uint)views.Length); copied = true;
            }
            else
            {
                try { result = OniNativeColony.CopyChanged(session, frame, ref status, views, out copied); }
                catch (EntryPointNotFoundException)
                {
                    // A developer bundle with an older native binding retains
                    // the complete-copy behavior rather than an empty cache.
                    supported = false; result = OniNativeColony.Copy(ref status, views, (uint)views.Length); copied = true;
                }
            }
            OniNativeColony.Check(result);
            session = status.Session; frame = status.CompletedFrames;
            if (copied) ++Copies; else ++Hits;
            if (!control || Comparisons >= 512) return;
            if (expected.Length != views.Length) expected = new OniNativeColony.CellView[views.Length];
            var full = OniNativeColony.Status.Empty;
            OniNativeColony.Check(OniNativeColony.Copy(ref full, expected, (uint)expected.Length));
            if (full.Session != session || full.CompletedFrames != frame) { ++ChangedDuringComparison; return; }
            if (!MemoryMarshal.AsBytes(views.AsSpan()).SequenceEqual(MemoryMarshal.AsBytes(expected.AsSpan())))
                throw new InvalidOperationException("Cached colony view bytes differ from a complete copy.");
            ++Comparisons;
        }
    }
}
