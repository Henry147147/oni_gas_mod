using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Core
{
    public sealed class FluidPlacementIssue
    {
        public int CellIndex { get; }
        public int WorldId { get; }
        public double LiquidVolumeCubicMetres { get; }
        public double CellVolumeCubicMetres { get; }
        public double ExcessLiquidVolumeCubicMetres => Math.Max(0, LiquidVolumeCubicMetres - CellVolumeCubicMetres);
        public bool GasHasNoHeadspace { get; }
        internal FluidPlacementIssue(int cell, int world, double liquidVolume, double volume, bool gasBlocked)
        { CellIndex = cell; WorldId = world; LiquidVolumeCubicMetres = liquidVolume; CellVolumeCubicMetres = volume; GasHasNoHeadspace = gasBlocked; }
    }

    /// <summary>
    /// Unpublished inventory capture, including compressed native liquids that the incompressible model cannot place.
    /// Its archive is deliberately distinct from a runnable custom-world checkpoint. No mass, energy or geometry is adjusted.
    /// </summary>
    public sealed class StagedColonyFluidImport
    {
        private static readonly byte[] ArchiveMagic = Encoding.ASCII.GetBytes("MASTAGED");
        private readonly ColonyFrameSnapshot inventoryCapture;
        internal ColonyFrameSnapshot InventoryCapture => inventoryCapture;
        public int Width => inventoryCapture.Width;
        public int Height => inventoryCapture.Height;
        public double CellVolumeCubicMetres => inventoryCapture.CellVolumeCubicMetres;
        public string MaterialModelVersion => inventoryCapture.MaterialModelVersion;
        public string MaterialModelFingerprint => inventoryCapture.MaterialModelFingerprint;
        public IReadOnlyList<NativeWorldPlacement> Worlds => inventoryCapture.Worlds;
        public IReadOnlyDictionary<int, FluidInventory> Inventories => inventoryCapture.Inventories;
        public IReadOnlyList<FluidPlacementIssue> PlacementIssues { get; }
        public bool CanPublish => PlacementIssues.Count == 0;

        internal StagedColonyFluidImport(ColonyFrameSnapshot capture)
        {
            inventoryCapture = capture; var issues = new List<FluidPlacementIssue>();
            foreach (var entry in capture.Inventories.OrderBy(entry => entry.Key))
            {
                // This checks species/temperature support, without pretending that
                // a cell's physical occupied-volume constraint has passed.
                capture.Catalog.Describe(entry.Value, double.MaxValue);
                var liquidVolume = 0.0; var hasGas = false;
                foreach (var mass in entry.Value.MassesKg)
                {
                    var material = capture.Catalog.Get(mass.Key);
                    if (material.Phase == NativeMatterPhase.Liquid) liquidVolume += mass.Value / material.LiquidDensityKgPerCubicMetre;
                    else hasGas = true;
                }
                if (liquidVolume > capture.CellVolumeCubicMetres || (hasGas && liquidVolume >= capture.CellVolumeCubicMetres))
                {
                    capture.Geometry.TryLocate(entry.Key, out var placement, out _);
                    issues.Add(new FluidPlacementIssue(entry.Key, placement.WorldId, liquidVolume, capture.CellVolumeCubicMetres,
                        hasGas && liquidVolume >= capture.CellVolumeCubicMetres));
                }
                else capture.Catalog.Describe(entry.Value, capture.CellVolumeCubicMetres);
            }
            PlacementIssues = new ReadOnlyCollection<FluidPlacementIssue>(issues);
        }

        public FluidInventory Totals() => inventoryCapture.Totals();

        public ColonyFluidState PublishValidated()
        {
            if (!CanPublish)
            {
                var issue = PlacementIssues[0];
                throw new InvalidOperationException("Unresolved fluid placement in world " + issue.WorldId + ", cell " + issue.CellIndex +
                    ": liquid occupied volume exceeds cell capacity or leaves no gas headspace. A supported relaxation or compressible model is required.");
            }
            return new ColonyFluidState(inventoryCapture);
        }

        /// <summary>Lossless staging evidence, never a publishable mixture-world checkpoint.</summary>
        public byte[] Save() => ColonyFluidState.SaveSnapshot(inventoryCapture, ArchiveMagic);

        public static StagedColonyFluidImport Restore(ReadOnlySpan<byte> bytes, FluidMaterialCatalog catalog)
            => new StagedColonyFluidImport(ColonyFluidState.RestoreSnapshot(bytes, catalog, ArchiveMagic, true));
    }
}
