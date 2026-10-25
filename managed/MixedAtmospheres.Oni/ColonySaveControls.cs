using System;
using System.IO;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Owner save controls for the colony-owner harness (docs/colony-owner.md#saves),
    // selected by MA_COLONY_SAVE_CONTROL (comma-separated, run in order from
    // half the frames; the run completes only after all of them):
    //   autosave    the game's own day autosave entry (GameClock.DoAutoSave ->
    //               Game.StartDelayedSave -> SaveLoader.Save, isAutoSave) while
    //               the colony runs; completes when that save has been written.
    //   save        an ordinary paused save (save-<frame>.sav) of whatever is in
    //               flight; with a scenario it runs at MA_COLONY_SAVE_FRAME.
    //   particle    a known CO2 parcel spawned just before an ordinary paused
    //               save (particle.sav), which must hold it in flight.
    //   io-failure  an ordinary save whose destination is a directory: ONI's
    //               writer fails after serialization; the owner must keep
    //               running, nothing is left at the path, and a later save works.
    internal static class ColonySaveControls
    {
        private static readonly string[] Controls = (Environment.GetEnvironmentVariable("MA_COLONY_SAVE_CONTROL") ?? "")
            .Split(new[] { ',' }, StringSplitOptions.RemoveEmptyEntries);
        private static readonly System.Reflection.MethodInfo AutoSave = AccessTools.Method(typeof(GameClock), "DoAutoSave");
        private static int index;
        private static readonly System.Collections.Generic.List<string> details = new System.Collections.Generic.List<string>();
        private static string Control => index < Controls.Length ? Controls[index] : "";
        internal static int Stage { get; private set; } // 0 idle, 1 running
        internal static string AutosavePath { get; private set; } = "";
        internal static string ParticlePath { get; private set; } = "";
        internal static int SavesBefore { get; private set; } = -1;
        internal static int AutoSaves { get; private set; }
        internal static bool Done => index >= Controls.Length;
        // During a scenario: the owner frame (after the run's start) at which the controls run.
        internal static ulong? ScenarioFrame => ulong.TryParse(Environment.GetEnvironmentVariable("MA_COLONY_SAVE_FRAME"), out var frame) ? frame : (ulong?)null;
        internal static object? Report() => Controls.Length == 0 ? null :
            new { Controls, Completed = index, Details = details.ToArray(), AutosavePath, ParticlePath, AutoSaves };
        internal static void Reset() { index = 0; Stage = 0; details.Clear(); AutosavePath = ParticlePath = ""; SavesBefore = -1; AutoSaves = 0; }
        private static void Complete(string detail) { details.Add(Control + ": " + detail); ++index; Stage = 0; }

        // Called each validation update before the frame target is reached.
        internal static void Step(string outputDirectory)
        {
            if (Done) return;
            switch (Control)
            {
                case "autosave":
                    if (Stage == 0)
                    {
                        if (AutoSave == null) throw new MissingMethodException(nameof(GameClock), "DoAutoSave");
                        SavesBefore = ColonyOwnerPersistence.Saves; Stage = 1;
                        AutoSave.Invoke(GameClock.Instance, new object[] { GameClock.Instance.GetCycle() });
                        return;
                    }
                    if (ColonyOwnerPersistence.Saves == SavesBefore) return; // the delayed save runs over a few frames
                    if (AutoSaves != 1 || ColonyOwnerPersistence.Saves != SavesBefore + 1 || !File.Exists(AutosavePath))
                        throw new InvalidOperationException("The game autosave did not write exactly one owner save: " + AutosavePath);
                    Complete($"autosave written at owner frame {ColonyOwnerPersistence.LastSavedFrame}");
                    return;
                case "io-failure":
                {
                    var path = Path.Combine(outputDirectory, "unwritable.sav");
                    Directory.CreateDirectory(path);
                    SavesBefore = ColonyOwnerPersistence.Saves; Stage = 1;
                    SpeedControlScreen.Instance.Pause(false);
                    string failure;
                    try { SaveLoader.Instance.Save(path, false, false); failure = "no exception (ONI reported the write problem)"; }
                    catch (Exception error) when (error is IOException || error is UnauthorizedAccessException) { failure = error.GetType().Name; }
                    var status = OniNativeColony.Status.Empty;
                    OniNativeColony.Check(OniNativeColony.CopyStatus(ref status));
                    if (!ColonyOwner.Active || status.State != 1 || !Directory.Exists(path) || File.Exists(path) ||
                        Directory.GetFileSystemEntries(path).Length != 0)
                        throw new InvalidOperationException("A failed save changed the owner or left data at its destination.");
                    Directory.Delete(path);
                    Complete($"write failed ({failure}); owner running at frame {status.CompletedFrames}; destination untouched");
                    return;
                }
                case "save":
                {
                    // An ordinary paused save at this frame (save-<frame>.sav), whatever is in flight.
                    SpeedControlScreen.Instance.Pause(false);
                    SavesBefore = ColonyOwnerPersistence.Saves; Stage = 1;
                    var path = Path.Combine(outputDirectory, "save-" + ColonyOwner.Status.CompletedFrames + ".sav");
                    SaveLoader.Instance.Save(path, false, false);
                    if (ColonyOwnerPersistence.Saves != SavesBefore + 1 || !File.Exists(path)) throw new InvalidOperationException("The control save was not written.");
                    ParticlePath = path; Complete($"saved at owner frame {ColonyOwnerPersistence.LastSavedFrame}");
                    return;
                }
                case "particle":
                {
                    // One known CO2 parcel in flight at an ordinary paused save.
                    SpeedControlScreen.Instance.Pause(false);
                    var minion = Components.LiveMinionIdentities.Items[0];
                    var cell = Grid.CellAbove(Grid.CellAbove(Grid.PosToCell(minion)));
                    CO2Manager.instance.SpawnCO2(Grid.CellToPosCCC(cell, Grid.SceneLayer.Front), .03125f, 333.25f, false);
                    SavesBefore = ColonyOwnerPersistence.Saves; Stage = 1;
                    var path = Path.Combine(outputDirectory, "particle.sav");
                    SaveLoader.Instance.Save(path, false, false);
                    if (ColonyOwnerPersistence.Saves != SavesBefore + 1 || !File.Exists(path) || ColonySavedParticles.SavedCount < 1)
                        throw new InvalidOperationException("The particle save did not keep its in-flight CO2.");
                    ParticlePath = path; Complete($"saved {ColonySavedParticles.SavedCount} particles at owner frame {ColonyOwnerPersistence.LastSavedFrame}");
                    return;
                }
                default: throw new InvalidDataException("Unknown MA_COLONY_SAVE_CONTROL: " + Control);
            }
        }
        internal static void ObserveFileSave(string path, bool isAutoSave)
        {
            if (Control != "autosave" || Stage != 1 || !isAutoSave) return;
            ++AutoSaves; AutosavePath = path;
        }
    }

    [HarmonyPatch(typeof(SaveLoader), nameof(SaveLoader.Save), new[] { typeof(string), typeof(bool), typeof(bool) })]
    internal static class ColonySaveControlObserver
    {
        private static void Postfix(string __0, bool __1) => ColonySaveControls.ObserveFileSave(__0, __1);
    }
}

namespace MixedAtmospheres.Oni
{
    // MA_COLONY_CONTINUATION_FRAMES=N: after a restore, hold the simulation tick
    // after each of the first N positive frames, let the owner finish it and
    // record the owner image hash at exactly restored + k (k = 1..N). Each
    // session of a menu reload records its own sequence; identical inputs must
    // give identical images.
    internal static class ColonyContinuationProbe
    {
        private static readonly int Frames = int.TryParse(Environment.GetEnvironmentVariable("MA_COLONY_CONTINUATION_FRAMES"), out var n) && n > 0 ? n : 0;
        private static ulong restored; private static int submitted; private static bool armed;
        private static readonly System.Collections.Generic.List<string> hashes = new System.Collections.Generic.List<string>();
        internal static ulong Frame { get; private set; }
        internal static string ImageSha256 => hashes.Count == 0 ? "" : hashes[hashes.Count - 1];
        internal static bool Done => Frames == 0 || !armed;
        internal static bool Holding => armed && submitted > hashes.Count;
        internal static object? Report() => Frames == 0 ? null : new { Frames, RestoredFrame = restored, Submitted = submitted, Frame, ImageSha256, Hashes = hashes.ToArray() };
        internal static void Reset() { restored = 0; submitted = 0; armed = false; Frame = 0; hashes.Clear(); }
        internal static void Restored(ulong frame) { if (Frames == 0) return; Reset(); restored = frame; armed = true; }
        private static (ulong Started, ulong Ended) framesBeforeHeld;
        internal static void Submitted(float seconds)
        {
            if (!armed || !(seconds > 0) || !ColonyOwner.Active) return;
            ++submitted; framesBeforeHeld = ColonySaveBarrier.NativeFrameCounters(); // this call posts the held frame next
        }
        // Validation update: capture each held frame once it has completed.
        internal static void Step()
        {
            if (!Holding) return;
            ColonySaveBarrier.WaitNativeFrameAfter(framesBeforeHeld);
            OniNativeColony.Check(OniNativeColony.Wait());
            var image = ColonyOwner.Checkpoint();
            var frame = ColonyCheckpointMetadata.CompletedFrames(image);
            var expected = restored + (ulong)hashes.Count + 1;
            if (frame != expected) throw new InvalidOperationException($"Continuation probe held at frame {frame}, expected {expected}.");
            using var sha = System.Security.Cryptography.SHA256.Create();
            hashes.Add(BitConverter.ToString(sha.ComputeHash(image)).Replace("-", "").ToLowerInvariant()); Frame = frame;
            if (hashes.Count == Frames) armed = false;
        }
    }

    [HarmonyPatch(typeof(Game), "UnsafeSim200ms")]
    internal static class ColonyContinuationSubmitPatch
    {
        private static bool Prefix(float __0)
        {
            ColonyPipeSave.BeforePositiveTime(__0);
            // Readiness can refuse inside this very call, after the outer
            // SimEveryTick guard has already passed. Do not submit that tick.
            if (__0 > 0 && ColonyLoadRefusal.Refused) return false;
            ColonyContinuationProbe.Submitted(__0);
            return true;
        }
    }
    [HarmonyPatch(typeof(Game), "SimEveryTick")]
    internal static class ColonyContinuationHoldPatch
    {
        [HarmonyPriority(Priority.First)]
        private static bool Prefix() => !ColonyContinuationProbe.Holding;
    }
}
