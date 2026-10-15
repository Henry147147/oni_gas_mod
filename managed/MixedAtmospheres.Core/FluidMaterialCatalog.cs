using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Core
{
    /// <summary>Explicit constant-property fluid model. No unknown material falls back to another material.</summary>
    public sealed class FluidMaterial
    {
        public string Id { get; }
        public NativeMatterPhase Phase { get; }
        public double MolarMassKgPerMol { get; }
        public double SpecificHeatCvJoulesPerKgKelvin { get; }
        public double LiquidDensityKgPerCubicMetre { get; }
        public double EnergyOffsetJoulesPerKg { get; }
        public double MinimumTemperatureKelvin { get; }
        public double MaximumTemperatureKelvin { get; }

        public FluidMaterial(string id, NativeMatterPhase phase, double molarMassKgPerMol,
            double specificHeatCvJoulesPerKgKelvin, double liquidDensityKgPerCubicMetre = 0,
            double energyOffsetJoulesPerKg = 0, double minTemperatureKelvin = 0,
            double maxTemperatureKelvin = double.MaxValue)
        {
            FluidChecks.Identity(id, nameof(id));
            if (phase != NativeMatterPhase.Gas && phase != NativeMatterPhase.Liquid)
                throw new ArgumentOutOfRangeException(nameof(phase));
            FluidChecks.Positive(molarMassKgPerMol, nameof(molarMassKgPerMol));
            FluidChecks.Positive(specificHeatCvJoulesPerKgKelvin, nameof(specificHeatCvJoulesPerKgKelvin));
            FluidChecks.Nonnegative(liquidDensityKgPerCubicMetre, nameof(liquidDensityKgPerCubicMetre));
            if (phase == NativeMatterPhase.Liquid && liquidDensityKgPerCubicMetre == 0)
                throw new ArgumentOutOfRangeException(nameof(liquidDensityKgPerCubicMetre));
            if (phase == NativeMatterPhase.Gas && liquidDensityKgPerCubicMetre != 0)
                throw new ArgumentException("Gas materials cannot declare incompressible liquid volume.");
            FluidChecks.Finite(energyOffsetJoulesPerKg, nameof(energyOffsetJoulesPerKg));
            FluidChecks.Nonnegative(minTemperatureKelvin, nameof(minTemperatureKelvin));
            FluidChecks.Positive(maxTemperatureKelvin, nameof(maxTemperatureKelvin));
            if (maxTemperatureKelvin <= minTemperatureKelvin) throw new ArgumentException("Invalid material temperature range.");
            Id = id; Phase = phase; MolarMassKgPerMol = molarMassKgPerMol;
            SpecificHeatCvJoulesPerKgKelvin = specificHeatCvJoulesPerKgKelvin;
            LiquidDensityKgPerCubicMetre = liquidDensityKgPerCubicMetre;
            EnergyOffsetJoulesPerKg = energyOffsetJoulesPerKg;
            MinimumTemperatureKelvin = minTemperatureKelvin; MaximumTemperatureKelvin = maxTemperatureKelvin;
        }
    }

    public sealed class FluidMaterialCatalog
    {
        public const double GasConstantJoulesPerMolKelvin = 8.31446261815324;
        public string Version { get; }
        public string Fingerprint { get; }
        public IReadOnlyDictionary<string, FluidMaterial> Materials { get; }
        internal byte[] FingerprintBytes { get; }

        public FluidMaterialCatalog(string version, IEnumerable<FluidMaterial> materials)
        {
            FluidChecks.Identity(version, nameof(version));
            if (materials == null) throw new ArgumentNullException(nameof(materials));
            var definitions = new SortedDictionary<string, FluidMaterial>(StringComparer.Ordinal);
            foreach (var material in materials)
            {
                if (material == null || definitions.ContainsKey(material.Id)) throw new ArgumentException("Null or duplicate fluid material.");
                definitions.Add(material.Id, material);
                if (definitions.Count > 65536) throw new ArgumentException("Too many fluid materials.");
            }
            if (definitions.Count == 0) throw new ArgumentException("A material catalog cannot be empty.");
            Version = version; Materials = new ReadOnlyDictionary<string, FluidMaterial>(definitions);
            using var stream = new MemoryStream();
            using (var writer = new BinaryWriter(stream, Encoding.UTF8, true))
            {
                writer.Write(version); writer.Write(definitions.Count);
                foreach (var material in definitions.Values)
                {
                    writer.Write(material.Id); writer.Write((int)material.Phase);
                    writer.Write(material.MolarMassKgPerMol); writer.Write(material.SpecificHeatCvJoulesPerKgKelvin);
                    writer.Write(material.LiquidDensityKgPerCubicMetre); writer.Write(material.EnergyOffsetJoulesPerKg);
                    writer.Write(material.MinimumTemperatureKelvin); writer.Write(material.MaximumTemperatureKelvin);
                }
            }
            using var sha = SHA256.Create(); FingerprintBytes = sha.ComputeHash(stream.ToArray());
            Fingerprint = BitConverter.ToString(FingerprintBytes).Replace("-", "").ToLowerInvariant();
        }

        public FluidInventory CreateInventory(IReadOnlyDictionary<string, double> massesKg, double temperatureKelvin,
            double velocityX = 0, double velocityY = 0, IReadOnlyDictionary<int, double>? diseaseCounts = null,
            double radiationQuantity = 0)
        {
            if (massesKg == null) throw new ArgumentNullException(nameof(massesKg));
            FluidChecks.Positive(temperatureKelvin, nameof(temperatureKelvin));
            FluidChecks.Finite(velocityX, nameof(velocityX)); FluidChecks.Finite(velocityY, nameof(velocityY));
            var energy = 0.0; var mass = 0.0;
            foreach (var entry in massesKg)
            {
                FluidChecks.Nonnegative(entry.Value, nameof(massesKg));
                var material = Get(entry.Key);
                if (entry.Value != 0 && (temperatureKelvin < material.MinimumTemperatureKelvin || temperatureKelvin > material.MaximumTemperatureKelvin))
                    throw new InvalidOperationException("Temperature outside the declared model for " + entry.Key);
                energy += entry.Value * (material.EnergyOffsetJoulesPerKg + material.SpecificHeatCvJoulesPerKgKelvin * temperatureKelvin);
                mass += entry.Value;
            }
            energy += 0.5 * mass * (velocityX * velocityX + velocityY * velocityY);
            var result = new FluidInventory(massesKg, energy, mass * velocityX, mass * velocityY, diseaseCounts, radiationQuantity);
            Describe(result, double.MaxValue);
            return result;
        }

        internal FluidMaterial Get(string id)
        {
            if (!Materials.TryGetValue(id, out var material)) throw new InvalidOperationException("Unsupported fluid material: " + id);
            return material;
        }

        internal FluidProperties Describe(FluidInventory inventory, double volume)
        {
            var cv = 0.0; var offset = 0.0; var moles = 0.0; var liquidVolume = 0.0;
            string? dominant = null; var dominantMass = -1.0;
            foreach (var entry in inventory.MassEntries)
            {
                var material = Get(entry.Key);
                cv += entry.Value * material.SpecificHeatCvJoulesPerKgKelvin;
                offset += entry.Value * material.EnergyOffsetJoulesPerKg;
                if (material.Phase == NativeMatterPhase.Gas) moles += entry.Value / material.MolarMassKgPerMol;
                else liquidVolume += entry.Value / material.LiquidDensityKgPerCubicMetre;
                // Ordinal dictionaries make equal-mass dominance deterministic.
                if (entry.Value > dominantMass) { dominantMass = entry.Value; dominant = entry.Key; }
            }
            if (inventory.TotalMassKg == 0) return default;
            var temperature = (inventory.InternalEnergyJoules - offset) / cv;
            FluidChecks.Positive(temperature, "reconstructed temperature");
            foreach (var entry in inventory.MassEntries)
            {
                var material = Get(entry.Key);
                if (temperature < material.MinimumTemperatureKelvin || temperature > material.MaximumTemperatureKelvin)
                    throw new InvalidOperationException("Temperature outside the declared model for " + entry.Key);
            }
            FluidChecks.Nonnegative(liquidVolume, "liquid volume");
            if (liquidVolume > volume || (moles > 0 && liquidVolume >= volume))
                throw new InvalidOperationException("Fluid inventory exceeds cell volume or leaves no gas headspace.");
            var pressure = moles == 0 ? 0 : moles * GasConstantJoulesPerMolKelvin * temperature / (volume - liquidVolume);
            FluidChecks.Nonnegative(pressure, "gas pressure");
            return new FluidProperties(temperature, pressure, liquidVolume / volume, dominant, Get(dominant!).Phase);
        }
    }

    internal readonly struct FluidProperties
    {
        public double Temperature { get; }
        public double GasPressure { get; }
        public double LiquidFraction { get; }
        public string? Dominant { get; }
        public NativeMatterPhase Phase { get; }
        public FluidProperties(double temperature, double pressure, double liquidFraction, string? dominant, NativeMatterPhase phase)
        { Temperature = temperature; GasPressure = pressure; LiquidFraction = liquidFraction; Dominant = dominant; Phase = phase; }
    }

    internal static class FluidChecks
    {
        internal const int MaximumDiseaseKinds = 65536;
        private static readonly Encoding StrictUtf8 = new UTF8Encoding(false, true);
        internal static void Finite(double value, string name)
        { if (double.IsNaN(value) || double.IsInfinity(value)) throw new ArgumentOutOfRangeException(name, "A finite value is required."); }
        internal static void Nonnegative(double value, string name)
        { Finite(value, name); if (value < 0) throw new ArgumentOutOfRangeException(name, "A nonnegative value is required."); }
        internal static void Positive(double value, string name)
        { Finite(value, name); if (value <= 0) throw new ArgumentOutOfRangeException(name, "A positive value is required."); }
        internal static void Identity(string value, string name)
        {
            if (string.IsNullOrWhiteSpace(value) || value.Length > 256 || value.Any(char.IsControl) || StrictUtf8.GetByteCount(value) > 1024)
                throw new ArgumentException("A bounded, stable identity is required.", name);
        }
    }
}
