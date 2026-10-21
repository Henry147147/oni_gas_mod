using System;
using System.Diagnostics;
using System.IO;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // DEVELOPMENT TELEMETRY for explicit long validation runs. Lines describe
    // the harness and the bounded scene it drives; they never certify a
    // playable colony, production timestep or release performance.
    internal static unsafe partial class ColonyRuntimeValidation
    {
        private static DevelopmentTelemetryJournal? telemetry;
        private static readonly Stopwatch TelemetryClock = new Stopwatch();
        private static readonly Stopwatch ProgressReportClock = new Stopwatch();
        private static string telemetrySession = "";
        private static long telemetryLineFrames;
        private static double telemetryLineSeconds;
        private static ulong telemetrySubsteps, telemetryRejectedAttempts;
        private static bool telemetryFinished;

        private static void StartTelemetry()
        {
            var path = Path.Combine(OutputDirectory, DevelopmentTelemetry.FileName);
            telemetry = new DevelopmentTelemetryJournal(path);
            telemetrySession = Guid.NewGuid().ToString("N");
            telemetryFinished = false; telemetryLineFrames = 0; telemetryLineSeconds = 0;
            telemetrySubsteps = telemetryRejectedAttempts = 0;
            Report.TelemetryIntervalFrames = configuration!.TelemetryIntervalFrames;
            Report.DevelopmentTelemetryFile = DevelopmentTelemetry.FileName;
            TelemetryClock.Restart();
            DevelopmentTelemetry.Sampler.Start();
        }

        // Called once per Unity frame by the validation driver.
        internal static void TickTelemetry()
        {
            if (telemetry == null || telemetryFinished || configuration == null) return;
            try
            {
                var seconds = TelemetryClock.Elapsed.TotalSeconds;
                var frames = DevelopmentTelemetry.Sampler.NativeFramesProcessed;
                var reason = telemetry.Lines == 0 ? "start" : done ? "final" :
                    DevelopmentTelemetrySampler.Due(frames, telemetryLineFrames, configuration.TelemetryIntervalFrames,
                        seconds, telemetryLineSeconds, DevelopmentTelemetry.HeartbeatSeconds);
                if (reason == null) return;
                AppendTelemetry(reason, seconds);
                if (reason == "final") FinishTelemetry();
            }
            catch (Exception error) { StopTelemetry(error); }
        }

        private static void ShutdownTelemetry()
        {
            if (telemetry == null || telemetryFinished) return;
            try { AppendTelemetry(done ? "final" : "sim-shutdown", TelemetryClock.Elapsed.TotalSeconds); if (done) FinishTelemetry(); }
            catch (Exception error) { StopTelemetry(error); }
        }

        private static void AppendTelemetry(string reason, double seconds)
        {
            var interval = DevelopmentTelemetry.Sampler.TakeInterval();
            var domain = NativeSealedDomainValidation.CaptureTelemetry();
            var game = Game.Instance;
            var clock = GameClock.Instance;
            var speed = SpeedControlScreen.Instance;
            var line = new
            {
                Kind = DevelopmentTelemetry.Kind, SchemaVersion = 1, DevelopmentOnly = true,
                Session = telemetrySession, Sequence = telemetry!.Lines + 1, Reason = reason,
                configuration!.Mode, RequestedFrames = configuration.CompletedFrames, configuration.TelemetryIntervalFrames,
                ValidationStatus = Report.Status, ValidationStage = Report.Stage,
                UtcTime = System.DateTime.UtcNow.ToString("O"), WallSeconds = seconds, IntervalWallSeconds = seconds - telemetryLineSeconds,
                UnityFrame = Time.frameCount, interval.NativeFramesProcessed, interval.IntervalNativeFrames,
                Game = new
                {
                    Started = game != null && game.GameStarted(),
                    TimeSeconds = clock == null ? (double?)null : clock.GetTime(),
                    Cycle = clock == null ? (int?)null : clock.GetCycle(),
                    TimeInCycles = clock == null ? (double?)null : clock.GetTimeInCycles(),
                    Paused = speed == null ? (bool?)null : speed.IsPaused,
                    Speed = speed == null ? (int?)null : speed.GetSpeed(),
                    DebugEnabled = DebugHandler.enabled,
                    DebugInvincible = DebugHandler.InvincibleMode,
                    Duplicants = game == null ? (int?)null : Components.LiveMinionIdentities.Count
                },
                Colony = new { Report.SnapshotsPublished, Report.NativeFramesAcknowledged, Report.ObserverTimestepReturns, Report.ObserverPhysicalUpdates },
                NativeDomain = domain == null ? null : new
                {
                    domain.Active, domain.Completed, domain.Failed, domain.State, domain.Session, domain.CompletedFrames,
                    domain.ProjectionPrimed, domain.ValidationFrames, domain.PhysicalTimeSeconds, domain.LastTimestepSeconds,
                    domain.MaximumRelativeMassError, domain.MaximumRelativeEnergyError,
                    domain.Substeps, domain.RejectedAttempts,
                    IntervalSubsteps = domain.Substeps >= telemetrySubsteps ? domain.Substeps - telemetrySubsteps : domain.Substeps,
                    IntervalRejectedAttempts = domain.RejectedAttempts >= telemetryRejectedAttempts ? domain.RejectedAttempts - telemetryRejectedAttempts : domain.RejectedAttempts,
                    domain.Publications, domain.NativeError
                },
                Latency = new
                {
                    interval.NativeFrameSubmitMs, interval.NativeFrameWaitMs, interval.ManagedPublicationMs,
                    interval.PresentationLagMs, interval.TextureBatchMs, interval.UnityFrameMs, interval.InvalidSamples
                },
                Memory = MemoryTelemetry()
            };
            var written = telemetry.Append(ReportJson.SerializeLine(line));
            telemetryLineFrames = interval.NativeFramesProcessed; telemetryLineSeconds = seconds;
            if (domain != null) { telemetrySubsteps = domain.Substeps; telemetryRejectedAttempts = domain.RejectedAttempts; }
            Report.DevelopmentTelemetryLines = telemetry.Lines; Report.DevelopmentTelemetryTruncated = telemetry.Truncated;
            if (written || telemetry.Truncated) WriteReport();
        }

        private static object MemoryTelemetry()
        {
            long? resident = null, peak = null; var source = "unavailable";
            try
            {
                const string status = "/proc/self/status";
                if (File.Exists(status))
                {
                    DevelopmentTelemetry.ParseProcessStatus(File.ReadAllLines(status), out resident, out peak);
                    source = "proc-self-status";
                }
                else
                {
                    using var process = Process.GetCurrentProcess();
                    resident = process.WorkingSet64; peak = process.PeakWorkingSet64; source = "process-working-set";
                }
            }
            catch (Exception error) when (error is IOException || error is InvalidDataException || error is InvalidOperationException ||
                error is NotSupportedException || error is UnauthorizedAccessException || error is PlatformNotSupportedException)
            { resident = peak = null; source = "unavailable: " + error.GetType().Name; }
            var collections = new int[GC.MaxGeneration + 1];
            for (var generation = 0; generation < collections.Length; ++generation) collections[generation] = GC.CollectionCount(generation);
            return new { ManagedHeapBytes = GC.GetTotalMemory(false), GcCollections = collections,
                ResidentBytes = resident, PeakResidentBytes = peak, ResidentSource = source };
        }

        private static void FinishTelemetry()
        {
            telemetryFinished = true; DevelopmentTelemetry.Sampler.Stop();
            telemetry?.Dispose(); WriteReport();
        }

        private static void StopTelemetry(Exception error)
        {
            telemetryFinished = true; DevelopmentTelemetry.Sampler.Stop();
            try { telemetry?.Dispose(); } catch (IOException) { }
            Report.DevelopmentTelemetryError = error.ToString();
            try { WriteReport(); } catch (IOException) { }
            UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Development telemetry stopped after an error: " + error.Message);
        }

        // A long run has no colony management. Once every duplicant has died,
        // ONI pauses on its Colony Lost screen and no frame can complete, so
        // the run ends with that reason instead of waiting for its timeout.
        private static bool longRunDuplicantsSeen;
        // A long run keeps every duplicant alive: one fewer than the most seen fails it.
        private static int longRunDuplicantsPeak;
        private static bool LongRunColonyLost()
        {
            if (configuration?.LongRun != true) return false;
            var living = Components.LiveMinionIdentities.Count;
            if (living > 0) longRunDuplicantsSeen = true;
            if (living >= longRunDuplicantsPeak) { longRunDuplicantsPeak = living; return false; }
            if (!longRunDuplicantsSeen) return false;
            Fail(new InvalidOperationException((living == 0 ? "All duplicants died" : $"A duplicant died ({living} of {longRunDuplicantsPeak} alive)") +
                " at cycle " + (GameClock.Instance?.GetCycle().ToString() ?? "unknown") + "."));
            return true;
        }

        // Observer forwarding refreshes its report on every Unity frame for
        // short controls. A long run keeps that report at most one second old.
        private static void WriteProgressReport()
        {
            if (configuration?.LongRun != true) { WriteReport(); return; }
            if (ProgressReportClock.IsRunning && ProgressReportClock.Elapsed.TotalSeconds < 1) return;
            ProgressReportClock.Restart(); WriteReport();
        }
    }
}
