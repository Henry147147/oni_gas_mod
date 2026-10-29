using System.Diagnostics;
using System.Reflection;
using System.Runtime.Loader;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using MixedAtmospheres.Oni;
var gameDirectory = args.FirstOrDefault(value => !value.StartsWith("--", StringComparison.Ordinal))
    ?? Environment.GetEnvironmentVariable("ONI_GAME_DIR")
    ?? throw new ArgumentException("Usage: CompactTextureTests <licensed ONI root> [--benchmark]");
var managedDirectory = Path.Combine(Path.GetFullPath(gameDirectory), "OxygenNotIncluded_Data", "Managed");
if (!File.Exists(Path.Combine(managedDirectory, "Assembly-CSharp.dll")))
    throw new ArgumentException("The supplied ONI root has no inspected managed assemblies.");
AssemblyLoadContext.Default.Resolving += (context, name) =>
{
    var candidate = Path.Combine(managedDirectory, name.Name + ".dll");
    return File.Exists(candidate) ? context.LoadFromAssemblyPath(candidate) : null;
};
return Proof.Run(args);
static partial class Proof
{
    static int checks; static double sink;
    static void Check(bool ok, string message)
    {
        ++checks;
        if (!ok)
            throw new Exception(message);
    }
    static void Exact(double a, double b, string label)
    {
        ++checks;
        if (BitConverter.DoubleToInt64Bits(a) != BitConverter.DoubleToInt64Bits(b))
            throw new Exception(label + ": " + a.ToString("R") + " != " + b.ToString("R"));
    }
    static byte Byte(double x) => (byte)Math.Round(Math.Max(0, Math.Min(1, x)) * 255, MidpointRounding.AwayFromZero);
    static byte Opacity(double mass, double volume, double density, double liquid, bool mixed, double minimum, double maximum, double visibility)
    {
        var f = mass <= 0 || volume <= 0 ? 0 : Math.Max(Math.Max(0, Math.Min(1, (density * 101.3 - minimum) / (maximum - minimum))), visibility);
        return Byte(mixed ? f * volume + liquid : f);
    }
    internal static int Run(string[] args)
    {
        var ids = new[] { "oxygen", "nitrogen", "carbon_dioxide", "hydrogen", "chlorine", "water", "oil" };
        var mm = new[] { .0319988, .0280134, .0440095, .002016, .0709, .01801528, .1 };
        var phases = ids.Select((x, i) => i < 5 ? NativeMatterPhase.Gas : NativeMatterPhase.Liquid).ToArray();
        var materials = ids.Select((x, i) => new FluidMaterial(x, phases[i], mm[i], 700, i < 5 ? 0 : 1000, 0, 1e6)).ToArray();
        var catalog = new FluidMaterialCatalog("texture-proof", materials);
        var styles = ids.Select((x, i) => new MixtureSpeciesStyle(x, x, new MixtureSrgbColor(.1 + .1 * i, .9 - .1 * i, .5))).ToArray();
        var models = new[] { new MixturePresentationModel(catalog, styles, new AtmosphereSettings(), "oxygen", "carbon_dioxide"), new MixturePresentationModel(catalog, styles, new AtmosphereSettings(), "oxygen", "carbon_dioxide", options: new MixtureVisualOptions { PatternEnabled = false }), new MixturePresentationModel(catalog, styles, new AtmosphereSettings(), "oxygen", "carbon_dioxide", options: new MixtureVisualOptions { PatternContrast = .25 }) };
        var rng = new Random(77143);
        var cases = new List<(ColonyPresentation.Table Table, OniNativeColony.Reading Cell)>();
        for (int ncase = 0; ncase < 12000; ++ncase)
        {
            var n = ncase % 8;
            var pick = Enumerable.Range(0, 7).OrderBy(_ => rng.Next()).Take(n).ToArray();
            var parts = new OniNativeColony.Constituent[n];
            double gasMoles = 0, liquidV = 0;
            for (int i = 0; i < n; ++i)
            {
                var k = pick[i];
                var mass = Math.Pow(10, rng.Next(-25, 2)) * (.1 + rng.NextDouble());
                var vol = k < 5 ? 0 : mass / 1000;
                parts[i] = new OniNativeColony.Constituent { Species = (uint)k, MassKg = mass, PartialVolumeCubicMetres = vol };
                if (k < 5)
                    gasMoles += mass / mm[k];
                else
                    liquidV += vol;
            }
            var t = n == 0 ? 0 : 100 + rng.NextDouble() * 1000;
            var gasVolume = gasMoles > 0 ? 1 - liquidV : 0;
            var p = gasMoles > 0 ? gasMoles * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * t / gasVolume : 0;
            for (int i = 0; i < n; ++i)
                if (parts[i].Species < 5)
                    parts[i].PartialVolumeCubicMetres = gasVolume * ((parts[i].MassKg / mm[parts[i].Species]) / gasMoles);
            var cell = new OniNativeColony.Reading { PublicCell = (uint)ncase, Constituents = (uint)n, TemperatureKelvin = t, GasPressurePa = p, MechanicalPressurePa = p, OccupiedVolumeCubicMetres = gasMoles > 0 ? 1 : liquidV, LiquidVolumeCubicMetres = liquidV, InternalEnergyJoules = n == 0 ? 0 : 42 };
            if (ncase % 19 == 0 && n > 0)
                cell.GasPressurePa *= 2;
            if (ncase % 23 == 0 && n > 0)
                cell.TemperatureKelvin = double.NaN;
            if (ncase % 29 == 0 && n > 1)
                parts[1] = parts[0];
            if (ncase % 31 == 0)
                cell.LiquidVolumeCubicMetres += .01;
            var table = new ColonyPresentation.Table(catalog, ids, phases, mm, new[] { cell }, parts);
            cases.Add((table, cell));
        }
        var success = new List<(ColonyPresentation.Table Table, OniNativeColony.Reading Cell)>();
        int rejected = 0;
        foreach (var test in cases)
        {
            foreach (var model in models)
            {
                MixturePresentation? old = null;
                MixtureTextureScalars value = default;
                string? oldError = null, newError = null;
                var x = (int)test.Cell.PublicCell % 37 - 19.25;
                var y = (int)test.Cell.PublicCell / 37 - 12.75;
                try
                {
                    old = model.Describe(ColonyPresentation.Create(test.Table, test.Cell, 7));
                }
                catch (Exception ex) { oldError = ex.GetType().FullName + ":" + ex.Message; }
                try
                {
                    value = ColonyCompactReadings.Sample(test.Table, test.Cell, 7, model, x, y);
                }
                catch (Exception ex) { newError = ex.GetType().FullName + ":" + ex.Message; }
                Check(oldError == newError, "rejection mismatch case " + test.Cell.PublicCell + ": " + oldError + " vs " + newError);
                if (old == null)
                {
                    ++rejected;
                    continue;
                }
                Exact(old.TotalPressurePa, value.TotalPressurePa, "pressure");
                Exact(old.GasMassKg, value.GasMassKg, "mass");
                Exact(old.GasVolumeCubicMetres, value.GasVolumeCubicMetres, "volume");
                Exact(old.GasDensityKgPerCubicMetre, value.GasDensityKgPerCubicMetre, "density");
                Exact(old.ThermodynamicReading!.LiquidVolumeCubicMetres, value.LiquidVolumeCubicMetres, "liquid");
                Exact(old.OxygenMoleFraction, value.OxygenMoleFraction, "o2");
                Exact(old.Co2MoleFraction, value.Co2MoleFraction, "co2");
                Exact(old.OxygenPartialPressurePa, value.OxygenPartialPressurePa, "o2p");
                Exact(old.Co2PartialPressurePa, value.Co2PartialPressurePa, "co2p");
                Check(old.Failures == value.Failures && old.IsBreathable == value.IsBreathable && old.HasPhaseVolumeAppearance == value.HasPhaseVolumeAppearance, "diagnostics");
                var color = old.WorldFluidColorAt(x, y);
                Exact(color.R, value.Color.R, "r");
                Exact(color.G, value.Color.G, "g");
                Exact(color.B, value.Color.B, "b");
                foreach (var vis in new[] { 0.0, .1, 1.0 })
                    Check(Opacity(old.GasMassKg, old.GasVolumeCubicMetres, old.GasDensityKgPerCubicMetre, old.ThermodynamicReading.LiquidVolumeCubicMetres, old.HasPhaseVolumeAppearance, 15, 200, vis) == Opacity(value.GasMassKg, value.GasVolumeCubicMetres, value.GasDensityKgPerCubicMetre, value.LiquidVolumeCubicMetres, value.HasPhaseVolumeAppearance, 15, 200, vis), "opacity");
                Check(Byte(color.R) == Byte(value.Color.R) && Byte(color.G) == Byte(value.Color.G) && Byte(color.B) == Byte(value.Color.B), "texture bytes");
                if (ReferenceEquals(model, models[0]))
                    success.Add(test);
            }
        }
        // Same thread scratch is warmed: no full readings, constituents, analysis,
        // lists, dictionary entries, or strings are allocated by positive sampling.
        foreach (var t in success.Take(2000))
            sink += ColonyCompactReadings.Sample(t.Table, t.Cell, 7, models[0], 4, 5).TotalPressurePa;
        long before = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 20000; ++i)
        {
            var t = success[i % success.Count];
            sink += ColonyCompactReadings.Sample(t.Table, t.Cell, 7, models[0], 4, 5).Color.R;
        }
        var allocated = GC.GetAllocatedBytesForCurrentThread() - before;
        Check(allocated == 0, "warmed compact allocation " + allocated);
        Parallel.For(0, 8, worker => { for (int i = 0; i < 3000; ++i) { var t = success[(i + worker) % success.Count]; var got = ColonyCompactReadings.Sample(t.Table, t.Cell, 7, models[0], 4, 5); var expected = models[0].Describe(ColonyPresentation.Create(t.Table, t.Cell, 7)); if (BitConverter.DoubleToInt64Bits(got.Color.R) != BitConverter.DoubleToInt64Bits(expected.WorldFluidColorAt(4, 5).R)) throw new Exception("worker equality"); } });
        OriginalOracleControls();
        OptionalRangeControls(models[0], success);
        LargeCases();
        Lifetime(models, success);
        BatchControls(models[0], success);
        CollisionAdmission(models, success);
        Boundaries(models[0], success);
        PairedControls(models[0], success);
        Console.WriteLine($"PASS texture scalar exact same-process {checks} checks, {success.Count} valid cases, {rejected} model rejections, warmed allocation {allocated} bytes/20000 samples, parallel workers8");
        if (args.Contains("--benchmark"))
        {
            ViewportBenchmark(models[0], success);
            CacheBenchmark(models[0], success);
            foreach (var which in new[] { false, true, false, true })
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();
                long alloc = GC.GetAllocatedBytesForCurrentThread();
                var watch = Stopwatch.StartNew();
                for (int batch = 0; batch < 60; ++batch)
                    for (int c = 0; c < 800; ++c)
                    {
                        var t = success[(batch * 800 + c) % success.Count];
                        if (which)
                        {
                            for (int channel = 0; channel < 3; ++channel)
                                sink += ColonyCompactReadings.Sample(t.Table, t.Cell, 7, models[0], c % 40, c / 40).Color.R;
                        }
                        else
                        {
                            var p = models[0].Describe(ColonyPresentation.Create(t.Table, t.Cell, 7));
                            sink += p.WorldFluidColorAt(c % 40, c / 40).R;
                            sink += p.GasDensityKgPerCubicMetre;
                            sink += (int)p.Failures;
                        }
                    }
                watch.Stop();
                alloc = GC.GetAllocatedBytesForCurrentThread() - alloc;
                Console.WriteLine($"BENCH {(which ? "compact-three-channels" : "full-once-cached")} cells48000 elapsed_ms {watch.Elapsed.TotalMilliseconds:F3} allocated_bytes {alloc}");
            }
        }
        GC.KeepAlive(sink);
        return 0;
    }
}
