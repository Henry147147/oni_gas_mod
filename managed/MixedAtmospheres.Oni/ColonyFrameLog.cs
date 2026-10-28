using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Threading;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Development-only frame-time log (MA_FRAME_LOG=1; docs/colony-owner.md#frame-log).
    // Per Unity frame it keeps the frame time and the main-thread time the mod
    // spent in each part, and writes one summary line per window to Player.log:
    // frame-time percentiles, mod time per frame, time blocked inside native
    // calls, hitch counts, garbage collections, and the slowest frame's parts.
    // Off by default: every hook is one static bool test.
    internal static class ColonyFrameLog
    {
        internal enum Part
        {
            // Blocked inside native calls (the game thread waiting).
            NativeSubmit, NativeWait, NativeOther,
            // Part of NativeWait: calls during which a garbage collection finished.
            NativeWaitGc,
            // Mod code on the main thread.
            Publication, Messages, Pacing, Tick,
            // Publication sub-parts (included in Publication).
            Views, Snapshot, Overlay, Receipts, Actors, Presentation,
            Count
        }
        private static readonly string[] Names =
            { "submit", "wait", "native_other", "wait_gc", "publication", "messages", "pacing", "tick", "views", "snapshot", "overlay", "receipts", "actors", "presentation" };
        internal static readonly bool Enabled = Environment.GetEnvironmentVariable("MA_FRAME_LOG") == "1";
        private const double WindowSeconds = 15;
        private static readonly long[] accumulated = new long[(int)Part.Count], worstParts = new long[(int)Part.Count];
        private static readonly double[] sums = new double[(int)Part.Count];
        private static readonly List<double> frames = new List<double>(2048), mod = new List<double>(2048), blocked = new List<double>(2048), other = new List<double>(2048);
        private static readonly Stopwatch clock = Stopwatch.StartNew();
        private static long last, windowStart, hitch33, hitch50, hitch100, publications, worstFrameTicks;
        private static int collections0, collections1, collections2, lastCollections;
        private static long gcFrames, gcFrameTicks, gcWorstTicks;
        private static long lastHeap, allocated, publicationAllocated;
        private static long heapStart;
        private static bool started;

        // Installed from Mod.OnLoad so the log also covers games without an owner.
        internal static void Install()
        {
            if (!Enabled) return;
            try
            {
                UnityEngine.Debug.Log(FormattableString.Invariant($"[Mixed Atmospheres] frame-log environment: gc_mode {UnityEngine.Scripting.GarbageCollector.GCMode} incremental {UnityEngine.Scripting.GarbageCollector.isIncremental} vsync {QualitySettings.vSyncCount} target_fps {Application.targetFrameRate} max_gen {GC.MaxGeneration} cpu \"{SystemInfo.processorType}\" logical {SystemInfo.processorCount} gpu \"{SystemInfo.graphicsDeviceName}\" unity {Application.unityVersion}"));
            }
            catch (Exception error) { UnityEngine.Debug.LogWarning("[Mixed Atmospheres] frame-log environment unavailable: " + error.Message); }
            var holder = new GameObject("Mixed Atmospheres frame log");
            UnityEngine.Object.DontDestroyOnLoad(holder); holder.AddComponent<ColonyFrameLogDriver>();
        }
        // Named scopes on any thread: calls, wall milliseconds and managed heap
        // growth (kept only when no collection ran inside the scope, so it is
        // approximate while other threads allocate).
        private static readonly Dictionary<string, double[]> tracks = new Dictionary<string, double[]>();
        internal readonly struct TrackScope : IDisposable
        {
            private readonly string name; private readonly long begun, heap; private readonly int collections;
            internal TrackScope(string name)
            { this.name = name; begun = Stopwatch.GetTimestamp(); heap = GC.GetTotalMemory(false); collections = GC.CollectionCount(0); }
            public void Dispose()
            {
                var ms = (Stopwatch.GetTimestamp() - begun) * 1000.0 / Stopwatch.Frequency;
                var grown = GC.CollectionCount(0) == collections ? Math.Max(0, GC.GetTotalMemory(false) - heap) / 1024.0 : 0;
                lock (tracks)
                {
                    if (!tracks.TryGetValue(name, out var t)) tracks[name] = t = new double[4];
                    ++t[0]; t[1] += ms; t[2] += grown; t[3] = Math.Max(t[3], ms);
                }
            }
        }
        // Heap growth attributed to a named step by the caller.
        internal static void AddHeap(string name, long bytes)
        {
            lock (tracks) { if (!tracks.TryGetValue(name, out var t)) tracks[name] = t = new double[4]; ++t[0]; t[2] += bytes / 1024.0; }
        }
        internal static TrackScope? Track(string name) => Enabled ? new TrackScope(name) : (TrackScope?)null;
        private static long counted;
        // Cells whose reading and presentation objects were built on demand.
        private static long countedBytes;
        private static readonly Dictionary<string, double[]> byproducts = new Dictionary<string, double[]>();
        // Solid byproducts spawned as debris, by element: count and kilograms.
        internal static void CountByproduct(string element, float kilograms)
        {
            if (!Enabled) return;
            lock (byproducts) { if (!byproducts.TryGetValue(element, out var t)) byproducts[element] = t = new double[2]; ++t[0]; t[1] += kilograms; }
        }
        internal static void CountCell() { if (Enabled) Interlocked.Increment(ref counted); }
        // Heap growth around building one lazy cell (approximate while other threads allocate).
        internal static long HeapNow => Enabled ? GC.GetTotalMemory(false) : 0;
        internal static void CountCellBytes(long before, int collections)
        { if (Enabled && GC.CollectionCount(0) == collections) { var grown = GC.GetTotalMemory(false) - before; if (grown > 0) Interlocked.Add(ref countedBytes, grown); } }
        internal static long Now => Enabled ? Stopwatch.GetTimestamp() : 0;
        internal static void Add(Part part, long begun) { if (Enabled) accumulated[(int)part] += Stopwatch.GetTimestamp() - begun; }
        internal static void AddTicks(Part part, long ticks) { if (Enabled) accumulated[(int)part] += ticks; }
        internal static void Published() { if (Enabled) ++publications; }
        // Managed heap growth across one publication (valid only when no
        // collection ran inside it).
        internal static void HeapDone(long before, int collections) { if (Enabled && GC.CollectionCount(0) == collections) publicationAllocated += Math.Max(0, GC.GetTotalMemory(false) - before); }

        // Once per Unity frame, from the pacing driver's Update.
        internal static void EndFrame()
        {
            if (!Enabled) return;
            var now = Stopwatch.GetTimestamp();
            if (!started)
            {
                started = true; last = windowStart = now; Array.Clear(accumulated, 0, accumulated.Length); Restart();
                return;
            }
            var frame = now - last; last = now;
            var collected = GC.CollectionCount(0);
            if (collected != lastCollections) { lastCollections = collected; ++gcFrames; gcFrameTicks += frame; gcWorstTicks = Math.Max(gcWorstTicks, frame); lastHeap = GC.GetTotalMemory(false); }
            else { var heap = GC.GetTotalMemory(false); if (heap > lastHeap) allocated += heap - lastHeap; lastHeap = heap; }
            var ms = 1000.0 / Stopwatch.Frequency;
            double modTicks = accumulated[(int)Part.Publication] + accumulated[(int)Part.Messages] + accumulated[(int)Part.Pacing] + accumulated[(int)Part.Tick];
            double blockedTicks = accumulated[(int)Part.NativeSubmit] + accumulated[(int)Part.NativeWait] + accumulated[(int)Part.NativeOther];
            frames.Add(frame * ms); mod.Add(modTicks * ms); blocked.Add(blockedTicks * ms); other.Add(Math.Max(0, frame - modTicks - blockedTicks) * ms);
            for (var k = 0; k < accumulated.Length; ++k) sums[k] += accumulated[k] * ms;
            if (frame > worstFrameTicks) { worstFrameTicks = frame; Array.Copy(accumulated, worstParts, accumulated.Length); }
            var frameMs = frame * ms;
            if (frameMs > 33.4) ++hitch33; if (frameMs > 50) ++hitch50; if (frameMs > 100) ++hitch100;
            Array.Clear(accumulated, 0, accumulated.Length);
            if ((now - windowStart) * 1.0 / Stopwatch.Frequency >= WindowSeconds) { Write(); Restart(); windowStart = now; }
        }

        private static void Restart()
        {
            frames.Clear(); mod.Clear(); blocked.Clear(); other.Clear(); Array.Clear(sums, 0, sums.Length);
            hitch33 = hitch50 = hitch100 = publications = worstFrameTicks = 0; gcFrames = gcFrameTicks = gcWorstTicks = allocated = publicationAllocated = 0; Array.Clear(worstParts, 0, worstParts.Length);
            collections0 = GC.CollectionCount(0); collections1 = GC.CollectionCount(1); collections2 = GC.CollectionCount(2);
            heapStart = GC.GetTotalMemory(false);
        }

        private static double Rank(double[] sorted, double fraction) =>
            sorted[Math.Max(0, Math.Min(sorted.Length - 1, (int)Math.Ceiling(fraction * sorted.Length) - 1))];

        private static void Stats(StringBuilder line, string name, List<double> values)
        {
            var c = CultureInfo.InvariantCulture;
            var sorted = values.ToArray(); Array.Sort(sorted);
            double sum = 0; foreach (var v in sorted) sum += v;
            line.Append(' ').Append(name).Append(" mean ").Append((sum / sorted.Length).ToString("0.00", c))
                .Append(" p50 ").Append(Rank(sorted, .5).ToString("0.00", c)).Append(" p95 ").Append(Rank(sorted, .95).ToString("0.00", c))
                .Append(" p99 ").Append(Rank(sorted, .99).ToString("0.00", c)).Append(" max ").Append(sorted[sorted.Length - 1].ToString("0.00", c));
        }

        private static void Write()
        {
            if (frames.Count == 0) return;
            try
            {
                var c = CultureInfo.InvariantCulture;
                var line = new StringBuilder(512);
                line.Append("[Mixed Atmospheres] frame-log frames ").Append(frames.Count.ToString(c)).Append(" owner ").Append(ColonyOwner.Active ? 1 : 0);
                line.Append(" scale ").Append(Time.timeScale.ToString("0.00", c)).Append(" pace ").Append(ColonyPacing.OwnerRate.ToString("0.00", c));
                Stats(line, "frame_ms", frames); Stats(line, "mod_ms", mod); Stats(line, "blocked_ms", blocked); Stats(line, "other_ms", other);
                line.Append(" hitch33 ").Append(hitch33.ToString(c)).Append(" hitch50 ").Append(hitch50.ToString(c)).Append(" hitch100 ").Append(hitch100.ToString(c));
                line.Append(" gc0 ").Append((GC.CollectionCount(0) - collections0).ToString(c)).Append(" gc1 ").Append((GC.CollectionCount(1) - collections1).ToString(c))
                    .Append(" gc2 ").Append((GC.CollectionCount(2) - collections2).ToString(c)).Append(" heap_mb ").Append((GC.GetTotalMemory(false) / 1048576.0).ToString("0.0", c))
                    .Append(" gc_frames ").Append(gcFrames.ToString(c)).Append(" gc_frame_mean_ms ").Append((gcFrames == 0 ? 0 : gcFrameTicks * 1000.0 / Stopwatch.Frequency / gcFrames).ToString("0.0", c))
                    .Append(" gc_frame_max_ms ").Append((gcWorstTicks * 1000.0 / Stopwatch.Frequency).ToString("0.0", c))
                    .Append(" alloc_kb_per_frame ").Append((allocated / 1024.0 / frames.Count).ToString("0.0", c)).Append(" publication_alloc_kb ").Append((publicationAllocated / 1024.0 / Math.Max(1, publications)).ToString("0.0", c))
                    .Append(" heap_delta_mb ").Append(((GC.GetTotalMemory(false) - heapStart) / 1048576.0).ToString("0.0", c));
                line.Append(" per_frame_ms");
                for (var k = 0; k < sums.Length; ++k) line.Append(' ').Append(Names[k]).Append(' ').Append((sums[k] / frames.Count).ToString("0.000", c));
                line.Append(" worst_frame_ms ").Append((worstFrameTicks * 1000.0 / Stopwatch.Frequency).ToString("0.0", c)).Append(" worst_parts_ms");
                for (var k = 0; k < worstParts.Length; ++k) line.Append(' ').Append(Names[k]).Append(' ').Append((worstParts[k] * 1000.0 / Stopwatch.Frequency).ToString("0.0", c));
                lock (tracks)
                {
                    foreach (var pair in tracks)
                        line.Append(" track_").Append(pair.Key).Append(" calls ").Append(pair.Value[0].ToString("0", c)).Append(" ms ").Append(pair.Value[1].ToString("0.0", c))
                            .Append(" max_ms ").Append(pair.Value[3].ToString("0.0", c)).Append(" kb ").Append(pair.Value[2].ToString("0", c));
                    tracks.Clear();
                }
                lock (byproducts)
                {
                    foreach (var pair in byproducts) line.Append(" byproduct_").Append(pair.Key).Append(' ').Append(pair.Value[0].ToString("0", c)).Append(" kg ").Append(pair.Value[1].ToString("0.###", c));
                    byproducts.Clear();
                }
                line.Append(" lazy_cells ").Append(Interlocked.Exchange(ref counted, 0).ToString(c)).Append(" lazy_cell_kb ").Append((Interlocked.Exchange(ref countedBytes, 0) / 1024).ToString(c));
                line.Append(" publications ").Append(publications.ToString(c));
                UnityEngine.Debug.Log(line.ToString());
            }
            catch (Exception error) { UnityEngine.Debug.LogWarning("[Mixed Atmospheres] frame log failed: " + error.Message); }
        }
    }

    public sealed class ColonyFrameLogDriver : MonoBehaviour
    {
        private void Update() => ColonyFrameLog.EndFrame();
    }
}
