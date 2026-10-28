using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Diagnostics, not a pacing input. Clock deltas retain physical progress
    // when Unity misses completions. The native owner timer excludes projection;
    // submit-to-publication measures the asynchronous handoff through its final
    // managed publication, including queueing and observation latency.
    internal static class ColonyPerformanceLog
    {
        internal const int WindowFrames = 150;
        private const int MaximumPublicationSamples = 2 * WindowFrames;
        internal static bool Enabled = Environment.GetEnvironmentVariable("MA_PERFORMANCE_LOG") != "0";
        private static readonly string[] StageNames = { "edits", "heat", "transfers", "flow", "space", "ledger_projection" };
        private static readonly List<double> owner = new List<double>(WindowFrames), interval = new List<double>(WindowFrames),
            publication = new List<double>(WindowFrames);
        private static readonly double[] stages = new double[6];
        private static readonly ColonyPerformanceClock clock = new ColonyPerformanceClock(Stopwatch.Frequency);
        private static ColonyPerformanceClock.Mark window;
        private static ulong lastSampled, latencySession;
        private static long lastCompletion, unityFrames, runUnityFrames, heldAtStart, lastHeld, unmatchedPublications;
        private static long droppedPublicationSamples, runDroppedPublicationSamples;
        private static double slowestUnityFrame, runSlowestUnityFrame, lastCapacityEstimate;
        private static bool started;

        internal static void Reset()
        {
            owner.Clear(); interval.Clear(); publication.Clear(); Array.Clear(stages, 0, stages.Length);
            clock.Reset(); started = false; window = default;
            lastSampled = latencySession = 0;
            lastCompletion = unityFrames = runUnityFrames = heldAtStart = lastHeld = unmatchedPublications = 0;
            droppedPublicationSamples = runDroppedPublicationSamples = 0;
            slowestUnityFrame = runSlowestUnityFrame = lastCapacityEstimate = 0;
        }

        // Once per Unity frame, including duplicates and paused observations.
        internal static void UnityFrame(float unscaledSeconds)
        {
            if (!Enabled) return;
            try
            {
                if (!ObserveClock(ColonyOwner.Status)) return;
                ++unityFrames; ++runUnityFrames;
                slowestUnityFrame = Math.Max(slowestUnityFrame, unscaledSeconds);
                runSlowestUnityFrame = Math.Max(runSlowestUnityFrame, unscaledSeconds);
            }
            catch (Exception error) { Failed(error); }
        }

        private static bool ObserveClock(in OniNativeColony.Status status)
        {
            // A loaded world's GameClock is restored during startup. Its old
            // absolute time must not appear as newly simulated game progress.
            if (Game.Instance == null || !Game.Instance.GameStarted()) return false;
            var gameClock = GameClock.Instance;
            if (gameClock == null) return false;
            var speed = SpeedControlScreen.Instance;
            var reason = clock.Observe(status.Session, status.CompletedFrames, status.TimeSeconds,
                gameClock.GetTime(), Stopwatch.GetTimestamp(), speed == null || speed.IsPaused);
            if (reason != ColonyPerformanceClock.ResetReason.None)
            {
                if (started && clock.PreviousRun.Valid) WriteRun(clock.PreviousRun, "clock_reset_" + reason);
                owner.Clear(); interval.Clear(); Array.Clear(stages, 0, stages.Length);
                if (reason != ColonyPerformanceClock.ResetReason.Initial || latencySession != status.Session)
                { publication.Clear(); unmatchedPublications = droppedPublicationSamples = runDroppedPublicationSamples = 0; latencySession = status.Session; }
                lastCompletion = unityFrames = runUnityFrames = 0;
                slowestUnityFrame = runSlowestUnityFrame = lastCapacityEstimate = 0;
                heldAtStart = lastHeld = ColonyPacing.HeldTicks;
                started = clock.Ready;
                if (!started) return false;
                window = clock.Current; lastSampled = status.CompletedFrames;
                UnityEngine.Debug.Log(FormattableString.Invariant($"[Mixed Atmospheres] perf header: os \"{SystemInfo.operatingSystem}\" cpu \"{SystemInfo.processorType}\" logical {SystemInfo.processorCount} memory_mb {SystemInfo.systemMemorySize} gpu \"{SystemInfo.graphicsDeviceName}\" species {status.Species} session {status.Session} frame {status.CompletedFrames} clock_start {reason}; owner_ms excludes projection/publication; capacity_estimate is not measured game speed; rates include paused wall time"));
            }
            return started;
        }

        // Called only when pacing sees a completion. Unsampled completions affect
        // clock deltas, not the distribution of the latest per-frame work timer.
        internal static void OwnerFrame(in OniNativeColony.Status status, double capacityEstimate, long heldTicks)
        {
            if (!Enabled || !started || status.Session != clock.Current.Session || status.CompletedFrames <= lastSampled) return;
            try
            {
                var now = clock.Current.WallTicks;
                if (lastCompletion != 0) interval.Add((now - lastCompletion) * 1000.0 / Stopwatch.Frequency);
                lastCompletion = now; lastSampled = status.CompletedFrames;
                lastCapacityEstimate = capacityEstimate; lastHeld = heldTicks;
                owner.Add(status.LastAdvanceMilliseconds);
                if (status.StageMilliseconds != null)
                    for (var k = 0; k < stages.Length && k < status.StageMilliseconds.Length; ++k) stages[k] += status.StageMilliseconds[k];
                if (status.CompletedFrames - window.Frame >= WindowFrames) WriteWindow(false);
            }
            catch (Exception error) { Failed(error); }
        }

        internal static void Published(ulong session, long submittedAt, long completedAt, bool matched)
        {
            if (!Enabled || !started || session != clock.Current.Session) return;
            try
            {
                if (latencySession != session) { publication.Clear(); unmatchedPublications = 0; latencySession = session; }
                if (matched && completedAt >= submittedAt)
                {
                    if (publication.Count == MaximumPublicationSamples)
                    { publication.RemoveAt(0); ++droppedPublicationSamples; ++runDroppedPublicationSamples; }
                    publication.Add((completedAt - submittedAt) * 1000.0 / Stopwatch.Frequency);
                }
                else ++unmatchedPublications;
            }
            catch (Exception error) { Failed(error); }
        }

        // Harness reports take their own final observation instead of relying on
        // a following Unity frame or on process teardown to flush a log window.
        internal static object Timerun()
        {
            if (Enabled)
                try { _ = ObserveClock(ColonyOwner.Status); }
                catch (Exception error) { Failed(error); }
            var measured = clock.Total;
            return new {
                Enabled, Valid = Enabled && measured.Valid, Scope = "monotonic wall time including pauses",
                measured.Session, FirstObservedFrame = measured.FirstFrame, LastObservedFrame = measured.LastFrame,
                measured.OwnerSeconds, measured.GameSeconds, measured.WallSeconds, measured.OwnerPerReal, measured.GamePerReal,
                measured.PauseObserved, CapacityEstimate = lastCapacityEstimate,
                DroppedSubmissionSamples = ColonyOwner.PerformanceDroppedSubmissions,
                DroppedPublicationSamples = runDroppedPublicationSamples,
                OwnerTimerScope = "native owner_ms excludes projection/publication",
                PublicationTimerScope = "matched submit through completed managed publication, including queueing and observation"
            };
        }

        // Reset runs at the actual owner lifecycle boundary. A short timerun gets
        // its partial final window as well as a total independent of speed keys.
        internal static void FinishRun()
        {
            if (!Enabled) { Reset(); return; }
            try
            {
                if (started)
                {
                    _ = ObserveClock(ColonyOwner.Status);
                    if (owner.Count != 0) WriteWindow(true);
                    if (clock.Total.Valid) WriteRun(clock.Total, "session_end");
                }
            }
            catch (Exception error) { Failed(error); }
            finally { Reset(); }
        }

        private static void WriteWindow(bool partial)
        {
            var measured = clock.Since(window);
            if (!measured.Valid) return;
            var c = CultureInfo.InvariantCulture;
            var line = new StringBuilder(512);
            line.Append("[Mixed Atmospheres] perf frames ").Append((window.Frame + 1).ToString(c)).Append('-').Append(measured.LastFrame.ToString(c));
            line.Append(" session ").Append(measured.Session.ToString(c)).Append(" partial ").Append(partial ? 1 : 0);
            Summary(line.Append(": owner_ms "), owner);
            line.Append(" stages_ms");
            for (var k = 0; k < stages.Length; ++k) line.Append(' ').Append(StageNames[k]).Append(' ').Append((owner.Count == 0 ? 0 : stages[k] / owner.Count).ToString("0.0", c));
            Summary(line.Append(" interval_ms "), interval);
            Summary(line.Append(" submit_to_publication_ms "), publication);
            line.Append(" unmatched_publications ").Append(unmatchedPublications.ToString(c));
            line.Append(" publication_samples ").Append(publication.Count.ToString(c));
            line.Append(" dropped_publication_samples ").Append(droppedPublicationSamples.ToString(c));
            line.Append(" dropped_submission_samples ").Append(ColonyOwner.PerformanceDroppedSubmissions.ToString(c));
            Rates(line, measured);
            line.Append(" capacity_estimate ").Append(lastCapacityEstimate.ToString("0.00", c));
            line.Append(" scale ").Append(Time.timeScale.ToString("0.00", c));
            line.Append(" speed_button ").Append(SpeedControlScreen.Instance == null ? 0 : SpeedControlScreen.Instance.GetSpeed() + 1);
            line.Append(" fps ").Append((unityFrames / measured.WallSeconds).ToString("0", c));
            line.Append(" slowest_frame_ms ").Append((slowestUnityFrame * 1000).ToString("0", c));
            line.Append(" held ").Append((lastHeld - heldAtStart).ToString(c));
            line.Append(" samples ").Append(owner.Count.ToString(c));
            line.Append(" owner_frames ").Append(measured.Frames.ToString(c));
            UnityEngine.Debug.Log(line.ToString());
            owner.Clear(); interval.Clear(); publication.Clear(); Array.Clear(stages, 0, stages.Length);
            unityFrames = unmatchedPublications = droppedPublicationSamples = 0; slowestUnityFrame = 0;
            heldAtStart = lastHeld; window = clock.Current;
        }

        private static void WriteRun(ColonyPerformanceClock.Measurement measured, string reason)
        {
            var c = CultureInfo.InvariantCulture;
            var line = new StringBuilder(256);
            line.Append("[Mixed Atmospheres] perf run session ").Append(measured.Session.ToString(c));
            line.Append(" frames ").Append(measured.FirstFrame.ToString(c)).Append('-').Append(measured.LastFrame.ToString(c));
            line.Append(" reason ").Append(reason); Rates(line, measured);
            line.Append(" unity_frames ").Append(runUnityFrames.ToString(c));
            line.Append(" slowest_frame_ms ").Append((runSlowestUnityFrame * 1000).ToString("0", c));
            UnityEngine.Debug.Log(line.ToString());
        }

        private static void Rates(StringBuilder line, ColonyPerformanceClock.Measurement measured)
        {
            var c = CultureInfo.InvariantCulture;
            line.Append(" owner_seconds ").Append(measured.OwnerSeconds.ToString("0.000000", c));
            line.Append(" game_seconds ").Append(measured.GameSeconds.ToString("0.000000", c));
            line.Append(" wall_seconds ").Append(measured.WallSeconds.ToString("0.000000", c));
            line.Append(" owner_per_real ").Append(measured.OwnerPerReal.ToString("0.000", c));
            line.Append(" game_per_real ").Append(measured.GamePerReal.ToString("0.000", c));
            line.Append(" pause_observed ").Append(measured.PauseObserved ? 1 : 0);
        }

        private static void Summary(StringBuilder line, List<double> values)
        {
            var c = CultureInfo.InvariantCulture;
            if (values.Count == 0) { line.Append("none"); return; }
            var sorted = values.ToArray(); Array.Sort(sorted); double sum = 0;
            foreach (var value in sorted) sum += value;
            line.Append("mean ").Append((sum / sorted.Length).ToString("0.0", c));
            line.Append(" p95 ").Append(sorted[(int)Math.Ceiling(.95 * sorted.Length) - 1].ToString("0.0", c));
            line.Append(" max ").Append(sorted[sorted.Length - 1].ToString("0.0", c));
        }

        private static void Failed(Exception error)
        { Enabled = false; UnityEngine.Debug.LogWarning("[Mixed Atmospheres] performance log stopped: " + error.Message); }
    }
}
