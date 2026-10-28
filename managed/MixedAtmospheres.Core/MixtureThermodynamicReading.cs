using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using MixedAtmospheres.Core.Interop;

namespace MixedAtmospheres.Core
{
    /// <summary>One present constituent of an ideal-gas/elastic-liquid thermodynamic reading.</summary>
    public readonly struct MixtureThermodynamicConstituent
    {
        public string MaterialId { get; }
        public NativeMatterPhase Phase { get; }
        public double MassKg { get; }
        public double MolarMassKgPerMol { get; }
        public double Moles { get; }
        /// <summary>Gas thermodynamic partial volume, or actual compressed liquid volume; not spatial gas separation.</summary>
        public double PartialVolumeCubicMetres { get; }

        public MixtureThermodynamicConstituent(string materialId, NativeMatterPhase phase, double massKg,
            double molarMassKgPerMol, double partialVolumeCubicMetres)
        {
            FluidChecks.Identity(materialId, nameof(materialId));
            if (phase != NativeMatterPhase.Gas && phase != NativeMatterPhase.Liquid)
                throw new ArgumentOutOfRangeException(nameof(phase), "Only gas and liquid constituents are supported.");
            FluidChecks.Positive(massKg, nameof(massKg));
            FluidChecks.Positive(molarMassKgPerMol, nameof(molarMassKgPerMol));
            FluidChecks.Positive(partialVolumeCubicMetres, nameof(partialVolumeCubicMetres));
            var moles = massKg / molarMassKgPerMol;
            FluidChecks.Positive(moles, "representable constituent moles");
            MaterialId = materialId; Phase = phase; MassKg = massKg;
            MolarMassKgPerMol = molarMassKgPerMol; Moles = moles; PartialVolumeCubicMetres = partialVolumeCubicMetres;
        }
    }

    /// <summary>
    /// Immutable supplied thermodynamic facts for common-temperature ideal gases and elastic liquids.
    /// This is a presentation input, not a simulation owner, EOS solver, completed frame or restart format.
    /// </summary>
    public sealed class MixtureThermodynamicReading
    {
        public const double MaximumVolumeClosureRelativeTolerance = 2e-10;
        private const double ArithmeticTolerance = 64 * 2.2204460492503131e-16;
        private const int MaximumConstituents = 65536;
        public IReadOnlyList<MixtureThermodynamicConstituent> Constituents { get; }
        public string SourceId { get; }
        public long Revision { get; }
        public string ThermodynamicProfileVersion { get; }
        public string ThermodynamicProfileFingerprint { get; }
        public double TotalMassKg { get; }
        public double MassResidualKg { get; }
        public double StoredInternalEnergyJoules { get; }
        public double ResolvedKineticEnergyJoules { get; }
        public double MomentumX { get; }
        public double MomentumY { get; }
        public double BulkKineticEnergyJoules { get; }
        public double MechanicalVarianceJoules => ResolvedKineticEnergyJoules - BulkKineticEnergyJoules;
        public double KineticArithmeticBoundJoules { get; }
        public double TemperatureKelvin { get; }
        public double GasPressurePa { get; }
        public double MechanicalPressurePa { get; }
        public double VolumeCubicMetres { get; }
        public double GasVolumeCubicMetres { get; }
        public double LiquidVolumeCubicMetres { get; }
        public double VacuumVolumeCubicMetres { get; }
        public double GasMassKg { get; }
        public double GasMoles { get; }
        public double VolumeClosureRelativeTolerance { get; }
        public double VolumeClosureResidualCubicMetres { get; }
        public double GasPartialVolumeResidualCubicMetres { get; }
        public double LiquidPartialVolumeResidualCubicMetres { get; }
        public double IdealGasVolumeResidualCubicMetres { get; }

        public MixtureThermodynamicReading(IEnumerable<MixtureThermodynamicConstituent> constituents,
            double totalMassKg, double storedInternalEnergyJoules, double resolvedKineticEnergyJoules,
            double temperatureKelvin, double gasPressurePa, double mechanicalPressurePa,
            double volumeCubicMetres, double gasVolumeCubicMetres, double liquidVolumeCubicMetres,
            double vacuumVolumeCubicMetres, string thermodynamicProfileVersion, string thermodynamicProfileFingerprint,
            long revision = 0, string sourceId = "thermodynamic-reading", double momentumX = 0, double momentumY = 0,
            double volumeClosureRelativeTolerance = MaximumVolumeClosureRelativeTolerance, bool resolvedVacuumInterface = false)
        {
            if (constituents == null) throw new ArgumentNullException(nameof(constituents));
            FluidChecks.Identity(sourceId, nameof(sourceId));
            FluidChecks.Identity(thermodynamicProfileVersion, nameof(thermodynamicProfileVersion));
            FluidChecks.Identity(thermodynamicProfileFingerprint, nameof(thermodynamicProfileFingerprint));
            if (revision < 0) throw new ArgumentOutOfRangeException(nameof(revision));
            FluidChecks.Nonnegative(totalMassKg, nameof(totalMassKg));
            FluidChecks.Finite(storedInternalEnergyJoules, nameof(storedInternalEnergyJoules));
            FluidChecks.Nonnegative(resolvedKineticEnergyJoules, nameof(resolvedKineticEnergyJoules));
            FluidChecks.Finite(momentumX, nameof(momentumX)); FluidChecks.Finite(momentumY, nameof(momentumY));
            FluidChecks.Nonnegative(temperatureKelvin, nameof(temperatureKelvin));
            FluidChecks.Nonnegative(gasPressurePa, nameof(gasPressurePa));
            FluidChecks.Nonnegative(mechanicalPressurePa, nameof(mechanicalPressurePa));
            FluidChecks.Positive(volumeCubicMetres, nameof(volumeCubicMetres));
            FluidChecks.Nonnegative(gasVolumeCubicMetres, nameof(gasVolumeCubicMetres));
            FluidChecks.Nonnegative(liquidVolumeCubicMetres, nameof(liquidVolumeCubicMetres));
            FluidChecks.Nonnegative(vacuumVolumeCubicMetres, nameof(vacuumVolumeCubicMetres));
            FluidChecks.Nonnegative(volumeClosureRelativeTolerance, nameof(volumeClosureRelativeTolerance));
            if (volumeClosureRelativeTolerance > MaximumVolumeClosureRelativeTolerance)
                throw new ArgumentOutOfRangeException(nameof(volumeClosureRelativeTolerance));

            // Constituents are kept sorted by ordinal identity in a plain array (no
            // per-reading dictionary): a presentation builds one per cell it shows.
            var capacity = constituents is ICollection<MixtureThermodynamicConstituent> sized ? Math.Min(sized.Count, MaximumConstituents + 1) : 4;
            var values = new MixtureThermodynamicConstituent[capacity];
            var valueCount = 0; HashSet<string>? seen = null;
            var mass = new Sum(); var gasMass = new Sum(); var gasMoles = new Sum();
            var gasPartials = new Sum(); var liquidPartials = new Sum();
            foreach (var entry in constituents)
            {
                // A default value-type entry has never passed the constituent constructor.
                FluidChecks.Identity(entry.MaterialId, nameof(constituents));
                FluidChecks.Positive(entry.MassKg, nameof(constituents));
                FluidChecks.Positive(entry.MolarMassKgPerMol, nameof(constituents));
                FluidChecks.Positive(entry.Moles, nameof(constituents));
                FluidChecks.Positive(entry.PartialVolumeCubicMetres, nameof(constituents));
                if (entry.Phase != NativeMatterPhase.Gas && entry.Phase != NativeMatterPhase.Liquid)
                    throw new ArgumentException("Unsupported constituent phase.", nameof(constituents));
                if (valueCount >= 24 && seen == null) { seen = new HashSet<string>(StringComparer.Ordinal); for (var i = 0; i < valueCount; ++i) seen.Add(values[i].MaterialId); }
                if (seen != null) { if (!seen.Add(entry.MaterialId)) throw new ArgumentException("Duplicate constituent identity.", nameof(constituents)); }
                else for (var i = 0; i < valueCount; ++i)
                    if (string.Equals(values[i].MaterialId, entry.MaterialId, StringComparison.Ordinal)) throw new ArgumentException("Duplicate constituent identity.", nameof(constituents));
                if (valueCount == values.Length) Array.Resize(ref values, Math.Min(MaximumConstituents + 1, Math.Max(4, valueCount * 2)));
                values[valueCount++] = entry;
                if (valueCount > MaximumConstituents) throw new ArgumentException("Too many constituents.", nameof(constituents));
                mass.Add(entry.MassKg);
                if (entry.Phase == NativeMatterPhase.Gas)
                { gasMass.Add(entry.MassKg); gasMoles.Add(entry.Moles); gasPartials.Add(entry.PartialVolumeCubicMetres); }
                else liquidPartials.Add(entry.PartialVolumeCubicMetres);
            }
            if (valueCount > 16) Array.Sort(values, 0, valueCount, ConstituentOrder.Instance);
            else
                for (var i = 1; i < valueCount; ++i)
                {
                    var moving = values[i]; var j = i - 1;
                    for (; j >= 0 && string.CompareOrdinal(values[j].MaterialId, moving.MaterialId) > 0; --j) values[j + 1] = values[j];
                    values[j + 1] = moving;
                }
            var representedMass = mass.Value;
            MassResidualKg = Difference(representedMass, totalMassKg);
            Within(MassResidualKg, Math.Max(representedMass, totalMassKg), ArithmeticTolerance, "Constituent mass does not match total mass.");
            if (valueCount != 0 && totalMassKg == 0) throw new ArgumentException("Positive constituents require positive total mass.");
            if (valueCount == 0 && (totalMassKg != 0 || storedInternalEnergyJoules != 0 || resolvedKineticEnergyJoules != 0 || momentumX != 0 || momentumY != 0 || temperatureKelvin != 0 || mechanicalPressurePa != 0))
                throw new ArgumentException("Vacuum cannot store fluid mass, energy, momentum, temperature or pressure.");
            if (valueCount != 0) FluidChecks.Positive(temperatureKelvin, nameof(temperatureKelvin));
            GasMassKg = gasMass.Value; GasMoles = gasMoles.Value;
            if (GasMoles == 0)
            {
                if (gasVolumeCubicMetres != 0 || gasPressurePa != 0) throw new ArgumentException("No gas constituent supports the declared gas volume/pressure.");
                if (vacuumVolumeCubicMetres > 0 && mechanicalPressurePa != 0)
                    throw new ArgumentException("Gasless liquid with a vacuum cannot support positive equilibrium pressure.");
            }
            else
            {
                FluidChecks.Positive(gasVolumeCubicMetres, nameof(gasVolumeCubicMetres));
                FluidChecks.Positive(gasPressurePa, nameof(gasPressurePa));
                if ((vacuumVolumeCubicMetres != 0 && !resolvedVacuumInterface) || gasPressurePa != mechanicalPressurePa)
                    throw new ArgumentException("A gas/vacuum interface requires an explicit resolved-interface reading; gas and material pressure must agree.");
                // This is a scalar consistency check, not an EOS inversion. Keep
                // the supplied state and its signed discrepancy at declared EOS precision.
                var idealVolume = ProductRatio(GasMoles, FluidMaterialCatalog.GasConstantJoulesPerMolKelvin, temperatureKelvin, gasPressurePa);
                FluidChecks.Positive(idealVolume, "representable ideal-gas volume");
                IdealGasVolumeResidualCubicMetres = Difference(idealVolume, gasVolumeCubicMetres);
                var gasTolerance = Math.Max(ArithmeticTolerance, volumeClosureRelativeTolerance);
                Within(IdealGasVolumeResidualCubicMetres, Math.Max(idealVolume, gasVolumeCubicMetres), gasTolerance, "Gas pressure, temperature, moles and headspace contradict the declared ideal-gas profile.");
                for (var index = 0; index < valueCount; ++index)
                {
                    var entry = values[index];
                    if (entry.Phase == NativeMatterPhase.Gas)
                    {
                        var share = ProductRatio(gasVolumeCubicMetres, entry.Moles, 1, GasMoles);
                        FluidChecks.Positive(share, "representable ideal-gas partial volume");
                        Within(Difference(entry.PartialVolumeCubicMetres, share), Math.Max(entry.PartialVolumeCubicMetres, share), gasTolerance, "Gas partial volume contradicts its canonical mole share.");
                    }
                }
            }
            if (liquidPartials.Value == 0 && liquidVolumeCubicMetres != 0)
                throw new ArgumentException("No liquid constituent supports the declared liquid volume.");
            GasPartialVolumeResidualCubicMetres = Difference(gasPartials.Value, gasVolumeCubicMetres);
            LiquidPartialVolumeResidualCubicMetres = Difference(liquidPartials.Value, liquidVolumeCubicMetres);
            Within(GasPartialVolumeResidualCubicMetres, Math.Max(gasPartials.Value, gasVolumeCubicMetres), volumeClosureRelativeTolerance, "Gas partial volumes do not match actual headspace.");
            Within(LiquidPartialVolumeResidualCubicMetres, Math.Max(liquidPartials.Value, liquidVolumeCubicMetres), volumeClosureRelativeTolerance, "Liquid partial volumes do not match actual liquid volume.");
            var volumes = new Sum(); volumes.Add(gasVolumeCubicMetres); volumes.Add(liquidVolumeCubicMetres); volumes.Add(vacuumVolumeCubicMetres); volumes.Add(-volumeCubicMetres);
            VolumeClosureResidualCubicMetres = volumes.Value;
            Within(VolumeClosureResidualCubicMetres, volumeCubicMetres, volumeClosureRelativeTolerance, "Actual phase volumes do not close the reading volume.");
            var bulkTerms = new Sum();
            if (momentumX != 0) AddBulk(ref bulkTerms, momentumX, totalMassKg);
            if (momentumY != 0) AddBulk(ref bulkTerms, momentumY, totalMassKg);
            var bulk = bulkTerms.Value;
            FluidChecks.Nonnegative(bulk, "bulk kinetic energy"); BulkKineticEnergyJoules = bulk;
            KineticArithmeticBoundJoules = ArithmeticTolerance * Math.Max(bulk, resolvedKineticEnergyJoules);
            if (bulk > resolvedKineticEnergyJoules && bulk - resolvedKineticEnergyJoules > KineticArithmeticBoundJoules)
                throw new ArgumentException("Resolved kinetic energy is below bulk kinetic energy.");

            if (values.Length != valueCount) Array.Resize(ref values, valueCount);
            Constituents = new ReadOnlyCollection<MixtureThermodynamicConstituent>(values);
            SourceId = sourceId; Revision = revision; ThermodynamicProfileVersion = thermodynamicProfileVersion;
            ThermodynamicProfileFingerprint = thermodynamicProfileFingerprint; TotalMassKg = totalMassKg;
            StoredInternalEnergyJoules = storedInternalEnergyJoules; ResolvedKineticEnergyJoules = resolvedKineticEnergyJoules;
            MomentumX = momentumX; MomentumY = momentumY; TemperatureKelvin = temperatureKelvin;
            GasPressurePa = gasPressurePa; MechanicalPressurePa = mechanicalPressurePa; VolumeCubicMetres = volumeCubicMetres;
            GasVolumeCubicMetres = gasVolumeCubicMetres; LiquidVolumeCubicMetres = liquidVolumeCubicMetres;
            VacuumVolumeCubicMetres = vacuumVolumeCubicMetres; VolumeClosureRelativeTolerance = volumeClosureRelativeTolerance;
        }

        internal double GasPartialPressure(MixtureThermodynamicConstituent constituent)
        {
            if (constituent.Phase != NativeMatterPhase.Gas) throw new ArgumentException("A gas constituent is required.", nameof(constituent));
            var pressure = ProductRatio(GasPressurePa, constituent.Moles, 1, GasMoles);
            FluidChecks.Nonnegative(pressure, "gas partial pressure");
            return pressure;
        }

        private static double Difference(double a, double b) { var sum = new Sum(); sum.Add(a); sum.Add(-b); return sum.Value; }
        private static void Within(double residual, double scale, double relative, string message)
        { if (Math.Abs(residual) > relative * scale) throw new ArgumentException(message); }
        private static void AddBulk(ref Sum terms, double momentum, double mass)
        {
            var term = ProductRatio(Math.Abs(momentum), Math.Abs(momentum), .5, mass);
            FluidChecks.Positive(term, "representable bulk kinetic energy"); terms.Add(term);
        }
        // A positive fraction may underflow before multiplication by a large
        // headspace restores a representable partial volume. Normalize each
        // finite factor first; this never normalizes the supplied state.
        internal static double ProductRatio(double a, double b, double c, double divisor)
        {
            var ma = Mantissa(a, out var ea); var mb = Mantissa(b, out var eb);
            var mc = Mantissa(c, out var ec); var md = Mantissa(divisor, out var ed);
            var value = (ma / md) * mb * mc; var exponent = ea + eb + ec - ed;
            // The normalized product is in (1/2,8), so at most three shifts.
            while (value >= 2) { value *= .5; ++exponent; }
            if (value < 1) { value *= 2; --exponent; }
            if (exponent > 1023) return double.PositiveInfinity;
            if (exponent < -1075) return 0;
            if (exponent >= -1022) return value * PowerOfTwo(exponent);
            // Form a normal significand first, then round once into subnormals.
            return (value * PowerOfTwo(-1022)) * PowerOfTwo(exponent + 1022);
        }
        private static double Mantissa(double value, out int exponent)
        {
            FluidChecks.Positive(value, "positive reading product factor");
            var bits = BitConverter.DoubleToInt64Bits(value); var shift = 0;
            if ((bits & 0x7ff0000000000000L) == 0)
            { value *= 4503599627370496; bits = BitConverter.DoubleToInt64Bits(value); shift = 52; }
            exponent = (int)((bits >> 52) & 2047) - 1023 - shift;
            return BitConverter.Int64BitsToDouble((bits & 0x000fffffffffffffL) | 0x3ff0000000000000L);
        }
        private static double PowerOfTwo(int exponent) => BitConverter.Int64BitsToDouble((long)(exponent + 1023) << 52);
        private sealed class ConstituentOrder : IComparer<MixtureThermodynamicConstituent>
        {
            internal static readonly ConstituentOrder Instance = new ConstituentOrder();
            public int Compare(MixtureThermodynamicConstituent a, MixtureThermodynamicConstituent b) => string.CompareOrdinal(a.MaterialId, b.MaterialId);
        }
        private struct Sum
        {
            private double high, low;
            internal void Add(double value)
            {
                var next = high + value;
                FluidChecks.Finite(next, "reading sum");
                low += Math.Abs(high) >= Math.Abs(value) ? (high - next) + value : (value - next) + high;
                high = next; FluidChecks.Finite(low, "reading sum residual");
            }
            internal double Value { get { var value = high + low; FluidChecks.Finite(value, "reading sum"); return value; } }
        }
    }
}
