using System;
using System.Collections.Generic;

namespace MixedAtmospheres.Oni
{
    // Germ emitters with a negative count: a Sanishell cleaning the water it
    // stands in (CrabFreshWaterConfig, CreatureDiseaseCleaner). ONI's native
    // simulation runs every germ emitter. An emitter that adds germs adds
    // them to ONI's own cells, where the owner finds them; one that removes
    // germs finds none to remove there, because the owner holds the world's
    // germs itself (docs/colony-owner.md#germs), so until 1.13.9 a Sanishell
    // cleaned nothing.
    //
    // The owner therefore applies a negative emitter itself, as ONI does
    // (measured under ONI's own simulation, scenario germ-cleaner and its
    // probe): once per emit interval the count is taken from the emitter's
    // cell and from every open cell within depth - 1 steps of it (depth 2:
    // the cell and its four neighbours), each cell's count ending at zero at
    // least. The messages still reach ONI, which keeps the emitter's handle.
    internal static unsafe class ColonyGermCleaners
    {
        private const int ModifyDiseaseEmitter = -1899123924, RemoveDiseaseEmitter = 468135926;
        private sealed class Emitter { public int Cell, Count; public byte Disease, Depth; public float Interval, Elapsed; }
        private static readonly Dictionary<int, Emitter> emitters = new Dictionary<int, Emitter>();
        private static readonly List<ColonyWorldGerms.GermEdit> edits = new List<ColonyWorldGerms.GermEdit>();
        private static readonly List<int> reached = new List<int>();
        internal static int Emitters => emitters.Count;
        internal static long Emissions { get; private set; }
        internal static long EditedCells { get; private set; }
        internal static void Reset() { emitters.Clear(); Emissions = 0; EditedCells = 0; }

        // Every message, whether or not the owner runs yet: a loaded colony's critters register before it starts.
        internal static void Observe(int message, int length, int count, byte* payload)
        {
            if (message == ModifyDiseaseEmitter && length == 20)
                for (var k = 0; k < count; ++k)
                {
                    var record = payload + k * length;
                    int handle = *(int*)record, cell = *(int*)(record + 4), emitCount = *(int*)(record + 16);
                    var interval = *(float*)(record + 12);
                    if (emitCount >= 0 || record[8] == byte.MaxValue || !(interval > 0)) { emitters.Remove(handle); continue; }
                    emitters.TryGetValue(handle, out var known);
                    emitters[handle] = new Emitter { Cell = cell, Count = emitCount, Disease = record[8], Depth = record[9], Interval = interval, Elapsed = known?.Elapsed ?? 0 };
                }
            else if (message == RemoveDiseaseEmitter && length == 8)
                for (var k = 0; k < count; ++k) emitters.Remove(*(int*)(payload + k * length));
        }

        // A positive game tick of the running owner.
        internal static void Tick(float seconds)
        {
            // Only where the owner evolves the germs: in a legacy colony ONI's own emitter finds the shown germ
            // in its cells, and removing it here as well would clean twice.
            if (emitters.Count == 0 || !(seconds > 0) || !ColonyWorldGerms.OwnerEvolves) return;
            edits.Clear();
            foreach (var emitter in emitters.Values)
            {
                emitter.Elapsed += seconds;
                // Once per interval: an interval shorter than the tick emits several times in it (eight at most).
                var due = (int)Math.Min(8, Math.Floor(emitter.Elapsed / emitter.Interval));
                if (due <= 0) continue;
                emitter.Elapsed -= due * emitter.Interval;
                if (emitter.Elapsed >= emitter.Interval) emitter.Elapsed = 0;
                Emissions += due;
                Reach(emitter.Cell, Math.Max(0, emitter.Depth - 1));
                foreach (var cell in reached)
                    if (ColonyOwner.OwnsFluidCell(cell))
                        edits.Add(new ColonyWorldGerms.GermEdit { PublicCell = (uint)cell, Disease = emitter.Disease + 1u, Change = (double)emitter.Count * due });
            }
            if (edits.Count == 0) return;
            ColonyWorldGerms.Queue(edits);
            EditedCells += edits.Count;
        }

        // The emitter's cell and the open cells of its world within the given number of steps.
        private static void Reach(int origin, int steps)
        {
            reached.Clear();
            if (!Grid.IsValidCell(origin) || Grid.Solid[origin]) return;
            reached.Add(origin);
            for (int from = 0, step = 0; step < steps; ++step)
            {
                var to = reached.Count;
                for (; from < to; ++from)
                    foreach (var next in new[] { Grid.CellLeft(reached[from]), Grid.CellRight(reached[from]), Grid.CellAbove(reached[from]), Grid.CellBelow(reached[from]) })
                        if (Grid.IsValidCell(next) && !Grid.Solid[next] && Grid.WorldIdx[next] == Grid.WorldIdx[origin] && !reached.Contains(next)) reached.Add(next);
            }
        }
    }
}
