using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Playtest performance summary (docs/colony-owner.md#performance-log): one
    // line in Player.log per 150 owner frames (30 s of game time), plus a
    // header with the machine. Reads only the status the pacing already polls,
    // which reports the newest frame's timing: when several frames finish in
    // one Unity frame the others go unsampled, so windows follow frame numbers.
    internal static class ColonyPerformanceLog
    {
        internal const int WindowFrames = 150;
        internal static bool Enabled = Environment.GetEnvironmentVariable("MA_PERFORMANCE_LOG") != "0";
        private static readonly string[] StageNames = { "edits", "heat", "transfers", "flow", "space", "ledger" };
        private static readonly List<double> owner = new List<double>(WindowFrames), interval = new List<double>(WindowFrames);
        private static readonly double[] stages = new double[6];
        private static readonly Stopwatch clock = new Stopwatch();
        private static ulong session, firstFrame;
        private static long lastCompletion, unityFrames, heldAtStart, lastHeld;
        private static double unityFrameSeconds, slowestUnityFrame, gameSeconds, lastRate;
        private static bool header;

        internal static void Reset()
        {
            owner.Clear(); interval.Clear(); Array.Clear(stages, 0, stages.Length);
            session = firstFrame = 0; lastCompletion = unityFrames = heldAtStart = lastHeld = 0;
            unityFrameSeconds = slowestUnityFrame = gameSeconds = lastRate = 0; header = false;
        }

        // Once per Unity frame while the owner is active.
        internal static void UnityFrame(float unscaledSeconds)
        {
            if (!Enabled) return;
            ++unityFrames; unityFrameSeconds += unscaledSeconds;
            slowestUnityFrame = Math.Max(slowestUnityFrame, unscaledSeconds);
        }

        // Once per Unity frame that sees newly completed owner frames.
        internal static void OwnerFrame(in OniNativeColony.Status status, double ownerRate, long heldTicks)
        {
            if (!Enabled) return;
            try
            {
                if (!clock.IsRunning) clock.Start();
                var now = clock.ElapsedTicks;
                if (status.Session != session) { Reset(); session = status.Session; }
                var frame = status.CompletedFrames;
                if (!header) { WriteHeader(status); header = true; firstFrame = frame; }
                if (frame >= firstFrame + WindowFrames)
                {
                    // The window's last frame went unsampled.
                    if (owner.Count > 0) WriteWindow();
                    firstFrame += (frame - firstFrame) / WindowFrames * WindowFrames;
                }
                if (owner.Count == 0) heldAtStart = heldTicks;
                else { interval.Add((now - lastCompletion) * 1000.0 / Stopwatch.Frequency); gameSeconds += status.LastFrameSeconds; }
                lastCompletion = now; lastRate = ownerRate; lastHeld = heldTicks;
                owner.Add(status.LastAdvanceMilliseconds);
                if (status.StageMilliseconds != null)
                    for (var k = 0; k < stages.Length && k < status.StageMilliseconds.Length; ++k) stages[k] += status.StageMilliseconds[k];
                if (frame == firstFrame + WindowFrames - 1) { WriteWindow(); firstFrame += WindowFrames; }
            }
            catch (Exception error) { Enabled = false; Debug.LogWarning("[Mixed Atmospheres] performance log stopped: " + error.Message); }
        }

        private static void WriteHeader(in OniNativeColony.Status status)
        {
            Debug.Log(FormattableString.Invariant(
                $"[Mixed Atmospheres] perf header: os \"{SystemInfo.operatingSystem}\" cpu \"{SystemInfo.processorType}\" logical {SystemInfo.processorCount} memory_mb {SystemInfo.systemMemorySize} gpu \"{SystemInfo.graphicsDeviceName}\" species {status.Species} frame {status.CompletedFrames}"));
        }

        private static void WriteWindow()
        {
            var line = new StringBuilder(256);
            var c = CultureInfo.InvariantCulture;
            line.Append("[Mixed Atmospheres] perf frames ").Append(firstFrame.ToString(c)).Append('-').Append((firstFrame + WindowFrames - 1).ToString(c));
            Summary(line.Append(": owner_ms "), owner);
            line.Append(" stages_ms");
            for (var k = 0; k < stages.Length; ++k)
                line.Append(' ').Append(StageNames[k]).Append(' ').Append((stages[k] / owner.Count).ToString("0.0", c));
            Summary(line.Append(" interval_ms "), interval);
            var wall = interval.Count == 0 ? 0 : SumOf(interval) / 1000;
            line.Append(" game_per_real ").Append((wall > 0 ? gameSeconds / wall : 0).ToString("0.00", c));
            line.Append(" pace ").Append(lastRate.ToString("0.00", c));
            line.Append(" scale ").Append(Time.timeScale.ToString("0.00", c));
            line.Append(" fps ").Append((unityFrameSeconds > 0 ? unityFrames / unityFrameSeconds : 0).ToString("0", c));
            line.Append(" slowest_frame_ms ").Append((slowestUnityFrame * 1000).ToString("0", c));
            line.Append(" held ").Append((lastHeld - heldAtStart).ToString(c));
            line.Append(" samples ").Append(owner.Count.ToString(c));
            Debug.Log(line.ToString());
            owner.Clear(); interval.Clear(); Array.Clear(stages, 0, stages.Length);
            unityFrames = 0; unityFrameSeconds = slowestUnityFrame = gameSeconds = 0;
        }

        private static double SumOf(List<double> values) { double sum = 0; foreach (var v in values) sum += v; return sum; }

        private static void Summary(StringBuilder line, List<double> values)
        {
            var c = CultureInfo.InvariantCulture;
            if (values.Count == 0) { line.Append("none"); return; }
            var sorted = values.ToArray(); Array.Sort(sorted);
            var p95 = sorted[Math.Min(sorted.Length - 1, (int)Math.Ceiling(.95 * sorted.Length) - 1)];
            line.Append("mean ").Append((SumOf(values) / values.Count).ToString("0.0", c))
                .Append(" p95 ").Append(p95.ToString("0.0", c))
                .Append(" max ").Append(sorted[sorted.Length - 1].ToString("0.0", c));
        }
    }
}
