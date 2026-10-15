using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Security.Cryptography;
using System.Threading;
using HarmonyLib;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace MixedAtmospheres.Oni
{
    internal sealed class OwnedReservoirLoadHook
    {
        internal MethodInfo Target { get; }
        internal MethodInfo? Prefix { get; }
        internal MethodInfo? Postfix { get; }
        internal MethodInfo? Finalizer { get; }
        internal MethodInfo? Transpiler { get; }
        internal OwnedReservoirLoadHook(MethodInfo target, MethodInfo? prefix, MethodInfo? postfix = null, MethodInfo? finalizer = null, MethodInfo? transpiler = null)
        { Target = target; Prefix = prefix; Postfix = postfix; Finalizer = finalizer; Transpiler = transpiler; }
    }

    // A bounded managed diagnostic, not an ownership token or evidence of native
    // shutdown. Do not serialize the Game references through Unity properties.
    internal sealed class OwnedReservoirNativeViewRetirement
    {
        internal object AttemptIdentity { get; }
        internal Guid AttemptId { get; }
        internal int ActivationThreadId { get; }
        internal int ObservedThreadId { get; }
        internal bool AttemptMatches { get; }
        internal Game? ExpectedOwner { get; }
        internal Game? ObservedOwner { get; }
        internal bool OwnerMatches { get; }
        internal bool Retired { get; }
        internal int? PriorCellCount { get; }
        internal int? CellCount { get; }
        internal int? PriorWidth { get; }
        internal int? PriorHeight { get; }
        internal int? WidthInCells { get; }
        internal int? HeightInCells { get; }
        internal string Reason { get; }
        internal OwnedReservoirNativeViewRetirement(OwnedReservoirLoadAttempt attempt, int activationThreadId, int observedThreadId,
            bool attemptMatches, Game? expectedOwner, Game? observedOwner, bool retired, string reason,
            int? priorCellCount = null, int? cellCount = null, int? priorWidth = null, int? priorHeight = null,
            int? widthInCells = null, int? heightInCells = null)
        {
            AttemptIdentity = attempt.Identity; AttemptId = attempt.Id;
            ActivationThreadId = activationThreadId; ObservedThreadId = observedThreadId; AttemptMatches = attemptMatches;
            ExpectedOwner = expectedOwner; ObservedOwner = observedOwner;
            OwnerMatches = !ReferenceEquals(expectedOwner, null) && ReferenceEquals(expectedOwner, observedOwner);
            Retired = retired; Reason = reason; PriorCellCount = priorCellCount; CellCount = cellCount;
            PriorWidth = priorWidth; PriorHeight = priorHeight; WidthInCells = widthInCells; HeightInCells = heightInCells;
        }
    }

    internal sealed class OwnedReservoirPreGameSceneCapture
    {
        public object AttemptIdentity { get; }
        public Guid AttemptId { get; }
        public SaveLoader OwnerLoader { get; }
        public int SceneHandle { get; }
        public string SceneName { get; }
        public string ScenePath { get; }
        public int SceneBuildIndex { get; }
        public KBatchedAnimUpdater CapturedUpdater { get; }
        public Array? InitialControllerGrid { get; }
        public int? InitialGridRows { get; }
        public int? InitialGridColumns { get; }
        internal OwnedReservoirPreGameSceneCapture(OwnedReservoirLoadAttempt attempt, SaveLoader loader, int handle, string name,
            string path, int buildIndex, KBatchedAnimUpdater updater, Array? grid)
        {
            AttemptIdentity = attempt.Identity; AttemptId = attempt.Id; OwnerLoader = loader; SceneHandle = handle;
            SceneName = name; ScenePath = path; SceneBuildIndex = buildIndex; CapturedUpdater = updater; InitialControllerGrid = grid;
            InitialGridRows = grid?.GetLength(0); InitialGridColumns = grid?.GetLength(1);
        }
    }
    internal sealed class OwnedReservoirEmptyAnimationGridSuppression
    {
        public object AttemptIdentity { get; }
        public Guid AttemptId { get; }
        public KBatchedAnimUpdater CapturedUpdater { get; }
        public KBatchedAnimUpdater ObservedUpdater { get; }
        public Array ControllerGrid { get; }
        public int GridRows { get; }
        public int GridColumns { get; }
        public int ActivationThreadId { get; }
        public int ObservedThreadId { get; }
        public long Count { get; }
        internal OwnedReservoirEmptyAnimationGridSuppression(OwnedReservoirLoadAttempt attempt, KBatchedAnimUpdater captured,
            KBatchedAnimUpdater observed, Array grid, int expectedThread, int observedThread, long count)
        {
            AttemptIdentity = attempt.Identity; AttemptId = attempt.Id; CapturedUpdater = captured; ObservedUpdater = observed;
            ControllerGrid = grid; GridRows = grid.GetLength(0); GridColumns = grid.GetLength(1);
            ActivationThreadId = expectedThread; ObservedThreadId = observedThread; Count = count;
        }
    }
    internal sealed class OwnedReservoirLoadBodyInvocation
    {
        internal OwnedReservoirLoadAttempt Attempt { get; }
        internal OwnedReservoirLoadBodyKind Kind { get; }
        internal object Owner { get; }
        internal OwnedReservoirLoadTeardown? Completion { get; }
        internal bool Entered, Returned, NormalPathCertified, Closed, Faulted;
        internal OwnedReservoirLoadBodyInvocation(OwnedReservoirLoadAttempt attempt, OwnedReservoirLoadBodyKind kind, object owner, OwnedReservoirLoadTeardown? completion)
        { Attempt = attempt; Kind = kind; Owner = owner; Completion = completion; }
    }

    // Manual Install only: no HarmonyPatch attribute or automatic PatchAll
    // discovery. This adds rejection, never successful loading or save rights.
    internal static class OwnedReservoirLoadBoundary
    {
        private static OwnedReservoirLoadAttemptTracker attempts = new OwnedReservoirLoadAttemptTracker();
        private static Game? ownerGame;
        private static SaveLoader? ownerLoader;
        private static OwnedReservoirSelectedCall? selectedCall;
        private static OwnedReservoirSelectedLoad? selected;
        private static SaveLoader? selectedLoader;
        private static Game? selectedGame;
        private static OwnedReservoirLoadAttempt? selectedFailure;
        private static int activationThread;
        private static string? installedOwner;
        private static OwnedReservoirOnSpawnCall? onSpawnCall;
        private static SaveLoader? onSpawnLoader;
        private static readonly List<OwnedReservoirLoadBodyInvocation> bodyInvocations = new List<OwnedReservoirLoadBodyInvocation>();
        private static AccessTools.FieldRef<SaveLoader?>? loaderInstanceReference;
        private static readonly FieldInfo LoaderInstanceField = AccessTools.DeclaredField(typeof(SaveLoader), "<Instance>k__BackingField")
            ?? throw new NotSupportedException("Pinned SaveLoader singleton field is absent.");
        private static readonly FieldInfo ControllerGridField = AccessTools.DeclaredField(typeof(KBatchedAnimUpdater), "controllerGrid")
            ?? throw new NotSupportedException("Pinned animation controller grid is absent.");
        private static readonly FieldInfo UpdaterInstanceField = AccessTools.DeclaredField(typeof(Singleton<KBatchedAnimUpdater>), "_instance")
            ?? throw new NotSupportedException("Pinned existing animation updater field is absent.");
        private const string UnityCoreModuleSha256 = "a9613c2b0f1258ac7f7d9323181f816f5849beb24114e4a7665037fd11ddd488";
        private const string HarmonySha256 = "aec7446028fe31d00dfecc684e40011c4208bb036e82617b8de32002a8e55b53";
        private static readonly FieldInfo RestartField = RequireField("mustRestartOnFail");
        private static readonly FieldInfo CorruptField = RequireField("saveFileCorrupt");
        private static readonly MethodInfo LoadedSetter = AccessTools.PropertySetter(typeof(SaveLoader), "loadedFromSave")
            ?? throw new NotSupportedException("Pinned SaveLoader.loadedFromSave setter is absent.");
        internal static bool Installed { get; private set; }
        internal static bool RejectionOnly => true;
        internal static bool IsFatal => attempts.IsFatal;
        internal static OwnedReservoirLoadAttempt? CurrentAttempt => attempts.Current;
        internal static OwnedReservoirLoadAttempt? LastAttempt => attempts.Last;
        internal static IReadOnlyList<OwnedReservoirLoadAttempt> CompletedAttempts => attempts.Completed;
        internal static OwnedReservoirNativeViewRetirement? LastNativeViewRetirement { get; private set; }
        internal static OwnedReservoirPreGameSceneCapture? LastPreGameSceneCapture { get; private set; }
        internal static long EmptyAnimationGridSuppressionCount { get; private set; }
        internal static OwnedReservoirEmptyAnimationGridSuppression? LastEmptyAnimationGridSuppression { get; private set; }

        internal static void Install(Harmony harmony)
        {
            if (harmony == null) throw new ArgumentNullException(nameof(harmony));
            if (Installed)
            {
                if (installedOwner != harmony.Id || activationThread != Thread.CurrentThread.ManagedThreadId)
                    throw new InvalidOperationException("Load boundary already belongs to another installation/thread.");
                return;
            }
            RequireHash(typeof(SaveLoader).Assembly, OwnedReservoirLoadHeaderV1.AssemblyCSharpSha256);
            RequireHash(typeof(IReader).Assembly, OwnedReservoirLoadHeaderV1.FirstpassSha256);
            RequireHash(typeof(SceneManager).Assembly, UnityCoreModuleSha256);
            RequireHash(typeof(Harmony).Assembly, HarmonySha256);
            var hooks = GuardContracts();
            loaderInstanceReference = AccessTools.StaticFieldRefAccess<SaveLoader?>(LoaderInstanceField);
            var patched = new List<OwnedReservoirLoadHook>();
            try
            {
                foreach (var hook in hooks)
                {
                    // Record before Patch so a partially completed patch is
                    // also rolled back by its exact patch methods on failure.
                    patched.Add(hook);
                    harmony.Patch(hook.Target, HM(hook.Prefix), HM(hook.Postfix), HM(hook.Transpiler), HM(hook.Finalizer));
                }
                activationThread = Thread.CurrentThread.ManagedThreadId;
                // A diagnostic read before Install must not choose the future
                // native activation thread for the pure lifecycle holder.
                attempts = new OwnedReservoirLoadAttemptTracker();
                installedOwner = harmony.Id; Installed = true;
            }
            catch
            {
                for (var i = patched.Count - 1; i >= 0; --i)
                {
                    var hook = patched[i];
                    foreach (var method in new[] { hook.Prefix, hook.Postfix, hook.Transpiler, hook.Finalizer })
                        if (method != null) harmony.Unpatch(hook.Target, method);
                }
                throw;
            }
        }

        private static HarmonyMethod? HM(MethodInfo? method) => method == null ? null : new HarmonyMethod(method) { priority = Priority.First };
        private static void RequireHash(Assembly assembly, string expected)
        {
            using var stream = File.OpenRead(assembly.Location); using var hash = SHA256.Create();
            var actual = BitConverter.ToString(hash.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
            if (actual != expected) throw new NotSupportedException("Owned-load boundary requires the exact inspected 744825 assembly: " + assembly.GetName().Name);
        }
        private static FieldInfo RequireField(string name)
        {
            var field = AccessTools.DeclaredField(typeof(SaveLoader), name);
            if (field == null || field.IsStatic || field.FieldType != typeof(bool)) throw new NotSupportedException("Pinned SaveLoader field differs: " + name);
            return field;
        }
        private static MethodInfo Method(Type type, string name, Type result, bool isStatic, params Type[] parameters)
        {
            var method = AccessTools.DeclaredMethod(type, name, parameters);
            if (method == null || method.ReturnType != result || method.IsStatic != isStatic || method.GetMethodBody() == null)
                throw new NotSupportedException("Pinned owned-load hook differs: " + type.Name + "." + name);
            return method;
        }
        private static MethodInfo Patch(string name) => AccessTools.DeclaredMethod(typeof(OwnedReservoirLoadBoundary), name)
            ?? throw new MissingMethodException(typeof(OwnedReservoirLoadBoundary).Name, name);

        internal static IReadOnlyList<OwnedReservoirLoadHook> GuardContracts()
        {
            RequireViewRetirementContract();
            RequirePreGameContracts();
            var hooks = new List<OwnedReservoirLoadHook>();
            void Add(Type type, string name, Type result, bool isStatic, string prefix, Type[] parameters, string? postfix = null, string? finalizer = null, string? transpiler = null)
                => hooks.Add(new OwnedReservoirLoadHook(Method(type, name, result, isStatic, parameters), Patch(prefix),
                    postfix == null ? null : Patch(postfix), finalizer == null ? null : Patch(finalizer), transpiler == null ? null : Patch(transpiler)));
            Add(typeof(SaveLoader), "OnSpawn", typeof(void), false, nameof(OnSpawnPrefix), Type.EmptyTypes, finalizer: nameof(OnSpawnFinalizer), transpiler: nameof(OnSpawnContinuationTranspiler));
            Add(typeof(SaveLoader), "Load", typeof(bool), false, nameof(LoadFilePrefix), new[] { typeof(string) }, nameof(LoadFilePostfix), nameof(LoadFileFinalizer));
            Add(typeof(SaveLoader), "Load", typeof(bool), false, nameof(RejectBoolPrefix), new[] { typeof(IReader) }, nameof(LoadResultPostfix), nameof(LoadFinalizer));
            Add(typeof(SaveGame), "GetHeader", typeof(SaveGame.GameInfo), true, nameof(ActualHeaderPrefix),
                new[] { typeof(IReader), typeof(SaveGame.Header).MakeByRefType(), typeof(string) });
            Add(typeof(SaveLoader), "LoadFromWorldGen", typeof(bool), false, nameof(RejectBoolPrefix), Type.EmptyTypes);
            Add(typeof(SaveLoader), "InitialSave", typeof(void), false, nameof(RejectSavePrefix), Type.EmptyTypes);
            Add(typeof(SaveLoader), "Save", typeof(string), false, nameof(RejectSavePrefix), new[] { typeof(string), typeof(bool), typeof(bool) });
            Add(typeof(SaveLoader), "Save", typeof(void), false, nameof(RejectSavePrefix), new[] { typeof(BinaryWriter) });
            Add(typeof(Sim), "LoadWorld", typeof(int), true, nameof(RejectIntPrefix), new[] { typeof(IReader) });
            Add(typeof(Sim), "Load", typeof(int), true, nameof(RejectIntPrefix), new[] { typeof(IReader) });
            Add(typeof(Sim), "Start", typeof(void), true, nameof(RejectVoidPrefix), Type.EmptyTypes);
            Add(typeof(Sim), "Save", typeof(void), true, nameof(RejectSavePrefix), new[] { typeof(BinaryWriter), typeof(int), typeof(int) });
            Add(typeof(SaveManager), "Load", typeof(bool), false, nameof(RejectBoolPrefix), new[] { typeof(IReader) }, nameof(ManagerLoadResultPostfix));
            Add(typeof(SaveLoadRoot), "Load", typeof(SaveLoadRoot), true, nameof(RejectRootPrefix),
                new[] { typeof(GameObject), typeof(Vector3), typeof(Quaternion), typeof(Vector3), typeof(IReader) });
            foreach (var name in new[] { "OnSpawn", "Update", "LateUpdate" }) Add(typeof(Game), name, typeof(void), false, nameof(RejectVoidPrefix), Type.EmptyTypes);
            Add(typeof(Game), "StartDelayedInitialSave", typeof(void), false, nameof(RejectSavePrefix), Type.EmptyTypes);
            Add(typeof(Game), "StartDelayedSave", typeof(void), false, nameof(RejectSavePrefix), new[] { typeof(string), typeof(bool), typeof(bool) });
            Add(typeof(Game), "Save", typeof(void), false, nameof(RejectSavePrefix), new[] { typeof(BinaryWriter) });
            Add(typeof(Game), "Load", typeof(void), false, nameof(RejectLoadContinuation), new[] { typeof(KSerialization.Deserializer) }, nameof(RejectLoadContinuation));
            Add(typeof(Game), "DestroyInstances", typeof(void), false, nameof(TeardownPrefix), Type.EmptyTypes, nameof(BodyPostfix), nameof(BodyFinalizer), nameof(BodyWitnessTranspiler));
            Add(typeof(KMonoBehaviour), "OnDestroy", typeof(void), false, nameof(LoaderDestroyPrefix), Type.EmptyTypes, nameof(BodyPostfix), nameof(BodyFinalizer), nameof(BodyWitnessTranspiler));
            Add(typeof(SceneManager), "Internal_SceneUnloaded", typeof(void), true, nameof(SceneUnloadPrefix), new[] { typeof(Scene) }, nameof(BodyPostfix), nameof(BodyFinalizer), nameof(BodyWitnessTranspiler));
            Add(typeof(KBatchedAnimUpdater), "DoGridProcessing", typeof(bool), false, nameof(EmptyAnimationGridPrefix), Type.EmptyTypes);
            Add(typeof(Game), "OnPrefabInit", typeof(void), false, nameof(LateGamePrefabPrefix), Type.EmptyTypes);
            hooks.Add(new OwnedReservoirLoadHook(Method(typeof(Sim), "Shutdown", typeof(void), true), null, Patch(nameof(ShutdownPostfix))));
            return new ReadOnlyCollection<OwnedReservoirLoadHook>(hooks);
        }

        private static void RequireViewRetirementContract()
        {
            // The inspected original call must remain a managed field
            // assignment. Foreign Harmony callbacks are revalidated separately.
            var method = Method(typeof(Game), "SetIsLoading", typeof(void), false);
            var field = AccessTools.DeclaredField(typeof(Game), "isLoading");
            var body = method.GetMethodBody()!;
            var il = body.GetILAsByteArray();
            if (field == null || field.IsStatic || field.FieldType != typeof(bool) || body.ExceptionHandlingClauses.Count != 0 ||
                il == null || il.Length != 8 || il[0] != 0x02 || il[1] != 0x17 || il[2] != 0x7d || il[7] != 0x2a ||
                BitConverter.ToInt32(il, 3) != field.MetadataToken)
                throw new NotSupportedException("Pinned Game.SetIsLoading must only assign its exact managed isLoading field to true.");
        }
        private static void RequirePreGameContracts()
        {
            if (!LoaderInstanceField.IsStatic || LoaderInstanceField.FieldType != typeof(SaveLoader) || !LoaderInstanceField.IsPrivate ||
                ControllerGridField.IsStatic || ControllerGridField.FieldType != typeof(Dictionary<int, KBatchedAnimController>[,]) ||
                !UpdaterInstanceField.IsStatic || UpdaterInstanceField.FieldType != typeof(KBatchedAnimUpdater))
                throw new NotSupportedException("Pinned loader singleton or animation-grid field differs.");
            var getter = Method(typeof(SaveLoader), "get_Instance", typeof(SaveLoader), true);
            var setter = Method(typeof(SaveLoader), "set_Instance", typeof(void), true, typeof(SaveLoader));
            var destroy = Method(typeof(SaveLoader), "DestroyInstance", typeof(void), true);
            var g = getter.GetMethodBody()!.GetILAsByteArray()!;
            var s = setter.GetMethodBody()!.GetILAsByteArray()!;
            var d = destroy.GetMethodBody()!.GetILAsByteArray()!;
            if (g.Length != 6 || g[0] != 0x7e || BitConverter.ToInt32(g, 1) != LoaderInstanceField.MetadataToken || g[5] != 0x2a ||
                s.Length != 7 || s[0] != 0x02 || s[1] != 0x80 || BitConverter.ToInt32(s, 2) != LoaderInstanceField.MetadataToken || s[6] != 0x2a ||
                d.Length != 7 || d[0] != 0x14 || d[1] != 0x28 || BitConverter.ToInt32(d, 2) != setter.MetadataToken || d[6] != 0x2a)
                throw new NotSupportedException("Pinned SaveLoader singleton access/reset must only read or assign its exact managed field.");
            foreach (var method in new[] { getter, setter, destroy, Method(typeof(Game), "DestroyInstances", typeof(void), false),
                Method(typeof(KMonoBehaviour), "OnDestroy", typeof(void), false), Method(typeof(SceneManager), "Internal_SceneUnloaded", typeof(void), true, typeof(Scene)) })
                if (method.GetMethodBody()!.ExceptionHandlingClauses.Count != 0)
                    throw new NotSupportedException("Body witness does not support exception regions: " + method.Name);
            var handle = AccessTools.PropertyGetter(typeof(Scene), "handle") ?? throw new NotSupportedException("Scene.handle is absent.");
            var sceneField = AccessTools.DeclaredField(typeof(Scene), "m_Handle") ?? throw new NotSupportedException("Scene handle field is absent.");
            var h = handle.GetMethodBody()?.GetILAsByteArray();
            var releaseHandle = h != null && h.Length == 7 && h[0] == 0x02 && h[1] == 0x7b &&
                BitConverter.ToInt32(h, 2) == sceneField.MetadataToken && h[6] == 0x2a;
            var debugHandle = h != null && h.Length == 12 && h[0] == 0x00 && h[1] == 0x02 && h[2] == 0x7b &&
                BitConverter.ToInt32(h, 3) == sceneField.MetadataToken && h[7] == 0x0a && h[8] == 0x2b && h[9] == 0 && h[10] == 0x06 && h[11] == 0x2a;
            if (handle.ReturnType != sceneField.FieldType || (!releaseHandle && !debugHandle))
                throw new NotSupportedException("Scene.handle must only read its exact managed handle field.");
        }

        private sealed class FailureOwner
        {
            internal readonly SaveLoader Loader;
            internal readonly Game? Game;
            internal readonly OwnedReservoirLoadCompletionKind Kind;
            internal readonly int Handle, BuildIndex;
            internal readonly string Name = "", Path = "", Error = "";
            internal readonly KBatchedAnimUpdater? Updater;
            internal readonly Array? Grid;
            internal FailureOwner(FailureOwner original, string error)
            { Loader = original.Loader; Game = original.Game; Kind = OwnedReservoirLoadCompletionKind.None; Error = error; }
            internal FailureOwner(SaveLoader loader, Game? game)
            {
                Loader = loader; Game = game;
                if (!ReferenceEquals(game, null)) { Kind = OwnedReservoirLoadCompletionKind.ActualGameDestroyInstances; return; }
                try
                {
                    if (loader == null) throw new InvalidOperationException("A live native SaveLoader is required.");
                    var scene = loader.gameObject.scene;
                    if (!scene.IsValid() || !scene.isLoaded || scene.name != "backend")
                        throw new InvalidOperationException("No-Game cancellation requires the loader's exact live backend scene.");
                    Handle = scene.handle; Name = scene.name; Path = scene.path; BuildIndex = scene.buildIndex;
                    Updater = Singleton<KBatchedAnimUpdater>.Instance ?? throw new InvalidOperationException("No existing animation updater.");
                    Grid = (Array?)ControllerGridField.GetValue(Updater);
                    Kind = OwnedReservoirLoadCompletionKind.PreGameLoaderScene;
                }
                catch (Exception error) { Error = error.GetType().Name + ": " + error.Message; Kind = OwnedReservoirLoadCompletionKind.None; }
            }
        }
        private static void AttachFailureOwner(OwnedReservoirLoadAttempt attempt, FailureOwner owner)
        {
            ownerGame = owner.Game; ownerLoader = owner.Loader;
            if (owner.Kind == OwnedReservoirLoadCompletionKind.PreGameLoaderScene)
                LastPreGameSceneCapture = new OwnedReservoirPreGameSceneCapture(attempt, owner.Loader, owner.Handle, owner.Name, owner.Path,
                    owner.BuildIndex, owner.Updater!, owner.Grid);
            else if (owner.Kind == OwnedReservoirLoadCompletionKind.None)
                attempts.InvalidatePreGame(attempt, "Cannot capture an exact first-failure owner: " + owner.Error);
        }
        private static bool HasOnSpawnHold(SaveLoader? loader)
            => onSpawnCall != null && ReferenceEquals(onSpawnLoader, loader);
        private static bool SameFailureCapture(OwnedReservoirSelectedCall? expectedSelected, OwnedReservoirSelectedCall? observedSelected,
            OwnedReservoirOnSpawnCall? expectedSpawn, OwnedReservoirOnSpawnCall? observedSpawn, Game? expectedGame, Game? observedGame,
            SaveLoader expectedLoader, SaveLoader? observedLoader, KBatchedAnimUpdater? expectedUpdater, object? observedUpdater)
            => ReferenceEquals(expectedSelected, observedSelected) && ReferenceEquals(expectedSpawn, observedSpawn) &&
                ReferenceEquals(expectedGame, observedGame) && ReferenceEquals(expectedLoader, observedLoader) && ReferenceEquals(expectedUpdater, observedUpdater);
        private static FailureOwner? CaptureFailureOwner(SaveLoader loader)
        {
            var expectedSelected = selectedCall; var expectedSpawn = onSpawnCall;
            var game = Game.Instance;
            var owner = new FailureOwner(loader, game);
            var observedGame = Game.Instance;
            var observedLoader = SaveLoader.Instance;
            var observedUpdater = UpdaterInstanceField.GetValue(null);
            // All possibly patched/native getters have returned before this
            // final check. Reentry may establish its own first fatal context.
            if (attempts.Current != null) return null;
            if (!SameFailureCapture(expectedSelected, selectedCall, expectedSpawn, onSpawnCall, game, observedGame, loader, observedLoader,
                owner.Updater, owner.Kind == OwnedReservoirLoadCompletionKind.PreGameLoaderScene ? observedUpdater : null))
                return new FailureOwner(owner, "First-failure owner or active invocation changed during native capture.");
            return owner;
        }
        private static OwnedReservoirLoadAttempt BeginFailure(SaveLoader loader, OwnedReservoirLoadHeaderInspection input, string origin)
        {
            var existing = attempts.Current;
            if (existing != null) return existing;
            var owner = CaptureFailureOwner(loader);
            if (owner == null) return attempts.Current ?? throw new InvalidOperationException("A reentrant first-failure owner disappeared.");
            var attempt = attempts.Reject(origin, input, selectedCallPending: selectedCall != null, completionKind: owner.Kind, onSpawnCallPending: HasOnSpawnHold(loader));
            AttachFailureOwner(attempt, owner); return attempt;
        }

        private static void RequireActivationThread()
        { if (Thread.CurrentThread.ManagedThreadId != activationThread) throw new InvalidOperationException("The native load boundary requires its installation thread."); }
        private static string Name(MethodBase method) => method.DeclaringType!.Name + "." + method.Name;
        private static bool Blocked(MethodBase method)
        {
            if (!Installed) return false;
            var attempt = attempts.Current;
            if (attempt == null) return false;
            attempts.Record(attempt, Name(method) + ":blocked", "Original body did not run while fatal.");
            return true;
        }
        private static void SetFailureFields(SaveLoader loader, OwnedReservoirLoadAttempt attempt)
        {
            try
            {
                RestartField.SetValue(loader, true); CorruptField.SetValue(loader, true);
                LoadedSetter.Invoke(loader, new object[] { false });
            }
            catch (Exception error) { attempts.Record(attempt, "native-failure-fields:error", error.GetType().Name + ": " + error.Message); }
        }
        private static bool InspectSelection(SaveLoader loader, string? selectedPath, string origin)
            => InspectSelection(loader, selectedPath, origin, out _);
        private static bool InspectSelection(SaveLoader loader, string? selectedPath, string origin, out OwnedReservoirLoadHeaderInspection? inspection)
        {
            RequireActivationThread();
            inspection = null;
            var existing = attempts.Current;
            if (existing != null)
            {
                if (selectedCall != null) selectedFailure = existing = attempts.HoldSelectedCall(existing);
                attempts.Record(existing, origin + ":blocked", "A previous fatal attempt has not completed actual teardown.");
                SetFailureFields(loader, existing); RequestFrontend(existing); return false;
            }
            inspection = OwnedReservoirLoadHeaderV1.InspectFile(selectedPath);
            if (!inspection.MustReject) return true;
            // Associate the actual invocation/scene, never a saved ID or a
            // native pointer. No successful restore is available in this slice.
            var attempt = BeginFailure(loader, inspection, origin);
            if (selectedCall != null) selectedFailure = attempt;
            SetFailureFields(loader, attempt); RequestFrontend(attempt); return false;
        }
        private static void RequestFrontend(OwnedReservoirLoadAttempt attempt)
        {
            if (!attempts.TryRequestFrontend(attempt)) return;
            // The flag is set before any reentrant scene callback. No native
            // Sim.Shutdown call is made, and no alternative file is loaded.
            try
            {
                if (!RetireNativeViewBeforeFrontend(attempt)) return;
                KMonoBehaviour.isLoadingScene = true; SaveLoader.SetActiveSaveFilePath(null); App.LoadScene("frontend");
            }
            catch (Exception error) { attempts.Record(attempt, "frontend-request:failed", error.GetType().Name + ": " + error.Message); }
        }
        private enum ViewRetirementDecision { NotCurrentAttempt, NoCurrentOwner, ForeignOwner, Retire }
        private static ViewRetirementDecision DecideViewRetirement(bool installed, int expectedThread, int observedThread,
            OwnedReservoirLoadAttempt expected, OwnedReservoirLoadAttempt? current, Game? expectedOwner, Game? observedOwner)
        {
            if (!installed || expectedThread != observedThread || current == null ||
                !ReferenceEquals(expected.Identity, current.Identity) || !current.FrontendRequested || current.TeardownStarted)
                return ViewRetirementDecision.NotCurrentAttempt;
            if (ReferenceEquals(observedOwner, null)) return ViewRetirementDecision.NoCurrentOwner;
            return ReferenceEquals(expectedOwner, null) || !ReferenceEquals(expectedOwner, observedOwner)
                ? ViewRetirementDecision.ForeignOwner : ViewRetirementDecision.Retire;
        }
        private static bool RetireNativeViewBeforeFrontend(OwnedReservoirLoadAttempt attempt)
        {
            RequireActivationThread();
            var expectedOwner = ownerGame;
            var observedOwner = Game.Instance;
            var observedThread = Thread.CurrentThread.ManagedThreadId;
            var current = attempts.Current;
            var decision = DecideViewRetirement(Installed, activationThread, observedThread, attempt, current, expectedOwner, observedOwner);
            if (decision != ViewRetirementDecision.Retire)
            {
                // A missing or foreign owner never permits reading or writing
                // global Grid state. A stale attempt cannot request a successor
                // scene transition or replace the successor's observation.
                if (decision == ViewRetirementDecision.NotCurrentAttempt) return false;
                LastNativeViewRetirement = new OwnedReservoirNativeViewRetirement(attempt, activationThread, observedThread,
                    true, expectedOwner, observedOwner, false, decision == ViewRetirementDecision.ForeignOwner
                        ? "Current Game is foreign to the fatal attempt; Grid and frontend scene untouched."
                        : "No current Game owner; Grid untouched before frontend request.");
                attempts.Record(attempt, "native-view-retirement:skipped", LastNativeViewRetirement.Reason);
                if (decision != ViewRetirementDecision.NoCurrentOwner || current!.CompletionKind != OwnedReservoirLoadCompletionKind.PreGameLoaderScene) return false;
                var capture = LastPreGameSceneCapture;
                return capture != null && ReferenceEquals(current.Identity, capture.AttemptIdentity) && ReferenceEquals(ownerLoader, capture.OwnerLoader) &&
                    loaderInstanceReference != null && ReferenceEquals(loaderInstanceReference(), capture.OwnerLoader);
            }
            var priorCellCount = Grid.CellCount;
            var priorWidth = Grid.WidthInCells;
            var priorHeight = Grid.HeightInCells;
            // The original method is verified field-only, but a foreign Harmony
            // prefix/postfix may still reenter. Revalidate all authority after
            // the call before touching global Grid state or requesting a scene.
            expectedOwner!.SetIsLoading();
            RequireActivationThread();
            observedOwner = Game.Instance;
            observedThread = Thread.CurrentThread.ManagedThreadId;
            current = attempts.Current;
            var confirmed = DecideViewRetirement(Installed, activationThread, observedThread, attempt, current, expectedOwner, observedOwner);
            if (confirmed != ViewRetirementDecision.Retire)
            {
                if (current != null && ReferenceEquals(attempt.Identity, current.Identity))
                {
                    LastNativeViewRetirement = new OwnedReservoirNativeViewRetirement(attempt, activationThread, observedThread,
                        true, expectedOwner, observedOwner, false, "SetIsLoading reentry changed the current retirement decision to " + confirmed + "; no subsequent Grid access or frontend request.",
                        priorCellCount: priorCellCount, priorWidth: priorWidth, priorHeight: priorHeight);
                    attempts.Record(attempt, "native-view-retirement:skipped-after-loading", LastNativeViewRetirement.Reason);
                }
                return false;
            }
            Grid.CellCount = 0;
            LastNativeViewRetirement = new OwnedReservoirNativeViewRetirement(attempt, activationThread, observedThread,
                true, expectedOwner, observedOwner, true, "Managed cell views retired before frontend/native StopBE.",
                priorCellCount, Grid.CellCount, priorWidth, priorHeight, Grid.WidthInCells, Grid.HeightInCells);
            attempts.Record(attempt, "native-view-retired", "priorCellCount=" + priorCellCount + "; cellCount=" + Grid.CellCount +
                "; widthInCells=" + Grid.WidthInCells + "; heightInCells=" + Grid.HeightInCells + "; exactOwner=true; exactAttempt=true; activationThread=" + observedThread);
            return true;
        }
        private static bool OnSpawnPrefix(SaveLoader __instance, out OwnedReservoirOnSpawnCall? __state)
        {
            __state = null;
            if (!Installed) return true;
            RequireActivationThread();
            if (onSpawnCall != null)
            {
                var reentrant = BeginFailure(onSpawnLoader ?? __instance, Failure(OwnedReservoirLoadInputKind.ReadError, "Recursive OnSpawn loading is unsupported."), "SaveLoader.OnSpawn:reentrant");
                attempts.HoldOnSpawnCall(reentrant);
                attempts.FailBodyWitness(reentrant, "Recursive/foreign OnSpawn cannot authorize a successful abort completion.");
                if (reentrant.CompletionKind == OwnedReservoirLoadCompletionKind.PreGameLoaderScene)
                    attempts.InvalidatePreGame(reentrant, "Recursive OnSpawn retained the outer invocation hold and invalidated completion.");
                return false;
            }
            __state = onSpawnCall = new OwnedReservoirOnSpawnCall(); onSpawnLoader = __instance;
            if (attempts.IsFatal)
            {
                if (ReferenceEquals(ownerLoader, __instance)) attempts.HoldOnSpawnCall(attempts.Current!);
                return InspectSelection(__instance, null, "SaveLoader.OnSpawn");
            }
            string? path;
            try { path = SaveLoader.GetActiveSaveFilePath(); }
            catch (Exception error)
            {
                var input = new OwnedReservoirLoadHeaderInspection(OwnedReservoirLoadInputKind.ReadError, "<active-save-path unavailable>", -1,
                    false, error.GetType().Name + ": " + error.Message, null, Array.Empty<byte>());
                var attempt = BeginFailure(__instance, input, "SaveLoader.OnSpawn");
                SetFailureFields(__instance, attempt); RequestFrontend(attempt); return false;
            }
            return InspectSelection(__instance, path, "SaveLoader.OnSpawn");
        }
        private static Exception? OnSpawnFinalizer(OwnedReservoirOnSpawnCall? __state, Exception? __exception)
        {
            if (__state == null || !ReferenceEquals(__state, onSpawnCall)) return __exception;
            var attempt = attempts.Current;
            if (attempt != null && ReferenceEquals(ownerLoader, onSpawnLoader))
            {
                if (__exception != null) attempts.FailBodyWitness(attempt, "OnSpawn escaped: " + __exception.GetType().Name);
                if (attempts.ReleaseOnSpawnCall(attempt)) { ownerGame = null; ownerLoader = null; }
            }
            onSpawnCall = null; onSpawnLoader = null;
            TryCompletePreGameScene();
            return __exception;
        }
        private static bool StopFailedOnSpawnContinuation(SaveLoader loader)
            => Installed && !loader.loadedFromSave;
        private static IEnumerable<CodeInstruction> OnSpawnContinuationTranspiler(IEnumerable<CodeInstruction> instructions, ILGenerator generator, MethodBase original)
        {
            var onSpawn = Method(typeof(SaveLoader), "OnSpawn", typeof(void), false);
            if (original != onSpawn || onSpawn.GetMethodBody()!.ExceptionHandlingClauses.Count != 0)
                throw new NotSupportedException("Pinned SaveLoader.OnSpawn continuation requires its exact body without exception regions.");
            var source = new List<CodeInstruction>(instructions);
            var load = Method(typeof(SaveLoader), "Load", typeof(bool), false, typeof(string));
            var stop = Patch(nameof(StopFailedOnSpawnContinuation));
            var index = -1;
            for (var i = 0; i < source.Count; ++i)
            {
                var item = source[i];
                if (item.blocks.Count != 0) throw new NotSupportedException("OnSpawn continuation does not support composed exception regions.");
                if (item.operand is MethodInfo called && called == stop)
                    throw new NotSupportedException("OnSpawn continuation guard is already present.");
                if (item.operand is MethodInfo method && method == load)
                {
                    if (item.opcode != OpCodes.Call || index != -1) throw new NotSupportedException("OnSpawn requires one exact direct Load(string) call.");
                    index = i;
                }
            }
            // There are two native loadedFromSave assignments. Only the setter
            // consuming this exact Load(string) result is a continuation point.
            if (index < 0 || index + 2 >= source.Count || source[index + 1].opcode != OpCodes.Call
                || !Equals(source[index + 1].operand, LoadedSetter) || Labels(source[index + 1]).Count != 0)
                throw new NotSupportedException("Pinned Load(string) result/setter adjacency or clean continuation changed.");
            object continueOriginal = generator.DefineLabel();
            var result = new List<CodeInstruction>();
            for (var i = 0; i < source.Count; ++i)
            {
                var clone = new CodeInstruction(source[i].opcode, source[i].operand);
                foreach (var label in Labels(source[i])) Labels(clone).Add(label);
                // All exception regions were rejected above. Keep all existing
                // labels on their original instructions, including worldgen's
                // paths; only fallthrough from the selected setter is fenced.
                if (i == index + 2) Labels(clone).Add(continueOriginal);
                result.Add(clone);
                if (i != index + 1) continue;
                result.Add(new CodeInstruction(OpCodes.Ldarg_0));
                result.Add(new CodeInstruction(OpCodes.Call, stop));
                result.Add(new CodeInstruction(OpCodes.Brfalse, continueOriginal));
                result.Add(new CodeInstruction(OpCodes.Ret));
            }
            return result;
        }
        private static IList Labels(CodeInstruction instruction)
            => AccessTools.DeclaredField(typeof(CodeInstruction), "labels")?.GetValue(instruction) as IList
                ?? throw new NotSupportedException("Pinned Harmony instruction labels are absent.");
        private static bool LoadFilePrefix(SaveLoader __instance, string filename, ref bool __result, out OwnedReservoirSelectedCall? __state)
        {
            __state = null;
            if (!Installed) return true;
            RequireActivationThread();
            if (selectedCall != null)
            {
                RejectSelected(Failure(OwnedReservoirLoadInputKind.ReadError, "Recursive selected loading is unsupported."), "SaveLoader.Load(string):reentrant");
                __result = false; return false;
            }
            // Pin the public invocation before preflight can request frontend
            // and synchronously reenter successful teardown. No path remains
            // inactive for normal new-colony/worldgen flow.
            if (!string.IsNullOrEmpty(filename))
            {
                selectedCall = __state = new OwnedReservoirSelectedCall(filename);
                selectedLoader = __instance; selectedGame = Game.Instance; selectedFailure = null;
            }
            if (!InspectSelection(__instance, filename, "SaveLoader.Load(string)", out var inspected)) { __result = false; return false; }
            if (inspected!.Kind == OwnedReservoirLoadInputKind.NoSelection) return true;
            selected = new OwnedReservoirSelectedLoad(inspected);
            return true;
        }
        private static OwnedReservoirLoadHeaderInspection Failure(OwnedReservoirLoadInputKind kind, string reason)
            => new OwnedReservoirLoadHeaderInspection(kind, selected?.Header.SelectedPath ?? selectedCall?.SelectedPath ?? "", -1, false, reason, null, Array.Empty<byte>());
        private static void RejectSelected(OwnedReservoirLoadHeaderInspection input, string origin, bool requestFrontend = true)
        {
            RequireActivationThread();
            var attempt = attempts.Current ?? (ReferenceEquals(selectedLoader, null)
                ? attempts.Reject(origin, input, selectedCallPending: selectedCall != null, completionKind: OwnedReservoirLoadCompletionKind.None)
                : BeginFailure(selectedLoader, input, origin));
            selectedFailure = attempt;
            if (!ReferenceEquals(ownerLoader, null)) SetFailureFields(ownerLoader!, attempt);
            if (requestFrontend) RequestFrontend(attempt);
        }
        private static void ActualHeaderPrefix(IReader br, string debugFileName)
        {
            // Save-list metadata scans are untouched unless the exact public
            // selected-load call is currently executing on its native reader.
            var scope = selected;
            if (!Installed || scope == null) return;
            RequireActivationThread();
            // Metadata scans for another save are outside this selected call's
            // header identity and remain native, even if reentered by UI code.
            string actualPath;
            try { actualPath = Path.GetFullPath(debugFileName); }
            catch (Exception error) when (error is ArgumentException || error is NotSupportedException) { return; }
            if (!string.Equals(actualPath, scope.Header.SelectedPath,
                Path.DirectorySeparatorChar == '\\' ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal)) return;
            if (attempts.IsFatal) throw new InvalidDataException("Fatal selected load cannot read another native header.");
            OwnedReservoirLoadHeaderInspection actual;
            try
            {
                if (scope.ActualHeaderVerified)
                    actual = Failure(OwnedReservoirLoadInputKind.InvalidHeader, "Selected header callback is duplicated.");
                else actual = OwnedReservoirLoadHeaderV1.VerifyActualHeader(scope.Header, br.RawBytes(), br.Position);
            }
            catch (Exception error) { actual = Failure(OwnedReservoirLoadInputKind.ReadError, "Actual selected header reader failed: " + error.GetType().Name + ": " + error.Message); }
            if (actual.MustReject)
            { RejectSelected(actual, "SaveGame.GetHeader:actual-copied-header"); throw new InvalidDataException(actual.Error); }
            selected = scope.WithVerifiedHeader();
        }
        private static void CompleteSelected(OwnedReservoirSelectedCall? scope)
        {
            if (scope == null || selectedCall == null || !ReferenceEquals(scope.Identity, selectedCall.Identity)) return;
            var failure = selectedFailure;
            selectedCall = null; selected = null; selectedLoader = null; selectedGame = null; selectedFailure = null;
            if (failure != null && attempts.ReleaseSelectedCall(failure)) { ownerGame = null; ownerLoader = null; }
            TryCompletePreGameScene();
        }
        private static OwnedReservoirLoadAttempt? ExactFailedSelection(SaveLoader loader, OwnedReservoirSelectedCall? scope)
        {
            if (!Installed || scope == null || selectedCall == null || !ReferenceEquals(scope.Identity, selectedCall.Identity)
                || !ReferenceEquals(loader, selectedLoader)) return null;
            var attempt = attempts.Current;
            return attempt != null && selectedFailure != null && attempt.SelectedCallPending
                && ReferenceEquals(attempt.Identity, selectedFailure.Identity) ? attempt : null;
        }
        private static void ClearFailedSelection(SaveLoader loader, OwnedReservoirSelectedCall? scope, string boundary)
        {
            var attempt = ExactFailedSelection(loader, scope);
            if (attempt == null) return;
            // The original outer loader may overwrite the pointer with an
            // autosave-derived name after an inner false result was ignored.
            // Reassert only this still-held failure, never another selection.
            try
            {
                SaveLoader.SetActiveSaveFilePath(null);
                attempts.Record(attempt, boundary + ":active-path-cleared", "Reasserted null while the exact failed selected call is held.");
            }
            catch (Exception error) { attempts.Record(attempt, boundary + ":active-path-clear-failed", error.GetType().Name + ": " + error.Message); }
        }
        private static void LoadFilePostfix(SaveLoader __instance, ref bool __result, MethodBase __originalMethod, OwnedReservoirSelectedCall? __state)
        {
            if (!Installed) return;
            if (__state != null && selectedCall != null && ReferenceEquals(__state.Identity, selectedCall.Identity) && selected != null && !selected.ActualHeaderVerified)
            { RejectSelected(Failure(OwnedReservoirLoadInputKind.ReadError, "Selected native loader did not verify its actual copied header."), "SaveLoader.Load(string):missing-header"); __result = false; }
            LoadResultPostfix(__instance, ref __result, __originalMethod);
            ClearFailedSelection(__instance, __state, "SaveLoader.Load(string):result");
        }
        private static Exception? LoadFileFinalizer(SaveLoader __instance, Exception? __exception, MethodBase __originalMethod, OwnedReservoirSelectedCall? __state)
        {
            try
            {
                if (__exception != null && __state != null && selectedCall != null && ReferenceEquals(__state.Identity, selectedCall.Identity) && !attempts.IsFatal)
                    RejectSelected(Failure(OwnedReservoirLoadInputKind.ReadError, "Escaping selected-load exception: " + __exception.GetType().Name + ": " + __exception.Message), "SaveLoader.Load(string):escaping-exception");
                return LoadFinalizer(__instance, __exception, __originalMethod);
            }
            finally
            {
                try { ClearFailedSelection(__instance, __state, "SaveLoader.Load(string):finalizer"); }
                finally { CompleteSelected(__state); }
            }
        }
        private static void LoadResultPostfix(SaveLoader __instance, ref bool __result, MethodBase __originalMethod)
        {
            if (!Installed) return;
            // The original outer loader discards its inner bool. A selected
            // native false result must establish failure before checking for an
            // already-fatal attempt, including a caught outer-load failure.
            var wasFatal = attempts.IsFatal;
            var firstOwner = !wasFatal && !__result && selectedCall != null && !ReferenceEquals(selectedLoader, null)
                ? CaptureFailureOwner(selectedLoader!) : null;
            var attempt = attempts.ObserveSelectedResult(selectedCall, Name(__originalMethod) + " " + __originalMethod, __result,
                firstOwner?.Kind ?? OwnedReservoirLoadCompletionKind.ActualGameDestroyInstances, firstOwner != null && HasOnSpawnHold(firstOwner.Loader));
            if (firstOwner != null && attempt != null) AttachFailureOwner(attempt, firstOwner);
            if (!__result && selectedCall != null && attempt != null)
            {
                selectedFailure = attempt;
                if (!ReferenceEquals(ownerLoader, null)) SetFailureFields(ownerLoader!, attempt);
                RequestFrontend(attempt);
            }
            if (attempt == null) return;
            attempts.Record(attempt, Name(__originalMethod) + ":result", "Observed native result " + __result + "; forced false while fatal.");
            if (!ReferenceEquals(__instance, ownerLoader)) attempts.Record(attempt, "foreign-loader:blocked", "A different loader cannot replace the fatal selection.");
            __result = false; SetFailureFields(__instance, attempt);
        }
        private static Exception? LoadFinalizer(SaveLoader __instance, Exception? __exception, MethodBase __originalMethod)
        {
            var attempt = Installed ? attempts.Current : null;
            if (attempt != null)
            {
                attempts.Record(attempt, Name(__originalMethod) + ":finalizer", __exception == null ? "No escaping exception; fatal remains latched." : __exception.GetType().Name + ": " + __exception.Message);
                SetFailureFields(__instance, attempt);
            }
            return __exception;
        }
        private static bool RejectBoolPrefix(MethodBase __originalMethod, ref bool __result)
        { if (!Blocked(__originalMethod)) return true; __result = false; return false; }
        private static void ManagerLoadResultPostfix(MethodBase __originalMethod, ref bool __result)
        { if (Blocked(__originalMethod)) __result = false; }
        private static void RejectLoadContinuation(MethodBase __originalMethod)
        {
            // A reentrant saved-root or Game.OnLoad callback may latch fatal
            // inside the original body. Throw into the original outer catch
            // before later Camera/WorldGrid/offset restoration can continue.
            // Already performed native/object changes are not undone here.
            if (Blocked(__originalMethod)) throw new InvalidDataException("Fatal selected load cannot continue game deserialization.");
        }
        private static bool RejectIntPrefix(MethodBase __originalMethod, IReader reader, ref int __result)
        {
            if (Blocked(__originalMethod)) { __result = -1; return false; }
            if (!Installed || selected == null) return true;
            RequireActivationThread();
            OwnedReservoirLoadHeaderInspection? rejection;
            try
            {
                rejection = !selected.ActualHeaderVerified ? Failure(OwnedReservoirLoadInputKind.InvalidNativeSection, "Native world section reached before the actual selected header was verified.") :
                    OwnedReservoirLoadHeaderV1.InspectNativeSection(reader.RawBytes(), reader.Position, selected.Header.SelectedPath);
            }
            catch (Exception error) { rejection = Failure(OwnedReservoirLoadInputKind.InvalidNativeSection, error.GetType().Name + ": " + error.Message); }
            if (rejection == null) return true;
            RejectSelected(rejection, Name(__originalMethod) + ":native-section"); __result = -1; return false;
        }
        private static bool RejectRootPrefix(MethodBase __originalMethod, ref SaveLoadRoot? __result)
        { if (!Blocked(__originalMethod)) return true; __result = null; return false; }
        private static bool RejectVoidPrefix(MethodBase __originalMethod) => !Blocked(__originalMethod);
        private static void RejectSavePrefix(MethodBase __originalMethod)
        { if (Blocked(__originalMethod)) throw new InvalidOperationException("Save/serialization is prohibited during a fatal selected-load attempt."); }
        private static void TeardownPrefix(Game __instance, out OwnedReservoirLoadBodyInvocation? __state)
        {
            __state = null;
            if (Installed && selectedCall != null && ReferenceEquals(__instance, selectedGame) && !attempts.IsFatal)
                RejectSelected(Failure(OwnedReservoirLoadInputKind.ReadError, "Actual scene teardown reentered an active selected load."), "Game.DestroyInstances:reentrant-selected-load", requestFrontend: false);
            if (!Installed || !ReferenceEquals(__instance, ownerGame)) return;
            var attempt = attempts.Current;
            if (attempt == null) return;
            __state = BeginBodyWitness(attempt, OwnedReservoirLoadBodyKind.GameDestroyInstances, __instance);
        }
        private static OwnedReservoirLoadBodyInvocation? BeginBodyWitness(OwnedReservoirLoadAttempt attempt, OwnedReservoirLoadBodyKind kind, object owner)
        {
            RequireActivationThread();
            if (bodyInvocations.Count >= 64) { attempts.FailBodyWitness(attempt, "Body invocation stack exceeded its bound."); return null; }
            var completion = kind == OwnedReservoirLoadBodyKind.GameDestroyInstances ? attempts.BeginTeardown(attempt) : attempts.BeginPreGameBody(attempt, kind);
            var witness = new OwnedReservoirLoadBodyInvocation(attempt, kind, owner, completion);
            bodyInvocations.Add(witness); return witness;
        }
        private static void LoaderDestroyPrefix(KMonoBehaviour __instance, out OwnedReservoirLoadBodyInvocation? __state)
        {
            __state = null;
            var capture = LastPreGameSceneCapture;
            var attempt = Installed ? attempts.Current : null;
            if (attempt == null || capture == null || attempt.CompletionKind != OwnedReservoirLoadCompletionKind.PreGameLoaderScene ||
                !ReferenceEquals(attempt.Identity, capture.AttemptIdentity) || !ReferenceEquals(__instance, capture.OwnerLoader)) return;
            __state = BeginBodyWitness(attempt, OwnedReservoirLoadBodyKind.LoaderOnDestroy, capture.OwnerLoader);
        }
        private static void SceneUnloadPrefix(Scene scene, out OwnedReservoirLoadBodyInvocation? __state)
        {
            __state = null;
            var handle = scene.handle;
            var capture = LastPreGameSceneCapture;
            var attempt = Installed ? attempts.Current : null;
            if (attempt == null || capture == null || attempt.CompletionKind != OwnedReservoirLoadCompletionKind.PreGameLoaderScene ||
                !ReferenceEquals(attempt.Identity, capture.AttemptIdentity) || handle != capture.SceneHandle) return;
            __state = BeginBodyWitness(attempt, OwnedReservoirLoadBodyKind.SceneUnloaded, capture);
        }
        private static void ObserveBody(OwnedReservoirLoadBodyKind kind, object owner, bool returned)
        {
            // Instrumentation never replaces an original return or exception.
            // Ordinary bodies have no witness and remain entirely native.
            try
            {
                if (bodyInvocations.Count == 0) return;
                var witness = bodyInvocations[bodyInvocations.Count - 1];
                if (witness.Kind != kind || !ReferenceEquals(witness.Owner, owner)) return;
                RequireActivationThread();
                if (witness.Closed || (returned && !witness.Entered))
                { attempts.FailBodyWitness(witness.Attempt, "Invalid original-body entry/return sequence."); return; }
                if (returned) witness.Returned = true;
                else if (witness.Entered) attempts.FailBodyWitness(witness.Attempt, "Original body entered more than once for one invocation.");
                else witness.Entered = true;
            }
            catch (Exception)
            {
                // A failed witness cannot authorize completion. Never replace
                // an original body exception with bookkeeping/thread failure.
                if (bodyInvocations.Count != 0) bodyInvocations[bodyInvocations.Count - 1].Faulted = true;
            }
        }
        private static void GameBodyEntry(Game owner) => ObserveBody(OwnedReservoirLoadBodyKind.GameDestroyInstances, owner, false);
        private static void GameBodyReturn(Game owner) => ObserveBody(OwnedReservoirLoadBodyKind.GameDestroyInstances, owner, true);
        private static void LoaderBodyEntry(KMonoBehaviour owner) => ObserveBody(OwnedReservoirLoadBodyKind.LoaderOnDestroy, owner, false);
        private static void LoaderBodyReturn(KMonoBehaviour owner) => ObserveBody(OwnedReservoirLoadBodyKind.LoaderOnDestroy, owner, true);
        private static void SceneBodyEntry(Scene scene)
        { var capture = LastPreGameSceneCapture; if (capture != null && capture.SceneHandle == scene.handle) ObserveBody(OwnedReservoirLoadBodyKind.SceneUnloaded, capture, false); }
        private static void SceneBodyReturn(Scene scene)
        { var capture = LastPreGameSceneCapture; if (capture != null && capture.SceneHandle == scene.handle) ObserveBody(OwnedReservoirLoadBodyKind.SceneUnloaded, capture, true); }
        private static IEnumerable<CodeInstruction> BodyWitnessTranspiler(IEnumerable<CodeInstruction> instructions, MethodBase original)
        {
            string entry, returned;
            if (original == Method(typeof(Game), "DestroyInstances", typeof(void), false)) { entry = nameof(GameBodyEntry); returned = nameof(GameBodyReturn); }
            else if (original == Method(typeof(KMonoBehaviour), "OnDestroy", typeof(void), false)) { entry = nameof(LoaderBodyEntry); returned = nameof(LoaderBodyReturn); }
            else if (original == Method(typeof(SceneManager), "Internal_SceneUnloaded", typeof(void), true, typeof(Scene))) { entry = nameof(SceneBodyEntry); returned = nameof(SceneBodyReturn); }
            else throw new NotSupportedException("Unsupported original-body witness target.");
            if (original.GetMethodBody()!.ExceptionHandlingClauses.Count != 0) throw new NotSupportedException("Body witness does not accept exception regions.");
            var source = new List<CodeInstruction>(instructions);
            if (source.Count == 0) throw new NotSupportedException("Empty original body.");
            var count = 0;
            foreach (var item in source)
            {
                if (item.blocks.Count != 0 || item.opcode == OpCodes.Tailcall) throw new NotSupportedException("Unsupported body-witness composition.");
                if (item.operand is MethodInfo called && called.DeclaringType == typeof(OwnedReservoirLoadBoundary) &&
                    (called.Name == entry || called.Name == returned)) throw new NotSupportedException("Original body witness is already present.");
                if (item.opcode == OpCodes.Ret) ++count;
            }
            if (count == 0) throw new NotSupportedException("Original body has no normal return.");
            var result = new List<CodeInstruction> { new CodeInstruction(OpCodes.Ldarg_0), new CodeInstruction(OpCodes.Call, Patch(entry)) };
            foreach (var item in source)
            {
                var clone = new CodeInstruction(item.opcode, item.operand);
                foreach (var label in Labels(item)) Labels(clone).Add(label);
                if (item.opcode == OpCodes.Ret)
                {
                    var load = new CodeInstruction(OpCodes.Ldarg_0);
                    foreach (var label in Labels(clone)) Labels(load).Add(label);
                    Labels(clone).Clear(); result.Add(load); result.Add(new CodeInstruction(OpCodes.Call, Patch(returned)));
                }
                result.Add(clone);
            }
            return result;
        }
        private static void BodyPostfix(OwnedReservoirLoadBodyInvocation? __state, bool __runOriginal)
        {
            // In the pinned Harmony emitter only the normal original/skip join
            // reaches postfixes. Exception-path finalizers never replay them.
            // A pre-ret marker alone cannot certify a later transpiler throw.
            if (__state == null || __state.Closed) return;
            try
            {
                RequireActivationThread();
                if (bodyInvocations.Count == 0 || !ReferenceEquals(bodyInvocations[bodyInvocations.Count - 1], __state) || __state.NormalPathCertified)
                { __state.Faulted = true; attempts.FailBodyWitness(__state.Attempt, "Invalid normal-path certificate invocation."); return; }
                if (__runOriginal && __state.Entered && __state.Returned && !__state.Faulted) __state.NormalPathCertified = true;
            }
            catch (Exception) { __state.Faulted = true; }
        }
        private static Exception? BodyFinalizer(OwnedReservoirLoadBodyInvocation? __state, Exception? __exception)
        {
            if (__state == null || __state.Closed) return __exception;
            if (Thread.CurrentThread.ManagedThreadId != activationThread) { __state.Faulted = true; return __exception; }
            if (bodyInvocations.Count == 0 || !ReferenceEquals(bodyInvocations[bodyInvocations.Count - 1], __state))
            { attempts.FailBodyWitness(__state.Attempt, "Foreign or out-of-order original-body finalizer."); return __exception; }
            bodyInvocations.RemoveAt(bodyInvocations.Count - 1); __state.Closed = true;
            var error = __exception;
            if (error == null && (!__state.Entered || !__state.Returned || !__state.NormalPathCertified || __state.Faulted)) error = new InvalidOperationException("Original teardown body was skipped, faulted or did not reach the normal-path certificate.");
            attempts.Record(__state.Attempt, __state.Kind + ":body-witness", "Entered=" + __state.Entered + "; returned=" + __state.Returned + "; normal-path-certified=" + __state.NormalPathCertified + "; exception=" + (__exception?.GetType().Name ?? "none"));
            if (__state.Completion == null)
            { if (error != null) attempts.FailBodyWitness(__state.Attempt, "Nested original teardown failed: " + error.GetType().Name); }
            else if (__state.Kind == OwnedReservoirLoadBodyKind.GameDestroyInstances)
            { if (attempts.CompleteTeardown(__state.Completion, error)) { ownerGame = null; ownerLoader = null; } }
            else attempts.CompletePreGameBody(__state.Completion, __state.Entered, __state.Returned, error);
            TryCompletePreGameScene();
            return __exception;
        }
        private static bool LateGamePrefabPrefix(Game __instance)
        {
            var attempt = Installed ? attempts.Current : null;
            if (attempt == null) return true;
            RequireActivationThread();
            if (attempt.CompletionKind == OwnedReservoirLoadCompletionKind.PreGameLoaderScene)
                attempts.InvalidatePreGame(attempt, "Unexpected Game.OnPrefabInit was blocked; no owner was adopted.");
            attempts.Record(attempt, "Game.OnPrefabInit:blocked", "Original body did not run while fatal."); return false;
        }
        private static bool ShouldFenceEmptyAnimationGrid(bool installed, int expectedThread, int observedThread,
            OwnedReservoirLoadAttempt? attempt, OwnedReservoirPreGameSceneCapture? capture,
            KBatchedAnimUpdater instance, KBatchedAnimUpdater? singleton, Array? grid)
            => installed && expectedThread == observedThread && attempt != null && capture != null &&
                attempt.CompletionKind == OwnedReservoirLoadCompletionKind.PreGameLoaderScene && attempt.FrontendRequested &&
                ReferenceEquals(attempt.Identity, capture.AttemptIdentity) && ReferenceEquals(instance, capture.CapturedUpdater) &&
                ReferenceEquals(singleton, capture.CapturedUpdater) && grid != null && grid.Rank == 2 && grid.Length == 0;
        private static bool EmptyAnimationGridPrefix(KBatchedAnimUpdater __instance, ref bool __result)
        {
            var attempt = Installed ? attempts.Current : null;
            var capture = LastPreGameSceneCapture;
            if (attempt == null || capture == null || !ReferenceEquals(attempt.Identity, capture.AttemptIdentity) ||
                !ReferenceEquals(__instance, capture.CapturedUpdater)) return true;
            RequireActivationThread();
            var singleton = Singleton<KBatchedAnimUpdater>.Instance;
            var grid = (Array?)ControllerGridField.GetValue(__instance);
            var observedThread = Thread.CurrentThread.ManagedThreadId;
            var currentAttempt = attempts.Current;
            var currentCapture = LastPreGameSceneCapture;
            if (!ReferenceEquals(currentCapture, capture) || currentAttempt == null || !ReferenceEquals(currentAttempt.Identity, attempt.Identity) ||
                !ReferenceEquals(ownerLoader, capture.OwnerLoader) || !ReferenceEquals(UpdaterInstanceField.GetValue(null), singleton) ||
                !ShouldFenceEmptyAnimationGrid(Installed, activationThread, observedThread, currentAttempt, currentCapture, __instance, singleton, grid)) return true;
            if (EmptyAnimationGridSuppressionCount != long.MaxValue) ++EmptyAnimationGridSuppressionCount;
            LastEmptyAnimationGridSuppression = new OwnedReservoirEmptyAnimationGridSuppression(attempt, capture.CapturedUpdater,
                __instance, grid!, activationThread, observedThread, EmptyAnimationGridSuppressionCount);
            __result = false; return false;
        }
        private enum LoaderReferenceClearResult { Cleared, AlreadyNull, Foreign }
        private static LoaderReferenceClearResult ClearExactLoaderReference(ref SaveLoader? location, SaveLoader expected)
        {
            var old = Interlocked.CompareExchange(ref location, null, expected);
            return ReferenceEquals(old, expected) ? LoaderReferenceClearResult.Cleared : ReferenceEquals(old, null) ? LoaderReferenceClearResult.AlreadyNull : LoaderReferenceClearResult.Foreign;
        }
        private static bool SamePreGameFinalizationAuthority(bool installed, int expectedThread, int observedThread,
            OwnedReservoirLoadAttempt expected, OwnedReservoirLoadAttempt? current,
            OwnedReservoirPreGameSceneCapture capture, OwnedReservoirPreGameSceneCapture? currentCapture,
            SaveLoader? currentOwner, Game? observedGame, bool appIsExiting)
            => installed && expectedThread == observedThread && current != null &&
                ReferenceEquals(expected.Identity, current.Identity) && ReferenceEquals(capture, currentCapture) &&
                ReferenceEquals(current.Identity, capture.AttemptIdentity) && ReferenceEquals(currentOwner, capture.OwnerLoader) &&
                ReferenceEquals(observedGame, null) && !appIsExiting;
        private static void TryCompletePreGameScene()
        {
            var attempt = attempts.Current;
            var capture = LastPreGameSceneCapture;
            if (attempt == null || capture == null || !ReferenceEquals(attempt.Identity, capture.AttemptIdentity) || !attempts.ReadyToClearOwnerReference(attempt)) return;
            RequireActivationThread();
            // These getters can be patched. Observe them before revalidating
            // all authority and holds; no callback follows that check before CAS.
            var observedGame = Game.Instance;
            var exiting = App.IsExiting;
            var current = attempts.Current;
            if (loaderInstanceReference == null ||
                !SamePreGameFinalizationAuthority(Installed, activationThread, Thread.CurrentThread.ManagedThreadId,
                    attempt, current, capture, LastPreGameSceneCapture, ownerLoader, observedGame, exiting) ||
                !attempts.ReadyToClearOwnerReference(attempt))
            {
                if (current != null && ReferenceEquals(current.Identity, attempt.Identity))
                    attempts.InvalidatePreGame(attempt, "Exact no-Game owner authority changed before dead-loader reference cleanup.");
                return;
            }
            var result = ClearExactLoaderReference(ref loaderInstanceReference(), capture.OwnerLoader);
            attempts.Record(attempt, "SaveLoader:dead-owner-reference", "Exact backing-field CAS outcome=" + result + "; no native reset helper was invoked.");
            if (result == LoaderReferenceClearResult.Foreign || !ReferenceEquals(loaderInstanceReference(), null))
            { attempts.InvalidatePreGame(attempt, "A foreign/successor loader reference was preserved."); return; }
            if (!attempts.ReadyToClearOwnerReference(attempt)) return;
            if (attempts.CompletePreGameOwnerReference(attempt)) { ownerGame = null; ownerLoader = null; }
        }
        private static void ShutdownPostfix()
        {
            var attempt = Installed ? attempts.Current : null;
            if (attempt != null) attempts.Record(attempt, "Sim.Shutdown:observed", "Native caller completed shutdown; fatal is not released until actual Game teardown.");
        }
    }
}
