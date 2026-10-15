using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Core
{
    /// <summary>A constituent's declared incompressible reference, not a measured physical volume or equation of state.</summary>
    public sealed class FluidConstituentPlacementDiagnostic
    {
        public string MaterialId { get; }
        public NativeMatterPhase Phase { get; }
        public double MassKg { get; }
        public double? LiquidReferenceDensityKgPerCubicMetre { get; }
        public double? LiquidReferenceVolumeCubicMetres { get; }

        internal FluidConstituentPlacementDiagnostic(FluidMaterial material, double mass)
        {
            MaterialId = material.Id; Phase = material.Phase; MassKg = mass;
            if (material.Phase == NativeMatterPhase.Liquid)
            {
                LiquidReferenceDensityKgPerCubicMetre = material.LiquidDensityKgPerCubicMetre;
                LiquidReferenceVolumeCubicMetres = mass / material.LiquidDensityKgPerCubicMetre;
            }
        }
    }

    public sealed class FluidCellPlacementDiagnostic
    {
        public int WorldId { get; }
        public int CellIndex { get; }
        public int X { get; }
        public int Y { get; }
        /// <summary>Immutable captured constituent, disease and radiation quantities; energy follows the catalogue surrogate.</summary>
        public FluidInventory CapturedSurrogateInventory { get; }
        public double TemperatureKelvin { get; }
        public double LiquidReferenceVolumeCubicMetres { get; }
        public double CellCapacityCubicMetres { get; }
        public double ExcessLiquidReferenceVolumeCubicMetres => Math.Max(0, LiquidReferenceVolumeCubicMetres - CellCapacityCubicMetres);
        public bool GasHasNoHeadspace { get; }
        public IReadOnlyList<FluidConstituentPlacementDiagnostic> Constituents { get; }

        internal FluidCellPlacementDiagnostic(ColonyFrameSnapshot capture, FluidPlacementIssue issue)
        {
            WorldId = issue.WorldId; CellIndex = issue.CellIndex; X = CellIndex % capture.Width; Y = CellIndex / capture.Width;
            CapturedSurrogateInventory = capture.Inventories[CellIndex];
            TemperatureKelvin = capture.Catalog.Describe(CapturedSurrogateInventory, double.MaxValue).Temperature;
            LiquidReferenceVolumeCubicMetres = issue.LiquidVolumeCubicMetres;
            CellCapacityCubicMetres = issue.CellVolumeCubicMetres; GasHasNoHeadspace = issue.GasHasNoHeadspace;
            Constituents = Array.AsReadOnly(CapturedSurrogateInventory.MassesKg.Select(entry =>
                new FluidConstituentPlacementDiagnostic(capture.Catalog.Get(entry.Key), entry.Value)).ToArray());
        }
    }

    /// <summary>
    /// A necessary volume bound for one four-neighbour, same-world pore component.
    /// Adequate aggregate reference capacity does not prove that conservative relaxation or a physical state exists.
    /// </summary>
    public sealed class FluidComponentPlacementDiagnostic
    {
        public int WorldId { get; }
        public int RepresentativeCellIndex { get; }
        public int PoreCellCount { get; }
        public double PoreCapacityCubicMetres { get; }
        public double LiquidReferenceVolumeCubicMetres { get; }
        public double MinimumLiquidReferenceVolumeDeficitCubicMetres => Math.Max(0, LiquidReferenceVolumeCubicMetres - PoreCapacityCubicMetres);
        public bool ContainsGas { get; }
        public bool GasHasNoHeadspace => ContainsGas && LiquidReferenceVolumeCubicMetres >= PoreCapacityCubicMetres;
        public bool CannotFitDeclaredReferenceVolume => LiquidReferenceVolumeCubicMetres > PoreCapacityCubicMetres || GasHasNoHeadspace;
        public IReadOnlyList<int> IssueCellIndices { get; }
        public FluidInventory CapturedSurrogateInventory { get; }

        internal FluidComponentPlacementDiagnostic(int world, int count, double capacity, double liquidVolume,
            bool hasGas, List<int> issueCells, FluidInventory inventory)
        {
            issueCells.Sort(); IssueCellIndices = new ReadOnlyCollection<int>(issueCells);
            WorldId = world; RepresentativeCellIndex = issueCells[0]; PoreCellCount = count;
            PoreCapacityCubicMetres = capacity; LiquidReferenceVolumeCubicMetres = liquidVolume;
            ContainsGas = hasGas; CapturedSurrogateInventory = inventory;
        }
    }

    public sealed class ColonyPlacementDiagnosticReport
    {
        public string MaterialModelVersion { get; }
        public string MaterialModelFingerprint { get; }
        public string InventoryStatus => "unpublished capture; diagnostics do not place or publish fluid";
        public string EnergyInterpretation => "Catalogue surrogate energy only; the native save does not disclose conserved internal or compression energy.";
        public string ComponentInterpretation => "Same-world four-neighbour non-solid pore capacity is a necessary volume bound, not proof of a supported relaxation.";
        public bool CanPublishWithoutPlacementChanges { get; }
        public double CellVolumeCubicMetres { get; }
        public FluidInventory CapturedSurrogateInventoryTotals { get; }
        public IReadOnlyList<FluidCellPlacementDiagnostic> Cells { get; }
        public IReadOnlyList<FluidComponentPlacementDiagnostic> Components { get; }
        public int ComponentsThatCannotFitDeclaredReferenceVolume { get; }
        public double MinimumLiquidReferenceVolumeDeficitCubicMetres { get; }

        internal ColonyPlacementDiagnosticReport(StagedColonyFluidImport staged, List<FluidCellPlacementDiagnostic> cells,
            List<FluidComponentPlacementDiagnostic> components)
        {
            MaterialModelVersion = staged.MaterialModelVersion; MaterialModelFingerprint = staged.MaterialModelFingerprint;
            CanPublishWithoutPlacementChanges = staged.CanPublish; CellVolumeCubicMetres = staged.CellVolumeCubicMetres;
            CapturedSurrogateInventoryTotals = staged.Totals();
            Cells = new ReadOnlyCollection<FluidCellPlacementDiagnostic>(cells);
            Components = new ReadOnlyCollection<FluidComponentPlacementDiagnostic>(components);
            foreach (var component in components)
            {
                if (component.CannotFitDeclaredReferenceVolume) ++ComponentsThatCannotFitDeclaredReferenceVolume;
                MinimumLiquidReferenceVolumeDeficitCubicMetres += component.MinimumLiquidReferenceVolumeDeficitCubicMetres;
            }
            FluidChecks.Nonnegative(MinimumLiquidReferenceVolumeDeficitCubicMetres, "total reference volume deficit");
        }
    }

    public static class ColonyPlacementDiagnostics
    {
        /// <summary>
        /// Describes every rejected cell and its connected pore capacity without changing inventories or geometry.
        /// Only components containing rejected cells are reported. Solids, world boundaries and other worlds never provide capacity.
        /// </summary>
        public static ColonyPlacementDiagnosticReport Analyze(StagedColonyFluidImport staged)
        {
            if (staged == null) throw new ArgumentNullException(nameof(staged));
            var capture = staged.InventoryCapture;
            var cells = staged.PlacementIssues.Select(issue => new FluidCellPlacementDiagnostic(capture, issue)).ToList();
            var components = new List<FluidComponentPlacementDiagnostic>();
            var issueSet = new HashSet<int>(cells.Select(cell => cell.CellIndex));
            for (var worldIndex = 0; worldIndex < capture.Worlds.Count; ++worldIndex)
            {
                var world = capture.Worlds[worldIndex];
                var worldIssues = cells.Where(cell => cell.WorldId == world.WorldId).ToArray();
                if (worldIssues.Length == 0) continue;
                var elements = capture.Geometry.Elements[worldIndex];
                var visited = new bool[elements.Length]; var queue = new int[elements.Length];
                foreach (var issue in worldIssues)
                {
                    var start = issue.X - world.OffsetX + (issue.Y - world.OffsetY) * world.Width;
                    if (visited[start]) continue;
                    var head = 0; var tail = 0; Enqueue(start);
                    var issueCells = new List<int>(); var inventory = new DiagnosticInventorySum();
                    while (head < tail)
                    {
                        var local = queue[head++]; var x = local % world.Width; var y = local / world.Width;
                        var global = x + world.OffsetX + (y + world.OffsetY) * capture.Width;
                        if (issueSet.Contains(global)) issueCells.Add(global);
                        if (capture.Inventories.TryGetValue(global, out var parcel))
                            inventory.Add(parcel);
                        if (x > 0) Enqueue(local - 1);
                        if (x + 1 < world.Width) Enqueue(local + 1);
                        if (y > 0) Enqueue(local - world.Width);
                        if (y + 1 < world.Height) Enqueue(local + world.Width);
                    }
                    var totalInventory = inventory.Finish(); var liquidVolume = 0.0; var hasGas = false;
                    foreach (var mass in totalInventory.MassesKg)
                    {
                        var material = capture.Catalog.Get(mass.Key);
                        if (material.Phase == NativeMatterPhase.Liquid) liquidVolume += mass.Value / material.LiquidDensityKgPerCubicMetre;
                        else hasGas = true;
                    }
                    var capacity = tail * capture.CellVolumeCubicMetres;
                    FluidChecks.Positive(capacity, "component pore capacity");
                    FluidChecks.Nonnegative(liquidVolume, "component reference liquid volume");
                    components.Add(new FluidComponentPlacementDiagnostic(world.WorldId, tail, capacity, liquidVolume,
                        hasGas, issueCells, totalInventory));

                    void Enqueue(int local)
                    {
                        if (visited[local] || elements[local] != capture.Geometry.VacuumHash) return;
                        visited[local] = true; queue[tail++] = local;
                    }
                }
            }
            components.Sort((left, right) => left.WorldId != right.WorldId ? left.WorldId.CompareTo(right.WorldId) :
                left.RepresentativeCellIndex.CompareTo(right.RepresentativeCellIndex));
            return new ColonyPlacementDiagnosticReport(staged, cells, components);
        }

        private sealed class DiagnosticInventorySum
        {
            private readonly Dictionary<string, double> masses = new Dictionary<string, double>(StringComparer.Ordinal);
            private readonly Dictionary<int, double> diseases = new Dictionary<int, double>();
            private double energy, momentumX, momentumY, radiation;

            internal void Add(FluidInventory inventory)
            {
                foreach (var mass in inventory.MassesKg)
                { masses.TryGetValue(mass.Key, out var current); masses[mass.Key] = current + mass.Value; }
                foreach (var disease in inventory.DiseaseCounts)
                { diseases.TryGetValue(disease.Key, out var current); diseases[disease.Key] = current + disease.Value; }
                energy += inventory.TotalEnergyJoules; momentumX += inventory.MomentumX; momentumY += inventory.MomentumY;
                radiation += inventory.RadiationQuantity;
            }

            internal FluidInventory Finish() => new FluidInventory(masses, energy, momentumX, momentumY, diseases, radiation);
        }
    }
}
