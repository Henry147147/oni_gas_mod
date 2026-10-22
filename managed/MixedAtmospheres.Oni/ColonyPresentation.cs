using System;
using System.Collections.Generic;
using System.Threading;
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
        private static long publications;
        internal static long Publications => Interlocked.Read(ref publications);
        internal static int RejectedCells { get; private set; }
        internal static string FirstRejection { get; private set; } = "";
        internal static double LastBuildMilliseconds { get; private set; } // worker
        internal static double CopyMilliseconds { get; private set; }      // game thread
        internal static string LastError { get; private set; } = "";

        internal static void Reset() { catalog = null; publishedFrame = ulong.MaxValue; Interlocked.Exchange(ref publications, 0); LastError = ""; }

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

        // Called after each data request. The game thread copies the completed
        // frame's readings (made on the solver thread); a worker builds the
        // presentation from them, one frame at a time: a frame completed while
        // a build runs waits for the next request, so the presentation may skip
        // frames but never holds the game thread.
        private static int building;
        internal static void Publish(ulong completedFrame)
        {
            if (catalog == null || completedFrame == publishedFrame || Interlocked.CompareExchange(ref building, 1, 0) != 0) return;
            var started = System.Diagnostics.Stopwatch.GetTimestamp();
            ulong frame; uint count, partCount;
            try
            {
                OniNativeColony.Check(OniNativeColony.Readings(out frame, null, 0, out count, null, 0, out partCount));
                if (readings.Length < count) readings = new OniNativeColony.Reading[count + count / 4];
                if (parts.Length < partCount) parts = new OniNativeColony.Constituent[partCount + partCount / 4];
                OniNativeColony.Check(OniNativeColony.Readings(out frame, readings, (uint)readings.Length, out count, parts, (uint)parts.Length, out partCount));
            }
            catch (Exception error) { Failed(error, completedFrame); Volatile.Write(ref building, 0); return; }
            publishedFrame = frame;
            CopyMilliseconds = Milliseconds(started);
            int width = Grid.WidthInCells, height = Grid.HeightInCells;
            // Configure replaces these arrays rather than editing them, so the
            // worker keeps a consistent set even if the owner restarts.
            var table = new Table(catalog!, ids, phases, molarMasses, readings, parts);
            ThreadPool.QueueUserWorkItem(_ =>
            {
                var begun = System.Diagnostics.Stopwatch.GetTimestamp();
                try
                {
                    var cells = new Dictionary<int, MixtureThermodynamicReading>((int)count);
                    var rejected = 0;
                    for (var r = 0; r < count; ++r)
                    {
                        try { cells.Add((int)table.Readings[r].PublicCell, Create(table, table.Readings[r], (long)frame)); }
                        catch (ArgumentException error)
                        {
                            // That cell keeps its vanilla projection.
                            if (rejected++ == 0) FirstRejection = "cell " + table.Readings[r].PublicCell + ": " + error.Message;
                        }
                    }
                    RejectedCells = rejected;
                    if (cells.Count != 0 && MixturePresentationRuntime.PublishNativeDomainFrame(width, height, (long)frame, cells)) Interlocked.Increment(ref publications);
                }
                catch (Exception error)
                {
                    // A frame still building when the owner stops or the game
                    // shuts the presentation down is dropped, not a failure.
                    if (catalog == table.Catalog && MixturePresentationRuntime.CurrentModel != null) Failed(error, frame);
                }
                finally { LastBuildMilliseconds = Milliseconds(begun); Volatile.Write(ref building, 0); }
            });
        }
        // Presentation never stops the owner; the vanilla projection stays visible.
        private static void Failed(Exception error, ulong frame)
        {
            if (LastError != error.Message) Debug.LogWarning("[Mixed Atmospheres] Colony presentation failed: " + error.Message);
            LastError = error.Message; publishedFrame = frame;
        }
        private static double Milliseconds(long since) =>
            (System.Diagnostics.Stopwatch.GetTimestamp() - since) * 1000.0 / System.Diagnostics.Stopwatch.Frequency;

        // Gas partial volumes follow the reading's ideal-gas contract (n R T / p),
        // scaled to the cell's headspace when that geometry agrees with it.
        private sealed class Table
        {
            internal readonly FluidMaterialCatalog Catalog; internal readonly string[] Ids; internal readonly NativeMatterPhase[] Phases;
            internal readonly double[] MolarMasses; internal readonly OniNativeColony.Reading[] Readings; internal readonly OniNativeColony.Constituent[] Parts;
            internal Table(FluidMaterialCatalog catalog, string[] ids, NativeMatterPhase[] phases, double[] molarMasses,
                OniNativeColony.Reading[] readings, OniNativeColony.Constituent[] parts)
            { Catalog = catalog; Ids = ids; Phases = phases; MolarMasses = molarMasses; Readings = readings; Parts = parts; }
        }
        private static MixtureThermodynamicReading Create(Table table, OniNativeColony.Reading cell, long revision)
        {
            var parts = table.Parts; var phases = table.Phases; var molarMasses = table.MolarMasses; var ids = table.Ids; var catalog = table.Catalog;
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
                vacuum, catalog.Version, catalog.Fingerprint, revision, "colony:cell:" + cell.PublicCell,
                resolvedVacuumInterface: true);
        }
    }
}
