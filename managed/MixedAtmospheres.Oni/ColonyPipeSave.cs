using System;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Mixed-pipe runtime in owner saves (docs/mixed-pipes.md#saved-framing):
    // MACOLP03 section 1 holds ColonyPipeRuntime's opaque MACPIP01 schema-4
    // image. Capture runs inside the settled save; a loaded image is restored
    // once the owner is restored and the game has started, before any positive
    // simulation time, pipe update or save. A failed restore is never replaced
    // by scalar packet mode.
    internal static class ColonyPipeSave
    {
        private static byte[]? pending;
        // Until the full mixed save path passes its in-game controls, only an
        // explicit control (MA_COLONY_PIPE_SAVES=1) lifts the early save guard.
        internal static bool WriterEnabled => Environment.GetEnvironmentVariable("MA_COLONY_PIPE_SAVES") == "1";
        internal static bool Pending => pending != null;
        internal static int Captured { get; private set; }
        internal static int Restored { get; private set; }
        internal static string CapturedSha256 { get; private set; } = "";
        internal static string RestoredSha256 { get; private set; } = "";
        internal static void Reset() { pending = null; Captured = Restored = 0; CapturedSha256 = RestoredSha256 = ""; }
        internal static object Report()
        {
            string audit = ""; bool? closed = null;
            if (ColonyPipeRuntime.Active) { closed = ColonyConduits.Audit(out audit); }
            return new { Pending, Captured, CapturedSha256, Restored, RestoredSha256, ColonyPipeRuntime.Active, AuditClosed = closed, Audit = audit,
                BiologyContext = ColonyPipeCultureState.Report() };
        }
        private static string Hash(byte[] bytes)
        { using var sha = System.Security.Cryptography.SHA256.Create(); return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant(); }

        // Inside Sim.Save, after the barrier and the owner checkpoint.
        internal static byte[]? Capture()
        {
            TryRestore();
            if (pending != null || ColonyLoadRefusal.Refused) throw new InvalidOperationException("A loaded mixed-pipe image has not been restored; the colony cannot be saved.");
            if (!ColonyPipeRuntime.Active) return null;
            var image = ColonyPipeRuntime.Capture();
            ++Captured; CapturedSha256 = Hash(image); return image;
        }
        internal static void Load(byte[] image)
        {
            if (image == null || image.Length == 0) throw new System.IO.InvalidDataException("Empty mixed-pipe section.");
            pending = image;
        }
        // Never throws into a game hook: a failed restore refuses the colony.
        internal static void TryRestore()
        {
            var image = pending;
            if (image == null || ColonyLoadRefusal.Refused || !ColonyOwner.Active || Game.Instance == null || !Game.Instance.GameStarted()) return;
            pending = null;
            try { ColonyPipeRuntime.Restore(image); ++Restored; RestoredSha256 = Hash(image); }
            catch (Exception error) { ColonyLoadRefusal.Refuse(error); }
        }
        // Positive time must never run before a loaded pipe image is restored.
        internal static void BeforePositiveTime(float seconds)
        {
            if (!(seconds > 0)) return;
            if(pending!=null)TryRestore();
            if (pending != null && !ColonyLoadRefusal.Refused)
                ColonyLoadRefusal.Refuse(new InvalidOperationException("Simulation time cannot advance before the saved mixed pipes are restored."));
            if(!ColonyLoadRefusal.Refused&&ColonyPipeCultureState.NeedsReadyBarrier)
                try{ColonyPipeCultureState.RequireReady();}
                catch(Exception error){ColonyLoadRefusal.Refuse(error);}
        }
    }

    [HarmonyPatch(typeof(ConduitFlow), nameof(ConduitFlow.Sim200ms))]
    internal static class ColonyPipeSaveFlowPatch
    {
        [HarmonyPriority(Priority.First)]
        private static bool Prefix()
        {
            ColonyPipeSave.TryRestore();
            // A refused or unrestored colony's pipes never move.
            return !ColonyPipeSave.Pending && !ColonyLoadRefusal.Refused;
        }
    }
}
