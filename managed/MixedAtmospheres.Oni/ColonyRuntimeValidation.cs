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
    internal static unsafe class ColonyRuntimeValidation
    {
        internal sealed class Configuration
        {
            public string Mode = "snapshot-copy";
            public string LoadSavePath = "";
            public bool AutoStartNewColony = false;
            public bool ReuseGeneratedWorld = false;
            public bool IdleReservoirLeaseOnly = false;
            public string PrivateFileProtocolAssemblySha256 = "";
            public bool PrivateOwnedLoadRejection = false;
            public bool QuitWhenComplete = false;
            public bool SaveOnComplete = false;
            public bool SaveBeforeResume = false;
            public bool VerifySaveIoRecovery = false;
            public bool ReloadInProcess = false;
            public int CompletedFrames = 30;
            public int TimeoutSeconds = 600;
            public string Seed = "147147";
            public string ColonyName = "MA Visual Test";
        }

        internal sealed class ValidationReport
        {
            public string Kind = "actual-game-colony-boundary-validation";
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
        }

        private static Configuration? configuration;
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
        internal static Configuration? Settings => configuration;

        internal static void Initialize()
        {
            var file = Path.Combine(Mod.DirectoryPath, "colony-validation.json");
            if (!File.Exists(file)) return;
            var bytes = File.ReadAllText(file);
            if (bytes.Length > 8192) throw new InvalidDataException("Colony validation configuration is too large.");
            configuration = JsonConvert.DeserializeObject<Configuration>(bytes, new JsonSerializerSettings
            { TypeNameHandling = TypeNameHandling.None, MissingMemberHandling = MissingMemberHandling.Error })
                ?? throw new InvalidDataException("Missing colony validation configuration.");
            if ((configuration.Mode != "snapshot-copy" && configuration.Mode != "native-observer" && configuration.Mode != "native-import-domain" && configuration.Mode != "native-liquid-import-domain" && configuration.Mode != "native-domain" && configuration.Mode != "native-liquid-domain" && configuration.Mode != "native-liquid-transfer-domain" && configuration.Mode != "native-heat-domain" && configuration.Mode != "native-liquid-heat-domain" && configuration.Mode != "native-transfer-domain" && configuration.Mode != "native-actor-domain" && configuration.Mode != "native-liquid-actor-domain" && configuration.Mode != "native-pressure-read-domain" && configuration.Mode != "native-liquid-pressure-read-domain" && configuration.Mode != "native-breathing-read-domain" && configuration.Mode != "native-carbon-particle-domain" && configuration.Mode != "native-carbon-domain" && configuration.Mode != "native-conduction-domain" && configuration.Mode != "native-gravity-domain" && configuration.Mode != "native-liquid-gravity-domain" && configuration.Mode != "native-checkpoint-domain" && configuration.Mode != "native-paired-checkpoint-domain" && configuration.Mode != "native-save-domain" && configuration.Mode != "native-load-domain" && configuration.Mode != "presentation" && configuration.Mode != "tile-hover" && configuration.Mode != "coupled-presentation" && configuration.Mode != "load-rejection") || configuration.CompletedFrames < 1 ||
                configuration.CompletedFrames > 10000 || configuration.TimeoutSeconds < 30 || configuration.TimeoutSeconds > 7200)
                throw new InvalidDataException("Unsupported colony validation mode or limits.");
            if ((configuration.Mode == "native-carbon-particle-domain" || configuration.Mode == "native-breathing-read-domain") && configuration.CompletedFrames < 30)
                throw new InvalidDataException("Native CO2 particle and breathing-reader controls require at least 30 frames.");
            if ((configuration.Mode == "native-actor-domain" || configuration.Mode == "native-liquid-actor-domain") && configuration.CompletedFrames < NativeSealedDomainValidation.MinimumActorValidationFrames)
                throw new InvalidDataException("The registered actor scene requires at least 80 native frames.");
            if (NativeSealedDomainValidation.ColonyRestoreMode != !string.IsNullOrEmpty(configuration.LoadSavePath) ||
                (NativeSealedDomainValidation.ColonyRestoreMode && (!Path.IsPathRooted(configuration.LoadSavePath) ||
                    !File.Exists(configuration.LoadSavePath) || configuration.ReuseGeneratedWorld)))
                throw new InvalidDataException("Paired colony loading requires its explicit mode, an existing absolute save path and the normal saved-colony lifecycle.");
            if ((configuration.Mode == "native-observer" || NativeSealedDomainValidation.IsMode) && !NativeBackendObservation.Enabled)
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
            if (configuration.SaveOnComplete && (!NativeSealedDomainValidation.IsMode || NativeSealedDomainValidation.ColonyRestoreMode))
                throw new InvalidDataException("SaveOnComplete requires an initial native domain validation scene.");
            if (configuration.SaveBeforeResume && !NativeSealedDomainValidation.ColonyRestoreMode)
                throw new InvalidDataException("SaveBeforeResume requires an admitted saved-colony validation scene.");
            if (configuration.VerifySaveIoRecovery && (configuration.Mode!="native-save-domain" || configuration.CompletedFrames<16))
                throw new InvalidDataException("Save IO recovery requires the explicit source-save validation scene and at least sixteen frames.");
            if(configuration.ReloadInProcess && (!NativeSealedDomainValidation.ColonyRestoreMode || !configuration.SaveBeforeResume))
                throw new InvalidDataException("In-process reload requires a saved-colony load and the paused initial autosave control.");
            Directory.CreateDirectory(OutputDirectory);
            if (NativeSealedDomainValidation.IsMode)
            {
                Report.SnapshotCopiesIncludeCustomProjections = true;
                Report.AllCopiesByteIdentical = false;
            }
            WriteReport();
            Debug.Log("[Mixed Atmospheres] Explicit colony validation enabled: " + configuration.Mode +
                (NativeSealedDomainValidation.IsMode ? ". Experimental sealed-domain ownership checks are required." : ". Colony physics remains native."));
        }

        internal static bool ObserveMessage => configuration != null && !done;

        internal static IntPtr AfterNativeMessage(int message, IntPtr result)
        {
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
            if(driver!=null) {driver.Initialize(menu);return;}
            var runner = new GameObject("Mixed Atmospheres private colony validation");
            UnityEngine.Object.DontDestroyOnLoad(runner);
            driver=runner.AddComponent<ColonyValidationDriver>();driver.Initialize(menu);
        }

        internal static void UpdateColony()
        {
            if (configuration == null || done) return;
            if (configuration.Mode == "load-rejection")
            {
                try { OwnedReservoirLoadVisualValidation.Tick(); }
                catch (Exception error) { OwnedReservoirLoadVisualValidation.RecordFailure(error); Fail(error); }
                return;
            }
            if (Game.Instance == null) return;
            Report.GameStarted = Game.Instance.GameStarted();
            if (!Report.GameStarted) return;
            if (NativeSealedDomainValidation.IsMode)
            {
                NativeSealedDomainValidation.Tick(); return;
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
                    Report.ObserverPhysicalUpdates < (ulong)configuration.CompletedFrames)) { WriteReport(); return; }
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
                LoadingOverlay.Load(PauseScreen.TriggerQuitGame);return;
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
        private static void WriteReport() => File.WriteAllText(ReportPath, ReportJson.Serialize(Report));
        internal static void Shutdown()
        {
            actualColonyActive = false; publisher?.Dispose(); publisher = null;
            if (NativeSealedDomainValidation.IsMode) NativeSealedDomainValidation.Shutdown();
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
            foreach(var label in GetComponents<NativeSealedDomainLabel>()) UnityEngine.Object.Destroy(label);
            menu=mainMenu;started=Time.realtimeSinceStartup;nextAction=started+3;
            completionWaitFrames=0;minionsReadyAt=-1;visited.Clear();
        }
        private void Update()
        {
            var config = ColonyRuntimeValidation.Settings;
            if (config == null) return;
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
                    if (config.Mode == "native-load-domain")
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
