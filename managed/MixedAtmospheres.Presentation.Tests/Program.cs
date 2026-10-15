using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Threading.Tasks;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

internal static class Program
{
    private static int passed;
    private static readonly FluidMaterialCatalog Catalog = new("presentation-test-model-v1", new[]
    {
        new FluidMaterial("oxygen", NativeMatterPhase.Gas, 0.0319988, 658),
        new FluidMaterial("nitrogen", NativeMatterPhase.Gas, 0.0280134, 743),
        new FluidMaterial("carbon_dioxide", NativeMatterPhase.Gas, 0.0440095, 655),
        new FluidMaterial("chlorine", NativeMatterPhase.Gas, 0.070906, 362),
        new FluidMaterial("hydrogen", NativeMatterPhase.Gas, 0.00201588, 10180),
        new FluidMaterial("water", NativeMatterPhase.Liquid, 0.01801528, 4184, 1000)
    });
    private static readonly MixtureSpeciesStyle[] Styles =
    {
        new("oxygen", "Oxygen", new MixtureSrgbColor(0.1, 0.5, 1)),
        new("nitrogen", "Nitrogen", new MixtureSrgbColor(0.4, 0.5, 0.65)),
        new("carbon_dioxide", "Carbon dioxide", new MixtureSrgbColor(0.5, 0.3, 0.8)),
        new("chlorine", "Chlorine", new MixtureSrgbColor(0.3, 0.8, 0.2)),
        new("hydrogen", "Hydrogen", new MixtureSrgbColor(0.9, 0.3, 0.5)),
        new("water", "Water", new MixtureSrgbColor(0.1, 0.3, 0.8))
    };
    private static readonly Dictionary<string, double> Limits = new(StringComparer.Ordinal) { ["chlorine"] = 0.000001, ["hydrogen"] = 0.01 };

    private static int Main(string[] args)
    {
        if (args.Length == 0) NativeTestLibrary.PinFromEnvironment();
        if (args.Length != 0)
        {
            if (args.Length != 1 || args[0] != "--presentation-benchmark") throw new ArgumentException("Usage: --presentation-benchmark");
            Benchmark(); return 0;
        }
        CorrectnessTests();
        Console.WriteLine($"{passed} mixture presentation tests passed"); return 0;
    }

    private static MixturePresentationModel Model(AtmosphereSettings? settings = null, MixtureVisualOptions? options = null,
        IEnumerable<MixtureSpeciesStyle>? styles = null, IReadOnlyDictionary<string, double>? limits = null) =>
        new(Catalog, styles ?? Styles, settings ?? new AtmosphereSettings(), "oxygen", "carbon_dioxide", limits ?? Limits, options);
    private static FluidInventory Air(double oxygen = 0.21, double co2 = 0, double pressure = 101325, double temperature = 300) =>
        FromMoleFractions(new Dictionary<string, double> { ["oxygen"] = oxygen, ["nitrogen"] = 1 - oxygen - co2, ["carbon_dioxide"] = co2 }, pressure, temperature);
    private static FluidInventory FromMoleFractions(IReadOnlyDictionary<string, double> fractions, double pressure = 101325, double temperature = 300)
    {
        var moles = pressure / (FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * temperature);
        return Catalog.CreateInventory(fractions.ToDictionary(entry => entry.Key, entry => moles * entry.Value * Catalog.Materials[entry.Key].MolarMassKgPerMol), temperature);
    }
    private static void Test(string name, Action test) { test(); ++passed; Console.WriteLine("PASS " + name); }
    private static void Check(bool condition) { if (!condition) throw new Exception("Assertion failed."); }
    private static void Near(double actual, double expected, double tolerance = 1e-11)
        => Check(double.IsFinite(actual) && Math.Abs(actual - expected) <= Math.Max(double.Epsilon, Math.Abs(expected) * tolerance));
    private static void Reject(Action action)
    {
        try { action(); } catch { return; }
        throw new Exception("Expected operation rejection.");
    }
    private static double ColorDifference(MixtureSrgbColor left, MixtureSrgbColor right) =>
        Math.Max(Math.Abs(left.R - right.R), Math.Max(Math.Abs(left.G - right.G), Math.Abs(left.B - right.B)));

    private static void CorrectnessTests()
    {
        Test("air shows gas mole percentages rather than constituent mass fractions", () =>
        {
            var view = Model().Describe(Air(), 1, 7, "cell:1"); var oxygen = view.Constituents.Single(row => row.MaterialId == "oxygen");
            Near(oxygen.GasMoleFraction!.Value, 0.21); Near(oxygen.PartialPressurePa!.Value, 101325 * 0.21);
            Check(Math.Abs(oxygen.MassKg / view.TotalMassKg - 0.21) > 0.01);
            Check(view.DisplayName == "Nitrogen–Oxygen mixture\nBreathable" && view.IsBreathable && view.Failures == BreathingFailure.None);
            Near(view.TemperatureKelvin, 300); Near(view.TotalPressurePa, 101325); Near(view.GasDensityKgPerCubicMetre, view.GasMassKg);
            Check(view.Revision == 7 && view.SourceId == "cell:1" && view.IsReadOnly);
        });

        Test("dilution can have enough oxygen fraction but insufficient oxygen partial pressure", () =>
        {
            var view = Model().Describe(Air(pressure: 10000), 1);
            Check(view.Failures == BreathingFailure.OxygenPartialPressureLow && view.Warnings.Count == 1);
            Check(view.Warnings[0].Contains("partial pressure", StringComparison.Ordinal));
        });

        Test("hard mode upper oxygen limit and easy mode match native exposure parameters", () =>
        {
            var hard = Model().Describe(Air(0.9), 1); var easy = Model(new AtmosphereSettings { HardMode = false }).Describe(Air(0.9), 1);
            Check(hard.Failures == BreathingFailure.OxygenFractionHigh && easy.IsBreathable);
            var thinEasy = Model(new AtmosphereSettings { HardMode = false }).Describe(Air(0.9, pressure: 100), 1);
            Check(thinEasy.Failures == BreathingFailure.OxygenPartialPressureLow);
            var pure = Catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = 1 }, 300);
            Check(Model().Describe(pure, 1).DisplayName == "Oxygen\nUnsafe atmosphere");
            Check(Model(new AtmosphereSettings { HardMode = false }).Describe(pure, 1).DisplayName == "Oxygen\nBreathable");
            Check(Model().Describe(FluidInventory.Empty, 1).DisplayName == "Vacuum / no atmosphere");
        });

        Test("carbon dioxide fraction and pressure failures are independently visible", () =>
        {
            var fractionOnly = Model().Describe(Air(0.21, 0.006, 50000), 1);
            Check(fractionOnly.Failures.HasFlag(BreathingFailure.CarbonDioxideFractionHigh));
            Check(!fractionOnly.Failures.HasFlag(BreathingFailure.CarbonDioxidePartialPressureHigh));
            var pressureOnly = Model().Describe(Air(0.21, 0.004, 200000), 1);
            Check(!pressureOnly.Failures.HasFlag(BreathingFailure.CarbonDioxideFractionHigh));
            Check(pressureOnly.Failures.HasFlag(BreathingFailure.CarbonDioxidePartialPressureHigh));
            Check(Model().Describe(Air(0.15, 0.005, 100000), 1).IsBreathable);
        });

        Test("explicit trace contaminants flag risk without color or dominant species inference", () =>
        {
            var mix = FromMoleFractions(new Dictionary<string, double> { ["oxygen"] = 0.21, ["nitrogen"] = 0.789998, ["chlorine"] = 0.000002 });
            var view = Model().Describe(mix, 1); var row = view.Constituents.Single(item => item.MaterialId == "chlorine");
            Check(view.Failures == BreathingFailure.Contaminant && row.IsTrace && row.IsContaminant);
            Check(view.DisplayName == "Nitrogen–Oxygen mixture (+ traces)\nUnsafe atmosphere" && view.Warnings.Single().Contains("Chlorine", StringComparison.Ordinal));
            var recolored = Styles.Select(style => new MixtureSpeciesStyle(style.MaterialId, style.DisplayName, new MixtureSrgbColor(0.2, 0.2, 0.2)));
            Check(Model(styles: recolored).Describe(mix, 1).Failures == view.Failures);
        });

        Test("every positive trace row survives formatting and never implies purity or zero", () =>
        {
            var inventory = Catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = 1, ["chlorine"] = 1e-25 }, 300);
            var view = Model().Describe(inventory, 1); var trace = view.Constituents.Single(row => row.MaterialId == "chlorine");
            Check(view.Constituents.Count == 2 && view.DisplayName.Contains("traces", StringComparison.Ordinal));
            Check(trace.MassKg > 0 && trace.Moles > 0 && trace.GasMoleFraction > 0 && trace.PartialPressurePa > 0);
            Check(trace.MolePercentText != "0%" && trace.MassTextKg != "0 kg" && trace.PartialPressureText != "0 Pa");
            Check(view.Constituents.Single(row => row.MaterialId == "oxygen").MolePercentText == "<100%");
            var almostPure = Catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = 1, ["nitrogen"] = 1e-8 }, 300);
            Check(Model().Describe(almostPure, 1).Constituents.Single(row => row.MaterialId == "oxygen").MolePercentText == "<100%");
        });

        Test("tiny and zero hazard limits cannot be suppressed by arithmetic tolerance or underflow", () =>
        {
            var tiny = FromMoleFractions(new Dictionary<string, double> { ["oxygen"] = .21, ["nitrogen"] = .79, ["chlorine"] = 2e-20 });
            var view = Model(limits: new Dictionary<string, double> { ["chlorine"] = 1e-20 }).Describe(tiny, 1);
            Check(view.Failures == BreathingFailure.Contaminant && view.Warnings.Single().Contains("Chlorine", StringComparison.Ordinal));
            Check(view.Constituents.Single(row => row.MaterialId == "chlorine").IsContaminant);
            Check(Model(limits: new Dictionary<string, double> { ["chlorine"] = 3e-20 }).Describe(tiny, 1).IsBreathable);
            var masses = Air().MassesKg.ToDictionary(entry => entry.Key, entry => entry.Value);
            masses["chlorine"] = double.Epsilon;
            var subnormal = Model(limits: new Dictionary<string, double> { ["chlorine"] = 0 }).Describe(Catalog.CreateInventory(masses, 300), 1);
            var row = subnormal.Constituents.Single(value => value.MaterialId == "chlorine");
            Check(subnormal.Failures == BreathingFailure.Contaminant && row.IsContaminant && row.MassKg == double.Epsilon);
            Check(row.MolePercentText == "below numeric range" && row.PartialPressureText == "below numeric range Pa");
            var zeroCo2 = new AtmosphereSettings { MaximumCo2Fraction = 0, MaximumCo2PartialPressurePa = 0 };
            var co2 = FromMoleFractions(new Dictionary<string, double> { ["oxygen"] = .21, ["nitrogen"] = .79, ["carbon_dioxide"] = 1e-25 });
            var co2View = Model(zeroCo2).Describe(co2, 1);
            Check(co2View.Failures == (BreathingFailure.CarbonDioxideFractionHigh | BreathingFailure.CarbonDioxidePartialPressureHigh));
        });

        Test("liquid constituents do not enter gas mole fractions partial pressures or gas color", () =>
        {
            var air = Air(); var masses = air.MassesKg.ToDictionary(entry => entry.Key, entry => entry.Value); masses["water"] = 500;
            var view = Model().Describe(Catalog.CreateInventory(masses, 300), 1); var water = view.Constituents.Single(row => row.MaterialId == "water");
            Check(water.GasMoleFraction == null && water.PartialPressurePa == null && !water.IsTrace && !water.IsContaminant);
            Near(view.GasConstituents.Sum(row => row.GasMoleFraction!.Value), 1); Near(view.LiquidVolumeFraction, 0.5);
            Near(view.TotalPressurePa, 2 * 101325); Near(view.GasDensityKgPerCubicMetre, air.TotalMassKg / 0.5);
            Near(ColorDifference(view.BaseColor, Model().Describe(air, 1).BaseColor), 0);
            var liquidOnly = Model().Describe(Catalog.CreateInventory(new Dictionary<string, double> { ["water"] = 1000 }, 300), 1);
            Check(liquidOnly.Failures == BreathingFailure.Vacuum && liquidOnly.GasConstituents.Count == 0 && liquidOnly.DisplayName == "Water (liquid)\nVacuum / no atmosphere");
            Near(liquidOnly.GasDensityKgPerCubicMetre, 0);
        });

        Test("same total mass with different composition changes appearance and pressure", () =>
        {
            var left = Model().Describe(Catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = 1, ["nitrogen"] = 1 }, 300), 1);
            var right = Model().Describe(Catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = 1.8, ["nitrogen"] = 0.2 }, 300), 1);
            Near(left.TotalMassKg, right.TotalMassKg); Near(left.GasDensityKgPerCubicMetre, right.GasDensityKgPerCubicMetre);
            Check(ColorDifference(left.BaseColor, right.BaseColor) > 0.03 && Math.Abs(left.TotalPressurePa - right.TotalPressurePa) > 1000);
        });

        Test("mole-weighted colors are mixed in linear RGB and returned in sRGB", () =>
        {
            var styles = Styles.Select(style => new MixtureSpeciesStyle(style.MaterialId, style.DisplayName,
                style.MaterialId == "oxygen" ? new MixtureSrgbColor(1, 0, 0) : style.MaterialId == "nitrogen" ? new MixtureSrgbColor(0, 0, 1) : style.Color));
            var view = Model(styles: styles).Describe(Air(0.5), 1);
            Near(view.BaseColor.R, 0.7353569830524495); Near(view.BaseColor.G, 0); Near(view.BaseColor.B, 0.7353569830524495);
        });

        Test("world-anchored pattern is stable periodic smooth and continuous through dominance ties", () =>
        {
            var model = Model(); var tie = model.Describe(Air(0.5), 1);
            Near(ColorDifference(tie.ColorAt(0.125, -0.25), tie.ColorAt(4.125, 3.75)), 0, 1e-8);
            Check(ColorDifference(tie.ColorAt(0.125, 0.2), tie.ColorAt(0.12500001, 0.2)) < 1e-8);
            var before = model.Describe(Air(0.5 - 1e-9), 1); var after = model.Describe(Air(0.5 + 1e-9), 1);
            Check(ColorDifference(before.ColorAt(1.25, 2.5), after.ColorAt(1.25, 2.5)) < 1e-8);
            var reversed = Model(styles: Styles.Reverse()).Describe(Air(0.5), 1);
            Near(ColorDifference(tie.ColorAt(1.25, 2.5), reversed.ColorAt(1.25, 2.5)), 0);
            Check(tie.PatternEnabled && tie.PatternContrast == 0.04);
            var noPattern = model.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false }).Describe(Air(0.5), 1);
            Near(ColorDifference(noPattern.ColorAt(1.25, 2.5), noPattern.BaseColor), 0);
            var zeroContrast = model.WithVisualOptions(new MixtureVisualOptions { PatternContrast = 0 }).Describe(Air(0.5), 1);
            Near(ColorDifference(zeroContrast.ColorAt(1.25, 2.5), zeroContrast.BaseColor), 0);
        });

        Test("presentation owns settings options styles and contaminant policy and reconfiguration preserves physiology", () =>
        {
            var settings = new AtmosphereSettings(); var options = new MixtureVisualOptions(); var styles = Styles.ToList();
            var limits = new Dictionary<string, double>(Limits); var model = Model(settings, options, styles, limits);
            settings.HardMode = false; options.PatternEnabled = false; styles.Clear(); limits["chlorine"] = 1;
            var view = model.Describe(Air(0.9), 1); Check(view.Failures == BreathingFailure.OxygenFractionHigh && view.PatternEnabled);
            var exposed = model.BreathingSettings; exposed.HardMode = false; var visual = model.VisualOptions; visual.PatternContrast = 0;
            Check(model.BreathingSettings.HardMode && model.VisualOptions.PatternContrast == 0.04);
            Check(model.HardMode && model.MinimumOxygenPartialPressurePa == 15000 && model.MaximumCo2PartialPressurePa == 500);
            Check(model.GasMaterialIds.Count == 5 && !model.GasMaterialIds.Contains("water") && model.OxygenMaterialId == "oxygen");
            var recolored = model.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false, PatternContrast = 0.12 });
            Check(recolored.Describe(Air(0.9), 1).Failures == view.Failures && !recolored.Describe(Air(), 1).PatternEnabled);
            Reject(() => ((IDictionary<string, MixtureSpeciesStyle>)model.Styles).Clear());
            Reject(() => ((IList<MixtureConstituentPresentation>)view.Constituents).Clear());
        });

        Test("cell and container sampling use one immutable revision and reject stale combined frames", () =>
        {
            var state = Fixture(); var snapshot = state.CaptureSnapshot(); var samples = new List<MixtureContainerSample> { new("pipe:1", Air(), 0.2, snapshot.Revision) };
            var model = Model(); var frame = model.CaptureFrame(snapshot, samples); samples.Clear();
            Check(frame.Width == 4 && frame.Height == 4 && frame.Revision == snapshot.Revision && frame.Containers.Count == 1);
            Check(ReferenceEquals(frame.SampleCell(5), frame.SampleCell(5)) && ReferenceEquals(frame.SampleCell(6), frame.Vacuum));
            Check(!frame.TrySampleCell(9, out _) && !frame.TrySampleCell(0, out _));
            Check(frame.SampleContainer("pipe:1").SourceId == "pipe:1" && !frame.TrySampleContainer("missing", out _));
            using (var transaction = state.BeginTransaction()) { transaction.Transfer(5, 6, 0.5); transaction.CommitFrame(1, 0.1); }
            var newer = model.CaptureFrame(state.CaptureSnapshot()); Near(frame.SampleCell(5).TotalMassKg, 2); Near(newer.SampleCell(5).TotalMassKg, 1.5);
            Check(newer.Revision == frame.Revision + 1 && newer.CompletedFrame == 1);
            Reject(() => model.CaptureFrame(state.CaptureSnapshot(), new[] { new MixtureContainerSample("stale", Air(), 1, 0) }));
            Reject(() => model.CaptureFrame(snapshot, new[] { new MixtureContainerSample("duplicate", Air(), 1, 0), new MixtureContainerSample("duplicate", Air(), 1, 0) }));
        });

        Test("invalid models display inputs and nonfinite pattern coordinates reject explicitly", () =>
        {
            Reject(() => Model(options: new MixtureVisualOptions { PatternContrast = double.NaN }));
            Reject(() => Model(styles: Styles.Skip(1))); Reject(() => Model(limits: new Dictionary<string, double> { ["water"] = 0.1 }));
            Reject(() => Model(new AtmosphereSettings { MaximumOxygenFraction = 0.1 }));
            Reject(() => Model().Describe(Air(), 0)); Reject(() => Model().Describe(Air(), 1).ColorAt(double.PositiveInfinity, 0));
            var changed = new FluidMaterialCatalog("other", Catalog.Materials.Values);
            var wrongModel = new MixturePresentationModel(changed, Styles, new AtmosphereSettings(), "oxygen", "carbon_dioxide");
            Reject(() => wrongModel.CaptureFrame(Fixture().CaptureSnapshot()));
        });

        Test("cached immutable analysis is shared while source revision volume and visual policy stay independent", () =>
        {
            var model = Model(); var inventory = Air(); var first = model.Describe(inventory, 1, 4, "cell:4");
            var second = model.Describe(inventory, 1, 5, "container:5");
            Check(ReferenceEquals(first.Constituents, second.Constituents) && ReferenceEquals(first.Warnings, second.Warnings));
            Check(first.Revision == 4 && first.SourceId == "cell:4" && second.Revision == 5 && second.SourceId == "container:5");
            var expanded = model.Describe(inventory, 2, 6, "expanded"); Near(expanded.TotalPressurePa, first.TotalPressurePa / 2);
            Check(!ReferenceEquals(first.Constituents, expanded.Constituents));
            for (var i = 3; i <= 10; ++i) Near(model.Describe(inventory, i).TotalPressurePa, first.TotalPressurePa / i);
            Near(model.Describe(inventory, 1).TotalPressurePa, first.TotalPressurePa);
            var disabled = model.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false }).Describe(inventory, 1);
            Check(first.PatternEnabled && !disabled.PatternEnabled && first.Revision == 4);
            Parallel.For(0, 100, i =>
            {
                var projection = model.Describe(inventory, 1, i, "parallel:" + i);
                Check(projection.Revision == i && projection.SourceId == "parallel:" + i);
                Near(projection.TotalPressurePa, first.TotalPressurePa);
            });
        });

        Test("weak analysis cache does not retain past fluid inventories", () =>
        {
            var model = Model(); var reference = CacheTemporaryInventory(model);
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
            Check(!reference.IsAlive); GC.KeepAlive(model);
        });

        Test("lazy inspection rows warnings names and cell labels publish immutable thread-safe values", () =>
        {
            var model = Model(); var inventory = FromMoleFractions(new Dictionary<string, double>
                { ["oxygen"] = 0.21, ["nitrogen"] = 0.789998, ["chlorine"] = 0.000002 });
            var projection = model.Describe(inventory, 1, 19, "lazy-container");
            Check(projection.Failures == BreathingFailure.Contaminant);
            var frame = model.CaptureFrame(Fixture().CaptureSnapshot()); var cell = frame.SampleCell(5);
            Parallel.For(0, 100, _ =>
            {
                Check(projection.Constituents.Count == 3 && projection.GasConstituents.Count == 3);
                Check(projection.DisplayName == "Nitrogen–Oxygen mixture (+ traces)\nUnsafe atmosphere" && projection.Warnings.Single().Contains("Chlorine", StringComparison.Ordinal));
                Check(projection.Constituents.Single(row => row.MaterialId == "chlorine").MolePercentText != "0%");
                Check(cell.SourceId == "owned-cell:5" && cell.Revision == frame.Revision);
            });
            Reject(() => ((IList<MixtureConstituentPresentation>)projection.Constituents).Clear());
            Check(projection.Revision == 19 && projection.SourceId == "lazy-container");
        });

        Test("single-pass projection matches canonical cell thermodynamics across gas liquid trace and momentum states", () =>
        {
            var state = Fixture(); var model = Model(); var random = new Random(17347);
            for (var i = 0; i < 100; ++i)
            {
                var masses = new Dictionary<string, double>
                {
                    ["oxygen"] = 0.01 + random.NextDouble() * 2, ["nitrogen"] = 0.01 + random.NextDouble() * 2,
                    ["carbon_dioxide"] = i % 2 == 0 ? 1e-200 : random.NextDouble() * 0.1,
                    ["chlorine"] = 1e-14 + random.NextDouble() * 1e-8,
                    ["water"] = i % 5 == 0 ? 0 : random.NextDouble() * 900
                };
                var inventory = Catalog.CreateInventory(masses, 100 + random.NextDouble() * 1900,
                    random.NextDouble() * 200 - 100, random.NextDouble() * 200 - 100);
                using (var transaction = state.BeginTransaction())
                {
                    var old = transaction.SampleCell(6).Inventory;
                    if (old.TotalMassKg != 0) transaction.Extract(6, old.TotalMassKg, "differential-clear");
                    transaction.Inject(6, inventory, "differential-initialization"); transaction.Commit();
                }
                var canonical = state.CaptureSnapshot().SampleCell(6); var actual = model.Describe(inventory, 1);
                Check(actual.TemperatureKelvin == canonical.TemperatureKelvin && actual.TotalPressurePa == canonical.GasPressurePa);
                Check(actual.LiquidVolumeFraction == canonical.LiquidVolumeFraction);
                Check(actual.Constituents.Count == inventory.MassesKg.Count);
                foreach (var row in actual.Constituents)
                {
                    Check(row.MassKg == inventory.MassesKg[row.MaterialId]);
                    Near(row.Moles, row.MassKg / Catalog.Materials[row.MaterialId].MolarMassKgPerMol);
                }
            }
        });

        Test("optimized projection preserves canonical offset temperature-range and occupied-volume rejection", () =>
        {
            var materials = new[]
            {
                new FluidMaterial("oxygen", NativeMatterPhase.Gas, 0.0319988, 658, energyOffsetJoulesPerKg: 5e7, minTemperatureKelvin: 200, maxTemperatureKelvin: 400),
                new FluidMaterial("nitrogen", NativeMatterPhase.Gas, 0.0280134, 743, energyOffsetJoulesPerKg: -1e6, minTemperatureKelvin: 250, maxTemperatureKelvin: 500),
                new FluidMaterial("carbon_dioxide", NativeMatterPhase.Gas, 0.0440095, 655, energyOffsetJoulesPerKg: -2e7, minTemperatureKelvin: 200, maxTemperatureKelvin: 600),
                new FluidMaterial("water", NativeMatterPhase.Liquid, 0.01801528, 4184, 1000, -1e7, 275, 350)
            };
            var catalog = new FluidMaterialCatalog("offset-range-test-v1", materials);
            var styles = Styles.Where(style => catalog.Materials.ContainsKey(style.MaterialId));
            var model = new MixturePresentationModel(catalog, styles, new AtmosphereSettings(), "oxygen", "carbon_dioxide");
            var state = Fixture(fixtureCatalog: catalog);
            var masses = new Dictionary<string, double> { ["oxygen"] = 0.2, ["nitrogen"] = 0.8, ["water"] = 200 };
            var inventory = catalog.CreateInventory(masses, 300, 30, -50);
            using (var transaction = state.BeginTransaction()) { transaction.Inject(6, inventory, "offset-range-initialization"); transaction.Commit(); }
            var canonical = state.CaptureSnapshot().SampleCell(6); var actual = model.Describe(inventory, 1);
            Check(actual.TemperatureKelvin == canonical.TemperatureKelvin && actual.TotalPressurePa == canonical.GasPressurePa);
            using var validation = state.BeginTransaction(); validation.Extract(6, validation.SampleCell(6).Inventory.TotalMassKg, "clear-validation-cell");
            var capacity = masses.Sum(entry => entry.Value * catalog.Materials[entry.Key].SpecificHeatCvJoulesPerKgKelvin);
            foreach (var energy in new[] { inventory.TotalEnergyJoules + capacity * 1000, inventory.TotalEnergyJoules - capacity * 200 })
            {
                var unsupported = new FluidInventory(inventory.MassesKg, energy, inventory.MomentumX, inventory.MomentumY);
                Reject(() => model.Describe(unsupported, 1)); Reject(() => validation.Inject(6, unsupported, "unsupported-temperature"));
            }
            foreach (var invalid in new[]
            {
                catalog.CreateInventory(new Dictionary<string, double> { ["water"] = 1100 }, 300),
                catalog.CreateInventory(new Dictionary<string, double> { ["water"] = 1000, ["oxygen"] = 0.1 }, 300),
                new FluidInventory(new Dictionary<string, double> { ["unmodeled"] = 1 }, 30000)
            })
            { Reject(() => model.Describe(invalid, 1)); Reject(() => validation.Inject(6, invalid, "unsupported-model-or-volume")); }
        });

        Test("managed presentation assessment matches native flags and boundary tolerance", NativeAssessmentParity);
        OwnedContainerPresentationTests.Run(Test, Check, Reject, () => Fixture(), () => Model(), Catalog);
        PreparedPresentationPublicationTests.Run(Test, Check, Reject, () => Fixture(), () => Model(), Catalog);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference CacheTemporaryInventory(MixturePresentationModel model)
    {
        var inventory = Air(0.222); model.Describe(inventory, 1); return new WeakReference(inventory);
    }

    private static void NativeAssessmentParity()
    {
        using var native = new FluidWorld(new WorldConfiguration { Width = 3, Height = 2, Gravity = 0, Diffusivity = 0, ThermalConduction = false });
        var cases = new[]
        {
            Air(), Air(0.15, 0.005, 100000), Air(0.15 - 1e-15, 0.005, 100000), Air(0.15 - 1e-8),
            Air(0.35), Air(0.35 + 1e-8), Air(0.21, 0.005 + 1e-8), Air(0.21, 0.004, 200000),
            Air(pressure: 1000), Air(0.9), FluidInventory.Empty,
            FromMoleFractions(new Dictionary<string, double> { ["oxygen"] = 0.21, ["nitrogen"] = 0.789998, ["chlorine"] = 0.000002 }),
            FromMoleFractions(new Dictionary<string, double> { ["oxygen"] = 0.21, ["nitrogen"] = 0.77, ["hydrogen"] = 0.02 })
        };
        foreach (var settings in new[] { new AtmosphereSettings(), new AtmosphereSettings { HardMode = false }, new AtmosphereSettings { MinimumOxygenFraction = 0.17, MinimumOxygenPartialPressurePa = 16000 },
            new AtmosphereSettings { MaximumCo2Fraction = 0, MaximumCo2PartialPressurePa = 0 } })
            foreach (var inventory in cases)
            {
                native.InitializeCell(0, 0, inventory.MassesKg, 300);
                var assessment = native.AssessAtmosphere(0, 0, settings); var projection = Model(settings).Describe(inventory, 1);
                if (assessment.Failures != projection.Failures)
                    throw new Exception("Native/presentation breathing mismatch: native=" + assessment.Failures + ", presentation=" + projection.Failures +
                        ", native O2=" + assessment.OxygenMoleFraction.ToString("R") + ", presentation O2=" + projection.OxygenMoleFraction.ToString("R") +
                        ", CO2=" + projection.Co2MoleFraction.ToString("R") + ", settings=" + settings.ToJson());
                Check(projection.DisplayName.EndsWith(assessment.IsBreathable ? "Breathable" :
                    (assessment.Failures & BreathingFailure.Vacuum) != 0 ? "Vacuum / no atmosphere" : "Unsafe atmosphere", StringComparison.Ordinal));
                Near(assessment.PressurePa, projection.TotalPressurePa); Near(assessment.OxygenMoleFraction, projection.OxygenMoleFraction);
                Near(assessment.OxygenPartialPressurePa, projection.OxygenPartialPressurePa); Near(assessment.Co2MoleFraction, projection.Co2MoleFraction);
            }
    }

    private static ColonyFluidState Fixture(int width = 2, int height = 2, bool allGas = false, float initialGasMassKg = 1, FluidMaterialCatalog? fixtureCatalog = null)
    {
        var paddedWidth = width + 2; var paddedHeight = height + 2; var count = paddedWidth * paddedHeight;
        var raw = new byte[29 + 36 * count]; new byte[] { 83, 73, 77, 83, 65, 86, 69, 0 }.CopyTo(raw, 0);
        void Int(int offset, int value) => BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(offset, 4), value);
        void Float(int offset, float value) => Int(offset, BitConverter.SingleToInt32Bits(value));
        Int(8, 15); Int(12, paddedWidth); Int(16, paddedHeight); Int(20, 1); Int(24, 1);
        for (var i = 0; i < count; ++i)
        {
            var x = i % paddedWidth; var y = i / paddedWidth;
            var gas = allGas && x > 0 && x <= width && y > 0 && y <= height;
            Int(29 + i * 16, gas ? 2 : 4); Float(29 + i * 16 + 4, 300); Float(29 + i * 16 + 8, gas ? initialGasMassKg : 1000);
            Int(29 + count * 24 + i * 12, 1);
        }
        if (!allGas)
        {
            Int(29 + 5 * 16, 2); Float(29 + 5 * 16 + 8, 2);
            Int(29 + 6 * 16, 1); raw.AsSpan(29 + 6 * 16 + 4, 12).Clear();
            Int(29 + 10 * 16, 3); Float(29 + 10 * 16 + 8, 100);
        }
        var envelope = new byte[4 + raw.Length]; BinaryPrimitives.WriteInt32LittleEndian(envelope, raw.Length); raw.CopyTo(envelope, 4);
        var import = NativeColonyImport.StageNewColony(width + 2, height + 2, new[] { new NativeWorldPlacement(7, width, height, 1, 1) },
            new[] { new NativeWorldSource(7, envelope) }, new Dictionary<int, NativeMatterPhase> { [1] = NativeMatterPhase.Vacuum, [2] = NativeMatterPhase.Gas, [3] = NativeMatterPhase.Liquid, [4] = NativeMatterPhase.Solid },
            new Dictionary<int, NativeFluidBinding> { [2] = new("oxygen", NativeMatterPhase.Gas), [3] = new("water", NativeMatterPhase.Liquid) }, 1);
        return ColonyFluidState.Import(import, fixtureCatalog ?? Catalog);
    }

    private static void Benchmark()
    {
        const int count = 100000; var model = Model(); var inventory = Air();
        for (var i = 0; i < 100; ++i) model.Describe(inventory, 1);
        var watch = Stopwatch.StartNew(); var allocations = GC.GetAllocatedBytesForCurrentThread();
        var checksum = 0.0;
        for (var i = 0; i < count; ++i) checksum += model.Describe(inventory, 1, 0, "benchmark").TotalPressurePa;
        watch.Stop(); var describeMs = watch.Elapsed.TotalMilliseconds; var describeAllocations = GC.GetAllocatedBytesForCurrentThread() - allocations;
        var initialOxygenMass = (float)(101325 * 0.21 * Catalog.Materials["oxygen"].MolarMassKgPerMol / (FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300));
        allocations = GC.GetAllocatedBytesForCurrentThread(); watch.Restart();
        var state = Fixture(500, 200, true, initialOxygenMass);
        var nitrogenMass = initialOxygenMass / Catalog.Materials["oxygen"].MolarMassKgPerMol * Catalog.Materials["nitrogen"].MolarMassKgPerMol * 0.79 / 0.21;
        var nitrogen = Catalog.CreateInventory(new Dictionary<string, double> { ["nitrogen"] = nitrogenMass }, 300);
        using (var transaction = state.BeginTransaction())
        {
            for (var y = 1; y <= 200; ++y) for (var x = 1; x <= 500; ++x)
                transaction.Inject(x + y * 502, nitrogen, "benchmark-mixed-air-initialization");
            transaction.Commit();
        }
        var snapshot = state.CaptureSnapshot();
        watch.Stop(); var initializationMs = watch.Elapsed.TotalMilliseconds; var initializationAllocations = GC.GetAllocatedBytesForCurrentThread() - allocations;
        allocations = GC.GetAllocatedBytesForCurrentThread(); watch.Restart(); var frame = model.CaptureFrame(snapshot); watch.Stop();
        var captureMs = watch.Elapsed.TotalMilliseconds; var captureAllocations = GC.GetAllocatedBytesForCurrentThread() - allocations;
        using (var transaction = state.BeginTransaction())
        { transaction.Transfer(1 + frame.Width, 2 + frame.Width, 0.01); transaction.CommitFrame(1, 0.1); }
        allocations = GC.GetAllocatedBytesForCurrentThread(); watch.Restart(); var nextFrame = model.CaptureFrame(state.CaptureSnapshot()); watch.Stop();
        var unchangedCaptureMs = watch.Elapsed.TotalMilliseconds; var unchangedCaptureAllocations = GC.GetAllocatedBytesForCurrentThread() - allocations;
        Check(nextFrame.Revision == frame.Revision + 1 && ReferenceEquals(frame.SampleCell(3 + frame.Width).Constituents, nextFrame.SampleCell(3 + frame.Width).Constituents));
        allocations = GC.GetAllocatedBytesForCurrentThread(); watch.Restart();
        using (var transaction = state.BeginTransaction())
        {
            for (var y = 1; y <= 200; ++y) for (var x = 1; x <= 500; ++x)
                transaction.ApplyHeat(x + y * 502, 1 + (x + y * 502) * 0.00001, "benchmark-change-every-inventory");
            transaction.CommitFrame(2, 0.2);
        }
        watch.Stop(); var changedUpdateMs = watch.Elapsed.TotalMilliseconds; var changedUpdateAllocations = GC.GetAllocatedBytesForCurrentThread() - allocations;
        allocations = GC.GetAllocatedBytesForCurrentThread(); watch.Restart(); var changedFrame = model.CaptureFrame(state.CaptureSnapshot()); watch.Stop();
        var allChangedCaptureMs = watch.Elapsed.TotalMilliseconds; var allChangedCaptureAllocations = GC.GetAllocatedBytesForCurrentThread() - allocations;
        Check(changedFrame.Revision == nextFrame.Revision + 1 && changedFrame.SampleCell(1 + frame.Width).TemperatureKelvin > nextFrame.SampleCell(1 + frame.Width).TemperatureKelvin);
        Check(changedFrame.SampleCell(3 + frame.Width).TemperatureKelvin != changedFrame.SampleCell(4 + frame.Width).TemperatureKelvin);
        for (var i = 0; i < 1000; ++i) { frame.TrySampleCell(1 + frame.Width, out var warm); checksum += warm!.ColorAt(0.2, 0.3).R; }
        const int reads = 1000000;
        allocations = GC.GetAllocatedBytesForCurrentThread(); watch.Restart();
        for (var i = 0; i < reads; ++i)
        {
            var cell = 1 + i % 500 + (1 + i / 500 % 200) * frame.Width;
            Check(frame.TrySampleCell(cell, out var view)); checksum += view!.TotalPressurePa;
        }
        watch.Stop(); var readMs = watch.Elapsed.TotalMilliseconds; var readAllocations = GC.GetAllocatedBytesForCurrentThread() - allocations;
        var sample = frame.SampleCell(1 + frame.Width);
        allocations = GC.GetAllocatedBytesForCurrentThread(); watch.Restart();
        for (var i = 0; i < reads; ++i) checksum += sample.ColorAt(i % 500 + 0.5, i / 500 % 200 + 0.5).R;
        watch.Stop(); var colorMs = watch.Elapsed.TotalMilliseconds; var colorAllocations = GC.GetAllocatedBytesForCurrentThread() - allocations;
        Check(readAllocations == 0 && colorAllocations == 0 && checksum > 0 && frame.Cells.Count == count);
        Console.WriteLine(JsonSerializer.Serialize(new
        {
            kind = "mixture-presentation-benchmark", cells = count, readSamples = reads,
            cellMixture = "21% oxygen / 79% nitrogen by gas moles", patternContrast = 0.04,
            describeMilliseconds = describeMs, describeAllocatedBytes = describeAllocations,
            initializationMilliseconds = initializationMs, initializationAllocatedBytes = initializationAllocations,
            captureFrameMilliseconds = captureMs, captureFrameAllocatedBytes = captureAllocations,
            initializationAndCaptureMilliseconds = initializationMs + captureMs,
            initializationAndCaptureAllocatedBytes = initializationAllocations + captureAllocations,
            changedCellsInNextFrame = 2, nextRevisionCaptureMilliseconds = unchangedCaptureMs, nextRevisionCaptureAllocatedBytes = unchangedCaptureAllocations,
            allChangedCells = count, allChangedRevisionCaptureMilliseconds = allChangedCaptureMs, allChangedRevisionCaptureAllocatedBytes = allChangedCaptureAllocations,
            allChangedUpdateMilliseconds = changedUpdateMs, allChangedUpdateAllocatedBytes = changedUpdateAllocations,
            allChangedUpdateAndCaptureMilliseconds = changedUpdateMs + allChangedCaptureMs,
            allChangedUpdateAndCaptureAllocatedBytes = changedUpdateAllocations + allChangedCaptureAllocations,
            allChangedStateVariation = "distinct positive heat per cell; new inventory references and temperatures",
            readMilliseconds = readMs, readAllocatedBytes = readAllocations,
            colorMilliseconds = colorMs, colorAllocatedBytes = colorAllocations,
            productionSolverGate = false, checksum
        }, new JsonSerializerOptions { WriteIndented = true }));
    }
}
