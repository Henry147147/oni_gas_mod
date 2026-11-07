// Original minimal declarations for exercising our diagnostics without a game
// installation. No game implementation or licensed metadata is embedded here.
namespace UnityEngine
{
    internal static class Debug
    {
        internal static readonly List<string> Lines = new();
        public static void Log(object value) => Lines.Add(value.ToString()!);
        public static void LogWarning(object value) => Lines.Add("WARNING " + value);
    }
    internal static class Time { public static float timeScale = 1; }
    internal static class SystemInfo
    {
        public static string operatingSystem = "test", processorType = "test", graphicsDeviceName = "test";
        public static int processorCount = 1, systemMemorySize = 1024;
    }
}
internal sealed class GameClock
{
    public static GameClock? Instance;
    internal double Seconds;
    public double GetTime() => Seconds;
}
internal sealed class Game
{
    public static Game? Instance;
    internal bool Started;
    public bool GameStarted() => Started;
}
internal sealed class SpeedControlScreen
{
    public static SpeedControlScreen? Instance;
    public bool IsPaused;
    internal int Speed;
    public int GetSpeed() => Speed;
}
namespace MixedAtmospheres.Oni
{
    internal static class ColonyPacing { internal static long HeldTicks = 0; }
    internal static class ColonyOwner
    {
        internal static OniNativeColony.Status Status;
        internal static long PerformanceDroppedSubmissions = 0;
    }
    internal static class OniNativeColony
    {
        internal struct Status
        {
            internal ulong Session, CompletedFrames;
            internal uint Species;
            internal double TimeSeconds, LastAdvanceMilliseconds;
            internal double[]? StageMilliseconds;
        }
        // ma_oni_native_timing: [mean, max, count] per phase; post_to_handoff (phase 8) is 12.5/40/150.
        internal static uint NativeTiming(double[] values, uint capacity)
        {
            if (capacity < 33) return 0;
            for (var k = 0; k < 11; ++k) { values[3 * k] = k; values[3 * k + 1] = 2 * k; values[3 * k + 2] = 150; }
            values[24] = 12.5; values[25] = 40; return 11;
        }
    }
}
