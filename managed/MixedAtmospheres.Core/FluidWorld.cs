using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Text;

namespace MixedAtmospheres.Core
{
    public sealed class WorldConfiguration
    {
        public uint Width { get; set; } = 16;
        public uint Height { get; set; } = 16;
        public double CellSizeMetres { get; set; } = 1;
        public double DepthMetres { get; set; } = 1;
        public double Gravity { get; set; } = 9.81;
        public double Diffusivity { get; set; } = 2e-5;
        public bool ThermalConduction { get; set; } = true;
        public bool OpenTop { get; set; }
        internal NativeConfig ToNative() => new NativeConfig
        {
            Width = Width, Height = Height, CellSize = CellSizeMetres, Depth = DepthMetres,
            Gravity = Gravity, Diffusivity = Diffusivity, Conduction = ThermalConduction ? 1U : 0U, OpenTop = OpenTop ? 1U : 0U
        };
    }

    public sealed class FluidSample
    {
        public IReadOnlyDictionary<string, double> MassesKg { get; }
        public double TotalMassKg { get; }
        public double TotalMassGrams => TotalMassKg * 1000;
        public double TemperatureKelvin { get; }
        public double PressurePa { get; }
        public double InternalEnergyJoules { get; }
        public double VelocityX { get; }
        public double VelocityY { get; }
        public bool IsWall { get; }
        internal FluidSample(NativeSample sample, Dictionary<string, double> masses)
        {
            MassesKg = new ReadOnlyDictionary<string, double>(masses);
            TotalMassKg = sample.Mass; TemperatureKelvin = sample.Temperature; PressurePa = sample.Pressure;
            InternalEnergyJoules = sample.Energy; VelocityX = sample.VelocityX; VelocityY = sample.VelocityY;
            IsWall = sample.Wall != 0;
        }
    }

    public sealed class ConservationLedger
    {
        public IReadOnlyDictionary<string, double> MassesKg { get; }
        public IReadOnlyDictionary<string, double> EscapedMassesKg { get; }
        public double EnergyJoules { get; }
        public double EscapedEnergyJoules { get; }
        internal ConservationLedger(Dictionary<string, double> masses, Dictionary<string, double> escaped, double energy, double escapedEnergy)
        {
            MassesKg = new ReadOnlyDictionary<string, double>(masses);
            EscapedMassesKg = new ReadOnlyDictionary<string, double>(escaped);
            EnergyJoules = energy; EscapedEnergyJoules = escapedEnergy;
        }
    }

    public readonly struct StepResult
    {
        public double SimulatedSeconds { get; }
        public ulong Substeps { get; }
        internal StepResult(NativeStep result) { SimulatedSeconds = result.Seconds; Substeps = result.Substeps; }
    }

    /// <summary>Thread-serialized binding to the standalone gas reference solver.</summary>
    public sealed class FluidWorld : IDisposable
    {
        private readonly object sync = new object();
        private readonly WorldHandle handle;
        private readonly string[] species;
        private readonly Dictionary<string, int> indices;
        private bool disposed;
        public IReadOnlyList<string> SpeciesIds { get; }
        public uint Width { get; }
        public uint Height { get; }

        public FluidWorld(WorldConfiguration configuration) : this(Create(configuration)) { }

        private FluidWorld(IntPtr pointer)
        {
            handle = new WorldHandle(pointer);
            try
            {
                NativeMethods.Check(NativeMethods.GetConfig(handle, out var config));
                Width = config.Width; Height = config.Height;
                NativeMethods.Check(NativeMethods.SpeciesCount(handle, out var count));
                species = new string[checked((int)count)];
                indices = new Dictionary<string, int>(StringComparer.Ordinal);
                for (uint i = 0; i < count; ++i)
                {
                    NativeMethods.Check(NativeMethods.SpeciesId(handle, i, null, 0, out var length));
                    var bytes = new byte[checked((int)length)];
                    NativeMethods.Check(NativeMethods.SpeciesId(handle, i, bytes, length, out _));
                    species[i] = Encoding.UTF8.GetString(bytes, 0, bytes.Length - 1);
                    indices.Add(species[i], checked((int)i));
                }
                SpeciesIds = Array.AsReadOnly(species);
            }
            catch { handle.Dispose(); throw; }
        }

        private static void CheckAbi()
        {
            if (NativeMethods.AbiVersion() != 1) throw new NotSupportedException("Native Mixed Atmospheres ABI mismatch.");
        }

        private static IntPtr Create(WorldConfiguration configuration)
        {
            if (configuration == null) throw new ArgumentNullException(nameof(configuration));
            CheckAbi();
            NativeMethods.Check(NativeMethods.Create(configuration.ToNative(), out var pointer));
            return pointer;
        }

        public static FluidWorld Restore(byte[] save)
        {
            if (save == null) throw new ArgumentNullException(nameof(save));
            CheckAbi();
            NativeMethods.Check(NativeMethods.Load(save, (ulong)save.LongLength, out var pointer));
            return new FluidWorld(pointer);
        }

        private void CheckOpen() { if (disposed) throw new ObjectDisposedException(nameof(FluidWorld)); }
        private Dictionary<string, double> NameMasses(double[] masses)
        {
            var result = new Dictionary<string, double>(StringComparer.Ordinal);
            for (var i = 0; i < species.Length; ++i) result.Add(species[i], masses[i]);
            return result;
        }

        /// <summary>Initialization operation. Runtime machine transfers must use transactional inventory adapters.</summary>
        public void InitializeCell(uint x, uint y, IReadOnlyDictionary<string, double> massesKg, double temperatureKelvin,
                                   double velocityX = 0, double velocityY = 0)
        {
            if (massesKg == null) throw new ArgumentNullException(nameof(massesKg));
            lock (sync)
            {
                CheckOpen();
                var masses = new double[species.Length];
                foreach (var entry in massesKg)
                {
                    if (!indices.TryGetValue(entry.Key, out var index)) throw new ArgumentException("Unknown species: " + entry.Key, nameof(massesKg));
                    masses[index] = entry.Value;
                }
                NativeMethods.Check(NativeMethods.SetCell(handle, x, y, masses, (uint)masses.Length, temperatureKelvin, velocityX, velocityY));
            }
        }

        public void SetWall(uint x, uint y, bool wall)
        {
            lock (sync) { CheckOpen(); NativeMethods.Check(NativeMethods.SetWall(handle, x, y, wall ? 1U : 0U)); }
        }

        public FluidSample Sample(uint x, uint y)
        {
            lock (sync)
            {
                CheckOpen(); var masses = new double[species.Length];
                NativeMethods.Check(NativeMethods.GetCell(handle, x, y, out var sample, masses, (uint)masses.Length));
                return new FluidSample(sample, NameMasses(masses));
            }
        }

        public Breathability AssessAtmosphere(uint x, uint y, AtmosphereSettings? settings = null)
        {
            lock (sync)
            {
                CheckOpen();
                NativeMethods.Check(NativeMethods.Assess(handle, x, y, (settings ?? new AtmosphereSettings()).ToNative(), out var result));
                return new Breathability(result);
            }
        }

        public StepResult Step(double seconds, ulong substepBudget = 100000)
        {
            lock (sync) { CheckOpen(); NativeMethods.Check(NativeMethods.Step(handle, seconds, substepBudget, out var result)); return new StepResult(result); }
        }

        public ConservationLedger Totals()
        {
            lock (sync)
            {
                CheckOpen(); var masses = new double[species.Length]; var escaped = new double[species.Length];
                NativeMethods.Check(NativeMethods.Totals(handle, masses, (uint)masses.Length, out var energy, escaped, out var escapedEnergy));
                return new ConservationLedger(NameMasses(masses), NameMasses(escaped), energy, escapedEnergy);
            }
        }

        public byte[] Save()
        {
            lock (sync)
            {
                CheckOpen();
                NativeMethods.Check(NativeMethods.Save(handle, null, 0, out var size));
                var bytes = new byte[checked((int)size)];
                NativeMethods.Check(NativeMethods.Save(handle, bytes, size, out var written));
                if (written != size) throw new InvalidOperationException("Native snapshot size changed while synchronized.");
                return bytes;
            }
        }

        public void Dispose()
        {
            lock (sync) { if (disposed) return; disposed = true; handle.Dispose(); }
        }
    }
}
