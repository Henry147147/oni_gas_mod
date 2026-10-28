using System;
namespace MixedAtmospheres.Core
{
    internal static class MixtureScalarColor
    {
        internal static MixtureSrgbColor Blend(ReadOnlySpan<MixtureConstituentValue> values, double x, double y, bool textured,
            bool phaseVolumes, double liquidR, double liquidG, double liquidB, double worldGasWeight, double patternContrast)
        {
            var r = phaseVolumes ? liquidR : 0;
            var g = phaseVolumes ? liquidG : 0;
            var b = phaseVolumes ? liquidB : 0;
            var gasWeight = phaseVolumes ? worldGasWeight : 1;
            foreach (var species in values)
            {
                var style = species.Definition.Pattern;
                if (style == null)
                    continue;
                var modulation = textured ? 1 + patternContrast * style.Signal(x, y) : 1;
                r += gasWeight * species.GasFraction * style.R * modulation;
                g += gasWeight * species.GasFraction * style.G * modulation;
                b += gasWeight * species.GasFraction * style.B * modulation;
            }
            return MixtureSrgbColor.Linear(r, g, b);
        }
    }
    internal readonly struct MixtureTextureScalars
    {
        internal readonly MixtureSrgbColor Color;
        internal readonly double GasMassKg, GasVolumeCubicMetres, GasDensityKgPerCubicMetre, LiquidVolumeCubicMetres,
            VolumeCubicMetres, TotalPressurePa, OxygenMoleFraction, Co2MoleFraction, OxygenPartialPressurePa, Co2PartialPressurePa;
        internal readonly BreathingFailure Failures;
        internal readonly bool HasPhaseVolumeAppearance;
        internal bool IsBreathable => Failures == BreathingFailure.None;
        internal MixtureTextureScalars(MixtureSrgbColor color, MixtureReadingAnalysis reading, double density,
            double oxygen, double co2, double oxygenPressure, double co2Pressure, BreathingFailure failures, bool hasPhase)
        {
            Color = color;
            GasMassKg = reading.GasMassKg;
            GasVolumeCubicMetres = reading.GasVolumeCubicMetres;
            GasDensityKgPerCubicMetre = density;
            LiquidVolumeCubicMetres = reading.LiquidVolumeCubicMetres;
            VolumeCubicMetres = reading.VolumeCubicMetres;
            TotalPressurePa = reading.GasPressurePa;
            OxygenMoleFraction = oxygen;
            Co2MoleFraction = co2;
            OxygenPartialPressurePa = oxygenPressure;
            Co2PartialPressurePa = co2Pressure;
            Failures = failures;
            HasPhaseVolumeAppearance = hasPhase;
        }
    }
    public sealed partial class MixturePresentationModel
    {
        internal MixtureTextureScalars SampleTexture(MixtureReadingAnalysis reading, ReadOnlySpan<MixtureThermodynamicConstituent> constituents, MixtureConstituentValue[] scratch, double worldX, double worldY)
        {
            var values = scratch.AsSpan(0, constituents.Length);
            var oxygen = 0.0;
            var co2 = 0.0;
            var hasContaminant = false;
            var hasCo2 = false;
            var oxygenPressure = 0.0;
            var co2Pressure = 0.0;
            var index = 0;
            foreach (var entry in constituents)
            {
                if (!resolvedMaterials.TryGetValue(entry.MaterialId, out var resolved))
                    throw new InvalidOperationException("Unsupported fluid material: " + entry.MaterialId);
                if (resolved.Material.Phase != entry.Phase)
                    throw new InvalidOperationException("Thermodynamic constituent phase differs from its presentation definition: " + entry.MaterialId);
                var gas = entry.Phase == Interop.NativeMatterPhase.Gas;
                var fraction = gas ? entry.Moles / reading.GasMoles : 0;
                double? partialPressure = gas ? reading.GasPartialPressure(entry) : (double?)null;
                values[index++] = new MixtureConstituentValue(resolved, entry.MassKg, entry.Moles, fraction, entry.PartialVolumeCubicMetres, partialPressure);
                if (!gas)
                    continue;
                if (resolved.ContaminantLimit.HasValue && ExceedsLimit(resolved.ContaminantLimit.Value, fraction, true))
                    hasContaminant = true;
                if (resolved.IsOxygen)
                {
                    oxygen = fraction;
                    oxygenPressure = partialPressure!.Value;
                }
                if (resolved.IsCo2)
                {
                    co2 = fraction;
                    co2Pressure = partialPressure!.Value;
                    hasCo2 = true;
                }
            }
            var failures = Assess(reading.GasMoles, oxygen, co2, hasContaminant, hasCo2, oxygenPressure, co2Pressure);
            var density = reading.GasMassKg == 0 ? 0 : reading.GasMassKg / reading.GasVolumeCubicMetres;
            FluidChecks.Nonnegative(density, nameof(MixturePresentation.GasDensityKgPerCubicMetre));
            var hasPhase = reading.GasVolumeCubicMetres > 0 && reading.LiquidVolumeCubicMetres > 0;
            double liquidR = 0, liquidG = 0, liquidB = 0, gasWeight = 1;
            if (hasPhase)
            {
                gasWeight = reading.GasVolumeCubicMetres / reading.VolumeCubicMetres;
                foreach (var constituent in values)
                {
                    if (constituent.Definition.Material.Phase != Interop.NativeMatterPhase.Liquid)
                        continue;
                    var weight = constituent.ThermodynamicPartialVolume!.Value / reading.VolumeCubicMetres;
                    liquidR += weight * constituent.Definition.LinearR;
                    liquidG += weight * constituent.Definition.LinearG;
                    liquidB += weight * constituent.Definition.LinearB;
                }
            }
            // The original data constructor always computes BaseColor, including
            // validation, before any coordinate-dependent texture request.
            var color = MixtureScalarColor.Blend(values, 0, 0, false, false, liquidR, liquidG, liquidB, gasWeight, options.PatternContrast);
            if (reading.GasMassKg > 0 && reading.GasVolumeCubicMetres > 0)
            {
                FluidChecks.Finite(worldX, nameof(worldX));
                FluidChecks.Finite(worldY, nameof(worldY));
                if (hasPhase || options.PatternEnabled && options.PatternContrast != 0)
                    color = MixtureScalarColor.Blend(values, worldX % 4, worldY % 4, options.PatternEnabled && options.PatternContrast != 0, hasPhase,
                    liquidR, liquidG, liquidB, gasWeight, options.PatternContrast);
            }
            return new MixtureTextureScalars(color, reading, density, oxygen, co2, oxygenPressure, co2Pressure, failures, hasPhase);
        }
    }
}
