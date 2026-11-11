using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

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

        internal static void Forget() { runCount = -1; runViews = null; runMap = null; }
        internal static void Reset() { Forget(); solidOf = null; solidElements = null; solidCount = -1; RunPasses = FullPasses = 0; }

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
