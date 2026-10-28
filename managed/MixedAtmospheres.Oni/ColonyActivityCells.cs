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
    internal sealed class ColonyActivityCells
    {
        private readonly Dictionary<long, uint[]> previous = new Dictionary<long, uint[]>();
        private readonly HashSet<long> seen = new HashSet<long>();
        private readonly HashSet<uint> actors = new HashSet<uint>(), touched = new HashSet<uint>(), retired = new HashSet<uint>();
        private readonly List<long> missing = new List<long>();
        private int width, height;
        internal int ActorCount => seen.Count;
        internal void Begin(int width, int height)
        {
            if (width <= 0 || height <= 0 || (long)width * height > int.MaxValue) throw new ArgumentOutOfRangeException(nameof(width));
            if (this.width != width || this.height != height) { previous.Clear(); retired.Clear(); }
            this.width = width; this.height = height;
            actors.Clear(); touched.Clear(); seen.Clear(); missing.Clear();
            foreach (var cell in retired) touched.Add(cell); retired.Clear();
        }
        internal void Observe(long identity, ReadOnlySpan<uint> cells)
        {
            for (var i = 0; i < cells.Length; ++i)
                if ((long)cells[i] >= (long)width * height || i != 0 && cells[i] <= cells[i - 1])
                    throw new ArgumentException("Actor cells must be sorted, unique and inside the grid.", nameof(cells));
            if (!seen.Add(identity)) throw new InvalidOperationException("Actor identity appeared twice in one census.");
            foreach (var cell in cells) actors.Add(cell);
            if (previous.TryGetValue(identity, out var before))
            {
                if (cells.SequenceEqual(before)) return;
                foreach (var cell in before) touched.Add(cell);
                foreach (var cell in cells) touched.Add(cell);
            }
            previous[identity] = cells.ToArray();
        }
        internal void Retire(long identity)
        {
            if (!previous.TryGetValue(identity, out var before)) return;
            foreach (var cell in before) retired.Add(cell);
            previous.Remove(identity);
        }
        internal ColonyActivitySnapshot Finish()
        {
            foreach (var pair in previous)
                if (!seen.Contains(pair.Key)) { foreach (var cell in pair.Value) touched.Add(cell); missing.Add(pair.Key); }
            foreach (var identity in missing) previous.Remove(identity);
            var a = new uint[actors.Count]; actors.CopyTo(a); Array.Sort(a);
            var t = new uint[touched.Count]; touched.CopyTo(t); Array.Sort(t);
            return new ColonyActivitySnapshot(a, t);
        }
        internal void Reset() { previous.Clear(); seen.Clear(); actors.Clear(); touched.Clear(); retired.Clear(); missing.Clear(); width = height = 0; }
    }
}
