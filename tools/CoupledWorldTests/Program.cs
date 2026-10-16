using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading.Tasks;
using MixedAtmospheres.Core;

if (args.Length != 1) throw new ArgumentException("Supply the exact coupled native library path.");
var library = Path.GetFullPath(args[0]); var module = NativeLibrary.Load(library);
NativeLibrary.SetDllImportResolver(typeof(CoupledSimulationWorld).Assembly, (name, _, _) => name == "mixed_atmospheres_coupled" ? module : IntPtr.Zero);
Console.WriteLine("Coupled native library SHA256: " + Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(library))).ToLowerInvariant());
var definitions = CoupledSimulationWorld.ReadMaterialDefinitions().ToDictionary(value => value.Id);
MutationTests.Run(definitions, (name, action) => { action(); Console.WriteLine("PASS " + name); });
void Check(bool condition, string reason) { if (!condition) throw new Exception(reason); }
void Near(double a, double b, double scale, double tolerance = 1e-10) { Check(double.IsFinite(a) && double.IsFinite(b) && Math.Abs(a - b) <= tolerance * scale, "Independent coupled value differs"); }
void Reject(Action action) { var rejected = false; try { action(); } catch { rejected = true; } Check(rejected, "Invalid operation accepted"); }
void Test(string name, Action action) { action(); Console.WriteLine("PASS " + name); }
CoupledWorldConfiguration Config() => new() { Width = 2, Height = 2, Gravity = 0, Diffusivity = 0, Viscosity = 0, ThermalConduction = false, MaximumTimestepSeconds = .001 };
CoupledCellInitialization Cell(double alpha = .25, double temperature = 300, double px = 0)
{
    var n = 100000 * (1 - alpha) / (FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * temperature);
    var masses = new Dictionary<string, double> { ["nitrogen"] = .79 * n * definitions["nitrogen"].MolarMassKgPerMol,
        ["oxygen"] = .21 * n * definitions["oxygen"].MolarMassKgPerMol, ["water"] = alpha * definitions["water"].CondensedDensityKgPerCubicMetre, ["helium"] = 1e-30 };
    var energy = masses.Sum(entry => entry.Value * (definitions[entry.Key].EnergyOffsetJoulesPerKg + definitions[entry.Key].SpecificHeatCv * temperature));
    return new CoupledCellInitialization(masses, energy, px);
}
CoupledSimulationWorld World(bool uniform = true) => CoupledSimulationWorld.Create(Config(), Enumerable.Range(0, 4).Select(i => Cell(uniform ? .25 : .1 + .05 * i, uniform ? 300 : 298 + 2 * i)).ToArray());
bool Same(CoupledWorldSnapshot a, CoupledWorldSnapshot b) => a.Revision == b.Revision && a.CompletedTimeSeconds == b.CompletedTimeSeconds &&
    a.InternalEnergyJoules == b.InternalEnergyJoules && a.KineticEnergyJoules == b.KineticEnergyJoules && a.PotentialEnergyJoules == b.PotentialEnergyJoules &&
    a.Cells.Zip(b.Cells).All(pair => pair.First.InternalEnergyJoules == pair.Second.InternalEnergyJoules && pair.First.MomentumX == pair.Second.MomentumX &&
        pair.First.MomentumY == pair.Second.MomentumY && pair.First.PressurePa == pair.Second.PressurePa && pair.First.TemperatureKelvin == pair.Second.TemperatureKelvin &&
        pair.First.LiquidVolumeFraction == pair.Second.LiquidVolumeFraction && pair.First.TotalMassKg == pair.Second.TotalMassKg && pair.First.IsWall == pair.Second.IsWall &&
        pair.First.MassesKg.Count == pair.Second.MassesKg.Count && pair.First.MassesKg.All(entry => pair.Second.MassesKg.TryGetValue(entry.Key, out var mass) && mass == entry.Value) &&
        pair.First.RadiationQuantity == pair.Second.RadiationQuantity && pair.First.DiseaseCounts.Count == pair.Second.DiseaseCounts.Count &&
        pair.First.DiseaseCounts.All(entry => pair.Second.DiseaseCounts.TryGetValue(entry.Key, out var count) && count == entry.Value));

Test("coupled model identity, owned initialization and immutable configuration", () =>
{
    Check(definitions["oxygen"].Phase == CoupledMaterialPhase.Gas && definitions["water"].Phase == CoupledMaterialPhase.Liquid, "Material phase differs");
    var configuration = Config(); var cells = Enumerable.Repeat(Cell(px: .7000000000000001), 4).ToArray();
    using var world = CoupledSimulationWorld.Create(configuration, cells); var before = world.CaptureSnapshot();
    Check(before.Cells[0].MomentumX == cells[0].MomentumX && before.Cells[0].InternalEnergyJoules == cells[0].InternalEnergyJoules, "Import changed primary state");
    cells[0] = CoupledCellInitialization.Wall(); configuration.Width = 99;
    Check(world.Width == 2 && Same(before, world.CaptureSnapshot()), "Caller initialization mutation escaped");
    Reject(() => ((IDictionary<string, double>)before.Cells[0].MassesKg)["helium"] = 0);
});
Test("readings and every constituent come from one completed state", () =>
{
    using var world = World(false); var snapshot = world.CaptureSnapshot();
    Check(snapshot.Revision == 0 && snapshot.CompletedTimeSeconds == 0 && snapshot.Cells.Count == 4, "Initial publication differs");
    for (var i = 0; i < 4; ++i)
    {
        var cell = snapshot.Cells[i]; var liquid = cell.MassesKg["water"] / definitions["water"].CondensedDensityKgPerCubicMetre;
        var moles = cell.MassesKg.Where(entry => definitions[entry.Key].Phase == CoupledMaterialPhase.Gas).Sum(entry => entry.Value / definitions[entry.Key].MolarMassKgPerMol);
        Near(cell.TemperatureKelvin, 298 + 2 * i, 300); Near(cell.PressurePa, moles * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * cell.TemperatureKelvin / (1 - liquid), cell.PressurePa);
        Near(cell.LiquidVolumeFraction, liquid, liquid); Check(cell.MassesKg["helium"] == 1e-30, "Initialization trace erased");
    }
});
Test("coupled 2D gas-liquid advance, traces and separate immutable source", () =>
{
    using var source = World(false); var before = source.CaptureSnapshot(); var advanced = source.Advance(1.0 / 1024);
    using var child = advanced.World; var after = child.CaptureSnapshot();
    Check(after.Revision == 1 && after.CompletedTimeSeconds == 1.0 / 1024 && advanced.Diagnostics.Revision == after.Revision &&
        advanced.Diagnostics.CompletedTimeSeconds == after.CompletedTimeSeconds && Same(before, source.CaptureSnapshot()), "Mixed revision or modified source");
    foreach (var id in before.Cells.SelectMany(cell => cell.MassesKg.Keys).Distinct())
    {
        var a = before.Cells.Sum(cell => cell.MassesKg.TryGetValue(id, out var mass) ? mass : 0);
        var b = after.Cells.Sum(cell => cell.MassesKg.TryGetValue(id, out var mass) ? mass : 0); Near(a, b, Math.Max(a, b));
    }
    var heat = before.Cells.Sum(cell => cell.MassesKg.Sum(entry => entry.Value * definitions[entry.Key].SpecificHeatCv * cell.TemperatureKelvin));
    Near(before.InternalEnergyJoules + before.KineticEnergyJoules, after.InternalEnergyJoules + after.KineticEnergyJoules, heat, 1e-8);
    Check(after.Cells.All(cell => cell.MassesKg["helium"] > 0), "Transport discarded trace");
});
Test("whole-call budget failure preserves source and permits a later valid fork", () =>
{
    using var source = World(); var before = source.CaptureSnapshot();
    var rejected = false; try { source.Advance(.003, 1).World.Dispose(); } catch (FluidSimulationException error) { rejected = error.Status == 2; }
    Check(rejected && Same(before, source.CaptureSnapshot()), "Failed partial advance escaped");
    using var child = source.Advance(.001, 1).World; Check(child.CaptureSnapshot().Revision == 1, "Failed source could not advance later");
    Reject(() => source.Advance(double.NaN)); Reject(() => source.Advance(0)); Reject(() => source.Advance(.001, 0));
    Check(Same(before, source.CaptureSnapshot()), "Invalid request changed source");
});
Test("retained snapshots and independent forks outlive disposal and permit concurrent inspection", () =>
{
    var source = World(); var before = source.CaptureSnapshot(); var fork = source.Advance(.001); source.Dispose(); source.Dispose();
    using var child = fork.World; Reject(() => source.CaptureSnapshot()); Reject(() => source.Advance(.001));
    var expected = child.CaptureSnapshot();
    Task.WhenAll(Enumerable.Range(0, 8).Select(_ => Task.Run(() => { for (var i = 0; i < 8; ++i) Check(Same(expected, child.CaptureSnapshot()), "Concurrent inspection differs"); }))).GetAwaiter().GetResult();
    Check(before.Revision == 0 && before.Cells[0].MassesKg["helium"] == 1e-30, "Retained managed frame damaged");
});
Test("unsupported vacuum, unknown species, overfill and invalid configuration reject", () =>
{
    var cells = Enumerable.Repeat(Cell(), 4).ToArray();
    cells[3] = new CoupledCellInitialization(new Dictionary<string, double>(), 0); Reject(() => CoupledSimulationWorld.Create(Config(), cells).Dispose());
    cells[3] = new CoupledCellInitialization(new Dictionary<string, double> { ["unmodeled"] = 1 }, 100); Reject(() => CoupledSimulationWorld.Create(Config(), cells).Dispose());
    cells[3] = new CoupledCellInitialization(new Dictionary<string, double> { ["water"] = 2000 }, 100); Reject(() => CoupledSimulationWorld.Create(Config(), cells).Dispose());
    cells[3] = CoupledCellInitialization.Wall(); using var valid = CoupledSimulationWorld.Create(Config(), cells); Check(valid.CaptureSnapshot().Cells[3].IsWall, "Solid cell not retained");
    var config = Config(); config.Preconditioner = (CoupledPressurePreconditioner)99; Reject(() => CoupledSimulationWorld.Create(config, cells).Dispose());
    config = Config(); config.Width = uint.MaxValue; Reject(() => CoupledSimulationWorld.Create(config, cells).Dispose());
});
Test("sparse signed disease identities and radiation follow actual coupled transport", () =>
{
    var input = Enumerable.Range(0, 4).Select(i => Cell(px: .1).WithPassiveMetadata(
        new Dictionary<int, double> { [i == 0 ? int.MinValue : 17 + i] = i == 0 ? 1e-30 : 1000 + i }, .125 * (i + 1))).ToArray();
    using var world = CoupledSimulationWorld.Create(Config(), input); var before = world.CaptureSnapshot();
    Check(before.Cells[0].DiseaseCounts[int.MinValue] == 1e-30, "Signed disease identity or tiny count changed");
    using var child = world.Advance(1.0 / 1024).World; var after = child.CaptureSnapshot();
    Check(Same(before, world.CaptureSnapshot()), "Passive advance changed source");
    foreach (var identity in before.Cells.SelectMany(cell => cell.DiseaseCounts.Keys).Distinct())
    {
        var a = before.Cells.Sum(cell => cell.DiseaseCounts.TryGetValue(identity, out var count) ? count : 0);
        var b = after.Cells.Sum(cell => cell.DiseaseCounts.TryGetValue(identity, out var count) ? count : 0);
        Near(a, b, Math.Max(a, b)); Check(b > 0, "A disease identity disappeared");
    }
    Near(before.Cells.Sum(cell => cell.RadiationQuantity), after.Cells.Sum(cell => cell.RadiationQuantity), before.Cells.Sum(cell => cell.RadiationQuantity));
    Check(after.Cells.Where((cell, i) => cell.DiseaseCounts.Keys.Any(id => !before.Cells[i].DiseaseCounts.ContainsKey(id))).Any(), "Metadata did not follow material flow");
});
Test("passive initialization, rollback, immutable reads and nonfluid ownership", () =>
{
    var disease = new Dictionary<int, double> { [-42] = 3, [17] = 1e-30 };
    var cell = Cell().WithPassiveMetadata(disease, .25); disease[-42] = 999;
    using var world = CoupledSimulationWorld.Create(Config(), Enumerable.Repeat(cell, 4).ToArray()); var before = world.CaptureSnapshot();
    Check(before.Cells[0].DiseaseCounts[-42] == 3 && before.Cells[0].RadiationQuantity == .25, "Caller metadata mutation escaped");
    Reject(() => world.Advance(.003, 1)); Check(Same(before, world.CaptureSnapshot()), "Failed step changed passive state");
    Reject(() => ((IDictionary<int, double>)before.Cells[0].DiseaseCounts)[17] = 0);
    Reject(() => Cell().WithPassiveMetadata(new Dictionary<int, double> { [0] = 1 }));
    Reject(() => Cell().WithPassiveMetadata(new Dictionary<int, double> { [17] = double.NaN }));
    Reject(() => CoupledCellInitialization.Wall().WithPassiveMetadata(new Dictionary<int, double> { [17] = 1 }));
    Reject(() => Cell().WithPassiveMetadata(new Dictionary<int, double>(), -1));
    using var child = world.Advance(.001, 1).World; world.Dispose();
    Check(child.CaptureSnapshot().Cells[0].DiseaseCounts[-42] == 3 && before.Cells[0].DiseaseCounts[17] == 1e-30, "Disposed parent damaged metadata view");
});
ColonyCoordinatorTests.Run(Test, Check, Reject, (a, b, scale) => Near(a, b, scale));
ColonyCoordinatorTests.RunPresentation(Test, Check, Reject);
ColonyCoordinatorTests.RunMutations(Test, Check, Reject);
