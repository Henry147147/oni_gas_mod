using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    public sealed class MixtureContainerValidationCase
    {
        public string Surface { get; internal set; } = "";
        public string OwnerName { get; internal set; } = "";
        public string InventoryId { get; internal set; } = "";
        public string Components { get; internal set; } = "";
        public bool SurfaceContractMatches { get; internal set; }
        public bool ActualReadingResolved { get; internal set; }
        public bool CompleteCompositionVisible { get; internal set; }
        public bool WarningsVisible { get; internal set; }
        public bool NoNativeSubstitute { get; internal set; }
        public string Error { get; internal set; } = "";
        public bool Passed => SurfaceContractMatches && ActualReadingResolved && CompleteCompositionVisible && WarningsVisible && NoNativeSubstitute && Error.Length == 0;
    }

    public sealed class MixtureContainerValidationReport
    {
        public IReadOnlyList<MixtureContainerValidationCase> Cases { get; internal set; } = Array.Empty<MixtureContainerValidationCase>();
        public bool FailingAdapterUnavailable { get; internal set; }
        public bool FailingAdapterDidNotUseNativePacket { get; internal set; }
        public bool UnregisterRemovedOwnerAndProvider { get; internal set; }
        public IReadOnlyList<string> Errors { get; internal set; } = Array.Empty<string>();
        public bool Passed => Cases.Count == 6 && Cases.All(item => item.Passed) && FailingAdapterUnavailable &&
            FailingAdapterDidNotUseNativePacket && UnregisterRemovedOwnerAndProvider && Errors.Count == 0;
    }

    /// <summary>
    /// Game-only readout probe. The caller supplies real game owners/prefabs;
    /// no prefab, stored material, simulation message or selection is created.
    /// Temporary bindings are explicitly labelled read-only visual fixtures.
    /// Unspawned prefab fallbacks remain unavailable. Passing validates the
    /// mixture binding, not native pipe/tank operation or gameplay ownership.
    /// </summary>
    public static class MixtureInspectionValidation
    {
        private static readonly string[] Surfaces = { "pipe", "reservoir", "bottle", "suit", "vent", "machine" };

        // Run with a Unity MonoBehaviour.StartCoroutine, AFTER configuring the
        // presentation model. Frame yields test the actual once-per-frame pin.
        // Owner keys must contain pipe/reservoir/bottle/suit/vent/machine.
        public static IEnumerator ValidateContainerSurfaces(IReadOnlyDictionary<string, GameObject> owners,
            FluidInventory fixtureInventory, double fixtureVolumeCubicMetres,
            System.Action<MixtureContainerValidationReport> completed)
        {
            if (owners == null) throw new ArgumentNullException(nameof(owners));
            if (fixtureInventory == null) throw new ArgumentNullException(nameof(fixtureInventory));
            if (completed == null) throw new ArgumentNullException(nameof(completed));
            var report = new MixtureContainerValidationReport();
            var cases = new List<MixtureContainerValidationCase>();
            var errors = new List<string>();
            var registered = new List<(GameObject owner, string id)>();
            var token = Guid.NewGuid().ToString("N");
            try
            {
                foreach (var surface in Surfaces)
                {
                    var result = new MixtureContainerValidationCase { Surface = surface, InventoryId = "inspection-fixture:" + token + ":" + surface };
                    cases.Add(result);
                    try
                    {
                        if (!owners.TryGetValue(surface, out var owner) || owner == null) throw new ArgumentException("A real " + surface + " owner is required.");
                        result.OwnerName = owner.name;
                        result.Components = string.Join(", ", owner.GetComponents<Component>().Where(component => component != null).Select(component => component.GetType().Name));
                        result.SurfaceContractMatches = Matches(surface, owner);
                        if (!result.SurfaceContractMatches) throw new ArgumentException("Owner does not expose the " + surface + " inspection surface.");
                        MixtureInspection.RegisterInventory(owner, result.InventoryId, () => fixtureInventory,
                            fixtureVolumeCubicMetres, "[Read-only visual fixture] " + surface);
                        registered.Add((owner, result.InventoryId));
                    }
                    catch (Exception error) { result.Error = error.Message; }
                }
                yield return null;
                foreach (var result in cases.Where(item => item.Error.Length == 0))
                {
                    try
                    {
                        var owner = owners[result.Surface];
                        result.ActualReadingResolved = MixturePresentationRuntime.TryResolveInventory(result.InventoryId, out var reading) && reading != null;
                        if (reading == null) throw new InvalidOperationException("Registered fixture has no completed-frame reading.");
                        var text = MixtureInspection.GetContainerInspectionText(owner);
                        result.CompleteCompositionVisible = text.Contains("[Read-only visual fixture] " + result.Surface) &&
                            reading.Constituents.All(row => text.Contains(MixtureInspection.FormatConstituent(row)));
                        result.WarningsVisible = reading.Warnings.All(text.Contains);
                        result.NoNativeSubstitute = !text.Contains("Native inventory");
                    }
                    catch (Exception error) { result.Error = error.Message; }
                }
                if (registered.Count != 0)
                {
                    var target = registered[0];
                    MixturePresentationRuntime.RegisterInventory(target.id,
                        () => throw new InvalidOperationException("fixture-inventory-read-failed"), fixtureVolumeCubicMetres, () => target.owner != null);
                    yield return null;
                    try
                    {
                        var text = MixtureInspection.GetContainerInspectionText(target.owner);
                        report.FailingAdapterUnavailable = text.Contains("unavailable") && text.Contains("fixture-inventory-read-failed");
                        report.FailingAdapterDidNotUseNativePacket = !text.Contains("Native inventory");
                    }
                    catch (Exception error) { errors.Add("Failing adapter: " + error.Message); }
                }
            }
            finally
            {
                foreach (var item in registered) MixtureInspection.UnregisterInventory(item.owner, item.id);
            }
            yield return null;
            report.UnregisterRemovedOwnerAndProvider = true;
            foreach (var item in registered)
            {
                try
                {
                    if (MixturePresentationRuntime.TryResolveInventory(item.id, out _) ||
                        MixtureInspection.GetContainerInspectionText(item.owner).Contains("[Read-only visual fixture]"))
                        report.UnregisterRemovedOwnerAndProvider = false;
                }
                catch (Exception error) { errors.Add("Unregister: " + error.Message); }
            }
            report.Cases = cases.AsReadOnly(); report.Errors = errors.AsReadOnly(); completed(report);
        }

        private static bool Matches(string surface, GameObject owner)
        {
            switch (surface)
            {
                case "pipe": return owner.GetComponent<Conduit>() != null;
                case "reservoir":
                case "machine": return owner.GetComponentsInChildren<IStorage>().Length != 0;
                case "bottle":
                    var primary = owner.GetComponent<PrimaryElement>();
                    return primary != null && primary.Element != null && (primary.Element.IsGas || primary.Element.IsLiquid);
                case "suit": return owner.GetComponent<SuitTank>() != null || owner.GetComponent<Equipment>() != null || owner.GetComponent<MinionIdentity>() != null;
                case "vent": return owner.GetComponent<Vent>() != null || owner.GetComponent<ConduitDispenser>() != null;
                default: return false;
            }
        }
    }
}
