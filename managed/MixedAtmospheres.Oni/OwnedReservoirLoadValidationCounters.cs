using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal sealed class OwnedReservoirLoadCounterPreflight
    {
        public int ExpectedMethodCount => MethodKeys.Count;
        public int ExpectedNativeSiteSpecifications => NativeSiteCounts.Count;
        public int ExpectedNativeMapKeyCount { get; }
        public int ExpectedManagedAntecedentKeyCount { get; }
        public IReadOnlyList<string> MethodKeys { get; }
        public IReadOnlyList<string> InitializationCallerKeys { get; }
        public IReadOnlyDictionary<string, int> NativeSiteCounts { get; }
        public IReadOnlyDictionary<string, int> OriginalReturnSiteCounts { get; }
        public bool InstallInvoked => false;
        public bool HarmonyPatchInvoked => false;
        public bool NativeInvoked => false;
        internal OwnedReservoirLoadCounterPreflight(List<string> methods, List<string> initializers,
            Dictionary<string, int> sites, Dictionary<string, int> returns, int nativeKeys, int antecedentKeys)
        {
            methods.Sort(StringComparer.Ordinal); initializers.Sort(StringComparer.Ordinal);
            MethodKeys = new ReadOnlyCollection<string>(methods.ToArray()); InitializationCallerKeys = new ReadOnlyCollection<string>(initializers.ToArray());
            NativeSiteCounts = new ReadOnlyDictionary<string, int>(new SortedDictionary<string, int>(sites, StringComparer.Ordinal));
            OriginalReturnSiteCounts = new ReadOnlyDictionary<string, int>(new SortedDictionary<string, int>(returns, StringComparer.Ordinal));
            ExpectedNativeMapKeyCount = nativeKeys; ExpectedManagedAntecedentKeyCount = antecedentKeys;
        }
    }

    internal sealed class OwnedReservoirLoadCounterContractEvidence
    {
        public string Key { get; }
        public int OriginalBodyEntryCallbacks { get; }
        public int OriginalReturnSites { get; }
        public int ExpectedNativeSites { get; }
        public IReadOnlyDictionary<string, string> ObservedCallsiteTargets { get; }
        internal OwnedReservoirLoadCounterContractEvidence(string key, int returns, int sites, Dictionary<string, string> targets)
        {
            Key = key; OriginalBodyEntryCallbacks = 1; OriginalReturnSites = returns; ExpectedNativeSites = sites;
            ObservedCallsiteTargets = new ReadOnlyDictionary<string, string>(new SortedDictionary<string, string>(targets, StringComparer.Ordinal));
        }
    }

    // Private validation evidence only. Public getters make these otherwise
    // internal, immutable values serializable without a mutable JSON adapter.
    internal sealed class OwnedReservoirLoadCounterCounts
    {
        public long Invocations { get; }
        public long OriginalEligibleEntries { get; }
        public long OriginalBodyEntries { get; }
        public long OriginalBodyReturns { get; }
        public long OriginalDeserializingBodyEntries { get; }
        public long FinalizerInvocations { get; }
        public long FinalizerExceptions { get; }
        public long CleanTeardownCompletions { get; }
        internal OwnedReservoirLoadCounterCounts(long invocations, long eligible, long entries, long returns,
            long deserializing, long finalizers, long exceptions, long teardown)
        {
            Invocations = invocations; OriginalEligibleEntries = eligible; OriginalBodyEntries = entries;
            OriginalBodyReturns = returns; OriginalDeserializingBodyEntries = deserializing;
            FinalizerInvocations = finalizers; FinalizerExceptions = exceptions; CleanTeardownCompletions = teardown;
        }
        internal static OwnedReservoirLoadCounterCounts Difference(OwnedReservoirLoadCounterCounts a, OwnedReservoirLoadCounterCounts b)
            => new OwnedReservoirLoadCounterCounts(Sub(a.Invocations, b.Invocations), Sub(a.OriginalEligibleEntries, b.OriginalEligibleEntries),
                Sub(a.OriginalBodyEntries, b.OriginalBodyEntries), Sub(a.OriginalBodyReturns, b.OriginalBodyReturns),
                Sub(a.OriginalDeserializingBodyEntries, b.OriginalDeserializingBodyEntries), Sub(a.FinalizerInvocations, b.FinalizerInvocations),
                Sub(a.FinalizerExceptions, b.FinalizerExceptions), Sub(a.CleanTeardownCompletions, b.CleanTeardownCompletions));
        internal static long Sub(long before, long after)
        {
            if (before < 0 || after < before) throw new InvalidOperationException("Counter history is not monotonic.");
            return after - before;
        }
    }

    internal sealed class OwnedReservoirLoadCounterSnapshot
    {
        internal object Identity { get; }
        public bool Installed { get; }
        public long Sequence { get; }
        public int CapturedThreadId { get; }
        public string CapturedUtc { get; }
        public bool Saturated { get; }
        public long ObservationFailures { get; }
        public long ObserverErrorCount { get; }
        public long ObserverErrorsDropped => ObserverErrorCount - ObserverErrors.Count;
        public IReadOnlyList<string> ObserverErrors { get; }
        public IReadOnlyDictionary<string, OwnedReservoirLoadCounterCounts> Counts { get; }
        public IReadOnlyDictionary<string, OwnedReservoirLoadCounterContractEvidence> ContractEvidence { get; }
        public int ExpectedMethodCount => ContractEvidence.Count;
        public int ExpectedNativeCallsiteKeyCount => NativeCallsiteEntries.Count;
        public int ExpectedManagedAntecedentKeyCount => ManagedNativeDispatchAntecedents.Count;
        public IReadOnlyDictionary<string, long> NativeCallsiteEntries { get; }
        public IReadOnlyDictionary<string, long> ManagedNativeDispatchAntecedents { get; }
        public IReadOnlyDictionary<string, long> OriginalEligibleSceneRequests { get; }
        public IReadOnlyDictionary<string, long> OriginalBodySceneRequests { get; }
        public IReadOnlyList<string> CoverageNotes { get; }
        internal OwnedReservoirLoadCounterSnapshot(object identity, bool installed, long sequence, bool saturated,
            long failures, long observerCount, IReadOnlyList<string> observerErrors,
            IReadOnlyDictionary<string, OwnedReservoirLoadCounterCounts> counts,
            IReadOnlyDictionary<string, OwnedReservoirLoadCounterContractEvidence> evidence, IReadOnlyDictionary<string, long> native,
            IReadOnlyDictionary<string, long> antecedents, IReadOnlyDictionary<string, long> eligibleScenes,
            IReadOnlyDictionary<string, long> bodyScenes, IReadOnlyList<string> notes)
        {
            Identity = identity; Installed = installed; Sequence = sequence; Saturated = saturated;
            ObservationFailures = failures; ObserverErrorCount = observerCount; ObserverErrors = observerErrors;
            Counts = counts; ContractEvidence = evidence; NativeCallsiteEntries = native; ManagedNativeDispatchAntecedents = antecedents;
            OriginalEligibleSceneRequests = eligibleScenes; OriginalBodySceneRequests = bodyScenes; CoverageNotes = notes;
            CapturedThreadId = Thread.CurrentThread.ManagedThreadId;
            CapturedUtc = System.DateTime.UtcNow.ToString("O", System.Globalization.CultureInfo.InvariantCulture);
        }
    }

    internal sealed class OwnedReservoirLoadCounterDelta
    {
        public long BeforeSequence { get; }
        public long AfterSequence { get; }
        public long ObservationFailures { get; }
        public long ObserverErrorCount { get; }
        public bool CompleteContractVerified { get; }
        public int ExpectedMethodCount { get; }
        public int ExpectedNativeCallsiteKeyCount { get; }
        public int ExpectedManagedAntecedentKeyCount { get; }
        public IReadOnlyDictionary<string, OwnedReservoirLoadCounterCounts> Counts { get; }
        public IReadOnlyDictionary<string, long> NativeCallsiteEntries { get; }
        public IReadOnlyDictionary<string, long> ManagedNativeDispatchAntecedents { get; }
        public IReadOnlyDictionary<string, long> OriginalEligibleSceneRequests { get; }
        public IReadOnlyDictionary<string, long> OriginalBodySceneRequests { get; }
        internal OwnedReservoirLoadCounterDelta(OwnedReservoirLoadCounterSnapshot before, OwnedReservoirLoadCounterSnapshot after)
        {
            if (!ReferenceEquals(before.Identity, after.Identity) || !before.Installed || !after.Installed
                || before.Saturated || after.Saturated) throw new InvalidOperationException("Snapshots must belong to one installed, unsaturated counter history.");
            OwnedReservoirLoadCounterCounts.Sub(before.Sequence, after.Sequence);
            BeforeSequence = before.Sequence; AfterSequence = after.Sequence;
            ObservationFailures = OwnedReservoirLoadCounterCounts.Sub(before.ObservationFailures, after.ObservationFailures);
            ObserverErrorCount = OwnedReservoirLoadCounterCounts.Sub(before.ObserverErrorCount, after.ObserverErrorCount);
            var rows = new SortedDictionary<string, OwnedReservoirLoadCounterCounts>(StringComparer.Ordinal);
            if (before.Counts.Count != after.Counts.Count) throw new InvalidOperationException("Instrumentation changed between snapshots.");
            foreach (var row in before.Counts)
            {
                if (!after.Counts.TryGetValue(row.Key, out var value)) throw new InvalidOperationException("Instrumentation changed between snapshots.");
                rows.Add(row.Key, OwnedReservoirLoadCounterCounts.Difference(row.Value, value));
            }
            Counts = new ReadOnlyDictionary<string, OwnedReservoirLoadCounterCounts>(rows);
            NativeCallsiteEntries = Difference(before.NativeCallsiteEntries, after.NativeCallsiteEntries);
            ManagedNativeDispatchAntecedents = Difference(before.ManagedNativeDispatchAntecedents, after.ManagedNativeDispatchAntecedents);
            OriginalEligibleSceneRequests = Difference(before.OriginalEligibleSceneRequests, after.OriginalEligibleSceneRequests);
            OriginalBodySceneRequests = Difference(before.OriginalBodySceneRequests, after.OriginalBodySceneRequests);
            ExpectedMethodCount = after.ExpectedMethodCount; ExpectedNativeCallsiteKeyCount = after.ExpectedNativeCallsiteKeyCount;
            ExpectedManagedAntecedentKeyCount = after.ExpectedManagedAntecedentKeyCount;
            if (Counts.Count != ExpectedMethodCount || before.ContractEvidence.Count != ExpectedMethodCount
                || NativeCallsiteEntries.Count != before.NativeCallsiteEntries.Count
                || ManagedNativeDispatchAntecedents.Count != before.ManagedNativeDispatchAntecedents.Count)
                throw new InvalidOperationException("The complete installed contract was not preserved.");
            foreach (var row in after.ContractEvidence)
            {
                if (!before.ContractEvidence.TryGetValue(row.Key, out var previous) || !Counts.ContainsKey(row.Key)
                    || previous.OriginalBodyEntryCallbacks != 1 || row.Value.OriginalBodyEntryCallbacks != 1
                    || previous.OriginalReturnSites != row.Value.OriginalReturnSites || row.Value.OriginalReturnSites <= 0
                    || previous.ExpectedNativeSites != row.Value.ExpectedNativeSites
                    || previous.ObservedCallsiteTargets.Count != row.Value.ObservedCallsiteTargets.Count)
                    throw new InvalidOperationException("Original-body/callsite instrumentation changed between snapshots.");
                foreach (var site in previous.ObservedCallsiteTargets)
                    if (!row.Value.ObservedCallsiteTargets.TryGetValue(site.Key, out var target) || target != site.Value)
                        throw new InvalidOperationException("Native callsite instrumentation changed between snapshots.");
            }
            CompleteContractVerified = true;
        }
        private static IReadOnlyDictionary<string, long> Difference(IReadOnlyDictionary<string, long> before, IReadOnlyDictionary<string, long> after)
        {
            var values = new SortedDictionary<string, long>(StringComparer.Ordinal);
            foreach (var row in after)
            { before.TryGetValue(row.Key, out var old); values.Add(row.Key, OwnedReservoirLoadCounterCounts.Sub(old, row.Value)); }
            foreach (var row in before) if (!after.ContainsKey(row.Key)) throw new InvalidOperationException("Counter history lost a key.");
            return new ReadOnlyDictionary<string, long>(values);
        }
    }

    // No discovery attributes, ordinary-use activation, file writer, load/save
    // permission, native call or mutation of original arguments/results/errors.
    // Root's explicit private driver alone may Install and attach the observer.
    internal static class OwnedReservoirLoadValidationCounters
    {
        private sealed class MutableCounts
        {
            internal long Invocations, Eligible, Entries, Returns, Deserializing, Finalizers, Exceptions, Teardowns;
            internal OwnedReservoirLoadCounterCounts Capture() => new OwnedReservoirLoadCounterCounts(Invocations, Eligible,
                Entries, Returns, Deserializing, Finalizers, Exceptions, Teardowns);
        }
        private sealed class Site
        {
            internal readonly MethodInfo Native;
            internal readonly MethodInfo? ManagedAlternative;
            internal readonly string Key;
            internal readonly bool MessageArgument;
            internal Site(MethodInfo caller, MethodInfo native, MethodInfo? alternative = null, bool messageArgument = false)
            { Native = native; ManagedAlternative = alternative; Key = Signature(caller) + " => " + Signature(native); MessageArgument = messageArgument; }
        }
        private sealed class Contract
        {
            internal readonly MethodInfo Method;
            internal readonly string Key;
            internal readonly List<Site> Sites = new List<Site>();
            internal OwnedReservoirLoadCounterContractEvidence? Evidence;
            internal Contract(MethodInfo method) { Method = method; Key = Signature(method); }
        }
        // Per-invocation state is observational only and never returned to game
        // code. It distinguishes skipped-original finalizers from real teardown.
        private sealed class Invocation
        {
            internal readonly string Key;
            internal bool BodyEntered, BodyReturned;
            internal Invocation(string key) { Key = key; }
        }

        private static readonly object Gate = new object();
        private static readonly object Identity = new object();
        private static readonly Dictionary<MethodBase, Contract> Contracts = new Dictionary<MethodBase, Contract>();
        private static readonly Dictionary<string, MutableCounts> Counts = new Dictionary<string, MutableCounts>(StringComparer.Ordinal);
        private static readonly Dictionary<string, long> NativeSites = new Dictionary<string, long>(StringComparer.Ordinal);
        private static readonly Dictionary<string, long> AntecedentSites = new Dictionary<string, long>(StringComparer.Ordinal);
        private static readonly Dictionary<string, long> EligibleScenes = new Dictionary<string, long>(StringComparer.Ordinal);
        private static readonly Dictionary<string, long> BodyScenes = new Dictionary<string, long>(StringComparer.Ordinal);
        private static readonly List<string> ObserverErrors = new List<string>();
        private static readonly OpCode[] OneByteCodes = new OpCode[256], TwoByteCodes = new OpCode[256];
        private static readonly MethodInfo AttemptMethod = Patch(nameof(AttemptPrefix));
        private static readonly MethodInfo EligibilityMethod = Patch(nameof(EligibilityPrefix));
        private static readonly MethodInfo SceneEligibilityMethod = Patch(nameof(SceneEligibilityPrefix));
        private static readonly MethodInfo FinalizerMethod = Patch(nameof(Finalizer));
        private static readonly MethodInfo TranspilerMethod = Patch(nameof(Transpiler));
        private static readonly MethodInfo EntryMethod = Patch(nameof(BodyEntry));
        private static readonly MethodInfo ReturnMethod = Patch(nameof(BodyReturn));
        private static readonly MethodInfo StorageEntryMethod = Patch(nameof(StorageSerializeEntry));
        private static readonly MethodInfo StoreEntryMethod = Patch(nameof(StoreEntry));
        private static readonly MethodInfo SceneEntryMethod = Patch(nameof(SceneEntry));
        private static readonly MethodInfo NativeSiteMethod = Patch(nameof(NativeSiteEntry));
        private static readonly MethodInfo NativeMessageSiteMethod = Patch(nameof(NativeMessageSiteEntry));
        private static readonly MethodInfo AntecedentMethod = Patch(nameof(AntecedentEntry));
        // The shipped assembly types labels through Unity's mscorlib; the
        // netstandard facade cannot import that generic Label signature.
        private static readonly FieldInfo LabelField = AccessTools.DeclaredField(typeof(CodeInstruction), "labels")
            ?? throw new NotSupportedException("Pinned Harmony instruction labels are absent.");
        private static readonly IReadOnlyList<string> Notes = new ReadOnlyCollection<string>(new[]
        {
            "Private manually installed observations; no rejection boundary is installed by this helper.",
            "Invocations counts a no-throw prefix at Priority.First+1; an earlier throwing foreign prefix can prevent observation.",
            "OriginalEligibleEntries samples __runOriginal at Priority.Last; later foreign prefix changes are not body execution.",
            "OriginalBodyEntries and Returns are transpiler callbacks inside the original body, bypassed by Harmony skip-original.",
            "CleanTeardownCompletions requires this invocation's real body entry/normal return and a null exception in its finalizer; it is not a frontend-ready predicate.",
            "NativeCallsiteEntries observes immediately before an exact native import call, not successful native completion.",
            "ManagedNativeDispatchAntecedents distinguishes an existing NativeMessageBoundary.Handle rewrite from a direct native import.",
            "SIM_Initialize, SIM_HandleMessage, SIM_Shutdown, SIM_BeginSave and SIM_EndSave are externs and are never body-patched.",
            "All initialization sites in the pinned Assembly-CSharp are checked against three exact SaveLoader callers and ProcGenGame.WorldGenSimUtil.DoSettleSim; other assemblies/mods are outside this closure.",
            "Scene names are requests, not actual scene completion. Iterator MoveNext entries/returns are resumptions, not successful saves.",
            "Storage observer errors and instrumentation failures never escape into the game; nonzero diagnostics invalidate acceptance evidence.",
            "Per-invocation tickets allocate in this private diagnostic helper. No production performance claim is made."
        });
        [ThreadStatic] private static List<Invocation>? invocations;
        private static System.Action<Storage>? storageObserver;
        private static string? owner;
        private static int installationThread;
        private static bool installed, installationFailed, saturated;
        private static long sequence, observationFailures, observerErrorCount;
        private static string storageSerializeKey = "", storeKey = "", sceneKey = "", teardownKey = "";

        static OwnedReservoirLoadValidationCounters()
        {
            foreach (var field in typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static))
            {
                if (!(field.GetValue(null) is OpCode code)) continue;
                var value = unchecked((ushort)code.Value);
                if (value < 256) OneByteCodes[value] = code;
                else if ((value & 0xff00) == 0xfe00) TwoByteCodes[value & 255] = code;
            }
        }
        internal static bool Installed => Volatile.Read(ref installed);
        internal static System.Action<Storage>? StorageSerializeBodyObserved
        { get => Volatile.Read(ref storageObserver); set => Volatile.Write(ref storageObserver, value); }
        internal static string Key(Type declaringType, string name, params Type[] parameterTypes)
        {
            var method = AccessTools.DeclaredMethod(declaringType, name, parameterTypes);
            return method != null ? Signature(method) : throw new NotSupportedException("Exact counter method is absent: " + declaringType.FullName + "." + name);
        }
        internal static string CoroutineMoveNextKey(string name, params Type[] parameterTypes)
            => Signature(IteratorMoveNext(Exact(typeof(Game), name, typeof(IEnumerator), false, parameterTypes)));
        internal static OwnedReservoirLoadCounterDelta Delta(OwnedReservoirLoadCounterSnapshot before, OwnedReservoirLoadCounterSnapshot after)
            => new OwnedReservoirLoadCounterDelta(before ?? throw new ArgumentNullException(nameof(before)), after ?? throw new ArgumentNullException(nameof(after)));

        // Pure licensed-IL preflight, available before patch installation. This
        // builds no runtime counter registry, invokes no inspected method, and
        // does not rely on Harmony's unsupported .NET10 Patch/BinaryFormatter
        // path. The real Mono installation/composed transpiler still needs its
        // separate acceptance; this is declaration and original-IL validation.
        internal static OwnedReservoirLoadCounterPreflight InspectContracts()
        {
            if (Installed || Contracts.Count != 0) throw new InvalidOperationException("Pure counter preflight must precede installation.");
            RequireHash(typeof(SaveLoader).Assembly, OwnedReservoirLoadHeaderV1.AssemblyCSharpSha256);
            RequireHash(typeof(IReader).Assembly, OwnedReservoirLoadHeaderV1.FirstpassSha256);
            RequireHash(typeof(Harmony).Assembly, "aec7446028fe31d00dfecc684e40011c4208bb036e82617b8de32002a8e55b53");
            var contracts = BuildContracts(); var methods = new List<string>(); var initializers = new List<string>();
            var sites = new Dictionary<string, int>(StringComparer.Ordinal); var returns = new Dictionary<string, int>(StringComparer.Ordinal);
            var initialize = Import(typeof(Sim), "SIM_Initialize", typeof(void), typeof(Sim.GAME_MessageHandler));
            var nativeKeys = 0; var antecedentKeys = 0;
            foreach (var contract in contracts)
            {
                methods.Add(contract.Key); var count = 0;
                foreach (var instruction in ReadInstructions(contract.Method))
                {
                    if (instruction.opcode == OpCodes.Tailcall) throw new NotSupportedException("Private body-return instrumentation does not support tail calls.");
                    if (instruction.opcode == OpCodes.Ret) ++count;
                }
                if (count == 0) throw new NotSupportedException("Private body-return instrumentation needs a normal return: " + contract.Key);
                returns.Add(contract.Key, count);
                foreach (var site in contract.Sites)
                {
                    sites.Add(site.Key, 1); nativeKeys += site.MessageArgument ? 4 : 1;
                    if (site.ManagedAlternative != null) ++antecedentKeys;
                    if (site.Native == initialize) initializers.Add(contract.Key);
                }
            }
            if (initializers.Count != 4 || sites.Count != 12 || nativeKeys != 15 || antecedentKeys != 4)
                throw new NotSupportedException("The exact private counter/native initialization contract changed.");
            return new OwnedReservoirLoadCounterPreflight(methods, initializers, sites, returns, nativeKeys, antecedentKeys);
        }

        internal static OwnedReservoirLoadCounterSnapshot Capture()
        {
            if (!Installed) throw new InvalidOperationException("Private load counters have not been installed.");
            VerifyInstalled();
            lock (Gate)
            {
                var rows = new SortedDictionary<string, OwnedReservoirLoadCounterCounts>(StringComparer.Ordinal);
                var evidence = new SortedDictionary<string, OwnedReservoirLoadCounterContractEvidence>(StringComparer.Ordinal);
                foreach (var row in Counts) rows.Add(row.Key, row.Value.Capture());
                foreach (var contract in Contracts.Values) evidence.Add(contract.Key, contract.Evidence
                    ?? throw new InvalidOperationException("Original-body instrumentation evidence is absent."));
                return new OwnedReservoirLoadCounterSnapshot(Identity, installed, sequence, saturated,
                    Interlocked.Read(ref observationFailures), observerErrorCount, new ReadOnlyCollection<string>(ObserverErrors.ToArray()),
                    new ReadOnlyDictionary<string, OwnedReservoirLoadCounterCounts>(rows),
                    new ReadOnlyDictionary<string, OwnedReservoirLoadCounterContractEvidence>(evidence), Copy(NativeSites), Copy(AntecedentSites),
                    Copy(EligibleScenes), Copy(BodyScenes), Notes);
            }
        }
        private static IReadOnlyDictionary<string, long> Copy(Dictionary<string, long> source)
            => new ReadOnlyDictionary<string, long>(new SortedDictionary<string, long>(source, StringComparer.Ordinal));

        internal static void Install(Harmony harmony)
        {
            if (harmony == null) throw new ArgumentNullException(nameof(harmony));
            if (Installed)
            {
                if (owner != harmony.Id || installationThread != Thread.CurrentThread.ManagedThreadId)
                    throw new InvalidOperationException("Private counters already belong to another owner/thread.");
                VerifyInstalled(); return;
            }
            if (installationFailed) throw new InvalidOperationException("A failed counter installation is not retryable in this process.");
            RequireHash(typeof(SaveLoader).Assembly, OwnedReservoirLoadHeaderV1.AssemblyCSharpSha256);
            RequireHash(typeof(IReader).Assembly, OwnedReservoirLoadHeaderV1.FirstpassSha256);
            RequireHash(typeof(Harmony).Assembly, "aec7446028fe31d00dfecc684e40011c4208bb036e82617b8de32002a8e55b53");
            // This exact shipped Harmony declares InjectionType.RunOriginal;
            // MethodCreator allocates/initializes it, AddPrefixes runs void
            // state/MethodBase/value-bool observers even after skip-original,
            // and MethodCreatorTools emits a value load for __runOriginal.
            var injection = typeof(Harmony).Assembly.GetType("HarmonyLib.InjectionType", true)!;
            if (!injection.IsEnum || !Enum.IsDefined(injection, "RunOriginal")) throw new NotSupportedException("Pinned Harmony lacks run-original injection.");
            var targets = BuildContracts();
            storageSerializeKey = Key(typeof(Storage), "Serialize", typeof(BinaryWriter));
            storeKey = Key(typeof(Storage), "Store", typeof(GameObject), typeof(bool), typeof(bool), typeof(bool), typeof(bool));
            sceneKey = Key(typeof(App), "LoadScene", typeof(string)); teardownKey = Key(typeof(Game), "DestroyInstances");
            var patched = new List<MethodInfo>();
            try
            {
                foreach (var contract in targets)
                {
                    Contracts.Add(contract.Method, contract); Counts.Add(contract.Key, new MutableCounts());
                    foreach (var site in contract.Sites)
                    {
                        NativeSites.Add(site.Key, 0);
                        if (site.ManagedAlternative != null) AntecedentSites.Add(site.Key, 0);
                        if (site.MessageArgument)
                            foreach (var suffix in new[] { " [message=-672538170]", " [message=-931446686]", " [message=other]" }) NativeSites.Add(site.Key + suffix, 0);
                    }
                }
                owner = harmony.Id; installationThread = Thread.CurrentThread.ManagedThreadId;
                foreach (var contract in targets)
                {
                    patched.Add(contract.Method);
                    harmony.Patch(contract.Method, new HarmonyMethod(AttemptMethod) { priority = Priority.First + 1 },
                        null, new HarmonyMethod(TranspilerMethod) { priority = Priority.Last },
                        new HarmonyMethod(FinalizerMethod) { priority = Priority.Last });
                    harmony.Patch(contract.Method, new HarmonyMethod(contract.Key == sceneKey ? SceneEligibilityMethod : EligibilityMethod) { priority = Priority.Last });
                }
                VerifyInstalled(); Volatile.Write(ref installed, true);
            }
            catch
            {
                installationFailed = true;
                foreach (var target in patched)
                    foreach (var method in new[] { AttemptMethod, EligibilityMethod, SceneEligibilityMethod, TranspilerMethod, FinalizerMethod })
                        harmony.Unpatch(target, method);
                throw;
            }
        }

        private static List<Contract> BuildContracts()
        {
            var values = new List<Contract>();
            Contract Add(MethodInfo method)
            {
                foreach (var previous in values) if (previous.Method == method) return previous;
                if (method.GetMethodBody() == null || method.ContainsGenericParameters) throw new NotSupportedException("Counter target has no concrete managed body: " + Signature(method));
                var added = new Contract(method); values.Add(added); return added;
            }
            foreach (var hook in OwnedReservoirLoadBoundary.GuardContracts()) Add(hook.Target);
            Add(Exact(typeof(Storage), "Serialize", typeof(void), false, typeof(BinaryWriter)));
            Add(Exact(typeof(Storage), "Deserialize", typeof(void), false, typeof(IReader)));
            Add(Exact(typeof(Storage), "Store", typeof(GameObject), false, typeof(GameObject), typeof(bool), typeof(bool), typeof(bool), typeof(bool)));
            Add(Exact(typeof(SaveManager), "Save", typeof(void), false, typeof(BinaryWriter)));
            Add(Exact(typeof(SaveManager), "ClearScene", typeof(void), false));
            Add(Exact(typeof(SaveLoadRoot), "LoadInternal", typeof(void), true, typeof(GameObject), typeof(IReader)));
            Add(Exact(typeof(SaveLoader), "DestroyInstance", typeof(void), true));
            Add(Exact(typeof(MainMenu), "OnSpawn", typeof(void), false));
            Add(Exact(typeof(App), "LoadScene", typeof(void), true, typeof(string)));
            Add(Exact(typeof(KSerialization.Manager), "Clear", typeof(void), true));
            Add(Exact(typeof(KSerialization.Manager), "DeserializeDirectory", typeof(void), true, typeof(IReader)));
            Add(Exact(typeof(Sim), "HandleMessage", typeof(IntPtr), true, typeof(SimMessageHashes), typeof(int), typeof(byte[])));
            foreach (var factory in new[]
            {
                Exact(typeof(Game), "DelayedInitialSave", typeof(IEnumerator), false),
                Exact(typeof(Game), "DelayedSave", typeof(IEnumerator), false, typeof(string), typeof(bool), typeof(bool))
            }) { Add(factory); Add(IteratorMoveNext(factory)); }

            var initialize = Import(typeof(Sim), "SIM_Initialize", typeof(void), typeof(Sim.GAME_MessageHandler));
            var handle = Import(typeof(Sim), "SIM_HandleMessage", typeof(IntPtr), typeof(int), typeof(int), typeof(byte).MakePointerType());
            var shutdown = Import(typeof(Sim), "SIM_Shutdown", typeof(void));
            var beginSave = Import(typeof(Sim), "SIM_BeginSave", typeof(byte).MakePointerType(), typeof(int).MakePointerType(), typeof(int), typeof(int));
            var endSave = Import(typeof(Sim), "SIM_EndSave", typeof(void));
            var boundary = Exact(typeof(NativeMessageBoundary), "Handle", typeof(IntPtr), true, typeof(int), typeof(int), typeof(byte).MakePointerType());
            var initializationCallers = new[]
            {
                Exact(typeof(SaveLoader), "OnSpawn", typeof(void), false),
                Exact(typeof(SaveLoader), "Load", typeof(bool), false, typeof(IReader)),
                Exact(typeof(SaveLoader), "LoadFromWorldGen", typeof(bool), false),
                Exact(DeclaredType(typeof(SaveLoader).Assembly, "ProcGenGame.WorldGenSimUtil"), "DoSettleSim", typeof(bool), true,
                    DeclaredType(typeof(IReader).Assembly, "ProcGen.WorldGenSettings"), typeof(BinaryWriter), typeof(uint),
                    DeclaredType(typeof(SaveLoader).Assembly, "ProcGenGame.WorldgenSimData").MakeByRefType(),
                    DeclaredType(typeof(SaveLoader).Assembly, "ProcGenGame.WorldGen+OfflineCallbackFunction"),
                    DeclaredType(typeof(SaveLoader).Assembly, "Klei.Data"),
                    typeof(List<>).MakeGenericType(DeclaredType(typeof(SaveLoader).Assembly, "ProcGenGame.TemplateSpawning+TemplateSpawner")),
                    typeof(System.Action<>).MakeGenericType(DeclaredType(typeof(SaveLoader).Assembly, "OfflineWorldGen+ErrorInfo")), typeof(int))
            };
            VerifyInitializationClosure(initialize, initializationCallers);
            foreach (var caller in initializationCallers) Add(caller).Sites.Add(new Site(caller, initialize));
            foreach (var caller in new[]
            {
                Exact(typeof(Sim), "LoadWorld", typeof(int), true, typeof(IReader)),
                Exact(typeof(Sim), "Load", typeof(int), true, typeof(IReader)),
                Exact(typeof(Sim), "Start", typeof(void), true),
                Exact(typeof(Sim), "HandleMessage", typeof(IntPtr), true, typeof(SimMessageHashes), typeof(int), typeof(byte[]))
            }) Add(caller).Sites.Add(new Site(caller, handle, boundary));
            var save = Exact(typeof(Sim), "Save", typeof(void), true, typeof(BinaryWriter), typeof(int), typeof(int));
            Add(save).Sites.Add(new Site(save, beginSave)); Add(save).Sites.Add(new Site(save, endSave));
            var stop = Exact(typeof(Sim), "Shutdown", typeof(void), true); Add(stop).Sites.Add(new Site(stop, shutdown));
            Add(boundary).Sites.Add(new Site(boundary, handle, messageArgument: true));
            foreach (var contract in values)
            {
                var original = ReadInstructions(contract.Method);
                foreach (var site in contract.Sites)
                {
                    var count = 0; foreach (var instruction in original) if (Calls(instruction, site.Native)) ++count;
                    if (count != 1) throw new NotSupportedException("Expected exactly one native call site: " + site.Key + "; got " + count);
                }
            }
            if (values.Count != 41) throw new NotSupportedException("Expected exactly41 private managed counter targets.");
            values.Sort((a, b) => StringComparer.Ordinal.Compare(a.Key, b.Key)); return values;
        }

        private static MethodInfo IteratorMoveNext(MethodInfo factory)
        {
            var state = factory.GetCustomAttribute<IteratorStateMachineAttribute>()?.StateMachineType;
            if (state == null || state.DeclaringType != typeof(Game) || !typeof(IEnumerator).IsAssignableFrom(state))
                throw new NotSupportedException("Pinned save iterator state machine is absent: " + Signature(factory));
            return Exact(state, "MoveNext", typeof(bool), false);
        }
        private static MethodInfo Exact(Type type, string name, Type result, bool isStatic, params Type[] parameters)
        {
            var method = AccessTools.DeclaredMethod(type, name, parameters);
            if (method == null || method.ReturnType != result || method.IsStatic != isStatic || method.GetMethodBody() == null)
                throw new NotSupportedException("Exact private counter contract differs: " + type.FullName + "." + name);
            return method;
        }
        private static Type DeclaredType(Assembly assembly, string fullName)
        {
            var type = assembly.GetType(fullName, false);
            if (type == null || type.Assembly != assembly || type.FullName != fullName || type.ContainsGenericParameters)
                throw new NotSupportedException("Exact private counter type is absent: " + fullName);
            return type;
        }
        private static MethodInfo Import(Type type, string name, Type result, params Type[] parameters)
        {
            var method = AccessTools.DeclaredMethod(type, name, parameters);
            var import = method?.GetCustomAttribute<DllImportAttribute>();
            if (method == null || !method.IsStatic || method.ReturnType != result || method.GetMethodBody() != null || import?.Value != "SimDLL")
                throw new NotSupportedException("Exact native observation contract differs: " + type.FullName + "." + name);
            return method;
        }
        private static string TypeName(Type type)
        {
            if (type.IsByRef) return TypeName(type.GetElementType()!) + "&";
            if (type.IsPointer) return TypeName(type.GetElementType()!) + "*";
            if (type.IsArray) return TypeName(type.GetElementType()!) + "[" + new string(',', type.GetArrayRank() - 1) + "]";
            return type.FullName ?? throw new NotSupportedException("Open/unnamed counter signature type.");
        }
        private static string Signature(MethodInfo method)
        {
            var parameters = method.GetParameters(); var names = new string[parameters.Length];
            for (var i = 0; i < names.Length; ++i) names[i] = TypeName(parameters[i].ParameterType);
            return TypeName(method.DeclaringType!) + "." + method.Name + "(" + string.Join(",", names) + ") -> " + TypeName(method.ReturnType);
        }
        private static MethodInfo Patch(string name) => AccessTools.DeclaredMethod(typeof(OwnedReservoirLoadValidationCounters), name)
            ?? throw new MissingMethodException(typeof(OwnedReservoirLoadValidationCounters).Name, name);
        private static void RequireHash(Assembly assembly, string expected)
        {
            using var file = File.OpenRead(assembly.Location); using var sha = SHA256.Create();
            if (BitConverter.ToString(sha.ComputeHash(file)).Replace("-", "").ToLowerInvariant() != expected)
                throw new NotSupportedException("Private load counters require the pinned assembly: " + assembly.GetName().Name);
        }
        private static List<CodeInstruction> ReadInstructions(MethodBase method)
        {
            MethodInfo? reader = null;
            foreach (var candidate in typeof(PatchProcessor).GetMethods(BindingFlags.Public | BindingFlags.Static))
            {
                var parameters = candidate.GetParameters();
                if (candidate.Name == "GetOriginalInstructions" && parameters.Length == 2 && parameters[0].ParameterType == typeof(MethodBase) && !parameters[1].ParameterType.IsByRef)
                { if (reader != null) throw new NotSupportedException("Ambiguous pinned Harmony IL reader."); reader = candidate; }
            }
            if (reader == null) throw new NotSupportedException("Pinned Harmony IL reader is absent.");
            var instructions = (IEnumerable<CodeInstruction>)(reader.Invoke(null, new object?[] { method, null })
                ?? throw new NotSupportedException("Pinned method has no readable instructions."));
            return new List<CodeInstruction>(instructions);
        }
        private static bool Calls(CodeInstruction instruction, MethodInfo target)
            => (instruction.opcode == OpCodes.Call || instruction.opcode == OpCodes.Callvirt) && instruction.operand is MethodInfo method && method == target;

        // Decode instruction boundaries, never search arbitrary bytes for a
        // token. This covers constructors and open generic callers without
        // asking MonoMod to construct a dynamic open-generic instruction reader.
        private static void VerifyInitializationClosure(MethodInfo native, MethodInfo[] expected)
        {
            var found = new HashSet<MethodBase>();
            foreach (var type in native.DeclaringType!.Assembly.GetTypes())
            {
                var methods = new List<MethodBase>();
                methods.AddRange(type.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance | BindingFlags.DeclaredOnly));
                methods.AddRange(type.GetConstructors(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance | BindingFlags.DeclaredOnly));
                foreach (var method in methods)
                {
                    var bytes = method.GetMethodBody()?.GetILAsByteArray(); if (bytes == null) continue;
                    var matches = 0; var position = 0;
                    while (position < bytes.Length)
                    {
                        var first = bytes[position++];
                        if (first == 0xfe && position >= bytes.Length) throw new NotSupportedException("Truncated pinned IL.");
                        var code = first == 0xfe ? TwoByteCodes[bytes[position++]] : OneByteCodes[first];
                        if (code.Size == 0) throw new NotSupportedException("Unknown pinned IL opcode.");
                        var operand = position; int size;
                        switch (code.OperandType)
                        {
                            case OperandType.InlineNone: size = 0; break;
                            case OperandType.ShortInlineBrTarget: case OperandType.ShortInlineI: case OperandType.ShortInlineVar: size = 1; break;
                            case OperandType.InlineVar: size = 2; break;
                            case OperandType.InlineI8: case OperandType.InlineR: size = 8; break;
                            case OperandType.InlineSwitch:
                                if (bytes.Length - position < 4) throw new NotSupportedException("Truncated pinned IL switch.");
                                var cases = BitConverter.ToInt32(bytes, position);
                                if (cases < 0 || cases > (bytes.Length - position - 4) / 4) throw new NotSupportedException("Invalid pinned IL switch.");
                                size = 4 + 4 * cases; break;
                            case OperandType.InlineBrTarget: case OperandType.InlineField: case OperandType.InlineI:
                            case OperandType.InlineMethod: case OperandType.InlineSig: case OperandType.InlineString:
                            case OperandType.InlineTok: case OperandType.InlineType: case OperandType.ShortInlineR: size = 4; break;
                            default: throw new NotSupportedException("Unsupported pinned IL operand.");
                        }
                        if (size > bytes.Length - position) throw new NotSupportedException("Truncated pinned IL operand.");
                        if (code == OpCodes.Call && BitConverter.ToInt32(bytes, operand) == native.MetadataToken) ++matches;
                        position += size;
                    }
                    if (matches == 0) continue;
                    if (matches != 1 || !(method is MethodInfo info) || Array.IndexOf(expected, info) < 0)
                        throw new NotSupportedException("Unexpected pinned initialization caller: " + method);
                    found.Add(method);
                }
            }
            if (found.Count != expected.Length) throw new NotSupportedException("Pinned native initialization closure changed.");
        }

        private static void VerifyInstalled()
        {
            foreach (var contract in Contracts.Values)
            {
                var patches = Harmony.GetPatchInfo(contract.Method) ?? throw new InvalidOperationException("Counter target is no longer patched: " + contract.Key);
                var attempt = 0; var eligibility = 0; var transpiler = 0; var finalizer = 0;
                foreach (var patch in patches.Prefixes)
                {
                    if (patch.PatchMethod == AttemptMethod && patch.priority == Priority.First + 1) ++attempt;
                    if (patch.PatchMethod == (contract.Key == sceneKey ? SceneEligibilityMethod : EligibilityMethod) && patch.priority == Priority.Last) ++eligibility;
                }
                foreach (var patch in patches.Transpilers)
                {
                    if (patch.PatchMethod == TranspilerMethod && patch.priority == Priority.Last) ++transpiler;
                    else if (patch.priority <= Priority.Last || patch.before.Length != 0 || patch.after.Length != 0)
                        throw new NotSupportedException("A foreign transpiler may run after the exact call-site counter; dependency overrides are unsupported.");
                }
                foreach (var patch in patches.Finalizers) if (patch.PatchMethod == FinalizerMethod && patch.priority == Priority.Last) ++finalizer;
                if (attempt != 1 || eligibility != 1 || transpiler != 1 || finalizer != 1)
                    throw new InvalidOperationException("Private instrumentation is incomplete or duplicated: " + contract.Key);
                var evidence = contract.Evidence;
                if (evidence == null || evidence.OriginalBodyEntryCallbacks != 1 || evidence.OriginalReturnSites <= 0
                    || evidence.ExpectedNativeSites != contract.Sites.Count || evidence.ObservedCallsiteTargets.Count != contract.Sites.Count)
                    throw new InvalidOperationException("Private body/native-site insertion was not verified: " + contract.Key);
            }
        }

        private static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions, MethodBase original)
        {
            if (!Contracts.TryGetValue(original, out var contract)) throw new NotSupportedException("Unknown private counter target.");
            var source = new List<CodeInstruction>();
            foreach (var instruction in instructions)
            {
                if (instruction.opcode == OpCodes.Tailcall) throw new NotSupportedException("Private body-return instrumentation does not support tail calls.");
                var clone = new CodeInstruction(instruction.opcode, instruction.operand);
                CopyLabels(instruction, clone, false); clone.blocks.AddRange(instruction.blocks); source.Add(clone);
            }
            if (source.Count == 0) throw new NotSupportedException("Empty private counter body.");
            var result = new List<CodeInstruction>();
            if (contract.Key == storageSerializeKey)
            { result.Add(new CodeInstruction(OpCodes.Ldarg_0)); result.Add(new CodeInstruction(OpCodes.Call, StorageEntryMethod)); }
            else
            {
                result.Add(new CodeInstruction(OpCodes.Ldstr, contract.Key));
                if (contract.Key == sceneKey) { result.Add(new CodeInstruction(OpCodes.Ldarg_0)); result.Add(new CodeInstruction(OpCodes.Call, SceneEntryMethod)); }
                else if (contract.Key == storeKey) { result.Add(new CodeInstruction(OpCodes.Ldarg_S, (byte)5)); result.Add(new CodeInstruction(OpCodes.Call, StoreEntryMethod)); }
                else result.Add(new CodeInstruction(OpCodes.Call, EntryMethod));
            }
            var sites = new int[contract.Sites.Count]; var returns = 0;
            var observedTargets = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var instruction in source)
            {
                var added = new List<CodeInstruction>();
                for (var i = 0; i < contract.Sites.Count; ++i)
                {
                    var site = contract.Sites[i]; var direct = Calls(instruction, site.Native);
                    var alternative = site.ManagedAlternative != null && Calls(instruction, site.ManagedAlternative);
                    if (!direct && !alternative) continue;
                    if (instruction.opcode != OpCodes.Call) throw new NotSupportedException("Native observation expected a static call.");
                    observedTargets.Add(site.Key, Signature((MethodInfo)instruction.operand));
                    ++sites[i]; added.Add(new CodeInstruction(OpCodes.Ldstr, site.Key));
                    if (site.MessageArgument && direct)
                    { added.Add(new CodeInstruction(OpCodes.Ldarg_0)); added.Add(new CodeInstruction(OpCodes.Call, NativeMessageSiteMethod)); }
                    else added.Add(new CodeInstruction(OpCodes.Call, direct ? NativeSiteMethod : AntecedentMethod));
                }
                if (instruction.opcode == OpCodes.Ret)
                { ++returns; added.Add(new CodeInstruction(OpCodes.Ldstr, contract.Key)); added.Add(new CodeInstruction(OpCodes.Call, ReturnMethod)); }
                if (added.Count != 0)
                {
                    CopyLabels(instruction, added[0], true);
                    // Harmony opens regions before an instruction and closes
                    // EndExceptionBlock after it. Keep ends on the original
                    // call/return; only move starts to precede the callback.
                    var endings = new List<ExceptionBlock>();
                    foreach (var block in instruction.blocks)
                        if (block.blockType == ExceptionBlockType.EndExceptionBlock) endings.Add(block);
                        else added[0].blocks.Add(block);
                    instruction.blocks.Clear(); instruction.blocks.AddRange(endings);
                    result.AddRange(added);
                }
                result.Add(instruction);
            }
            for (var i = 0; i < sites.Length; ++i) if (sites[i] != 1)
                throw new NotSupportedException("Exact rewritten native site count differs: " + contract.Sites[i].Key + "; got " + sites[i]);
            if (returns == 0) throw new NotSupportedException("Private body-return counter requires a normal return site: " + contract.Key);
            lock (Gate) contract.Evidence = new OwnedReservoirLoadCounterContractEvidence(contract.Key, returns, contract.Sites.Count, observedTargets);
            return result;
        }
        private static void CopyLabels(CodeInstruction source, CodeInstruction destination, bool removeSource)
        {
            var from = LabelField.GetValue(source) as IList ?? throw new NotSupportedException("Pinned Harmony labels are not an IList.");
            var to = LabelField.GetValue(destination) as IList ?? throw new NotSupportedException("Pinned Harmony labels are not an IList.");
            foreach (var label in from) to.Add(label);
            if (removeSource) from.Clear();
        }

        private static void Advance(ref long value)
        { if (value == long.MaxValue) saturated = true; else ++value; }
        private static void Failure()
        {
            while (true)
            {
                var current = Interlocked.Read(ref observationFailures);
                if (current == long.MaxValue) { Volatile.Write(ref saturated, true); return; }
                if (Interlocked.CompareExchange(ref observationFailures, current + 1, current) == current) return;
            }
        }
        private static void AttemptPrefix(MethodBase __originalMethod, out Invocation? __state)
        {
            __state = null;
            try
            {
                if (!Installed) return;
                var contract = Contracts[__originalMethod]; var invocation = new Invocation(contract.Key);
                (invocations ??= new List<Invocation>()).Add(invocation); __state = invocation;
                lock (Gate) { Advance(ref Counts[contract.Key].Invocations); Advance(ref sequence); }
            }
            catch { Failure(); }
        }
        private static void EligibilityPrefix(MethodBase __originalMethod, bool __runOriginal)
        {
            try
            {
                if (!Installed || !__runOriginal) return;
                lock (Gate) { Advance(ref Counts[Contracts[__originalMethod].Key].Eligible); Advance(ref sequence); }
            }
            catch { Failure(); }
        }
        private static void SceneEligibilityPrefix(MethodBase __originalMethod, bool __runOriginal, string scene_name)
        {
            EligibilityPrefix(__originalMethod, __runOriginal);
            try { if (Installed && __runOriginal) lock (Gate) { IncrementScene(EligibleScenes, scene_name); Advance(ref sequence); } }
            catch { Failure(); }
        }
        private static void Finalizer(MethodBase __originalMethod, Invocation? __state, Exception? __exception)
        {
            try
            {
                if (!Installed || __state == null) return;
                lock (Gate)
                {
                    var row = Counts[Contracts[__originalMethod].Key]; Advance(ref row.Finalizers);
                    if (__exception != null) Advance(ref row.Exceptions);
                    if (__state.Key == teardownKey && __state.BodyEntered && __state.BodyReturned && __exception == null) Advance(ref row.Teardowns);
                    Advance(ref sequence);
                }
                var stack = invocations;
                if (stack == null || stack.Count == 0 || !ReferenceEquals(stack[stack.Count - 1], __state)) { Failure(); return; }
                stack.RemoveAt(stack.Count - 1);
            }
            catch { Failure(); }
        }
        private static Invocation? CurrentInvocation(string key)
        {
            var stack = invocations;
            if (stack == null || stack.Count == 0 || stack[stack.Count - 1].Key != key) { Failure(); return null; }
            return stack[stack.Count - 1];
        }
        private static void BodyEntry(string key)
        {
            try
            {
                if (!Installed) return;
                var invocation = CurrentInvocation(key); if (invocation != null) invocation.BodyEntered = true;
                lock (Gate) { Advance(ref Counts[key].Entries); Advance(ref sequence); }
            }
            catch { Failure(); }
        }
        private static void BodyReturn(string key)
        {
            try
            {
                if (!Installed) return;
                var invocation = CurrentInvocation(key);
                if (invocation != null)
                { if (!invocation.BodyEntered || invocation.BodyReturned) Failure(); invocation.BodyReturned = true; }
                lock (Gate) { Advance(ref Counts[key].Returns); Advance(ref sequence); }
            }
            catch { Failure(); }
        }
        private static void StorageSerializeEntry(Storage storage)
        {
            BodyEntry(storageSerializeKey);
            if (!Installed) return;
            try { Volatile.Read(ref storageObserver)?.Invoke(storage); }
            catch (Exception error)
            {
                try
                {
                    var message = error.GetType().FullName + ": " + error.Message;
                    if (message.Length > 2048) message = message.Substring(0, 2048);
                    lock (Gate)
                    {
                        Advance(ref observerErrorCount); Advance(ref sequence);
                        if (ObserverErrors.Count == 32) ObserverErrors.RemoveAt(0);
                        ObserverErrors.Add(message);
                    }
                }
                catch { Failure(); }
            }
        }
        private static void StoreEntry(string key, bool isDeserializing)
        {
            BodyEntry(key);
            try { if (Installed && isDeserializing) lock (Gate) { Advance(ref Counts[key].Deserializing); Advance(ref sequence); } }
            catch { Failure(); }
        }
        private static void SceneEntry(string key, string scene)
        {
            BodyEntry(key);
            try { if (Installed) lock (Gate) { IncrementScene(BodyScenes, scene); Advance(ref sequence); } }
            catch { Failure(); }
        }
        private static void IncrementScene(Dictionary<string, long> values, string? name)
        {
            var key = name ?? "<null>";
            if (key.Length > 256 || (!values.ContainsKey(key) && values.Count == 64)) { Failure(); return; }
            values.TryGetValue(key, out var count); Advance(ref count); values[key] = count;
        }
        private static void NativeSiteEntry(string key)
        { try { if (Installed) lock (Gate) { var value = NativeSites[key]; Advance(ref value); NativeSites[key] = value; Advance(ref sequence); } } catch { Failure(); } }
        private static void AntecedentEntry(string key)
        { try { if (Installed) lock (Gate) { var value = AntecedentSites[key]; Advance(ref value); AntecedentSites[key] = value; Advance(ref sequence); } } catch { Failure(); } }
        private static void NativeMessageSiteEntry(string key, int message)
        {
            NativeSiteEntry(key);
            try
            {
                if (!Installed) return;
                var categorized = key + (message == -672538170 ? " [message=-672538170]" : message == -931446686 ? " [message=-931446686]" : " [message=other]");
                lock (Gate) { var value = NativeSites[categorized]; Advance(ref value); NativeSites[categorized] = value; Advance(ref sequence); }
            }
            catch { Failure(); }
        }
    }
}
