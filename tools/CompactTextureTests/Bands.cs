using MixedAtmospheres.Core;using MixedAtmospheres.Core.Interop;using MixedAtmospheres.Oni;
// The map keeps a cell's appearance while its reading stays inside the bands
// of the reading it was computed from (ColonyTextureBands, user decision
// 2026-11-24), and computes it again from the first reading outside them.
static partial class Proof {
    static void TextureBands(MixturePresentationModel[] models, FluidMaterialCatalog catalog, string[] ids, NativeMatterPhase[] phases, double[] mm)
    {
        var saved = ColonyTextureBands.Enabled; ColonyTextureBands.Enabled = true; ColonyTextureBands.Reset();
        try
        {
            // Oxygen with a share of carbon dioxide (and optionally chlorine) over a film of water, at a pressure.
            (ColonyPresentation.Table Table, OniNativeColony.Reading Cell) Make(uint cell, double co2Share, double pressure, double water, double chlorineShare = -1)
            {
                var liquid = water / 1000; var gasVolume = 1 - liquid; var moles = pressure * gasVolume / (FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300);
                var parts = new List<OniNativeColony.Constituent>();
                void Gas(uint species, double share) => parts.Add(new OniNativeColony.Constituent { Species = species, MassKg = share * moles * mm[species], PartialVolumeCubicMetres = gasVolume * share });
                var chlorine = Math.Max(0, chlorineShare);
                Gas(0, 1 - co2Share - chlorine); Gas(2, co2Share); if (chlorineShare >= 0) Gas(4, chlorine);
                if (water > 0) parts.Add(new OniNativeColony.Constituent { Species = 5, MassKg = water, PartialVolumeCubicMetres = liquid });
                var reading = new OniNativeColony.Reading { PublicCell = cell, Constituents = (uint)parts.Count, TemperatureKelvin = 300, GasPressurePa = pressure, MechanicalPressurePa = pressure,
                    OccupiedVolumeCubicMetres = 1, LiquidVolumeCubicMetres = liquid, InternalEnergyJoules = 42 };
                return (new ColonyPresentation.Table(catalog, ids, phases, mm, new[] { reading }, parts.ToArray()), reading);
            }
            static bool Same(MixtureTextureScalars a, MixtureTextureScalars b) => BitConverter.DoubleToInt64Bits(a.Color.R) == BitConverter.DoubleToInt64Bits(b.Color.R) &&
                BitConverter.DoubleToInt64Bits(a.TotalPressurePa) == BitConverter.DoubleToInt64Bits(b.TotalPressurePa) && BitConverter.DoubleToInt64Bits(a.GasMassKg) == BitConverter.DoubleToInt64Bits(b.GasMassKg);
            var model = models[0]; var options = new MixtureWorldRendering.OpacityParameters(15, 200, .1);
            long x = BitConverter.DoubleToInt64Bits(3.0), y = BitConverter.DoubleToInt64Bits(5.0);
            var first = Make(1000, .20, 101325, .05); var epoch = ColonyTextureBands.Epoch(model, first.Table);
            Check(!ColonyTextureBands.TryReuse(first.Table, in first.Cell, epoch, x, y, out _), "a cell without an anchor has nothing to keep");
            var anchor = ColonyCompactReadings.Sample(first.Table, first.Cell, 7, model, 3, 5);
            ColonyTextureBands.Anchor(first.Table, in first.Cell, epoch, x, y, in anchor);
            Check(ColonyTextureBands.TryReuse(first.Table, in first.Cell, epoch, x, y, out var kept) && Same(kept, anchor), "the anchor's own reading keeps its appearance");
            // Inside every band: the anchor's appearance, not the new reading's.
            var inside = Make(1000, .20 + .015, 101325 * 1.02, .05 + 5);
            var fresh = ColonyCompactReadings.Sample(inside.Table, inside.Cell, 7, model, 3, 5);
            Check(ColonyTextureBands.Epoch(model, inside.Table) == epoch, "the same model and species table are one epoch");
            Check(ColonyTextureBands.TryReuse(inside.Table, in inside.Cell, epoch, x, y, out kept) && Same(kept, anchor) && !Same(kept, fresh), "a reading inside the bands keeps the anchor's appearance");
            var shown = MixtureWorldRendering.Sample(kept, options); var exact = MixtureWorldRendering.Sample(fresh, options);
            int Off(byte a, byte b) => Math.Abs(a - b);
            Check(Off(shown.R, exact.R) <= 6 && Off(shown.G, exact.G) <= 6 && Off(shown.B, exact.B) <= 6 && Off(shown.Opacity, exact.Opacity) <= 6 && shown.Hazard == exact.Hazard,
                $"a kept appearance is within six levels of 255 of the exact one: {shown.R},{shown.G},{shown.B},{shown.Opacity} against {exact.R},{exact.G},{exact.B},{exact.Opacity}");
            // Outside any one band, another place, model or table: computed again.
            foreach (var (name, other) in new[] { ("share", Make(1000, .20 + .03, 101325, .05)), ("pressure", Make(1000, .20, 101325 * 1.04, .05)), ("lower pressure", Make(1000, .20, 101325 * .96, .05)),
                ("liquid volume", Make(1000, .20, 101325, .05 + 30)), ("a new constituent", Make(1000, .20, 101325, .05, 0)), ("no liquid", Make(1000, .20, 101325, 0)) })
                Check(!ColonyTextureBands.TryReuse(other.Table, in other.Cell, ColonyTextureBands.Epoch(model, first.Table), x, y, out _), "a reading outside the band of its " + name + " is computed again");
            Check(!ColonyTextureBands.TryReuse(first.Table, in first.Cell, epoch, BitConverter.DoubleToInt64Bits(4.0), y, out _), "another pattern position is another appearance");
            var elsewhere = Make(1000 + 65521, .20, 101325, .05);
            Check(!ColonyTextureBands.TryReuse(elsewhere.Table, in elsewhere.Cell, epoch, x, y, out _), "a colliding cell does not take another cell's appearance");
            var otherEpoch = ColonyTextureBands.Epoch(models[1], first.Table);
            Check(otherEpoch != epoch && !ColonyTextureBands.TryReuse(first.Table, in first.Cell, otherEpoch, x, y, out _), "another presentation model is another epoch");
            epoch = ColonyTextureBands.Epoch(model, first.Table);
            ColonyTextureBands.Anchor(first.Table, in first.Cell, epoch, x, y, in anchor);
            // A slow drift never shows a share more than one tolerance from the truth: the first
            // reading outside the band becomes the new anchor.
            var anchoredShare = .20; var recomputed = 0; var worst = 0.0;
            for (var step = 1; step <= 400; ++step)
            {
                var share = .20 + step * .001; var now = Make(1000, share, 101325, .05);
                if (!ColonyTextureBands.TryReuse(now.Table, in now.Cell, epoch, x, y, out _))
                {
                    var value = ColonyCompactReadings.Sample(now.Table, now.Cell, 7, model, 3, 5);
                    ColonyTextureBands.Anchor(now.Table, in now.Cell, epoch, x, y, in value); anchoredShare = share; ++recomputed;
                }
                worst = Math.Max(worst, Math.Abs(share - anchoredShare));
            }
            Check(worst <= ColonyTextureBands.ShareTolerance + 1e-12 && recomputed >= 18 && recomputed <= 20, $"a drift of 40 points is recomputed about every two points: {recomputed} times, {worst:R} at most off");
            // Cells with more constituents than an entry holds are always computed.
            var counts = ColonyTextureBands.Counts;
            Check(counts.Reused > 0 && counts.Anchored > 0, "kept and computed cells are counted");
            // Off: nothing is kept.
            ColonyTextureBands.Enabled = false;
            Check(!ColonyTextureBands.TryReuse(first.Table, in first.Cell, epoch, x, y, out _), "the switch turns the bands off");
        }
        finally { ColonyTextureBands.Enabled = saved; ColonyTextureBands.Reset(); }
    }
}
