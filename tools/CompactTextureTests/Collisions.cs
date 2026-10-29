using MixedAtmospheres.Core;using MixedAtmospheres.Oni;
static partial class Proof {
    static void CollisionAdmission(MixturePresentationModel[] models, List<(ColonyPresentation.Table Table, OniNativeColony.Reading Cell)> cases)
    {
        var pair = BufferOf(cases, 4094); var b = pair.Buffer;
        var readings = new ColonyLazyReadings(b, pair.Table, 7, (_, _) => { });
        var full = readings.Describe(models[0]); var compact = (ICompactTextureSource)full;
        var fit = compact.PrepareTextureScalars(4093, 0, 0, 0, 1, out var visible);
        Check(visible == 2 && fit, "two visible colliding readings fit the bounded cache");
        var options = new MixtureWorldRendering.OpacityParameters(15, 200, .1);
        foreach (var index in new[] { 0, 4093, 0, 4093 })
        {
            Check(compact.TrySampleTexture(index, 0, index / 4093, out var scalar), "collision sample exists");
            var rich = MixtureWorldRendering.Sample(full[index], 0, index / 4093, options);
            var actual = MixtureWorldRendering.Sample(scalar, options);
            Check(rich.R == actual.R && rich.G == actual.G && rich.B == actual.B && rich.Alpha == actual.Alpha && rich.Opacity == actual.Opacity && rich.Hazard == actual.Hazard, "collision path matches rich renderer bytes");
        }
        Parallel.For(0, 8, worker =>
        {
            var sources = models.Select(m => (ICompactTextureSource)readings.Describe(m)).ToArray();
            static bool Same(double a, double b) => BitConverter.DoubleToInt64Bits(a) == BitConverter.DoubleToInt64Bits(b);
            for (int i = 0; i < 1000; ++i)
            {
                var index = (i & 1) == 0 ? 0 : 4093;
                var which = (worker + i) % models.Length; var m = models[which]; var source = sources[which];
                var x = (i % 5) * .375 - .25; var y = (worker % 3) * .625 - .5;
                if (!source.TrySampleTexture(index, x, y, out var scalar)) throw new Exception("parallel collision sample missing");
                var expected = m.Describe(ColonyPresentation.Create(pair.Table, b.Readings[index], 7));
                var color = expected.WorldFluidColorAt(x, y); var rich = MixtureWorldRendering.Sample(expected, x, y, options); var actual = MixtureWorldRendering.Sample(scalar, options);
                if (!Same(color.R, scalar.Color.R) || !Same(color.G, scalar.Color.G) || !Same(color.B, scalar.Color.B) ||
                    !Same(expected.TotalPressurePa, scalar.TotalPressurePa) || !Same(expected.GasMassKg, scalar.GasMassKg) ||
                    !Same(expected.GasVolumeCubicMetres, scalar.GasVolumeCubicMetres) || !Same(expected.GasDensityKgPerCubicMetre, scalar.GasDensityKgPerCubicMetre) ||
                    !Same(expected.ThermodynamicReading!.LiquidVolumeCubicMetres, scalar.LiquidVolumeCubicMetres) ||
                    !Same(expected.ThermodynamicReading!.VolumeCubicMetres, scalar.VolumeCubicMetres) ||
                    !Same(expected.OxygenMoleFraction, scalar.OxygenMoleFraction) || !Same(expected.Co2MoleFraction, scalar.Co2MoleFraction) ||
                    !Same(expected.OxygenPartialPressurePa, scalar.OxygenPartialPressurePa) || !Same(expected.Co2PartialPressurePa, scalar.Co2PartialPressurePa) ||
                    expected.Failures != scalar.Failures || expected.IsBreathable != scalar.IsBreathable || expected.HasPhaseVolumeAppearance != scalar.HasPhaseVolumeAppearance ||
                    rich.R != actual.R || rich.G != actual.G || rich.B != actual.B || rich.Alpha != actual.Alpha || rich.Opacity != actual.Opacity || rich.Hazard != actual.Hazard || !Same(rich.PhysicalPressurePa, actual.PhysicalPressurePa))
                    throw new Exception("parallel collision changed rich scalars or renderer bytes");
            }
        });
        Check(true, "concurrent colliding slots preserve model keys and rich bits");
    }
}
