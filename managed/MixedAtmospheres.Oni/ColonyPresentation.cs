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
        private static readonly object sessionGate = new object();
        private static FluidMaterialCatalog? catalog;
        private static string[] ids = Array.Empty<string>();
        private static NativeMatterPhase[] phases = Array.Empty<NativeMatterPhase>();
        private static double[] molarMasses = Array.Empty<double>();
        private static ulong publishedFrame = ulong.MaxValue;
        // Development control: build every reading and presentation at
        // publication, as before the lazy readings (MA_COLONY_PRESENTATION_EAGER=1).
        private static readonly bool eager = Environment.GetEnvironmentVariable("MA_COLONY_PRESENTATION_EAGER") == "1";
        private static OniNativeColony.Reading[] readings = Array.Empty<OniNativeColony.Reading>();
        private static OniNativeColony.Constituent[] parts = Array.Empty<OniNativeColony.Constituent>();
        private static ColonyReadingBuffer? publishedBuffer;
        private static long publications;
        private static readonly bool publicationLog = Environment.GetEnvironmentVariable("MA_COLONY_PUBLICATION_LOG") == "1";
        internal static long Publications => Interlocked.Read(ref publications);
        private static int rejectedCells;
        private static readonly object rejectionGate = new object();
        internal static int RejectedCells => Volatile.Read(ref rejectedCells);
        internal static string FirstRejection { get; private set; } = "";
        // A lazily built reading that fails validation keeps its cell's vanilla projection.
        private static void Rejected(int cell, string message)
        {
            if (Interlocked.Increment(ref rejectedCells) == 1) lock (rejectionGate) FirstRejection = "cell " + cell + ": " + message;
        }
        internal static double LastBuildMilliseconds { get; private set; } // worker
        internal static double CopyMilliseconds { get; private set; }      // game thread
        internal static string LastError { get; private set; } = "";

        internal static void Reset()
        {
            lock (sessionGate)
            {
                catalog = null; publishedFrame = ulong.MaxValue; ColonyReadingPool.Release(publishedBuffer); publishedBuffer = null;
                Interlocked.Exchange(ref publications, 0); LastError = "";
                Volatile.Write(ref rejectedCells, 0); FirstRejection = ""; LastBuildMilliseconds = 0; CopyMilliseconds = 0;
            }
        }

        internal static void Configure()
        {
            lock (sessionGate)
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
                // The colony's recorded breathing rules (docs/settings.md); by
                // default easy mode (docs/colony-owner.md): no upper oxygen bound.
                var breathing = ModSettingsRuntime.Breathing;
                MixturePresentationRuntime.ResetProviders();
                MixturePresentationRuntime.Configure(new MixturePresentationModel(catalog, styles, breathing,
                    SimHashes.Oxygen.ToString(), SimHashes.CarbonDioxide.ToString(), options: MixtureAppearanceSettings.Options));
                publishedFrame = ulong.MaxValue;
            }
        }

        // Called after each data request. The game thread copies the completed
        // frame's readings (made on the solver thread); a worker builds the
        // presentation from them, one frame at a time: a frame completed while
        // a build runs waits for the next request, so the presentation may skip
        // frames but never holds the game thread.
        private static int building;
        // Discard an in-flight pre-edit presentation. Its physics frame may
        // equal the replacement's, so a frame-number comparison is insufficient.
        private static ulong lifecycleRevision;
        internal static void InvalidateLifecycleReadings()
        {lock(sessionGate){++lifecycleRevision;publishedFrame=ulong.MaxValue;}}
        internal static void Publish(ulong completedFrame)
        {
            if (catalog == null || completedFrame == publishedFrame || Interlocked.CompareExchange(ref building, 1, 0) != 0) return;
            var started = System.Diagnostics.Stopwatch.GetTimestamp();
            ulong frame; uint count, partCount;
            if (!eager) { PublishLazy(completedFrame, started); return; }
            using var tracked = ColonyFrameLog.Track("pres_copy");
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
            if (publicationLog) Debug.Log(FormattableString.Invariant($"[Mixed Atmospheres] presentation copy frame {frame} copy_ms {CopyMilliseconds:F3}"));
            int width = Grid.WidthInCells, height = Grid.HeightInCells;
            // Configure replaces these arrays rather than editing them, so the
            // worker keeps a consistent set even if the owner restarts.
            var table = new Table(catalog!, ids, phases, molarMasses, readings, parts);
            var producerSession = MixturePresentationRuntime.NativeDomainSession;
            var revision=lifecycleRevision;
            ThreadPool.QueueUserWorkItem(_ =>
            {
                var begun = System.Diagnostics.Stopwatch.GetTimestamp();
                try
                {
                    var cells = new Dictionary<int, MixtureThermodynamicReading>((int)count);
                    var rejected = 0; var firstRejection = "";
                    using (ColonyFrameLog.Track("pres_readings"))
                    for (var r = 0; r < count; ++r)
                    {
                        try { cells.Add((int)table.Readings[r].PublicCell, Create(table, table.Readings[r], (long)frame)); }
                        catch (ArgumentException error)
                        {
                            // That cell keeps its vanilla projection.
                            if (rejected++ == 0) firstRejection = "cell " + table.Readings[r].PublicCell + ": " + error.Message;
                        }
                    }
                    lock (sessionGate)
                    {
                        if (catalog != table.Catalog || revision!=lifecycleRevision) return;
                        using (ColonyFrameLog.Track("pres_publish"))
                        if (cells.Count != 0 && !MixturePresentationRuntime.PublishNativeDomainFrame(width, height, (long)frame, cells, producerSession)) return;
                        Volatile.Write(ref rejectedCells, rejected);
                        if (rejected > 0) FirstRejection = firstRejection;
                        if (cells.Count != 0) Interlocked.Increment(ref publications);
                    }
                }
                catch (Exception error)
                {
                    // A frame still building when the owner stops or the game
                    // shuts the presentation down is dropped, not a failure.
                    lock (sessionGate)
                        if (catalog == table.Catalog && revision==lifecycleRevision && MixturePresentationRuntime.CurrentModel != null) Failed(error, frame);
                }
                finally
                {
                    lock (sessionGate) if (catalog == table.Catalog) LastBuildMilliseconds = Milliseconds(begun);
                    Volatile.Write(ref building, 0);
                }
            });
        }
        // The normal path: the completed frame's compact readings are copied
        // into a pooled buffer and become the lazily read presentation source.
        // Called with `building` held; releases it.
        private static void PublishLazy(ulong completedFrame, long started)
        {
            ColonyReadingBuffer? buffer = null; var published = false;
            try
            {
                using var tracked = ColonyFrameLog.Track("pres_copy");
                OniNativeColony.Check(OniNativeColony.Readings(out var frame, null, 0, out var count, null, 0, out var partCount));
                buffer = ColonyReadingPool.Acquire((int)count, (int)partCount);
                OniNativeColony.Check(OniNativeColony.Readings(out frame, buffer.Readings, (uint)buffer.Readings.Length, out count, buffer.Parts, (uint)buffer.Parts.Length, out partCount));
                buffer.Count = (int)count; buffer.PartCount = (int)partCount; buffer.Frame = frame; buffer.Sorted = true;
                for (var i = 1; i < buffer.Count && buffer.Sorted; ++i) buffer.Sorted = buffer.Readings[i - 1].PublicCell < buffer.Readings[i].PublicCell;
                publishedFrame = frame;
                CopyMilliseconds = Milliseconds(started);
                if (publicationLog) Debug.Log(FormattableString.Invariant($"[Mixed Atmospheres] presentation copy frame {frame} copy_ms {CopyMilliseconds:F3}"));
                var table = new Table(catalog!, ids, phases, molarMasses, buffer.Readings, buffer.Parts);
                var producerSession = MixturePresentationRuntime.NativeDomainSession;
                var revision = lifecycleRevision;
                using var publishing = ColonyFrameLog.Track("pres_publish");
                lock (sessionGate)
                {
                    if (catalog != table.Catalog || revision != lifecycleRevision || count == 0) return;
                    Volatile.Write(ref rejectedCells, 0);
                    var lazy = new ColonyLazyReadings(buffer, table, (long)frame, Rejected);
                    if (!MixturePresentationRuntime.PublishNativeDomainFrame(Grid.WidthInCells, Grid.HeightInCells, (long)frame, lazy, producerSession)) return;
                    ColonyReadingPool.Release(publishedBuffer); publishedBuffer = buffer; published = true;
                    Interlocked.Increment(ref publications);
                }
            }
            catch (Exception error)
            {
                // A frame still publishing when the owner stops is dropped, not a failure.
                lock (sessionGate)
                    if (catalog != null && MixturePresentationRuntime.CurrentModel != null) Failed(error, completedFrame);
            }
            finally
            {
                if (!published) ColonyReadingPool.Release(buffer);
                LastBuildMilliseconds = Milliseconds(started);
                Volatile.Write(ref building, 0);
            }
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
        internal sealed class Table
        {
            internal readonly FluidMaterialCatalog Catalog; internal readonly string[] Ids; internal readonly NativeMatterPhase[] Phases;
            internal readonly double[] MolarMasses; internal readonly OniNativeColony.Reading[] Readings; internal readonly OniNativeColony.Constituent[] Parts;
            internal Table(FluidMaterialCatalog catalog, string[] ids, NativeMatterPhase[] phases, double[] molarMasses,
                OniNativeColony.Reading[] readings, OniNativeColony.Constituent[] parts)
            { Catalog = catalog; Ids = ids; Phases = phases; MolarMasses = molarMasses; Readings = readings; Parts = parts; }
        }
        internal static MixtureThermodynamicReading Create(Table table, OniNativeColony.Reading cell, long revision)
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
