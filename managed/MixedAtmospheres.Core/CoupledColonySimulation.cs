using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Core
{
    public sealed class CoupledColonyAdvanceException : InvalidOperationException
    {
        public long AttemptedFrame { get; }
        public int CompletedWorldCandidates { get; }
        internal CoupledColonyAdvanceException(long frame, int worlds, Exception cause)
            : base("Coupled colony frame rejected before publication after " + worlds + " completed world candidates.", cause)
        { AttemptedFrame = frame; CompletedWorldCandidates = worlds; }
    }
    public readonly struct CoupledColonyCell
    {
        public int WorldId { get; }
        public int GlobalCellIndex { get; }
        public double VolumeCubicMetres { get; }
        public CoupledCellSnapshot PhysicalState { get; }
        internal CoupledColonyCell(int world, int cell, double volume, CoupledCellSnapshot physical)
        { WorldId = world; GlobalCellIndex = cell; VolumeCubicMetres = volume; PhysicalState = physical; }
    }

    /// <summary>One completed all-world physical frame and its read-only Core projection. Contains no native handles.</summary>
    public sealed class CoupledColonyFrame
    {
        public ColonyFrameSnapshot Projection { get; }
        public long CompletedFrame => Projection.CompletedFrame;
        public double SimulationTimeSeconds => Projection.SimulationTimeSeconds;
        public Guid ColonyId => Projection.ColonyId;
        public FluidMaterialCatalog MaterialCatalog => Projection.Catalog;
        public MixturePresentationPublication? Presentation { get; }
        public IReadOnlyDictionary<int, CoupledWorldSnapshot> Worlds { get; }
        public IReadOnlyDictionary<int, CoupledStepDiagnostics> Diagnostics { get; }
        /// <summary>Measured U error of the scalar total-energy projection; never heat or a replacement physical U.</summary>
        public IReadOnlyDictionary<int, double> InternalEnergyProjectionErrorsJoules { get; }
        public IReadOnlyList<CoupledMaterialDefinition> Materials { get; }
        internal CoupledColonyFrame(ColonyFrameSnapshot projection, Dictionary<int, CoupledWorldSnapshot> worlds,
            Dictionary<int, CoupledStepDiagnostics> diagnostics, Dictionary<int, double> errors, IReadOnlyList<CoupledMaterialDefinition> materials,
            MixturePresentationModel? presentationModel)
        {
            Projection = projection; Worlds = new ReadOnlyDictionary<int, CoupledWorldSnapshot>(worlds);
            Diagnostics = new ReadOnlyDictionary<int, CoupledStepDiagnostics>(diagnostics);
            InternalEnergyProjectionErrorsJoules = new ReadOnlyDictionary<int, double>(errors); Materials = materials;
            Presentation = presentationModel == null ? null : MixturePresentationPublication.Prepare(this, presentationModel);
        }
        public bool TrySampleCell(int globalCell, out CoupledColonyCell cell)
        {
            cell = default;
            if (!Projection.Geometry.TryLocate(globalCell, out var placement, out _)) return false;
            var x = globalCell % Projection.Width - placement.OffsetX;
            var y = globalCell / Projection.Width - placement.OffsetY;
            var physical = Worlds[placement.WorldId].Cells[checked(x + y * placement.Width)];
            cell = new CoupledColonyCell(placement.WorldId, globalCell, Projection.CellVolumeCubicMetres, physical); return true;
        }
        public CoupledColonyCell SampleCell(int globalCell) => TrySampleCell(globalCell, out var cell) ? cell :
            throw new ArgumentOutOfRangeException(nameof(globalCell), "Cell is outside every admitted world.");

        /// <summary>Physical facts of this closed constant-density-liquid reference, without a total-energy round trip.</summary>
        public MixtureThermodynamicReading ReadThermodynamicCell(int globalCell)
        {
            var sample = SampleCell(globalCell); var physical = sample.PhysicalState;
            if (physical.IsWall) throw new ArgumentException("A solid cell has no mixture reading.", nameof(globalCell));
            double liquidVolume = 0, gasMoles = 0;
            // Match the native registry order and the admitted model's constant
            // condensed density. This is not an elastic-liquid EOS inversion.
            foreach (var definition in Materials)
                if (physical.MassesKg.TryGetValue(definition.Id, out var mass))
                {
                    if (definition.Phase == CoupledMaterialPhase.Gas) gasMoles += mass / definition.MolarMassKgPerMol;
                    else if (definition.Phase == CoupledMaterialPhase.Liquid) liquidVolume += mass / definition.CondensedDensityKgPerCubicMetre;
                    else throw new InvalidOperationException("A fluid snapshot contains solid material.");
                }
            var gasVolume = gasMoles == 0 ? 0 : sample.VolumeCubicMetres - liquidVolume;
            var constituents = new List<MixtureThermodynamicConstituent>(physical.MassesKg.Count);
            foreach (var definition in Materials)
                if (physical.MassesKg.TryGetValue(definition.Id, out var mass))
                    constituents.Add(new MixtureThermodynamicConstituent(definition.Id,
                        definition.Phase == CoupledMaterialPhase.Gas ? NativeMatterPhase.Gas : NativeMatterPhase.Liquid,
                        mass, definition.MolarMassKgPerMol, definition.Phase == CoupledMaterialPhase.Gas ?
                        MixtureThermodynamicReading.ProductRatio(gasVolume, mass / definition.MolarMassKgPerMol, 1, gasMoles) :
                        mass / definition.CondensedDensityKgPerCubicMetre));
            var kinetic = (physical.MomentumX * physical.MomentumX + physical.MomentumY * physical.MomentumY) / (2 * physical.TotalMassKg);
            return new MixtureThermodynamicReading(constituents, physical.TotalMassKg, physical.InternalEnergyJoules, kinetic,
                physical.TemperatureKelvin, gasMoles == 0 ? 0 : physical.PressurePa, physical.PressurePa,
                sample.VolumeCubicMetres, gasVolume, liquidVolume, 0, CoupledSimulationWorld.ModelIdentity, MaterialCatalog.Fingerprint,
                Projection.Revision, "owned-cell:" + globalCell.ToString(System.Globalization.CultureInfo.InvariantCulture),
                physical.MomentumX, physical.MomentumY);
        }
    }

    /// <summary>
    /// All-world coordinator for the explicit closed reference. Physical U/P remain in immutable native owners.
    /// The writable Core projection authority is private; actor mutations, save and ONI takeover are not supplied.
    /// </summary>
    public sealed class CoupledColonySimulation : IDisposable
    {
        private readonly object sync = new object();
        private readonly ColonyFluidState projectionState;
        private readonly IReadOnlyList<CoupledMaterialDefinition> materials;
        private readonly MixturePresentationModel? presentationModel;
        private Dictionary<int, CoupledSimulationWorld> owners;
        private Dictionary<int, CoupledSimulationWorld>? retiredOwners;
        private CoupledColonyFrame current;
        private bool disposed;

        private CoupledColonySimulation(ColonyFluidState projection, Dictionary<int, CoupledSimulationWorld> owned,
            IReadOnlyList<CoupledMaterialDefinition> definitions, CoupledColonyFrame initial, MixturePresentationModel? model)
        { projectionState = projection; owners = owned; materials = definitions; current = initial; presentationModel = model; }

        public static FluidMaterialCatalog ReadMaterialCatalog() => Catalog(CoupledSimulationWorld.ReadMaterialDefinitions());
        private static FluidMaterialCatalog Catalog(IReadOnlyList<CoupledMaterialDefinition> definitions)
            => new FluidMaterialCatalog(CoupledSimulationWorld.ModelIdentity + "/read-projection-v1",
                definitions.Where(value => value.Phase != CoupledMaterialPhase.Solid).Select(value => new FluidMaterial(value.Id,
                    value.Phase == CoupledMaterialPhase.Gas ? NativeMatterPhase.Gas : NativeMatterPhase.Liquid,
                    value.MolarMassKgPerMol, value.SpecificHeatCv,
                    value.Phase == CoupledMaterialPhase.Liquid ? value.CondensedDensityKgPerCubicMetre : 0, value.EnergyOffsetJoulesPerKg)));

        public static CoupledColonySimulation Create(NativeColonyImport import, CoupledWorldConfiguration configuration,
            IReadOnlyDictionary<int, double>? pureLiquidPressuresPa = null, MixturePresentationModel? presentationModel = null)
        {
            if (import == null) throw new ArgumentNullException(nameof(import));
            if (configuration == null) throw new ArgumentNullException(nameof(configuration));
            var config = configuration.ToNative();
            if (config.Width != import.Width || config.Height != import.Height)
                throw new ArgumentException("Coupled colony configuration must name the complete global grid.");
            var volume = config.CellSize * config.CellSize * config.Depth; FluidChecks.Positive(volume, "coupled cell volume");
            var definitions = CoupledSimulationWorld.ReadMaterialDefinitions();
            var catalog = Catalog(definitions);
            var projection = ColonyFluidState.Import(import, catalog, volume); var basis = projection.CaptureSnapshot();
            presentationModel?.RequireCatalog(basis);
            var pressures = pureLiquidPressuresPa == null ? new Dictionary<int, double>() : pureLiquidPressuresPa.ToDictionary(entry => entry.Key, entry => entry.Value);
            var usedPressures = new HashSet<int>(); var staged = new List<(NativeWorldPlacement placement, CoupledCellInitialization[] cells)>();
            foreach (var placement in basis.Worlds.OrderBy(value => value.WorldId))
            {
                var cells = new CoupledCellInitialization[checked(placement.Width * placement.Height)];
                for (var y = 0; y < placement.Height; ++y) for (var x = 0; x < placement.Width; ++x)
                {
                    var global = checked(placement.OffsetX + x + (placement.OffsetY + y) * basis.Width); var source = basis.SampleCell(global);
                    if (source.IsSolid) { cells[x + y * placement.Width] = CoupledCellInitialization.Wall(); continue; }
                    var inventory = source.Inventory;
                    if (inventory.TotalMassKg == 0) throw new NotSupportedException("Closed coupled model cannot admit a vacuum or metadata-only fluid cell.");
                    var pressure = 0.0;
                    if (!inventory.MassesKg.Any(entry => catalog.Materials[entry.Key].Phase == NativeMatterPhase.Gas && entry.Value > 0))
                    {
                        if (!pressures.TryGetValue(global, out pressure)) throw new ArgumentException("Pure-liquid initialization requires explicit mechanical pressure for cell " + global);
                        FluidChecks.Nonnegative(pressure, "initial liquid pressure"); usedPressures.Add(global);
                    }
                    cells[x + y * placement.Width] = new CoupledCellInitialization(inventory.MassesKg, inventory.InternalEnergyJoules,
                        inventory.MomentumX, inventory.MomentumY, pressure).WithPassiveMetadata(inventory.DiseaseCounts, inventory.RadiationQuantity);
                }
                staged.Add((placement, cells));
            }
            if (usedPressures.Count != pressures.Count) throw new ArgumentException("Initial liquid pressure map contains unrelated cells.");
            var owned = new Dictionary<int, CoupledSimulationWorld>();
            try
            {
                foreach (var world in staged) owned.Add(world.placement.WorldId,
                    CoupledSimulationWorld.Create(Configuration(config, world.placement), world.cells));
                var frames = owned.ToDictionary(entry => entry.Key, entry => entry.Value.CaptureSnapshot());
                var updates = Project(basis, frames, out var errors);
                using var prepared = projection.Prepare(basis, updates, new Dictionary<int, int>(), null, basis.NextContainerGeneration,
                    FluidInventory.Empty, FluidInventory.Empty, 0, 0, 0);
                var initial = new CoupledColonyFrame(prepared.Snapshot, frames, new Dictionary<int, CoupledStepDiagnostics>(), errors, definitions, presentationModel);
                // Allocate the complete outer owner before the publication point too.
                var result = new CoupledColonySimulation(projection, owned, definitions, initial, presentationModel);
                prepared.ReserveWriter(); prepared.Commit(initial); return result;
            }
            catch { foreach (var owner in owned.Values) owner.Dispose(); throw; }
        }
        private static CoupledWorldConfiguration Configuration(CoupledNativeConfig config, NativeWorldPlacement world) => new CoupledWorldConfiguration
        {
            Width = checked((uint)world.Width), Height = checked((uint)world.Height), CellSizeMetres = config.CellSize,
            DepthMetres = config.Depth, Gravity = config.Gravity, Diffusivity = config.Diffusivity, Viscosity = config.Viscosity,
            MaximumTimestepSeconds = config.MaximumTimestep, MaximumOutgoingFraction = config.MaximumOutgoingFraction,
            ThermalConduction = config.Conduction != 0, Preconditioner = (CoupledPressurePreconditioner)config.Preconditioner
        };
        private static Dictionary<int, FluidInventory> Project(ColonyFrameSnapshot basis, Dictionary<int, CoupledWorldSnapshot> frames,
            out Dictionary<int, double> errors)
        {
            var updates = new Dictionary<int, FluidInventory>(); errors = new Dictionary<int, double>();
            foreach (var placement in basis.Worlds)
            {
                var world = frames[placement.WorldId];
                if (world.Width != placement.Width || world.Height != placement.Height) throw new InvalidOperationException("Coupled world footprint changed.");
                for (var y = 0; y < placement.Height; ++y) for (var x = 0; x < placement.Width; ++x)
                {
                    var global = checked(placement.OffsetX + x + (placement.OffsetY + y) * basis.Width);
                    var physical = world.Cells[x + y * placement.Width];
                    if (!basis.Geometry.TryLocate(global, out _, out var element) || physical.IsWall != (element != basis.Geometry.VacuumHash))
                        throw new InvalidOperationException("Coupled geometry differs from the admitted colony.");
                    if (physical.IsWall) continue;
                    double mass = 0; foreach (var value in physical.MassesKg.Values) mass += value;
                    if (mass <= 0) throw new InvalidOperationException("Closed coupled frame contains vacuum.");
                    var kinetic = .5 * (physical.MomentumX * (physical.MomentumX / mass) + physical.MomentumY * (physical.MomentumY / mass));
                    var inventory = new FluidInventory(physical.MassesKg, physical.InternalEnergyJoules + kinetic,
                        physical.MomentumX, physical.MomentumY, physical.DiseaseCounts, physical.RadiationQuantity);
                    var error = inventory.InternalEnergyJoules - physical.InternalEnergyJoules;
                    var scale = Math.Max(Math.Abs(physical.InternalEnergyJoules), Math.Abs(kinetic));
                    FluidChecks.Finite(error, "internal-energy projection error");
                    if (Math.Abs(error) > 64 * 2.2204460492503131e-16 * scale)
                        throw new InvalidOperationException("Read-only total-energy projection exceeds its arithmetic bound.");
                    if (error != 0) errors.Add(global, error);
                    updates.Add(global, inventory);
                }
            }
            return updates;
        }
        public CoupledColonyFrame CaptureFrame() { lock (sync) { CheckOpen(); return current; } }
        public CoupledColonyFrame AdvanceFrame(long completedFrame, double seconds, ulong attemptBudget = 100000)
        {
            lock (sync)
            {
                CheckOpen(); FluidChecks.Positive(seconds, nameof(seconds));
                if (seconds > 10 || attemptBudget == 0 || completedFrame != checked(current.CompletedFrame + 1))
                    throw new ArgumentException("Advance exactly the next global frame with a bounded positive duration/budget.");
                // Native retirement can fail, so do it before any new frame is
                // staged. Keep at most one retired generation between calls.
                if (retiredOwners != null) { foreach (var owner in retiredOwners.Values) owner.Dispose(); retiredOwners = null; }
                var time = current.SimulationTimeSeconds + seconds; FluidChecks.Finite(time, "completed simulation time");
                if (time <= current.SimulationTimeSeconds) throw new InvalidOperationException("Simulation clock cannot represent this advance.");
                var nextOwners = new Dictionary<int, CoupledSimulationWorld>(); var frames = new Dictionary<int, CoupledWorldSnapshot>();
                var diagnostics = new Dictionary<int, CoupledStepDiagnostics>();
                try
                {
                    var remaining = attemptBudget;
                    foreach (var source in owners.OrderBy(entry => entry.Key))
                    {
                        if (remaining == 0) throw new InvalidOperationException("All-world attempt budget exhausted before publication.");
                        var advanced = source.Value.Advance(seconds, remaining);
                        try { nextOwners.Add(source.Key, advanced.World); } catch { advanced.World.Dispose(); throw; }
                        var attempts = checked(advanced.Diagnostics.Substeps + advanced.Diagnostics.RejectedSubsteps);
                        if (attempts == 0 || attempts > remaining) throw new InvalidOperationException("Native attempt accounting differs.");
                        remaining -= attempts;
                        var captured = advanced.World.CaptureSnapshot();
                        if (captured.CompletedTimeSeconds != time || captured.Revision != checked(current.Worlds[source.Key].Revision + 1) ||
                            advanced.Diagnostics.CompletedTimeSeconds != time || advanced.Diagnostics.Revision != captured.Revision)
                            throw new InvalidOperationException("Native worlds did not complete the identical frame clock.");
                        frames.Add(source.Key, captured); diagnostics.Add(source.Key, advanced.Diagnostics);
                    }
                    var basis = projectionState.CaptureSnapshot();
                    if (!ReferenceEquals(basis, current.Projection)) throw new InvalidOperationException("Private projection publication changed unexpectedly.");
                    var updates = Project(basis, frames, out var errors);
                    using var prepared = projectionState.Prepare(basis, updates, new Dictionary<int, int>(), null, basis.NextContainerGeneration,
                        FluidInventory.Empty, FluidInventory.Empty, 0, completedFrame, time);
                    var candidate = new CoupledColonyFrame(prepared.Snapshot, frames, diagnostics, errors, materials, presentationModel);
                    prepared.ReserveWriter(); prepared.Commit(candidate);
                    retiredOwners = owners; owners = nextOwners; current = candidate;
                    return candidate;
                }
                catch (Exception error)
                {
                    if (!ReferenceEquals(owners, nextOwners)) foreach (var owner in nextOwners.Values) owner.Dispose();
                    throw new CoupledColonyAdvanceException(completedFrame, frames.Count, error);
                }
            }
        }
        private void CheckOpen() { if (disposed) throw new ObjectDisposedException(nameof(CoupledColonySimulation)); }
        public void Dispose()
        {
            lock (sync)
            {
                if (disposed) return; disposed = true;
                foreach (var owner in owners.Values) owner.Dispose();
                if (retiredOwners != null) foreach (var owner in retiredOwners.Values) owner.Dispose();
            }
        }
    }
}
