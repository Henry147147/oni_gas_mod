using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using MixedAtmospheres.Core;

internal static class MutationTests
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Reject(Action action)
    {
        var rejected = false; try { action(); } catch { rejected = true; }
        Check(rejected, "Invalid physical mutation accepted.");
    }
    private static CoupledWorldConfiguration Configuration() => new() {
        Width = 2, Height = 2, Gravity = 9.81, Diffusivity = 0, Viscosity = 0, ThermalConduction = false, MaximumTimestepSeconds = .001 };
    private static CoupledCellInitialization Copy(CoupledCellSnapshot cell, double? u = null, double? px = null,
        IReadOnlyDictionary<string, double>? masses = null, IReadOnlyDictionary<int, double>? disease = null, double? radiation = null)
        => new CoupledCellInitialization(masses ?? cell.MassesKg, u ?? cell.InternalEnergyJoules, px ?? cell.MomentumX, cell.MomentumY, cell.PressurePa)
            .WithPassiveMetadata(disease ?? cell.DiseaseCounts, radiation ?? cell.RadiationQuantity);
    private static double K(double mass, double p) => p * p / (2 * mass);
    internal static void Run(IReadOnlyDictionary<string, CoupledMaterialDefinition> definitions, Action<string, Action> test)
    {
        CoupledCellInitialization Cell(int index)
        {
            var alpha = .1 + .05 * index; var temperature = 298 + 2 * index;
            var n = 100000 * (1 - alpha) / (FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * temperature);
            var masses = new Dictionary<string, double> { ["nitrogen"] = .79 * n * definitions["nitrogen"].MolarMassKgPerMol,
                ["oxygen"] = .21 * n * definitions["oxygen"].MolarMassKgPerMol,
                ["water"] = alpha * definitions["water"].CondensedDensityKgPerCubicMetre, ["helium"] = 1e-30 };
            var u = masses.Sum(entry => entry.Value * (definitions[entry.Key].EnergyOffsetJoulesPerKg + definitions[entry.Key].SpecificHeatCv * temperature));
            var result = new CoupledCellInitialization(masses, u);
            return index == 0 ? result.WithPassiveMetadata(new Dictionary<int, double> { [int.MinValue] = 1e-30 }, .125) : result;
        }
        CoupledSimulationWorld World() => CoupledSimulationWorld.Create(Configuration(), Enumerable.Range(0, 4).Select(Cell).ToArray());
        var zero = new CoupledMutationBudget(new Dictionary<string, double>());
        test("physical edits retain exact U/P, source revision and complete immutable metadata", () =>
        {
            using var source = World(); var before = source.CaptureSnapshot(); var cell = before.Cells[0];
            var p = BitConverter.UInt64BitsToDouble(0x3fd999999999999cUL);
            Check(p / cell.TotalMassKg * cell.TotalMassKg != p, "Known momentum counterexample changed.");
            var budget = new CoupledMutationBudget(new Dictionary<string, double>(), shaftWorkJoules: K(cell.TotalMassKg, p), heatJoules: 1024, momentumX: p);
            var request = new[] { new CoupledCellEdit(2, Copy(before.Cells[2])), new CoupledCellEdit(0, Copy(cell, u: cell.InternalEnergyJoules + 1024, px: p)) };
            var mutation = source.PrepareMutation(0, request, budget); using var child = mutation.World; var after = child.CaptureSnapshot();
            Check(after.Revision == 1 && after.CompletedTimeSeconds == before.CompletedTimeSeconds && mutation.Diagnostics.EditedCells == 2 &&
                ReferenceEquals(mutation.Budget, budget), "Physical mutation publication differs.");
            Check(after.Cells[0].InternalEnergyJoules == request[1].Value.InternalEnergyJoules && after.Cells[0].MomentumX == p &&
                after.Cells[0].DiseaseCounts[int.MinValue] == 1e-30 && after.Cells[0].RadiationQuantity == .125,
                "Primary bits or passive metadata changed.");
            Check(source.CaptureSnapshot().Revision == 0 && source.CaptureSnapshot().Cells[0].MomentumX == 0, "Source was mutated.");
            Check(Math.Abs(mutation.Diagnostics.EnergyResidualJoules) <= mutation.Diagnostics.EnergyBoundJoules &&
                mutation.Diagnostics.MaximumSpeciesMassBoundRatio <= 1 && mutation.Diagnostics.MaximumPassiveBoundRatio <= 1,
                "Mutation accounting check differs.");
            using var advanced = child.Advance(1.0 / 65536).World;
            Check(advanced.CaptureSnapshot().Revision == 2 && advanced.CaptureSnapshot().CompletedTimeSeconds == 1.0 / 65536,
                "Solver did not advance from physical edits.");
        });
        test("physical mutation rejects unaccounted traces, metadata, energy, impulse and erased accounts", () =>
        {
            using var source = World(); var before = source.CaptureSnapshot(); var cell = before.Cells[0];
            void Attempt(CoupledCellInitialization value, CoupledMutationBudget? budget = null)
                => Reject(() => source.PrepareMutation(0, new[] { new CoupledCellEdit(0, value) }, budget ?? zero).World.Dispose());
            var masses = cell.MassesKg.ToDictionary(entry => entry.Key, entry => entry.Value); masses.Remove("helium"); Attempt(Copy(cell, masses: masses));
            Attempt(Copy(cell, disease: new Dictionary<int, double>())); Attempt(Copy(cell, u: cell.InternalEnergyJoules + 1024)); Attempt(Copy(cell, px: 1));
            Attempt(Copy(cell), new CoupledMutationBudget(new Dictionary<string, double> { ["helium"] = 1e-200 }));
            Attempt(Copy(cell), new CoupledMutationBudget(new Dictionary<string, double>(), diseaseChanges: new Dictionary<int, double> { [int.MinValue] = 1e-200 }));
            Attempt(Copy(cell), new CoupledMutationBudget(new Dictionary<string, double>(), heatJoules: 1e-200));
            Check(source.CaptureSnapshot().Revision == before.Revision && source.CaptureSnapshot().Cells[0].MassesKg["helium"] == 1e-30,
                "Rejected boundary escaped into source.");
        });
        test("physical mutation owns signed budgets and explicitly accounts metadata removal", () =>
        {
            var masses = new Dictionary<string, double>(); var disease = new Dictionary<int, double> { [int.MinValue] = -1e-30 };
            var budget = new CoupledMutationBudget(masses, diseaseChanges: disease, radiationChange: -.125);
            masses.Add("not-a-material", 1); disease[int.MinValue] = 999;
            Check(budget.MassChangesKg.Count == 0 && budget.DiseaseChanges[int.MinValue] == -1e-30, "Budget borrowed caller storage.");
            using var source = World(); var before = source.CaptureSnapshot();
            var mutation = source.PrepareMutation(0, new[] { new CoupledCellEdit(0, Copy(before.Cells[0], disease: new Dictionary<int, double>(), radiation: 0)) }, budget);
            using var child = mutation.World;
            Check(child.CaptureSnapshot().Cells[0].DiseaseCounts.Count == 0 && child.CaptureSnapshot().Cells[0].RadiationQuantity == 0 &&
                source.CaptureSnapshot().Cells[0].DiseaseCounts[int.MinValue] == 1e-30, "Signed metadata account did not preserve source.");
        });
        test("physical mutation validates stale, duplicate, unknown and late invalid inputs without publication", () =>
        {
            using var source = World(); var before = source.CaptureSnapshot(); var edit = new CoupledCellEdit(0, Copy(before.Cells[0]));
            Reject(() => source.PrepareMutation(1, new[] { edit }, zero).World.Dispose());
            Reject(() => source.PrepareMutation(0, new[] { edit, edit }, zero).World.Dispose());
            Reject(() => source.PrepareMutation(0, Array.Empty<CoupledCellEdit>(), zero).World.Dispose());
            Reject(() => source.PrepareMutation(0, new[] { new CoupledCellEdit(4, edit.Value) }, zero).World.Dispose());
            Reject(() => source.PrepareMutation(0, new[] { edit }, new CoupledMutationBudget(new Dictionary<string, double> { ["unknown"] = 1 })).World.Dispose());
            Reject(() => new CoupledCellEdit(0, CoupledCellInitialization.Wall()));
            Reject(() => new CoupledMutationBudget(new Dictionary<string, double>(), heatJoules: double.NaN));
            Reject(() => new CoupledMutationBudget(new Dictionary<string, double>(), diseaseChanges: new Dictionary<int, double> { [0] = 1 }));
            Reject(() => source.PrepareMutation(0, new[] { edit, new CoupledCellEdit(3, Copy(before.Cells[3], u: -1e300)) }, zero).World.Dispose());
            Check(source.CaptureSnapshot().Revision == 0 && source.CaptureSnapshot().Cells[3].InternalEnergyJoules == before.Cells[3].InternalEnergyJoules,
                "Late rejection published an incomplete physical edit.");
        });
        test("physical mutation retains liquid mechanical pressure and allows independent concurrent forks", () =>
        {
            var mass = definitions["water"].CondensedDensityKgPerCubicMetre;
            var u = mass * (definitions["water"].EnergyOffsetJoulesPerKg + definitions["water"].SpecificHeatCv * 300);
            var water = new CoupledCellInitialization(new Dictionary<string, double> { ["water"] = mass }, u, liquidPressurePa: 100000);
            using var source = CoupledSimulationWorld.Create(Configuration(), Enumerable.Repeat(water, 4).ToArray());
            var request = new[] { new CoupledCellEdit(0, new CoupledCellInitialization(water.MassesKg, water.InternalEnergyJoules, liquidPressurePa: 200000)) };
            Task.WhenAll(Enumerable.Range(0, 8).Select(_ => Task.Run(() => {
                var mutation = source.PrepareMutation(0, request, zero); using var child = mutation.World; var captured = child.CaptureSnapshot();
                Check(captured.Cells[0].PressurePa == 200000 && Math.Abs(captured.Cells[0].TemperatureKelvin - 300) <= 2e-10 * 300 && captured.Cells[0].MassesKg["water"] == mass,
                    "Liquid pressure or caloric state was reconstructed incorrectly.");
            }))).GetAwaiter().GetResult();
            Check(source.CaptureSnapshot().Revision == 0 && source.CaptureSnapshot().Cells[0].PressurePa == 100000, "Concurrent forks changed source.");
        });
    }
}
