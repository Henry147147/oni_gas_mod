using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Core
{
    /// <summary>Owned conserved quantities. Energy includes bulk kinetic energy; temperatures are derived from a catalog.</summary>
    public sealed class FluidInventory
    {
        private readonly OrdinalFluidMasses massStorage;
        public static FluidInventory Empty { get; } = new FluidInventory(new Dictionary<string, double>(), 0);
        public IReadOnlyDictionary<string, double> MassesKg => massStorage;
        internal ReadOnlySpan<KeyValuePair<string, double>> MassEntries => massStorage.Entries;
        public IReadOnlyDictionary<int, double> DiseaseCounts { get; }
        public double TotalMassKg { get; }
        public double TotalEnergyJoules { get; }
        public double MomentumX { get; }
        public double MomentumY { get; }
        // This is a passive native metadata quantity, not radiation energy or dose.
        // Its original per-cell value is retained and proportional transfers conserve it.
        public double RadiationQuantity { get; }
        public double VelocityX => TotalMassKg == 0 ? 0 : MomentumX / TotalMassKg;
        public double VelocityY => TotalMassKg == 0 ? 0 : MomentumY / TotalMassKg;
        public double KineticEnergyJoules => TotalMassKg == 0 ? 0 : 0.5 * (MomentumX * VelocityX + MomentumY * VelocityY);
        public double InternalEnergyJoules => TotalEnergyJoules - KineticEnergyJoules;
        internal bool IsEmpty => MassesKg.Count == 0 && DiseaseCounts.Count == 0 && RadiationQuantity == 0;

        public FluidInventory(IReadOnlyDictionary<string, double> massesKg, double totalEnergyJoules,
            double momentumX = 0, double momentumY = 0, IReadOnlyDictionary<int, double>? diseaseCounts = null,
            double radiationQuantity = 0)
        {
            if (massesKg == null) throw new ArgumentNullException(nameof(massesKg));
            massStorage = OrdinalFluidMasses.Create(massesKg, out var mass);
            FluidChecks.Nonnegative(mass, "total mass");
            FluidChecks.Finite(totalEnergyJoules, nameof(totalEnergyJoules));
            FluidChecks.Finite(momentumX, nameof(momentumX)); FluidChecks.Finite(momentumY, nameof(momentumY));
            FluidChecks.Nonnegative(radiationQuantity, nameof(radiationQuantity));
            if (mass == 0 && (totalEnergyJoules != 0 || momentumX != 0 || momentumY != 0))
                throw new ArgumentException("Massless cells cannot store fluid energy or momentum.");
            var diseases = new SortedDictionary<int, double>();
            if (diseaseCounts != null)
                foreach (var entry in diseaseCounts)
                {
                    FluidChecks.Nonnegative(entry.Value, nameof(diseaseCounts));
                    if (entry.Value > 0)
                    {
                        if (entry.Key == 0) throw new ArgumentException("Positive disease inventory needs a disease identity.");
                        diseases.Add(entry.Key, entry.Value);
                        if (diseases.Count > FluidChecks.MaximumDiseaseKinds) throw new ArgumentException("Too many disease identities.");
                    }
                }
            DiseaseCounts = new ReadOnlyDictionary<int, double>(diseases);
            TotalMassKg = mass; TotalEnergyJoules = totalEnergyJoules; MomentumX = momentumX; MomentumY = momentumY;
            RadiationQuantity = radiationQuantity;
            FluidChecks.Finite(KineticEnergyJoules, "kinetic energy"); FluidChecks.Finite(InternalEnergyJoules, "internal energy");
        }

        internal FluidInventory Scale(double fraction)
        {
            if (fraction == 0) return Empty;
            if (fraction == 1) return this;
            return new FluidInventory(MassesKg.ToDictionary(entry => entry.Key, entry => entry.Value * fraction, StringComparer.Ordinal),
                TotalEnergyJoules * fraction, MomentumX * fraction, MomentumY * fraction,
                DiseaseCounts.ToDictionary(entry => entry.Key, entry => entry.Value * fraction), RadiationQuantity * fraction);
        }

        internal static FluidInventory Combine(FluidInventory left, FluidInventory right, bool subtract = false)
        {
            var sign = subtract ? -1 : 1;
            var masses = left.MassesKg.ToDictionary(entry => entry.Key, entry => entry.Value, StringComparer.Ordinal);
            foreach (var entry in right.MassesKg)
            { masses.TryGetValue(entry.Key, out var value); masses[entry.Key] = value + sign * entry.Value; }
            var disease = left.DiseaseCounts.ToDictionary(entry => entry.Key, entry => entry.Value);
            foreach (var entry in right.DiseaseCounts)
            { disease.TryGetValue(entry.Key, out var value); disease[entry.Key] = value + sign * entry.Value; }
            return new FluidInventory(masses, left.TotalEnergyJoules + sign * right.TotalEnergyJoules,
                left.MomentumX + sign * right.MomentumX, left.MomentumY + sign * right.MomentumY,
                disease, left.RadiationQuantity + sign * right.RadiationQuantity);
        }
    }

    public sealed class FluidCellSnapshot
    {
        public int WorldId { get; }
        public int CellIndex { get; }
        public int X { get; }
        public int Y { get; }
        public int NativeElementHash { get; }
        public bool IsSolid { get; }
        public double VolumeCubicMetres { get; }
        public FluidInventory Inventory { get; }
        public double TemperatureKelvin { get; }
        /// <summary>Ideal gas headspace pressure only. Liquid mechanical pressure requires a coupled solver.</summary>
        public double GasPressurePa { get; }
        public double LiquidVolumeFraction { get; }
        public string? DominantMaterialId { get; }
        public NativeMatterPhase DominantPhase { get; }
        internal FluidCellSnapshot(int worldId, int cellIndex, int width, int elementHash, bool solid,
            double volume, FluidInventory inventory, FluidProperties properties)
        {
            WorldId = worldId; CellIndex = cellIndex; X = cellIndex % width; Y = cellIndex / width;
            NativeElementHash = elementHash; IsSolid = solid; VolumeCubicMetres = volume; Inventory = inventory;
            TemperatureKelvin = properties.Temperature; GasPressurePa = properties.GasPressure;
            LiquidVolumeFraction = properties.LiquidFraction; DominantMaterialId = properties.Dominant; DominantPhase = properties.Phase;
        }
    }

    /// <summary>One coherent immutable revision, including sparse vacuum metadata and every world's geometry.</summary>
    public sealed class ColonyFrameSnapshot
    {
        internal ColonyGeometry Geometry { get; }
        internal FluidMaterialCatalog Catalog { get; }
        public int Width => Geometry.Width;
        public int Height => Geometry.Height;
        public double CellVolumeCubicMetres => Geometry.CellVolume;
        public long Revision { get; }
        public long CompletedFrame { get; }
        public double SimulationTimeSeconds { get; }
        public string MaterialModelVersion => Catalog.Version;
        public string MaterialModelFingerprint => Catalog.Fingerprint;
        public IReadOnlyList<NativeWorldPlacement> Worlds => Geometry.Placements;
        public IReadOnlyDictionary<int, FluidInventory> Inventories { get; }
        public FluidInventory InitialInventory { get; }
        public FluidInventory SuppliedInventory { get; }
        public FluidInventory RemovedInventory { get; }
        public double ExternalHeatJoules { get; }

        internal ColonyFrameSnapshot(ColonyGeometry geometry, FluidMaterialCatalog catalog, long revision,
            long completedFrame, double simulationTime, Dictionary<int, FluidInventory> inventories,
            FluidInventory initial, FluidInventory supplied, FluidInventory removed, double heat)
        {
            Geometry = geometry; Catalog = catalog; Revision = revision; CompletedFrame = completedFrame; SimulationTimeSeconds = simulationTime;
            Inventories = new ReadOnlyDictionary<int, FluidInventory>(inventories);
            InitialInventory = initial; SuppliedInventory = supplied; RemovedInventory = removed; ExternalHeatJoules = heat;
        }

        public FluidCellSnapshot SampleCell(int globalCellIndex)
        {
            if (!TrySampleCell(globalCellIndex, out var cell)) throw new ArgumentOutOfRangeException(nameof(globalCellIndex), "Cell is outside every owned world.");
            return cell!;
        }

        public bool TrySampleCell(int globalCellIndex, out FluidCellSnapshot? cell)
        {
            cell = null;
            if (!Geometry.TryLocate(globalCellIndex, out var world, out var element)) return false;
            var inventory = InventoryAt(globalCellIndex);
            cell = new FluidCellSnapshot(world.WorldId, globalCellIndex, Width, element, element != Geometry.VacuumHash,
                CellVolumeCubicMetres, inventory, Catalog.Describe(inventory, CellVolumeCubicMetres));
            return true;
        }

        internal FluidInventory InventoryAt(int cell) => Inventories.TryGetValue(cell, out var inventory) ? inventory : FluidInventory.Empty;

        public FluidInventory Totals()
        {
            var result = FluidInventory.Empty;
            foreach (var entry in Inventories.OrderBy(entry => entry.Key)) result = FluidInventory.Combine(result, entry.Value);
            return result;
        }
    }

    internal sealed class ColonyGeometry
    {
        internal const int MaximumCells = 16_777_216;
        internal const int MaximumWorlds = 256;
        internal int Width { get; }
        internal int Height { get; }
        internal int VacuumHash { get; }
        internal double CellVolume { get; }
        internal IReadOnlyList<NativeWorldPlacement> Placements { get; }
        internal IReadOnlyList<int[]> Elements { get; }
        internal IReadOnlyList<int> SolidHashes { get; }
        private readonly HashSet<int> solidHashes;

        internal ColonyGeometry(int width, int height, int vacuumHash, double volume,
            IReadOnlyList<NativeWorldPlacement> placements, IReadOnlyList<int[]> elements, IReadOnlyList<int> solids)
        {
            if (width <= 0 || height <= 0 || (long)width * height > MaximumCells || placements.Count == 0 ||
                placements.Count > MaximumWorlds || placements.Count != elements.Count) throw new InvalidDataException("Invalid colony geometry.");
            FluidChecks.Positive(volume, nameof(volume));
            solidHashes = new HashSet<int>(solids);
            if (solidHashes.Count != solids.Count || solidHashes.Count > 65536 || solidHashes.Contains(vacuumHash))
                throw new InvalidDataException("Invalid native solid identity set.");
            var ids = new HashSet<int>();
            for (var i = 0; i < placements.Count; ++i)
            {
                var p = placements[i];
                if (p.WorldId < 0 || !ids.Add(p.WorldId) || p.Width <= 0 || p.Height <= 0 || p.OffsetX < 0 || p.OffsetY < 0 ||
                    (long)p.OffsetX + p.Width > width || (long)p.OffsetY + p.Height > height || (long)p.Width * p.Height != elements[i].Length)
                    throw new InvalidDataException("Invalid world geometry.");
                foreach (var element in elements[i])
                    if (element != vacuumHash && !solidHashes.Contains(element)) throw new InvalidDataException("Unowned native element in colony geometry.");
                for (var j = 0; j < i; ++j)
                {
                    var other = placements[j];
                    if (p.OffsetX < other.OffsetX + other.Width && other.OffsetX < p.OffsetX + p.Width &&
                        p.OffsetY < other.OffsetY + other.Height && other.OffsetY < p.OffsetY + p.Height)
                        throw new InvalidDataException("Overlapping world geometry.");
                }
            }
            Width = width; Height = height; VacuumHash = vacuumHash; CellVolume = volume;
            Placements = Array.AsReadOnly(placements.ToArray());
            Elements = Array.AsReadOnly(elements.Select(element => (int[])element.Clone()).ToArray());
            SolidHashes = Array.AsReadOnly(solids.OrderBy(hash => hash).ToArray());
        }

        internal bool TryLocate(int cell, out NativeWorldPlacement placement, out int element)
        {
            placement = default; element = 0;
            if (cell < 0 || cell >= (long)Width * Height) return false;
            var x = cell % Width; var y = cell / Width;
            for (var i = 0; i < Placements.Count; ++i)
            {
                var p = Placements[i];
                if (x < p.OffsetX || x >= p.OffsetX + p.Width || y < p.OffsetY || y >= p.OffsetY + p.Height) continue;
                placement = p; element = Elements[i][x - p.OffsetX + (y - p.OffsetY) * p.Width]; return true;
            }
            return false;
        }

        internal void RequireFluidCell(int cell)
        {
            if (!TryLocate(cell, out _, out var element)) throw new ArgumentOutOfRangeException(nameof(cell), "Cell is outside every owned world.");
            if (element != VacuumHash) throw new InvalidOperationException("Fluid placement into a native solid requires displacement support.");
        }

        internal void RequireSolidHash(int element)
        {
            if (!solidHashes.Contains(element)) throw new InvalidOperationException("Unknown or non-solid native element identity.");
        }

        internal ColonyGeometry WithChanges(IReadOnlyDictionary<int, int> changes)
        {
            if (changes.Count == 0) return this;
            var elements = Elements.Select(element => (int[])element.Clone()).ToArray();
            foreach (var change in changes)
            {
                if (!TryLocate(change.Key, out var placement, out _)) throw new ArgumentOutOfRangeException(nameof(changes));
                if (change.Value != VacuumHash) RequireSolidHash(change.Value);
                var world = 0; while (Placements[world].WorldId != placement.WorldId) ++world;
                elements[world][change.Key % Width - placement.OffsetX + (change.Key / Width - placement.OffsetY) * placement.Width] = change.Value;
            }
            return new ColonyGeometry(Width, Height, VacuumHash, CellVolume, Placements, elements, SolidHashes);
        }
    }

    /// <summary>All-world inventory authority and transactional publication. It supplies no production fluid timestep or ONI takeover.</summary>
    public sealed partial class ColonyFluidState
    {
        private readonly object sync = new object();
        private ColonyFrameSnapshot current;
        internal ColonyFluidState(ColonyFrameSnapshot initial) { current = initial; }

        public static ColonyFluidState Import(NativeColonyImport import, FluidMaterialCatalog catalog, double cellVolumeCubicMetres = 1)
            => StageImport(import, catalog, cellVolumeCubicMetres).PublishValidated();

        /// <summary>Captures every conserved inventory while reporting unsupported initial volume without publishing a fluid world.</summary>
        public static StagedColonyFluidImport StageImport(NativeColonyImport import, FluidMaterialCatalog catalog, double cellVolumeCubicMetres = 1)
            => new StagedColonyFluidImport(BuildImport(import, catalog, cellVolumeCubicMetres));

        private static ColonyFrameSnapshot BuildImport(NativeColonyImport import, FluidMaterialCatalog catalog, double cellVolumeCubicMetres)
        {
            if (import == null) throw new ArgumentNullException(nameof(import));
            if (catalog == null) throw new ArgumentNullException(nameof(catalog));
            var placements = import.Worlds.Select(world => world.Placement).ToArray();
            var elements = new List<int[]>();
            foreach (var world in import.Worlds)
            {
                var p = world.Placement; var raw = world.Migration.NativeData;
                var rowWidth = p.Width + 2; var cells = new int[checked(p.Width * p.Height)];
                for (var y = 0; y < p.Height; ++y)
                    for (var x = 0; x < p.Width; ++x)
                        cells[x + y * p.Width] = BinaryPrimitives.ReadInt32LittleEndian(raw.Slice(29 + ((x + 1) + (y + 1) * rowWidth) * 16, 4));
                elements.Add(cells);
            }
            var geometry = new ColonyGeometry(import.Width, import.Height, import.VacuumHash, cellVolumeCubicMetres, placements, elements, import.SolidElementHashes);
            var inventories = new Dictionary<int, FluidInventory>();
            foreach (var fluid in import.Fluids)
            {
                var source = fluid.Inventory; var material = catalog.Get(fluid.MaterialId);
                if (material.Phase != source.Phase) throw new InvalidOperationException("Imported phase differs from the material model: " + material.Id);
                var cell = checked(source.X + source.Y * import.Width); geometry.RequireFluidCell(cell);
                if (inventories.ContainsKey(cell)) throw new InvalidDataException("Duplicate imported fluid cell.");
                var disease = source.DiseaseCount == 0 ? null : new Dictionary<int, double> { [source.DiseaseHash] = source.DiseaseCount };
                FluidInventory inventory;
                if (source.MassKg == 0)
                    inventory = new FluidInventory(new Dictionary<string, double>(), 0, diseaseCounts: disease, radiationQuantity: source.Radiation);
                else
                    inventory = catalog.CreateInventory(new Dictionary<string, double> { [material.Id] = source.MassKg },
                        source.TemperatureKelvin, diseaseCounts: disease, radiationQuantity: source.Radiation);
                if (!inventory.IsEmpty) inventories.Add(cell, inventory);
            }
            var initial = FluidInventory.Empty;
            foreach (var entry in inventories.OrderBy(entry => entry.Key)) initial = FluidInventory.Combine(initial, entry.Value);
            return new ColonyFrameSnapshot(geometry, catalog, 0, 0, 0, inventories,
                initial, FluidInventory.Empty, FluidInventory.Empty, 0);
        }

        public ColonyFrameSnapshot CaptureSnapshot() { lock (sync) return current; }
        public FluidTransaction BeginTransaction() { lock (sync) return new FluidTransaction(this, current); }

        internal ColonyFrameSnapshot Publish(ColonyFrameSnapshot basis,
            Dictionary<int, FluidInventory> changes, Dictionary<int, int> geometryChanges, FluidInventory supplied, FluidInventory removed, double heat,
            long completedFrame, double simulationTime)
        {
            lock (sync)
            {
                if (!ReferenceEquals(basis, current)) throw new InvalidOperationException("Transaction is stale; the colony already published another revision.");
                if (completedFrame < basis.CompletedFrame || simulationTime < basis.SimulationTimeSeconds)
                    throw new InvalidOperationException("Completed frame and simulation time cannot go backwards.");
                FluidChecks.Nonnegative(simulationTime, nameof(simulationTime));
                var geometry = basis.Geometry.WithChanges(geometryChanges);
                var inventories = basis.Inventories.ToDictionary(entry => entry.Key, entry => entry.Value);
                foreach (var entry in changes)
                {
                    if (!entry.Value.IsEmpty) geometry.RequireFluidCell(entry.Key);
                    basis.Catalog.Describe(entry.Value, basis.CellVolumeCubicMetres);
                    if (entry.Value.IsEmpty) inventories.Remove(entry.Key); else inventories[entry.Key] = entry.Value;
                }
                foreach (var entry in geometryChanges)
                    if (entry.Value != geometry.VacuumHash && inventories.ContainsKey(entry.Key))
                        throw new InvalidOperationException("A solid geometry update cannot erase fluid inventory or metadata.");
                var nextSupplied = FluidInventory.Combine(basis.SuppliedInventory, supplied);
                var nextRemoved = FluidInventory.Combine(basis.RemovedInventory, removed);
                var nextHeat = basis.ExternalHeatJoules + heat; FluidChecks.Finite(nextHeat, nameof(heat));
                current = new ColonyFrameSnapshot(geometry, basis.Catalog, checked(basis.Revision + 1),
                    completedFrame, simulationTime, inventories, basis.InitialInventory, nextSupplied, nextRemoved, nextHeat);
                return current;
            }
        }
    }

    /// <summary>Optimistic, atomic changes. Callbacks and game side effects must run only after a successful commit.</summary>
    public sealed class FluidTransaction : IDisposable
    {
        private readonly ColonyFluidState owner;
        private readonly ColonyFrameSnapshot basis;
        private readonly Dictionary<int, FluidInventory> changes = new Dictionary<int, FluidInventory>();
        private readonly Dictionary<int, int> geometryChanges = new Dictionary<int, int>();
        private FluidInventory supplied = FluidInventory.Empty;
        private FluidInventory removed = FluidInventory.Empty;
        private double heat;
        private bool closed;
        internal FluidTransaction(ColonyFluidState owner, ColonyFrameSnapshot basis) { this.owner = owner; this.basis = basis; }
        private void CheckOpen() { if (closed) throw new ObjectDisposedException(nameof(FluidTransaction)); }
        private int ElementAt(int cell)
        {
            if (!basis.Geometry.TryLocate(cell, out _, out var element)) throw new ArgumentOutOfRangeException(nameof(cell), "Cell is outside every owned world.");
            return geometryChanges.TryGetValue(cell, out var changed) ? changed : element;
        }
        private FluidInventory Get(int cell)
        {
            if (ElementAt(cell) != basis.Geometry.VacuumHash) throw new InvalidOperationException("Cell contains a native solid.");
            return changes.TryGetValue(cell, out var value) ? value : basis.InventoryAt(cell);
        }
        private void Validate(FluidInventory inventory) => basis.Catalog.Describe(inventory, basis.CellVolumeCubicMetres);
        private static void Reason(string reason) => FluidChecks.Identity(reason, nameof(reason));

        private static FluidInventory Parcel(FluidInventory source, double mass)
        {
            FluidChecks.Positive(mass, nameof(mass));
            if (mass > source.TotalMassKg) throw new InvalidOperationException("Insufficient fluid input.");
            return source.Scale(mass / source.TotalMassKg);
        }

        private FluidInventory SpeciesParcel(FluidInventory source, string materialId, double massKg)
        {
            FluidChecks.Positive(massKg, nameof(massKg)); basis.Catalog.Get(materialId);
            if (!source.MassesKg.TryGetValue(materialId, out var available) || massKg > available)
                throw new InvalidOperationException("Insufficient selected constituent.");
            if (source.MassesKg.Count == 1 && massKg == available) return source;
            var temperature = basis.Catalog.Describe(source, basis.CellVolumeCubicMetres).Temperature;
            var fraction = massKg / source.TotalMassKg;
            return basis.Catalog.CreateInventory(new Dictionary<string, double> { [materialId] = massKg }, temperature,
                source.VelocityX, source.VelocityY, source.DiseaseCounts.ToDictionary(entry => entry.Key, entry => entry.Value * fraction),
                source.RadiationQuantity * fraction);
        }

        /// <summary>Reads this transaction's staged inventory and geometry, without publishing a revision.</summary>
        public FluidCellSnapshot SampleCell(int cell)
        {
            CheckOpen(); var element = ElementAt(cell); basis.Geometry.TryLocate(cell, out var world, out _);
            var inventory = changes.TryGetValue(cell, out var changed) ? changed : basis.InventoryAt(cell);
            return new FluidCellSnapshot(world.WorldId, cell, basis.Width, element, element != basis.Geometry.VacuumHash,
                basis.CellVolumeCubicMetres, inventory, basis.Catalog.Describe(inventory, basis.CellVolumeCubicMetres));
        }

        public void Transfer(int sourceCell, int destinationCell, double massKg)
        {
            CheckOpen();
            if (sourceCell == destinationCell) throw new ArgumentException("Transfer requires distinct cells.");
            var source = Get(sourceCell); var destination = Get(destinationCell); var parcel = Parcel(source, massKg);
            var remainder = FluidInventory.Combine(source, parcel, true); var combined = FluidInventory.Combine(destination, parcel);
            Validate(remainder); Validate(combined);
            changes[sourceCell] = remainder; changes[destinationCell] = combined;
        }

        public void TransferSpecies(int sourceCell, int destinationCell, string materialId, double massKg)
        {
            CheckOpen();
            if (sourceCell == destinationCell) throw new ArgumentException("Transfer requires distinct cells.");
            var source = Get(sourceCell); var destination = Get(destinationCell); var parcel = SpeciesParcel(source, materialId, massKg);
            // Reconstructing selected parcel energy uses the source's actual temperature.
            var remainder = FluidInventory.Combine(source, parcel, true); var combined = FluidInventory.Combine(destination, parcel);
            Validate(remainder); Validate(combined);
            changes[sourceCell] = remainder; changes[destinationCell] = combined;
        }

        public void Inject(int destinationCell, FluidInventory parcel, string reason)
        {
            CheckOpen(); Reason(reason); if (parcel == null) throw new ArgumentNullException(nameof(parcel));
            var destination = Get(destinationCell); Validate(parcel);
            var combined = FluidInventory.Combine(destination, parcel); Validate(combined);
            var nextSupplied = FluidInventory.Combine(supplied, parcel);
            changes[destinationCell] = combined; supplied = nextSupplied;
        }

        public FluidInventory Extract(int sourceCell, double massKg, string reason)
        {
            CheckOpen(); Reason(reason); var source = Get(sourceCell); var parcel = Parcel(source, massKg);
            var remainder = FluidInventory.Combine(source, parcel, true); Validate(remainder);
            var nextRemoved = FluidInventory.Combine(removed, parcel);
            changes[sourceCell] = remainder; removed = nextRemoved; return parcel;
        }

        public FluidInventory ExtractSpecies(int sourceCell, string materialId, double massKg, string reason)
        {
            CheckOpen(); Reason(reason); var source = Get(sourceCell); var parcel = SpeciesParcel(source, materialId, massKg);
            var remainder = FluidInventory.Combine(source, parcel, true); Validate(remainder);
            var nextRemoved = FluidInventory.Combine(removed, parcel);
            changes[sourceCell] = remainder; removed = nextRemoved; return parcel;
        }

        public void ApplyHeat(int cell, double joules, string reason)
        {
            CheckOpen(); Reason(reason); FluidChecks.Finite(joules, nameof(joules)); var inventory = Get(cell);
            var updated = new FluidInventory(inventory.MassesKg, inventory.TotalEnergyJoules + joules, inventory.MomentumX,
                inventory.MomentumY, inventory.DiseaseCounts, inventory.RadiationQuantity); Validate(updated);
            var nextHeat = heat + joules; FluidChecks.Finite(nextHeat, "external heat"); changes[cell] = updated; heat = nextHeat;
        }

        /// <summary>Opens a cell after a native solid removal. Coupling the native operation remains the adapter's responsibility.</summary>
        public void ClearSolid(int cell)
        {
            CheckOpen(); ElementAt(cell); geometryChanges[cell] = basis.Geometry.VacuumHash;
        }

        /// <summary>Places a known native solid only when no fluid mass or metadata would be lost.</summary>
        public void SetSolid(int cell, int solidElementHash)
        {
            CheckOpen(); ElementAt(cell); basis.Geometry.RequireSolidHash(solidElementHash);
            var inventory = changes.TryGetValue(cell, out var changed) ? changed : basis.InventoryAt(cell);
            if (!inventory.IsEmpty) throw new InvalidOperationException("Solid placement requires conservative fluid displacement first.");
            geometryChanges[cell] = solidElementHash;
        }

        /// <summary>Moves the entire inventory and metadata, then places a solid, with no partial changes on blocked output.</summary>
        public void DisplaceAndSetSolid(int cell, int solidElementHash, int destinationCell)
        {
            CheckOpen();
            if (cell == destinationCell) throw new ArgumentException("Displacement requires a different destination.");
            basis.Geometry.RequireSolidHash(solidElementHash);
            var source = Get(cell); var destination = Get(destinationCell);
            var combined = FluidInventory.Combine(destination, source); Validate(combined);
            changes[cell] = FluidInventory.Empty; changes[destinationCell] = combined; geometryChanges[cell] = solidElementHash;
        }

        public ColonyFrameSnapshot Commit() => CommitFrame(basis.CompletedFrame, basis.SimulationTimeSeconds);
        public ColonyFrameSnapshot CommitFrame(long completedFrame, double simulationTimeSeconds)
        {
            CheckOpen();
            var result = owner.Publish(basis, changes, geometryChanges, supplied, removed, heat, completedFrame, simulationTimeSeconds);
            closed = true; return result;
        }
        public void Dispose() { closed = true; }
    }
}
