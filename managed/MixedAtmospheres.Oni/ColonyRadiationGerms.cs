using System;
using System.Collections.Generic;

namespace MixedAtmospheres.Oni
{
    // Radiation kills the germs of owned cells (docs/colony-owner.md#germs).
    // ONI's native simulation takes radiation x the disease's kill rate germs
    // from every cell each 0.2 s tick; the owner's world evolution does the
    // same inside its own tick from the radiation field of the latest
    // published frame, which this class hands over (the game computes the field
    // itself: Grid.Radiation). The field is an environment sample and is not
    // saved.
    internal static class ColonyRadiationGerms
    {
        private static bool anyKiller;
        private static bool sentField;
        internal static long Fields { get; private set; }
        internal static long Cells { get; private set; }
        // The field handed over last: the owner keeps it until the next one, and
        // building its lookup there costs about as much as a copy of the views
        // (1.5 ms a tick on a large Spaced Out! colony), so a publication whose
        // field equals it hands nothing over.
        private static uint[] lastCells = Array.Empty<uint>(); private static float[] lastRads = Array.Empty<float>(); private static int lastCount = -1;
        internal static long Unchanged { get; private set; }

        internal static void Reset() { Fields = 0; Cells = 0; Unchanged = 0; sentField = false; lastCount = -1; }

        // At every owner start and restore: a new owner holds no field.
        internal static void Configure()
        {
            Reset();
            anyKiller = false;
            var table = Db.Get().Diseases;
            for (var i = 0; i < table.Count; ++i) anyKiller |= table[i].radiationKillRate > 0;
        }

        internal static bool Enabled => ColonyWorldGerms.Supported && anyKiller && Sim.IsRadiationEnabled();

        // The irradiated owned cells (public cell, rads) of one published frame.
        // The first count entries of cells/rads; the native owner copies them.
        internal static void Apply(uint[] cells, float[] rads, int count)
        {
            if (!Enabled) return;
            if (count == 0 && !sentField) return;
            if (count < 0 || count > cells.Length || count > rads.Length) throw new ArgumentOutOfRangeException(nameof(count));
            if (count == lastCount && cells.AsSpan(0, count).SequenceEqual(lastCells.AsSpan(0, count)) &&
                System.Runtime.InteropServices.MemoryMarshal.AsBytes(rads.AsSpan(0, count)).SequenceEqual(System.Runtime.InteropServices.MemoryMarshal.AsBytes(lastRads.AsSpan(0, count))))
            { ++Unchanged; return; }
            lastCount = -1; // not the owner's field until the call has returned
            OniNativeColony.Check(ColonyWorldGerms.SetRadiation(cells, rads, (uint)count));
            if (lastCells.Length < count) { lastCells = new uint[cells.Length]; lastRads = new float[rads.Length]; }
            Array.Copy(cells, lastCells, count); Array.Copy(rads, lastRads, count); lastCount = count;
            sentField = count != 0; ++Fields; Cells += count;
        }
    }
}
