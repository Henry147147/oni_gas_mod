using System;
using System.Threading;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Oni
{
    // The gas map's colours are kept while a cell stays near the state they
    // were computed from (user decision 2026-11-24: bands of about 5 % are
    // not noticeable on the map).
    //
    // After every tick the map's three gas textures (colour, opacity, hazard)
    // need the mixture's appearance for every cell in view. Computing it
    // (ColonyCompactReadings.Sample) builds and validates a reading's
    // constituents and blends their colours: about a microsecond a cell, for
    // every visible cell, in the tick's frame. Most cells change by far less
    // than the eye can see from one tick to the next.
    //
    // A cell's appearance is therefore remembered together with the reading
    // it was computed from (its anchor), and used again while the new reading
    // has the same constituents, each gas's mole share within ShareTolerance
    // of the anchor's, each liquid's volume and the cell's occupied and
    // liquid volume within VolumeTolerance of the cell, and the gas pressure
    // and gas mass within AmountTolerance (relative). The first reading
    // outside any of them is computed in full and becomes the new anchor, so
    // the drift never accumulates past one band. The band is 2*tolerance
    // wide: 4 points of a share, 5 % of a pressure.
    //
    // Only the map's textures read these values. The hover card, the status
    // panel, breathing and every sensor read the cell's own reading, to the
    // digit. A hazard tint can lag its threshold by at most the tolerance.
    //
    // Entries live in one table by public cell (a collision only costs a
    // computation), under a gate per entry: the texture jobs of one frame
    // run on several threads. MA_COLONY_TEXTURE_BANDS=0 turns it off, as do
    // the compact texture proof and the validation probes, which compare
    // every written byte with the full computation.
    internal static class ColonyTextureBands
    {
        internal const double ShareTolerance = .02, VolumeTolerance = .02, AmountTolerance = .025;
        private const int Slots = 65521, MaximumConstituents = 12;
        internal static bool Enabled = Environment.GetEnvironmentVariable("MA_COLONY_TEXTURE_BANDS") != "0" &&
            Environment.GetEnvironmentVariable("MA_COLONY_COMPACT_TEXTURE_PROOF") != "1";
        private sealed class Table
        {
            internal readonly int[] Gates = new int[Slots], Counts = new int[Slots];
            internal readonly uint[] Cells = new uint[Slots];              // public cell + 1; 0: empty
            internal readonly long[] Epochs = new long[Slots], Xs = new long[Slots], Ys = new long[Slots];
            internal readonly double[] Pressures = new double[Slots], GasMasses = new double[Slots], Occupied = new double[Slots], Liquid = new double[Slots];
            internal readonly uint[] Species = new uint[Slots * MaximumConstituents];
            internal readonly double[] Shares = new double[Slots * MaximumConstituents]; // a gas's mole share; a liquid's partial volume
            internal readonly MixtureTextureScalars[] Values = new MixtureTextureScalars[Slots];
        }
        private static Table? table;
        private static long reused, anchored;
        internal static (long Reused, long Anchored) Counts => (Interlocked.Read(ref reused), Interlocked.Read(ref anchored));
        internal static void Reset() { Volatile.Write(ref table, null); Interlocked.Exchange(ref reused, 0); Interlocked.Exchange(ref anchored, 0); Volatile.Write(ref current, null); }

        // One number for the presentation model (its options and styles) and
        // the species table an appearance was computed with.
        private sealed class EpochKey
        {
            internal readonly MixturePresentationModel Model; internal readonly string[] Ids; internal readonly long Value;
            internal EpochKey(MixturePresentationModel model, string[] ids, long value) { Model = model; Ids = ids; Value = value; }
        }
        private static EpochKey? current; private static long epochs;
        internal static long Epoch(MixturePresentationModel model, ColonyPresentation.Table species)
        {
            var key = Volatile.Read(ref current);
            if (key != null && ReferenceEquals(key.Model, model) && ReferenceEquals(key.Ids, species.Ids)) return key.Value;
            key = new EpochKey(model, species.Ids, Interlocked.Increment(ref epochs));
            Volatile.Write(ref current, key);
            return key.Value;
        }

        // The cell's remembered appearance, if its reading is still inside the anchor's bands.
        internal static bool TryReuse(ColonyPresentation.Table species, in OniNativeColony.Reading cell, long modelEpoch, long x, long y, out MixtureTextureScalars value)
        {
            value = default;
            var t = Volatile.Read(ref table);
            if (!Enabled || t == null || cell.Constituents == 0 || cell.Constituents > MaximumConstituents) return false;
            var slot = (int)(cell.PublicCell % Slots);
            if (Interlocked.CompareExchange(ref t.Gates[slot], 1, 0) != 0) return false;
            try
            {
                if (t.Cells[slot] != cell.PublicCell + 1 || t.Epochs[slot] != modelEpoch || t.Xs[slot] != x || t.Ys[slot] != y || t.Counts[slot] != cell.Constituents) return false;
                if (!Near(cell.GasPressurePa, t.Pressures[slot]) || Math.Abs(cell.OccupiedVolumeCubicMetres - t.Occupied[slot]) > VolumeTolerance ||
                    Math.Abs(cell.LiquidVolumeCubicMetres - t.Liquid[slot]) > VolumeTolerance) return false;
                var parts = species.Parts; var phases = species.Phases; var molar = species.MolarMasses;
                double gasMoles = 0, gasMass = 0;
                for (var c = 0; c < cell.Constituents; ++c)
                {
                    var part = parts[cell.FirstConstituent + c];
                    if (part.Species != t.Species[slot * MaximumConstituents + c]) return false;
                    if (phases[part.Species] == NativeMatterPhase.Gas) { gasMoles += part.MassKg / molar[part.Species]; gasMass += part.MassKg; }
                }
                if (!Near(gasMass, t.GasMasses[slot])) return false;
                for (var c = 0; c < cell.Constituents; ++c)
                {
                    var part = parts[cell.FirstConstituent + c];
                    var anchor = t.Shares[slot * MaximumConstituents + c];
                    if (phases[part.Species] == NativeMatterPhase.Gas)
                    { if (!(Math.Abs(part.MassKg / molar[part.Species] / gasMoles - anchor) <= ShareTolerance)) return false; }
                    else if (!(Math.Abs(part.PartialVolumeCubicMetres - anchor) <= VolumeTolerance)) return false;
                }
                value = t.Values[slot]; Interlocked.Increment(ref reused);
                return true;
            }
            finally { Volatile.Write(ref t.Gates[slot], 0); }
        }
        // Both zero, or within the relative tolerance of the anchor.
        private static bool Near(double now, double anchor) => now == anchor || anchor > 0 && Math.Abs(now - anchor) <= AmountTolerance * anchor;

        // The appearance just computed from this reading becomes the cell's anchor.
        internal static void Anchor(ColonyPresentation.Table species, in OniNativeColony.Reading cell, long modelEpoch, long x, long y, in MixtureTextureScalars value)
        {
            if (!Enabled || cell.Constituents == 0 || cell.Constituents > MaximumConstituents) return;
            var t = Volatile.Read(ref table);
            if (t == null) { var made = new Table(); t = Interlocked.CompareExchange(ref table, made, null) ?? made; }
            var slot = (int)(cell.PublicCell % Slots);
            if (Interlocked.CompareExchange(ref t.Gates[slot], 1, 0) != 0) return;
            try
            {
                t.Cells[slot] = 0; // not an entry until it is complete
                var parts = species.Parts; var phases = species.Phases; var molar = species.MolarMasses;
                double gasMoles = 0, gasMass = 0;
                for (var c = 0; c < cell.Constituents; ++c)
                {
                    var part = parts[cell.FirstConstituent + c];
                    if (phases[part.Species] == NativeMatterPhase.Gas) { gasMoles += part.MassKg / molar[part.Species]; gasMass += part.MassKg; }
                }
                for (var c = 0; c < cell.Constituents; ++c)
                {
                    var part = parts[cell.FirstConstituent + c];
                    t.Species[slot * MaximumConstituents + c] = part.Species;
                    t.Shares[slot * MaximumConstituents + c] = phases[part.Species] == NativeMatterPhase.Gas ? part.MassKg / molar[part.Species] / gasMoles : part.PartialVolumeCubicMetres;
                }
                t.Counts[slot] = (int)cell.Constituents; t.Epochs[slot] = modelEpoch; t.Xs[slot] = x; t.Ys[slot] = y;
                t.Pressures[slot] = cell.GasPressurePa; t.GasMasses[slot] = gasMass; t.Occupied[slot] = cell.OccupiedVolumeCubicMetres; t.Liquid[slot] = cell.LiquidVolumeCubicMetres;
                t.Values[slot] = value; t.Cells[slot] = cell.PublicCell + 1; Interlocked.Increment(ref anchored);
            }
            finally { Volatile.Write(ref t.Gates[slot], 0); }
        }
    }
}
