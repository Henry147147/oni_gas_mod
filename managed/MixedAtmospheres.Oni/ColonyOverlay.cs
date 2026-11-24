using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;

namespace MixedAtmospheres.Oni
{
    // The colony owner's overlay pass (ColonyOwner.Publish) over the views
    // rectangle, with the cached publication map. A cell the owner does not
    // publish shows native's cell, as does one a solid has just replaced (the
    // owner displaces its fluid at its next step). ONI's Grid.Element follows
    // only substance-change events, so a cell the owner left gets one.
    //
    // A full pass also records the ownership runs of the views it read:
    // maximal stretches of view cells with one ownership whose public cells
    // are consecutive. While those views stay the published ones (the view
    // cache found no new frame: every paused or zero-time data request), the
    // next pass walks the runs instead: it reads the views only of owned cells
    // and checks unowned ones straight from the copy. It visits the same cells
    // in the same order with the same reads of the copy and Grid.Element, so
    // every write and event, and their order, are those of the full pass
    // (tools/OwnedSnapshotTests compares both with the original loop).
    //
    // A full pass over a large rectangle is made in parts on several threads
    // (it runs in the frame of every tick, 3 ms for the 260,000 views of a
    // Spaced Out! cluster on one thread). Each part walks a stretch of the
    // views exactly as the single loop does and keeps its own events and
    // runs; a cell is written by the one part that holds its view, and the
    // parts' events and runs are joined in view order, a run that crosses a
    // boundary becoming one run again. MA_COLONY_OVERLAY_THREADS=1 keeps the
    // single loop.
    internal static unsafe class ColonyOverlay
    {
        internal struct Planes { internal float* Masses, Temperatures, Radiation; internal ushort* Elements; internal byte* Diseases; internal int* Germs; }
        private static int[] runs = new int[3 * 64]; // first view index, first public cell, length*2 + owned
        private static int runCount = -1;            // -1: no runs for the current views
        private static OniNativeColony.CellView[]? runViews;
        private static int[]? runMap;
        internal static int Runs => runCount < 0 ? -1 : runCount / 3;
        internal static long RunPasses { get; private set; }
        internal static long FullPasses { get; private set; }

        // Views per part, the rectangle size from which a full pass is made in
        // parts, and the threads that take them (the caller is one).
        internal static int PartViews = 16384, ParallelViews = 65536;
        private static readonly ParallelOptions threads = new ParallelOptions { MaxDegreeOfParallelism = Threads() };
        private static int Threads()
        {
            if (int.TryParse(Environment.GetEnvironmentVariable("MA_COLONY_OVERLAY_THREADS"), out var requested) && requested >= 1) return Math.Min(requested, 16);
            return Math.Max(1, Math.Min(4, Environment.ProcessorCount / 4));
        }
        private sealed class Part
        {
            internal Sim.SubstanceChangeInfo[] Changes = new Sim.SubstanceChangeInfo[64]; internal int ChangeCount;
            internal uint[] Cells = new uint[64]; internal float[] Rads = new float[64]; internal int Irradiated;
            internal int[] Runs = new int[3 * 64]; internal int RunCount, Owned;
        }
        private static Part[] parts = Array.Empty<Part>();
        internal static long ParallelPasses { get; private set; }
        internal static bool SeveralThreads => threads.MaxDegreeOfParallelism > 1;
        // The runs of the last full pass (tests).
        internal static int[] RunTriples() => runCount < 0 ? Array.Empty<int>() : runs.AsSpan(0, runCount).ToArray();

        internal static void Forget() { runCount = -1; runViews = null; runMap = null; }
        internal static void Reset() { Forget(); solidOf = null; solidElements = null; solidCount = -1; RunPasses = FullPasses = ParallelPasses = 0; }

        // `unchanged`: the views hold the frame the previous pass read (the
        // same array, not written since). Returns the owned cells.
        internal static int Apply(OniNativeColony.CellView[] views, int[] map, in Planes planes, Element[] shown, bool[] solid, bool unchanged,
            ref Sim.SubstanceChangeInfo[] changes, ref int changeCount, ref uint[] irradiatedCells, ref float[] irradiatedRads, ref int irradiatedCount)
        {
            if (map.Length < views.Length) throw new ArgumentException("The publication map does not cover the views.", nameof(map));
            var owned = 0;
            if (unchanged && runCount >= 0 && ReferenceEquals(runViews, views) && ReferenceEquals(runMap, map))
            {
                ++RunPasses;
                for (var r = 0; r < runCount; r += 3)
                {
                    int first = runs[r], global = runs[r + 1], length = runs[r + 2] >> 1;
                    if ((runs[r + 2] & 1) == 0)
                    {
                        for (var k = 0; k < length; ++k) Unowned(global + k, planes.Elements, shown, ref changes, ref changeCount);
                        continue;
                    }
                    owned += length;
                    for (var k = 0; k < length; ++k)
                        Owned(ref views[first + k], global + k, in planes, shown, solid, ref changes, ref changeCount, ref irradiatedCells, ref irradiatedRads, ref irradiatedCount);
                }
                return owned;
            }
            Forget(); ++FullPasses;
            if (threads.MaxDegreeOfParallelism > 1 && views.Length >= ParallelViews && PartViews > 0)
                return ApplyInParts(views, map, in planes, shown, solid, ref changes, ref changeCount, ref irradiatedCells, ref irradiatedRads, ref irradiatedCount);
            var count = 0; int runFirst = 0, runGlobal = 0, runLength = 0; var runOwned = false;
            for (var i = 0; i < views.Length; ++i)
            {
                ref var view = ref views[i];
                var isOwned = (view.Flags & OniNativeColony.ViewOwned) != 0;
                var global = map[i];
                if (runLength != 0 && isOwned == runOwned && global == runGlobal + runLength) ++runLength;
                else
                {
                    if (runLength != 0) AddRun(ref count, runFirst, runGlobal, runLength, runOwned);
                    runFirst = i; runGlobal = global; runLength = 1; runOwned = isOwned;
                }
                if (!isOwned) { Unowned(global, planes.Elements, shown, ref changes, ref changeCount); continue; }
                ++owned;
                Owned(ref view, global, in planes, shown, solid, ref changes, ref changeCount, ref irradiatedCells, ref irradiatedRads, ref irradiatedCount);
            }
            if (runLength != 0) AddRun(ref count, runFirst, runGlobal, runLength, runOwned);
            runCount = count; runViews = views; runMap = map;
            return owned;
        }
        private static void AddRun(ref int count, int first, int global, int length, bool owned)
        {
            if (count + 3 > runs.Length) Array.Resize(ref runs, runs.Length * 2);
            runs[count] = first; runs[count + 1] = global; runs[count + 2] = (length << 1) | (owned ? 1 : 0); count += 3;
        }
        // The full pass in parts: the single loop's body over each stretch of views.
        private static int ApplyInParts(OniNativeColony.CellView[] views, int[] map, in Planes planes, Element[] shown, bool[] solid,
            ref Sim.SubstanceChangeInfo[] changes, ref int changeCount, ref uint[] irradiatedCells, ref float[] irradiatedRads, ref int irradiatedCount)
        {
            var size = PartViews; var needed = (views.Length + size - 1) / size;
            if (parts.Length < needed) { var grown = new Part[needed]; parts.CopyTo(grown, 0); for (var k = parts.Length; k < needed; ++k) grown[k] = new Part(); parts = grown; }
            var work = parts; var p = planes;
            try
            {
                Parallel.For(0, needed, threads, k =>
                {
                    var part = work[k]; part.ChangeCount = part.Irradiated = part.RunCount = part.Owned = 0;
                    int end = Math.Min(views.Length, (k + 1) * size), runFirst = 0, runGlobal = 0, runLength = 0; var runOwned = false;
                    void Run()
                    {
                        if (part.RunCount + 3 > part.Runs.Length) Array.Resize(ref part.Runs, part.Runs.Length * 2);
                        part.Runs[part.RunCount] = runFirst; part.Runs[part.RunCount + 1] = runGlobal; part.Runs[part.RunCount + 2] = (runLength << 1) | (runOwned ? 1 : 0); part.RunCount += 3;
                    }
                    for (var i = k * size; i < end; ++i)
                    {
                        ref var view = ref views[i];
                        var isOwned = (view.Flags & OniNativeColony.ViewOwned) != 0;
                        var global = map[i];
                        if (runLength != 0 && isOwned == runOwned && global == runGlobal + runLength) ++runLength;
                        else
                        {
                            if (runLength != 0) Run();
                            runFirst = i; runGlobal = global; runLength = 1; runOwned = isOwned;
                        }
                        if (!isOwned) { Unowned(global, p.Elements, shown, ref part.Changes, ref part.ChangeCount); continue; }
                        ++part.Owned;
                        Owned(ref view, global, in p, shown, solid, ref part.Changes, ref part.ChangeCount, ref part.Cells, ref part.Rads, ref part.Irradiated);
                    }
                    if (runLength != 0) Run();
                });
            }
            catch (AggregateException error) when (error.InnerException != null)
            { System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(error.InnerException).Throw(); throw; }
            ++ParallelPasses;
            var owned = 0; var count = 0;
            for (var k = 0; k < needed; ++k)
            {
                var part = work[k]; owned += part.Owned;
                if (part.ChangeCount != 0)
                {
                    if (changeCount + part.ChangeCount > changes.Length) Array.Resize(ref changes, Math.Max(changes.Length * 2, changeCount + part.ChangeCount));
                    Array.Copy(part.Changes, 0, changes, changeCount, part.ChangeCount); changeCount += part.ChangeCount;
                }
                if (part.Irradiated != 0)
                {
                    if (irradiatedCount + part.Irradiated > irradiatedCells.Length)
                    { var room = Math.Max(irradiatedCells.Length * 2, irradiatedCount + part.Irradiated); Array.Resize(ref irradiatedCells, room); Array.Resize(ref irradiatedRads, room); }
                    Array.Copy(part.Cells, 0, irradiatedCells, irradiatedCount, part.Irradiated); Array.Copy(part.Rads, 0, irradiatedRads, irradiatedCount, part.Irradiated);
                    irradiatedCount += part.Irradiated;
                }
                var from = 0;
                // A run that crosses the boundary is one run, as the single loop makes it.
                if (count != 0 && part.RunCount != 0 && (runs[count - 1] & 1) == (part.Runs[2] & 1) && part.Runs[1] == runs[count - 2] + (runs[count - 1] >> 1))
                { runs[count - 1] = (((runs[count - 1] >> 1) + (part.Runs[2] >> 1)) << 1) | (runs[count - 1] & 1); from = 3; }
                if (count + part.RunCount - from > runs.Length) Array.Resize(ref runs, Math.Max(runs.Length * 2, count + part.RunCount - from));
                Array.Copy(part.Runs, from, runs, count, part.RunCount - from); count += part.RunCount - from;
            }
            runCount = count; runViews = views; runMap = map;
            return owned;
        }
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void Unowned(int global, ushort* elements, Element[] shown, ref Sim.SubstanceChangeInfo[] changes, ref int changeCount)
        {
            var published = elements[global];
            var cell = shown[global];
            if (cell != null && cell.idx != published) AddChange(ref changes, ref changeCount, global, published);
        }
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void Owned(ref OniNativeColony.CellView view, int global, in Planes p, Element[] shown, bool[] solid,
            ref Sim.SubstanceChangeInfo[] changes, ref int changeCount, ref uint[] irradiatedCells, ref float[] irradiatedRads, ref int irradiatedCount)
        {
            var published = p.Elements[global];
            if (solid[published]) { Unowned(global, p.Elements, shown, ref changes, ref changeCount); return; }
            var mass = view.MassKg;
            if (p.Radiation != null && p.Radiation[global] > 0 && mass > 0)
            {
                if (irradiatedCount == irradiatedCells.Length) { Array.Resize(ref irradiatedCells, irradiatedCount * 2); Array.Resize(ref irradiatedRads, irradiatedCount * 2); }
                irradiatedCells[irradiatedCount] = (uint)global; irradiatedRads[irradiatedCount++] = p.Radiation[global];
            }
            var element = (ushort)view.ElementIndex;
            p.Masses[global] = mass; p.Temperatures[global] = mass > 0 ? view.TemperatureK : 0;
            p.Diseases[global] = OniNativeColony.DiseaseIdx(view.Disease); p.Germs[global] = view.Disease != 0 ? view.DiseaseCount : 0;
            if (published != element || shown[global].idx != element) AddChange(ref changes, ref changeCount, global, element);
            p.Elements[global] = element;
        }
        private static void AddChange(ref Sim.SubstanceChangeInfo[] changes, ref int count, int cell, ushort element)
        {
            if (count == changes.Length) Array.Resize(ref changes, count * 2);
            changes[count++] = new Sim.SubstanceChangeInfo { cellIdx = cell, newElemIdx = element };
        }

        // Element.IsSolid by element index (element states never change in a
        // running game). Null when the table has a missing entry.
        private static List<Element>? solidOf; private static int solidCount = -1; private static bool[]? solidElements;
        internal static bool[]? SolidElements(List<Element> catalogue)
        {
            if (ReferenceEquals(catalogue, solidOf) && catalogue.Count == solidCount) return solidElements;
            bool[]? table = new bool[catalogue.Count];
            for (var k = 0; k < table.Length; ++k)
            {
                if (catalogue[k] == null) { table = null; break; }
                table[k] = catalogue[k].IsSolid;
            }
            solidOf = catalogue; solidCount = catalogue.Count; solidElements = table;
            return table;
        }
    }
}
