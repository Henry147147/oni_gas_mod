using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Core
{
    [Flags]
    public enum FluidContainerPhases { Gas = 1, Liquid = 2 }

    /// <summary>Exact allocation identity. A handle returned by a transaction remains provisional until that transaction publishes.</summary>
    public readonly struct FluidContainerHandle : IEquatable<FluidContainerHandle>
    {
        public Guid ColonyId { get; }
        public string Id { get; }
        public long Generation { get; }
        public Guid AllocationToken { get; }
        internal FluidContainerHandle(Guid colonyId, string id, long generation, Guid allocationToken)
        {
            FluidChecks.Identity(id, nameof(id));
            if (colonyId == Guid.Empty || allocationToken == Guid.Empty || generation <= 0) throw new ArgumentException("Invalid container allocation identity.");
            ColonyId = colonyId; Id = id; Generation = generation; AllocationToken = allocationToken;
        }
        public bool Equals(FluidContainerHandle other) => ColonyId == other.ColonyId && Generation == other.Generation &&
            AllocationToken == other.AllocationToken && string.Equals(Id, other.Id, StringComparison.Ordinal);
        public override bool Equals(object? other) => other is FluidContainerHandle handle && Equals(handle);
        public override int GetHashCode() => HashCode.Combine(ColonyId, Id == null ? 0 : StringComparer.Ordinal.GetHashCode(Id), Generation, AllocationToken);
        public override string ToString() => (Id ?? "invalid") + "#" + Generation.ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>Explicit storage geometry and admission policy. MaximumMassKg is gameplay capacity, never a physical pressure limit.</summary>
    public sealed class FluidContainerDefinition
    {
        public double VolumeCubicMetres { get; }
        public double MaximumMassKg { get; }
        public FluidContainerPhases AllowedPhases { get; }
        public FluidContainerDefinition(double volumeCubicMetres, double maximumMassKg, FluidContainerPhases allowedPhases)
        {
            FluidChecks.Positive(volumeCubicMetres, nameof(volumeCubicMetres));
            FluidChecks.Nonnegative(maximumMassKg, nameof(maximumMassKg));
            if (allowedPhases == 0 || (allowedPhases & ~(FluidContainerPhases.Gas | FluidContainerPhases.Liquid)) != 0)
                throw new ArgumentOutOfRangeException(nameof(allowedPhases));
            VolumeCubicMetres = volumeCubicMetres; MaximumMassKg = maximumMassKg; AllowedPhases = allowedPhases;
        }
        internal FluidProperties Validate(FluidInventory inventory, FluidMaterialCatalog catalog)
        {
            if (inventory.TotalMassKg > MaximumMassKg) throw new InvalidOperationException("Container inventory exceeds its declared gameplay mass capacity.");
            foreach (var entry in inventory.MassEntries)
            {
                var phase = catalog.Get(entry.Key).Phase == NativeMatterPhase.Gas ? FluidContainerPhases.Gas : FluidContainerPhases.Liquid;
                if ((AllowedPhases & phase) == 0) throw new InvalidOperationException("Container does not admit a constituent phase: " + entry.Key);
            }
            // This remains the explicit constant-density liquid surrogate, with
            // gas headspace required. No compression or capacity-as-pressure EOS.
            return catalog.Describe(inventory, VolumeCubicMetres);
        }
    }

    public sealed class FluidContainerSnapshot
    {
        internal const int MaximumContainers = 65536;
        internal static IReadOnlyDictionary<string, FluidContainerSnapshot> EmptyTable { get; } =
            new ReadOnlyDictionary<string, FluidContainerSnapshot>(new Dictionary<string, FluidContainerSnapshot>(StringComparer.Ordinal));
        public FluidContainerHandle Handle { get; }
        public string Id => Handle.Id;
        public FluidContainerDefinition Definition { get; }
        public FluidInventory Inventory { get; }
        public double TemperatureKelvin { get; }
        /// <summary>Ideal-gas headspace pressure only; no liquid mechanical pressure or vessel strength model.</summary>
        public double GasPressurePa { get; }
        public double LiquidVolumeFraction { get; }
        internal FluidContainerSnapshot(FluidContainerHandle handle, FluidContainerDefinition definition, FluidInventory inventory, FluidMaterialCatalog catalog)
        {
            Handle = handle; Definition = definition; Inventory = inventory;
            var properties = definition.Validate(inventory, catalog);
            TemperatureKelvin = properties.Temperature; GasPressurePa = properties.GasPressure; LiquidVolumeFraction = properties.LiquidFraction;
        }
        internal FluidContainerSnapshot WithInventory(FluidInventory inventory, FluidMaterialCatalog catalog)
            => new FluidContainerSnapshot(Handle, Definition, inventory, catalog);
        internal static void ValidateSet(Guid colonyId, long nextGeneration, IReadOnlyDictionary<string, FluidContainerSnapshot> containers, FluidMaterialCatalog catalog)
        {
            if (colonyId == Guid.Empty || nextGeneration <= 0 || containers.Count > MaximumContainers) throw new InvalidDataException("Invalid container allocation table.");
            var generations = new HashSet<long>(); var tokens = new HashSet<Guid>();
            foreach (var entry in containers)
            {
                var handle = entry.Value.Handle;
                if (handle.ColonyId != colonyId || handle.Id != entry.Key || handle.Generation <= 0 || handle.Generation >= nextGeneration ||
                    handle.AllocationToken == Guid.Empty || !generations.Add(handle.Generation) || !tokens.Add(handle.AllocationToken))
                    throw new InvalidDataException("Inconsistent or duplicate container allocation identity.");
                entry.Value.Definition.Validate(entry.Value.Inventory, catalog);
            }
        }
    }

    public sealed partial class FluidTransaction
    {
        private Dictionary<string, FluidContainerSnapshot?>? containerChanges;
        private long nextContainerGeneration;
        private int activeContainerCount;
        private Dictionary<string, FluidContainerSnapshot?> ContainerChanges => containerChanges ??=
            new Dictionary<string, FluidContainerSnapshot?>(StringComparer.Ordinal);

        /// <summary>Creates empty storage only. The unique token prevents an aborted/stale provisional handle from aliasing a later allocation.</summary>
        public FluidContainerHandle CreateContainer(string id, FluidContainerDefinition definition)
        {
            CheckOpen(); FluidChecks.Identity(id, nameof(id)); if (definition == null) throw new ArgumentNullException(nameof(definition));
            var found = containerChanges != null && containerChanges.TryGetValue(id, out var changed) ? changed :
                basis.Containers.TryGetValue(id, out var existing) ? existing : null;
            if (found != null) throw new InvalidOperationException("Container ID is already active.");
            if (activeContainerCount >= FluidContainerSnapshot.MaximumContainers) throw new InvalidOperationException("Container count limit reached.");
            var next = checked(nextContainerGeneration + 1);
            var handle = new FluidContainerHandle(basis.ColonyId, id, nextContainerGeneration, Guid.NewGuid());
            var container = new FluidContainerSnapshot(handle, definition, FluidInventory.Empty, basis.Catalog);
            StageContainer(id, container); nextContainerGeneration = next; ++activeContainerCount; return handle;
        }

        private FluidContainerSnapshot Container(FluidContainerHandle handle)
        {
            CheckOpen();
            if (handle.ColonyId != basis.ColonyId || handle.Id == null) throw new InvalidOperationException("Container handle belongs to another colony or is invalid.");
            var found = containerChanges != null && containerChanges.TryGetValue(handle.Id, out var changed) ? changed :
                basis.Containers.TryGetValue(handle.Id, out var existing) ? existing : null;
            if (found == null || !found.Handle.Equals(handle)) throw new InvalidOperationException("Container handle is stale, retired or unpublished.");
            return found;
        }

        public FluidContainerSnapshot SampleContainer(FluidContainerHandle handle) => Container(handle);

        public void RetireContainer(FluidContainerHandle handle)
        {
            var container = Container(handle);
            if (!container.Inventory.IsEmpty) throw new InvalidOperationException("Container retirement requires empty mass and metadata or explicit conserved evacuation.");
            StageContainer(handle.Id, null); --activeContainerCount;
        }

        public void TransferCellToContainer(int sourceCell, FluidContainerHandle destination, double massKg)
            => CellToContainer(sourceCell, destination, massKg, null, false);
        public void TransferCellToContainer(int sourceCell, FluidContainerHandle destination, string materialId, double massKg)
        { FluidChecks.Identity(materialId, nameof(materialId)); CellToContainer(sourceCell, destination, massKg, materialId, false); }
        public void TransferAllCellToContainer(int sourceCell, FluidContainerHandle destination)
            => CellToContainer(sourceCell, destination, 0, null, true);

        private void CellToContainer(int sourceCell, FluidContainerHandle destinationHandle, double massKg, string? materialId, bool all)
        {
            CheckOpen(); var source = Get(sourceCell); var destination = Container(destinationHandle);
            var parcel = all ? source : materialId == null ? Parcel(source, massKg) : SpeciesParcel(source, materialId, massKg);
            var remainder = all ? FluidInventory.Empty : FluidInventory.Combine(source, parcel, true);
            Validate(remainder); var combined = destination.WithInventory(FluidInventory.Combine(destination.Inventory, parcel), basis.Catalog);
            StageCell(sourceCell, remainder); StageContainer(destinationHandle.Id, combined);
        }

        public void TransferContainerToCell(FluidContainerHandle source, int destinationCell, double massKg)
            => ContainerToCell(source, destinationCell, massKg, null, false, false);
        public void TransferContainerToCell(FluidContainerHandle source, int destinationCell, string materialId, double massKg)
        { FluidChecks.Identity(materialId, nameof(materialId)); ContainerToCell(source, destinationCell, massKg, materialId, false, false); }
        public void TransferAllContainerToCell(FluidContainerHandle source, int destinationCell)
            => ContainerToCell(source, destinationCell, 0, null, true, false);
        public void RetireContainerToCell(FluidContainerHandle source, int destinationCell)
            => ContainerToCell(source, destinationCell, 0, null, true, true);

        private void ContainerToCell(FluidContainerHandle sourceHandle, int destinationCell, double massKg, string? materialId, bool all, bool retire)
        {
            CheckOpen(); var source = Container(sourceHandle); var destination = Get(destinationCell);
            var parcel = all ? source.Inventory : materialId == null ? Parcel(source.Inventory, massKg) :
                SpeciesParcel(source.Inventory, materialId, massKg, source.Definition.VolumeCubicMetres);
            var remainder = source.WithInventory(all ? FluidInventory.Empty : FluidInventory.Combine(source.Inventory, parcel, true), basis.Catalog);
            var combined = FluidInventory.Combine(destination, parcel); Validate(combined);
            StageContainer(sourceHandle.Id, retire ? null : remainder); StageCell(destinationCell, combined);
            if (retire) --activeContainerCount;
        }

        public void TransferContainerToContainer(FluidContainerHandle source, FluidContainerHandle destination, double massKg)
            => ContainerToContainer(source, destination, massKg, null, false, false);
        public void TransferContainerToContainer(FluidContainerHandle source, FluidContainerHandle destination, string materialId, double massKg)
        { FluidChecks.Identity(materialId, nameof(materialId)); ContainerToContainer(source, destination, massKg, materialId, false, false); }
        public void TransferAllContainerToContainer(FluidContainerHandle source, FluidContainerHandle destination)
            => ContainerToContainer(source, destination, 0, null, true, false);
        public void RetireContainerToContainer(FluidContainerHandle source, FluidContainerHandle destination)
            => ContainerToContainer(source, destination, 0, null, true, true);

        private void ContainerToContainer(FluidContainerHandle sourceHandle, FluidContainerHandle destinationHandle, double massKg, string? materialId, bool all, bool retire)
        {
            CheckOpen(); var source = Container(sourceHandle); var destination = Container(destinationHandle);
            if (sourceHandle.Equals(destinationHandle)) throw new ArgumentException("Container transfer requires different allocations.");
            var parcel = all ? source.Inventory : materialId == null ? Parcel(source.Inventory, massKg) :
                SpeciesParcel(source.Inventory, materialId, massKg, source.Definition.VolumeCubicMetres);
            var remainder = source.WithInventory(all ? FluidInventory.Empty : FluidInventory.Combine(source.Inventory, parcel, true), basis.Catalog);
            var combined = destination.WithInventory(FluidInventory.Combine(destination.Inventory, parcel), basis.Catalog);
            StageContainer(sourceHandle.Id, retire ? null : remainder); StageContainer(destinationHandle.Id, combined);
            if (retire) --activeContainerCount;
        }
    }
}
