using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using HarmonyLib;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal sealed class OwnedFluidOperationRejectedException : InvalidOperationException
    { internal OwnedFluidOperationRejectedException(string message) : base(message) { } }
    internal sealed class OwnedFluidSaveBlockedException : IOException
    { internal OwnedFluidSaveBlockedException(string message) : base(message) { } }

    // Explicit instance activation only. Installing the conditional guards never
    // adopts a prefab, native packet, pipe network, building heat or world fluid.
    internal static class OwnedGasReservoirBridge
    {
        private sealed class StorageIdentityComparer : IEqualityComparer<Storage>
        {
            public bool Equals(Storage? x, Storage? y) => ReferenceEquals(x, y);
            public int GetHashCode(Storage value) => RuntimeHelpers.GetHashCode(value);
        }
        private sealed class NativeHost : IOwnedFluidActorHost, IOwnedFluidActorSaveEligibilityHost
        {
            internal readonly Storage Storage;
            internal readonly KPrefabID Prefab;
            internal readonly PrimaryElement Construction;
            internal readonly GameObject OwnerObject;
            internal readonly int NativeInstanceId;
            internal readonly SimHashes ConstructionElement;
            internal readonly float ConstructionMass;
            internal readonly Reservoir Reservoir;
            internal readonly SmartReservoir Smart;
            internal readonly ConduitConsumer Consumer;
            internal readonly ConduitDispenser Dispenser;
            internal readonly Rotatable? OrientationComponent;
            internal OwnedGasReservoirLifetime? Lifetime;
            internal OwnedFluidActorLease Lease;
            public string OwnerId { get; }
            internal NativeHost(Storage storage, KPrefabID prefab, PrimaryElement construction)
            {
                Storage = storage; Prefab = prefab; Construction = construction; OwnerObject = storage.gameObject; NativeInstanceId = prefab.InstanceID;
                ConstructionElement = construction.ElementID; ConstructionMass = construction.Mass;
                Reservoir = OwnerObject.GetComponent<Reservoir>(); Smart = OwnerObject.GetComponent<SmartReservoir>();
                Consumer = OwnerObject.GetComponent<ConduitConsumer>(); Dispenser = OwnerObject.GetComponent<ConduitDispenser>();
                OrientationComponent = OwnerObject.GetComponent<Rotatable>();
                OwnerId = "gas-reservoir:" + prefab.InstanceID.ToString(System.Globalization.CultureInfo.InvariantCulture);
            }
            public void ValidateNativeState(FluidContainerSnapshot allocation)
            {
                if (Storage == null || Prefab == null || Construction == null || Prefab.InstanceID != NativeInstanceId || Prefab.pendingDestruction || Prefab.conflicted ||
                    !Storage.isSpawned || Storage.IsEndOfLife()) throw new InvalidOperationException("The native reservoir is absent, unspawned, conflicted or being destroyed.");
                if (Storage.items == null || Storage.items.Count != 0) throw new InvalidOperationException("An owned reservoir must contain strictly zero native packets.");
                if (allocation.Definition.AllowedPhases != FluidContainerPhases.Gas ||
                    allocation.Definition.MaximumMassKg != Storage.capacityKg)
                    throw new InvalidOperationException("Owned reservoir gas admission/capacity differs from its explicit Core allocation.");
                if (Construction.ElementID != ConstructionElement || Construction.Mass != ConstructionMass)
                    throw new InvalidOperationException("The reservoir construction PrimaryElement changed; this bridge owns no construction material.");
            }
            public void NotifyChanged(OwnedFluidActorReceipt receipt)
            {
                RequireNativeOwnership(this);
                // Reservoir ignores the native event argument. There is no fake
                // PrimaryElement packet for other native consumers to mutate.
                Storage.Trigger(-1697596308, (object)null!);
                RequireNativeOwnership(this);
                Storage.OnStorageChange?.Invoke(null!);
                RequireNativeOwnership(this);
                Storage.GetComponent<SmartReservoir>()?.Sim200ms(0);
                RequireNativeOwnership(this);
            }
            public OwnedReservoirNativeObservation CaptureIdleSaveObservation(FluidContainerSnapshot allocation, ColonyFrameSnapshot source)
            {
                RequireNativeOwnership(this); ValidateNativeState(allocation);
                var go = OwnerObject;
                if (go == null || !go.activeInHierarchy || !ReferenceEquals(Storage.gameObject, go) ||
                    !ReferenceEquals(go.GetComponent<KPrefabID>(), Prefab) || !ReferenceEquals(go.GetComponent<PrimaryElement>(), Construction) ||
                    Prefab.InstanceID < 0 || Prefab.PrefabTag.Name != GasReservoirConfig.ID || Hosts.Count != 2)
                    throw new InvalidOperationException("Exact native reservoir identity or prefab changed before idle eligibility.");
                var flags = new List<OwnedReservoirComponentObservation>(9);
                RequireCapturedComponent(typeof(Storage), Storage, "storage", flags);
                RequireCapturedComponent(typeof(KPrefabID), Prefab, "prefab", flags);
                RequireCapturedComponent(typeof(PrimaryElement), Construction, "construction", flags);
                RequireCapturedComponent(typeof(Reservoir), Reservoir, "reservoir", flags);
                RequireCapturedComponent(typeof(SmartReservoir), Smart, "smart", flags);
                RequireCapturedComponent(typeof(ConduitConsumer), Consumer, "consumer", flags);
                RequireCapturedComponent(typeof(ConduitDispenser), Dispenser, "dispenser", flags);
                RequireCapturedComponent(typeof(OwnedGasReservoirLifetime), Lifetime, "lifetime", flags);
                if (Lifetime == null || !ReferenceEquals(Lifetime.Owner, Storage)) throw new InvalidOperationException("Native lifetime ownership changed.");
                var rotations = go.GetComponents<Rotatable>();
                if (ReferenceEquals(OrientationComponent, null))
                { if (rotations.Length != 0) throw new InvalidOperationException("An orientation component was added after native binding."); }
                else RequireCapturedComponent(typeof(Rotatable), OrientationComponent, "orientation", flags);
                if (Consumer.conduitType != ConduitType.Gas || Dispenser.conduitType != ConduitType.Gas || Consumer.IsConnected || Dispenser.IsConnected ||
                    Storage.capacityKg != 1000 || allocation.Definition.MaximumMassKg != 1000 || allocation.Definition.AllowedPhases != FluidContainerPhases.Gas)
                    throw new InvalidOperationException("Idle eligibility requires isolated gas ports and exact 1000 kg admission.");
                var anchor = Grid.PosToCell(go);
                if (!Grid.IsValidCell(anchor) || !source.TrySampleCell(anchor, out var cell) || cell!.WorldId != Grid.WorldIdx[anchor])
                    throw new InvalidOperationException("The native owner anchor differs from its supplied Core world geometry.");
                var observation = new OwnedReservoirNativeObservation(Prefab.InstanceID, Prefab.PrefabTag.Name, cell.WorldId, anchor,
                    OrientationComponent == null ? (int)Orientation.Neutral : (int)OrientationComponent.Orientation,
                    (int)Construction.ElementID, Construction.Mass, Storage.capacityKg, flags);
                RequireNativeOwnership(this); return observation;
            }
            private void RequireCapturedComponent(Type role, KMonoBehaviour? captured, string name, List<OwnedReservoirComponentObservation> flags)
            {
                var actual = OwnerObject.GetComponents(role);
                if (captured == null || actual.Length != 1 || !ReferenceEquals(actual[0], captured) || !captured.isSpawned)
                    throw new InvalidOperationException("Native component replaced, duplicated, destroyed or not yet spawned: " + name);
                // Native components can disable Mono updates deliberately.
                // Enabled is evidence, never an unsupported eligibility rule.
                flags.Add(new OwnedReservoirComponentObservation(name, captured.enabled, captured.isSpawned));
            }
        }

        private static readonly Dictionary<Storage, NativeHost> Hosts = new Dictionary<Storage, NativeHost>(new StorageIdentityComparer());
        private static OwnedFluidActorSession? active, lastEnded;
        private static bool installed;
        internal static bool Installed => installed;
        internal static OwnedFluidActorSession? ActiveSession => active;
        internal static ColonyFrameSnapshot? RecoverySnapshot => lastEnded?.RecoverySnapshot;
        internal static IReadOnlyList<OwnedFluidActorOrphan> RecoveryOrphans => lastEnded?.Orphans ?? Array.Empty<OwnedFluidActorOrphan>();
        internal static int BindingCount => Hosts.Count;
        internal static bool WorldRecoveryRequired => active?.WorldRecoveryRequired == true || lastEnded?.WorldRecoveryRequired == true;
        internal static bool UnresolvedConsumptionRecoveryRequired => active?.UnresolvedConsumptionAtTeardown == true || lastEnded?.UnresolvedConsumptionAtTeardown == true;
        internal static string LastRejection { get; private set; } = "";
        internal static long RejectedMutationCount { get; private set; }
        internal static long BlockedPortCount { get; private set; }
        internal static long BlockedSaveCount { get; private set; }
        internal static System.Action<string>? FailureReporter { get; set; }

        static OwnedGasReservoirBridge() { MixturePresentationRuntime.SessionEnding += OnSessionEnding; }

        internal static OwnedFluidActorSession BeginSession(ColonyFluidState state, ExperimentalFluidMessageRouter router, bool explicitPrivateOptIn)
        {
            if (!explicitPrivateOptIn) throw new InvalidOperationException("Owned gas reservoirs require explicit private experimental activation.");
            if (!installed) throw new InvalidOperationException("Install and verify owned reservoir guards before activation.");
            if (active != null) throw new InvalidOperationException("An owned reservoir session is already active.");
            if (HasNonemptyRecovery()) throw new InvalidOperationException("A prior owned allocation needs explicit recovery before another activation.");
            active = new OwnedFluidActorSession(state, router, true); return active;
        }

        internal static OwnedReservoirIdleSaveLease ReserveIdleSaveEligibility(ColonyFramePublication expected)
        {
            var session = active ?? throw new InvalidOperationException("No explicitly activated reservoir session.");
            if (!installed || Hosts.Count != 2) throw new InvalidOperationException("Exactly two guarded native reservoir hosts are required.");
            foreach (var pair in Hosts)
            {
                if (!ReferenceEquals(pair.Key, pair.Value.Storage)) throw new InvalidOperationException("Native storage registration identity changed.");
                session.RequireExactHostBinding(pair.Value.Lease, pair.Value);
            }
            // The session installs its acquisition guard before any host/native
            // validation. This never changes SaveIsBlocked or serializer guards.
            return session.ReserveIdleSaveEligibility(expected);
        }

        // Call only at a known idle post-load boundary, after native spawn and
        // the initial prepared publication have completed consumption. Starting
        // ownership during an already running native save is unsupported.
        internal static OwnedFluidActorLease BindNativeEmptyReservoir(Storage storage, FluidContainerHandle container)
        {
            var session = active ?? throw new InvalidOperationException("No explicitly activated reservoir session.");
            if (storage == null) throw new ArgumentNullException(nameof(storage));
            if (!storage.isSpawned || storage.IsEndOfLife()) throw new InvalidOperationException("Spawn the native reservoir before binding it.");
            if (Hosts.ContainsKey(storage)) throw new InvalidOperationException("Native storage is already owned.");
            var go = storage.gameObject; var prefab = go.GetComponent<KPrefabID>(); var construction = go.GetComponent<PrimaryElement>();
            if (prefab == null || prefab.InstanceID < 0 || prefab.PrefabTag.Name != GasReservoirConfig.ID || construction == null ||
                go.GetComponents<Storage>().Length != 1 || go.GetComponent<Reservoir>() == null || go.GetComponent<SmartReservoir>() == null ||
                go.GetComponent<ConduitConsumer>() == null || go.GetComponent<ConduitDispenser>() == null)
                throw new NotSupportedException("Only a spawned, uniquely identified native GasReservoir with one Storage is supported.");
            var consumer = go.GetComponent<ConduitConsumer>(); var dispenser = go.GetComponent<ConduitDispenser>();
            if (consumer.conduitType != ConduitType.Gas || dispenser.conduitType != ConduitType.Gas || consumer.IsConnected || dispenser.IsConnected)
                throw new NotSupportedException("The first owned reservoir adapter requires isolated gas ports with no native pipe connection.");
            var host = new NativeHost(storage, prefab, construction);
            var lease = session.Bind(host, container); host.Lease = lease;
            try
            {
                Hosts.Add(storage, host);
                var lifetime = go.AddOrGet<OwnedGasReservoirLifetime>(); lifetime.Owner = storage;
                host.Lifetime = lifetime;
                MixtureInspection.RegisterContainerOwner(go, lease.Container, "Owned reservoir contents");
                return lease;
            }
            catch
            {
                // Binding did not import or debit native mass. Preserve the
                // allocation even if component/inspection registration failed.
                Hosts.Remove(storage); session.ForceDetach(lease, "Native owner registration failed."); throw;
            }
        }

        internal static void UnbindEmpty(Storage storage)
        {
            if (!Hosts.TryGetValue(storage, out var host)) throw new InvalidOperationException("Reservoir is not owned.");
            (active ?? throw new InvalidOperationException("No active owner session.")).UnbindEmpty(host.Lease);
            Hosts.Remove(storage); MixtureInspection.UnregisterContainerOwner(storage.gameObject, host.Lease.Container);
        }

        internal static void ForcedCleanup(Storage? storage, string reason)
        {
            if (ReferenceEquals(storage, null) || !Hosts.TryGetValue(storage, out var host)) return;
            RequireCleanupReason(reason);
            try { active?.ForceDetach(host.Lease, reason); }
            finally
            {
                Hosts.Remove(storage);
                MixtureInspection.UnregisterContainerOwner(host.OwnerObject, host.Lease.Container);
            }
            // Unity may already consider the object destroyed; never ask for
            // its native properties here. Session reset clears weak UI tables.
            LastRejection = "Owned reservoir orphan retained: " + reason;
        }

        private static void OnSessionEnding() => EndSession("Native game session ended; owned allocations retained for recovery.");
        internal static void EndSession(string reason)
        {
            var session = active; if (session == null) return;
            RequireCleanupReason(reason);
            try
            {
                session.EndSession(reason);
                foreach (var host in Hosts.Values) MixtureInspection.UnregisterContainerOwner(host.OwnerObject, host.Lease.Container);
            }
            finally { lastEnded = session; active = null; Hosts.Clear(); }
        }
        private static void RequireCleanupReason(string reason)
        { if (string.IsNullOrWhiteSpace(reason) || reason.Length > 4096) throw new ArgumentException("A bounded explicit cleanup reason is required.", nameof(reason)); }

        internal static bool SaveIsBlocked => active != null || HasNonemptyRecovery();
        private static bool HasNonemptyRecovery()
        {
            if (lastEnded == null) return false;
            if (lastEnded.UnresolvedConsumptionAtTeardown) return true;
            if (lastEnded.WorldRecoveryRequired) return true;
            var snapshot = lastEnded.RecoverySnapshot;
            // The explicitly supplied Core state's entire container table is
            // this session's ownership domain, including allocations whose
            // native owner was never bound or failed during registration.
            foreach (var allocation in snapshot.Containers.Values)
                if (!OwnedFluidActorSession.InventoryIsEmpty(allocation.Inventory)) return true;
            return false;
        }

        private static string SaveReason => "Saving is blocked while experimental owned reservoirs, retained orphan inventories, custom Core world evacuations or unresolved consumption exist. " +
            "Native saves cannot preserve these inventories. Complete acknowledged conservative evacuation and normal unbind, or explicitly recover the orphan allocation.";
        private static void Report(string message, bool dialog)
        {
            LastRejection = message;
            if (FailureReporter != null) { FailureReporter(message); return; }
            Debug.LogWarning("[Mixed Atmospheres] " + message);
            if (dialog && GameScreenManager.Instance != null && ScreenPrefabs.Instance != null)
                ((ConfirmDialogScreen)GameScreenManager.Instance.StartScreen(ScreenPrefabs.Instance.ConfirmDialogScreen.gameObject,
                    GameScreenManager.Instance.ssOverlayCanvas.gameObject)).PopupConfirmDialog(message, null, null);
        }

        private static NativeHost? Owned(Storage? storage) => !ReferenceEquals(storage, null) && Hosts.TryGetValue(storage, out var host) ? host : null;
        private static FluidContainerSnapshot Read(NativeHost host)
        {
            if (host.Storage.items == null || host.Storage.items.Count != 0)
                throw new InvalidOperationException("Owned reservoir native packet list is not empty.");
            return (active ?? throw new InvalidOperationException("Owned reservoir session is absent.")).ReadCompleted(host.Lease);
        }
        private static void RequireNativeOwnership(NativeHost host)
        {
            if (!Hosts.TryGetValue(host.Storage, out var actual) || !ReferenceEquals(actual, host))
                throw new InvalidOperationException("Native reservoir ownership ended during notification.");
            (active ?? throw new InvalidOperationException("Native owner session ended.")).RequireExactHostBinding(host.Lease, host);
            host.ValidateNativeState(Read(host)); // Full lease/session plus native liveness and construction validation.
        }
        private static void RejectMutation(Storage? storage, string method)
        {
            if (Owned(storage) == null) return;
            ++RejectedMutationCount;
            var message = "Unsupported native mutation rejected before changing owned reservoir inventory: " + method;
            LastRejection = message; throw new OwnedFluidOperationRejectedException(message);
        }

        internal static bool StorageMutationPrefix(Storage __instance, MethodBase __originalMethod)
        { RejectMutation(__instance, __originalMethod.Name); return true; }
        internal static bool StorePrefix(Storage __instance, GameObject go)
        {
            RejectMutation(__instance, "Store");
            // Native Store reparents the item and can transfer disease before
            // its later absorption/event paths inspect the previous storage.
            // Reject even a stale source reference before those side effects;
            // a bound reservoir's native packet list must remain empty.
            if (Hosts.Count != 0 && go != null) RejectStoreSource(go.GetComponent<Pickupable>());
            return true;
        }
        internal static void RejectStoreSource(Pickupable? source)
        {
            if (!ReferenceEquals(source, null)) RejectMutation(source.storage, "Store source");
        }
        internal static bool StorageTransferPrefix(Storage __instance, object[] __args, MethodBase __originalMethod)
        {
            RejectMutation(__instance, __originalMethod.Name);
            foreach (var argument in __args) if (argument is Storage storage) RejectMutation(storage, __originalMethod.Name + " destination");
            return true;
        }
        internal static bool AbsorbPrefix(Pickupable __instance, Pickupable other)
        {
            if (Hosts.Count == 0) return true;
            RejectMutation(__instance.storage, "Pickupable.TryAbsorb");
            if (!ReferenceEquals(other, null)) RejectMutation(other.storage, "Pickupable.TryAbsorb source");
            return true;
        }
        internal static bool PortPrefix(KMonoBehaviour __instance)
        {
            if (Hosts.Count == 0) return true;
            var storage = __instance is ConduitConsumer consumer ? consumer.storage : (__instance as ConduitDispenser)?.storage;
            if (Owned(storage) == null) return true;
            ++BlockedPortCount; return false;
        }
        internal static bool MassPrefix(Storage __instance, ref float __result)
        { var host = Owned(__instance); if (host == null) return true; __result = (float)Read(host).Inventory.TotalMassKg; return false; }
        internal static bool RemainingPrefix(Storage __instance, ref float __result)
        { var host = Owned(__instance); if (host == null) return true; var allocation = Read(host); __result = (float)Math.Max(0, allocation.Definition.MaximumMassKg - allocation.Inventory.TotalMassKg); return false; }
        internal static bool EmptyPrefix(Storage __instance, ref bool __result)
        { var host = Owned(__instance); if (host == null) return true; __result = OwnedFluidActorSession.InventoryIsEmpty(Read(host).Inventory); return false; }
        internal static bool FullPrefix(Storage __instance, ref bool __result)
        { var host = Owned(__instance); if (host == null) return true; var allocation = Read(host); __result = allocation.Inventory.TotalMassKg >= allocation.Definition.MaximumMassKg; return false; }
        internal static bool DestroyPrefix(Deconstructable __instance)
        {
            if (Hosts.Count == 0) return true;
            var storage = __instance.GetComponent<Storage>(); if (Owned(storage) == null) return true;
            try { UnbindEmpty(storage); return true; }
            catch (InvalidOperationException error) { Report("Owned reservoir deconstruction blocked: " + error.Message, true); return false; }
        }
        // Native callers consume the returned list directly (for example
        // CraftModuleInterface uses AddRange). A blocked destruction drops no
        // construction materials, so it returns an empty list, never null.
        internal static bool ForceDestroyPrefix(Deconstructable __instance, ref List<GameObject> __result)
        {
            if (DestroyPrefix(__instance)) return true;
            __result = new List<GameObject>();
            return false;
        }
        internal static bool QueueDestroyPrefix(Storage __instance)
        {
            if (Owned(__instance) == null) return true;
            ForcedCleanup(__instance, "Native object destruction was queued before acknowledged evacuation.");
            return false; // Do not execute native vent/drop on custom contents.
        }
        internal static bool SaveUiPrefix()
        { if (!SaveIsBlocked) return true; ++BlockedSaveCount; Report(SaveReason, true); return false; }
        internal static bool QuitPrefix(bool saveFirst)
        { return !saveFirst || SaveUiPrefix(); }
        internal static bool SavePrefix()
        { if (!SaveIsBlocked) return true; ++BlockedSaveCount; LastRejection = SaveReason; throw new OwnedFluidSaveBlockedException(SaveReason); }
        internal static bool StorageSerializePrefix(Storage __instance)
        {
            var host = Owned(__instance);
            if (host == null) return true;
            if (OwnedFluidActorSession.InventoryIsEmpty(Read(host).Inventory) && active?.HasPendingBatch == false) return true;
            ++BlockedSaveCount; LastRejection = SaveReason; throw new OwnedFluidSaveBlockedException(SaveReason);
        }

        internal static IReadOnlyList<(MethodInfo target, MethodInfo prefix)> GuardContracts()
        {
            var result = new List<(MethodInfo, MethodInfo)>();
            void Add(Type type, string name, string prefix, Type[]? arguments = null)
            {
                var method = arguments == null ? AccessTools.Method(type, name) : AccessTools.Method(type, name, arguments);
                if (method == null || method.GetMethodBody() == null) throw new NotSupportedException("Owned reservoir guard contract changed: " + type.Name + "." + name);
                result.Add((method, AccessTools.Method(typeof(OwnedGasReservoirBridge), prefix)));
            }
            Add(typeof(Storage), nameof(Storage.Store), nameof(StorePrefix));
            var mutationNames = new HashSet<string>(new[] { "AddElement", "AddGasChunk", "AddLiquid", "AddOre", "AddToPrimaryElement", "ConsumeAndGetDisease", "ConsumeIgnoringDisease", "ConsumeAllIgnoringDisease", "Remove", "Drop", "DropAll", "DropSome", "DropUnlessMatching", "DropUnlessHasTag", "DropHasTags", "ForceStore" }, StringComparer.Ordinal);
            foreach (var method in typeof(Storage).GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.DeclaredOnly))
            {
                if (mutationNames.Contains(method.Name)) result.Add((method, AccessTools.Method(typeof(OwnedGasReservoirBridge), nameof(StorageMutationPrefix))));
                if (method.Name == "Transfer" || method.Name == "TransferMass" || method.Name == "TransferUnitMass")
                    result.Add((method, AccessTools.Method(typeof(OwnedGasReservoirBridge), nameof(StorageTransferPrefix))));
            }
            Add(typeof(Pickupable), nameof(Pickupable.TryAbsorb), nameof(AbsorbPrefix));
            Add(typeof(ConduitConsumer), "Consume", nameof(PortPrefix), new[] { typeof(float), typeof(ConduitFlow) });
            Add(typeof(ConduitDispenser), "Dispense", nameof(PortPrefix), new[] { typeof(float) });
            Add(typeof(Storage), nameof(Storage.ExactMassStored), nameof(MassPrefix));
            Add(typeof(Storage), nameof(Storage.MassStored), nameof(MassPrefix));
            Add(typeof(Storage), nameof(Storage.RemainingCapacity), nameof(RemainingPrefix));
            Add(typeof(Storage), nameof(Storage.IsEmpty), nameof(EmptyPrefix));
            Add(typeof(Storage), nameof(Storage.IsFull), nameof(FullPrefix));
            Add(typeof(Storage), "OnQueueDestroyObject", nameof(QueueDestroyPrefix));
            Add(typeof(Deconstructable), "OnCompleteWork", nameof(DestroyPrefix), new[] { typeof(WorkerBase) });
            Add(typeof(Deconstructable), nameof(Deconstructable.ForceDestroyAndGetMaterials), nameof(ForceDestroyPrefix));
            Add(typeof(SaveLoader), nameof(SaveLoader.Save), nameof(SavePrefix), new[] { typeof(string), typeof(bool), typeof(bool) });
            Add(typeof(Storage), nameof(Storage.Serialize), nameof(StorageSerializePrefix), new[] { typeof(BinaryWriter) });
            Add(typeof(Game), nameof(Game.StartDelayedSave), nameof(SaveUiPrefix), new[] { typeof(string), typeof(bool), typeof(bool) });
            Add(typeof(PauseScreen), "DoSave", nameof(SaveUiPrefix), new[] { typeof(string) });
            Add(typeof(SaveScreen), "DoSave", nameof(SaveUiPrefix), new[] { typeof(string) });
            Add(typeof(PauseScreen), "OnQuitConfirm", nameof(QuitPrefix), new[] { typeof(bool) });
            Add(typeof(PauseScreen), "OnDesktopQuitConfirm", nameof(QuitPrefix), new[] { typeof(bool) });
            return result.AsReadOnly();
        }

        internal static void Install(Harmony harmony)
        {
            if (harmony == null) throw new ArgumentNullException(nameof(harmony));
            if (installed) return;
            var contracts = GuardContracts(); // Validate every target before installing any guard.
            foreach (var contract in contracts) harmony.Patch(contract.target, prefix: new HarmonyMethod(contract.prefix));
            foreach (var contract in contracts)
                if (Harmony.GetPatchInfo(contract.target)?.Prefixes.Any(patch => patch.PatchMethod == contract.prefix) != true)
                    throw new InvalidOperationException("Owned reservoir guard did not install: " + contract.target);
            installed = true;
        }
    }

    // Added only to an explicitly bound live instance. Scene load can bypass
    // OnCleanUp, but KMonoBehaviour always calls OnForcedCleanUp first.
    internal sealed class OwnedGasReservoirLifetime : KMonoBehaviour
    {
        internal Storage? Owner;
        protected override void OnForcedCleanUp()
        {
            OwnedGasReservoirBridge.ForcedCleanup(Owner, "Owned reservoir forced cleanup or scene destruction.");
            Owner = null; base.OnForcedCleanUp();
        }
    }
}
