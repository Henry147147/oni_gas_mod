using System;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Frame timing fix (docs/settings.md, performance.frameTimingFix).
    //
    // With V-Sync on, Unity derives each frame's delta time from the display's
    // refresh count. On some displays (very high or variable refresh rates,
    // such as a 500 Hz G-SYNC monitor) every presented frame counts as one
    // refresh, so Unity reports about 2 ms a frame while about 10 ms really
    // pass, and periodically puts the missing time into one frame of several
    // seconds. ONI caps a frame's simulation step at 0.2 s, so that time is
    // lost: the colony runs at a fraction of its speed setting and motion
    // crawls, then jumps. Measured on a player's Windows PC (2026-11-07):
    // 101 fps, game at 0.7-1.4x at speed 3, "slowest frames" of 2-25 s that
    // never happened on screen.
    //
    // Once a frame (from the pacing driver) this compares Unity's unscaled
    // delta time with the real time between frames. When two consecutive
    // windows of 240 frames show most frames reported at under 75% of their
    // real length and the reported total under 80% of the real total, it turns
    // V-Sync off (Unity then times frames from the CPU clock) and caps the
    // frame rate at the display's refresh rate. ONI's graphics options can
    // reset the quality level, so a correction is reapplied while it stands.
    // Turning the setting off restores the previous V-Sync and frame cap.
    internal static class UnityFrameTiming
    {
        private static bool enabled = true, corrected, stillMismatched;
        private static int savedVSync = -1, savedTarget = -1, mismatchedWindows;
        private static long lastTimestamp;
        private static readonly FrameClockWindow window = new FrameClockWindow();
        // Performance-log window (taken and reset by Take).
        private static int logFrames, logShortFrames;
        private static double logReal, logReported, logLongestReported;
        internal static bool Corrected => corrected;

        internal static void SetPreference(bool on)
        {
            enabled = on;
            if (!on && corrected) Restore("the frame timing fix was turned off");
        }

        internal static void Frame()
        {
            try { Observe(Stopwatch.GetTimestamp(), Time.unscaledDeltaTime); }
            catch (Exception error) { UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Frame timing check failed: " + error.Message); }
        }

        private static void Observe(long now, float reported)
        {
            var previous = lastTimestamp; lastTimestamp = now;
            if (previous == 0) return;
            var real = (now - previous) / (double)Stopwatch.Frequency;
            logLongestReported = Math.Max(logLongestReported, reported);
            if (real > 0 && real <= FrameClockWindow.MaximumFrameSeconds)
            { ++logFrames; logReal += real; logReported += reported; if (reported < FrameClockWindow.ShortFrame * real) ++logShortFrames; }
            if (!window.Add(real, reported)) return;
            var mismatched = window.Mismatched;
            var detail = string.Format(CultureInfo.InvariantCulture,
                "over {0} frames the engine reported {1:0.0} ms a frame while {2:0.0} ms passed ({3:0}% of frames under 75% of their real length; V-Sync {4}, frame cap {5}, display {6:0.#} Hz)",
                window.Frames, 1000 * window.ReportedSeconds / window.Frames, 1000 * window.RealSeconds / window.Frames, 100.0 * window.ShortFrames / window.Frames,
                QualitySettings.vSyncCount, Application.targetFrameRate, RefreshRate());
            window.Clear();
            mismatchedWindows = mismatched ? mismatchedWindows + 1 : 0;
            if (corrected)
            {
                // ONI's graphics options may have reset the quality level.
                if (enabled && QualitySettings.vSyncCount != 0) { QualitySettings.vSyncCount = 0; Log("V-Sync came back on; turned it off again: " + detail); }
                if (mismatched && !stillMismatched) { stillMismatched = true; Log("frame timing still disagrees with real time after the fix: " + detail); }
                return;
            }
            if (!enabled || mismatchedWindows < 2) return;
            if (QualitySettings.vSyncCount == 0)
            {
                if (!stillMismatched) { stillMismatched = true; Log("frame timing disagrees with real time with V-Sync already off: " + detail); }
                return;
            }
            savedVSync = QualitySettings.vSyncCount; savedTarget = Application.targetFrameRate;
            QualitySettings.vSyncCount = 0;
            var cap = (int)Math.Round(RefreshRate());
            if (Application.targetFrameRate <= 0) Application.targetFrameRate = cap >= 30 && cap <= 1000 ? cap : 240;
            corrected = true;
            Log("the game clock was losing time: " + detail + ". V-Sync is now off and the frame rate is capped at " +
                Application.targetFrameRate.ToString(CultureInfo.InvariantCulture) + " fps, so the engine times frames from the CPU clock " +
                "(Settings → Performance → Frame timing fix).");
        }

        private static void Restore(string why)
        {
            if (savedVSync >= 0) QualitySettings.vSyncCount = savedVSync;
            if (savedVSync >= 0) Application.targetFrameRate = savedTarget;
            corrected = stillMismatched = false; mismatchedWindows = 0; savedVSync = savedTarget = -1;
            Log(why + "; V-Sync and the frame cap are back to the game's values.");
        }

        private static double RefreshRate()
        {
            try { return Screen.currentResolution.refreshRateRatio.value; } catch (Exception) { return 0; }
        }

        private static void Log(string message) => UnityEngine.Debug.Log("[Mixed Atmospheres] Frame timing: " + message);

        // The performance log's view of the window since the last call.
        internal static void Take(StringBuilder line)
        {
            var c = CultureInfo.InvariantCulture;
            line.Append(" engine_frame_ms ").Append((logFrames == 0 ? 0 : 1000 * logReported / logFrames).ToString("0.00", c))
                .Append(" real_frame_ms ").Append((logFrames == 0 ? 0 : 1000 * logReal / logFrames).ToString("0.00", c))
                .Append(" short_frames ").Append((logFrames == 0 ? 0 : 100.0 * logShortFrames / logFrames).ToString("0", c)).Append('%')
                .Append(" engine_longest_ms ").Append((1000 * logLongestReported).ToString("0", c))
                .Append(" vsync ").Append(QualitySettings.vSyncCount.ToString(c))
                .Append(" timing_fix ").Append(corrected ? 1 : 0);
            logFrames = logShortFrames = 0; logReal = logReported = logLongestReported = 0;
        }

        internal static void Reset() { lastTimestamp = 0; window.Clear(); mismatchedWindows = 0; }
    }
}
