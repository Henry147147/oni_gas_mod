using System;
using System.Collections.Generic;

namespace MixedAtmospheres.Oni
{
    // Pure cell geometry. Off-grid radii are clipped in x/y, never wrapped into
    // another row. The native owner decides which world/region owns a cell.
    internal sealed class ColonyActivityFootprint
    {
        private readonly HashSet<uint> cells = new HashSet<uint>();
        private int width, height, left, right, bottom, top;
        internal void Begin(int width, int height)
        {
            if (width <= 0 || height <= 0 || (long)width * height > int.MaxValue) throw new ArgumentOutOfRangeException(nameof(width));
            this.width = width; this.height = height; left = bottom = 0; right = width; top = height; cells.Clear();
        }
        internal void Clip(int x, int y, int width, int height)
        {
            if(x<0 || y<0 || width<=0 || height<=0 || (long)x+width>this.width || (long)y+height>this.height)
                throw new ArgumentOutOfRangeException(nameof(width));
            left=x;right=x+width;bottom=y;top=y+height;
        }
        internal void Add(int cell)
        {
            if(cell<0 || (long)cell>=(long)width*height)return;
            var x=cell%width;var y=cell/width;
            if(x>=left && x<right && y>=bottom && y<top){cells.Add((uint)cell);return;}
            // An actor immediately outside a diagnostic owner's rectangle
            // can still touch its boundary fluid. Retain that contact inside
            // the rectangle accepted by the native census API.
            if(y>=bottom && y<top){if(x==left-1)cells.Add((uint)(cell+1));if(x==right)cells.Add((uint)(cell-1));}
            if(x>=left && x<right){if(y==bottom-1)cells.Add((uint)(cell+width));if(y==top)cells.Add((uint)(cell-width));}
        }
        internal void Offset(int anchor, int x, int y, int orientation = 0)
        {
            if (anchor < 0 || (long)anchor >= (long)width * height) return;
            (x, y) = orientation switch {
                0 => (x, y), 1 => (y, -x), 2 => (-x, -y), 3 => (-y, x),
                5 => (-x, y), 6 => (x, -y), _ => throw new InvalidOperationException("Unknown actor footprint orientation.") };
            var px = (long)(anchor % width) + x; var py = (long)(anchor / width) + y;
            if (px >= 0 && px < width && py >= 0 && py < height) Add((int)(px + py * width));
        }
        internal void Radius(int anchor, int radius)
        {
            if (radius < 0 || radius > 255) throw new ArgumentOutOfRangeException(nameof(radius));
            if (anchor < 0 || (long)anchor >= (long)width * height) return;
            var ax = anchor % width; var ay = anchor / width;
            for (var y = Math.Max(bottom, ay - radius); y <= Math.Min(top - 1, ay + radius); ++y)
                for (var x = Math.Max(left, ax - radius); x <= Math.Min(right - 1, ax + radius); ++x) cells.Add((uint)(x + y * width));
        }
        internal uint[] Finish(uint[] previous)
        {
            if (previous.Length == cells.Count)
            {
                var equal = true; foreach (var cell in previous) if (!cells.Contains(cell)) { equal = false; break; }
                if (equal) return previous;
            }
            var result = new uint[cells.Count]; cells.CopyTo(result); Array.Sort(result); return result;
        }
    }

    internal sealed class ColonyActivitySnapshot
    {
        private readonly uint[] actors, touched;
        internal ReadOnlySpan<uint> Actors => actors;
        internal ReadOnlySpan<uint> Touched => touched;
        internal ColonyActivitySnapshot(uint[] actors, uint[] touched) { this.actors = actors; this.touched = touched; }
    }

    internal static class ColonyActivityMask
    {
        // The core keeps actor presence on fluid cells. A solid footprint
        // activates each cardinal fluid contact instead of the solid itself.
        internal static SortedSet<uint> Project(ReadOnlySpan<uint> cells, int width, int height, Func<uint, bool> fluid)
        {
            var result = new SortedSet<uint>();
            foreach (var cell in cells)
            {
                if ((long)cell >= (long)width * height) throw new ArgumentOutOfRangeException(nameof(cells));
                if (fluid(cell)) { result.Add(cell); continue; }
                var x = cell % width; var y = cell / width;
                if (x > 0) Add(cell - 1); if (x + 1 < width) Add(cell + 1);
                if (y > 0) Add(cell - (uint)width); if (y + 1 < height) Add(cell + (uint)width);
            }
            return result;
            void Add(uint cell) { if (fluid(cell)) result.Add(cell); }
        }
    }

    // Full actor presence plus one-frame motion/retirement touches. First-time
    // seeding is presence only: loading an unchanged actor is not a new input.
    //
    // A complete census observes every actor: one it does not observe has
    // left. A partial census observes only the actors that may have changed;
    // the others keep the cells of their last observation. Presence is kept as
    // a count per cell, so either kind costs what it observes, and both give
    // the same snapshot for the same actor cells.
    internal sealed class ColonyActivityCells
    {
        private readonly Dictionary<long, uint[]> previous = new Dictionary<long, uint[]>();
        private readonly HashSet<long> seen = new HashSet<long>();
        private readonly HashSet<uint> touched = new HashSet<uint>(), retired = new HashSet<uint>();
        private readonly List<long> missing = new List<long>();
        // Cells whose count reached or left zero since the last snapshot.
        private readonly List<uint> crossed = new List<uint>();
        private readonly List<uint> changed = new List<uint>();
        private int[] presence = Array.Empty<int>();
        private int width, height;
        private bool partial;
        internal int ActorCount => previous.Count;
        internal void Begin(int width, int height, bool partial = false)
        {
            if (width <= 0 || height <= 0 || (long)width * height > int.MaxValue) throw new ArgumentOutOfRangeException(nameof(width));
            if (this.width != width || this.height != height)
            {
                if (partial) throw new InvalidOperationException("A partial actor census needs the grid of the last complete one.");
                // Nobody is present on the new grid until observed there.
                previous.Clear(); retired.Clear(); crossed.AddRange(lastActors);
                if (presence.Length == width * height) Array.Clear(presence, 0, presence.Length); else presence = new int[width * height];
            }
            this.width = width; this.height = height; this.partial = partial;
            touched.Clear(); seen.Clear(); missing.Clear();
            foreach (var cell in retired) touched.Add(cell); retired.Clear();
        }
        private void Enter(uint cell) { if (presence[cell]++ == 0) crossed.Add(cell); }
        private void Leave(uint cell) { if (--presence[cell] == 0) crossed.Add(cell); }
        internal void Observe(long identity, ReadOnlySpan<uint> cells)
        {
            for (var i = 0; i < cells.Length; ++i)
                if ((long)cells[i] >= (long)width * height || i != 0 && cells[i] <= cells[i - 1])
                    throw new ArgumentException("Actor cells must be sorted, unique and inside the grid.", nameof(cells));
            if (!seen.Add(identity)) throw new InvalidOperationException("Actor identity appeared twice in one census.");
            if (previous.TryGetValue(identity, out var before))
            {
                if (cells.SequenceEqual(before)) return;
                foreach (var cell in before) { touched.Add(cell); Leave(cell); }
                foreach (var cell in cells) { touched.Add(cell); Enter(cell); }
            }
            else foreach (var cell in cells) Enter(cell);
            previous[identity] = cells.ToArray();
        }
        // An actor that left between two censuses: its cells are touched in the next one.
        internal void Retire(long identity)
        {
            if (!previous.TryGetValue(identity, out var before)) return;
            foreach (var cell in before) { retired.Add(cell); Leave(cell); }
            previous.Remove(identity);
        }
        // An actor found gone during this census: its cells are touched in this one.
        internal void Drop(long identity)
        {
            if (!previous.TryGetValue(identity, out var before)) return;
            foreach (var cell in before) { touched.Add(cell); Leave(cell); }
            previous.Remove(identity);
        }
        // Snapshot arrays are never written after Finish: presence equal to the
        // last census shares its array, and no touch shares the empty one.
        private uint[] lastActors = Array.Empty<uint>();
        // `always`: the identities a partial census observes every time, so
        // one of them it did not observe has left (a complete census holds
        // that for every identity).
        internal ColonyActivitySnapshot Finish(Func<long, bool>? always = null)
        {
            foreach (var pair in previous)
                if ((!partial || always != null && always(pair.Key)) && !seen.Contains(pair.Key)) missing.Add(pair.Key);
            foreach (var identity in missing) Drop(identity);
            missing.Clear();
            var a = Actors(); lastActors = a;
            uint[] t;
            if (touched.Count == 0) t = Array.Empty<uint>();
            else { t = new uint[touched.Count]; touched.CopyTo(t); Array.Sort(t); }
            return new ColonyActivitySnapshot(a, t);
        }
        // The cells with an actor, sorted: the last snapshot's with the cells
        // that entered or left since merged in.
        private uint[] Actors()
        {
            if (crossed.Count == 0) return lastActors;
            crossed.Sort(); changed.Clear();
            var added = 0;
            for (var i = 0; i < crossed.Count; ++i)
            {
                var cell = crossed[i];
                if (i != 0 && cell == crossed[i - 1]) continue;
                var now = cell < (uint)presence.Length && presence[cell] > 0;
                if (now == Array.BinarySearch(lastActors, cell) >= 0) continue;
                changed.Add(cell); if (now) ++added;
            }
            crossed.Clear();
            if (changed.Count == 0) return lastActors;
            var result = new uint[lastActors.Length + added - (changed.Count - added)];
            int from = 0, change = 0, to = 0;
            while (from < lastActors.Length || change < changed.Count)
            {
                if (change == changed.Count || from < lastActors.Length && lastActors[from] < changed[change]) result[to++] = lastActors[from++];
                else if (from < lastActors.Length && lastActors[from] == changed[change]) { ++from; ++change; }
                else result[to++] = changed[change++];
            }
            return result;
        }
        internal void Reset()
        {
            previous.Clear(); seen.Clear(); touched.Clear(); retired.Clear(); missing.Clear(); crossed.Clear(); changed.Clear();
            presence = Array.Empty<int>(); width = height = 0; partial = false; lastActors = Array.Empty<uint>();
        }
    }
}
