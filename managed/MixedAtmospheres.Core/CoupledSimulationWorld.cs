using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;

namespace MixedAtmospheres.Core
{
    public enum CoupledMaterialPhase { Gas, Liquid, Solid }
    public enum CoupledPressurePreconditioner { Automatic, Jacobi, IncompleteCholesky }
    public sealed class CoupledWorldConfiguration
    {
        public uint Width { get; set; } = 16;
        public uint Height { get; set; } = 16;
        public double CellSizeMetres { get; set; } = 1;
        public double DepthMetres { get; set; } = 1;
        public double Gravity { get; set; } = 9.81;
        public double Diffusivity { get; set; } = 2e-5;
        public double Viscosity { get; set; } = 1.5e-5;
        public double MaximumTimestepSeconds { get; set; } = .1;
        public double MaximumOutgoingFraction { get; set; } = .45;
        public bool ThermalConduction { get; set; } = true;
        public CoupledPressurePreconditioner Preconditioner { get; set; }
        internal CoupledNativeConfig ToNative() => new CoupledNativeConfig
        {
            Size = 96, Version = 1, Model = 1, Width = Width, Height = Height, CellSize = CellSizeMetres, Depth = DepthMetres,
            Gravity = Gravity, Diffusivity = Diffusivity, Viscosity = Viscosity, MaximumTimestep = MaximumTimestepSeconds,
            MaximumOutgoingFraction = MaximumOutgoingFraction, Conduction = ThermalConduction ? 1U : 0U, Preconditioner = (uint)Preconditioner
        };
    }
    public sealed class CoupledMaterialDefinition
    {
        public string Id { get; }
        public CoupledMaterialPhase Phase { get; }
        public double MolarMassKgPerMol { get; }
        public double SpecificHeatCv { get; }
        public double EnergyOffsetJoulesPerKg { get; }
        public double CondensedDensityKgPerCubicMetre { get; }
        internal CoupledMaterialDefinition(string id, CoupledNativeSpecies value)
        {
            if (value.Phase > 2 || value.Reserved != 0) throw new NotSupportedException("Unsupported coupled material metadata.");
            Id = id; Phase = (CoupledMaterialPhase)value.Phase; MolarMassKgPerMol = value.MolarMass;
            SpecificHeatCv = value.Cv; EnergyOffsetJoulesPerKg = value.EnergyOffset; CondensedDensityKgPerCubicMetre = value.Density;
        }
    }
    public sealed class CoupledCellInitialization
    {
        public IReadOnlyDictionary<string, double> MassesKg { get; }
        public double InternalEnergyJoules { get; }
        public double MomentumX { get; }
        public double MomentumY { get; }
        public double LiquidPressurePa { get; }
        public bool IsWall { get; }
        public CoupledCellInitialization(IReadOnlyDictionary<string, double> masses, double internalEnergyJoules,
            double momentumX = 0, double momentumY = 0, double liquidPressurePa = 101325)
            : this(masses, internalEnergyJoules, momentumX, momentumY, liquidPressurePa, false) { }
        private CoupledCellInitialization(IReadOnlyDictionary<string, double> masses, double energy, double px, double py, double p, bool wall)
        {
            if (masses == null) throw new ArgumentNullException(nameof(masses));
            var owned = new Dictionary<string, double>(StringComparer.Ordinal);
            foreach (var entry in masses)
            {
                FluidChecks.Identity(entry.Key, nameof(masses)); FluidChecks.Nonnegative(entry.Value, nameof(masses));
                owned.Add(entry.Key, entry.Value);
            }
            FluidChecks.Finite(energy, nameof(energy)); FluidChecks.Finite(px, nameof(px)); FluidChecks.Finite(py, nameof(py));
            FluidChecks.Nonnegative(p, nameof(p));
            MassesKg = new ReadOnlyDictionary<string, double>(owned); InternalEnergyJoules = energy;
            MomentumX = px; MomentumY = py; LiquidPressurePa = p; IsWall = wall;
        }
        public static CoupledCellInitialization Wall() => new CoupledCellInitialization(new Dictionary<string, double>(), 0, 0, 0, 0, true);
    }
    public sealed class CoupledCellSnapshot
    {
        public IReadOnlyDictionary<string, double> MassesKg { get; }
        public double InternalEnergyJoules { get; }
        public double MomentumX { get; }
        public double MomentumY { get; }
        public double PressurePa { get; }
        public double TemperatureKelvin { get; }
        public double LiquidVolumeFraction { get; }
        public double TotalMassKg { get; }
        public bool IsWall { get; }
        internal CoupledCellSnapshot(CoupledNativeCell value, Dictionary<string, double> masses)
        {
            if (value.Wall > 1 || value.Reserved != 0) throw new NotSupportedException("Unsupported coupled cell metadata.");
            MassesKg = new ReadOnlyDictionary<string, double>(masses); InternalEnergyJoules = value.InternalEnergy;
            MomentumX = value.MomentumX; MomentumY = value.MomentumY; PressurePa = value.Pressure;
            TemperatureKelvin = value.Temperature; LiquidVolumeFraction = value.LiquidFraction;
            TotalMassKg = value.Mass; IsWall = value.Wall != 0;
        }
    }
    public sealed class CoupledWorldSnapshot
    {
        public ulong Revision { get; }
        public double CompletedTimeSeconds { get; }
        public uint Width { get; }
        public uint Height { get; }
        public IReadOnlyList<CoupledCellSnapshot> Cells { get; }
        public double InternalEnergyJoules { get; }
        public double KineticEnergyJoules { get; }
        public double PotentialEnergyJoules { get; }
        internal CoupledWorldSnapshot(CoupledNativeSnapshot value, uint width, uint height, CoupledCellSnapshot[] cells)
        {
            Revision = value.Revision; CompletedTimeSeconds = value.Time; Width = width; Height = height;
            Cells = Array.AsReadOnly(cells); InternalEnergyJoules = value.InternalEnergy;
            KineticEnergyJoules = value.KineticEnergy; PotentialEnergyJoules = value.PotentialEnergy;
        }
    }
    public sealed class CoupledStepDiagnostics
    {
        public double SimulatedSeconds { get; }
        public double CompletedTimeSeconds { get; }
        public ulong Revision { get; }
        public ulong Substeps { get; }
        public ulong RejectedSubsteps { get; }
        public ulong PressureIterations { get; }
        public ulong NonlinearIterations { get; }
        public ulong ThermalIterations { get; }
        public double MaximumLinearResidualRatio { get; }
        public double MaximumVolumeResidual { get; }
        public double MaximumConservationErrorJoules { get; }
        public double InitialProjectionHeatJoules { get; }
        public double FinalProjectionHeatJoules { get; }
        public double PressureWorkRoundoffJoules { get; }
        internal CoupledStepDiagnostics(CoupledNativeStep value)
        {
            SimulatedSeconds = value.Seconds; CompletedTimeSeconds = value.CompletedTime; Revision = value.Revision;
            Substeps = value.Substeps; RejectedSubsteps = value.RejectedSubsteps; PressureIterations = value.PressureIterations;
            NonlinearIterations = value.NonlinearIterations; ThermalIterations = value.ThermalIterations;
            MaximumLinearResidualRatio = value.MaximumLinearResidualRatio; MaximumVolumeResidual = value.MaximumVolumeResidual;
            MaximumConservationErrorJoules = value.MaximumConservationError; InitialProjectionHeatJoules = value.InitialProjectionHeat;
            FinalProjectionHeatJoules = value.FinalProjectionHeat; PressureWorkRoundoffJoules = value.PressureWorkRoundoff;
        }
    }
    public sealed class CoupledAdvance
    {
        public CoupledSimulationWorld World { get; }
        public CoupledStepDiagnostics Diagnostics { get; }
        internal CoupledAdvance(CoupledSimulationWorld world, CoupledNativeStep result)
        { World = world; Diagnostics = new CoupledStepDiagnostics(result); }
    }

    /// <summary>Immutable forks of the explicit closed gas/liquid reference. Not the completed production solver.</summary>
    public sealed class CoupledSimulationWorld : IDisposable
    {
        public const string ModelIdentity = "mixed-atmospheres-closed-fluid-reference-v1";
        private readonly object sync = new object();
        private readonly CoupledWorldHandle handle;
        private readonly CoupledNativeConfig config;
        private readonly CoupledMaterialDefinition[] materials;
        private bool disposed;
        public IReadOnlyList<CoupledMaterialDefinition> Materials { get; }
        public uint Width => config.Width;
        public uint Height => config.Height;
        public double CellVolumeCubicMetres => config.CellSize * config.CellSize * config.Depth;
        private CoupledSimulationWorld(CoupledWorldHandle owner, CoupledMaterialDefinition[] definitions)
        {
            handle = owner; materials = definitions; Materials = Array.AsReadOnly(definitions);
            CoupledNativeMethods.Check(CoupledNativeMethods.Config(owner, out config));
            if (config.Version != 1 || config.Model != 1) throw new NotSupportedException("Unsupported coupled physical model.");
        }
        public static IReadOnlyList<CoupledMaterialDefinition> ReadMaterialDefinitions()
        {
            CoupledNativeMethods.CheckAbi(); CoupledNativeMethods.Check(CoupledNativeMethods.SpeciesCount(out var count));
            var result = new CoupledMaterialDefinition[checked((int)count)];
            for (uint i = 0; i < count; ++i)
            {
                CoupledNativeMethods.Check(CoupledNativeMethods.SpeciesId(i, null, 0, out var size));
                var bytes = new byte[checked((int)size)];
                CoupledNativeMethods.Check(CoupledNativeMethods.SpeciesId(i, bytes, size, out _));
                CoupledNativeMethods.Check(CoupledNativeMethods.SpeciesDefinition(i, out var definition));
                result[i] = new CoupledMaterialDefinition(Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1), definition);
            }
            return Array.AsReadOnly(result);
        }
        public static CoupledSimulationWorld Create(CoupledWorldConfiguration configuration, IReadOnlyList<CoupledCellInitialization> cells)
        {
            if (configuration == null) throw new ArgumentNullException(nameof(configuration));
            if (cells == null) throw new ArgumentNullException(nameof(cells));
            var config = configuration.ToNative(); var definitions = ReadMaterialDefinitions().ToArray();
            var count = (ulong)config.Width * config.Height;
            if (count == 0 || count > 1000000 || count != (ulong)cells.Count) throw new ArgumentException("Coupled initialization dimensions differ.");
            var source = cells.ToArray(); var inputs = new CoupledNativeInput[source.Length];
            var masses = new double[checked(source.Length * definitions.Length)];
            var indices = definitions.Select((value, index) => new { value.Id, index }).ToDictionary(value => value.Id, value => value.index, StringComparer.Ordinal);
            for (var i = 0; i < source.Length; ++i)
            {
                var cell = source[i] ?? throw new ArgumentException("Null coupled initialization cell.");
                inputs[i] = new CoupledNativeInput { InternalEnergy = cell.InternalEnergyJoules, MomentumX = cell.MomentumX,
                    MomentumY = cell.MomentumY, LiquidPressure = cell.LiquidPressurePa, Wall = cell.IsWall ? 1U : 0U };
                foreach (var entry in cell.MassesKg)
                {
                    if (!indices.TryGetValue(entry.Key, out var index)) throw new ArgumentException("Unknown coupled material: " + entry.Key);
                    masses[i * definitions.Length + index] = entry.Value;
                }
            }
            CoupledNativeMethods.Check(CoupledNativeMethods.Create(config, inputs, (ulong)inputs.Length, masses, (ulong)masses.Length, out var pointer));
            return Adopt(pointer, definitions);
        }
        private static CoupledSimulationWorld Adopt(IntPtr pointer, CoupledMaterialDefinition[] definitions)
        {
            CoupledWorldHandle? owner = null;
            try { owner = new CoupledWorldHandle(pointer); return new CoupledSimulationWorld(owner, definitions); }
            catch { if (owner == null) CoupledNativeMethods.Destroy(pointer); else owner.Dispose(); throw; }
        }
        public CoupledAdvance Advance(double seconds, ulong attemptBudget = 100000)
        {
            lock (sync)
            {
                CheckOpen(); CoupledNativeMethods.Check(CoupledNativeMethods.Advance(handle, seconds, attemptBudget, out var pointer, out var result));
                var child = Adopt(pointer, materials);
                try { return new CoupledAdvance(child, result); } catch { child.Dispose(); throw; }
            }
        }
        public CoupledWorldSnapshot CaptureSnapshot()
        {
            lock (sync)
            {
                CheckOpen(); var cells = new CoupledNativeCell[checked((int)((ulong)config.Width * config.Height))];
                var masses = new double[checked(cells.Length * materials.Length)];
                CoupledNativeMethods.Check(CoupledNativeMethods.Snapshot(handle, out var header, cells, (ulong)cells.Length, masses, (ulong)masses.Length));
                if (header.Cells != (ulong)cells.Length || header.MassValues != (ulong)masses.Length || header.Species != materials.Length ||
                    header.Reserved != 0 || header.Reserved2 != 0 || header.Reserved3 != 0) throw new NotSupportedException("Coupled snapshot shape differs.");
                var owned = new CoupledCellSnapshot[cells.Length];
                for (var i = 0; i < cells.Length; ++i)
                {
                    var contents = new Dictionary<string, double>(StringComparer.Ordinal);
                    for (var s = 0; s < materials.Length; ++s) if (masses[i * materials.Length + s] != 0)
                        contents.Add(materials[s].Id, masses[i * materials.Length + s]);
                    owned[i] = new CoupledCellSnapshot(cells[i], contents);
                }
                return new CoupledWorldSnapshot(header, config.Width, config.Height, owned);
            }
        }
        private void CheckOpen() { if (disposed) throw new ObjectDisposedException(nameof(CoupledSimulationWorld)); }
        public void Dispose() { lock (sync) { if (disposed) return; disposed = true; handle.Dispose(); } }
    }
}
