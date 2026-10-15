using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using HarmonyLib;
using Klei;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // A separately opted-in disposable native colony experiment. No ownership
    // session, owner assignment, mixture registration or owned restore exists
    // here. The only produced reserved header is deliberately invalid schema99.
    internal static class OwnedReservoirLoadVisualValidation
    {
        private sealed class Packet
        {
            public int Element, MassBits, TemperatureBits, DiseaseCount;
            public float MassKg, TemperatureK;
            public byte DiseaseIndex;
            public string DiseaseId = "";
        }
        private static readonly List<object> evidence = new List<object>();
        private static int phase, phaseFrame, stableMenuFrames, anchor, savedId, width, height, worlds;
        private static int normalAnimationReadyFrame = -1;
        private static float next, menuReadyAt;
        private static MainMenu? menu;
        private static Game? oldGame;
        private static SaveLoader? oldLoader;
        private static GameObject? reservoir;
        private static Storage? storage;
        private static Packet? savedPacket, serializedPacket;
        private static int serializedCount, directoryEnd;
        private static string ordinaryPath = "", malformedPath = "", failedBodyPath = "", ordinaryHash = "", malformedHash = "", failedBodyHash = "";
        private static long ordinaryLength, malformedLength, failedBodyLength;
        private static long emptyAnimationGuardBaseline;
        private static string directoryObservationError = "";
        private static IReadOnlyDictionary<string, string>? rejectionFileInventory;
        private static bool finished, completedSuccessfully, failureRecorded, nativeErrorObserved;
        private static bool contractRecorded;
        private static OwnedReservoirLoadCounterSnapshot? baseline;
        private static OwnedReservoirLoadCounterSnapshot? normalAnimationBaseline;
        internal static bool Started { get; private set; }
        private static string ReportPath => Path.Combine(ColonyRuntimeValidation.OutputDirectory, "owned-load-rejection.json");

        internal static void Install(Harmony harmony)
        {
            var settings = ColonyRuntimeValidation.Settings;
            Require(settings?.Mode == "load-rejection" && settings.PrivateOwnedLoadRejection,
                "Load rejection hooks require the explicit private experiment.");
            var preflight = OwnedReservoirLoadValidationCounters.InspectContracts();
            Require(preflight.ExpectedMethodCount == 45 && preflight.ExpectedNativeSiteSpecifications == 12 &&
                preflight.ExpectedNativeMapKeyCount == 15 && preflight.ExpectedManagedAntecedentKeyCount == 4 && preflight.InitializationCallerKeys.Count == 4,
                "Private load instrumentation preflight has an incomplete original-IL contract.");
            evidence.Add(new { Kind = "pure-original-il-preflight-before-private-hooks", Contract = preflight });
            Write("pending");
            var resume = AccessTools.DeclaredMethod(typeof(MainMenu), "ResumeGame", Type.EmptyTypes);
            Require(resume != null && !resume.IsStatic && resume.ReturnType == typeof(void) && resume.GetMethodBody() != null,
                "The private no-Resume guard requires the pinned menu contract.");
            harmony.Patch(resume, prefix: new HarmonyMethod(typeof(OwnedReservoirLoadVisualValidation), nameof(BlockAutomaticResume)) { priority = Priority.First });
            OwnedReservoirLoadBoundary.Install(harmony);
            OwnedReservoirLoadValidationCounters.Install(harmony);
            OwnedReservoirLoadValidationCounters.StorageSerializeBodyObserved += ObserveSerialize;
            var quit = AccessTools.DeclaredMethod(typeof(App), "QuitCode", new[] { typeof(int) });
            Require(quit != null && quit.IsStatic && quit.ReturnType == typeof(void) && quit.GetMethodBody() != null,
                "Private quit observation requires the pinned App.QuitCode contract.");
            harmony.Patch(quit, prefix: new HarmonyMethod(typeof(OwnedReservoirLoadVisualValidation), nameof(ObserveQuitCode)) { priority = Priority.First });
            Application.quitting += ObserveApplicationQuitting;
            Application.logMessageReceived += ObserveNativeError;
            var directory = AccessTools.DeclaredMethod(typeof(KSerialization.Manager), "DeserializeDirectory", new[] { typeof(IReader) });
            Require(directory != null && directory.ReturnType == typeof(void) && directory.IsStatic && directory.GetMethodBody() != null,
                "Private directory observation requires the exact native managed contract.");
            harmony.Patch(directory, postfix: new HarmonyMethod(typeof(OwnedReservoirLoadVisualValidation), nameof(ObserveDirectoryReturn)) { priority = Priority.Last });
            evidence.Add(new { Kind = "private-rejection-only-installed", OwnedReservoirLoadBoundary.RejectionOnly,
                OwnedReservoirLoadBoundary.Installed, NativePhysics = true, OwnedRestorePermitted = false });
            Write("pending");
        }

        private static bool BlockAutomaticResume()
        {
            var error = new InvalidOperationException("The private load experiment blocked an unexpected automatic Resume attempt.");
            RecordFailure(error); ColonyRuntimeValidation.Fail(error);
            return false;
        }

        internal static bool ObserveMainMenu(MainMenu current)
        {
            menu = current;
            stableMenuFrames = 0; menuReadyAt = 0;
            evidence.Add(new { Kind = "main-menu-observed", Phase = phase, Frame = Time.frameCount, Id = current.GetInstanceID(),
                GenericGameSettings.instance.devBootSmoke, GenericGameSettings.instance.devBootModReport,
                GenericGameSettings.instance.autoResumeGame, ScriptedFrameCount = GenericGameSettings.instance.scriptedProfile.frameCount,
                ColonyRuntimeValidation.Settings!.QuitWhenComplete });
            Write("pending");
            return Started;
        }

        internal static void Tick()
        {
            if (finished) return;
            if (!Started)
            {
                if (Game.Instance == null || !Game.Instance.GameStarted()) return;
                // Wait for the native initial-save coroutine to finish before
                // creating or measuring the separate ordinary checkpoint.
                var initial = SaveLoader.GetActiveSaveFilePath();
                if (string.IsNullOrEmpty(initial) || !File.Exists(initial)) return;
                Started = true; width = Grid.WidthInCells; height = Grid.HeightInCells;
                worlds = ClusterManager.Instance.WorldContainers.Count;
                SpeedControlScreen.Instance.Pause(false);
                Require(SpeedControlScreen.Instance.IsPaused, "Native colony did not pause.");
                var folder = Path.GetFullPath(Path.GetDirectoryName(initial)!);
                var parent = Path.GetFullPath(SaveLoader.GetSavePrefix()).TrimEnd(Path.DirectorySeparatorChar);
                Require(Path.GetDirectoryName(folder) == parent && Path.GetFileName(folder) == ColonyRuntimeValidation.Settings!.ColonyName,
                    "Only the fresh disposable colony directory may receive test saves.");
                ordinaryPath = Path.Combine(folder, "ordinary-unmarked.sav");
                malformedPath = Path.Combine(folder, "rejected-header.sav");
                failedBodyPath = Path.Combine(folder, "caught-native-failure.sav");
                Require(!File.Exists(ordinaryPath) && !File.Exists(malformedPath) && !File.Exists(failedBodyPath), "Test checkpoint paths must be fresh.");
                SpawnReservoir();
                Stage(1, "waiting-for-unowned-reservoir", 2);
                return;
            }
            if (Time.realtimeSinceStartup < next) return;
            switch (phase)
            {
                case 1:
                    Require(storage != null && reservoir != null, "Native reservoir disappeared.");
                    if (!Spawned(reservoir!)) return;
                    RequireUnowned(storage!);
                    Require(storage!.items.Count == 0, "Ordinary fixture must begin with empty native storage.");
                    Require(Db.Get().Diseases.resources.Count > 0, "No native disease table entry for the preservation test.");
                    var disease = Db.Get().Diseases.resources[0];
                    var index = Db.Get().Diseases.GetIndex(disease.id);
                    Require(index != byte.MaxValue && Db.Get().Diseases[index].id == disease.id, "Native disease index did not roundtrip.");
                    storage.AddGasChunk(SimHashes.Oxygen, 12.5f, 310f, index, 12345, false, false);
                    Stage(2, "native-unowned-packet-created", 1);
                    break;
                case 2:
                    RequireUnowned(storage!); savedPacket = ReadPacket(storage!);
                    Require(savedPacket.MassBits == Bits(12.5f) && savedPacket.TemperatureBits == Bits(310f) && savedPacket.DiseaseCount == 12345,
                        "Observed native packet differs before any save.");
                    savedId = reservoir!.GetComponent<KPrefabID>().InstanceID;
                    Require(savedId >= 0, "Native fixture did not receive a saved instance ID.");
                    FocusAndSelect(reservoir);
                    evidence.Add(new { Kind = "unowned-packet-before-save", savedId, Anchor = anchor, Packet = savedPacket,
                        OwnedGasReservoirBridge.BindingCount, Paused = SpeedControlScreen.Instance.IsPaused });
                    ColonyRuntimeValidation.CaptureScreenshot("01-unowned-native-reservoir.png");
                    Stage(3, "saving-native-unmarked-control", 1);
                    break;
                case 3:
                    Require(SpeedControlScreen.Instance.IsPaused, "Native simulation resumed before saving.");
                    baseline = CaptureCounters();
                    var returned = SaveLoader.Instance.Save(ordinaryPath, false, false);
                    Require(returned == ordinaryPath && File.Exists(ordinaryPath), "Native save did not create the exact fresh checkpoint.");
                    Require(serializedCount == 1 && serializedPacket != null && Equal(savedPacket!, serializedPacket),
                        "The real native Storage.Serialize did not preserve the observed packet exactly once.");
                    savedPacket = serializedPacket;
                    ordinaryLength = new FileInfo(ordinaryPath).Length; ordinaryHash = HashFile(ordinaryPath);
                    Require(OwnedReservoirLoadHeaderV1.InspectFile(ordinaryPath).Kind == OwnedReservoirLoadInputKind.Unmarked,
                        "The native positive control is not an ordinary unmarked save.");
                    var saveDelta = CounterDelta();
                    RequireBody(saveDelta, typeof(SaveLoader), "Save", typeof(string), typeof(bool), typeof(bool));
                    RequireBody(saveDelta, typeof(SaveLoader), "Save", typeof(BinaryWriter));
                    RequireBody(saveDelta, typeof(Sim), "Save", typeof(BinaryWriter), typeof(int), typeof(int));
                    RequireBody(saveDelta, typeof(Game), "Save", typeof(BinaryWriter));
                    RequireBody(saveDelta, typeof(SaveManager), "Save", typeof(BinaryWriter));
                    Require(NativeTotal(saveDelta, "SIM_BeginSave(") > 0 && NativeTotal(saveDelta, "SIM_EndSave(") > 0,
                        "Positive native save dispatch was not observed.");
                    evidence.Add(new { Kind = "ordinary-native-save", Passed = true, Path = ordinaryPath, ordinaryLength, ordinaryHash,
                        Packet = serializedPacket, Counters = saveDelta });
                    CreateMalformedCopy(); VerifyFiles();
                    oldGame = Game.Instance; oldLoader = SaveLoader.Instance;
                    Stage(4, "returning-to-menu-before-control-load", 0);
                    PauseScreen.TriggerQuitGame();
                    break;
                case 4:
                    if (!StableFrontend()) return;
                    VerifyFiles();
                    baseline = CaptureCounters();
                    Stage(5, "loading-exact-native-control", 0);
                    LoadingOverlay.Load(() => LoadScreen.DoLoad(ordinaryPath));
                    break;
                case 5:
                    if (Game.Instance == null || !Game.Instance.GameStarted() || SaveLoader.Instance == null || !SaveLoader.Instance.loadedFromSave) return;
                    Require(!OwnedReservoirLoadBoundary.IsFatal, "Ordinary unmarked native load was incorrectly rejected.");
                    SpeedControlScreen.Instance.Pause(false);
                    if (normalAnimationReadyFrame < 0)
                    {
                        normalAnimationBaseline = CaptureCounters();
                        normalAnimationReadyFrame = Time.frameCount;
                        next = Time.realtimeSinceStartup + 0.25f;
                        return;
                    }
                    var matches = UnityEngine.Object.FindObjectsByType<KPrefabID>(FindObjectsSortMode.None)
                        .Where(value => value.InstanceID == savedId && value.PrefabTag.Name == GasReservoirConfig.ID).ToArray();
                    Require(matches.Length == 1 && !ReferenceEquals(matches[0].gameObject, reservoir), "Native save did not reconstruct the exact ordinary owner.");
                    if (!Spawned(matches[0].gameObject)) return;
                    reservoir = matches[0].gameObject; storage = reservoir.GetComponent<Storage>();
                    Require(Grid.PosToCell(reservoir.transform.GetPosition()) == anchor, "Reconstructed ordinary owner moved to another anchor.");
                    RequireUnowned(storage);
                    var afterLoad = ReadPacket(storage);
                    var loadDelta = CounterDelta();
                    RequireBody(loadDelta, typeof(SaveLoader), "Load", typeof(string));
                    RequireBody(loadDelta, typeof(SaveLoader), "Load", typeof(IReader));
                    RequireBody(loadDelta, typeof(Sim), "LoadWorld", typeof(IReader));
                    RequireBody(loadDelta, typeof(Sim), "Start");
                    RequireBody(loadDelta, typeof(SaveManager), "Load", typeof(IReader));
                    RequireBody(loadDelta, typeof(SaveLoadRoot), "Load", typeof(GameObject), typeof(Vector3), typeof(Quaternion), typeof(Vector3), typeof(IReader));
                    RequireBody(loadDelta, typeof(Storage), "Deserialize", typeof(IReader));
                    RequireBody(loadDelta, typeof(SaveManager), "ClearScene");
                    var storeKey = OwnedReservoirLoadValidationCounters.Key(typeof(Storage), "Store", typeof(GameObject), typeof(bool), typeof(bool), typeof(bool), typeof(bool));
                    Require(loadDelta.Counts.TryGetValue(storeKey, out var stores) && stores.OriginalDeserializingBodyEntries > 0,
                        "Positive control did not observe real native deserializing Storage.Store.");
                    Require(NativeTotal(loadDelta, "SIM_Initialize(") > 0 && NativeLoadDispatches(loadDelta) > 0 && NativeStartDispatches(loadDelta) > 0,
                        "Positive native initialization/load/start dispatch was not observed.");
                    var animationKey = OwnedReservoirLoadValidationCounters.Key(typeof(KBatchedAnimUpdater), "DoGridProcessing");
                    var normalAnimationDelta = OwnedReservoirLoadValidationCounters.Delta(normalAnimationBaseline!, CaptureCounters());
                    Require(normalAnimationDelta.CompleteContractVerified && normalAnimationDelta.ObservationFailures == 0 &&
                        normalAnimationDelta.ObserverErrorCount == 0, "Ordinary animation control lost its complete counter contract.");
                    Require(normalAnimationDelta.Counts.TryGetValue(animationKey, out var normalAnimation) && normalAnimation.OriginalBodyEntries > 0 &&
                        normalAnimation.OriginalBodyReturns > 0 && normalAnimation.FinalizerExceptions == 0,
                        "Ordinary native animation grid processing did not execute and return through its actual body.");
                    var normalUpdater = Singleton<KBatchedAnimUpdater>.Instance;
                    var normalAnimationGrid = AccessTools.DeclaredField(typeof(KBatchedAnimUpdater), "controllerGrid")?.GetValue(normalUpdater) as Array;
                    Require(normalUpdater != null && normalAnimationGrid != null && normalAnimationGrid.Rank == 2 &&
                        normalAnimationGrid.GetLength(0) > 0 && normalAnimationGrid.GetLength(1) > 0 &&
                        Camera.main != null && Time.frameCount > normalAnimationReadyFrame &&
                        OwnedReservoirLoadBoundary.EmptyAnimationGridSuppressionCount == 0,
                        "Ordinary animation control lacks its positive native grid or was suppressed outside a failed attempt.");
                    evidence.Add(new { Kind = "ordinary-native-animation-control", Passed = true, Counters = normalAnimation,
                        GridRows = normalAnimationGrid.GetLength(0), GridColumns = normalAnimationGrid.GetLength(1),
                        ObservedFramesAfterGameStarted = Time.frameCount - normalAnimationReadyFrame,
                        OwnedReservoirLoadBoundary.EmptyAnimationGridSuppressionCount });
                    evidence.Add(new { Kind = "ordinary-native-load", Passed = Equal(savedPacket!, afterLoad), savedId, Anchor = anchor,
                        Expected = savedPacket, Actual = afterLoad, Counters = loadDelta, Paused = SpeedControlScreen.Instance.IsPaused });
                    Write("pending");
                    Require(Equal(savedPacket!, afterLoad), "Native packet mass/temperature/disease changed across real menu/save/reload.");
                    Require(directoryObservationError.Length == 0, "Private native-directory observation failed: " + directoryObservationError);
                    CreateFailedCompressionCopy(); VerifyFiles(); FocusAndSelect(reservoir);
                    ColonyRuntimeValidation.CaptureScreenshot("02-unowned-reservoir-after-real-reload.png");
                    Stage(6, "ready-for-direct-rejection", 2);
                    break;
                case 6:
                    oldGame = Game.Instance; oldLoader = SaveLoader.Instance;
                    FreezeRejectionFiles("direct-selected-header");
                    baseline = CaptureCounters();
                    var beforePacket = ReadPacket(storage!);
                    var beforeGrid = new { Grid.WidthInCells, Grid.HeightInCells, Grid.CellCount };
                    Stage(7, "direct-selected-header-rejection", 0);
                    var result = oldLoader!.Load(malformedPath);
                    var immediate = CounterDelta();
                    var attempt = OwnedReservoirLoadBoundary.CurrentAttempt ?? OwnedReservoirLoadBoundary.LastAttempt;
                    var retirement = OwnedReservoirLoadBoundary.LastNativeViewRetirement;
                    var teardownAlreadyStarted = attempt?.TeardownStarted == true;
                    Packet? immediatePacket = null;
                    if (!teardownAlreadyStarted)
                    {
                        Require(oldGame != null && oldLoader != null && storage != null && ReferenceEquals(Game.Instance, oldGame),
                            "Old native scene disappeared without matching teardown observation.");
                        immediatePacket = ReadPacket(storage!);
                    }
                    evidence.Add(new { Kind = "direct-rejection-return", Result = result, BeforeGrid = beforeGrid,
                        AfterGrid = new { Grid.WidthInCells, Grid.HeightInCells, Grid.CellCount }, PacketBefore = beforePacket,
                        PacketAfter = immediatePacket, TeardownAlreadyStarted = teardownAlreadyStarted,
                        PacketComparisonScope = teardownAlreadyStarted ? "Native teardown already began; destroyed objects are not inspected." : "Exact native stored packet after managed view retirement, before actual teardown.",
                        NativeViewRetirement = DescribeRetirement(retirement, attempt),
                        Counters = immediate, Attempt = DescribeAttempt(attempt),
                        ActivePath = SaveLoader.GetActiveSaveFilePath(), Loaded = oldLoader == null ? (bool?)null : oldLoader.loadedFromSave,
                        OldOwnerIsLoading = teardownAlreadyStarted || oldGame == null ? (bool?)null : oldGame.IsLoading() });
                    Write("pending");
                    Require(!result && attempt != null && attempt.Input.ReservedNamespaceObserved && attempt.Input.SelectedPath == malformedPath &&
                        attempt.FrontendRequested && !attempt.SelectedCallPending && !attempt.TeardownFailed,
                        "Direct selected rejection did not latch and unwind the exact fatal call.");
                    Require(SaveLoader.GetActiveSaveFilePath() == null && (oldLoader == null || !oldLoader.loadedFromSave),
                        "Direct rejection retained a save selection or native success flag.");
                    Require(retirement != null && retirement.Retired && retirement.AttemptMatches && retirement.OwnerMatches &&
                        ReferenceEquals(retirement.AttemptIdentity, attempt.Identity) && retirement.AttemptId == attempt.Id &&
                        ReferenceEquals(retirement.ExpectedOwner, oldGame) && ReferenceEquals(retirement.ObservedOwner, oldGame) &&
                        retirement.ActivationThreadId == retirement.ObservedThreadId &&
                        retirement.PriorCellCount == beforeGrid.CellCount && beforeGrid.CellCount == width * height &&
                        retirement.PriorWidth == beforeGrid.WidthInCells && retirement.PriorHeight == beforeGrid.HeightInCells &&
                        retirement.CellCount == 0 && retirement.WidthInCells == width && retirement.HeightInCells == height &&
                        attempt.Observations.Any(row => row.Boundary == "native-view-retired"),
                        "Direct rejection did not explicitly retire the exact failed owner's managed view before requesting frontend.");
                    if (!teardownAlreadyStarted)
                        Require(immediatePacket != null && Equal(beforePacket, immediatePacket) &&
                            Grid.WidthInCells == width && Grid.HeightInCells == height && Grid.CellCount == 0 && oldGame!.IsLoading(),
                            "Direct rejection changed the native stored packet/dimensions or re-exposed retired cells before actual teardown.");
                    RequireRejectedBodiesZero(immediate, early: false);
                    VerifyFiles();
                    break;
                case 7:
                    if (!StableFrontend()) return;
                    VerifyRejectedEpoch("direct-selected-header", early: false, expectedSequence: 1);
                    ColonyRuntimeValidation.CaptureScreenshot("03-direct-rejection-completed-frontend.png");
                    Stage(8, "ready-for-early-onspawn-rejection", 2);
                    break;
                case 8:
                    VerifyFiles(); FreezeRejectionFiles("early-backend-onspawn"); baseline = CaptureCounters();
                    emptyAnimationGuardBaseline = OwnedReservoirLoadBoundary.EmptyAnimationGridSuppressionCount;
                    menu = null; stableMenuFrames = 0; menuReadyAt = 0;
                    Stage(9, "early-onspawn-header-rejection", 0);
                    LoadingOverlay.Load(() => LoadScreen.DoLoad(malformedPath));
                    break;
                case 9:
                    if (!StableFrontend()) return;
                    VerifyRejectedEpoch("early-backend-onspawn", early: true, expectedSequence: 2);
                    ColonyRuntimeValidation.CaptureScreenshot("04-early-rejection-completed-frontend.png");
                    Stage(10, "ready-for-caught-native-failure", 2);
                    break;
                case 10:
                    VerifyFiles(); FreezeRejectionFiles("caught-native-false"); baseline = CaptureCounters();
                    emptyAnimationGuardBaseline = OwnedReservoirLoadBoundary.EmptyAnimationGridSuppressionCount;
                    menu = null; stableMenuFrames = 0; menuReadyAt = 0;
                    Stage(11, "caught-native-load-failure", 0);
                    LoadingOverlay.Load(() => LoadScreen.DoLoad(failedBodyPath));
                    break;
                case 11:
                    if (!StableFrontend()) return;
                    VerifyCaughtFalseEpoch();
                    ColonyRuntimeValidation.CaptureScreenshot("05-caught-false-load-completed-frontend.png");
                    Stage(12, "completed-load-rejection-checks", 1);
                    break;
                case 12:
                    VerifyFiles();
                    ColonyRuntimeValidation.CompleteLoadRejection(width, height, worlds);
                    completedSuccessfully = true; finished = true; Write("passed");
                    break;
            }
        }

        private static void Stage(int value, string text, float delay)
        {
            phase = value; phaseFrame = Time.frameCount; next = Time.realtimeSinceStartup + delay;
            evidence.Add(new { Kind = "stage", Stage = text, Phase = phase, Frame = phaseFrame, Utc = System.DateTime.UtcNow.ToString("O") });
            ColonyRuntimeValidation.SetStage(text); Write("pending");
        }
        private static bool StableFrontend()
        {
            var ready = menu != null && menu.isSpawned && menu.gameObject.activeInHierarchy &&
                App.GetCurrentSceneName() == "frontend" && !App.isLoading &&
                UnityEngine.SceneManagement.SceneManager.GetActiveScene().name == "frontend" &&
                Game.Instance == null && SaveLoader.Instance == null && Grid.CellCount == 0 &&
                oldGame == null && oldLoader == null && Time.frameCount > phaseFrame;
            if (!ready) { stableMenuFrames = 0; menuReadyAt = 0; return false; }
            if (stableMenuFrames++ == 0) menuReadyAt = Time.realtimeSinceStartup;
            return stableMenuFrames >= 3 && Time.realtimeSinceStartup - menuReadyAt >= 2;
        }
        private static void VerifyRejectedEpoch(string name, bool early, long expectedSequence)
        {
            VerifyFiles(); VerifyRejectionFiles();
            var delta = CounterDelta();
            var attempt = OwnedReservoirLoadBoundary.LastAttempt;
            evidence.Add(new { Kind = "rejection-epoch-observed", Name = name, Counters = delta, Attempt = DescribeAttempt(attempt),
                FatalStillActive = OwnedReservoirLoadBoundary.IsFatal, Completed = OwnedReservoirLoadBoundary.CompletedAttempts.Count,
                ActivePath = SaveLoader.GetActiveSaveFilePath(), Scene = App.GetCurrentSceneName(), NativeGridCells = Grid.CellCount,
                StableFrontendFrames = stableMenuFrames, StableFrontendSeconds = Time.realtimeSinceStartup - menuReadyAt,
                OwnedGasReservoirBridge.BindingCount, ordinaryHash, malformedHash });
            Write("pending");
            Require(attempt != null && attempt.Sequence == expectedSequence && attempt.Input.SelectedPath == malformedPath &&
                attempt.Input.ReservedNamespaceObserved && attempt.FrontendRequested && attempt.TeardownStarted && attempt.TeardownCompleted &&
                !attempt.TeardownFailed && !attempt.SelectedCallPending && !attempt.OnSpawnCallPending && !OwnedReservoirLoadBoundary.IsFatal &&
                OwnedReservoirLoadBoundary.CompletedAttempts.Count == expectedSequence,
                "Actual failed load did not complete its held native scene teardown.");
            Require(SaveLoader.GetActiveSaveFilePath() == null && OwnedGasReservoirBridge.BindingCount == 0,
                "Completed rejection retained a save selection or acquired fluid ownership.");
            Require(attempt.CompletionKind == (early ? OwnedReservoirLoadCompletionKind.PreGameLoaderScene : OwnedReservoirLoadCompletionKind.ActualGameDestroyInstances),
                "Rejected load completed through the wrong native lifecycle kind.");
            if (early) VerifyPreGameCompletion(attempt);
            Require(NativeTotal(delta, "SIM_Shutdown(") == (early ? 0 : 1),
                "The rejection epoch performed an unexpected native shutdown count.");
            RequireRejectedBodiesZero(delta, early);
            RequireCleanTeardownAndFrontendRequest(delta, preGame: early);
            evidence.Add(new { Kind = "rejection-epoch-passed", Name = name, Sequence = expectedSequence });
            Write("pending");
        }

        private static void SpawnReservoir()
        {
            var definition = Assets.GetBuildingDef(GasReservoirConfig.ID);
            Require(definition != null && definition.BuildingComplete != null, "Native GasReservoir definition is unavailable.");
            var offsets = definition.PlacementOffsets.Concat(new[] { definition.UtilityInputOffset, definition.UtilityOutputOffset }).Distinct().ToArray();
            anchor = -1;
            var focus = CameraController.Instance.transform.GetPosition();
            var candidates = Enumerable.Range(0, Grid.CellCount).Where(cell => Grid.Visible[cell] > 0 && Grid.IsValidBuildingCell(cell) && Grid.IsActiveWorld(cell))
                .OrderBy(cell => (Grid.CellToPosCCC(cell, Grid.SceneLayer.BuildingFront) - focus).sqrMagnitude);
            foreach (var cell in candidates)
                {
                    var x = cell % Grid.WidthInCells; var y = cell / Grid.WidthInCells;
                    if (x < 3 || x >= Grid.WidthInCells - 3 || y < 1 || y >= Grid.HeightInCells - 4) continue;
                    var clear = offsets.All(offset =>
                    {
                        var other = Grid.XYToCell(x + offset.x, y + offset.y);
                        if (!Grid.IsValidBuildingCell(other) || !Grid.IsActiveWorld(other) || Grid.Solid[other]) return false;
                        for (var layer = 0; layer < (int)ObjectLayer.NumLayers; ++layer) if (Grid.Objects[other, layer] != null) return false;
                        return true;
                    });
                    if (clear && definition.IsValidBuildLocation(definition.BuildingComplete, cell, Orientation.Neutral, false, out _)) { anchor = cell; break; }
                }
            Require(anchor >= 0, "No clear native reservoir footprint with valid support was found in the disposable colony.");
            reservoir = definition.Build(anchor, Orientation.Neutral, null, definition.DefaultElements(), 293.15f, playsound: false);
            Require(reservoir != null, "Native factory failed to build the ordinary reservoir.");
            storage = reservoir!.GetComponent<Storage>();
            Require(storage != null, "Native reservoir has no Storage component.");
            evidence.Add(new { Kind = "ordinary-native-reservoir-created", Anchor = anchor, World = (int)Grid.WorldIdx[anchor],
                Footprint = offsets.Select(offset => Grid.OffsetCell(anchor, offset)).ToArray(), Factory = "BuildingDef.Build", NoObjectsRemoved = true });
        }
        private static bool Spawned(GameObject value) => value.activeInHierarchy &&
            value.GetComponents<KMonoBehaviour>().Where(component => component.enabled).All(component => component.isSpawned);
        private static void RequireUnowned(Storage value)
        {
            Require(value != null && value.isSpawned && OwnedGasReservoirBridge.BindingCount == 0 && !OwnedGasReservoirBridge.SaveIsBlocked,
                "Ordinary native storage is not spawned or a fluid ownership/save lease is active.");
            Require(value.GetComponent<OwnedReservoirOwnerMarker>()?.ReadRecord().State == OwnedReservoirMarkerState.Unassigned,
                "Ordinary native reservoir does not have its inert unassigned marker.");
            Require(!value.GetComponent<ConduitConsumer>().IsConnected && !value.GetComponent<ConduitDispenser>().IsConnected,
                "The ordinary native reservoir must remain disconnected.");
        }
        private static Packet ReadPacket(Storage value)
        {
            Require(value.items.Count == 1 && value.items[0] != null, "Ordinary reservoir lost its one native gas packet.");
            var item = value.items[0]; var element = item.GetComponent<PrimaryElement>();
            Require(element != null && item.GetComponent<SaveLoadRoot>() != null && element.Mass > 0 && element.DiseaseIdx != byte.MaxValue,
                "Native packet lacks positive saved material or real disease metadata.");
            Require(ReferenceEquals(item.transform.parent, value.transform), "Native packet is not actually parented to its storage.");
            return new Packet { Element = (int)element.ElementID, MassKg = element.Mass, TemperatureK = element.Temperature,
                MassBits = Bits(element.Mass), TemperatureBits = Bits(element.Temperature), DiseaseIndex = element.DiseaseIdx,
                DiseaseCount = element.DiseaseCount, DiseaseId = Db.Get().Diseases[element.DiseaseIdx].id.ToString() };
        }
        private static bool Equal(Packet a, Packet b) => a.Element == b.Element && a.MassBits == b.MassBits &&
            a.TemperatureBits == b.TemperatureBits && a.DiseaseIndex == b.DiseaseIndex && a.DiseaseId == b.DiseaseId && a.DiseaseCount == b.DiseaseCount;
        private static int Bits(float value) => BitConverter.ToInt32(BitConverter.GetBytes(value), 0);
        private static void ObserveSerialize(Storage value)
        {
            if (phase != 3 || !ReferenceEquals(value, storage)) return;
            serializedPacket = ReadPacket(value); ++serializedCount;
        }
        private static void ObserveDirectoryReturn(IReader reader)
        {
            // Only the actual successful native positive-control load supplies
            // this offset. No extra Manager call/reset or private KSAV parser.
            if (phase != 5 || OwnedReservoirLoadBoundary.IsFatal) return;
            try
            {
                var bytes = reader.RawBytes();
                Require(bytes.LongLength == ordinaryLength && Hash(bytes) == ordinaryHash && reader.Position > 16 && reader.Position < bytes.Length,
                    "Native directory return did not refer to the exact frozen positive-control reader.");
                Require(directoryEnd == 0, "Native positive-control directory was read more than once.");
                directoryEnd = reader.Position;
                evidence.Add(new { Kind = "actual-native-directory-return-observed", Offset = directoryEnd, ReaderBytes = bytes.Length });
            }
            catch (Exception error) { directoryObservationError = error.ToString(); }
        }
        private static void ObserveQuitCode(int exitCode) => ObserveQuit("App.QuitCode", exitCode);
        private static void ObserveApplicationQuitting() => ObserveQuit("Application.quitting", null);
        private static void ObserveNativeError(string condition, string stackTrace, LogType type)
        {
            if (nativeErrorObserved || (type != LogType.Exception && type != LogType.Error && type != LogType.Assert)) return;
            // Native exceptions can reach Unity without escaping Tick. Latch
            // the first error even after completion, so a delayed error dialog
            // cannot leave this isolated run reported as passed. Warning logs
            // from the deliberately invalid zlib control are not exceptions.
            nativeErrorObserved = true;
            var error = new InvalidOperationException("Unity reported " + type + " during the private load experiment: " + condition);
            try
            {
                evidence.Add(new { Kind = "native-unity-error", Phase = phase, Frame = Time.frameCount,
                    Type = type.ToString(), Condition = condition, Stack = stackTrace });
                RecordFailure(error);
            }
            catch
            {
                failureRecorded = true; completedSuccessfully = false; finished = true;
                try { Write("failed"); } catch { /* Never replace a native logging failure. */ }
            }
            finally
            {
                // Private report/screenshot failure cannot skip the primary
                // report's late-error downgrade.
                try { ColonyRuntimeValidation.Fail(error); } catch { }
            }
        }
        private static void ObserveQuit(string origin, int? exitCode)
        {
            // Diagnostic only: never cancel or replace a native quit. Capture
            // final lifecycle/counters before a pending test process disappears.
            try
            {
                object? counters = null; string counterError = "";
                try { if (OwnedReservoirLoadValidationCounters.Installed) counters = OwnedReservoirLoadValidationCounters.Capture(); }
                catch (Exception error) { counterError = error.ToString(); }
                var stack = Environment.StackTrace;
                evidence.Add(new { Kind = "quit-observed", Origin = origin, ExitCode = exitCode, Phase = phase, Finished = finished,
                    CompletedSuccessfully = completedSuccessfully, FailureRecorded = failureRecorded, NativeErrorObserved = nativeErrorObserved,
                    Frame = Time.frameCount, Scene = App.GetCurrentSceneName(), App.isLoading, Stack = stack, Counters = counters, CounterError = counterError,
                    CurrentAttempt = DescribeAttempt(OwnedReservoirLoadBoundary.CurrentAttempt), LastAttempt = DescribeAttempt(OwnedReservoirLoadBoundary.LastAttempt),
                    GenericGameSettings.instance.devBootSmoke, GenericGameSettings.instance.devBootModReport,
                    GenericGameSettings.instance.autoResumeGame, ScriptedFrameCount = GenericGameSettings.instance.scriptedProfile.frameCount });
                Write(failureRecorded ? "failed" : completedSuccessfully ? "passed" : "aborted");
                Debug.Log("[Mixed Atmospheres private validation] Observed " + origin + " at phase " + phase + ": " + stack);
            }
            catch { /* Observation must not change native quit semantics. */ }
        }
        private static void FocusAndSelect(GameObject value)
        {
            CameraController.Instance.SnapTo(value.transform.GetPosition());
            CameraController.Instance.OrthographicSize = 12;
            SelectTool.Instance.Select(value.GetComponent<KSelectable>(), true);
        }
        private static void CreateMalformedCopy()
        {
            var original = File.ReadAllBytes(ordinaryPath);
            var count = BinaryPrimitives.ReadInt32LittleEndian(original.AsSpan(4));
            Require(count > 0 && count <= OwnedReservoirLoadHeaderV1.MaximumJsonBytes && original.Length > 16 + count,
                "Native positive control lacks a bounded JSON header and body.");
            var utf8 = new UTF8Encoding(false, true);
            var text = utf8.GetString(original, 16, count);
            var end = text.Length - 1; while (end >= 0 && char.IsWhiteSpace(text[end])) --end;
            Require(end >= 0 && text[end] == '}', "Native header does not end in an object.");
            var modified = utf8.GetBytes(text.Insert(end, ",\"mixedAtmospheresOwnedReservoirs\":{\"formatVersion\":99}"));
            Require(modified.Length <= OwnedReservoirLoadHeaderV1.MaximumJsonBytes, "Malformed test header exceeds its bound.");
            using (var stream = new FileStream(malformedPath, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                var prefix = original.AsSpan(0, 16).ToArray(); BinaryPrimitives.WriteInt32LittleEndian(prefix.AsSpan(4), modified.Length);
                stream.Write(prefix, 0, prefix.Length); stream.Write(modified, 0, modified.Length);
                stream.Write(original, 16 + count, original.Length - 16 - count); stream.Flush(true);
            }
            var changed = File.ReadAllBytes(malformedPath);
            Require(original.AsSpan(16 + count).SequenceEqual(changed.AsSpan(16 + modified.Length)),
                "Private malformed header creation changed the native serialization directory/body.");
            var inspection = OwnedReservoirLoadHeaderV1.InspectFile(malformedPath);
            Require(inspection.Kind == OwnedReservoirLoadInputKind.InvalidHeader && inspection.ReservedNamespaceObserved &&
                inspection.Error.Contains("formatVersion"), "The deliberate schema99 test header was not rejected.");
            malformedLength = changed.LongLength; malformedHash = HashFile(malformedPath);
            evidence.Add(new { Kind = "private-deliberately-invalid-header-derived", ordinaryPath, ordinaryHash, malformedPath, malformedHash,
                OldHeaderBytes = count, NewHeaderBytes = modified.Length, BodyBytes = original.Length - 16 - count,
                BodySha256 = Hash(original.AsSpan(16 + count).ToArray()), BodyCopiedExactly = true,
                InspectionKind = inspection.Kind.ToString(), inspection.Error, OwnedRestorePermitted = false });
        }
        private static void VerifyFiles()
        {
            Require(File.Exists(ordinaryPath) && new FileInfo(ordinaryPath).Length == ordinaryLength && HashFile(ordinaryPath) == ordinaryHash &&
                File.Exists(malformedPath) && new FileInfo(malformedPath).Length == malformedLength && HashFile(malformedPath) == malformedHash,
                "An input checkpoint was moved, renamed, overwritten or changed during the experiment.");
            if (failedBodyHash.Length != 0)
                Require(File.Exists(failedBodyPath) && new FileInfo(failedBodyPath).Length == failedBodyLength && HashFile(failedBodyPath) == failedBodyHash,
                    "The caught-failure input checkpoint was moved, renamed, overwritten or changed.");
        }
        private static void CreateFailedCompressionCopy()
        {
            var bytes = File.ReadAllBytes(ordinaryPath);
            Require(directoryEnd > 16 && directoryEnd < bytes.Length && BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(12)) == 1,
                "Caught-failure fixture requires the observed native directory boundary and compressed native save.");
            using (var stream = new FileStream(failedBodyPath, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                stream.Write(bytes, 0, directoryEnd);
                // Invalid zlib CMF/FLG is rejected by the managed decompressor
                // before a FastReader or native simulator sees a world body.
                stream.Write(new byte[] { 255, 255, 255, 255, 255, 255, 255, 255 }, 0, 8);
                stream.Flush(true);
            }
            var bad = File.ReadAllBytes(failedBodyPath);
            Require(bytes.AsSpan(0, directoryEnd).SequenceEqual(bad.AsSpan(0, directoryEnd)) &&
                OwnedReservoirLoadHeaderV1.InspectFile(failedBodyPath).Kind == OwnedReservoirLoadInputKind.Unmarked,
                "Caught-failure copy changed the ordinary header or serialization directory.");
            failedBodyHash = Hash(bad); failedBodyLength = bad.LongLength;
            evidence.Add(new { Kind = "private-managed-decompression-failure-derived", Path = failedBodyPath, failedBodyHash, failedBodyLength,
                NativeHeaderAndDirectoryPrefixBytes = directoryEnd, NativePrefixSha256 = Hash(bytes.AsSpan(0, directoryEnd).ToArray()),
                PrefixCopiedExactly = true, PrivateBodyHex = "ffffffffffffffff", ExpectedFailure = "managed zlib header error before inner world reader" });
        }
        private static void VerifyCaughtFalseEpoch()
        {
            VerifyFiles(); VerifyRejectionFiles();
            var delta = CounterDelta();
            var attempt = OwnedReservoirLoadBoundary.LastAttempt;
            evidence.Add(new { Kind = "caught-native-false-epoch-observed", Counters = delta, Attempt = DescribeAttempt(attempt),
                FatalStillActive = OwnedReservoirLoadBoundary.IsFatal, Completed = OwnedReservoirLoadBoundary.CompletedAttempts.Count,
                ActivePath = SaveLoader.GetActiveSaveFilePath(), Scene = App.GetCurrentSceneName(), NativeGridCells = Grid.CellCount,
                StableFrontendFrames = stableMenuFrames, StableFrontendSeconds = Time.realtimeSinceStartup - menuReadyAt,
                ordinaryHash, malformedHash, failedBodyHash });
            Write("pending");
            Require(attempt != null && attempt.Sequence == 3 && attempt.Input.SelectedPath == failedBodyPath &&
                !attempt.Input.ReservedNamespaceObserved && attempt.Origin.Contains("false-result") && attempt.FrontendRequested &&
                attempt.TeardownCompleted && !attempt.TeardownFailed && !attempt.SelectedCallPending && !attempt.OnSpawnCallPending &&
                !OwnedReservoirLoadBoundary.IsFatal && OwnedReservoirLoadBoundary.CompletedAttempts.Count == 3,
                "Real caught native false result did not fail and finish actual startup teardown.");
            Require(SaveLoader.GetActiveSaveFilePath() == null && OwnedGasReservoirBridge.BindingCount == 0,
                "Caught false result retained a selected path or ownership.");
            Require(attempt.CompletionKind == OwnedReservoirLoadCompletionKind.PreGameLoaderScene,
                "Caught startup failure did not use its actual pre-Game lifecycle.");
            VerifyPreGameCompletion(attempt);
            RequireBody(delta, typeof(SaveLoader), "OnSpawn");
            RequireBody(delta, typeof(SaveLoader), "Load", typeof(string));
            RequireBody(delta, typeof(KSerialization.Manager), "DeserializeDirectory", typeof(IReader));
            Require(NativeTotal(delta, "SIM_Initialize(") == 1 && NativeTotal(delta, "SIM_Shutdown(") == 1,
                "Real failed startup did not exercise exactly one native initialization and its catch's shutdown.");
            RequireCleanTeardownAndFrontendRequest(delta, preGame: true);
            // These two permitted originals establish the real failure; all
            // inner-world/start/object/save/fallback continuations stay absent.
            RequireRejectedBodiesZero(delta, early: false, allowFailedOuter: true);
            evidence.Add(new { Kind = "caught-native-false-epoch-passed", Sequence = 3 });
            Write("pending");
        }
        private static void VerifyPreGameCompletion(OwnedReservoirLoadAttempt attempt)
        {
            var capture = OwnedReservoirLoadBoundary.LastPreGameSceneCapture;
            var suppression = OwnedReservoirLoadBoundary.LastEmptyAnimationGridSuppression;
            var suppressionCount = OwnedReservoirLoadBoundary.EmptyAnimationGridSuppressionCount;
            evidence.Add(new { Kind = "pre-game-lifecycle-observed", AttemptId = attempt.Id,
                attempt.LoaderDestroyCompleted, attempt.SceneUnloadCompleted, attempt.OnSpawnCallPending,
                attempt.PreGameInvalidated, attempt.OwnerReferenceCleared,
                LoaderClrNull = ReferenceEquals(SaveLoader.Instance, null), GameClrNull = ReferenceEquals(Game.Instance, null),
                Capture = DescribePreGameCapture(attempt),
                Suppression = suppression == null ? null : new {
                    suppression.AttemptId, AttemptIdentityMatches = ReferenceEquals(suppression.AttemptIdentity, attempt.Identity),
                    UpdaterMatches = ReferenceEquals(suppression.CapturedUpdater, suppression.ObservedUpdater),
                    UpdaterMatchesCapture = capture != null && ReferenceEquals(suppression.CapturedUpdater, capture.CapturedUpdater),
                    InitialGridMatches = capture != null && ReferenceEquals(suppression.ControllerGrid, capture.InitialControllerGrid),
                    suppression.GridRows, suppression.GridColumns, GridLength = suppression.ControllerGrid.Length,
                    suppression.ActivationThreadId, suppression.ObservedThreadId, suppression.Count },
                SuppressionBaseline = emptyAnimationGuardBaseline, SuppressionCount = suppressionCount });
            Write("pending");
            Require(attempt.LoaderDestroyCompleted && attempt.SceneUnloadCompleted && !attempt.PreGameInvalidated &&
                attempt.OwnerReferenceCleared && !attempt.OnSpawnCallPending && !attempt.SelectedCallPending &&
                capture != null && capture.AttemptId == attempt.Id && ReferenceEquals(capture.AttemptIdentity, attempt.Identity) &&
                !ReferenceEquals(capture.OwnerLoader, null) && capture.OwnerLoader == null && capture.SceneName == "backend" &&
                ReferenceEquals(SaveLoader.Instance, null) && ReferenceEquals(Game.Instance, null),
                "Pre-Game rejection lacks its exact destroyed loader, unloaded backend scene, held-call completion or cleared owner reference.");
            Require(suppression != null && suppression.AttemptId == attempt.Id && ReferenceEquals(suppression.AttemptIdentity, attempt.Identity) &&
                ReferenceEquals(suppression.CapturedUpdater, suppression.ObservedUpdater) && ReferenceEquals(suppression.CapturedUpdater, capture.CapturedUpdater) &&
                suppression.ControllerGrid.Length == 0 && suppression.ActivationThreadId == suppression.ObservedThreadId &&
                suppressionCount > emptyAnimationGuardBaseline && suppression.Count == suppressionCount,
                "Pre-Game acceptance did not exercise its exact existing empty-animation-grid guard.");
        }
        private static object? DescribePreGameCapture(OwnedReservoirLoadAttempt attempt)
        {
            var capture = OwnedReservoirLoadBoundary.LastPreGameSceneCapture;
            return capture == null ? null : new {
                capture.AttemptId, AttemptIdentityMatches = ReferenceEquals(capture.AttemptIdentity, attempt.Identity),
                capture.SceneHandle, capture.SceneName, capture.ScenePath, capture.SceneBuildIndex,
                LoaderClrNull = ReferenceEquals(capture.OwnerLoader, null), LoaderUnityNull = capture.OwnerLoader == null,
                UpdaterClrNull = ReferenceEquals(capture.CapturedUpdater, null),
                capture.InitialGridRows, capture.InitialGridColumns, InitialGridLength = capture.InitialControllerGrid?.Length };
        }
        private static string HashFile(string path) { using var stream = File.OpenRead(path); using var sha = SHA256.Create(); return Hex(sha.ComputeHash(stream)); }
        private static string Hash(byte[] bytes) { using var sha = SHA256.Create(); return Hex(sha.ComputeHash(bytes)); }
        private static string Hex(byte[] bytes) => BitConverter.ToString(bytes).Replace("-", "").ToLowerInvariant();
        private static IReadOnlyDictionary<string, string> CaptureRejectionFiles()
        {
            var parent = Path.GetDirectoryName(ordinaryPath)!;
            var files = Directory.GetFiles(parent, "*", SearchOption.AllDirectories);
            Require(files.Length <= 128 && files.Sum(file => new FileInfo(file).Length) <= 512L * 1024 * 1024,
                "Disposable save-directory inventory exceeds the private inspection bound.");
            var inventory = new SortedDictionary<string, string>(StringComparer.Ordinal);
            foreach (var file in files) inventory.Add(file.Substring(parent.Length + 1), new FileInfo(file).Length + ":" + HashFile(file));
            return inventory;
        }
        private static void FreezeRejectionFiles(string name)
        {
            rejectionFileInventory = CaptureRejectionFiles();
            evidence.Add(new { Kind = "rejection-input-directory-frozen", Name = name, Root = Path.GetDirectoryName(ordinaryPath), Files = rejectionFileInventory });
        }
        private static void VerifyRejectionFiles()
        {
            var actual = CaptureRejectionFiles();
            Require(rejectionFileInventory != null && actual.Count == rejectionFileInventory.Count && actual.All(row =>
                rejectionFileInventory.TryGetValue(row.Key, out var expected) && expected == row.Value),
                "A disposable save-directory file was created, removed or changed during rejection.");
        }
        private static void RequireBody(OwnedReservoirLoadCounterDelta delta, Type type, string name, params Type[] parameters)
        {
            var key = OwnedReservoirLoadValidationCounters.Key(type, name, parameters);
            Require(delta.Counts.TryGetValue(key, out var count) && count.OriginalBodyEntries > 0, "Positive-control original body was not observed: " + key);
        }
        private static OwnedReservoirLoadCounterSnapshot CaptureCounters()
        {
            var value = OwnedReservoirLoadValidationCounters.Capture();
            Require(value.Installed && !value.Saturated && value.ObservationFailures == 0 && value.ObserverErrorCount == 0 && value.ObserverErrorsDropped == 0 &&
                value.Counts.Count == value.ExpectedMethodCount && value.ExpectedMethodCount == 45 && value.ExpectedNativeCallsiteKeyCount == 15 &&
                value.ExpectedManagedAntecedentKeyCount == 4, "Private counter contract is incomplete or has dropped/failed observations.");
            if (!contractRecorded)
            {
                contractRecorded = true;
                evidence.Add(new { Kind = "complete-private-counter-contract", value.ExpectedMethodCount, value.ExpectedNativeCallsiteKeyCount,
                    value.ExpectedManagedAntecedentKeyCount, value.ContractEvidence, value.CoverageNotes });
            }
            return value;
        }
        private static OwnedReservoirLoadCounterDelta CounterDelta()
        {
            var value = OwnedReservoirLoadValidationCounters.Delta(baseline!, CaptureCounters());
            Require(value.CompleteContractVerified && value.ObservationFailures == 0 && value.ObserverErrorCount == 0,
                "Counter epoch contains failed observations or changed instrumentation.");
            return value;
        }
        private static long NativeTotal(OwnedReservoirLoadCounterDelta delta, string target)
            => delta.NativeCallsiteEntries.Where(row => row.Key.Contains(" => Sim." + target)).Sum(row => row.Value);
        private static long NativeLoadDispatches(OwnedReservoirLoadCounterDelta delta)
            => delta.NativeCallsiteEntries.Where(row => row.Key.EndsWith(" [message=-672538170]", StringComparison.Ordinal) ||
                ((row.Key.StartsWith("Sim.LoadWorld(", StringComparison.Ordinal) || row.Key.StartsWith("Sim.Load(", StringComparison.Ordinal)) && row.Key.Contains(" => Sim.SIM_HandleMessage("))).Sum(row => row.Value);
        private static long NativeStartDispatches(OwnedReservoirLoadCounterDelta delta)
            => delta.NativeCallsiteEntries.Where(row => row.Key.EndsWith(" [message=-931446686]", StringComparison.Ordinal) ||
                (row.Key.StartsWith("Sim.Start(", StringComparison.Ordinal) && row.Key.Contains(" => Sim.SIM_HandleMessage("))).Sum(row => row.Value);
        private static void RequireCleanTeardownAndFrontendRequest(OwnedReservoirLoadCounterDelta delta, bool preGame)
        {
            var key = OwnedReservoirLoadValidationCounters.Key(typeof(Game), "DestroyInstances");
            Require(delta.Counts.TryGetValue(key, out var teardown), "Native Game teardown counter is missing.");
            if (!preGame)
                Require(teardown!.CleanTeardownCompletions == 1 && teardown.FinalizerExceptions == 0,
                    "Matching native Game teardown did not return cleanly through its actual body.");
            else
            {
                Require(teardown!.OriginalBodyEntries == 0 && teardown.CleanTeardownCompletions == 0,
                    "A pre-Game rejection unexpectedly used Game teardown.");
                foreach (var body in new[] {
                    OwnedReservoirLoadValidationCounters.Key(typeof(KMonoBehaviour), "OnDestroy"),
                    OwnedReservoirLoadValidationCounters.Key(typeof(UnityEngine.SceneManagement.SceneManager), "Internal_SceneUnloaded", typeof(UnityEngine.SceneManagement.Scene)) })
                    Require(delta.Counts.TryGetValue(body, out var completed) && completed.OriginalBodyEntries > 0 &&
                        completed.OriginalBodyReturns > 0 && completed.FinalizerExceptions == 0,
                        "Native loader/scene lifetime body did not return cleanly: " + body);
            }
            Require(delta.OriginalBodySceneRequests.TryGetValue("frontend", out var requests) && requests == 1,
                "Rejected load must request exactly one actual frontend transition.");
        }
        private static void RequireRejectedBodiesZero(OwnedReservoirLoadCounterDelta delta, bool early, bool allowFailedOuter = false)
        {
            // Loader OnSpawn, metadata header reads, frontend UI and actual
            // teardown are permitted calls. All measured load/save bodies below
            // must be bypassed, including queued save coroutine continuations.
            foreach (var pair in delta.Counts)
            {
                var key = pair.Key;
                var forbidden = key.StartsWith("SaveLoader.Load(", StringComparison.Ordinal) ||
                    key.StartsWith("ProcGenGame.WorldGenSimUtil.DoSettleSim(", StringComparison.Ordinal) ||
                    key.StartsWith("SaveLoader.LoadFromWorldGen(", StringComparison.Ordinal) ||
                    key.StartsWith("SaveLoader.Save(", StringComparison.Ordinal) || key.StartsWith("SaveLoader.InitialSave(", StringComparison.Ordinal) ||
                    key.StartsWith("Sim.Load", StringComparison.Ordinal) || key.StartsWith("Sim.Start(", StringComparison.Ordinal) || key.StartsWith("Sim.Save(", StringComparison.Ordinal) ||
                    key.StartsWith("SaveManager.Load(", StringComparison.Ordinal) || key.StartsWith("SaveManager.Save(", StringComparison.Ordinal) || key.StartsWith("SaveManager.ClearScene(", StringComparison.Ordinal) ||
                    key.StartsWith("SaveLoadRoot.Load", StringComparison.Ordinal) || key.StartsWith("Storage.Serialize(", StringComparison.Ordinal) || key.StartsWith("Storage.Deserialize(", StringComparison.Ordinal) ||
                    key.StartsWith("Game.Load(", StringComparison.Ordinal) || key.StartsWith("Game.Save(", StringComparison.Ordinal) || key.StartsWith("Game.OnPrefabInit(", StringComparison.Ordinal) || key.StartsWith("Game.StartDelayed", StringComparison.Ordinal) ||
                    key.Contains("DelayedInitialSave>") || key.Contains("DelayedSave>") || key.StartsWith("KSerialization.Manager.Clear(", StringComparison.Ordinal) ||
                    key.StartsWith("KSerialization.Manager.DeserializeDirectory(", StringComparison.Ordinal) || (early && key.StartsWith("SaveLoader.OnSpawn(", StringComparison.Ordinal)) || key.StartsWith("Game.OnSpawn(", StringComparison.Ordinal);
                var permittedOuterFailure = allowFailedOuter && (key == OwnedReservoirLoadValidationCounters.Key(typeof(SaveLoader), "Load", typeof(string)) ||
                    key.StartsWith("KSerialization.Manager.Clear(", StringComparison.Ordinal) || key.StartsWith("KSerialization.Manager.DeserializeDirectory(", StringComparison.Ordinal));
                Require(!forbidden || permittedOuterFailure || pair.Value.OriginalBodyEntries == 0, "A forbidden original load/save body ran during rejection: " + key);
            }
            Require((allowFailedOuter || NativeTotal(delta, "SIM_Initialize(") == 0) && NativeLoadDispatches(delta) == 0 && NativeStartDispatches(delta) == 0 &&
                NativeTotal(delta, "SIM_BeginSave(") == 0 && NativeTotal(delta, "SIM_EndSave(") == 0,
                "A forbidden native initialize/load/start/save call site ran during rejection.");
            foreach (var row in delta.ManagedNativeDispatchAntecedents)
                if (row.Key.StartsWith("Sim.LoadWorld(", StringComparison.Ordinal) || row.Key.StartsWith("Sim.Load(", StringComparison.Ordinal) || row.Key.StartsWith("Sim.Start(", StringComparison.Ordinal))
                    Require(row.Value == 0, "A forbidden managed native-dispatch antecedent ran during rejection: " + row.Key);
        }
        // Mod.OnLoad failed after (part of) Install: Mod has already removed
        // every Harmony patch. Detach the Unity/counter observers too and keep
        // the explicit private report failed, as ObserveNativeError would.
        internal static void AbandonFailedLoad(Exception error)
        {
            Application.quitting -= ObserveApplicationQuitting;
            Application.logMessageReceived -= ObserveNativeError;
            OwnedReservoirLoadValidationCounters.StorageSerializeBodyObserved -= ObserveSerialize;
            failureRecorded = true; completedSuccessfully = false; finished = true;
            evidence.Add(new { Kind = "mod-load-failed-hooks-removed", Error = error.ToString() });
            try { Write("failed"); }
            finally { ColonyRuntimeValidation.Fail(error); }
        }
        internal static void RecordFailure(Exception error)
        {
            failureRecorded = true; completedSuccessfully = false; finished = true;
            evidence.Add(new { Kind = "failure", Phase = phase, Frame = Time.frameCount, Error = error.ToString(),
                CurrentAttempt = DescribeAttempt(OwnedReservoirLoadBoundary.CurrentAttempt), LastAttempt = DescribeAttempt(OwnedReservoirLoadBoundary.LastAttempt) });
            Write("failed"); ColonyRuntimeValidation.CaptureScreenshot("failure-phase-" + phase + ".png");
        }
        private static object? DescribeAttempt(OwnedReservoirLoadAttempt? value) => value == null ? null : new
        {
            value.Id, value.Sequence, value.Origin, value.Fatal, value.FrontendRequested, value.TeardownStarted, value.TeardownCompleted,
            value.TeardownFailed, value.SelectedCallPending, value.DroppedObservations,
            CompletionKind = value.CompletionKind.ToString(), value.LoaderDestroyCompleted, value.SceneUnloadCompleted,
            value.OnSpawnCallPending, value.PreGameInvalidated, value.OwnerReferenceCleared,
            Input = new { Kind = value.Input.Kind.ToString(), value.Input.SelectedPath, value.Input.ReservedNamespaceObserved, value.Input.Error },
            Observations = value.Observations.Select(row => new { row.Boundary, row.Detail, row.RecordedUtc }).ToArray()
        };
        private static object? DescribeRetirement(OwnedReservoirNativeViewRetirement? value, OwnedReservoirLoadAttempt? attempt) => value == null ? null : new
        {
            value.AttemptId, AttemptIdentityMatches = attempt != null && ReferenceEquals(value.AttemptIdentity, attempt.Identity),
            value.ActivationThreadId, value.ObservedThreadId, value.AttemptMatches, value.OwnerMatches, value.Retired,
            ExpectedOwnerMatchesOldGame = ReferenceEquals(value.ExpectedOwner, oldGame),
            ObservedOwnerMatchesOldGame = ReferenceEquals(value.ObservedOwner, oldGame),
            value.PriorCellCount, value.CellCount, value.PriorWidth, value.PriorHeight, value.WidthInCells, value.HeightInCells, value.Reason
        };
        private static void Write(string status) => File.WriteAllText(ReportPath, ReportJson.Serialize(new
        {
            Kind = "actual-game-private-owned-load-rejection", Status = status, Phase = phase, Evidence = evidence,
            OrdinaryNativeStorageOnly = true, OwnedRestorePermitted = false, FirstPlayableReady = false,
            CounterScope = "instrumented managed original bodies and native call sites; actual native internals are not instrumented"
        }));
        private static void Require([DoesNotReturnIf(false)] bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    }
}
