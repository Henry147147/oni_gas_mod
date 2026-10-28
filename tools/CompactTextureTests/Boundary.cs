using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using MixedAtmospheres.Oni;
static partial class Proof
{
    static RuntimeMixturePresentationFrame Runtime(ColonyPresentation.Table table, OniNativeColony.Reading cell, MixturePresentationModel model, Action<int, string> rejected)
    {
        var buffer = new ColonyReadingBuffer { Readings = new[] { cell }, Parts = table.Parts, Count = 1, PartCount = table.Parts.Length, Frame = 7, Lease = 123, Sorted = true, CompactCache = new ColonyCompactReadings.Cache(1, 123) };
        var lazy = new ColonyLazyReadings(buffer, table, 7, rejected);
        MixturePresentationRuntime.ResetProviders();
        MixturePresentationRuntime.Configure(model);
        Check(MixturePresentationRuntime.PublishNativeDomainFrame(1, 1, 7, lazy, MixturePresentationRuntime.NativeDomainSession), "boundary publication");
        return MixturePresentationRuntime.AcquireFrameFor(987)!;
    }
    static string Error(Action action)
    {
        try
        {
            action();
            return "no error";
        }
        catch (Exception e) { return e.GetType().FullName + ":" + e.Message; }
    }
    static void Boundaries(MixturePresentationModel model, List<(ColonyPresentation.Table Table, OniNativeColony.Reading Cell)> cases)
    {
        var input = cases.First(t => t.Cell.Constituents > 0 && t.Table.Phases[t.Table.Parts[0].Species] == NativeMatterPhase.Gas);
        var native = input.Table;
        var ids = (string[])native.Ids.Clone();
        var phases = (NativeMatterPhase[])native.Phases.Clone();
        var molars = (double[])native.MolarMasses.Clone();
        var parts = new[] { new OniNativeColony.Constituent { Species = 0, MassKg = 1, PartialVolumeCubicMetres = 1 } };
        var pressure = 1 / molars[0] * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300;
        var cell = new OniNativeColony.Reading { PublicCell = 0, Constituents = 1, TemperatureKelvin = 300, GasPressurePa = pressure, MechanicalPressurePa = pressure, OccupiedVolumeCubicMetres = 1, InternalEnergyJoules = 1 };
        var table = new ColonyPresentation.Table(native.Catalog, ids, phases, molars, new[] { cell }, parts);
        var parameters = new MixtureWorldRendering.OpacityParameters(15, 200, .1);
        int rejects = 0;
        var frame = Runtime(table, cell, model, (_, _) => ++rejects);
        var full = model.Describe(ColonyPresentation.Create(table, cell, 7));
        var expected = Error(() => MixtureWorldRendering.Sample(full, double.NaN, 0, parameters));
        var actual = Error(() => frame.TryGetTextureScalars(0, double.NaN, 0, out _));
        Check(expected == actual && actual != "no error", "coordinate error propagates with exact type/message");
        Check(rejects == 0, "coordinate error is not a constructor rejection");
        foreach (var range in new[] { new MixtureWorldRendering.OpacityParameters(15, 200, .1), new MixtureWorldRendering.OpacityParameters(0, 500, .8), new MixtureWorldRendering.OpacityParameters(1, 2, 0) })
        {
            Check(frame.TryGetTextureScalars(0, 0, 0, out var scalar), "dynamic range scalar cached");
            Check(MixtureWorldRendering.WorldFluidOpacity(full, range) == MixtureWorldRendering.WorldFluidOpacity(scalar, range), "dynamic opacity uses current batch range");
        }
        ids[0] = "unsupported-native-identity";
        rejects = 0;
        frame = Runtime(table, cell, model, (_, _) => ++rejects);
        expected = Error(() => model.Describe(ColonyPresentation.Create(table, cell, 7)));
        actual = Error(() => frame.TryGetTextureScalars(0, 0, 0, out _));
        Check(expected == actual && actual.Contains("Unsupported fluid material"), "model identity failure propagates");
        Check(rejects == 0, "model identity failure is not constructor rejection");
        ids[0] = native.Ids[0];
        // A valid supplied reading may overflow presentation density. Its argument
        // failure belongs to Describe, outside TryCreate's constructor-only catch.
        molars[0] = 1e308;
        parts[0].MassKg = 1e308;
        parts[0].PartialVolumeCubicMetres = FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 1e-100 / 1e208;
        cell.TemperatureKelvin = 1e-100;
        cell.GasPressurePa = cell.MechanicalPressurePa = 1e208;
        cell.OccupiedVolumeCubicMetres = parts[0].PartialVolumeCubicMetres;
        table = new ColonyPresentation.Table(native.Catalog, ids, phases, molars, new[] { cell }, parts);
        rejects = 0;
        frame = Runtime(table, cell, model, (_, _) => ++rejects);
        expected = Error(() => model.Describe(ColonyPresentation.Create(table, cell, 7)));
        actual = Error(() => frame.TryGetTextureScalars(0, 0, 0, out _));
        Check(expected == actual && actual.Contains("GasDensityKgPerCubicMetre"), "model ArgumentException propagates without fallback");
        Check(rejects == 0, "model ArgumentException is not constructor rejection");
        // Fresh actual runtime fixture, repeated three channels: full fallback owns
        // the one rejection and cached null. Compact never records it itself.
        ids = (string[])native.Ids.Clone();
        molars = (double[])native.MolarMasses.Clone();
        var dupParts = new[] { new OniNativeColony.Constituent { Species = 0, MassKg = 1, PartialVolumeCubicMetres = .5 }, new OniNativeColony.Constituent { Species = 0, MassKg = 1, PartialVolumeCubicMetres = .5 } };
        cell = new OniNativeColony.Reading { PublicCell = 0, Constituents = 2, TemperatureKelvin = 300, GasPressurePa = pressure * 2, MechanicalPressurePa = pressure * 2, OccupiedVolumeCubicMetres = 1, InternalEnergyJoules = 1 };
        table = new ColonyPresentation.Table(native.Catalog, ids, phases, molars, new[] { cell }, dupParts);
        rejects = 0;
        string? detail = null;
        frame = Runtime(table, cell, model, (_, m) => { ++rejects; detail = m; });
        for (int channel = 0; channel < 3; ++channel)
        {
            Check(!frame.TryGetTextureScalars(0, 0, 0, out _), "invalid reading compact fallback");
            Check(!frame.TryGetCell(0, out _), "invalid reading full fallback");
        }
        Check(rejects == 1 && detail!.Contains("Duplicate constituent identity"), "actual runtime rejection once across channels");
        MixturePresentationRuntime.ResetProviders();
        Console.WriteLine("PASS actual runtime constructor-rejection cached-null count, model-error and coordinate-error boundaries, dynamic opacity");
    }
}
