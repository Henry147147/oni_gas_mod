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
            var hooks = GuardContracts();
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
            var hooks = new List<OwnedReservoirLoadHook>();
            void Add(Type type, string name, Type result, bool isStatic, string prefix, Type[] parameters, string? postfix = null, string? finalizer = null, string? transpiler = null)
                => hooks.Add(new OwnedReservoirLoadHook(Method(type, name, result, isStatic, parameters), Patch(prefix),
                    postfix == null ? null : Patch(postfix), finalizer == null ? null : Patch(finalizer), transpiler == null ? null : Patch(transpiler)));
            Add(typeof(SaveLoader), "OnSpawn", typeof(void), false, nameof(OnSpawnPrefix), Type.EmptyTypes, transpiler: nameof(OnSpawnContinuationTranspiler));
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
            Add(typeof(Game), "DestroyInstances", typeof(void), false, nameof(TeardownPrefix), Type.EmptyTypes, finalizer: nameof(TeardownFinalizer));
            hooks.Add(new OwnedReservoirLoadHook(Method(typeof(Sim), "Shutdown", typeof(void), true), null, Patch(nameof(ShutdownPostfix))));
            return new ReadOnlyCollection<OwnedReservoirLoadHook>(hooks);
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
            ownerGame = Game.Instance; ownerLoader = loader;
            var attempt = attempts.Reject(origin, inspection, selectedCallPending: selectedCall != null);
            if (selectedCall != null) selectedFailure = attempt;
            SetFailureFields(loader, attempt); RequestFrontend(attempt); return false;
        }
        private static void RequestFrontend(OwnedReservoirLoadAttempt attempt)
        {
            if (!attempts.TryRequestFrontend(attempt)) return;
            // The flag is set before any reentrant scene callback. No native
            // Sim.Shutdown call is made, and no alternative file is loaded.
            try { KMonoBehaviour.isLoadingScene = true; SaveLoader.SetActiveSaveFilePath(null); App.LoadScene("frontend"); }
            catch (Exception error) { attempts.Record(attempt, "frontend-request:failed", error.GetType().Name + ": " + error.Message); }
        }
        private static bool OnSpawnPrefix(SaveLoader __instance)
        {
            if (!Installed) return true;
            if (attempts.IsFatal) return InspectSelection(__instance, null, "SaveLoader.OnSpawn");
            string? path;
            try { path = SaveLoader.GetActiveSaveFilePath(); }
            catch (Exception error)
            {
                RequireActivationThread(); ownerGame = Game.Instance; ownerLoader = __instance;
                var input = new OwnedReservoirLoadHeaderInspection(OwnedReservoirLoadInputKind.ReadError, "<active-save-path unavailable>", -1,
                    false, error.GetType().Name + ": " + error.Message, null, Array.Empty<byte>());
                var attempt = attempts.Reject("SaveLoader.OnSpawn", input);
                SetFailureFields(__instance, attempt); RequestFrontend(attempt); return false;
            }
            return InspectSelection(__instance, path, "SaveLoader.OnSpawn");
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
            if (attempts.Current == null) { ownerGame = selectedGame; ownerLoader = selectedLoader; }
            var attempt = attempts.Reject(origin, input, selectedCallPending: selectedCall != null);
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
            var attempt = attempts.ObserveSelectedResult(selectedCall, Name(__originalMethod) + " " + __originalMethod, __result);
            if (!__result && selectedCall != null && attempt != null)
            {
                if (!wasFatal) { ownerGame = selectedGame; ownerLoader = selectedLoader; }
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
        private static void TeardownPrefix(Game __instance, out OwnedReservoirLoadTeardown? __state)
        {
            __state = null;
            if (Installed && selectedCall != null && ReferenceEquals(__instance, selectedGame) && !attempts.IsFatal)
                RejectSelected(Failure(OwnedReservoirLoadInputKind.ReadError, "Actual scene teardown reentered an active selected load."), "Game.DestroyInstances:reentrant-selected-load", requestFrontend: false);
            if (!Installed || !ReferenceEquals(__instance, ownerGame)) return;
            var attempt = attempts.Current;
            if (attempt == null) return;
            __state = attempts.BeginTeardown(attempt);
        }
        private static Exception? TeardownFinalizer(OwnedReservoirLoadTeardown? __state, Exception? __exception)
        {
            if (__state != null && attempts.CompleteTeardown(__state, __exception)) { ownerGame = null; ownerLoader = null; }
            return __exception;
        }
        private static void ShutdownPostfix()
        {
            var attempt = Installed ? attempts.Current : null;
            if (attempt != null) attempts.Record(attempt, "Sim.Shutdown:observed", "Native caller completed shutdown; fatal is not released until actual Game teardown.");
        }
    }
}
