using System;
using System.Collections.Generic;
using System.Linq;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

internal static class ThermodynamicReadingTests
{
    private const string Profile = "fixed-ideal-gas-elastic-liquid-reading-v1";
    private const string Fingerprint = "actual-eos-profile-identity";

    internal static void Run(Action<string, Action> test, Action<bool> check, Action<Action> reject,
        Func<MixturePresentationModel> model, FluidMaterialCatalog catalog)
    {
        test("actual EOS readings retain stored U and supplied thermodynamics when resolved kinetic energy changes", () =>
        {
            var source = Reading(kinetic: 64); var other = Reading(kinetic: 64000);
            var first = model().Describe(source); var second = model().Describe(other);
            check(source.StoredInternalEnergyJoules == -200 && other.StoredInternalEnergyJoules == -200);
            check(first.TemperatureKelvin == 425 && second.TemperatureKelvin == 425);
            check(first.GasPressurePa == source.GasPressurePa && second.GasPressurePa == source.GasPressurePa);
            check(first.LiquidVolumeFraction == .2 && second.LiquidVolumeFraction == .2);
            check(first.BaseColor.Equals(second.BaseColor) && first.Failures == second.Failures);
            check(ReferenceEquals(first.ThermodynamicReading, source) && ReferenceEquals(second.ThermodynamicReading, other));
        });
        test("canonical molar masses set fractions independently of the legacy presentation catalog", () =>
        {
            var reading = Reading(); var view = model().Describe(reading);
            var nitrogen = view.Constituents.Single(row => row.MaterialId == "nitrogen");
            var expected = (.75 / .028) / (.75 / .028 + .25 / .032);
            check(catalog.Materials["nitrogen"].MolarMassKgPerMol == .0280134);
            check(nitrogen.Moles == .75 / .028 && nitrogen.GasMoleFraction == expected);
            var substituted = (.75 / .0280134) / (.75 / .0280134 + .25 / catalog.Materials["oxygen"].MolarMassKgPerMol);
            check(Math.Abs(nitrogen.GasMoleFraction!.Value - substituted) > 1e-6);
            check(Math.Abs(nitrogen.PartialPressurePa!.Value - expected * reading.GasPressurePa) < 1e-14 * reading.GasPressurePa);
        });
        test("actual compressed liquid partial volume and gas headspace bypass nominal liquid density", () =>
        {
            var reading = Reading(); var view = model().Describe(reading);
            check(view.TotalMassKg == 251 && view.GasMassKg == 1 && view.GasVolumeCubicMetres == .8);
            check(view.GasDensityKgPerCubicMetre == 1.25 && view.LiquidVolumeFraction == .2);
            var water = view.Constituents.Single(row => row.MaterialId == "water");
            check(water.MassKg / catalog.Materials["water"].LiquidDensityKgPerCubicMetre == .25);
            check(water.ThermodynamicPartialVolumeCubicMetres == .2 && !water.PartialPressurePa.HasValue);
            foreach (var gas in view.GasConstituents)
                check(gas.ThermodynamicPartialVolumeCubicMetres == .8 * gas.GasMoleFraction);
        });
        test("pure compressed liquid exposes mechanical pressure while gas pressure and oxygen partial pressure remain zero", () =>
        {
            var reading = Reading(new[] { new MixtureThermodynamicConstituent("water", NativeMatterPhase.Liquid, 1100, .018, 1) },
                gasVolume: 0, liquidVolume: 1, mechanicalPressure: 2e8);
            var view = model().Describe(reading);
            check(view.MechanicalPressurePa == 2e8 && view.GasPressurePa == 0 && view.TotalPressurePa == 0);
            check(view.OxygenPartialPressurePa == 0 && view.Failures == BreathingFailure.Vacuum && !view.IsBreathable);
            check(view.LiquidVolumeFraction == 1 && view.GasDensityKgPerCubicMetre == 0);
            check(view.BreathingStatus == "No gas phase / not breathable" && view.DisplayName == "Water (liquid)\n" + view.BreathingStatus);
        });
        test("world mixture colour uses actual compressed phase volumes in linear light while gas colour stays mole weighted", () =>
        {
            var definitions = model();
            var styles = definitions.Styles.Values.Select(s => new MixtureSpeciesStyle(s.MaterialId, s.DisplayName,
                s.MaterialId == "water" ? new MixtureSrgbColor(0, 0, 1) : new MixtureSrgbColor(1, 0, 0)));
            var plain = new MixturePresentationModel(catalog, styles, definitions.BreathingSettings,
                "oxygen", "carbon_dioxide", options: new MixtureVisualOptions { PatternEnabled = false });
            var source = Reading(); var view = plain.Describe(source); var colour = view.WorldFluidColorAt(2.5, -1.25);
            check(view.HasPhaseVolumeAppearance && Math.Abs(view.BaseColor.R - 1) < 1e-14 && view.BaseColor.B == 0);
            check(Math.Abs(colour.R - Encode(.8)) < 1e-14 && colour.G == 0 && Math.Abs(colour.B - Encode(.2)) < 1e-14);
            check(Math.Abs(colour.B - Encode(.25)) > .04); // Nominal water density gives the wrong volume.
            check(view.Constituents.Single(row => row.MaterialId == "water").GasMoleFraction == null);
            check(ReferenceEquals(view.ThermodynamicReading, source) && source.StoredInternalEnergyJoules == -200);
            reject(() => view.WorldFluidColorAt(double.NaN, 0));
        });
        test("each liquid contributes its own actual partial volume and configured colour", () =>
        {
            var definitions = model();
            var expanded = new FluidMaterialCatalog("two-liquid-reading", catalog.Materials.Values.Append(
                new FluidMaterial("oil", NativeMatterPhase.Liquid, .15, 1800, 800)));
            var styles = definitions.Styles.Values.Select(s => new MixtureSpeciesStyle(s.MaterialId, s.DisplayName,
                s.MaterialId == "water" ? new MixtureSrgbColor(0, 0, 1) : new MixtureSrgbColor(1, 0, 0)))
                .Append(new MixtureSpeciesStyle("oil", "Oil", new MixtureSrgbColor(0, 1, 0)));
            var twoLiquids = new MixturePresentationModel(expanded, styles, definitions.BreathingSettings,
                "oxygen", "carbon_dioxide", options: new MixtureVisualOptions { PatternEnabled = false });
            var source = Reading(new[]
            {
                new MixtureThermodynamicConstituent("oxygen", NativeMatterPhase.Gas, 1, .032, .5),
                new MixtureThermodynamicConstituent("water", NativeMatterPhase.Liquid, 250, .018, .2),
                new MixtureThermodynamicConstituent("oil", NativeMatterPhase.Liquid, 270, .15, .3)
            }, gasVolume: .5, liquidVolume: .5);
            var view = twoLiquids.Describe(source); var colour = view.WorldFluidColorAt(0, 0);
            check(view.HasPhaseVolumeAppearance && Math.Abs(colour.R - Encode(.5)) < 1e-14);
            check(Math.Abs(colour.G - Encode(.3)) < 1e-14 && Math.Abs(colour.B - Encode(.2)) < 1e-14);
        });
        test("wet world patterns retain world anchoring with gas amplitude reduced by actual gas volume", () =>
        {
            var definitions = model(); var view = definitions.Describe(Reading());
            var water = definitions.Styles["water"].Color;
            foreach (var point in new[] { (0.125, -.25), (1.75, 2.5), (-2.125, -3.5) })
            {
                var gas = view.ColorAt(point.Item1, point.Item2); var actual = view.WorldFluidColorAt(point.Item1, point.Item2);
                check(Math.Abs(actual.R - Encode(.8 * Decode(gas.R) + .2 * Decode(water.R))) < 2e-14);
                check(Math.Abs(actual.G - Encode(.8 * Decode(gas.G) + .2 * Decode(water.G))) < 2e-14);
                check(Math.Abs(actual.B - Encode(.8 * Decode(gas.B) + .2 * Decode(water.B))) < 2e-14);
                var periodic = view.WorldFluidColorAt(point.Item1 + 4, point.Item2 + 4);
                check(Math.Abs(actual.R - periodic.R) < 1e-14 && Math.Abs(actual.B - periodic.B) < 1e-14);
            }
        });
        test("dry canonical and legacy wet appearances retain their existing gas colour contract", () =>
        {
            var definitions = model();
            var dry = definitions.Describe(Reading(new[]
                { new MixtureThermodynamicConstituent("oxygen", NativeMatterPhase.Gas, 1, .032, 1) }, gasVolume: 1, liquidVolume: 0));
            var legacy = definitions.Describe(catalog.CreateInventory(new Dictionary<string, double>
                { ["oxygen"] = 1, ["water"] = 250 }, 300), 1);
            foreach (var view in new[] { dry, legacy })
            {
                check(!view.HasPhaseVolumeAppearance);
                check(view.WorldFluidColorAt(1.25, 2.5).Equals(view.ColorAt(1.25, 2.5)));
            }
        });
        test("explicit vacuum reading retains zero fluid quantities and a known empty volume", () =>
        {
            var reading = Reading(Array.Empty<MixtureThermodynamicConstituent>(), internalEnergy: 0, kinetic: 0,
                temperature: 0, gasVolume: 0, liquidVolume: 0, vacuumVolume: 1);
            var view = model().Describe(reading);
            check(view.TotalMassKg == 0 && view.GasConstituents.Count == 0 && view.GasDensityKgPerCubicMetre == 0);
            check(view.Failures == BreathingFailure.Vacuum && view.MechanicalPressurePa == 0);
            check(view.BreathingStatus == "Vacuum / no atmosphere" && view.DisplayName == view.BreathingStatus);
        });
        test("positive canonical traces remain visible as rows and violate an explicit zero contaminant limit", () =>
        {
            var source = MixedRows(true); var reading = Reading(source);
            var definitions = model();
            var strict = new MixturePresentationModel(catalog, definitions.Styles.Values, definitions.BreathingSettings,
                "oxygen", "carbon_dioxide", new Dictionary<string, double> { ["chlorine"] = 0 });
            var view = strict.Describe(reading); var trace = view.Constituents.Single(row => row.MaterialId == "chlorine");
            check(trace.MassKg == Math.Pow(2, -80) && trace.Moles > 0 && trace.GasMoleFraction > 0);
            check(trace.IsTrace && trace.IsContaminant && trace.MolePercentText != "0%" && trace.PartialPressurePa > 0);
            check((view.Failures & BreathingFailure.Contaminant) != 0 && view.Warnings.Any(text => text.Contains("Chlorine")));
            check(trace.ThermodynamicPartialVolumeCubicMetres > 0);
        });
        test("caller collection retirement cannot change retained readings or lazily constructed rows", () =>
        {
            var caller = MixedRows(); var reading = Reading(caller); var view = model().Describe(reading);
            caller.Clear(); caller.Add(new MixtureThermodynamicConstituent("nitrogen", NativeMatterPhase.Gas, 1, .028, 1));
            check(reading.Constituents.Count == 3 && view.Constituents.Count == 3 && view.TotalMassKg == 251);
            reject(() => ((IList<MixtureThermodynamicConstituent>)reading.Constituents).Clear());
            check(view.Constituents.Any(row => row.MaterialId == "water") && reading.StoredInternalEnergyJoules == -200);
        });
        test("representable positive trace partial volumes survive an underflowed intermediate mole fraction", () =>
        {
            var definitions = model();
            var strict = new MixturePresentationModel(catalog, definitions.Styles.Values, definitions.BreathingSettings,
                "oxygen", "carbon_dioxide", new Dictionary<string, double> { ["chlorine"] = 0 });
            foreach (var exponent in new[] { -900, -1074 })
            {
                var large = Math.Pow(2, 500); var small = Math.Pow(2, exponent);
                var rows = new[]
                {
                    new MixtureThermodynamicConstituent("nitrogen", NativeMatterPhase.Gas, large, 1, large),
                    new MixtureThermodynamicConstituent("chlorine", NativeMatterPhase.Gas, small, 1, small),
                    new MixtureThermodynamicConstituent("oxygen", NativeMatterPhase.Gas, small, 1, small)
                };
                var pressure = FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300;
                var reading = new MixtureThermodynamicReading(rows, large, 0, 0, 300, pressure, pressure,
                    large, large, 0, 0, Profile, Fingerprint);
                var view = strict.Describe(reading); var trace = view.Constituents.Single(row => row.MaterialId == "chlorine");
                check(trace.MassKg == small && trace.Moles == small && trace.ThermodynamicPartialVolumeCubicMetres == small);
                check(trace.GasMoleFraction == 0 && trace.MolePercentText == "below numeric range");
                check(trace.PartialPressureText.StartsWith("below numeric range") && trace.IsContaminant);
                check((view.Failures & BreathingFailure.Contaminant) != 0 && (view.Failures & BreathingFailure.Vacuum) == 0);
                check(view.Warnings.Any(text => text.StartsWith("Oxygen fraction is too low: below numeric range")));
                check(view.Warnings.Any(text => text.StartsWith("Oxygen partial pressure is too low: below numeric range Pa")));
            }
        });
        test("representable trace partial pressures drive rows assessments and warnings when mole fractions underflow", () =>
        {
            var majorMoles = Math.Pow(2, 989); var traceMoles = Math.Pow(2, -400);
            var volume = Math.Pow(2, 500); var traceVolume = Math.Pow(2, -889);
            var pressure = Math.Pow(2, 489) * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300;
            // Independent ideal-gas species formula, with no total-mole fraction.
            var expected = traceMoles * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300 / volume;
            var rows = new[]
            {
                new MixtureThermodynamicConstituent("nitrogen", NativeMatterPhase.Gas, majorMoles, 1, volume),
                new MixtureThermodynamicConstituent("oxygen", NativeMatterPhase.Gas, traceMoles, 1, traceVolume),
                new MixtureThermodynamicConstituent("carbon_dioxide", NativeMatterPhase.Gas, traceMoles, 1, traceVolume)
            };
            var reading = new MixtureThermodynamicReading(rows, majorMoles, 0, 0, 300, pressure, pressure,
                volume, volume, 0, 0, Profile, Fingerprint);
            var definitions = model(); var settings = definitions.BreathingSettings;
            settings.MinimumOxygenPartialPressurePa = expected / 2;
            settings.MaximumCo2PartialPressurePa = expected / 2;
            var view = new MixturePresentationModel(catalog, definitions.Styles.Values, settings,
                "oxygen", "carbon_dioxide").Describe(reading);
            check(view.OxygenMoleFraction == 0 && view.Co2MoleFraction == 0);
            check(Math.Abs(view.OxygenPartialPressurePa - expected) < expected * 1e-14);
            check(view.Co2PartialPressurePa == view.OxygenPartialPressurePa);
            check((view.Failures & BreathingFailure.OxygenPartialPressureLow) == 0);
            check((view.Failures & BreathingFailure.CarbonDioxidePartialPressureHigh) != 0);
            var co2 = view.Constituents.Single(row => row.MaterialId == "carbon_dioxide");
            check(co2.PartialPressurePa == view.Co2PartialPressurePa && co2.PartialPressureText != "below numeric range Pa");
            var number = view.Co2PartialPressurePa.ToString("G8", System.Globalization.CultureInfo.InvariantCulture);
            check(view.Warnings.Any(text => text.StartsWith("Carbon dioxide partial pressure") && text.Contains(number + " Pa")));
        });
        test("reading identity revision and actual EOS provenance survive warm views and visual variants", () =>
        {
            var first = Reading(); var newer = Reading(temperature: 450, revision: 18, sourceId: "canonical-owner:next");
            var definitions = model();
            for (var i = 0; i < 3; ++i)
            {
                var view = definitions.Describe(first); _ = view.Constituents; _ = view.Warnings; _ = view.ColorAt(2, 3);
                check(view.SourceId == first.SourceId && view.Revision == 17 && ReferenceEquals(view.ThermodynamicReading, first));
                check(view.ThermodynamicReading!.ThermodynamicProfileVersion == Profile && view.ThermodynamicReading.ThermodynamicProfileFingerprint == Fingerprint);
                check(view.MaterialModelFingerprint == catalog.Fingerprint && view.MaterialModelVersion == catalog.Version);
            }
            var variant = definitions.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false, PatternContrast = .02 });
            var styled = variant.Describe(first); var changed = definitions.Describe(newer);
            check(!styled.PatternEnabled && ReferenceEquals(styled.ThermodynamicReading, first) && styled.TemperatureKelvin == 425);
            check(changed.Revision == 18 && changed.SourceId == "canonical-owner:next" && changed.TemperatureKelvin == 450);
            var sameStamp = Reading(temperature: 475); check(definitions.Describe(sameStamp).TemperatureKelvin == 475);
            check(definitions.Describe(first).TemperatureKelvin == 425);
        });
        test("actual EOS volume closure admits declared root precision and retains signed residuals", () =>
        {
            var reading = Reading(gasVolume: .8 + 1e-12, volumeTolerance: 2e-12);
            check(reading.VolumeClosureResidualCubicMetres > 0 && reading.GasPartialVolumeResidualCubicMetres < 0);
            check(reading.VolumeClosureRelativeTolerance == 2e-12);
            check(model().Describe(reading).GasVolumeCubicMetres == .8 + 1e-12);
            reject(() => Reading(gasVolume: .8 + 1e-6));
            reject(() => Reading(volumeTolerance: 1e-9));
            reject(() => Reading(gasVolume: .8 + 1e-12, volumeTolerance: 0));
        });
        test("contradictory masses phase volumes gas scalar facts and unsupported constituent identities reject", () =>
        {
            reject(() => Reading(totalMass: 250)); reject(() => Reading(gasPressure: 1));
            reject(() => Reading(gasVolume: 0)); reject(() => Reading(liquidVolume: 0));
            reject(() => Reading(vacuumVolume: .1)); reject(() => Reading(temperature: double.NaN));
            reject(() => Reading(new[] { default(MixtureThermodynamicConstituent) }));
            reject(() => Reading(MixedRows().Concat(MixedRows())));
            reject(() => new MixtureThermodynamicConstituent("oxygen", NativeMatterPhase.Solid, 1, .032, 1));
            reject(() => new MixtureThermodynamicConstituent("oxygen", NativeMatterPhase.Gas, -1, .032, 1));
            var wrongShares = MixedRows();
            wrongShares[0] = new MixtureThermodynamicConstituent("oxygen", NativeMatterPhase.Gas, .25, .032, .4);
            wrongShares[1] = new MixtureThermodynamicConstituent("nitrogen", NativeMatterPhase.Gas, .75, .028, .4);
            reject(() => Reading(wrongShares));
            var liquidWithVoid = new[] { new MixtureThermodynamicConstituent("water", NativeMatterPhase.Liquid, 500, .018, .5) };
            reject(() => Reading(liquidWithVoid, gasVolume: 0, liquidVolume: .5, vacuumVolume: .5, mechanicalPressure: 1));
            var unknown = Reading(new[] { new MixtureThermodynamicConstituent("unknown", NativeMatterPhase.Gas, 1, .1, 1) }, gasVolume: 1, liquidVolume: 0);
            reject(() => model().Describe(unknown));
            var wrongPhase = Reading(new[] { new MixtureThermodynamicConstituent("oxygen", NativeMatterPhase.Liquid, 1, .032, 1) }, gasVolume: 0, liquidVolume: 1);
            reject(() => model().Describe(wrongPhase));
        });
        test("resolved kinetic realizability uses a local arithmetic bound and never repairs stored energy", () =>
        {
            reject(() => Reading(kinetic: 1, momentumX: 251));
            reject(() => Reading(kinetic: double.NaN)); reject(() => Reading(kinetic: -1));
            var reading = Reading(kinetic: Math.BitDecrement(125.5), momentumX: 251);
            check(reading.BulkKineticEnergyJoules == 125.5 && reading.MechanicalVarianceJoules < 0);
            check(-reading.MechanicalVarianceJoules <= reading.KineticArithmeticBoundJoules && reading.StoredInternalEnergyJoules == -200);
            check(model().Describe(reading).TemperatureKelvin == 425);
        });
        test("legacy inventory presentation keeps its caloric contract and has no claimed actual EOS reading", () =>
        {
            var inventory = catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = 1 }, 300, 2, -1);
            var view = model().Describe(inventory, 1, 19, "legacy");
            check(view.ThermodynamicReading == null && view.MechanicalPressurePa == null);
            check(view.Revision == 19 && view.SourceId == "legacy" && view.TemperatureKelvin == 300);
            check(view.GasPressurePa == (1 / catalog.Materials["oxygen"].MolarMassKgPerMol) * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300);
            check(view.Constituents.Single().ThermodynamicPartialVolumeCubicMetres == null);
        });
    }

    private static double Decode(double s) => s <= .04045 ? s / 12.92 : Math.Pow((s + .055) / 1.055, 2.4);
    private static double Encode(double l) => l <= .0031308 ? 12.92 * l : 1.055 * Math.Pow(l, 1 / 2.4) - .055;

    private static List<MixtureThermodynamicConstituent> MixedRows(bool trace = false)
    {
        var masses = new[] { .25, .75, Math.Pow(2, -80) }; var molar = new[] { .032, .028, .071 };
        var moles = masses[0] / molar[0] + masses[1] / molar[1] + (trace ? masses[2] / molar[2] : 0);
        var result = new List<MixtureThermodynamicConstituent>
        {
            new("oxygen", NativeMatterPhase.Gas, masses[0], molar[0], .8 * (masses[0] / molar[0] / moles)),
            new("nitrogen", NativeMatterPhase.Gas, masses[1], molar[1], .8 * (masses[1] / molar[1] / moles)),
            new("water", NativeMatterPhase.Liquid, 250, .018, .2)
        };
        if (trace) result.Add(new MixtureThermodynamicConstituent("chlorine", NativeMatterPhase.Gas, masses[2], molar[2], .8 * (masses[2] / molar[2] / moles)));
        return result;
    }

    private static MixtureThermodynamicReading Reading(IEnumerable<MixtureThermodynamicConstituent>? rows = null,
        double internalEnergy = -200, double kinetic = 64, double temperature = 425, double gasVolume = .8,
        double liquidVolume = .2, double vacuumVolume = 0, double? gasPressure = null, double? mechanicalPressure = null,
        double? totalMass = null, long revision = 17, string sourceId = "canonical-owner:4", double momentumX = 0,
        double volumeTolerance = MixtureThermodynamicReading.MaximumVolumeClosureRelativeTolerance)
    {
        var values = (rows ?? MixedRows()).ToArray();
        var gasMoles = values.Where(row => row.Phase == NativeMatterPhase.Gas).Sum(row => row.Moles);
        var pressure = gasPressure ?? (gasMoles == 0 ? 0 : (gasMoles * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * temperature / gasVolume));
        return new MixtureThermodynamicReading(values, totalMass ?? values.Sum(row => row.MassKg), internalEnergy, kinetic,
            temperature, pressure, mechanicalPressure ?? pressure, 1, gasVolume, liquidVolume, vacuumVolume,
            Profile, Fingerprint, revision, sourceId, momentumX, 0, volumeTolerance);
    }
}
