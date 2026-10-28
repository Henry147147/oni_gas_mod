using System.Globalization;
using MixedAtmospheres.Oni;
using Clock = MixedAtmospheres.Oni.ColonyPerformanceClock;

var checks = 0;
void Check(bool ok, string why) { ++checks; if (!ok) throw new Exception(why); }
void Near(double actual, double expected, string why) => Check(Math.Abs(actual - expected) < 1e-10, why + $": {actual:R} != {expected:R}");
Clock Start(ulong frame = 0, double owner = 0, double game = 0)
{
    var result = new Clock(1000);
    Check(result.Observe(7, frame, owner, game, 1000, false) == Clock.ResetReason.Initial, "initial baseline");
    return result;
}

// Missing completion samples still account for every physical/game second.
var clock = Start();
var first = clock.Current;
Check(clock.Observe(7, 3, .6, .4, 1200, false) == Clock.ResetReason.None, "skipped completions reset a healthy clock");
Near(clock.Total.OwnerPerReal, 3, "skipped completion physical throughput");
Near(clock.Total.GamePerReal, 2, "independent game clock throughput");
Check(clock.Total.Frames == 3 && !clock.Total.PauseObserved, "skipped completion frame count");
clock.Observe(7, 3, .6, .4, 1300, false);
Near(clock.Total.OwnerSeconds, .6, "duplicate poll invented physical progress");
Near(clock.Total.WallSeconds, .3, "duplicate poll lost elapsed wall time");
Near(clock.Total.OwnerPerReal, 2, "duplicate poll hid a real stall");
clock.Observe(7, 10, 2, 1.8, 2000, false);
Near(clock.Total.OwnerPerReal, 2, "latest frame duration replaced missing physical progress");
Near(clock.Total.GamePerReal, 1.8, "game and owner clocks were conflated");

// Pauses remain in the denominator; an otherwise idle pause cannot certify 3x.
clock = Start();
clock.Observe(7, 5, 1, 1, 2000, false);
clock.Observe(7, 5, 1, 1, 3000, true);
clock.Observe(7, 5, 1, 1, 4000, false);
Near(clock.Total.OwnerPerReal, 1.0 / 3, "pause was silently subtracted");
Check(clock.Total.PauseObserved, "paused observation was not reported");
var afterPause = clock.Current;
clock.Observe(7, 20, 4, 4, 5000, false);
Near(clock.Since(afterPause).OwnerPerReal, 3, "clean post-pause window rate");
Check(!clock.Since(afterPause).PauseObserved && clock.Total.PauseObserved, "pause leaked into a later clean window or disappeared from totals");
clock = Start();
clock.Observe(7, 1, .2, 0, 2000, true);
Near(clock.Total.GamePerReal, 0, "paused game acquired the in-flight owner's progress");
Near(clock.Total.OwnerPerReal, .2, "in-flight completion after pause was lost");

// Saved absolute clocks/counters are origins, not newly simulated work.
clock = Start(1000000000, 200000000, 300000000);
first = clock.Current;
clock.Observe(7, 1000000003, 200000000.75, 300000000.5, 1250, false);
Near(clock.Total.OwnerPerReal, 3, "restored physical clock origin counted as progress");
Near(clock.Total.GamePerReal, 2, "restored game clock origin counted as progress");
Check(clock.Observe(8, 32, 1000, 800, 2000, false) == Clock.ResetReason.Session, "new session did not rebaseline");
Check(clock.PreviousRun.Valid && !clock.Since(first).Valid && !clock.Total.Valid, "session boundary mixed measurements");
clock.Observe(8, 33, 1000.5, 800.25, 2500, false);
Near(clock.Total.OwnerPerReal, 1, "new session owner rate includes its saved origin");

foreach (var expected in new[] { Clock.ResetReason.FrameBackwards, Clock.ResetReason.OwnerClockBackwards,
    Clock.ResetReason.GameClockBackwards, Clock.ResetReason.WallClockBackwards })
{
    clock = Start(); clock.Observe(7, 10, 2, 2, 2000, false); first = clock.Current;
    var reason = clock.Observe(7, expected == Clock.ResetReason.FrameBackwards ? 9UL : 11UL,
        expected == Clock.ResetReason.OwnerClockBackwards ? 1 : 3,
        expected == Clock.ResetReason.GameClockBackwards ? 1 : 3,
        expected == Clock.ResetReason.WallClockBackwards ? 1999 : 3000, false);
    Check(reason == expected && clock.PreviousRun.Valid && !clock.Since(first).Valid && !clock.Total.Valid, "regression failed to retire its prior epoch: " + expected);
}
foreach (var bad in new[] { -1, double.NaN, double.PositiveInfinity, double.NegativeInfinity })
{
    clock = Start(); clock.Observe(7, 1, .2, .2, 1200, false);
    Check(clock.Observe(7, 2, bad, .4, 1400, false) == Clock.ResetReason.InvalidClock && !clock.Ready, "invalid physical clock accepted");
    Check(clock.Observe(7, 2, .4, .4, 1400, false) == Clock.ResetReason.Initial && !clock.Total.Valid, "invalid observation poisoned later baselines");
    Check(clock.Observe(7, 3, .6, bad, 1600, false) == Clock.ResetReason.InvalidClock && !clock.Ready, "invalid game clock accepted");
}
clock = Start();
Check(clock.Observe(7, 1, .2, .2, -1, false) == Clock.ResetReason.InvalidClock, "negative monotonic stamp accepted");
clock = Start(); clock.Observe(7, 1, .2, .2, 1000, false);
Check(!clock.Total.Valid && clock.Total.OwnerPerReal == 0, "zero wall interval divided by zero");
clock.Observe(7, 2, .4, .4, 1200, false);
Near(clock.Total.OwnerPerReal, 2, "zero-length observation lost progress");
clock = Start(ulong.MaxValue - 2);
clock.Observe(7, ulong.MaxValue, 1, 1, 2000, false);
Check(clock.Total.Frames == 2, "large saved frame overflowed");
clock.Reset(); Check(!clock.Ready && !clock.Total.Valid, "explicit reset retained a stale run");
try { _ = new Clock(0); throw new Exception("zero-frequency clock accepted"); } catch (ArgumentOutOfRangeException) { ++checks; }

// The high-frequency sampling path must not add a new GC source.
clock = Start();
for (var i = 0; i < 1000; ++i) clock.Observe(7, 0, 0, 0, 2000 + i, false);
var allocated = GC.GetAllocatedBytesForCurrentThread();
for (var i = 0; i < 10000; ++i) { clock.Observe(7, 0, 0, 0, 3000 + i, false); _ = clock.Total; }
Check(GC.GetAllocatedBytesForCurrentThread() == allocated, "clock observations allocated managed objects");

// Publication matching is by submitted frame identity, not the latest submit.
var timing = new ColonyFrameTiming();
Check(timing.Submit(0, 100) == 1 && timing.Submit(0, 200) == 2, "queued submission identities");
Check(timing.Observe(1, out var submitted) && submitted == 100, "first completion paired with newest submission");
Check(timing.Observe(2, out submitted) && submitted == 200 && !timing.Observe(2, out _), "duplicate completion counted twice");
timing.Reset();
Check(timing.Submit(32, 300) == 33 && timing.Submit(32, 400) == 34, "restored frame submission identities");
Check(timing.Observe(34, out submitted) && submitted == 400, "skipped completion shifted latency identity");
timing = new ColonyFrameTiming(2);
timing.Submit(0, 100); timing.Submit(0, 200); timing.Submit(0, 300);
Check(timing.PendingCount == 2 && timing.DroppedSubmissions == 1 && !timing.Observe(1, out _), "stalled submissions grew without bound or retained a discarded match");
Check(timing.Observe(2, out submitted) && submitted == 200 && timing.Observe(3, out submitted) && submitted == 300,
    "bounded submission queue reassigned surviving frame identities");
timing.Reset(); Check(timing.PendingCount == 0 && timing.DroppedSubmissions == 0, "submission losses leaked across lifecycle reset");

// Exercise the actual formatter/control path using original minimal engine
// stubs: distinct game/owner clocks, skipped completions and a short final run.
ColonyPerformanceLog.Enabled = true; ColonyPerformanceLog.Reset();
GameClock.Instance = new GameClock(); SpeedControlScreen.Instance = new SpeedControlScreen { Speed = 2 };
Game.Instance = new Game { Started = true };
UnityEngine.Debug.Lines.Clear();
ColonyOwner.Status = new OniNativeColony.Status { Session = 9, Species = 2 };
ColonyPerformanceLog.UnityFrame(.01f);
ColonyOwner.Status = new OniNativeColony.Status { Session = 9, Species = 2, CompletedFrames = 150, TimeSeconds = 30,
    LastAdvanceMilliseconds = 50, StageMilliseconds = new[] { 1.0, 2, 3, 4, 5, 6 } };
GameClock.Instance.Seconds = 24;
ColonyPerformanceLog.Published(9, 0, System.Diagnostics.Stopwatch.Frequency / 10, true);
ColonyPerformanceLog.UnityFrame(.02f);
ColonyPerformanceLog.OwnerFrame(ColonyOwner.Status, 4, 0);
var line = UnityEngine.Debug.Lines.Single(value => value.StartsWith("[Mixed Atmospheres] perf frames ", StringComparison.Ordinal));
Check(line.Contains("owner_seconds 30.000000 game_seconds 24.000000") && line.Contains("owner_frames 150") && line.Contains("samples 1"), "logger lost unsampled clocks/counters");
Check(line.Contains("submit_to_publication_ms mean 100.0") && line.Contains("capacity_estimate 4.00") && line.Contains("speed_button 3"), "logger conflated handoff/measured speed/capacity/button");
Check(line.Contains("ledger_projection 6.0") && UnityEngine.Debug.Lines[0].Contains("owner_ms excludes projection/publication"), "partial native timer was labelled end to end");
ColonyPerformanceLog.OwnerFrame(ColonyOwner.Status, 4, 0);
Check(UnityEngine.Debug.Lines.Count(value => value.Contains("perf frames ")) == 1, "duplicate owner observation emitted a second window");
SpeedControlScreen.Instance.IsPaused = true; ColonyPerformanceLog.UnityFrame(.01f);
SpeedControlScreen.Instance.IsPaused = false;
ColonyOwner.Status = new OniNativeColony.Status { Session = 9, CompletedFrames = 153, TimeSeconds = 30.6, LastAdvanceMilliseconds = 50 };
GameClock.Instance.Seconds = 24.6; ColonyPerformanceLog.UnityFrame(.01f); ColonyPerformanceLog.OwnerFrame(ColonyOwner.Status, 4, 0);
var timerun = ColonyPerformanceLog.Timerun();
object Property(string name) => timerun.GetType().GetProperty(name)!.GetValue(timerun)!;
Check((bool)Property("Valid") && (bool)Property("PauseObserved") && (ulong)Property("LastObservedFrame") == 153,
    "bounded gameplay report lost final owner observation or pause scope");
Near((double)Property("OwnerSeconds"), 30.6, "gameplay report physical progress");
Near((double)Property("GameSeconds"), 24.6, "gameplay report game clock progress");
ColonyPerformanceLog.FinishRun();
Check(UnityEngine.Debug.Lines.Any(value => value.Contains("perf frames 151-153 session 9 partial 1")), "short final window missing");
Check(UnityEngine.Debug.Lines.Any(value => value.Contains("perf run session 9 frames 0-153 reason session_end") && value.Contains("pause_observed 1")), "final timerun missing or pause hidden");
Check(!UnityEngine.Debug.Lines.Any(value => value.StartsWith("WARNING", StringComparison.Ordinal)), "diagnostic formatter failed");
// Startup restores the game clock after the owner may already exist. Ignore
// that history until the actual game has started, then measure only new work.
Game.Instance.Started = false; GameClock.Instance.Seconds = 0;
ColonyOwner.Status = new OniNativeColony.Status { Session = 10, CompletedFrames = 32, TimeSeconds = 1000 };
ColonyPerformanceLog.UnityFrame(.01f);
timerun = ColonyPerformanceLog.Timerun();
Check(!(bool)Property("Valid"), "pre-start restored clock became a gameplay timerun");
Game.Instance.Started = true; GameClock.Instance.Seconds = 1000; ColonyPerformanceLog.UnityFrame(.01f);
ColonyOwner.Status = new OniNativeColony.Status { Session = 10, CompletedFrames = 33, TimeSeconds = 1000.2, LastAdvanceMilliseconds = 50 };
GameClock.Instance.Seconds = 1000.2; ColonyPerformanceLog.UnityFrame(.01f);
timerun = ColonyPerformanceLog.Timerun();
Near((double)Property("OwnerSeconds"), .2, "restored owner history became new progress");
Near((double)Property("GameSeconds"), .2, "restored game history became new progress");
ColonyPerformanceLog.FinishRun();
// Missing pacing observations must not accumulate unbounded latency storage.
ColonyOwner.Status = new OniNativeColony.Status { Session = 11 };
GameClock.Instance.Seconds = 0; ColonyPerformanceLog.UnityFrame(.01f);
for (var i = 0; i < 400; ++i) ColonyPerformanceLog.Published(11, i, i + 100, true);
timerun = ColonyPerformanceLog.Timerun();
Check((long)Property("DroppedPublicationSamples") == 100, "missing pacing observations grew latency samples without an explicit bound/loss count");
ColonyPerformanceLog.FinishRun();
Console.WriteLine($"PASS performance clocks, lifecycle resets and publication metrics ({checks} checks)");
