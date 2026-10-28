using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using MixedAtmospheres.Oni;
static partial class Proof
{
    static void LargeCases()
    {
        foreach (var count in new[] { 2, 5, 16, 17, 24, 25, 76, 79, 128, 256, 257 })
        {
            var ids = Enumerable.Range(0, count).Select(i => i == 0 ? "oxygen" : i == 1 ? "carbon_dioxide" : "species:" + i.ToString("D3")).ToArray();
            var phases = Enumerable.Range(0, count).Select(i => i % 3 == 2 ? NativeMatterPhase.Liquid : NativeMatterPhase.Gas).ToArray();
            var catalogM = Enumerable.Range(0, count).Select(i => .02 + i * .0001).ToArray();
            var materials = ids.Select((id, i) => new FluidMaterial(id, phases[i], catalogM[i], 700, phases[i] == NativeMatterPhase.Gas ? 0 : 1000, 0, 1e6)).ToArray();
            var catalog = new FluidMaterialCatalog("large", materials);
            var styles = ids.Select((id, i) => new MixtureSpeciesStyle(id, id, new MixtureSrgbColor(.1 + .8 * (i % 7) / 7, .2 + .7 * (i % 11) / 11, .3 + .6 * (i % 13) / 13))).ToArray();
            var model = new MixturePresentationModel(catalog, styles, new AtmosphereSettings(), ids[0], ids[1]);
            // The native reading's molar masses intentionally differ from legacy style
            // catalogue metadata; exact scalar fractions must use the native values.
            var nativeM = catalogM.Select((m, i) => i % 2 == 0 ? m * 1.002 : m).ToArray();
            for (int permutation = 0; permutation < 4; ++permutation)
            {
                var rng = new Random(11 + permutation);
                var order = Enumerable.Range(0, count).OrderBy(_ => rng.Next()).ToArray();
                var parts = order.Select(k => new OniNativeColony.Constituent { Species = (uint)k, MassKg = .0001 * (k + 1), PartialVolumeCubicMetres = phases[k] == NativeMatterPhase.Liquid ? .0001 * (k + 1) / 1000 : 0 }).ToArray();
                double gasMoles = 0, liquid = 0;
                foreach (var p in parts)
                {
                    if (phases[p.Species] == NativeMatterPhase.Gas)
                        gasMoles += p.MassKg / nativeM[p.Species];
                    else
                        liquid += p.PartialVolumeCubicMetres;
                }
                var gasVolume = 1 - liquid;
                var pressure = gasMoles * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300 / gasVolume;
                for (int i = 0; i < parts.Length; ++i)
                    if (phases[parts[i].Species] == NativeMatterPhase.Gas)
                        parts[i].PartialVolumeCubicMetres = gasVolume * ((parts[i].MassKg / nativeM[parts[i].Species]) / gasMoles);
                var cell = new OniNativeColony.Reading { PublicCell = 0, Constituents = (uint)count, TemperatureKelvin = 300, GasPressurePa = pressure, MechanicalPressurePa = pressure, OccupiedVolumeCubicMetres = 1, LiquidVolumeCubicMetres = liquid, InternalEnergyJoules = 1 };
                var table = new ColonyPresentation.Table(catalog, ids, phases, nativeM, new[] { cell }, parts);
                var full = model.Describe(ColonyPresentation.Create(table, cell, 77));
                if (count <= ColonyCompactReadings.MaximumCompactConstituents)
                {
                    foreach (var pos in new[] { (-5.5, .125), (4.0, 19.75) })
                    {
                        var scalar = ColonyCompactReadings.Sample(table, cell, 77, model, pos.Item1, pos.Item2);
                        var color = full.WorldFluidColorAt(pos.Item1, pos.Item2);
                        Exact(color.R, scalar.Color.R, "large colorR");
                        Exact(color.G, scalar.Color.G, "large colorG");
                        Exact(color.B, scalar.Color.B, "large colorB");
                        Exact(full.GasDensityKgPerCubicMetre, scalar.GasDensityKgPerCubicMetre, "large density");
                        Check(full.Failures == scalar.Failures, "large diagnostics");
                    }
                }
                var buffer = new ColonyReadingBuffer { Readings = new[] { cell }, Parts = parts, Count = 1, PartCount = count, Frame = 77, Lease = 3, Sorted = true, CompactCache = new ColonyCompactReadings.Cache(1, 3) };
                var source = (ICompactTextureSource)new ColonyLazyReadings(buffer, table, 77, (_, _) => { }).Describe(model);
                Check(source.PrepareTextureScalars(1, 0, 0, 0, 0, out _) == (count <= 256), "large capacity fallback");
                Check(source.TrySampleTexture(0, 0, 0, out _) == (count <= 256), "large direct source fallback");
            }
        }
        Console.WriteLine("PASS large mixed/permuted native readings2..257 constituents, 76/79catalogues, native molar mass authority, bounded unsupported fallback");
    }
}
