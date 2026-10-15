using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

internal static class ColonyPlacementDiagnosticsTests
{
    internal static void Run(Action<string, Action> test, Action<bool> check, Action<Action> reject,
        Func<byte[]> nativeFixture, Dictionary<int, NativeMatterPhase> suppliedPhases)
    {
        const int width = 32, height = 16;
        var phases = new Dictionary<int, NativeMatterPhase>(suppliedPhases) { [5] = NativeMatterPhase.Liquid };
        var bindings = new Dictionary<int, NativeFluidBinding>
        {
            [2] = new NativeFluidBinding("oxygen", NativeMatterPhase.Gas),
            [3] = new NativeFluidBinding("water", NativeMatterPhase.Liquid),
            [5] = new NativeFluidBinding("chlorine", NativeMatterPhase.Liquid)
        };
        var solid = new Cell(4, 1000); var vacuum = new Cell(1, 0);
        FluidMaterialCatalog Catalog(double volume = 1) => new FluidMaterialCatalog("explicit-nominal-test-v1", new[]
        {
            new FluidMaterial("oxygen", NativeMatterPhase.Gas, 0.032, 658),
            new FluidMaterial("water", NativeMatterPhase.Liquid, 0.018, 4184, 1000 / volume, -100000),
            new FluidMaterial("chlorine", NativeMatterPhase.Liquid, 0.071, 480, 1000 / volume)
        });
        NativeWorldSource Source(int world, int offsetX, params Cell[] cells)
        {
            check(cells.Length == 4);
            var raw = nativeFixture(); Int(raw, 20, offsetX);
            var positions = new[] { 5, 6, 9, 10 };
            for (var i = 0; i < cells.Length; ++i)
            {
                var cell = cells[i]; var offset = 29 + positions[i] * 16;
                Int(raw, offset, cell.Element); Float(raw, offset + 4, cell.Temperature);
                Float(raw, offset + 8, cell.Mass); Float(raw, offset + 12, cell.Radiation);
                var disease = 29 + 16 * 16 + positions[i] * 8;
                Int(raw, disease, cell.DiseaseHash); Int(raw, disease + 4, cell.DiseaseCount);
            }
            var envelope = new byte[raw.Length + 4]; Int(envelope, 0, raw.Length); raw.CopyTo(envelope, 4);
            return new NativeWorldSource(world, envelope);
        }
        NativeColonyImport Import(params NativeWorldSource[] sources) => NativeColonyImport.StageNewColony(width, height,
            sources.Select(source => new NativeWorldPlacement(source.WorldId, 2, 2,
                BinaryPrimitives.ReadInt32LittleEndian(source.LengthPrefixedData.Slice(24, 4)), 7)).ToArray(),
            sources, phases, bindings, 1);
        StagedColonyFluidImport Stage(params Cell[] cells) => ColonyFluidState.StageImport(Import(Source(7, 11, cells)), Catalog());
        void Near(double actual, double expected)
        { check(double.IsFinite(actual) && Math.Abs(actual - expected) <= Math.Max(1e-12, Math.Abs(expected) * 1e-12)); }
        void SameInventory(FluidInventory actual, FluidInventory expected)
        {
            check(actual.MassesKg.Count == expected.MassesKg.Count && actual.DiseaseCounts.Count == expected.DiseaseCounts.Count);
            foreach (var item in expected.MassesKg) Near(actual.MassesKg[item.Key], item.Value);
            foreach (var item in expected.DiseaseCounts) Near(actual.DiseaseCounts[item.Key], item.Value);
            Near(actual.TotalMassKg, expected.TotalMassKg); Near(actual.TotalEnergyJoules, expected.TotalEnergyJoules);
            Near(actual.MomentumX, expected.MomentumX); Near(actual.MomentumY, expected.MomentumY);
            Near(actual.RadiationQuantity, expected.RadiationQuantity);
        }

        test("placement diagnostics accept an exactly full liquid cell without inventing gas headspace", () =>
        {
            var staged = Stage(new Cell(3, 1000), solid, solid, solid);
            var report = ColonyPlacementDiagnostics.Analyze(staged);
            check(report.CanPublishWithoutPlacementChanges && report.Cells.Count == 0 && report.Components.Count == 0);
            check(report.ComponentsThatCannotFitDeclaredReferenceVolume == 0);
            Near(report.MinimumLiquidReferenceVolumeDeficitCubicMetres, 0);
            Near(staged.PublishValidated().CaptureSnapshot().SampleCell(11 + 7 * width).LiquidVolumeFraction, 1);
            SameInventory(report.CapturedSurrogateInventoryTotals, staged.Totals());
            check(report.MaterialModelVersion == staged.MaterialModelVersion && report.MaterialModelFingerprint == staged.MaterialModelFingerprint);
            check(report.EnergyInterpretation.Contains("surrogate") && report.EnergyInterpretation.Contains("native save"));
            reject(() => ColonyPlacementDiagnostics.Analyze(null!));
        });

        test("placement diagnostics describe every offending identity mass temperature and nominal reference", () =>
        {
            var staged = Stage(new Cell(3, 1100, 310, 7, 123, 456), vacuum, solid, solid);
            var report = ColonyPlacementDiagnostics.Analyze(staged); var cell = report.Cells.Single();
            check(!report.CanPublishWithoutPlacementChanges && cell.WorldId == 7 && cell.CellIndex == 11 + 7 * width && cell.X == 11 && cell.Y == 7);
            Near(cell.TemperatureKelvin, 310); Near(cell.CellCapacityCubicMetres, 1);
            Near(cell.LiquidReferenceVolumeCubicMetres, 1.1); Near(cell.ExcessLiquidReferenceVolumeCubicMetres, 0.1);
            var material = cell.Constituents.Single(); check(material.MaterialId == "water" && material.Phase == NativeMatterPhase.Liquid);
            Near(material.MassKg, 1100); Near(material.LiquidReferenceDensityKgPerCubicMetre!.Value, 1000);
            Near(material.LiquidReferenceVolumeCubicMetres!.Value, 1.1);
            check(ReferenceEquals(cell.CapturedSurrogateInventory, staged.Inventories[cell.CellIndex]));
            Near(cell.CapturedSurrogateInventory.TotalEnergyJoules, 1100 * (4184 * 310.0 - 100000));
            var component = report.Components.Single(); check(component.PoreCellCount == 2 && !component.CannotFitDeclaredReferenceVolume);
            Near(component.PoreCapacityCubicMetres, 2); Near(component.LiquidReferenceVolumeCubicMetres, 1.1);
            check(component.IssueCellIndices.SequenceEqual(new[] { cell.CellIndex }));
            reject(() => staged.PublishValidated()); // A necessary aggregate bound never relaxes strict cell publication.
        });

        test("sealed overfill diagnostics exclude solid and outside-world capacity", () =>
        {
            var staged = Stage(new Cell(3, 1400), solid, solid, solid);
            var report = ColonyPlacementDiagnostics.Analyze(staged); var component = report.Components.Single();
            check(component.WorldId == 7 && component.PoreCellCount == 1 && component.CannotFitDeclaredReferenceVolume);
            check(report.ComponentsThatCannotFitDeclaredReferenceVolume == 1 && !component.ContainsGas);
            Near(component.PoreCapacityCubicMetres, 1); Near(component.MinimumLiquidReferenceVolumeDeficitCubicMetres, 0.4);
            Near(report.MinimumLiquidReferenceVolumeDeficitCubicMetres, 0.4);
            reject(() => staged.PublishValidated());
        });

        test("diagonal pore cells remain separate with complete per-component species ledgers", () =>
        {
            var staged = Stage(new Cell(3, 1100), solid, solid, new Cell(5, 1500));
            var report = ColonyPlacementDiagnostics.Analyze(staged);
            check(report.Cells.Count == 2 && report.Components.Count == 2 && report.ComponentsThatCannotFitDeclaredReferenceVolume == 2);
            check(report.Cells.Select(cell => cell.Constituents.Single().MaterialId).SequenceEqual(new[] { "water", "chlorine" }));
            check(report.Components.All(component => component.PoreCellCount == 1));
            Near(report.MinimumLiquidReferenceVolumeDeficitCubicMetres, 0.6);
            Near(report.Components[0].CapturedSurrogateInventory.MassesKg["water"], 1100);
            Near(report.Components[1].CapturedSurrogateInventory.MassesKg["chlorine"], 1500);
        });

        test("adjacent worlds cannot supply each other nominal pore capacity", () =>
        {
            var staged = ColonyFluidState.StageImport(Import(Source(7, 11, solid, new Cell(3, 1100), solid, solid),
                Source(9, 13, vacuum, vacuum, vacuum, vacuum)), Catalog());
            var report = ColonyPlacementDiagnostics.Analyze(staged); var component = report.Components.Single();
            check(component.WorldId == 7 && component.PoreCellCount == 1 && component.CannotFitDeclaredReferenceVolume);
            Near(component.PoreCapacityCubicMetres, 1); Near(component.MinimumLiquidReferenceVolumeDeficitCubicMetres, 0.1);
            reject(() => staged.PublishValidated());
        });

        test("component volume exactly filled by liquid cannot also accommodate a gas parcel", () =>
        {
            var staged = Stage(new Cell(3, 2000), new Cell(2, 2), solid, solid);
            var report = ColonyPlacementDiagnostics.Analyze(staged); var component = report.Components.Single();
            Near(component.PoreCapacityCubicMetres, 2); Near(component.LiquidReferenceVolumeCubicMetres, 2);
            Near(component.MinimumLiquidReferenceVolumeDeficitCubicMetres, 0);
            check(component.ContainsGas && component.GasHasNoHeadspace && component.CannotFitDeclaredReferenceVolume);
            check(report.ComponentsThatCannotFitDeclaredReferenceVolume == 1);
            Near(component.CapturedSurrogateInventory.MassesKg["oxygen"], 2);
        });

        test("multiple liquid references share a component without treating adequate aggregate capacity as publication", () =>
        {
            var staged = Stage(new Cell(3, 1100), new Cell(5, 900), solid, solid);
            var report = ColonyPlacementDiagnostics.Analyze(staged); var component = report.Components.Single();
            Near(component.LiquidReferenceVolumeCubicMetres, 2);
            check(!component.ContainsGas && !component.CannotFitDeclaredReferenceVolume && !report.CanPublishWithoutPlacementChanges);
            check(component.CapturedSurrogateInventory.MassesKg.Count == 2 && report.Cells.Count == 1);
            reject(() => staged.PublishValidated());
        });

        test("reference density follows declared cell volume while nominal full-cell mass remains explicit", () =>
        {
            var staged = ColonyFluidState.StageImport(Import(Source(7, 11, new Cell(3, 1100), solid, solid, solid)), Catalog(0.25), 0.25);
            var report = ColonyPlacementDiagnostics.Analyze(staged); var cell = report.Cells.Single();
            Near(cell.Constituents[0].LiquidReferenceDensityKgPerCubicMetre!.Value, 4000);
            Near(cell.LiquidReferenceVolumeCubicMetres, 0.275); Near(cell.CellCapacityCubicMetres, 0.25);
            Near(report.MinimumLiquidReferenceVolumeDeficitCubicMetres, 0.025);
            reject(() => staged.PublishValidated());
        });

        test("diagnostic analysis is immutable lossless and repeatable across unpublished persistence", () =>
        {
            var source = Source(7, 11, new Cell(3, 1400, 310, 7, 123, 456), new Cell(2, 2, 290, 17, 456, 89), solid, solid);
            var originalSource = source.LengthPrefixedData.ToArray(); var import = Import(source);
            var originalShadow = import.Worlds[0].LengthPrefixedData.ToArray(); var staged = ColonyFluidState.StageImport(import, Catalog());
            var saved = staged.Save(); var totals = staged.Totals(); var report = ColonyPlacementDiagnostics.Analyze(staged);
            var component = report.Components.Single(); SameInventory(component.CapturedSurrogateInventory, totals);
            SameInventory(report.CapturedSurrogateInventoryTotals, totals);
            Near(component.CapturedSurrogateInventory.DiseaseCounts[123], 456); Near(component.CapturedSurrogateInventory.DiseaseCounts[456], 89);
            Near(component.CapturedSurrogateInventory.RadiationQuantity, 24);
            reject(() => ((IList<FluidCellPlacementDiagnostic>)report.Cells).Clear());
            reject(() => ((IList<FluidComponentPlacementDiagnostic>)report.Components).Clear());
            reject(() => ((IList<int>)component.IssueCellIndices).Clear());
            reject(() => ((IList<FluidConstituentPlacementDiagnostic>)report.Cells[0].Constituents).Clear());
            reject(() => ((IDictionary<string, double>)component.CapturedSurrogateInventory.MassesKg)["water"] = 0);
            check(saved.SequenceEqual(staged.Save()) && source.LengthPrefixedData.SequenceEqual(originalSource));
            check(import.Worlds[0].LengthPrefixedData.SequenceEqual(originalShadow));
            var restored = StagedColonyFluidImport.Restore(saved, Catalog()); var repeated = ColonyPlacementDiagnostics.Analyze(restored);
            check(saved.SequenceEqual(restored.Save()) && repeated.Cells.Count == report.Cells.Count && repeated.Components.Count == report.Components.Count);
            SameInventory(repeated.CapturedSurrogateInventoryTotals, totals);
            Near(repeated.Components[0].LiquidReferenceVolumeCubicMetres, component.LiquidReferenceVolumeCubicMetres);
            Near(repeated.Cells[0].TemperatureKelvin, report.Cells[0].TemperatureKelvin);
            reject(() => restored.PublishValidated());
        });
    }

    private readonly struct Cell
    {
        internal int Element { get; }
        internal float Mass { get; }
        internal float Temperature { get; }
        internal float Radiation { get; }
        internal int DiseaseHash { get; }
        internal int DiseaseCount { get; }
        internal Cell(int element, float mass, float temperature = 300, float radiation = 0, int diseaseHash = 0, int diseaseCount = 0)
        { Element = element; Mass = mass; Temperature = temperature; Radiation = radiation; DiseaseHash = diseaseHash; DiseaseCount = diseaseCount; }
    }

    private static void Int(byte[] bytes, int offset, int value) => BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(offset, 4), value);
    private static void Float(byte[] bytes, int offset, float value) => Int(bytes, offset, BitConverter.SingleToInt32Bits(value));
}
