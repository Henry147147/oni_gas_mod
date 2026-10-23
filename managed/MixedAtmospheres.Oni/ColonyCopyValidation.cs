using System;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    // Optional in-game control of the pinned output bindings against the
    // previous Mono array marshaller. Compare only the same completed frame;
    // a publication between the two calls is counted and tried again later.
    internal static class ColonyCopyValidation
    {
        internal static readonly bool Enabled = Environment.GetEnvironmentVariable("MA_COLONY_COPY_CONTROL") == "1";
        // A/B timings use the very same package and saved physical profile.
        internal static readonly bool UseMarshalledOutputs = Environment.GetEnvironmentVariable("MA_COLONY_MARSHALLED_OUTPUT") == "1";
        internal static readonly bool ReceiptEnabled = Environment.GetEnvironmentVariable("MA_COLONY_RECEIPT_COPY_CONTROL") == "1";
        internal static readonly bool UseMarshalledReceipts = Environment.GetEnvironmentVariable("MA_COLONY_MARSHALLED_RECEIPTS") == "1";
        internal static int ViewComparisons { get; private set; }
        internal static int ReadingComparisons { get; private set; }
        internal static int ChangedFrames { get; private set; }
        internal static int ReceiptComparisons { get; private set; }
        internal static int ReceiptEmptyComparisons { get; private set; }
        internal static ulong ReceiptEntriesCompared { get; private set; }
        internal static int ReceiptGrowthComparisons { get; private set; }
        private static ulong viewFrame = ulong.MaxValue, readingFrame = ulong.MaxValue;
        private static OniNativeColony.CellView[] views = Array.Empty<OniNativeColony.CellView>();
        private static OniNativeColony.Reading[] readings = Array.Empty<OniNativeColony.Reading>();
        private static OniNativeColony.Constituent[] parts = Array.Empty<OniNativeColony.Constituent>();
        private static OniNativeColony.Receipt[] receipts = Array.Empty<OniNativeColony.Receipt>();

        [DllImport("mixed_atmospheres_oni", EntryPoint = "ma_oni_colony_copy", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int CopyMarshalled(ref OniNativeColony.Status status, [Out] OniNativeColony.CellView[] output, uint capacity);
        [DllImport("mixed_atmospheres_oni", EntryPoint = "ma_oni_colony_readings", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int ReadingsMarshalled(out ulong frame, [Out] OniNativeColony.Reading[]? output, uint capacity, out uint count,
            [Out] OniNativeColony.Constituent[]? constituents, uint partCapacity, out uint partCount);
        [DllImport("mixed_atmospheres_oni", EntryPoint = "ma_oni_colony_copy_receipts", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int ReceiptsMarshalled([Out] OniNativeColony.Receipt[] output, uint capacity, out uint count);

        internal static void Receipts(OniNativeColony.Receipt[] actual, uint capacity, uint count)
        {
            if (count == 0 ? ReceiptEmptyComparisons >= 64 : ReceiptComparisons >= 128) return;
            if (receipts.Length != actual.Length) receipts = new OniNativeColony.Receipt[actual.Length];
            OniNativeColony.Check(ReceiptsMarshalled(receipts, capacity, out var expectedCount));
            // The owner may append committed outcomes between the two copies.
            // Neither copy acknowledges: the already-returned prefix is stable.
            if (expectedCount < count) throw new InvalidOperationException("Colony receipt prefix disappeared before acknowledgement.");
            Same(actual, receipts, Math.Min(count, capacity));
            if (expectedCount > count) ++ReceiptGrowthComparisons;
            if (count == 0) ++ReceiptEmptyComparisons;
            else { ++ReceiptComparisons; ReceiptEntriesCompared += Math.Min(count, capacity); }
        }

        internal static void Views(OniNativeColony.Status status, OniNativeColony.CellView[] actual, uint capacity)
        {
            if (ViewComparisons >= 64 || status.CompletedFrames == viewFrame) return;
            if (views.Length != actual.Length) views = new OniNativeColony.CellView[actual.Length];
            var expected = OniNativeColony.Status.Empty;
            OniNativeColony.Check(CopyMarshalled(ref expected, views, capacity));
            if (expected.Session != status.Session || expected.CompletedFrames != status.CompletedFrames) { ++ChangedFrames; return; }
            Same(actual, views, capacity);
            viewFrame = status.CompletedFrames; ++ViewComparisons;
        }

        internal static void Readings(ulong frame, OniNativeColony.Reading[] actual, uint count,
            OniNativeColony.Constituent[] actualParts, uint partCount)
        {
            if (ReadingComparisons >= 64 || frame == readingFrame) return;
            if (readings.Length != actual.Length) readings = new OniNativeColony.Reading[actual.Length];
            if (parts.Length != actualParts.Length) parts = new OniNativeColony.Constituent[actualParts.Length];
            OniNativeColony.Check(ReadingsMarshalled(out var expectedFrame, readings, (uint)readings.Length, out var expectedCount,
                parts, (uint)parts.Length, out var expectedPartCount));
            if (expectedFrame != frame) { ++ChangedFrames; return; }
            if (expectedCount != count || expectedPartCount != partCount) throw new InvalidOperationException("Pinned colony output counts differ.");
            Same(actual, readings, count); Same(actualParts, parts, partCount);
            readingFrame = frame; ++ReadingComparisons;
        }

        private static void Same<T>(T[] actual, T[] expected, uint count) where T : unmanaged
        {
            if (!MemoryMarshal.AsBytes(actual.AsSpan(0, checked((int)count))).SequenceEqual(
                MemoryMarshal.AsBytes(expected.AsSpan(0, checked((int)count)))))
                throw new InvalidOperationException("Pinned colony output bytes differ: " + typeof(T).Name);
        }
    }
}
