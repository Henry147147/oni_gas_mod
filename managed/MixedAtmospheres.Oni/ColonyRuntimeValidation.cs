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
            public bool AutoStartNewColony = false;
            public bool ReuseGeneratedWorld = false;
            public bool QuitWhenComplete = false;
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
            public int SnapshotChannels, EventChannels, Duplicants;
            public string[] ActiveDlcIds = Array.Empty<string>();
            public string[] ImportedMaterialIds = Array.Empty<string>();
            public string MaterialModel = "explicit local ONI constant-property surrogates; not a thermodynamic catalogue";
            public string MaterialFingerprint = "";
            public string StartedUtc = System.DateTime.UtcNow.ToString("O");
            public string FinishedUtc = "";
        }

        private static Configuration? configuration;
        private static readonly ValidationReport Report = new ValidationReport();
        private static OwnedSnapshotPublisher? publisher;
        private static bool inNewColonyLoad;
        private static bool actualColonyActive;
        private static bool done;
        private static int liveFrameBaseline = -1;
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
            if ((configuration.Mode != "snapshot-copy" && configuration.Mode != "presentation") || configuration.CompletedFrames < 1 ||
                configuration.CompletedFrames > 10000 || configuration.TimeoutSeconds < 30 || configuration.TimeoutSeconds > 7200)
                throw new InvalidDataException("Unsupported colony validation mode or limits.");
            Directory.CreateDirectory(OutputDirectory);
            WriteReport();
            Debug.Log("[Mixed Atmospheres] Explicit colony validation enabled: " + configuration.Mode + ". Colony physics remains native.");
        }

        internal static bool ObserveMessage => configuration != null && !done;

        internal static IntPtr AfterNativeMessage(int message, IntPtr result)
        {
            if (configuration?.Mode != "snapshot-copy" || !ObserveMessage || !actualColonyActive || result == IntPtr.Zero ||
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
        internal static void EndNewColony() { inNewColonyLoad = false; }

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

        internal static void OnMainMenu(MainMenu menu)
        {
            if (configuration == null || done) return;
            var runner = new GameObject("Mixed Atmospheres private colony validation");
            UnityEngine.Object.DontDestroyOnLoad(runner);
            runner.AddComponent<ColonyValidationDriver>().Initialize(menu);
        }

        internal static void UpdateColony()
        {
            if (configuration == null || done || Game.Instance == null) return;
            Report.GameStarted = Game.Instance.GameStarted();
            if (!Report.GameStarted) return;
            if (configuration.Mode == "presentation")
            {
                try { MixtureVisualValidation.Tick(); }
                catch (Exception error) { Fail(error); }
                return;
            }
            if (liveFrameBaseline < 0)
            {
                liveFrameBaseline = Report.NativeFramesAcknowledged;
                SetStage("running-disposable-colony");
            }
            Report.Duplicants = Components.LiveMinionIdentities.Count;
            if (Report.NativeFramesAcknowledged - liveFrameBaseline < configuration.CompletedFrames) return;
            Report.Status = Report.NewColonyStagingPassed && Report.CustomStateRoundtripPassed && Report.AllCopiesByteIdentical
                ? "passed" : "failed";
            if (Report.Status == "failed") Report.Error = "Required new-colony staging or roundtrip evidence is missing.";
            Report.ActiveDlcIds = SaveLoader.Instance.GameInfo.dlcIds.ToArray();
            Report.Stage = "completed";
            Report.FinishedUtc = System.DateTime.UtcNow.ToString("O");
            done = true;
            WriteReport();
            CaptureScreenshot("colony.png");
        }

        internal static void SetStage(string stage) { Report.Stage = stage; WriteReport(); }
        internal static void CompletePresentation()
        {
            Report.Kind = "actual-game-mixture-presentation-fixture-validation";
            Report.Status = "passed";
            Report.GameStarted = true;
            Report.Duplicants = Components.LiveMinionIdentities.Count;
            Report.Width = Grid.WidthInCells; Report.Height = Grid.HeightInCells;
            Report.WorldCount = ClusterManager.Instance.WorldContainers.Count;
            Report.ActiveDlcIds = SaveLoader.Instance.GameInfo.dlcIds.ToArray();
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
            if (done) return;
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
                new object[] { Path.Combine(OutputDirectory, filename), 1 });
        }
        private static void WriteReport() => File.WriteAllText(ReportPath, ReportJson.Serialize(Report));
        internal static void Shutdown() { actualColonyActive = false; publisher?.Dispose(); publisher = null; }
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
        internal void Initialize(MainMenu mainMenu) { menu = mainMenu; started = Time.realtimeSinceStartup; nextAction = started + 3; }
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
                    if (config.ReuseGeneratedWorld)
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
        private static Exception? Finalizer(Exception? __exception) { ColonyRuntimeValidation.EndNewColony(); return __exception; }
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
