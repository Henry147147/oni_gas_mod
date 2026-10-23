using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HarmonyLib;
using Klei;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using Newtonsoft.Json;
using ProcGenGame;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Explicit private development opt-in. This validates the real publication
    // and new-colony boundaries while native ONI still owns colony physics.
    // It must never be interpreted as permission to enable an incomplete owner.
    internal static unsafe partial class ColonyRuntimeValidation
    {
        internal sealed class ValidationReport
        {
            public string Kind = "actual-game-colony-boundary-validation";
            public string ColonySavePath = "";
            // Owner frames at the colony saves (an overwrite saves twice to one
            // path) and at the restore of a loaded save.
            public ulong FirstColonySaveFrame, ColonySaveFrame, RestoredFrame;
            // ONI's own Sim.Load result for a colony save loaded without the mod's loader (-1: refused).
            public int? VanillaLoadResult;
            public string OniBuild = "744825";
            public bool AuthoritativeColonySimulationReady = false;
            public string PhysicsOwner = "native ONI";
            public string Status = "pending";
            public string Error = "";
            public string CustomStateImportError = "";
            public string Stage = "loading";
            public string[] VisibleScreens = Array.Empty<string>();
            public int Width, Height, WorldCount, ImportedFluidCells;
            public double ImportedFluidMassKg;
            public bool NewColonyStagingPassed, CustomStateRoundtripPassed, GameStarted;
            public int SnapshotsPublished, NativeFramesAcknowledged, MaxEventsPerSnapshot;
            public long MaxOwnedBytes;
            public bool AllCopiesByteIdentical = true;
            public bool SnapshotCopiesIncludeCustomProjections = false;
            public bool NativeObserverForwardingPassed = false;
            public ulong ObserverTimestepReturns = 0, ObserverPhysicalUpdates = 0;
            public int SnapshotChannels, EventChannels, Duplicants;
            public string[] ActiveDlcIds = Array.Empty<string>();
            public bool LoadContentCompatibilityChecked, LoadContentCompatible;
            public string[] RequiredLoadDlcIds = Array.Empty<string>();
            public string[] UnavailableLoadDlcIds = Array.Empty<string>();
            public string[] RuntimeDlcIds = Array.Empty<string>();
            public string[] ImportedMaterialIds = Array.Empty<string>();
            public string MaterialModel = "explicit local ONI constant-property surrogates; not a thermodynamic catalogue";
            public string MaterialFingerprint = "";
            public string StartedUtc = System.DateTime.UtcNow.ToString("O");
            public string FinishedUtc = "";
            public bool LongRun;
            public int TelemetryIntervalFrames;
            public string DevelopmentTelemetryFile = "";
            public int DevelopmentTelemetryLines;
            public bool DevelopmentTelemetryTruncated;
            public string DevelopmentTelemetryError = "";
        }

        private static ColonyValidationConfiguration? configuration;
        private static readonly ValidationReport Report = new ValidationReport();
        private static OwnedSnapshotPublisher? publisher;
        private static ColonyValidationDriver? driver;
        private static bool inNewColonyLoad;
        private static bool actualColonyActive;
        private static bool done;
        private static int liveFrameBaseline = -1;
        private static ulong observerTimestepBaseline, observerUpdateBaseline;
        private static string ReportPath => Path.Combine(OutputDirectory, "colony-validation.json");
        internal static string OutputDirectory => Path.Combine(Mod.DirectoryPath, "game-contract-local", "colony-validation");
        internal static ColonyValidationConfiguration? Settings => configuration;

        internal static void Initialize()
        {
            var file = Path.Combine(Mod.DirectoryPath, "colony-validation.json");
            if (!File.Exists(file)) return;
            // Mode, frame/timeout, long-run and telemetry limits are engine-free.
            configuration = ColonyValidationLimits.Parse(File.ReadAllText(file));
            if ((configuration.Mode == "native-carbon-particle-domain" || configuration.Mode == "native-breathing-read-domain") && configuration.CompletedFrames < 30)
                throw new InvalidDataException("Native CO2 particle and breathing-reader controls require at least 30 frames.");
            if(configuration.SubnormalCarbonParticle && configuration.Mode!="native-carbon-particle-domain" &&
                !(configuration.VerifyTransientParticles && !configuration.VerifyLiveBreathing && (configuration.Mode=="native-domain" || configuration.Mode=="native-load-domain")))
                throw new InvalidDataException("Subnormal carbon input requires the carbon-particle scene or the explicit transient-particle save/load control.");
            if(configuration.VerifyTransientParticles && ((configuration.Mode!="native-domain" && configuration.Mode!="native-load-domain") ||
                (configuration.Mode=="native-domain" && !configuration.SaveOnComplete) ||
                (configuration.Mode=="native-load-domain" && !configuration.SaveBeforeResume)))
                throw new InvalidDataException("Transient particle validation requires a gas source save or a reload with a frame-zero resave.");
            if(configuration.VerifyLiveBreathing && ((configuration.Mode!="native-domain" && configuration.Mode!="native-load-domain") ||
                configuration.VerifyPersistentActors || (configuration.Mode=="native-domain" && (configuration.CompletedFrames<20 || configuration.CompletedFrames>25))))
                throw new InvalidDataException("Live breathing requires a 20–25-frame gas source or its saved reload, without registered fixture actors.");
            if (configuration.VerifyNaturalAutosave && (configuration.Mode != "native-domain" || configuration.CompletedFrames < 3100 || configuration.CompletedFrames > 5000 || configuration.SaveOnComplete))
                throw new InvalidDataException("Natural autosave validation requires mixed air, 3100..5000 frames and no manual source save.");
            if(configuration.VerifyPersistentActors && ((configuration.Mode!="native-domain" && configuration.Mode!="native-liquid-domain") ||
                (!configuration.SaveOnComplete && !configuration.VerifyNaturalAutosave) || configuration.CompletedFrames<80))
                throw new InvalidDataException("Persistent actor validation requires a gas/water source, at least eighty frames and an ordinary source save or natural autosave.");
            if ((configuration.Mode == "native-actor-domain" || configuration.Mode == "native-liquid-actor-domain") && configuration.CompletedFrames < NativeSealedDomainValidation.MinimumActorValidationFrames)
                throw new InvalidDataException("The registered actor scene requires at least 80 native frames.");
            if (configuration.Mode != "colony-owner-load" && NativeSealedDomainValidation.ColonyRestoreMode != !string.IsNullOrEmpty(configuration.LoadSavePath) ||
                (NativeSealedDomainValidation.ColonyRestoreMode && (!Path.IsPathRooted(configuration.LoadSavePath) ||
                    !File.Exists(configuration.LoadSavePath) || configuration.ReuseGeneratedWorld)))
                throw new InvalidDataException("Paired colony loading requires its explicit mode, an existing absolute save path and the normal saved-colony lifecycle.");
            if ((configuration.Mode == "native-observer" || configuration.Mode == "colony-vanilla" || configuration.Mode.StartsWith("colony-owner", StringComparison.Ordinal) || NativeSealedDomainValidation.IsMode) && !NativeBackendObservation.Enabled)
                throw new InvalidDataException("Native observer validation requires an explicitly enabled observer package.");
            if (configuration.IdleReservoirLeaseOnly && configuration.Mode != "presentation")
                throw new InvalidDataException("Idle reservoir eligibility requires the explicit private presentation test.");
            var fileProtocolHash = configuration.PrivateFileProtocolAssemblySha256;
            if (fileProtocolHash == null || (fileProtocolHash.Length != 0 &&
                (!configuration.IdleReservoirLeaseOnly || fileProtocolHash.Length != 64 || fileProtocolHash.Any(value =>
                    !((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))))))
                throw new InvalidDataException("Private file protocol validation requires an idle fixture and a pinned lowercase SHA256.");
            if ((configuration.Mode == "load-rejection") != configuration.PrivateOwnedLoadRejection ||
                (configuration.Mode == "load-rejection" && (configuration.IdleReservoirLeaseOnly || fileProtocolHash.Length != 0)))
                throw new InvalidDataException("Private load rejection validation requires its explicit flag and a separate native-only colony.");
            if (configuration.SaveOnComplete && configuration.Mode != "colony-owner" && configuration.Mode != "colony-owner-load" && (!NativeSealedDomainValidation.IsMode || NativeSealedDomainValidation.ColonyRestoreMode))
                throw new InvalidDataException("SaveOnComplete requires a colony owner or an initial native domain validation scene.");
            if (configuration.SaveBeforeResume && !NativeSealedDomainValidation.ColonyRestoreMode)
                throw new InvalidDataException("SaveBeforeResume requires an admitted saved-colony validation scene.");
            if (configuration.VerifySaveIoRecovery && (configuration.Mode!="native-save-domain" || configuration.CompletedFrames<16))
                throw new InvalidDataException("Save IO recovery requires the explicit source-save validation scene and at least sixteen frames.");
            if(configuration.ReloadInProcess && configuration.Mode != "colony-owner-load" && (!NativeSealedDomainValidation.ColonyRestoreMode || !configuration.SaveBeforeResume))
                throw new InvalidDataException("In-process reload requires an owner load or a native-domain load with its paused autosave control.");
            Directory.CreateDirectory(OutputDirectory);
            if (configuration.Floor.Length != 0) FloorArenas.Configure(configuration.Floor, OutputDirectory);
            if (NativeSealedDomainValidation.IsMode)
            {
                Report.SnapshotCopiesIncludeCustomProjections = true;
                Report.AllCopiesByteIdentical = false;
            }
            Report.LongRun = configuration.LongRun;
            if (configuration.LongRun) StartTelemetry();
            WriteReport();
            Debug.Log("[Mixed Atmospheres] Explicit colony validation enabled: " + configuration.Mode +
                (configuration.LongRun ? " (long run, " + configuration.CompletedFrames + " frames, development telemetry every " +
                    configuration.TelemetryIntervalFrames + " frames)" : "") +
                (NativeSealedDomainValidation.IsMode ? ". Experimental sealed-domain ownership checks are required." :
                 ColonyOwner.Available ? ". The colony owner takes the asteroid's fluids." : ". Colony physics remains native."));
        }

        internal static bool ObserveMessage => configuration != null && !done;

        internal static IntPtr AfterNativeMessage(int message, IntPtr result)
        {
            if (FloorArenas.Id.Length != 0 && actualColonyActive) FloorArenas.OnNativeMessage(message, result);
            if (configuration?.Mode == "presentation" && ObserveMessage && actualColonyActive && result != IntPtr.Zero &&
                (message == -931446686 || message == 1078620451))
            {
                try { OwnedReservoirVisualValidation.ObserveNativeSnapshot((Sim.GameDataUpdate*)result); }
                catch (Exception error) { Fail(error); }
                return result;
            }
            if ((configuration?.Mode != "snapshot-copy" && configuration?.Mode != "native-observer") || !ObserveMessage || !actualColonyActive || result == IntPtr.Zero ||
                (message != -931446686 && message != 1078620451)) return result;
            try
            {
                var source = (Sim.GameDataUpdate*)result;
                publisher ??= new OwnedSnapshotPublisher();
                var copy = publisher.Publish(source, Grid.WidthInCells, Grid.HeightInCells);
                copy.AssertEquivalentTo(source);
                Report.Width = Grid.WidthInCells; Report.Height = Grid.HeightInCells;
                Report.SnapshotChannels = OwnedGameDataSnapshot.ChannelCount;
                Report.EventChannels = OwnedGameDataSnapshot.EventChannelCount;
                Report.MaxOwnedBytes = Math.Max(Report.MaxOwnedBytes, copy.CapturedBytes);
                Report.MaxEventsPerSnapshot = Math.Max(Report.MaxEventsPerSnapshot,
                    source->numMassConsumedCallbacks + source->numMassEmittedCallbacks + source->numSubstanceChangeInfo +
                    source->numCallbackInfo + source->numSolidInfo + source->numBuildingTemperatures);
                ++Report.SnapshotsPublished;
                if (message == 1078620451) Report.NativeFramesAcknowledged += source->numFramesProcessed;
                if (Report.SnapshotsPublished == 1) WriteReport();
                return (IntPtr)copy.Pointer;
            }
            catch (Exception error)
            {
                Report.AllCopiesByteIdentical = false;
                Fail(error);
                // The original native snapshot is still valid. Never expose a
                // partial owned copy if any descriptor or validation fails.
                return result;
            }
        }

        internal static void BeginNewColony() { if (ObserveMessage) { inNewColonyLoad = true; actualColonyActive = true; } }
        internal static void EndNewColony(Exception? error)
        {
            inNewColonyLoad = false;
            // The native loader can fail before Game.Update ever starts. Keep
            // that failure in the private report instead of leaving it pending.
            if (error != null && ObserveMessage) Fail(error);
        }

        internal static void StageGeneratedWorlds(Cluster cluster, List<SimSaveFileStructure> worlds)
        {
            if (configuration?.Mode != "snapshot-copy" || !ObserveMessage || !inNewColonyLoad) return;
            try
            {
                var placements = cluster.worlds.Select((world, index) => new NativeWorldPlacement(index,
                    world.WorldSize.x, world.WorldSize.y, world.WorldOffset.x, world.WorldOffset.y)).ToArray();
                if (worlds.Count != placements.Length) throw new InvalidDataException("Incomplete generated cluster.");
                var sources = worlds.Select((world, index) => new NativeWorldSource(index, world.Sim)).ToArray();
                for (var index = 0; index < worlds.Count; ++index)
                    File.WriteAllBytes(Path.Combine(OutputDirectory, "generated-world-" + index + ".bin"), worlds[index].Sim);
                var phases = ElementLoader.elements.ToDictionary(element => (int)element.id, Phase);
                var bindings = ElementLoader.elements.Where(element => element.IsGas || element.IsLiquid)
                    .ToDictionary(element => (int)element.id, element => new NativeFluidBinding(OniNitrogenElements.Identity(element.id), Phase(element)));
                var staged = NativeColonyImport.StageNewColony(cluster.size.x, cluster.size.y, placements, sources,
                    phases, bindings, (int)SimHashes.Vacuum);
                Report.NewColonyStagingPassed = true;
                Report.WorldCount = staged.Worlds.Count;
                Report.ImportedFluidCells = staged.Fluids.Count;
                Report.ImportedFluidMassKg = staged.Fluids.Sum(cell => (double)cell.Inventory.MassKg);
                Report.ImportedMaterialIds = staged.Fluids.Select(cell => cell.MaterialId).Distinct().OrderBy(id => id, StringComparer.Ordinal).ToArray();

                // Private, read-only inputs for retrying native MAC admission.
                // Never replace unknown materials or publish these inventories.
                File.WriteAllText(Path.Combine(OutputDirectory, "native-mac-import-input.json"), ReportJson.Serialize(new
                {
                    Source = "generated-v15-worlds-before-native-load", PhysicsFlags = OniNativeDomain.PhysicsVacuum | OniNativeDomain.PhysicsMixture,
                    Worlds = placements, Fluids = staged.Fluids,
                    Bindings = new[] { OniNitrogenElements.Gas, SimHashes.Oxygen, SimHashes.Helium, SimHashes.Water, SimHashes.CarbonDioxide }
                        .Select((hash, species) => new { ElementHash = (int)hash, Species = species }).ToArray(),
                    NativePayloadsUnchanged = true, WholeWorldOwnerActivated = false
                }));

                var catalog = OniNominalFluidCatalog.Build(ElementLoader.elements, Report.ImportedMaterialIds, cellVolumeCubicMetres: 1);
                Report.MaterialModel = OniNominalFluidCatalog.ModelDescription;
                Report.MaterialFingerprint = catalog.Fingerprint;
                var captured = ColonyFluidState.StageImport(staged, catalog);
                var diagnostics = ColonyPlacementDiagnostics.Analyze(captured);
                File.WriteAllText(Path.Combine(OutputDirectory, "initial-placement-diagnostics.json"), ReportJson.Serialize(diagnostics));
                var state = captured.PublishValidated();
                var saved = state.Save();
                var restored = ColonyFluidState.Restore(saved, catalog);
                if (!saved.AsSpan().SequenceEqual(restored.Save())) throw new InvalidDataException("Colony state did not roundtrip exactly.");
                Report.MaterialFingerprint = catalog.Fingerprint;
                Report.CustomStateRoundtripPassed = true;
                File.WriteAllBytes(Path.Combine(OutputDirectory, "initial-custom-state.bin"), saved);
                // Source/native payloads deliberately remain untouched. This
                // test proves staging against real generation before takeover.
                WriteReport();
            }
            catch (Exception error)
            {
                // Continue the independent snapshot publication experiment even
                // when the explicitly limited material model rejects a colony.
                Report.CustomStateImportError = error.ToString();
                WriteReport();
                Debug.LogWarning("[Mixed Atmospheres] Custom colony import rejected; continuing snapshot validation: " + error.Message);
            }
        }

        private static NativeMatterPhase Phase(Element element) => element.IsGas ? NativeMatterPhase.Gas :
            element.IsLiquid ? NativeMatterPhase.Liquid : element.IsSolid ? NativeMatterPhase.Solid : NativeMatterPhase.Vacuum;

        internal static void ValidateSelectedSaveContent(string path)
        {
            var information = SaveLoader.LoadHeader(path, out _);
            if (information.dlcIds == null) throw new InvalidDataException("Selected colony has no content metadata.");
            Report.RequiredLoadDlcIds = information.dlcIds.ToArray();
            Report.RuntimeDlcIds = DlcManager.GetActiveDLCIds().ToArray();
            Report.UnavailableLoadDlcIds = information.dlcIds.Where(id => !DlcManager.IsContentSubscribed(id)).ToArray();
            Report.LoadContentCompatibilityChecked = true;
            Report.LoadContentCompatible = Report.UnavailableLoadDlcIds.Length == 0;
            WriteReport();
            if (!Report.LoadContentCompatible)
                throw new InvalidDataException("Selected colony requires unavailable or disabled content: " +
                    string.Join(", ", Report.UnavailableLoadDlcIds) + ". Install and enable that content before loading; the native backend has not been initialized.");
        }

        internal static void OnMainMenu(MainMenu menu)
        {
            if (configuration == null || done) return;
            if (configuration.Mode == "load-rejection" && OwnedReservoirLoadVisualValidation.ObserveMainMenu(menu)) return;
            NativeSealedDomainValidation.ObserveReloadMainMenu();
            ObserveOwnerReloadMainMenu();
            if(driver!=null) {driver.Initialize(menu);return;}
            var runner = new GameObject("Mixed Atmospheres private colony validation");
            UnityEngine.Object.DontDestroyOnLoad(runner);
            driver=runner.AddComponent<ColonyValidationDriver>();driver.Initialize(menu);
        }

        internal static void UpdateColony()
        {
            if (configuration == null || done || OwnerReloadRequested && ownerReload.Stage==1) return;
            if (configuration.Mode == "load-rejection")
            {
                try { OwnedReservoirLoadVisualValidation.Tick(); }
                catch (Exception error) { OwnedReservoirLoadVisualValidation.RecordFailure(error); Fail(error); }
                return;
            }
            if (Game.Instance == null) return;
            Report.GameStarted = Game.Instance.GameStarted();
            if (!Report.GameStarted) return;
            if (LongRunColonyLost()) return;
            // A requested game speed holds for the whole run (the player's
            // speed control, not the owner's pacing scale).
            var speed = SpeedControlScreen.Instance;
            if (configuration.Speed != 1 && speed != null && speed.GetSpeed() != configuration.Speed - 1) speed.SetSpeed(configuration.Speed - 1);
            if (NativeSealedDomainValidation.IsMode)
            {
                NativeSealedDomainValidation.Tick(); return;
            }
            if (configuration.Mode.StartsWith("colony-owner", StringComparison.Ordinal))
            {
                try { ColonyOwnerScene(); }
                catch (Exception error) { Fail(error); }
                return;
            }
            if (configuration.Mode == "colony-vanilla")
            {
                try { ColonyVanillaScene(); }
                catch (Exception error) { Fail(error); }
                return;
            }
            if (configuration.Mode == "coupled-presentation")
            {
                try { CoupledPresentationValidation.Tick(); }
                catch (Exception error)
                {
                    try { CoupledPresentationValidation.Abort(error); }
                    finally { Fail(error); }
                }
                return;
            }
            if (configuration.Mode == "tile-hover")
            {
                try { TileHoverContentsValidation.Tick(); }
                catch (Exception error)
                {
                    try { TileHoverContentsValidation.Abort(error); }
                    finally { Fail(error); }
                }
                return;
            }
            if (configuration.Mode == "presentation")
            {
                try { MixtureVisualValidation.Tick(); }
                catch (Exception error) { Fail(error); }
                return;
            }
            if (liveFrameBaseline < 0)
            {
                liveFrameBaseline = Report.NativeFramesAcknowledged;
                if (configuration.Mode == "native-observer")
                {
                    var observation = NativeBackendObservation.Capture();
                    observerTimestepBaseline = observation.PositiveTimestepReturns;
                    observerUpdateBaseline = observation.Hooks.Single(hook => hook.Name == "update-data").Entries;
                }
                SetStage("running-disposable-colony");
            }
            Report.Duplicants = Components.LiveMinionIdentities.Count;
            if (configuration.Mode == "native-observer")
            {
                var observation = NativeBackendObservation.Capture();
                if (observation.PositiveTimestepReturns < observerTimestepBaseline ||
                    observation.Hooks.Single(hook => hook.Name == "update-data").Entries < observerUpdateBaseline)
                    throw new InvalidOperationException("Native observer counters reset during the active colony validation.");
                Report.Kind = "actual-game-native-hook-forwarding-validation";
                Report.ObserverTimestepReturns = observation.PositiveTimestepReturns - observerTimestepBaseline;
                Report.ObserverPhysicalUpdates = observation.Hooks.Single(hook => hook.Name == "update-data").Entries - observerUpdateBaseline;
                var forwardingValid = observation.Attached && !observation.RecoveryRequired &&
                    !observation.AuthoritativeFluidOwner && Report.AllCopiesByteIdentical;
                // Native data publications/managed acknowledgments can repeat
                // without a physical timestep. Keep observing until both native
                // progress counters reach the requested amount.
                if (forwardingValid && (Report.ObserverTimestepReturns < (ulong)configuration.CompletedFrames ||
                    Report.ObserverPhysicalUpdates < (ulong)configuration.CompletedFrames)) { WriteProgressReport(); return; }
                Report.NativeObserverForwardingPassed = forwardingValid;
                Report.Status = Report.NativeObserverForwardingPassed ? "passed" : "failed";
                Report.WorldCount = ClusterManager.Instance.WorldContainers.Count;
                Report.MaterialModel = "Original native ONI physics; no custom state import or fluid takeover.";
                if (Report.Status == "failed") Report.Error = "Native forwarding progress or snapshot equivalence was not established.";
            }
            else
            {
                if (Report.NativeFramesAcknowledged - liveFrameBaseline < configuration.CompletedFrames) return;
                Report.Status = Report.NewColonyStagingPassed && Report.CustomStateRoundtripPassed && Report.AllCopiesByteIdentical
                    ? "passed" : "failed";
                if (Report.Status == "failed") Report.Error = "Required new-colony staging or roundtrip evidence is missing.";
            }
            Report.ActiveDlcIds = SaveLoader.Instance.GameInfo.dlcIds.ToArray();
            Report.Stage = "completed";
            Report.FinishedUtc = System.DateTime.UtcNow.ToString("O");
            done = true;
            WriteReport();
            CaptureScreenshot("colony.png");
        }

        // A normal new colony with the colony owner (docs/colony-owner.md): pass
        // after the requested owner frames without a fault.
        private static readonly ColonyFrameWindow colonyWindow = new ColonyFrameWindow();
        private static System.DateTime colonyOwnerWallStart;
        private static void ColonyOwnerScene()
        {
            Report.Kind = "actual-game-colony-owner-validation";
            Report.Width = Grid.WidthInCells; Report.Height = Grid.HeightInCells;
            Report.Duplicants = Components.LiveMinionIdentities.Count;
            if (ClusterManager.Instance != null) Report.WorldCount = ClusterManager.Instance.WorldContainers.Count;
            if (!ColonyOwner.Active)
            {
                if (ColonyOwner.StartFailure.Length != 0) throw new InvalidOperationException("Colony owner did not start: " + ColonyOwner.StartFailure);
                return;
            }
            var status = ColonyOwner.Status;
            if (colonyOwnerWallStart == default) colonyOwnerWallStart = System.DateTime.UtcNow;
            Report.PhysicsOwner = "colony owner (MacClosedWorld); native ONI for solids";
            var faulted = status.State == 2;
            // A system scenario (ColonyScenarios) decides when the run ends;
            // its checks of this frame go into the report below.
            var scenarioDone = configuration!.Scenario.Length != 0 && !faulted && ColonyScenarios.Update(configuration.Scenario, status);
            var owner = new {
                SchemaVersion = 1, status.State, Faulted = faulted, status.Session, status.CompletedFrames, status.Substeps, status.RejectedAttempts, status.Species,
                status.TimeSeconds, status.LastAdvanceMilliseconds, status.MaximumAdvanceMilliseconds, status.StageMilliseconds,
                status.NativeHeatJoules, status.NativeMassInKg, status.NativeMassOutKg, status.NativeChanges,
                status.EvaporatedKg, status.CondensedKg, status.SkippedRelaxations, status.ReleasedKg, status.ConvertedKg, status.SettledKg,
                status.MaximumRelativeMassError, status.MaximumRelativeEnergyError, status.ReservoirKg, status.SpaceSinkKg, status.TraceSinkKg, status.TraceSinkJoules,
                status.InjectedKg, status.ExtractedKg, status.CommittedTransfers, status.CommittedHeat, status.OpenedCells, status.ClosedCells,
                ColonyOwner.OwnedCells, ColonyOwner.Publications, ColonyOwnerPersistence.Saves, ColonyOwnerPersistence.Loads,
                ColonyPacing.HeldTicks, ColonyPacing.UnityFrames, ColonyPacing.OwnerRate, ColonyPacing.TimeScale,
                WallSeconds = (System.DateTime.UtcNow - colonyOwnerWallStart).TotalSeconds,
                PresentationFrames = ColonyPresentation.Publications, PresentationRejectedCells = ColonyPresentation.RejectedCells, ColonyPresentation.FirstRejection,
                PresentationBuildMilliseconds = ColonyPresentation.LastBuildMilliseconds, PresentationCopyMilliseconds = ColonyPresentation.CopyMilliseconds,
                ColonyOwner.PublicationMilliseconds, ColonyOwner.MaximumPublicationMilliseconds, PresentationError = ColonyPresentation.LastError,
                ColonyCopyValidation.ViewComparisons, ColonyCopyValidation.ReadingComparisons, CopyControlChangedFrames = ColonyCopyValidation.ChangedFrames,
                ViewCopies = ColonyViewCache.Copies, ViewCacheHits = ColonyViewCache.Hits,
                PublicationCellMapComparisons = ColonyOwner.CellMapComparisons,
                OwnerConfigFlags = ColonyOwner.Config.Flags, OwnerMaximumTimestepSeconds = ColonyOwner.Config.MaximumTimestepSeconds,
                ViewCacheComparisons = ColonyViewCache.Comparisons, ViewCacheChangedDuringComparison = ColonyViewCache.ChangedDuringComparison,
                ReceiptCopyComparisons = ColonyCopyValidation.ReceiptComparisons, ReceiptCopyEmptyComparisons = ColonyCopyValidation.ReceiptEmptyComparisons,
                ReceiptEntriesCompared = ColonyCopyValidation.ReceiptEntriesCompared, ReceiptCopyGrowthComparisons = ColonyCopyValidation.ReceiptGrowthComparisons,
                ReceiptCopyPinnedMilliseconds = ColonyCopyValidation.ReceiptPinnedMilliseconds,
                ReceiptCopyMarshalledMilliseconds = ColonyCopyValidation.ReceiptMarshalledMilliseconds,
                ReceiptCopyComparedCounts = ColonyCopyValidation.ReceiptComparedCounts,
                PresentationRevision = MixturePresentationRuntime.CurrentRevision, RendererTextureWrites = MixtureWorldRendering.TextureWrites,
                RendererError = MixtureWorldRendering.LastError,
                BlockedMilliseconds = ColonyOwner.BlockedMilliseconds.ToDictionary(item => item.Key.ToString(), item => item.Value),
                LongestBlockMilliseconds = ColonyOwner.LongestBlockMilliseconds.ToDictionary(item => item.Key.ToString(), item => item.Value),
                Census = ColonyOwner.Census.OrderByDescending(item => item.Value).ToDictionary(item => item.Key.ToString(), item => item.Value),
                Scenario = ColonyScenarios.Current, Scenarios = ColonyScenarios.Results, OwnerMenuReload = OwnerReloadReport(),
                Consumers = ColonyActors.Consumers, Emitters = ColonyActors.Emitters, NativeActors = ColonyActors.Native, ActorTransfers = ColonyActors.Queued, ConsumerEvents = ColonyActors.Delivered,
                Error = status.Error
            };
            ReportJson.WriteAtomic(Path.Combine(OutputDirectory, "colony-owner.json"), owner);
            if (faulted) throw new InvalidOperationException("Colony owner faulted: " + status.Error);
            var loading = configuration.Mode == "colony-owner-load";
            if (loading && ColonyOwnerPersistence.Loads != (OwnerReloadRequested && ownerReload.Stage>=2 ? 2 : 1)) throw new InvalidOperationException("The loaded colony did not restore its owner.");
            colonyWindow.Start(loading ? status.CompletedFrames : 0);
            if(loading) Report.RestoredFrame=colonyWindow.Baseline;
            // ONI pauses a colony when it loads it and after a save; an
            // unattended run plays on.
            if (SpeedControlScreen.Instance != null && SpeedControlScreen.Instance.IsPaused) SpeedControlScreen.Instance.Unpause(false);
            LogDuplicants(status.CompletedFrames);
            if (configuration.Floor.Length != 0)
            {
                if (!FloorArenas.Update()) { if (Time.frameCount % 60 == 0) WriteReport(); return; }
            }
            else if (configuration.Scenario.Length != 0)
            {
                if (!scenarioDone) { if (Time.frameCount % 60 == 0) WriteReport(); return; }
                if (!ColonyScenarios.AllPassed)
                    throw new InvalidOperationException("Colony scenarios failed: " + string.Join("; ", ColonyScenarios.Results.Where(r => !r.Passed)
                        .Select(r => r.Name + ": " + string.Join(", ", r.Checks.Where(check => !check.Passed).Select(check => check.Name + " (" + check.Detail + ")")))));
            }
            else if (!colonyWindow.HasReached(status.CompletedFrames, (ulong)configuration.CompletedFrames))
            {
                // Overwriting a save: the first save at half the frames, the
                // second to the same path at the end.
                if (configuration.OverwriteSave && Report.FirstColonySaveFrame == 0 &&
                    colonyWindow.HasReached(status.CompletedFrames, (ulong)configuration.CompletedFrames / 2))
                {
                    SaveColony(); Report.FirstColonySaveFrame = ColonyOwnerPersistence.LastSavedFrame; Report.ColonySavePath = "";
                }
                if (Time.frameCount % 60 == 0) WriteReport();
                return;
            }
            if (configuration.SaveOnComplete && Report.ColonySavePath.Length == 0) { SaveColony(); Report.ColonySaveFrame = ColonyOwnerPersistence.LastSavedFrame; }
            Report.Status = "passed"; Report.Stage = "completed";
            Report.ActiveDlcIds = SaveLoader.Instance.GameInfo.dlcIds.ToArray();
            Report.FinishedUtc = System.DateTime.UtcNow.ToString("O");
            if(ContinueOwnerMenuReload()) return;
            done = true; WriteReport();
            CaptureScreenshot("colony.png");
        }
        // MA_COLONY_DUPE_LOG=1: every 150 owner frames, each duplicant's needs
        // and the air at its cell and the cell above (a long run's diagnosis).
        private static readonly bool dupeLog = System.Environment.GetEnvironmentVariable("MA_COLONY_DUPE_LOG") == "1";
        private static ulong dupeLogged;
        private static void LogDuplicants(ulong frame)
        {
            if (!dupeLog || frame < dupeLogged + 150) return;
            dupeLogged = frame;
            string Air(int cell)
            {
                if (!Grid.IsValidCell(cell)) return "invalid";
                var breathable = ColonyOwner.TryReadBreathingCell(cell, out _, out var offered) && offered != SimHashes.Vacuum;
                var count = ColonyOwner.Composition(cell, out var indices, out var masses);
                double moles = 0, o2 = 0, co2 = 0;
                for (var k = 0; k < count; ++k)
                {
                    var e = ElementLoader.elements[(int)indices[k]]; if (!e.IsGas) continue;
                    var n = masses[k] / (e.molarMass / 1000); moles += n;
                    if (e.id == SimHashes.Oxygen || e.id == SimHashes.ContaminatedOxygen) o2 += n; else if (e.id == SimHashes.CarbonDioxide) co2 += n;
                }
                // Ideal-gas pressure of the cell's gas over a full cell (an estimate under liquid).
                var pressure = moles * 8.314462618 * Grid.Temperature[cell];
                return $"{Grid.Element[cell].id} {Grid.Mass[cell]:F2}kg p~{pressure / 1000:F0}kPa T={Grid.Temperature[cell] - 273.15f:F1}C O2={(moles > 0 ? 100 * o2 / moles : 0):F2}% CO2={(moles > 0 ? 100 * co2 / moles : 0):F2}% breathable={breathable}";
            }
            foreach (var building in Components.BuildingCompletes.Items)
            {
                var id = building.PrefabID().Name;
                if (id != CO2ScrubberConfig.ID && id != MineralDeoxidizerConfig.ID && id != GeneratorConfig.ID) continue;
                var stored = building.GetComponents<Storage>().SelectMany(s => s.items).Where(i => i != null)
                    .Select(i => i.GetComponent<PrimaryElement>()).Where(e => e != null).GroupBy(e => e.ElementID)
                    .Select(g => $"{g.Key} {g.Sum(e => e.Mass):F1}kg");
                Debug.Log($"[Mixed Atmospheres] machine {id} frame {frame}: {ColonyScenarios.MachineState(building.gameObject)}; stored {string.Join(", ", stored)}");
            }
            foreach (var minion in Components.LiveMinionIdentities.Items)
            {
                var cell = Grid.PosToCell(minion);
                float Value(Klei.AI.Amount a) => a.Lookup(minion.gameObject)?.value ?? float.NaN;
                Grid.CellToXY(cell, out var x, out var y);
                Debug.Log($"[Mixed Atmospheres] dupe {minion.name} frame {frame} at {x},{y} breath {Value(Db.Get().Amounts.Breath):F0} hp {Value(Db.Get().Amounts.HitPoints):F0} " +
                    $"kcal {Value(Db.Get().Amounts.Calories) / 1000:F0} stress {Value(Db.Get().Amounts.Stress):F0}; air {Air(cell)}; above {Air(Grid.CellAbove(cell))}");
            }
        }
        // A floor arena under ONI's own simulation: the owner never starts
        // (ColonyOwner.Available), so the arena is ONI's.
        private static void ColonyVanillaScene()
        {
            if (ColonyOwner.Active) throw new InvalidOperationException("The colony owner started in a vanilla run.");
            Report.PhysicsOwner = "ONI native simulation (colony owner off)";
            Report.Duplicants = Components.LiveMinionIdentities.Count;
            if (SpeedControlScreen.Instance != null && SpeedControlScreen.Instance.IsPaused) SpeedControlScreen.Instance.Unpause(false);
            if (!FloorArenas.Update()) { if (Time.frameCount % 60 == 0) WriteReport(); return; }
            Report.Status = "passed"; Report.Stage = "completed";
            Report.ActiveDlcIds = SaveLoader.Instance.GameInfo.dlcIds.ToArray();
            Report.FinishedUtc = System.DateTime.UtcNow.ToString("O");
            done = true; WriteReport();
        }
        // An ordinary manual save, through the game's own save path (the new
        // colony's first autosave already counted one).
        private static void SaveColony()
        {
            SpeedControlScreen.Instance.Pause(false);
            var path = Path.Combine(OutputDirectory, "colony-owner.sav");
            var before = ColonyOwnerPersistence.Saves;
            SaveLoader.Instance.Save(path, false, false);
            if (ColonyOwnerPersistence.Saves != before + 1 || !File.Exists(path)) throw new InvalidOperationException("The colony owner did not write its save.");
            Report.ColonySavePath = path;
        }
        internal static void SetStage(string stage) { Report.Stage = stage; WriteReport(); }
        internal static void RecordNativeDomainPublication(Sim.GameDataUpdate* source, OwnedGameDataSnapshot copy)
        {
            Report.PhysicsOwner = "experimental custom domain; ownership unverified; native ONI outside";
            Report.Width = Grid.WidthInCells; Report.Height = Grid.HeightInCells;
            // Sim.Start/PostLoadPrefabs publish a restored frame before the
            // managed cluster service is constructed. Metadata follows that
            // service's normal initialization; the native frame is already valid.
            if (ClusterManager.Instance != null) Report.WorldCount = ClusterManager.Instance.WorldContainers.Count;
            Report.Duplicants = Components.LiveMinionIdentities.Count;
            Report.ActiveDlcIds = SaveLoader.Instance.GameInfo.dlcIds.ToArray();
            Report.SnapshotChannels = OwnedGameDataSnapshot.ChannelCount;
            Report.EventChannels = OwnedGameDataSnapshot.EventChannelCount;
            Report.MaxOwnedBytes = Math.Max(Report.MaxOwnedBytes, copy.CapturedBytes);
            ++Report.SnapshotsPublished;
            Report.NativeFramesAcknowledged += source->numFramesProcessed;
        }
        internal static void CompletePresentation(string kind = "actual-game-mixture-presentation-fixture-validation")
        {
            if (NativeSealedDomainValidation.IsMode)
                Report.PhysicsOwner = "custom native sealed domain; native ONI outside";
            Report.Kind = kind;
            Report.Status = "passed";
            Report.GameStarted = true;
            Report.Duplicants = Components.LiveMinionIdentities.Count;
            Report.Width = Grid.WidthInCells; Report.Height = Grid.HeightInCells;
            Report.WorldCount = ClusterManager.Instance.WorldContainers.Count;
            Report.ActiveDlcIds = SaveLoader.Instance.GameInfo.dlcIds.ToArray();
            Report.Stage = "completed"; Report.FinishedUtc = System.DateTime.UtcNow.ToString("O");
            if(NativeSealedDomainValidation.ContinueInProcessReload())
            {
                File.WriteAllText(Path.Combine(OutputDirectory,"first-load-colony-validation.json"),ReportJson.Serialize(Report));
                Report.Status="pending";Report.Stage="returning-to-main-menu-for-reload";Report.FinishedUtc="";done=false;WriteReport();
                var driver=UnityEngine.Object.FindFirstObjectByType<ColonyValidationDriver>();
                if(driver==null) throw new InvalidOperationException("In-process reload has no screenshot handoff driver.");
                driver.StartCoroutine(ReturnToMenuAfterScreenshot());return;
            }
            done = true; WriteReport();
        }
        internal static void CompleteLoadRejection(int width, int height, int worldCount)
        {
            Report.Kind = "actual-game-owned-load-rejection-validation";
            Report.Status = "passed"; Report.GameStarted = true;
            Report.Width = width; Report.Height = height; Report.WorldCount = worldCount;
            Report.Stage = "completed"; Report.FinishedUtc = System.DateTime.UtcNow.ToString("O");
            done = true; WriteReport();
        }
        internal static void RecordWaitingScreens()
        {
            Report.VisibleScreens = Resources.FindObjectsOfTypeAll<KScreen>()
                .Where(screen => screen.gameObject.activeInHierarchy)
                .Select(screen => screen.GetType().FullName + ":" + screen.IsActive()).Distinct().OrderBy(name => name).ToArray();
            WriteReport();
            CaptureScreenshot("waiting.png");
        }
        internal static void RecordVanillaLoad(int result)
        {
            if (done) return;
            Report.VanillaLoadResult = result;
            if (result != -1) { Fail(new InvalidOperationException("ONI's own loader accepted the colony save (" + result + ").")); return; }
            Report.Status = "passed"; Report.Stage = "vanilla-load-refused";
            Report.FinishedUtc = System.DateTime.UtcNow.ToString("O");
            done = true; WriteReport();
        }
        internal static void Fail(Exception error)
        {
            // A Unity error may arrive after the private load driver's final
            // frame. Preserve that failure in the runner's primary report too.
            if (done && (configuration?.Mode != "load-rejection" || Report.Status != "passed")) return;
            done = true; Report.Status = "failed"; Report.Error = error.ToString();
            Report.FinishedUtc = System.DateTime.UtcNow.ToString("O");
            WriteReport();
            Debug.LogWarning("[Mixed Atmospheres] Colony validation failed: " + error);
        }
        internal static void CaptureScreenshot(string filename)
        {
            // Reflection keeps the normal adapter independent of an otherwise
            // unused Unity screenshot module. The image is produced by Unity.
            var capture = Type.GetType("UnityEngine.ScreenCapture, UnityEngine.ScreenCaptureModule");
            capture?.GetMethod("CaptureScreenshot", new[] { typeof(string), typeof(int) })?.Invoke(null,
                new object[] { Path.Combine(OutputDirectory, NativeSealedDomainValidation.ReloadScreenshotName(filename)), 1 });
        }
        private static System.Collections.IEnumerator ReturnToMenuAfterScreenshot(string filename="first-load-native-domain.png")
        {
            // Unity captures at the end of the frame. Starting the loading
            // overlay immediately would put that overlay in the evidence PNG.
            var path=Path.Combine(OutputDirectory,filename);
            var deadline=Time.realtimeSinceStartup+10;
            yield return new WaitForEndOfFrame();
            while(!CompletedScreenshot(path)) {
                if(Time.realtimeSinceStartup>deadline) {Fail(new IOException("First colony screenshot did not finish before reload."));yield break;}
                yield return null;
            }
            yield return null;
            LoadingOverlay.Load(PauseScreen.TriggerQuitGame);
        }
        private static bool CompletedScreenshot(string path)
        {
            try {
                using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite)) {
                    if(stream.Length<20) return false;
                    var start=new byte[8];if(stream.Read(start,0,8)!=8 || !start.SequenceEqual(new byte[]{137,80,78,71,13,10,26,10})) return false;
                    stream.Position=stream.Length-12;var end=new byte[12];
                    return stream.Read(end,0,12)==12 && end.SequenceEqual(new byte[]{0,0,0,0,73,69,78,68,174,66,96,130});
                }
            } catch(IOException) {return false;}
        }
        private static void WriteReport() => ReportJson.WriteAtomic(ReportPath, Report);
        internal static void Shutdown()
        {
            ShutdownTelemetry();
            if(OwnerReloadRequested) ownerReload.Shutdown();
            colonyWindow.Reset();
            ColonyConduits.Reset();
            actualColonyActive = false; publisher?.Dispose(); publisher = null;
            if (NativeSealedDomainValidation.IsMode) NativeSealedDomainValidation.Shutdown();
            ColonyOwner.ResetForNewSession();
            if (configuration?.Mode == "coupled-presentation") CoupledPresentationValidation.Abort();
        }
    }

    // A game-internal test runner, enabled only by the private configuration.
    // It uses normal new-colony entry points and never selects an existing save.
    public sealed class ColonyValidationDriver : MonoBehaviour
    {
        private float started, nextAction;
        private float nextWaitingCapture;
        private MainMenu? menu;
        private int completionWaitFrames;
        private float minionsReadyAt = -1;
        private readonly HashSet<int> visited = new HashSet<int>();
        internal void Initialize(MainMenu mainMenu)
        {
            menu=mainMenu;started=Time.realtimeSinceStartup;nextAction=started+3;
            completionWaitFrames=0;minionsReadyAt=-1;visited.Clear();
        }
        private void Update()
        {
            var config = ColonyRuntimeValidation.Settings;
            if (config == null) return;
            // Development telemetry also records the final line after the
            // validation completes and before the optional quit below.
            DevelopmentTelemetry.Sampler.RecordUnityFrame(Time.unscaledDeltaTime * 1000.0);
            ColonyRuntimeValidation.TickTelemetry();
            if (!ColonyRuntimeValidation.ObserveMessage)
            {
                if (config.QuitWhenComplete && ++completionWaitFrames >= 20) Application.Quit();
                return;
            }
            if (Time.realtimeSinceStartup - started > config.TimeoutSeconds)
            { ColonyRuntimeValidation.Fail(new TimeoutException("Disposable colony validation timed out.")); return; }
            ColonyRuntimeValidation.UpdateColony();
            if (config.Mode == "load-rejection" && OwnedReservoirLoadVisualValidation.Started) return;
            if (Game.Instance != null && !Game.Instance.GameStarted() && Time.realtimeSinceStartup >= nextWaitingCapture)
            {
                nextWaitingCapture = Time.realtimeSinceStartup + 10;
                ColonyRuntimeValidation.RecordWaitingScreens();
            }
            if (!config.AutoStartNewColony || Time.realtimeSinceStartup < nextAction) return;
            nextAction = Time.realtimeSinceStartup + 1;
            try
            {
                // Steam can refresh subscribed workshop metadata during a
                // disposable run. Acknowledge only that informational dialog;
                // never confirm arbitrary save, overwrite or purchase dialogs.
                var modEvents = Resources.FindObjectsOfTypeAll<ConfirmDialogScreen>().FirstOrDefault(dialog =>
                {
                    if (!dialog.gameObject.activeInHierarchy || !dialog.IsActive()) return false;
                    var title = AccessTools.Field(typeof(ConfirmDialogScreen), "titleText").GetValue(dialog);
                    return string.Equals((string?)title?.GetType().GetProperty("text")?.GetValue(title), "MOD EVENTS", StringComparison.OrdinalIgnoreCase);
                });
                if (modEvents != null) { modEvents.OnSelect_OK(); return; }
                if (menu != null && visited.Add(menu.GetInstanceID()))
                {
                    ColonyRuntimeValidation.SetStage("starting-disposable-colony");
                    if (config.Mode == "native-load-domain" || config.Mode == "colony-owner-load")
                    {
                        if (string.IsNullOrEmpty(config.LoadSavePath) || !File.Exists(config.LoadSavePath)) throw new FileNotFoundException("A completed native colony save is required.");
                        ColonyRuntimeValidation.ValidateSelectedSaveContent(Path.GetFullPath(config.LoadSavePath));
                        SaveLoader.SetActiveSaveFilePath(Path.GetFullPath(config.LoadSavePath));
                        LoadingOverlay.Load(SaveLoader.LoadScene);
                    }
                    else if (config.ReuseGeneratedWorld)
                    {
                        if (!File.Exists(WorldGen.WORLDGEN_SAVE_FILENAME)) throw new FileNotFoundException("No generated world available for the requested test.");
                        WorldGen.WaitForPendingLoadSettings();
                        SaveLoader.SetActiveSaveFilePath(null);
                        LoadingOverlay.Load(SaveLoader.LoadScene);
                    }
                    else AccessTools.Method(typeof(MainMenu), "NewGame").Invoke(menu, null);
                    return;
                }
                var mode = UnityEngine.Object.FindFirstObjectByType<ModeSelectScreen>();
                if (mode != null && mode.IsActive() && visited.Add(mode.GetInstanceID()))
                { Invoke(mode, "OnClickSurvival"); return; }
                var category = UnityEngine.Object.FindFirstObjectByType<ClusterCategorySelectionScreen>();
                if (category != null && category.IsActive() && visited.Add(category.GetInstanceID()))
                {
                    var method = AccessTools.Method(typeof(ClusterCategorySelectionScreen), "OnClickOption");
                    var type = method.GetParameters()[0].ParameterType;
                    method.Invoke(category, new[] { Enum.Parse(type, DlcManager.IsExpansion1Active() ? "SpacedOutStyle" : "Vanilla") });
                    return;
                }
                var destination = UnityEngine.Object.FindFirstObjectByType<ColonyDestinationSelectScreen>();
                if (destination != null && destination.IsActive() && visited.Add(destination.GetInstanceID()))
                {
                    if (!string.IsNullOrEmpty(config.Cluster))
                        CustomGameSettings.Instance.SetQualitySetting(Klei.CustomSettings.CustomGameSettingConfigs.ClusterLayout, config.Cluster);
                    CustomGameSettings.Instance.SetQualitySetting(Klei.CustomSettings.CustomGameSettingConfigs.WorldgenSeed, config.Seed);
                    CustomGameSettings.Instance.SetQualitySetting(Klei.CustomSettings.CustomGameSettingConfigs.SaveToCloud, "Disabled");
                    Invoke(destination, "LaunchClicked"); return;
                }
                var minions = Resources.FindObjectsOfTypeAll<MinionSelectScreen>()
                    .FirstOrDefault(screen => screen.gameObject.activeInHierarchy);
                if (minions != null && !visited.Contains(minions.GetInstanceID()))
                {
                    if (minionsReadyAt < 0) minionsReadyAt = Time.realtimeSinceStartup + 3;
                    var characters = minions.GetComponentsInChildren<CharacterContainer>();
                    if (Time.realtimeSinceStartup < minionsReadyAt || characters.Length != 3 || characters.Any(character => character.Stats?.personality == null)) return;
                    visited.Add(minions.GetInstanceID());
                    var gameInfo = SaveLoader.Instance.GameInfo;
                    gameInfo.baseName = config.ColonyName;
                    AccessTools.Property(typeof(SaveLoader), nameof(SaveLoader.GameInfo)).SetValue(SaveLoader.Instance, gameInfo);
                    SaveGame.Instance.SetBaseName(config.ColonyName);
                    CustomGameSettings.Instance.SetQualitySetting(Klei.CustomSettings.CustomGameSettingConfigs.SaveToCloud, "Disabled");
                    ColonyRuntimeValidation.SetStage("embarking");
                    Invoke(minions, "OnProceed");
                    return;
                }
                var intro = Resources.FindObjectsOfTypeAll<WattsonMessage>().FirstOrDefault(screen => screen.gameObject.activeInHierarchy);
                if (intro != null && visited.Add(intro.GetInstanceID()))
                {
                    // Native new-base callbacks can assign a random save path
                    // after character selection. Pin the unique disposable
                    // path immediately before the intro schedules InitialSave.
                    var saveRoot = Path.GetFullPath(SaveLoader.GetSavePrefix());
                    var folder = Path.GetFullPath(Path.Combine(saveRoot, config.ColonyName));
                    if (Path.GetDirectoryName(folder) != saveRoot.TrimEnd(Path.DirectorySeparatorChar) || Directory.Exists(folder))
                        throw new InvalidDataException("The disposable colony save directory is not a fresh immediate child.");
                    SaveLoader.SetActiveSaveFilePath(Path.Combine(folder, config.ColonyName + ".sav"));
                    intro.Deactivate();
                }
            }
            catch (Exception error) { ColonyRuntimeValidation.Fail(error); }
        }
        private static void Invoke(object target, string name) => AccessTools.Method(target.GetType(), name).Invoke(target, null);
    }

    [HarmonyPatch(typeof(MinionSelectScreen), "OnSpawn")]
    internal static class MinionSelectionValidationPatch
    {
        private static void Postfix(MinionSelectScreen __instance)
        {
            if (ColonyRuntimeValidation.Settings?.AutoStartNewColony != true) return;
            UnityEngine.Debug.Log("[Mixed Atmospheres] Disposable colony character selection spawned: " + __instance.IsActive());
        }
    }

    [HarmonyPatch(typeof(SaveLoader), nameof(SaveLoader.LoadFromWorldGen))]
    internal static class NewColonyValidationScope
    {
        private static void Prefix() => ColonyRuntimeValidation.BeginNewColony();
        private static Exception? Finalizer(Exception? __exception) { ColonyRuntimeValidation.EndNewColony(__exception); return __exception; }
    }

    [HarmonyPatch(typeof(Cluster), nameof(Cluster.LoadClusterSim))]
    internal static class GeneratedColonyValidationPatch
    {
        private static void Postfix(Cluster __instance, List<SimSaveFileStructure> __0) => ColonyRuntimeValidation.StageGeneratedWorlds(__instance, __0);
    }

    [HarmonyPatch(typeof(Sim), nameof(Sim.Shutdown))]
    internal static class SnapshotOwnerShutdownPatch
    {
        private static void Postfix() => ColonyRuntimeValidation.Shutdown();
    }
}
