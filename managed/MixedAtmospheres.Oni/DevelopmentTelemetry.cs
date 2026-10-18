using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;

namespace MixedAtmospheres.Oni
{
    // DEVELOPMENT TELEMETRY ONLY. Long validation runs append one compact line
    // per interval so a crash or hang still leaves evidence. Every latency is
    // a wall-clock interval measured around an actual game/native boundary;
    // none of it is a release performance claim or an acceptance threshold.
    internal sealed class LatencySummary
    {
        public long Samples;
        public int Retained;
        public double? P50, P95, Max, Mean;
    }

    // Fixed memory per metric. Count, mean and maximum cover every sample in
    // the interval; percentiles use the most recent Capacity samples and say
    // how many they retained.
    internal sealed class LatencyWindow
    {
        internal const int DefaultCapacity = 4096;
        private readonly double[] samples;
        private long count;
        private int next;
        private double sum, maximum;
        internal LatencyWindow(int capacity = DefaultCapacity)
        {
            if (capacity < 1 || capacity > 1 << 20) throw new ArgumentOutOfRangeException(nameof(capacity));
            samples = new double[capacity];
        }
        internal int Capacity => samples.Length;
        internal long Count => count;
        internal void Add(double milliseconds)
        {
            if (double.IsNaN(milliseconds) || double.IsInfinity(milliseconds) || milliseconds < 0)
                throw new ArgumentOutOfRangeException(nameof(milliseconds), "Latency samples are finite nonnegative milliseconds.");
            samples[next] = milliseconds; next = next + 1 == samples.Length ? 0 : next + 1;
            ++count; sum += milliseconds; if (count == 1 || milliseconds > maximum) maximum = milliseconds;
        }
        internal LatencySummary TakeSummary()
        {
            var retained = (int)Math.Min(count, samples.Length);
            var result = new LatencySummary { Samples = count, Retained = retained };
            if (retained > 0)
            {
                var sorted = new double[retained];
                Array.Copy(samples, sorted, retained);
                Array.Sort(sorted);
                result.P50 = NearestRank(sorted, .50); result.P95 = NearestRank(sorted, .95);
                result.Max = maximum; result.Mean = sum / count;
            }
            count = 0; next = 0; sum = 0; maximum = 0;
            return result;
        }
        // Nearest-rank percentile: the smallest retained sample with at least
        // the requested fraction of retained samples at or below it.
        private static double NearestRank(double[] sorted, double fraction) =>
            sorted[Math.Max(0, Math.Min(sorted.Length - 1, (int)Math.Ceiling(fraction * sorted.Length) - 1))];
    }

    internal sealed class DevelopmentTelemetryInterval
    {
        public long NativeFramesProcessed, IntervalNativeFrames;
        // Samples dropped because a clock moved backwards or was not finite.
        public long InvalidSamples;
        // Game-thread wall time inside SIM_HandleMessage for NewGameFrame and
        // PrepareGameData. The custom solver advances on the native worker
        // inside that frame; this is the wait the game saw, not solver time.
        public LatencySummary NativeFrameSubmitMs = new LatencySummary(), NativeFrameWaitMs = new LatencySummary();
        // Managed work after PrepareGameData returned: owned snapshot copy,
        // domain readings, presentation publication and development checks.
        public LatencySummary ManagedPublicationMs = new LatencySummary();
        // Native-domain presentation publication to the first stock property
        // texture batch that rendered that generation (or a newer one).
        public LatencySummary PresentationLagMs = new LatencySummary(), TextureBatchMs = new LatencySummary();
        public LatencySummary UnityFrameMs = new LatencySummary();
    }

    internal sealed class DevelopmentTelemetrySampler
    {
        internal const int NewGameFrameMessage = -775326397, PrepareGameDataMessage = 1078620451;
        private readonly object sync = new object();
        private readonly LatencyWindow submit, wait, publication, lag, texture, unity;
        private long framesProcessed, lineFrames, pendingGeneration = -1, pendingTimestamp, invalidSamples;
        private volatile bool active;
        internal DevelopmentTelemetrySampler(int capacity = LatencyWindow.DefaultCapacity)
        {
            submit = new LatencyWindow(capacity); wait = new LatencyWindow(capacity); publication = new LatencyWindow(capacity);
            lag = new LatencyWindow(capacity); texture = new LatencyWindow(capacity); unity = new LatencyWindow(capacity);
        }
        internal bool Active => active;
        internal long NativeFramesProcessed { get { lock (sync) return framesProcessed; } }
        internal static double Milliseconds(long start, long end) => (end - start) * 1000.0 / Stopwatch.Frequency;
        internal void Start()
        {
            lock (sync)
            {
                foreach (var window in new[] { submit, wait, publication, lag, texture, unity }) window.TakeSummary();
                framesProcessed = lineFrames = invalidSamples = 0; pendingGeneration = -1; pendingTimestamp = 0; active = true;
            }
        }
        // Recording runs inside game and native message paths. It never
        // throws there: an impossible interval is counted, not invented.
        private void Add(LatencyWindow window, long start, long end)
        {
            if (end < start) ++invalidSamples; else window.Add(Milliseconds(start, end));
        }
        internal void Stop() => active = false;
        internal void RecordNativeMessage(int message, long started, long returned, long published, int processedFrames)
        {
            if (!active || (message != NewGameFrameMessage && message != PrepareGameDataMessage)) return;
            lock (sync)
            {
                if (message == NewGameFrameMessage) { Add(submit, started, returned); return; }
                Add(wait, started, returned); Add(publication, returned, published);
                if (processedFrames > 0) framesProcessed += processedFrames;
            }
        }
        internal void ObservePublication(long generation, long timestamp)
        {
            if (!active) return;
            // Keep the oldest unrendered publication: lag is measured from it.
            lock (sync) if (pendingGeneration < 0) { pendingGeneration = generation; pendingTimestamp = timestamp; }
        }
        internal void ObserveTextureBatch(long generation, long started, long finished)
        {
            if (!active) return;
            lock (sync)
            {
                Add(texture, started, finished);
                if (pendingGeneration >= 0 && generation >= pendingGeneration)
                { Add(lag, pendingTimestamp, started); pendingGeneration = -1; pendingTimestamp = 0; }
            }
        }
        internal void RecordUnityFrame(double milliseconds)
        {
            if (!active) return;
            lock (sync)
                if (double.IsNaN(milliseconds) || double.IsInfinity(milliseconds) || milliseconds < 0) ++invalidSamples;
                else unity.Add(milliseconds);
        }
        internal DevelopmentTelemetryInterval TakeInterval()
        {
            lock (sync)
            {
                var result = new DevelopmentTelemetryInterval
                {
                    NativeFramesProcessed = framesProcessed, IntervalNativeFrames = framesProcessed - lineFrames, InvalidSamples = invalidSamples,
                    NativeFrameSubmitMs = submit.TakeSummary(), NativeFrameWaitMs = wait.TakeSummary(),
                    ManagedPublicationMs = publication.TakeSummary(), PresentationLagMs = lag.TakeSummary(),
                    TextureBatchMs = texture.TakeSummary(), UnityFrameMs = unity.TakeSummary()
                };
                lineFrames = framesProcessed; invalidSamples = 0;
                return result;
            }
        }
        // A line every interval of native frames, plus a wall-clock heartbeat
        // so loading, pauses or a stalled simulator still leave fresh evidence.
        internal static string? Due(long frames, long lastLineFrames, int intervalFrames, double seconds, double lastLineSeconds, double heartbeatSeconds) =>
            frames - lastLineFrames >= intervalFrames ? "interval" : seconds - lastLineSeconds >= heartbeatSeconds ? "heartbeat" : null;
    }

    internal static class DevelopmentTelemetry
    {
        internal const string FileName = "development-telemetry.jsonl";
        internal const string Kind = "mixed-atmospheres-development-telemetry";
        internal const double HeartbeatSeconds = 60;
        internal static readonly DevelopmentTelemetrySampler Sampler = new DevelopmentTelemetrySampler();
        internal static bool Active => Sampler.Active;

        // Linux reports resident memory in /proc/self/status; other hosts use
        // the process API. Values are bytes; a missing field stays null.
        internal static void ParseProcessStatus(string[] lines, out long? resident, out long? peak)
        {
            resident = null; peak = null;
            foreach (var line in lines)
            {
                if (line.StartsWith("VmRSS:", StringComparison.Ordinal)) resident = Kilobytes(line);
                else if (line.StartsWith("VmHWM:", StringComparison.Ordinal)) peak = Kilobytes(line);
            }
        }
        private static long Kilobytes(string line)
        {
            var parts = line.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length != 3 || parts[2] != "kB" || !long.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out var value))
                throw new InvalidDataException("Unexpected process memory field: " + line);
            return checked(value * 1024);
        }
    }
}
