using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using System.Threading;
using HarmonyLib;
using KSerialization;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal enum OwnedReservoirMarkerState { Unassigned, Assigned, Invalid }

    // Immutable interpretation of serialized data, never an ownership grant.
    internal sealed class OwnedReservoirMarkerRecord
    {
        internal OwnedReservoirMarkerState State { get; }
        internal string Error { get; }
        internal Guid NativeColonyId { get; }
        internal Guid PersistentOwnerToken { get; }
        internal Guid CoreColonyId { get; }
        internal string ContainerId { get; }
        internal long Generation { get; }
        internal Guid AllocationToken { get; }
        private OwnedReservoirMarkerRecord(OwnedReservoirMarkerState state, string error, Guid nativeColonyId = default,
            Guid ownerToken = default, Guid coreColonyId = default, string containerId = "", long generation = 0, Guid allocationToken = default)
        {
            State = state; Error = error; NativeColonyId = nativeColonyId; PersistentOwnerToken = ownerToken;
            CoreColonyId = coreColonyId; ContainerId = containerId; Generation = generation; AllocationToken = allocationToken;
        }
        internal bool Matches(FluidContainerHandle handle) => State == OwnedReservoirMarkerState.Assigned &&
            CoreColonyId == handle.ColonyId && ContainerId == handle.Id && Generation == handle.Generation && AllocationToken == handle.AllocationToken;

        internal static OwnedReservoirMarkerRecord Parse(int version, string? nativeColonyId, string? ownerToken,
            string? coreColonyId, string? containerId, long generation, string? allocationToken)
        {
            if (version == 0 && nativeColonyId == "" && ownerToken == "" && coreColonyId == "" && containerId == "" && generation == 0 && allocationToken == "")
                return new OwnedReservoirMarkerRecord(OwnedReservoirMarkerState.Unassigned, "");
            if (version != 1) return Invalid("Unsupported or partially populated marker version.");
            if (!TryGuid(nativeColonyId, out var native) || !TryGuid(ownerToken, out var token) ||
                !TryGuid(coreColonyId, out var core) || !TryGuid(allocationToken, out var allocation))
                return Invalid("Marker identities must be canonical nonempty lowercase Guid strings.");
            if (string.IsNullOrWhiteSpace(containerId) || containerId!.Length > 256 || generation <= 0)
                return Invalid("Marker container identity or generation is invalid.");
            foreach (var character in containerId) if (char.IsControl(character)) return Invalid("Marker container identity contains a control character.");
            try
            {
                if (new UTF8Encoding(false, true).GetByteCount(containerId) > 1024) return Invalid("Marker container identity exceeds its UTF-8 bound.");
            }
            catch (EncoderFallbackException) { return Invalid("Marker container identity is not valid UTF-8."); }
            return new OwnedReservoirMarkerRecord(OwnedReservoirMarkerState.Assigned, "", native, token, core, containerId, generation, allocation);
        }
        private static bool TryGuid(string? text, out Guid value)
        {
            value = default;
            return text != null && text.Length == 32 && Guid.TryParseExact(text, "N", out value) && value != Guid.Empty && text == value.ToString("N");
        }
        private static OwnedReservoirMarkerRecord Invalid(string error) => new OwnedReservoirMarkerRecord(OwnedReservoirMarkerState.Invalid, error);
    }

    // Present on the completed prefab before instantiation. Default fields are
    // inert. There is deliberately no assignment/adoption or lifecycle API.
    [SerializationConfig(MemberSerialization.OptIn)]
    internal sealed class OwnedReservoirOwnerMarker : KMonoBehaviour
    {
        [Serialize] private int schemaVersion = 0;
        [Serialize] private string nativeColonyId = "";
        [Serialize] private string persistentOwnerToken = "";
        [Serialize] private string coreColonyId = "";
        [Serialize] private string containerId = "";
        [Serialize] private long generation = 0;
        [Serialize] private string allocationToken = "";
        internal OwnedReservoirMarkerRecord ReadRecord() => OwnedReservoirMarkerRecord.Parse(schemaVersion, nativeColonyId,
            persistentOwnerToken, coreColonyId, containerId, generation, allocationToken);

        private static int installationThread;
        internal static bool Installed { get; private set; }
        // The original Install patches, for the game contract
        // (docs/game-update-resilience.md); keep in step with Install.
        internal static IEnumerable<MethodBase> ContractTargets() => new MethodBase[]
        {
            AccessTools.Method(typeof(GasReservoirConfig), nameof(GasReservoirConfig.DoPostConfigureComplete), new[] { typeof(GameObject) })
                ?? throw new NotSupportedException("The pinned completed GasReservoir prefab hook is unavailable.")
        };

        internal static void Install(Harmony harmony)
        {
            if (harmony == null) throw new ArgumentNullException(nameof(harmony));
            if (Installed)
            {
                RequireNativeThread(); return;
            }
            var target = AccessTools.Method(typeof(GasReservoirConfig), nameof(GasReservoirConfig.DoPostConfigureComplete), new[] { typeof(GameObject) });
            if (target == null || target.ReturnType != typeof(void) || target.GetMethodBody() == null)
                throw new NotSupportedException("The pinned completed GasReservoir prefab hook is unavailable.");
            harmony.Patch(target, postfix: new HarmonyMethod(typeof(OwnedReservoirOwnerMarker).GetMethod(nameof(AddToCompletePrefab), BindingFlags.NonPublic | BindingFlags.Static)));
            installationThread = Thread.CurrentThread.ManagedThreadId; Installed = true;
        }
        internal static void RequireNativeThread()
        {
            if (!Installed || installationThread != Thread.CurrentThread.ManagedThreadId)
                throw new InvalidOperationException("Native marker/footprint inspection requires the installation thread and installed prefab marker contract.");
        }
        // Failed OnLoad teardown: Mod has already removed this owner's patches.
        internal static void ResetAfterFailedLoad() { Installed = false; installationThread = 0; }

        private static void AddToCompletePrefab(GameObject go)
        {
            RequireNativeThread();
            // Another mod may subclass GasReservoirConfig and call this base
            // method for its own building. That prefab is not the pinned
            // GasReservoir contract: leave it untouched instead of aborting
            // its registration. The exact GasReservoir prefab stays strict.
            var prefab = go == null ? null : go.GetComponent<KPrefabID>();
            if (prefab == null || prefab.PrefabTag.Name != GasReservoirConfig.ID)
            {
                UnityEngine.Debug.LogWarning("[Mixed Atmospheres] Owner marker skipped for non-GasReservoir prefab " +
                    (prefab == null ? "<no KPrefabID>" : prefab.PrefabTag.Name) + " reached through GasReservoirConfig.DoPostConfigureComplete.");
                return;
            }
            if (go!.GetComponent<BuildingComplete>() == null)
                throw new InvalidOperationException("The marker hook received no completed GasReservoir prefab.");
            var markers = go.GetComponents<OwnedReservoirOwnerMarker>();
            if (markers.Length > 1) throw new InvalidOperationException("Duplicate reservoir owner markers on completed prefab.");
            var marker = markers.Length == 0 ? go.AddComponent<OwnedReservoirOwnerMarker>() : markers[0];
            if (marker.ReadRecord().State != OwnedReservoirMarkerState.Unassigned)
                throw new InvalidOperationException("The completed reservoir prefab must carry only an unassigned inert owner marker.");
        }
    }
}
