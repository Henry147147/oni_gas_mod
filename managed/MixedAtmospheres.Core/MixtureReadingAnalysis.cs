// Scalar analysis derived from the original rich constructor.
// The rich constructor stays unchanged and is the independent runtime oracle.
// Same-process scalar and full-property byte tests must fail on any drift.
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using MixedAtmospheres.Core.Interop;
using static MixedAtmospheres.Core.MixtureThermodynamicReading;
namespace MixedAtmospheres.Core
{
    internal struct MixtureReadingAnalysis
    {
        internal IReadOnlyList<MixtureThermodynamicConstituent> Constituents;
        internal string SourceId;
        internal long Revision;
        internal string ThermodynamicProfileVersion;
        internal string ThermodynamicProfileFingerprint;
        internal double TotalMassKg;
        internal double MassResidualKg;
        internal double StoredInternalEnergyJoules;
        internal double ResolvedKineticEnergyJoules;
        internal double MomentumX;
        internal double MomentumY;
        internal double BulkKineticEnergyJoules;
        internal double KineticArithmeticBoundJoules;
        internal double TemperatureKelvin;
        internal double GasPressurePa;
        internal double MechanicalPressurePa;
        internal double VolumeCubicMetres;
        internal double GasVolumeCubicMetres;
        internal double LiquidVolumeCubicMetres;
        internal double VacuumVolumeCubicMetres;
        internal double GasMassKg;
        internal double GasMoles;
        internal double VolumeClosureRelativeTolerance;
        internal double VolumeClosureResidualCubicMetres;
        internal double GasPartialVolumeResidualCubicMetres;
        internal double LiquidPartialVolumeResidualCubicMetres;
        internal double IdealGasVolumeResidualCubicMetres;
        internal MixtureReadingAnalysis(IEnumerable<MixtureThermodynamicConstituent> constituents,
            double totalMassKg, double storedInternalEnergyJoules, double resolvedKineticEnergyJoules,
            double temperatureKelvin, double gasPressurePa, double mechanicalPressurePa,
            double volumeCubicMetres, double gasVolumeCubicMetres, double liquidVolumeCubicMetres,
            double vacuumVolumeCubicMetres, string thermodynamicProfileVersion, string thermodynamicProfileFingerprint,
            long revision = 0, string sourceId = "thermodynamic-reading", double momentumX = 0, double momentumY = 0,
            double volumeClosureRelativeTolerance = MaximumVolumeClosureRelativeTolerance, bool resolvedVacuumInterface = false, MixtureThermodynamicConstituent[]? scratch = null, bool retain = true)
        {
            IdealGasVolumeResidualCubicMetres = 0;
            if (constituents == null)
                throw new ArgumentNullException(nameof(constituents));
            FluidChecks.Identity(sourceId, nameof(sourceId));
            FluidChecks.Identity(thermodynamicProfileVersion, nameof(thermodynamicProfileVersion));
            FluidChecks.Identity(thermodynamicProfileFingerprint, nameof(thermodynamicProfileFingerprint));
            if (revision < 0)
                throw new ArgumentOutOfRangeException(nameof(revision));
            FluidChecks.Nonnegative(totalMassKg, nameof(totalMassKg));
            FluidChecks.Finite(storedInternalEnergyJoules, nameof(storedInternalEnergyJoules));
            FluidChecks.Nonnegative(resolvedKineticEnergyJoules, nameof(resolvedKineticEnergyJoules));
            FluidChecks.Finite(momentumX, nameof(momentumX));
            FluidChecks.Finite(momentumY, nameof(momentumY));
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
            var values = scratch ?? new MixtureThermodynamicConstituent[capacity];
            var valueCount = 0;
            HashSet<string>? seen = null;
            var mass = new Sum();
            var gasMass = new Sum();
            var gasMoles = new Sum();
            var gasPartials = new Sum();
            var liquidPartials = new Sum();
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
                if (scratch == null && valueCount >= 24 && seen == null)
                {
                    seen = new HashSet<string>(StringComparer.Ordinal);
                    for (var i = 0; i < valueCount; ++i)
                        seen.Add(values[i].MaterialId);
                }
                if (seen != null)
                {
                    if (!seen.Add(entry.MaterialId))
                        throw new ArgumentException("Duplicate constituent identity.", nameof(constituents));
                }
                else
                    for (var i = 0; i < valueCount; ++i)
                        if (string.Equals(values[i].MaterialId, entry.MaterialId, StringComparison.Ordinal))
                            throw new ArgumentException("Duplicate constituent identity.", nameof(constituents));
                if (valueCount == values.Length)
                    Array.Resize(ref values, Math.Min(MaximumConstituents + 1, Math.Max(4, valueCount * 2)));
                values[valueCount++] = entry;
                if (valueCount > MaximumConstituents)
                    throw new ArgumentException("Too many constituents.", nameof(constituents));
                mass.Add(entry.MassKg);
                if (entry.Phase == NativeMatterPhase.Gas)
                {
                    gasMass.Add(entry.MassKg);
                    gasMoles.Add(entry.Moles);
                    gasPartials.Add(entry.PartialVolumeCubicMetres);
                }
                else
                    liquidPartials.Add(entry.PartialVolumeCubicMetres);
            }
            if (valueCount > 16)
                Array.Sort(values, 0, valueCount, ConstituentOrder.Instance);
            else
                for (var i = 1; i < valueCount; ++i)
                {
                    var moving = values[i];
                    var j = i - 1;
                    for (; j >= 0 && string.CompareOrdinal(values[j].MaterialId, moving.MaterialId) > 0; --j)
                        values[j + 1] = values[j];
                    values[j + 1] = moving;
                }
            var representedMass = mass.Value;
            MassResidualKg = Difference(representedMass, totalMassKg);
            Within(MassResidualKg, Math.Max(representedMass, totalMassKg), ArithmeticTolerance, "Constituent mass does not match total mass.");
            if (valueCount != 0 && totalMassKg == 0)
                throw new ArgumentException("Positive constituents require positive total mass.");
            if (valueCount == 0 && (totalMassKg != 0 || storedInternalEnergyJoules != 0 || resolvedKineticEnergyJoules != 0 || momentumX != 0 || momentumY != 0 || temperatureKelvin != 0 || mechanicalPressurePa != 0))
                throw new ArgumentException("Vacuum cannot store fluid mass, energy, momentum, temperature or pressure.");
            if (valueCount != 0)
                FluidChecks.Positive(temperatureKelvin, nameof(temperatureKelvin));
            GasMassKg = gasMass.Value;
            GasMoles = gasMoles.Value;
            if (GasMoles == 0)
            {
                if (gasVolumeCubicMetres != 0 || gasPressurePa != 0)
                    throw new ArgumentException("No gas constituent supports the declared gas volume/pressure.");
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
            var volumes = new Sum();
            volumes.Add(gasVolumeCubicMetres);
            volumes.Add(liquidVolumeCubicMetres);
            volumes.Add(vacuumVolumeCubicMetres);
            volumes.Add(-volumeCubicMetres);
            VolumeClosureResidualCubicMetres = volumes.Value;
            Within(VolumeClosureResidualCubicMetres, volumeCubicMetres, volumeClosureRelativeTolerance, "Actual phase volumes do not close the reading volume.");
            var bulkTerms = new Sum();
            if (momentumX != 0)
                AddBulk(ref bulkTerms, momentumX, totalMassKg);
            if (momentumY != 0)
                AddBulk(ref bulkTerms, momentumY, totalMassKg);
            var bulk = bulkTerms.Value;
            FluidChecks.Nonnegative(bulk, "bulk kinetic energy");
            BulkKineticEnergyJoules = bulk;
            KineticArithmeticBoundJoules = ArithmeticTolerance * Math.Max(bulk, resolvedKineticEnergyJoules);
            if (bulk > resolvedKineticEnergyJoules && bulk - resolvedKineticEnergyJoules > KineticArithmeticBoundJoules)
                throw new ArgumentException("Resolved kinetic energy is below bulk kinetic energy.");

            if (retain && values.Length != valueCount)
                Array.Resize(ref values, valueCount);
            Constituents = retain ? new ReadOnlyCollection<MixtureThermodynamicConstituent>(values) : null!;
            SourceId = sourceId;
            Revision = revision;
            ThermodynamicProfileVersion = thermodynamicProfileVersion;
            ThermodynamicProfileFingerprint = thermodynamicProfileFingerprint;
            TotalMassKg = totalMassKg;
            StoredInternalEnergyJoules = storedInternalEnergyJoules;
            ResolvedKineticEnergyJoules = resolvedKineticEnergyJoules;
            MomentumX = momentumX;
            MomentumY = momentumY;
            TemperatureKelvin = temperatureKelvin;
            GasPressurePa = gasPressurePa;
            MechanicalPressurePa = mechanicalPressurePa;
            VolumeCubicMetres = volumeCubicMetres;
            GasVolumeCubicMetres = gasVolumeCubicMetres;
            LiquidVolumeCubicMetres = liquidVolumeCubicMetres;
            VacuumVolumeCubicMetres = vacuumVolumeCubicMetres;
            VolumeClosureRelativeTolerance = volumeClosureRelativeTolerance;
        }
        internal double GasPartialPressure(MixtureThermodynamicConstituent constituent)
        {
            if (constituent.Phase != NativeMatterPhase.Gas)
                throw new ArgumentException("A gas constituent is required.", nameof(constituent));
            var pressure = ProductRatio(GasPressurePa, constituent.Moles, 1, GasMoles);
            FluidChecks.Nonnegative(pressure, "gas partial pressure");
            return pressure;
        }

    }
}
