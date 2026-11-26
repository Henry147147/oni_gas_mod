using System;
using System.Diagnostics;
using System.IO;
using System.Threading;

namespace MixedAtmospheres.Oni
{
    // Moono's background-save tweak (docs/game-update-resilience.md#moono)
    // writes a save into "<save>.writing" beside it and moves that over the
    // save when it is complete; until then the save holds only its header.
    // Before one of our readers opens a save that may have just been written
    // (menu load, resume, the load right after an autosave), the write is
    // waited for. Pure (System only) for tools/DistributionTests.
    internal static class PendingSaveWrite
    {
        internal const string Suffix = ".writing";
        // Moono retries the move for 30 s after compressing; a large colony
        // compresses in a few seconds.
        internal const int WaitMilliseconds = 45_000;

        // True when no write is pending (or it finished within the bound);
        // `waited` is how long the caller waited.
        internal static bool Wait(string save, out long waited, int bound = WaitMilliseconds, Func<string, bool>? exists = null, Action<int>? sleep = null)
        {
            exists ??= File.Exists; sleep ??= Thread.Sleep;
            waited = 0;
            string pending;
            try { pending = save + Suffix; if (!exists(pending)) return true; }
            catch (Exception) { return true; }
            var clock = Stopwatch.StartNew();
            for (var pause = 10; ; pause = Math.Min(pause * 2, 200))
            {
                bool still;
                try { still = exists(pending); } catch (Exception) { still = false; }
                waited = clock.ElapsedMilliseconds;
                if (!still) return true;
                if (waited >= bound) return false;
                sleep(pause);
            }
        }
    }
}
