using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using HarmonyLib;
using MixedAtmospheres.Core;
using Newtonsoft.Json;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Disposable presentation fixtures only. Native ONI factories initialize
    // the real objects; explicit conserved adapters supply separate, clearly
    // labelled read-only inventories. This never fills a native pipe/tank,
    // connects a network, equips a duplicant, or drives the simulation clock.
    internal sealed class MixtureContainerVisualValidation : IDisposable
    {
        internal sealed class Surface
        {
            public string Kind { get; internal set; } = "";
            public int Cell { get; internal set; }
            public string InventoryId { get; internal set; } = "";
            public string FixtureLabel => OwnedLabel ?? "[Read-only visual fixture] " + Kind;
            public IReadOnlyList<int> Footprint { get; internal set; } = Array.Empty<int>();
            public IReadOnlyList<int> ReservedCells { get; internal set; } = Array.Empty<int>();
            public int InstanceId => Owner != null ? Owner.GetInstanceID() : 0;
            [JsonIgnore] internal FluidContainerHandle? ContainerHandle;
            [JsonIgnore] internal bool ExpectHazardousTrace = true;
            [JsonIgnore] internal string? OwnedLabel;
            [JsonIgnore] public GameObject Owner { get; internal set; } = null!;
            [JsonIgnore] internal BuildingDef? Definition;
        }

        internal sealed class LabelEvidence
        {
            public string Name = "", Text = "", Tooltip = "";
            public bool ActiveSelf, ActiveInHierarchy, TextEnabled;
            public int Sibling;
            public float Height, Top, Bottom;
        }

        internal sealed class SelectionEvidence
        {
            public string Surface = "", OwnerName = "", InventoryId = "", SourceId = "";
            public string RawNativeDescription = "", ExpectedNativeDescription = "", ActualNativeDescription = "";
            public int Cell, InstanceId, UnityFrame, SelectedFrame;
            public long Revision;
            public bool NativeSpawned, NativeConduitRegistered, CorrectSelection, CorrectPanelTarget;
            public bool FixtureTitlePresent, NativeDescriptionPreserved, CompleteCompositionPresent;
            public bool WarningsPresentBeforeComposition, NoNativeSubstitute;
            public bool OwnedInspectionBeforeNativeDescription;
            public bool ExactOwnedAllocation, SharedCompletedSnapshot;
            public int IndependentInventoryProviders;
            public string[] Errors = Array.Empty<string>();
            public LabelEvidence[] Labels = Array.Empty<LabelEvidence>();
            public bool Passed => NativeSpawned && CorrectSelection && CorrectPanelTarget && FixtureTitlePresent &&
                NativeDescriptionPreserved && CompleteCompositionPresent && WarningsPresentBeforeComposition && OwnedInspectionBeforeNativeDescription && NoNativeSubstitute && SharedCompletedSnapshot && Errors.Length == 0;
        }

        private static readonly string[] Kinds = { "pipe", "reservoir", "bottle", "suit", "vent", "machine" };
        private static readonly Dictionary<string, string> BuildingIds = new Dictionary<string, string>(StringComparer.Ordinal)
        { ["pipe"] = "GasConduit", ["reservoir"] = "GasReservoir", ["vent"] = "GasVent", ["machine"] = "Electrolyzer" };
        private readonly List<Surface> surfaces = new List<Surface>();
        private readonly List<SelectionEvidence> evidence = new List<SelectionEvidence>();
        private readonly List<object> allocationEvidence = new List<object>();
        private readonly int createdFrame;
        private readonly float createdTime;
        private string selectedKind = "";
        private int selectedFrame = -1;
        private bool disposed;

        private MixtureContainerVisualValidation(List<Surface> planned)
        { surfaces = planned; createdFrame = Time.frameCount; createdTime = Time.realtimeSinceStartup; }

        internal IReadOnlyList<Surface> Surfaces => surfaces.AsReadOnly();
        internal IReadOnlyList<SelectionEvidence> Evidence => evidence.AsReadOnly();
        internal IReadOnlyList<object> AllocationEvidence => allocationEvidence.AsReadOnly();
        internal IReadOnlyDictionary<string, GameObject> Objects => surfaces.ToDictionary(surface => surface.Kind, surface => surface.Owner, StringComparer.Ordinal);
        internal string PendingReason { get; private set; } = "Waiting for native spawn and a completed UI frame.";

        // All footprints and port cells are checked before the first creation.
        // Callers reserve the cells and supporting floor in their disposable
        // arena; no existing colony object is removed to make room here.
        internal static MixtureContainerVisualValidation Create(IReadOnlyDictionary<string, int> placements,
            FluidInventory traceInventory, double volumeCubicMetres)
        {
            RequirePresentation();
            if (traceInventory == null) throw new ArgumentNullException(nameof(traceInventory));
            if (double.IsNaN(volumeCubicMetres) || double.IsInfinity(volumeCubicMetres) || volumeCubicMetres <= 0)
                throw new ArgumentOutOfRangeException(nameof(volumeCubicMetres));
            var planned = Plan(placements);
            var session = new MixtureContainerVisualValidation(planned);
            var token = Guid.NewGuid().ToString("N");
            try
            {
                foreach (var surface in planned)
                {
                    surface.InventoryId = "spawned-inspection-fixture:" + token + ":" + surface.Kind;
                    surface.Owner = Spawn(surface);
                    Require(surface.Owner != null, "Native factory did not create " + surface.Kind + ".");
                    MixtureInspection.RegisterInventory(surface.Owner, surface.InventoryId, () => traceInventory,
                        volumeCubicMetres, surface.FixtureLabel);
                }
                return session;
            }
            catch { session.Dispose(); throw; }
        }

        internal static MixtureContainerVisualValidation CreateOwned(IReadOnlyDictionary<string, int> placements,
            IReadOnlyDictionary<string, FluidContainerHandle> handles)
        {
            RequirePresentation();
            if (handles == null) throw new ArgumentNullException(nameof(handles));
            Require(handles.Count == Kinds.Length && Kinds.All(handles.ContainsKey), "Exactly six owned container handles are required.");
            var planned = Plan(placements);
            var frame = MixturePresentationRuntime.AcquireFrame();
            foreach (var surface in planned)
            {
                var handle = handles[surface.Kind];
                Require(MixtureInspection.TryResolveContainerBinding(frame, handle, out _, out _), "Owned container is not in the completed frame: " + surface.Kind);
                surface.ContainerHandle = handle; surface.InventoryId = handle.Id;
            }
            var session = new MixtureContainerVisualValidation(planned);
            try
            {
                foreach (var surface in planned)
                {
                    surface.Owner = Spawn(surface);
                    Require(surface.Owner != null, "Native factory did not create " + surface.Kind + ".");
                    MixtureInspection.RegisterContainerOwner(surface.Owner, surface.ContainerHandle!.Value, surface.FixtureLabel);
                }
                return session;
            }
            catch { session.Dispose(); throw; }
        }

        // Uses the same all-before-first-spawn footprint, port, construction
        // and native placement checks as the inspection fixtures. Ownership is
        // bound separately, after both native Storage components have spawned.
        internal static MixtureContainerVisualValidation CreateEmptyReservoirPair(IReadOnlyDictionary<string, int> placements)
        {
            RequirePresentation();
            var planned = Plan(placements, reservoirPair: true);
            var session = new MixtureContainerVisualValidation(planned);
            try
            {
                foreach (var surface in planned)
                {
                    surface.Owner = Spawn(surface);
                    Require(surface.Owner != null, "Native factory did not create " + surface.Kind + ".");
                }
                return session;
            }
            catch { session.Dispose(); throw; }
        }

        internal static IReadOnlyDictionary<string, IReadOnlyList<int>> RequiredReservoirPairCells(IReadOnlyDictionary<string, int> placements)
            => Plan(placements, requireClear: false, reservoirPair: true).ToDictionary(surface => surface.Kind, surface => surface.ReservedCells, StringComparer.Ordinal);

        internal void ObserveReservoirBinding(string kind, FluidContainerHandle handle)
        {
            RequireUsable(); var surface = Find(kind);
            Require(IsReady, PendingReason);
            Require(MixtureInspection.TryResolveContainerBinding(MixturePresentationRuntime.AcquireFrame(), handle, out _, out var error),
                "A completed owned reservoir view is required: " + error);
            surface.ContainerHandle = handle; surface.InventoryId = handle.Id; surface.OwnedLabel = "Owned reservoir contents";
        }

        internal bool IsReady
        {
            get
            {
                RequireUsable();
                var pending = surfaces.FirstOrDefault(surface => !Spawned(surface.Owner));
                if (pending != null) PendingReason = "Waiting for native KMonoBehaviour spawn: " + pending.Kind + ".";
                else if (Time.frameCount <= createdFrame + 1) PendingReason = "Waiting for a completed post-spawn UI frame.";
                else
                {
                    // A paused native simulation may not yet acknowledge the
                    // pipe's StructureTemperatureRegistered event. That is
                    // reported separately, never faked by a network shortcut.
                    PendingReason = ""; return true;
                }
                if (Time.realtimeSinceStartup - createdTime > 20) throw new TimeoutException(PendingReason);
                return false;
            }
        }

        internal void Select(string surfaceKind)
        {
            RequireUsable();
            Require(IsReady, PendingReason);
            var surface = Find(surfaceKind);
            var selectable = surface.Owner.GetComponent<KSelectable>();
            Require(selectable != null && selectable.isSpawned, "Native selectable has not spawned: " + surfaceKind + ".");
            selectedKind = surfaceKind; selectedFrame = Time.frameCount;
            SelectTool.Instance.Select(selectable, true);
        }

        // The parent captures its screenshot after a later UI frame, then
        // calls this method. Evidence is retained even when an assertion fails.
        internal SelectionEvidence ValidateSelected(string surfaceKind)
        {
            var result = ReadSelected(surfaceKind);
            if (!result.Passed) throw new InvalidOperationException("Spawned " + surfaceKind + " inspection failed: " + string.Join("; ", result.Errors));
            return result;
        }

        internal SelectionEvidence ReadSelected(string surfaceKind)
        {
            RequireUsable();
            var surface = Find(surfaceKind);
            var result = new SelectionEvidence
            {
                Surface = surfaceKind, Cell = surface.Cell, InstanceId = surface.InstanceId,
                OwnerName = surface.Owner != null ? surface.Owner.name : "destroyed", InventoryId = surface.InventoryId,
                UnityFrame = Time.frameCount, SelectedFrame = selectedFrame,
                NativeSpawned = Spawned(surface.Owner), NativeConduitRegistered = PipeRegistered(surface)
            };
            var errors = new List<string>();
            void Expect(bool passes, string message) { if (!passes) errors.Add(message); }
            try
            {
                var owner = surface.Owner;
                Require(owner != null, "The fixture owner was destroyed.");
                Expect(result.NativeSpawned, "Native object is not fully spawned.");
                Expect(selectedKind == surfaceKind && Time.frameCount > selectedFrame + 1, "No completed UI frame after this surface was selected.");
                result.CorrectSelection = SelectTool.Instance != null && SelectTool.Instance.selected == owner.GetComponent<KSelectable>();
                Expect(result.CorrectSelection, "SelectTool is not selecting the fixture owner.");
                var frame = MixturePresentationRuntime.AcquireFrame();
                MixturePresentation? reading = null;
                var resolved = surface.ContainerHandle.HasValue
                    ? frame != null && frame.TryResolveInventory(surface.ContainerHandle.Value, out reading)
                    : MixturePresentationRuntime.TryResolveInventory(surface.InventoryId, out reading);
                Require(resolved && reading != null,
                    "The explicit adapter has no completed-frame inventory.");
                result.SourceId = reading!.SourceId; result.Revision = reading.Revision;
                result.ExactOwnedAllocation = surface.ContainerHandle.HasValue;
                result.IndependentInventoryProviders = MixturePresentationRuntime.InventoryProviderCount;
                result.SharedCompletedSnapshot = !surface.ContainerHandle.HasValue ||
                    (frame != null && frame.Revision == reading.Revision && result.IndependentInventoryProviders == 0);
                Expect(result.SharedCompletedSnapshot, "Owned container is not using the same completed world frame or an independent provider remains.");
                Expect(reading.SourceId == surface.InventoryId, "A different inventory source was resolved.");
                Expect(!surface.ExpectHazardousTrace || (reading.Constituents.Any(row => row.IsTrace) && reading.Warnings.Count > 0),
                    "The supplied conserved fixture must contain a positive trace and an explicit breathing warning.");
                var screen = Resources.FindObjectsOfTypeAll<SimpleInfoScreen>().FirstOrDefault(value => value.gameObject.activeInHierarchy &&
                    ReferenceEquals(AccessTools.Field(typeof(SimpleInfoScreen), "selectedTarget").GetValue(value), owner));
                result.CorrectPanelTarget = screen != null;
                Expect(result.CorrectPanelTarget, "No active native SimpleInfoScreen targets the selected fixture.");
                Require(screen != null, "The selected object's native information panel is unavailable.");
                var panel = AccessTools.Field(typeof(SimpleInfoScreen), "infoPanel").GetValue(screen) as CollapsibleDetailContentPanel;
                Require(panel != null && panel.gameObject.activeInHierarchy, "Native information content is collapsed or inactive.");
                result.Labels = panel!.Content.GetComponentsInChildren<DetailLabel>(true).Select(ReadLabel).OrderBy(label => label.Sibling).ToArray();
                var active = result.Labels.Where(label => label.ActiveInHierarchy).ToArray();
                LabelEvidence? Row(string key) => active.SingleOrDefault(label => label.Name == "MixedAtmospheres.Inventory.0" + key);
                result.FixtureTitlePresent = Row("")?.Text == NativeLabelText(surface.FixtureLabel + " · " + MixtureInspection.FormatHeader(reading));
                Expect(result.FixtureTitlePresent, "The explicit read-only inventory title is absent or stale.");
                var description = NativeDescription(owner);
                var nativeDescription = active.SingleOrDefault(label => label.Name == "Description");
                result.RawNativeDescription = description;
                result.ExpectedNativeDescription = NativeLabelText(description);
                result.ActualNativeDescription = nativeDescription?.Text ?? "";
                result.NativeDescriptionPreserved = DescriptionIsPreserved(description, nativeDescription);
                Expect(result.NativeDescriptionPreserved, "The object's native description was changed or hidden.");
                var expectedRows = MixtureInspection.BuildDetailRows(reading);
                Expect(active.Count(label => label.Name.StartsWith("MixedAtmospheres.Inventory.0", StringComparison.Ordinal)) == expectedRows.Count + 1,
                    "A stale owned mixture row remains after the reading changed.");
                result.CompleteCompositionPresent = expectedRows.All(expected =>
                {
                    var actual = Row("." + expected.Key);
                    return actual != null && actual.TextEnabled && actual.Text == NativeLabelText(expected.Text) && actual.Height > 0;
                });
                Expect(result.CompleteCompositionPresent, "A complete measured constituent/state row is absent, stale or has zero layout height.");
                var density = Row(".GasDensity"); var composition = Row(".Composition");
                result.WarningsPresentBeforeComposition = density != null && composition != null;
                for (var i = 0; i < reading.Warnings.Count; ++i)
                {
                    var warning = Row(".Warning." + i);
                    result.WarningsPresentBeforeComposition &= warning != null && warning.TextEnabled && warning.Text == NativeLabelText(reading.Warnings[i]) && warning.Height > 0 &&
                        warning.Sibling < density!.Sibling && warning.Sibling < composition!.Sibling;
                }
                Expect(result.WarningsPresentBeforeComposition, "A warning is absent or appears below density/composition.");
                result.OwnedInspectionBeforeNativeDescription = nativeDescription == null ||
                    (Row("") is LabelEvidence title && title.Sibling < nativeDescription.Sibling && expectedRows.All(expected =>
                        Row("." + expected.Key) is LabelEvidence actual && actual.Sibling < nativeDescription.Sibling));
                Expect(result.OwnedInspectionBeforeNativeDescription, "The owned mixture inspection appears below the object's native description.");
                result.NoNativeSubstitute = active.All(label => label.Name != "MixedAtmospheres.NativeInventory" &&
                    label.Name != "MixedAtmospheres.InventoryUnavailable" && label.Name != "MixedAtmospheres.NativeUnavailable");
                Expect(result.NoNativeSubstitute, "Native fallback or an unavailable adapter substituted for the fixture reading.");
            }
            catch (Exception error) { errors.Add(error.GetType().Name + ": " + error.Message); }
            result.Errors = errors.ToArray(); evidence.Add(result); return result;
        }

        internal static IReadOnlyDictionary<string, IReadOnlyList<int>> RequiredCells(IReadOnlyDictionary<string, int> placements)
            => Plan(placements, requireClear: false).ToDictionary(surface => surface.Kind, surface => surface.ReservedCells, StringComparer.Ordinal);

        internal void Rebind(string kind, FluidContainerHandle replacement, bool expectHazardousTrace)
        {
            RequireUsable(); var surface = Find(kind);
            Require(MixtureInspection.TryResolveContainerBinding(MixturePresentationRuntime.AcquireFrame(), replacement, out _, out _), "Replacement allocation is unavailable.");
            MixtureInspection.RegisterContainerOwner(surface.Owner, replacement, surface.FixtureLabel);
            surface.ContainerHandle = replacement; surface.InventoryId = replacement.Id; surface.ExpectHazardousTrace = expectHazardousTrace;
        }

        internal void ValidateUnavailable(string kind)
        {
            RequireUsable(); var surface = Find(kind);
            Require(surface.ContainerHandle.HasValue, "An exact owned binding is required.");
            var text = MixtureInspection.GetContainerInspectionText(surface.Owner);
            var screen = Resources.FindObjectsOfTypeAll<SimpleInfoScreen>().FirstOrDefault(value => value.gameObject.activeInHierarchy &&
                ReferenceEquals(AccessTools.Field(typeof(SimpleInfoScreen), "selectedTarget").GetValue(value), surface.Owner));
            var panel = screen == null ? null : AccessTools.Field(typeof(SimpleInfoScreen), "infoPanel").GetValue(screen) as CollapsibleDetailContentPanel;
            var labels = panel == null ? Array.Empty<LabelEvidence>() : panel.Content.GetComponentsInChildren<DetailLabel>(true).Select(ReadLabel).Where(label => label.ActiveInHierarchy).ToArray();
            var unavailable = labels.SingleOrDefault(label => label.Name == "MixedAtmospheres.InventoryUnavailable");
            var passed = unavailable != null && unavailable.TextEnabled && unavailable.Height > 0 && unavailable.Text.Contains("retired, replaced") &&
                text.Contains("retired, replaced") && !text.Contains("Native inventory") &&
                labels.All(label => !label.Name.StartsWith("MixedAtmospheres.Inventory.", StringComparison.Ordinal) && label.Name != "MixedAtmospheres.NativeInventory");
            allocationEvidence.Add(new { Kind = kind, UnityFrame = Time.frameCount, Handle = surface.ContainerHandle.Value, Inspection = text, Labels = labels, Passed = passed });
            Require(passed, "Retired owned binding did not show an explicit unavailable status or retained stale/native rows.");
        }

        private static List<Surface> Plan(IReadOnlyDictionary<string, int> placements, bool requireClear = true, bool reservoirPair = false)
        {
            RequirePresentation();
            if (placements == null) throw new ArgumentNullException(nameof(placements));
            Require(Game.Instance != null && ClusterManager.Instance != null && Grid.WidthInCells > 0 && Grid.HeightInCells > 0,
                "A live initialized colony is required.");
            var kinds = reservoirPair ? new[] { "reservoir-a", "reservoir-b" } : Kinds;
            Require(placements.Count == kinds.Length && kinds.All(placements.ContainsKey), "The complete explicitly requested native surface placements are required.");
            var planned = new List<Surface>(); var reserved = new HashSet<int>();
            foreach (var kind in kinds)
            {
                var surface = new Surface { Kind = kind, Cell = placements[kind] };
                var footprint = new HashSet<int>(); var cells = new HashSet<int>();
                void AddOffset(CellOffset offset, bool body)
                {
                    var x = surface.Cell % Grid.WidthInCells + offset.x; var y = surface.Cell / Grid.WidthInCells + offset.y;
                    Require(x >= 0 && x < Grid.WidthInCells && y >= 0 && y < Grid.HeightInCells, kind + " footprint or port crosses the grid boundary.");
                    var cell = x + y * Grid.WidthInCells;
                    Require(Grid.IsValidBuildingCell(cell) && Grid.IsActiveWorld(cell), kind + " must be inside the active world's valid building cells.");
                    if (body) footprint.Add(cell); cells.Add(cell);
                }
                if (BuildingIds.TryGetValue(reservoirPair ? "reservoir" : kind, out var id))
                {
                    var definition = Assets.GetBuildingDef(id);
                    Require(definition != null && definition.BuildingComplete != null, "Native building definition unavailable: " + id + ".");
                    surface.Definition = definition;
                    foreach (var offset in definition.PlacementOffsets) AddOffset(offset, true);
                    if (definition.InputConduitType != ConduitType.None) AddOffset(definition.UtilityInputOffset, false);
                    if (definition.OutputConduitType != ConduitType.None) AddOffset(definition.UtilityOutputOffset, false);
                    if (definition.RequiresPowerInput) AddOffset(definition.PowerInputOffset, false);
                    if (definition.RequiresPowerOutput) AddOffset(definition.PowerOutputOffset, false);
                    foreach (var input in definition.BuildingComplete.GetComponents<ISecondaryInput>())
                        foreach (ConduitType type in new[] { ConduitType.Gas, ConduitType.Liquid, ConduitType.Solid })
                            if (input.HasSecondaryConduitType(type)) AddOffset(input.GetSecondaryConduitOffset(type), false);
                    foreach (var output in definition.BuildingComplete.GetComponents<ISecondaryOutput>())
                        foreach (ConduitType type in new[] { ConduitType.Gas, ConduitType.Liquid, ConduitType.Solid })
                            if (output.HasSecondaryConduitType(type)) AddOffset(output.GetSecondaryConduitOffset(type), false);
                    Require(definition.DefaultElements().Count == definition.MaterialCategory.Length, "Native construction materials are unavailable for " + id + ".");
                    if (requireClear)
                        Require(definition.IsValidBuildLocation(definition.BuildingComplete, surface.Cell, Orientation.Neutral, false, out var reason),
                            kind + " is not a valid native building location: " + reason);
                }
                else
                {
                    AddOffset(new CellOffset(0, 0), true);
                    var below = Grid.CellBelow(surface.Cell);
                    if (requireClear)
                        Require(Grid.IsValidCell(below) && Grid.Solid[below], kind + " needs a reserved supporting floor so the loose object cannot fall away.");
                    Require(Assets.GetPrefab(kind == "bottle" ? "Oxygen" : "Atmo_Suit") != null, "Native loose-object prefab is unavailable: " + kind + ".");
                }
                foreach (var cell in cells)
                {
                    if (requireClear)
                    {
                        Require(!Grid.Solid[cell], kind + " reserved cell (" + cell % Grid.WidthInCells + "," + cell / Grid.WidthInCells +
                            ") contains solid " + Grid.Element[cell]?.id + " at " + Grid.Mass[cell] + " kg.");
                        for (var layer = 0; layer < (int)ObjectLayer.NumLayers; ++layer)
                            Require(Grid.Objects[cell, layer] == null, kind + " reserved cell " + cell + " already contains an object on layer " + layer + ".");
                    }
                    Require(reserved.Add(cell), "Fixture footprints or ports overlap at cell " + cell + ".");
                }
                surface.Footprint = footprint.OrderBy(cell => cell).ToArray();
                surface.ReservedCells = cells.OrderBy(cell => cell).ToArray(); planned.Add(surface);
            }
            return planned;
        }

        private static GameObject Spawn(Surface surface)
        {
            if (surface.Definition != null)
                return surface.Definition.Build(surface.Cell, Orientation.Neutral, null, surface.Definition.DefaultElements(), 293.15f, playsound: false);
            var position = Grid.CellToPosCCC(surface.Cell, Grid.SceneLayer.Ore);
            if (surface.Kind == "bottle")
                return ElementLoader.FindElementByHash(SimHashes.Oxygen).substance.SpawnResource(position, 1, 293.15f, byte.MaxValue, 0, prevent_merge: true);
            var owner = GameUtil.KInstantiate(Assets.GetPrefab("Atmo_Suit"), position, Grid.SceneLayer.Ore);
            owner.SetActive(true); return owner;
        }

        private static bool Spawned(GameObject? owner) => owner != null && owner.activeInHierarchy && owner.GetComponent<KSelectable>() != null &&
            owner.GetComponents<KMonoBehaviour>().Where(component => component.enabled).All(component => component.isSpawned);

        private static bool PipeRegistered(Surface surface)
        {
            if (surface.Kind != "pipe" || surface.Owner == null || !Spawned(surface.Owner)) return false;
            var conduit = surface.Owner.GetComponent<Conduit>();
            if (conduit == null) return false;
            var flow = conduit.GetFlowManager();
            var grid = flow == null ? null : AccessTools.Field(typeof(ConduitFlow), "grid").GetValue(flow) as Array;
            var cell = conduit.Cell;
            return flow != null && grid != null && cell >= 0 && cell < grid.Length && flow.HasConduit(cell);
        }

        private static LabelEvidence ReadLabel(DetailLabel detail)
        {
            var text = typeof(DetailLabel).GetField("label")!.GetValue(detail);
            var rect = detail.transform as RectTransform; var corners = new Vector3[4];
            if (rect != null) rect.GetWorldCorners(corners);
            return new LabelEvidence
            {
                Name = detail.gameObject.name, Text = (string?)text?.GetType().GetProperty("text")?.GetValue(text) ?? "",
                Tooltip = detail.toolTip != null && detail.toolTip.multiStringCount > 0 ? detail.toolTip.GetMultiString(0) : "",
                ActiveSelf = detail.gameObject.activeSelf, ActiveInHierarchy = detail.gameObject.activeInHierarchy,
                TextEnabled = text is Behaviour behaviour && behaviour.isActiveAndEnabled,
                Sibling = detail.transform.GetSiblingIndex(), Height = rect?.rect.height ?? 0,
                Top = corners.Max(point => point.y), Bottom = corners.Min(point => point.y)
            };
        }

        private static string NativeDescription(GameObject owner)
        {
            if (owner.GetComponent<MinionIdentity>() != null) return "";
            var info = owner.GetComponent<InfoDescription>();
            if (info != null) return info.description ?? "";
            var building = owner.GetComponent<BuildingComplete>();
            if (building != null) return building.DescFlavour + "\n\n" + building.Desc;
            var construction = owner.GetComponent<BuildingUnderConstruction>();
            if (construction != null) return construction.DescFlavour + "\n\n" + construction.Desc;
            var edible = owner.GetComponent<Edible>();
            if (edible != null) return string.Format(STRINGS.UI.GAMEOBJECTEFFECTS.CALORIES, GameUtil.GetFormattedCalories(edible.FoodInfo.CaloriesPerUnit));
            var proxy = owner.GetComponent<ICellSelectionProxy>();
            if (proxy != null && proxy.Element != null) return proxy.Element.FullDescription(addHardnessColor: false);
            var primary = owner.GetComponent<PrimaryElement>();
            var element = primary != null ? ElementLoader.FindElementByHash(primary.ElementID) : null;
            return element != null ? element.FullDescription(addHardnessColor: false) : "";
        }

        // SetLabel enables AllowLinks before assigning LocText.text. The native
        // setter parses hotkey/click tokens and decorates links, so comparing
        // its displayed string with an unprocessed Building.Desc is incorrect.
        // Use the exact native pure transformations; never strip rich text or
        // mutate the live label to make a preservation assertion pass.
        internal static string NativeLabelText(string text)
        {
            if (text == null) throw new ArgumentNullException(nameof(text));
            var type = typeof(DetailLabel).GetField("label")!.FieldType;
            var parsed = AccessTools.Method(type, "ParseText").Invoke(null, new object[] { text }) as string
                ?? throw new NotSupportedException("Native label ParseText did not return text.");
            return AccessTools.Method(type, "ModifyLinkStrings").Invoke(null, new object[] { parsed }) as string
                ?? throw new NotSupportedException("Native label ModifyLinkStrings did not return text.");
        }

        internal static bool DescriptionIsPreserved(string raw, LabelEvidence? activeDescription) => raw.Length == 0
            ? activeDescription == null
            : activeDescription != null && activeDescription.ActiveInHierarchy && activeDescription.TextEnabled &&
                activeDescription.Height > 0 && activeDescription.Text == NativeLabelText(raw);

        private Surface Find(string kind) => surfaces.FirstOrDefault(surface => surface.Kind == kind) ?? throw new ArgumentException("Unknown container surface.", nameof(kind));
        private static void Require([DoesNotReturnIf(false)] bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
        private static void RequirePresentation() => Require(ColonyRuntimeValidation.Settings?.Mode == "presentation", "Spawned container fixtures require explicit private presentation mode.");
        private void RequireUsable() { RequirePresentation(); if (disposed) throw new ObjectDisposedException(nameof(MixtureContainerVisualValidation)); }

        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            if (SelectTool.Instance != null && surfaces.Any(surface => surface.Owner != null && SelectTool.Instance.selected == surface.Owner.GetComponent<KSelectable>()))
                SelectTool.Instance.Select(null, true);
            foreach (var surface in surfaces)
            {
                if (ReferenceEquals(surface.Owner, null)) continue;
                if (surface.ContainerHandle.HasValue) MixtureInspection.UnregisterContainerOwner(surface.Owner, surface.ContainerHandle.Value);
                else MixtureInspection.UnregisterInventory(surface.Owner, surface.InventoryId);
                if (surface.Owner != null) Util.KDestroyGameObject(surface.Owner);
            }
        }
    }
}
