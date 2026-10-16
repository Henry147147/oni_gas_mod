using System;
using System.Collections.Generic;
using System.Linq;
using MixedAtmospheres.Core;

internal static partial class ColonyCoordinatorTests
{
    private static MixturePresentationModel Model() => Model(CoupledColonySimulation.ReadMaterialCatalog());
    private static MixturePresentationModel Model(FluidMaterialCatalog catalog) => new MixturePresentationModel(catalog,
        catalog.Materials.Keys.Select(id => new MixtureSpeciesStyle(id, id, new MixtureSrgbColor(.4, .6, .7))),
        new AtmosphereSettings(), "oxygen", "carbon_dioxide", new Dictionary<string, double> { ["chlorine"] = 0 });

    internal static void RunPresentation(Action<string, Action> test, Action<bool, string> check, Action<Action> reject)
    {
        test("game presentation scenario transports four bulk gases and hazardous and benign traces", () =>
        {
            using var simulation = CoupledColonySimulation.Create(MixedAtmospheres.Oni.CoupledPresentationScenario.Import(16, 12, 3, 2, 7),
                MixedAtmospheres.Oni.CoupledPresentationScenario.Configuration(16, 12), presentationModel: Model());
            var frame = simulation.AdvanceFrame(1, .03); var target = frame.Presentation!.Frame.SampleCell(3 + 2 * 16);
            check(target.GasConstituents.Count == 6 && target.GasConstituents.Count(row => !row.IsTrace) == 4 &&
                target.GasConstituents.Single(row => row.MaterialId == "chlorine").IsTrace &&
                target.GasConstituents.Single(row => row.MaterialId == "helium").IsTrace &&
                target.Warnings.Any(warning => warning.Contains("chlorine", StringComparison.OrdinalIgnoreCase)),
                "Planned game scene lacks four bulk gases or explicit trace warnings");
            var later = simulation.AdvanceFrame(2, .03).Presentation!.Frame.SampleCell(3 + 2 * 16);
            check(later.GasConstituents.Count == 6 && later.GasConstituents.Single(row => row.MaterialId == "oxygen").GasMoleFraction !=
                target.GasConstituents.Single(row => row.MaterialId == "oxygen").GasMoleFraction, "Game scene did not change composition");
        });
        test("completed physical mixture readings drive presentation, all constituents and trace warnings", () =>
        {
            var configuration = Configuration(); configuration.Diffusivity = .1;
            using var simulation = CoupledColonySimulation.Create(Import(chemistry: true), configuration, presentationModel: Model());
            var original = simulation.CaptureFrame(); var first = simulation.AdvanceFrame(1, .001); var frame = simulation.AdvanceFrame(2, .001);
            var publication = frame.Presentation!;
            check(ReferenceEquals(publication.Source, frame.Projection) && ReferenceEquals(publication.PhysicalSource, frame) &&
                publication.Frame.CompletedFrame == 2 && publication.Frame.SimulationTimeSeconds == frame.SimulationTimeSeconds,
                "Presentation did not bind the identical physical frame");
            foreach (var entry in publication.Frame.Cells)
            {
                var physical = frame.SampleCell(entry.Key).PhysicalState; var reading = entry.Value; var facts = reading.ThermodynamicReading!;
                check(reading.Revision == frame.Projection.Revision && reading.TemperatureKelvin == physical.TemperatureKelvin &&
                    reading.GasPressurePa == physical.PressurePa && facts.StoredInternalEnergyJoules == physical.InternalEnergyJoules &&
                    facts.MomentumX == physical.MomentumX && facts.MomentumY == physical.MomentumY,
                    "Presentation reconstructed physical U, T or p from the Core projection");
                check(reading.Constituents.Count == physical.MassesKg.Count && reading.Constituents.All(row => row.MassKg == physical.MassesKg[row.MaterialId]),
                    "Physical constituent or trace was dropped");
            }
            var target = publication.Frame.SampleCell(3 + 2 * 16);
            check(target.GasConstituents.Count == 4 && target.GasConstituents.Single(row => row.MaterialId == "chlorine").IsTrace &&
                target.Warnings.Any(warning => warning.Contains("chlorine", StringComparison.OrdinalIgnoreCase)),
                "Imperceptible transported contaminant lacks its complete row and warning");
            check(original.Presentation!.Frame.CompletedFrame == 0 && first.Presentation!.Frame.CompletedFrame == 1,
                "Later publication changed a retained presentation");
        });
        test("pure-liquid mechanical pressure and native energy survive appearance recapture", () =>
        {
            const int global = 3 + 2 * 16;
            using var simulation = CoupledColonySimulation.Create(Import(water: true), Configuration(),
                new Dictionary<int, double> { [global] = 100000 }, Model());
            var frame = simulation.CaptureFrame(); var publication = frame.Presentation!;
            var before = publication.Frame.SampleCell(global);
            check(before.GasPressurePa == 0 && before.MechanicalPressurePa == 100000 && before.GasConstituents.Count == 0 &&
                before.ThermodynamicReading!.StoredInternalEnergyJoules == frame.SampleCell(global).PhysicalState.InternalEnergyJoules,
                "Liquid pressure was lost or relabelled as gas pressure");
            var changed = publication.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false, PatternContrast = .12 });
            var after = changed.Frame.SampleCell(global);
            check(ReferenceEquals(changed.PhysicalSource, frame) && ReferenceEquals(changed.Source, publication.Source) &&
                changed.Model.IsVisualVariantOf(publication.Model) && !changed.Model.VisualOptions.PatternEnabled &&
                after.MechanicalPressurePa == before.MechanicalPressurePa && after.ThermodynamicReading!.StoredInternalEnergyJoules == before.ThermodynamicReading!.StoredInternalEnergyJoules,
                "Appearance changed physical source or reconstructed its readings");
        });
        test("physics and prepared presentation remain paired through all-world rejection", () =>
        {
            var model = Model(); using var simulation = CoupledColonySimulation.Create(Import(uniform: true), Configuration(), presentationModel: model);
            var before = simulation.CaptureFrame(); reject(() => simulation.AdvanceFrame(1, .001, 1));
            check(ReferenceEquals(before, simulation.CaptureFrame()) && ReferenceEquals(before.Presentation, simulation.CaptureFrame().Presentation),
                "Rejected physics exposed a new presentation");
            var after = simulation.AdvanceFrame(1, .001, 2);
            check(ReferenceEquals(after.Presentation!.PhysicalSource, after) && after.Presentation.Frame.Revision == after.Projection.Revision,
                "Completed physics and its prepared presentation were published separately");
            var wrong = new FluidMaterialCatalog("unrelated-model", before.MaterialCatalog.Materials.Values);
            reject(() => CoupledColonySimulation.Create(Import(), Configuration(), presentationModel: Model(wrong)).Dispose());
        });
    }
}
