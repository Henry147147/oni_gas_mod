using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using MixedAtmospheres.Core;

internal static partial class ColonyCoordinatorTests
{
    private static CoupledWorldMutation Heat(CoupledColonyFrame source, int world, double heat)
    {
        var cell = source.Worlds[world].Cells[0];
        var value = new CoupledCellInitialization(cell.MassesKg, cell.InternalEnergyJoules + heat, cell.MomentumX, cell.MomentumY, cell.PressurePa)
            .WithPassiveMetadata(cell.DiseaseCounts, cell.RadiationQuantity);
        return new CoupledWorldMutation(world, new[] { new CoupledCellEdit(0, value) },
            new CoupledMutationBudget(new Dictionary<string, double>(), heatJoules: heat));
    }
    internal static void RunMutations(Action<string, Action> test, Action<bool, string> check, Action<Action> reject)
    {
        test("colony frame commits native edits, all-world advances, boundary accounts and physical presentation together", () =>
        {
            using var simulation = CoupledColonySimulation.Create(Import(uniform: true), Configuration(), presentationModel: Model());
            var before = simulation.CaptureFrame(); var cell = before.Worlds[7].Cells[0]; var oxygen = before.Materials.Single(value => value.Id == "oxygen");
            var masses = cell.MassesKg.ToDictionary(entry => entry.Key, entry => entry.Value); masses.Add("oxygen", .125);
            var parcelU = .125 * (oxygen.EnergyOffsetJoulesPerKg + oxygen.SpecificHeatCv * 310);
            var value = new CoupledCellInitialization(masses, cell.InternalEnergyJoules + parcelU + 128, cell.MomentumX, cell.MomentumY, cell.PressurePa)
                .WithPassiveMetadata(cell.DiseaseCounts, cell.RadiationQuantity);
            var inlet = new CoupledWorldMutation(7, new[] { new CoupledCellEdit(0, value) }, new CoupledMutationBudget(
                new Dictionary<string, double> { ["oxygen"] = .125 }, materialEnergyJoules: parcelU, heatJoules: 128));
            var after = simulation.AdvanceFrame(before, 1, 1.0 / 65536, new[] { Heat(before, 9, 256), inlet }, 2);
            check(after.CompletedFrame == 1 && after.Worlds.Values.All(world => world.Revision == 2 && world.CompletedTimeSeconds == after.SimulationTimeSeconds) &&
                after.BoundaryChanges.Count == 2 && after.Diagnostics.Count == 2, "All-world mutation clock differs");
            check(after.BoundaryChanges[7].SourceRevision == 0 && after.BoundaryChanges[7].PreparedRevision == 1 &&
                after.BoundaryChanges[7].Diagnostics.EditedCells == 1 && ReferenceEquals(after.BoundaryChanges[7].Budget, inlet.Budget), "Mutation receipt changed");
            check(after.BoundaryLedger.MassChangesKg["oxygen"].Binary64Units == CoupledAccountQuantity.FromDouble(.125).Binary64Units &&
                after.BoundaryLedger.HeatJoules.Binary64Units == CoupledAccountQuantity.FromDouble(384).Binary64Units &&
                after.BoundaryLedger.MaterialEnergyJoules.Binary64Units == CoupledAccountQuantity.FromDouble(parcelU).Binary64Units,
                "Physical external accounts were lost or relabelled as heat");
            check(before.BoundaryChanges.Count == 0 && before.BoundaryLedger.MassChangesKg.Count == 0 &&
                before.Worlds.Values.All(world => world.Revision == 0), "Source accounts or native frames changed");
            check(after.Projection.IsReadOnlyPhysicalProjection && ReferenceEquals(after.Presentation!.PhysicalSource, after), "Physical presentation source differs");
            foreach (var entry in after.Presentation!.Frame.Cells)
            {
                var physical = after.SampleCell(entry.Key).PhysicalState; var reading = entry.Value;
                check(reading.ThermodynamicReading!.StoredInternalEnergyJoules == physical.InternalEnergyJoules &&
                    reading.TemperatureKelvin == physical.TemperatureKelvin && reading.GasPressurePa == physical.PressurePa,
                    "Edited world presentation used projected physical values");
            }
            reject(() => _ = after.Projection.InitialInventory); reject(() => _ = after.Projection.SuppliedInventory);
            reject(() => _ = after.Projection.RemovedInventory); reject(() => _ = after.Projection.ExternalHeatJoules);
            reject(() => ((IDictionary<int, CoupledBoundaryReceipt>)after.BoundaryChanges).Clear());
        });
        test("later-world unaccounted metadata rejects earlier completed edits and preserves one publication", () =>
        {
            using var simulation = CoupledColonySimulation.Create(Import(uniform: true), Configuration(), presentationModel: Model());
            var before = simulation.CaptureFrame(); var cell = before.Worlds[9].Cells[0];
            var value = new CoupledCellInitialization(cell.MassesKg, cell.InternalEnergyJoules, cell.MomentumX, cell.MomentumY, cell.PressurePa)
                .WithPassiveMetadata(new Dictionary<int, double>(), cell.RadiationQuantity);
            var bad = new CoupledWorldMutation(9, new[] { new CoupledCellEdit(0, value) }, new CoupledMutationBudget(new Dictionary<string, double>()));
            var rejected = false;
            try { simulation.AdvanceFrame(before, 1, .001, new[] { Heat(before, 7, 1), bad }, 2); }
            catch (CoupledColonyAdvanceException error) { rejected = error.CompletedWorldCandidates == 1 && error.AttemptedFrame == 1; }
            check(rejected && ReferenceEquals(before, simulation.CaptureFrame()) && before.BoundaryLedger.HeatJoules.Binary64Units.IsZero &&
                before.Worlds.Values.All(world => world.Revision == 0) && ReferenceEquals(before.Presentation, simulation.CaptureFrame().Presentation),
                "Late native mutation failure published an earlier world or account");
            var corrected = new CoupledWorldMutation(9, bad.Edits, new CoupledMutationBudget(new Dictionary<string, double>(),
                diseaseChanges: cell.DiseaseCounts.ToDictionary(entry => entry.Key, entry => -entry.Value)));
            var after = simulation.AdvanceFrame(before, 1, .001, new[] { Heat(before, 7, 1), corrected }, 2);
            check(after.BoundaryLedger.HeatJoules.Binary64Units == CoupledAccountQuantity.FromDouble(1).Binary64Units &&
                after.BoundaryLedger.DiseaseChanges[int.MinValue].Binary64Units == CoupledAccountQuantity.FromDouble(-25).Binary64Units,
                "Rejected mutation was counted or corrected metadata account was lost");
        });
        test("shared step budget rejects edited frame atomically and does not charge rejected accounts", () =>
        {
            using var simulation = CoupledColonySimulation.Create(Import(uniform: true), Configuration()); var before = simulation.CaptureFrame();
            var edits = new[] { Heat(before, 7, 1) }; var rejected = false;
            try { simulation.AdvanceFrame(before, 1, .001, edits, 1); }
            catch (CoupledColonyAdvanceException error) { rejected = error.CompletedWorldCandidates == 1; }
            check(rejected && ReferenceEquals(before, simulation.CaptureFrame()) && before.BoundaryChanges.Count == 0, "Partial mutation escaped a shared attempt budget");
            var after = simulation.AdvanceFrame(before, 1, .001, edits, 2);
            check(after.Worlds[7].Revision == 2 && after.Worlds[9].Revision == 1 && after.BoundaryChanges.Count == 1 &&
                after.BoundaryLedger.HeatJoules.Binary64Units == CoupledAccountQuantity.FromDouble(1).Binary64Units, "Failed frame charged a boundary account");
            var next = simulation.AdvanceFrame(2, .001, 2);
            check(next.Worlds[7].Revision == 3 && next.Worlds[9].Revision == 2 && next.BoundaryChanges.Count == 0 &&
                ReferenceEquals(next.BoundaryLedger, after.BoundaryLedger), "Mutation-free frame reset native owners or accounts");
        });
        test("colony mutation inputs are owned and reject stale, foreign, duplicate and unowned plans", () =>
        {
            using var simulation = CoupledColonySimulation.Create(Import(uniform: true), Configuration()); var before = simulation.CaptureFrame();
            using var foreign = CoupledColonySimulation.Create(Import(uniform: true), Configuration());
            var plan = Heat(before, 7, 1); var edits = plan.Edits.ToArray(); var owned = new CoupledWorldMutation(7, edits, plan.Budget); edits[0] = null!;
            check(owned.Edits[0] != null, "World mutation borrowed its caller's edit list");
            reject(() => ((IList<CoupledCellEdit>)owned.Edits).Clear());
            reject(() => simulation.AdvanceFrame(foreign.CaptureFrame(), 1, .001, new[] { owned }, 2));
            reject(() => simulation.AdvanceFrame(before, 1, .001, new[] { owned, owned }, 2));
            reject(() => simulation.AdvanceFrame(before, 1, .001, new[] { new CoupledWorldMutation(1234, owned.Edits, owned.Budget) }, 2));
            reject(() => simulation.AdvanceFrame(before, 1, .001, new[] { new CoupledWorldMutation(7,
                new[] { new CoupledCellEdit(4, owned.Edits[0].Value) }, owned.Budget) }, 2));
            check(ReferenceEquals(before, simulation.CaptureFrame()), "Invalid mutation plan changed publication");
            var after = simulation.AdvanceFrame(before, 1, .001, new[] { owned }, 2);
            reject(() => simulation.AdvanceFrame(before, 2, .001, Array.Empty<CoupledWorldMutation>(), 2));
            check(ReferenceEquals(after, simulation.CaptureFrame()), "Stale source was accepted after publication");
        });
        test("game scene physical inlet and heat publish four bulk hover constituents and retained hazardous traces", () =>
        {
            using var simulation = CoupledColonySimulation.Create(MixedAtmospheres.Oni.CoupledPresentationScenario.Import(16, 12, 3, 2, 7),
                MixedAtmospheres.Oni.CoupledPresentationScenario.Configuration(16, 12), presentationModel: Model());
            simulation.AdvanceFrame(1, .03); var before = simulation.AdvanceFrame(2, .03); const int target = 3 + 2 * 16;
            var after = simulation.AdvanceFrame(before, 3, .03, new[] { MixedAtmospheres.Oni.CoupledPresentationScenario.InventoryAddition(before, target) });
            var reading = after.Presentation!.Frame.SampleCell(target); var physical = after.SampleCell(target).PhysicalState;
            check(after.Worlds[7].Revision == 4 && after.BoundaryChanges[7].SourceRevision == 2 && after.BoundaryChanges[7].PreparedRevision == 3 &&
                after.BoundaryLedger.HeatJoules.Binary64Units == CoupledAccountQuantity.FromDouble(4096).Binary64Units, "Planned game mutation did not commit");
            check(reading.GasConstituents.Count == 6 && reading.GasConstituents.Count(row => !row.IsTrace) == 4 &&
                reading.Warnings.Any(warning => warning.Contains("chlorine", StringComparison.OrdinalIgnoreCase)) &&
                reading.ThermodynamicReading!.StoredInternalEnergyJoules == physical.InternalEnergyJoules && reading.TemperatureKelvin == physical.TemperatureKelvin &&
                reading.GasPressurePa == physical.PressurePa && physical.TemperatureKelvin != before.SampleCell(target).PhysicalState.TemperatureKelvin,
                "Planned game mutation changed or hid physical readings");
            var colour = after.Presentation.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false });
            check(ReferenceEquals(colour.PhysicalSource, after) && colour.Frame.SampleCell(target).ThermodynamicReading!.StoredInternalEnergyJoules == physical.InternalEnergyJoules,
                "Appearance recapture discarded edited physical source");
        });
        test("boundary account sums retain tiny operands, cancellation, subnormals and explicit rounding overflow", () =>
        {
            var rows = new (string Name, double[] Input)[] {
                ("tiny-tail", new[] { 1.0, Math.ScaleB(1, -54), -1.0 }),
                ("negative-tail", new[] { -1.0, -Math.ScaleB(1, -54), 1.0 }),
                ("tie-even", new[] { 1.0, Math.ScaleB(1, -53) }),
                ("tie-odd", new[] { 1.0 + Math.ScaleB(1, -52), Math.ScaleB(1, -53) }),
                ("negative-tie", new[] { -1.0 - Math.ScaleB(1, -52), -Math.ScaleB(1, -53) }),
                ("subnormal", new[] { double.Epsilon, double.Epsilon, -double.Epsilon }),
                ("normal-boundary", new[] { Math.ScaleB(1, -1022), -double.Epsilon }),
                ("overflow-cancel", new[] { double.MaxValue, double.MaxValue, -double.MaxValue }),
                ("overflow", new[] { double.MaxValue, double.MaxValue }),
                ("zero", new[] { -0.0, 0.0 }) };
            foreach (var row in rows)
            {
                var account = default(CoupledAccountQuantity);
                foreach (var value in row.Input) account = account.Add(CoupledAccountQuantity.FromDouble(value));
                var representable = account.TryGetRoundedValue(out var rounded);
                Console.WriteLine("ACCOUNT " + JsonSerializer.Serialize(new {
                    row.Name, Input = row.Input.Select(value => unchecked((ulong)BitConverter.DoubleToInt64Bits(value)).ToString("x16", CultureInfo.InvariantCulture)).ToArray(),
                    Units = account.Binary64Units.ToString(CultureInfo.InvariantCulture), Representable = representable,
                    RoundedHex = unchecked((ulong)BitConverter.DoubleToInt64Bits(rounded)).ToString("x16", CultureInfo.InvariantCulture) }));
                if (row.Name == "tiny-tail") check(representable && rounded == Math.ScaleB(1, -54), "Tiny external-account operand was erased");
                if (row.Name == "overflow") check(!representable && account.Binary64Units.Sign > 0, "Account overflow was hidden");
            }
            reject(() => CoupledAccountQuantity.FromDouble(double.NaN)); reject(() => CoupledAccountQuantity.FromDouble(double.PositiveInfinity));
        });
        test("boundary receipts and exact accounts survive native retirement and retain no prior receipt history", () =>
        {
            var simulation = CoupledColonySimulation.Create(Import(uniform: true), Configuration(), presentationModel: Model());
            var before = simulation.CaptureFrame(); var first = simulation.AdvanceFrame(before, 1, .001, new[] { Heat(before, 7, 32) }, 2);
            var second = simulation.AdvanceFrame(first, 2, .001, new[] { Heat(first, 7, -32) }, 2);
            var last = simulation.AdvanceFrame(3, .001, 2); simulation.Dispose();
            check(first.BoundaryLedger.HeatJoules.Binary64Units == CoupledAccountQuantity.FromDouble(32).Binary64Units &&
                second.BoundaryLedger.HeatJoules.Binary64Units.IsZero && last.BoundaryChanges.Count == 0 &&
                ReferenceEquals(second.BoundaryLedger, last.BoundaryLedger), "Cumulative accounts borrowed or retained receipt history");
            Task.WhenAll(Enumerable.Range(0, 8).Select(_ => Task.Run(() => {
                check(first.BoundaryChanges[7].Budget.HeatJoules == 32 && first.BoundaryChanges[7].PreparedRevision == 1 &&
                    first.Presentation!.Frame.SampleCell(3 + 2 * 16).ThermodynamicReading!.StoredInternalEnergyJoules == first.SampleCell(3 + 2 * 16).PhysicalState.InternalEnergyJoules,
                    "Retired native owner invalidated a completed physical read");
            }))).GetAwaiter().GetResult();
            reject(() => simulation.CaptureFrame());
        });
    }
}
