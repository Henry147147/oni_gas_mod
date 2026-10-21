using System;
using System.Collections.Generic;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Presentation (docs/colony-owner.md#presentation): each completed owner
    // frame becomes the source of the mixture gas renderer, hover, overlays and
    // status display, one reading per open owned cell holding material. Species
    // use the game's element ids, names and substance colours.
    internal static class ColonyPresentation
    {
        private const string Version = "oni-colony-presentation-v1";
        private static FluidMaterialCatalog? catalog;
        private static string[] ids = Array.Empty<string>();
        private static NativeMatterPhase[] phases = Array.Empty<NativeMatterPhase>();
        private static double[] molarMasses = Array.Empty<double>();
        private static ulong publishedFrame = ulong.MaxValue;
        private static OniNativeColony.Reading[] readings = Array.Empty<OniNativeColony.Reading>();
        private static OniNativeColony.Constituent[] parts = Array.Empty<OniNativeColony.Constituent>();
        internal static long Publications { get; private set; }
        internal static int RejectedCells { get; private set; }
        internal static string FirstRejection { get; private set; } = "";
        internal static double LastBuildMilliseconds { get; private set; }
        internal static string LastError { get; private set; } = "";

        internal static void Reset() { catalog = null; publishedFrame = ulong.MaxValue; Publications = 0; LastError = ""; }

        internal static void Configure()
        {
            OniNativeColony.Check(OniNativeColony.SpeciesList(null, 0, out var count));
            var species = new OniNativeColony.Species[count];
            OniNativeColony.Check(OniNativeColony.SpeciesList(species, count, out count));
            var materials = new List<FluidMaterial>(); var styles = new List<MixtureSpeciesStyle>();
            ids = new string[count]; phases = new NativeMatterPhase[count]; molarMasses = new double[count];
            for (var k = 0; k < count; ++k)
            {
                var s = species[k]; var element = ElementLoader.elements[(int)s.NativeIndex];
                ids[k] = element.id.ToString(); phases[k] = s.Phase == 1 ? NativeMatterPhase.Gas : NativeMatterPhase.Liquid;
                molarMasses[k] = s.MolarMassKgPerMol;
                materials.Add(new FluidMaterial(ids[k], phases[k], s.MolarMassKgPerMol, s.CvJoulesPerKgKelvin,
                    phases[k] == NativeMatterPhase.Liquid ? s.DensityKgPerCubicMetre : 0, s.EnergyOffsetJoulesPerKg, 1, 1e9));
                Color colour = element.substance.colour;
                var name = global::STRINGS.UI.StripLinkFormatting(element.name);
                styles.Add(new MixtureSpeciesStyle(ids[k], string.IsNullOrWhiteSpace(name) ? ids[k] : name, new MixtureSrgbColor(colour.r, colour.g, colour.b)));
            }
            catalog = new FluidMaterialCatalog(Version, materials);
            // Easy-mode breathing (docs/colony-owner.md): no upper oxygen bound.
            var breathing = new AtmosphereSettings { HardMode = false, MaximumOxygenFraction = 1 };
            MixturePresentationRuntime.ResetProviders();
            MixturePresentationRuntime.Configure(new MixturePresentationModel(catalog, styles, breathing,
                SimHashes.Oxygen.ToString(), SimHashes.CarbonDioxide.ToString()));
            publishedFrame = ulong.MaxValue;
        }

        // Called after each data request; builds readings once per completed frame.
        internal static void Publish(ulong completedFrame)
        {
            if (catalog == null || completedFrame == publishedFrame) return;
            var started = System.Diagnostics.Stopwatch.GetTimestamp();
            try
            {
                OniNativeColony.Check(OniNativeColony.Readings(out var frame, null, 0, out var count, null, 0, out var partCount));
                if (readings.Length < count) readings = new OniNativeColony.Reading[count + count / 4];
                if (parts.Length < partCount) parts = new OniNativeColony.Constituent[partCount + partCount / 4];
                OniNativeColony.Check(OniNativeColony.Readings(out frame, readings, (uint)readings.Length, out count, parts, (uint)parts.Length, out partCount));
                var cells = new Dictionary<int, MixtureThermodynamicReading>((int)count);
                var rejected = 0;
                for (var r = 0; r < count; ++r)
                {
                    try { cells.Add((int)readings[r].PublicCell, Create(readings[r], (long)frame)); }
                    catch (ArgumentException error)
                    {
                        // That cell keeps its vanilla projection.
                        if (rejected++ == 0) FirstRejection = "cell " + readings[r].PublicCell + ": " + error.Message;
                    }
                }
                RejectedCells = rejected;
                if (cells.Count != 0) MixturePresentationRuntime.PublishNativeDomainFrame(Grid.WidthInCells, Grid.HeightInCells, (long)frame, cells);
                publishedFrame = frame; ++Publications;
            }
            catch (Exception error)
            {
                // Presentation never stops the owner; the vanilla projection stays visible.
                if (LastError != error.Message) Debug.LogWarning("[Mixed Atmospheres] Colony presentation failed: " + error.Message);
                LastError = error.Message; publishedFrame = completedFrame;
            }
            LastBuildMilliseconds = (System.Diagnostics.Stopwatch.GetTimestamp() - started) * 1000.0 / System.Diagnostics.Stopwatch.Frequency;
        }

        // Gas partial volumes follow the reading's ideal-gas contract (n R T / p),
        // scaled to the cell's headspace when that geometry agrees with it.
        private static MixtureThermodynamicReading Create(OniNativeColony.Reading cell, long revision)
        {
            var gasMoles = 0.0;
            for (var c = 0; c < cell.Constituents; ++c)
            {
                var part = parts[cell.FirstConstituent + c];
                if (phases[part.Species] == NativeMatterPhase.Gas) gasMoles += part.MassKg / molarMasses[part.Species];
            }
            var headspace = cell.OccupiedVolumeCubicMetres - cell.LiquidVolumeCubicMetres;
            var idealVolume = gasMoles > 0 && cell.GasPressurePa > 0 ? gasMoles * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * cell.TemperatureKelvin / cell.GasPressurePa : 0;
            var geometric = gasMoles > 0 && headspace > 0 && Math.Abs(headspace - idealVolume) <=
                MixtureThermodynamicReading.MaximumVolumeClosureRelativeTolerance * Math.Max(headspace, idealVolume);
            var constituents = new List<MixtureThermodynamicConstituent>((int)cell.Constituents); var gasVolume = 0.0;
            for (var c = 0; c < cell.Constituents; ++c)
            {
                var part = parts[cell.FirstConstituent + c]; var k = (int)part.Species;
                double volume;
                if (phases[k] == NativeMatterPhase.Liquid) volume = part.PartialVolumeCubicMetres;
                else
                {
                    var moles = part.MassKg / molarMasses[k];
                    volume = geometric ? headspace * (moles / gasMoles) : idealVolume * (moles / gasMoles);
                    gasVolume += volume;
                }
                constituents.Add(new MixtureThermodynamicConstituent(ids[k], phases[k], part.MassKg, molarMasses[k], volume));
            }
            if (geometric) gasVolume = headspace;
            var total = 0.0; foreach (var c in constituents) total += c.MassKg;
            var vacuum = Math.Max(0, 1 - cell.OccupiedVolumeCubicMetres);
            // A liquid surface under vacuum carries no pressure in the reading model.
            var mechanical = gasVolume == 0 && vacuum > 0 ? 0 : cell.MechanicalPressurePa;
            return new MixtureThermodynamicReading(constituents, total, cell.InternalEnergyJoules, 0,
                cell.TemperatureKelvin, gasVolume > 0 ? cell.GasPressurePa : 0, mechanical, 1, gasVolume, cell.LiquidVolumeCubicMetres,
                vacuum, catalog!.Version, catalog.Fingerprint, revision, "colony:cell:" + cell.PublicCell,
                resolvedVacuumInterface: true);
        }
    }
}
