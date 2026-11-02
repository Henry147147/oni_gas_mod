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

        internal static void Reset() { Fields = 0; Cells = 0; sentField = false; }

        internal static void Configure()
        {
            anyKiller = false;
            var table = Db.Get().Diseases;
            for (var i = 0; i < table.Count; ++i) anyKiller |= table[i].radiationKillRate > 0;
        }

        internal static bool Enabled => ColonyWorldGerms.Supported && anyKiller && Sim.IsRadiationEnabled();

        // The irradiated owned cells (public cell, rads) of one published frame.
        internal static void Apply(List<(uint cell, float rads)> cells)
        {
            if (!Enabled) return;
            if (cells.Count == 0 && !sentField) return;
            var ids = new uint[cells.Count]; var rads = new float[cells.Count];
            for (var i = 0; i < ids.Length; ++i) { ids[i] = cells[i].cell; rads[i] = cells[i].rads; }
            OniNativeColony.Check(ColonyWorldGerms.SetRadiation(ids, rads, (uint)ids.Length));
            sentField = ids.Length != 0; ++Fields; Cells += ids.Length;
        }
    }
}
