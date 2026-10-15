using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Reflection;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal enum OwnedReservoirOccupant { Absent, SameOwner, Other }
    internal readonly struct OwnedReservoirOffset : IEquatable<OwnedReservoirOffset>
    {
        internal int X { get; }
        internal int Y { get; }
        internal OwnedReservoirOffset(int x, int y) { X = x; Y = y; }
        public bool Equals(OwnedReservoirOffset other) => X == other.X && Y == other.Y;
        public override bool Equals(object? obj) => obj is OwnedReservoirOffset other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(X, Y);
    }
    internal sealed class OwnedReservoirFootprintCell
    {
        internal int Cell { get; }
        internal int NativeWorldId { get; }
        internal int CoreWorldId { get; }
        internal bool IsSolid { get; }
        internal OwnedReservoirOccupant BuildingOccupant { get; }
        internal OwnedReservoirFootprintCell(int cell, int nativeWorldId, int coreWorldId, bool solid, OwnedReservoirOccupant occupant)
        { Cell = cell; NativeWorldId = nativeWorldId; CoreWorldId = coreWorldId; IsSolid = solid; BuildingOccupant = occupant; }
    }
    internal sealed class OwnedReservoirFootprintPort
    {
        internal bool IsInput { get; }
        internal int Cell { get; }
        internal int CachedCell { get; }
        internal int ConduitType { get; }
        internal OwnedReservoirOccupant ConnectionOccupant { get; }
        internal bool GasConduitPresent { get; }
        internal OwnedReservoirFootprintPort(bool input, int cell, int cachedCell, int conduitType, OwnedReservoirOccupant connectionOccupant, bool gasConduitPresent)
        { IsInput = input; Cell = cell; CachedCell = cachedCell; ConduitType = conduitType; ConnectionOccupant = connectionOccupant; GasConduitPresent = gasConduitPresent; }
    }
    internal sealed class OwnedReservoirFootprintComponent
    {
        internal string Role { get; }
        internal int Count { get; }
        internal bool IsSpawned { get; }
        internal bool Enabled { get; }
        internal OwnedReservoirFootprintComponent(string role, int count, bool spawned, bool enabled)
        { Role = role; Count = count; IsSpawned = spawned; Enabled = enabled; }
    }
    internal sealed class OwnedReservoirFootprintIdentity
    {
        internal Guid NativeColonyId { get; }
        internal int SavedInstanceId { get; }
        internal string PrefabId { get; }
        internal string DefinitionPrefabId { get; }
        internal int AnchorCell { get; }
        internal int WorldId { get; }
        internal int Orientation { get; }
        internal int DefinitionWidth { get; }
        internal int DefinitionHeight { get; }
        internal int ObjectLayer { get; }
        internal int ConstructionElementId { get; }
        internal double ConstructionMassKg { get; }
        internal double CapacityKg { get; }
        internal bool Active { get; }
        internal bool PendingDestruction { get; }
        internal bool Conflicted { get; }
        internal bool EndOfLife { get; }
        internal bool NativeItemsEmpty { get; }
        internal bool GlobalTrackerMatches { get; }
        internal OwnedReservoirOffset InputOffset { get; }
        internal OwnedReservoirOffset OutputOffset { get; }
        internal int DefinitionInputConduitType { get; }
        internal int DefinitionOutputConduitType { get; }
        internal OwnedReservoirFootprintIdentity(Guid nativeColonyId, int savedInstanceId, string prefabId, string definitionPrefabId,
            int anchorCell, int worldId, int orientation, int definitionWidth, int definitionHeight, int objectLayer,
            int constructionElementId, double constructionMassKg, double capacityKg, bool active, bool pendingDestruction,
            bool conflicted, bool endOfLife, bool nativeItemsEmpty, OwnedReservoirOffset inputOffset, OwnedReservoirOffset outputOffset,
            int definitionInputConduitType = (int)ConduitType.Gas, int definitionOutputConduitType = (int)ConduitType.Gas,
            bool globalTrackerMatches = false)
        {
            NativeColonyId = nativeColonyId; SavedInstanceId = savedInstanceId; PrefabId = prefabId; DefinitionPrefabId = definitionPrefabId;
            AnchorCell = anchorCell; WorldId = worldId; Orientation = orientation; DefinitionWidth = definitionWidth; DefinitionHeight = definitionHeight;
            ObjectLayer = objectLayer; ConstructionElementId = constructionElementId; ConstructionMassKg = constructionMassKg; CapacityKg = capacityKg;
            Active = active; PendingDestruction = pendingDestruction; Conflicted = conflicted; EndOfLife = endOfLife; NativeItemsEmpty = nativeItemsEmpty;
            GlobalTrackerMatches = globalTrackerMatches;
            InputOffset = inputOffset; OutputOffset = outputOffset;
            DefinitionInputConduitType = definitionInputConduitType; DefinitionOutputConduitType = definitionOutputConduitType;
        }
    }
    // Copied facts only. No Unity references, deferred callbacks or permission
    // to save/activate are retained. Native capture and pure validation differ.
    internal sealed class OwnedReservoirFootprintObservation
    {
        internal ColonyFrameSnapshot Source { get; }
        internal FluidContainerSnapshot? Allocation { get; }
        internal OwnedReservoirFootprintIdentity Identity { get; }
        internal OwnedReservoirMarkerRecord Marker { get; }
        internal IReadOnlyList<OwnedReservoirOffset> DefinitionOffsets { get; }
        internal IReadOnlyList<int>? BuildingCells { get; }
        internal IReadOnlyList<OwnedReservoirOffset> OccupyUnrotatedOffsets { get; }
        internal IReadOnlyList<OwnedReservoirOffset> OccupyRotatedOffsets { get; }
        internal IReadOnlyList<int>? OccupyCachedCells { get; }
        internal IReadOnlyList<int> OccupyLayers { get; }
        internal bool OccupyAppliesToCells { get; }
        internal IReadOnlyList<OwnedReservoirFootprintCell> Cells { get; }
        internal IReadOnlyList<OwnedReservoirFootprintPort> Ports { get; }
        internal IReadOnlyList<OwnedReservoirFootprintComponent> Components { get; }
        internal OwnedReservoirFootprintObservation(ColonyFrameSnapshot source, FluidContainerSnapshot? allocation,
            OwnedReservoirFootprintIdentity identity, OwnedReservoirMarkerRecord marker, IReadOnlyList<OwnedReservoirOffset> definitionOffsets,
            IReadOnlyList<int>? buildingCells, IReadOnlyList<OwnedReservoirOffset> occupyUnrotatedOffsets,
            IReadOnlyList<OwnedReservoirOffset> occupyRotatedOffsets, IReadOnlyList<int>? occupyCachedCells, IReadOnlyList<int> occupyLayers,
            bool occupyAppliesToCells, IReadOnlyList<OwnedReservoirFootprintCell> cells, IReadOnlyList<OwnedReservoirFootprintPort> ports,
            IReadOnlyList<OwnedReservoirFootprintComponent> components)
        {
            Source = source ?? throw new ArgumentNullException(nameof(source)); Allocation = allocation;
            Identity = identity ?? throw new ArgumentNullException(nameof(identity)); Marker = marker ?? throw new ArgumentNullException(nameof(marker));
            DefinitionOffsets = Copy(definitionOffsets); BuildingCells = buildingCells == null ? null : Copy(buildingCells);
            OccupyUnrotatedOffsets = Copy(occupyUnrotatedOffsets); OccupyRotatedOffsets = Copy(occupyRotatedOffsets);
            OccupyCachedCells = occupyCachedCells == null ? null : Copy(occupyCachedCells); OccupyLayers = Copy(occupyLayers);
            OccupyAppliesToCells = occupyAppliesToCells; Cells = Copy(cells); Ports = Copy(ports); Components = Copy(components);
        }
        private static IReadOnlyList<T> Copy<T>(IReadOnlyList<T> values)
        {
            if (values == null || values.Count > 64) throw new ArgumentException("A bounded copied reservoir observation is required.");
            var copy = new T[values.Count]; for (var i = 0; i < copy.Length; ++i) copy[i] = values[i];
            return new ReadOnlyCollection<T>(copy);
        }
    }
    internal sealed class OwnedReservoirValidatedFootprints
    {
        internal IReadOnlyList<OwnedReservoirFootprintObservation> Owners { get; }
        internal bool ManifestValidated { get; }
        internal OwnedReservoirValidatedFootprints(OwnedReservoirFootprintObservation a, OwnedReservoirFootprintObservation b, bool manifest)
        { Owners = new ReadOnlyCollection<OwnedReservoirFootprintObservation>(new[] { a, b }); ManifestValidated = manifest; }
    }
    internal static class OwnedReservoirFootprintValidation
    {
        internal static OwnedReservoirOffset Rotate(OwnedReservoirOffset value, int orientation) => orientation switch
        {
            0 => value, 1 => new OwnedReservoirOffset(value.Y, -value.X), 2 => new OwnedReservoirOffset(-value.X, -value.Y),
            3 => new OwnedReservoirOffset(-value.Y, value.X), 5 => new OwnedReservoirOffset(-value.X, value.Y),
            6 => new OwnedReservoirOffset(value.X, -value.Y), _ => throw new InvalidOperationException("Unsupported reservoir orientation.")
        };
        internal static int CellAt(ColonyFrameSnapshot source, int anchor, OwnedReservoirOffset offset)
        {
            if (anchor < 0 || (long)anchor >= (long)source.Width * source.Height) throw new InvalidOperationException("Reservoir anchor is outside the declared grid.");
            var x = (long)(anchor % source.Width) + offset.X; var y = (long)(anchor / source.Width) + offset.Y;
            if (x < 0 || x >= source.Width || y < 0 || y >= source.Height) throw new InvalidOperationException("Reservoir offset crosses grid bounds or wraps a row.");
            return checked((int)(x + y * source.Width));
        }
        internal static IReadOnlyList<OwnedReservoirOffset> PinnedOffsets()
        {
            var offsets = new OwnedReservoirOffset[15]; var index = 0;
            for (var y = 0; y < 3; ++y) for (var x = -2; x <= 2; ++x) offsets[index++] = new OwnedReservoirOffset(x, y);
            return new ReadOnlyCollection<OwnedReservoirOffset>(offsets);
        }
        internal static OwnedReservoirValidatedFootprints ValidatePair(IReadOnlyList<OwnedReservoirFootprintObservation> observations,
            IReadOnlyList<OwnedReservoirCheckpointOwner>? expectedOwners = null, Guid expectedNativeColonyId = default)
        {
            if (observations == null || observations.Count != 2 || observations[0] == null || observations[1] == null)
                throw new InvalidOperationException("Exactly two copied reservoir observations are required.");
            var a = observations[0]; var b = observations[1];
            if (!ReferenceEquals(a.Source, b.Source) || a.Identity.SavedInstanceId == b.Identity.SavedInstanceId ||
                a.Identity.NativeColonyId != b.Identity.NativeColonyId)
                throw new InvalidOperationException("Reservoir observations differ in completed source/colony or reuse a native owner identity.");
            var all = new HashSet<int>();
            foreach (var observation in new[] { a, b })
            {
                ValidateOne(observation);
                foreach (var cell in observation.Cells) if (!all.Add(cell.Cell)) throw new InvalidOperationException("Reservoir footprints overlap at cell " + cell.Cell + ".");
            }
            if (expectedOwners != null)
            {
                if (expectedNativeColonyId == Guid.Empty || expectedOwners.Count != 2 || a.Source.Inventories.Count != 0)
                    throw new InvalidOperationException("Manifest validation requires exactly two records, a native colony and an empty Core world domain.");
                var ids = new HashSet<int>(); var tokens = new HashSet<Guid>(); var handles = new HashSet<FluidContainerHandle>();
                foreach (var owner in expectedOwners)
                {
                    if (owner == null || !ids.Add(owner.SavedInstanceId) || !tokens.Add(owner.PersistentOwnerToken) || !handles.Add(owner.Container))
                        throw new InvalidOperationException("Manifest owner identities, tokens or full handles are duplicate.");
                    var observation = a.Identity.SavedInstanceId == owner.SavedInstanceId ? a : b;
                    ValidateManifestOwner(observation, owner, expectedNativeColonyId);
                }
                foreach (var allocation in a.Source.Containers.Values)
                    if ((allocation.Inventory.MassesKg.Count != 0 || allocation.Inventory.DiseaseCounts.Count != 0 || allocation.Inventory.RadiationQuantity != 0) && !handles.Contains(allocation.Handle))
                        throw new InvalidOperationException("A nonempty Core allocation has no exact manifest owner.");
            }
            return new OwnedReservoirValidatedFootprints(a, b, expectedOwners != null);
        }
        private static void ValidateOne(OwnedReservoirFootprintObservation value)
        {
            var id = value.Identity;
            if (id.NativeColonyId == Guid.Empty || id.SavedInstanceId < 0 || id.PrefabId != "GasReservoir" || id.DefinitionPrefabId != "GasReservoir" ||
                id.WorldId < 0 || id.DefinitionWidth != 5 || id.DefinitionHeight != 3 || id.ObjectLayer != (int)ObjectLayer.Building ||
                !id.Active || id.PendingDestruction || id.Conflicted || id.EndOfLife || !id.NativeItemsEmpty || !id.GlobalTrackerMatches || id.CapacityKg != 1000 ||
                !Finite(id.ConstructionMassKg) || id.ConstructionMassKg < 0 || (double)(float)id.ConstructionMassKg != id.ConstructionMassKg ||
                id.DefinitionInputConduitType != (int)ConduitType.Gas || id.DefinitionOutputConduitType != (int)ConduitType.Gas ||
                !id.InputOffset.Equals(new OwnedReservoirOffset(1, 2)) || !id.OutputOffset.Equals(new OwnedReservoirOffset(0, 0)) ||
                value.Marker.State == OwnedReservoirMarkerState.Invalid)
                throw new InvalidOperationException("Reservoir identity, definition, lifecycle, native packet or marker contract is invalid.");
            var roles = new Dictionary<string, OwnedReservoirFootprintComponent>(StringComparer.Ordinal);
            foreach (var role in value.Components)
                if (role == null || roles.ContainsKey(role.Role)) throw new InvalidOperationException("Duplicate or absent component diagnostic."); else roles.Add(role.Role, role);
            foreach (var name in new[] { "storage", "prefab", "construction", "building", "occupy-area", "reservoir", "smart", "consumer", "dispenser", "marker" })
                if (!roles.TryGetValue(name, out var role) || role.Count != 1 || !role.IsSpawned) throw new InvalidOperationException("Native required role is absent, duplicated or unspawned: " + name);
            if (roles.TryGetValue("orientation", out var rotation) && (rotation.Count != 1 || !rotation.IsSpawned))
                throw new InvalidOperationException("Native orientation role is duplicated or unspawned.");
            if (id.Orientation != 0 && !roles.ContainsKey("orientation")) throw new InvalidOperationException("Nonneutral native orientation has no component.");
            var shape = new HashSet<OwnedReservoirOffset>(PinnedOffsets());
            SameOffsets(value.DefinitionOffsets, shape, "definition"); SameOffsets(value.OccupyUnrotatedOffsets, shape, "unrotated occupancy");
            var rotated = new HashSet<OwnedReservoirOffset>(); var cells = new HashSet<int>();
            foreach (var offset in shape)
            {
                var actual = Rotate(offset, id.Orientation); rotated.Add(actual); cells.Add(CellAt(value.Source, id.AnchorCell, actual));
            }
            SameOffsets(value.OccupyRotatedOffsets, rotated, "rotated occupancy"); SameCells(value.BuildingCells, cells, "building placement cache");
            if (value.OccupyLayers.Count != 0)
            {
                if (!value.OccupyAppliesToCells) throw new InvalidOperationException("Configured native occupancy layers are inactive.");
                var layers = new HashSet<int>();
                foreach (var layer in value.OccupyLayers) if (layer != (int)ObjectLayer.Building || !layers.Add(layer)) throw new InvalidOperationException("Unsupported or duplicate native occupancy layer.");
                SameCells(value.OccupyCachedCells, cells, "occupied-grid cache");
            }
            else if (value.OccupyCachedCells != null && value.OccupyCachedCells.Count != 0) SameCells(value.OccupyCachedCells, cells, "optional occupied-grid cache");
            if (value.Cells.Count != 15) throw new InvalidOperationException("Reservoir must report every footprint cell exactly once.");
            foreach (var cell in value.Cells)
            {
                if (cell == null || !cells.Remove(cell.Cell) || cell.NativeWorldId != id.WorldId || cell.CoreWorldId != id.WorldId || cell.IsSolid ||
                    cell.BuildingOccupant != OwnedReservoirOccupant.SameOwner || !value.Source.TrySampleCell(cell.Cell, out var geometry) || geometry!.WorldId != id.WorldId)
                    throw new InvalidOperationException("Reservoir footprint is missing, foreign, solid or crosses world geometry.");
            }
            if (value.Ports.Count != 2 || value.Ports[0] == null || value.Ports[1] == null || value.Ports[0].IsInput == value.Ports[1].IsInput)
                throw new InvalidOperationException("Both distinct reservoir utility ports are required.");
            foreach (var port in value.Ports)
            {
                var expected = CellAt(value.Source, id.AnchorCell, Rotate(port.IsInput ? id.InputOffset : id.OutputOffset, id.Orientation));
                if (port.Cell != expected || port.CachedCell != expected || port.ConduitType != (int)ConduitType.Gas ||
                    port.ConnectionOccupant != OwnedReservoirOccupant.SameOwner || port.GasConduitPresent)
                    throw new InvalidOperationException("Native utility cell/cache/connection or disconnected-gas contract differs.");
            }
        }
        private static void ValidateManifestOwner(OwnedReservoirFootprintObservation value, OwnedReservoirCheckpointOwner owner, Guid nativeColonyId)
        {
            var id = value.Identity; var marker = value.Marker;
            if (id.SavedInstanceId != owner.SavedInstanceId || id.NativeColonyId != nativeColonyId || marker.NativeColonyId != nativeColonyId ||
                marker.PersistentOwnerToken != owner.PersistentOwnerToken || !marker.Matches(owner.Container) ||
                id.PrefabId != owner.PrefabId || id.AnchorCell != owner.AnchorCell || id.WorldId != owner.WorldId || id.Orientation != owner.Orientation ||
                id.ConstructionElementId != owner.ConstructionElementId || id.ConstructionMassKg != owner.ConstructionMassKg || id.CapacityKg != owner.MaximumMassKg ||
                value.Allocation == null || !ReferenceEquals(value.Allocation, value.Source.SampleContainer(owner.Container)) ||
                value.Allocation.Definition.VolumeCubicMetres != owner.VolumeCubicMetres || value.Allocation.Definition.MaximumMassKg != owner.MaximumMassKg ||
                value.Allocation.Definition.AllowedPhases != owner.AllowedPhases)
                throw new InvalidOperationException("Assigned marker, exact Core allocation or native owner differs from the manifest.");
        }
        private static void SameCells(IReadOnlyList<int>? actual, HashSet<int> expected, string name)
        {
            if (actual == null || actual.Count != expected.Count || !new HashSet<int>(actual).SetEquals(expected) || new HashSet<int>(actual).Count != actual.Count)
                throw new InvalidOperationException("Stale or duplicate reservoir " + name + ".");
        }
        private static void SameOffsets(IReadOnlyList<OwnedReservoirOffset> actual, HashSet<OwnedReservoirOffset> expected, string name)
        {
            if (actual.Count != expected.Count || !new HashSet<OwnedReservoirOffset>(actual).SetEquals(expected) || new HashSet<OwnedReservoirOffset>(actual).Count != actual.Count)
                throw new InvalidOperationException("Unsupported or stale reservoir " + name + ".");
        }
        private static bool Finite(double value) => !double.IsNaN(value) && !double.IsInfinity(value);
    }

    internal static class OwnedReservoirFootprintCapture
    {
        private static readonly FieldInfo BuildingCache = RequireField(typeof(Building), "placementCells", typeof(int[]));
        private static readonly FieldInfo ConsumerCell = RequireField(typeof(ConduitConsumer), "utilityCell", typeof(int));
        private static readonly FieldInfo DispenserCell = RequireField(typeof(ConduitDispenser), "utilityCell", typeof(int));
        private static readonly FieldInfo TrackerInstance = typeof(KPrefabIDTracker).GetField("Instance", BindingFlags.Static | BindingFlags.NonPublic)
            is FieldInfo field && field.FieldType == typeof(KPrefabIDTracker) ? field
            : throw new NotSupportedException("Pinned native saved-ID tracker singleton is unavailable.");
        internal static OwnedReservoirFootprintObservation Capture(Storage storage, ColonyFrameSnapshot source, FluidContainerHandle? container = null)
        {
            OwnedReservoirOwnerMarker.RequireNativeThread();
            if (storage == null || source == null) throw new ArgumentNullException(storage == null ? nameof(storage) : nameof(source));
            var go = storage.gameObject; var references = new List<KMonoBehaviour>(); var components = new List<OwnedReservoirFootprintComponent>();
            T Required<T>(string role) where T : KMonoBehaviour
            {
                var found = go.GetComponents<T>();
                if (found.Length != 1 || found[0] == null || !found[0].isSpawned) throw new InvalidOperationException("Absent, duplicate or unspawned native footprint role: " + role);
                references.Add(found[0]); components.Add(new OwnedReservoirFootprintComponent(role, found.Length, found[0].isSpawned, found[0].enabled)); return found[0];
            }
            if (!ReferenceEquals(Required<Storage>("storage"), storage)) throw new InvalidOperationException("Passed native Storage is not the exact owner component.");
            var prefab = Required<KPrefabID>("prefab"); var construction = Required<PrimaryElement>("construction");
            var savedInstanceId = prefab.InstanceID;
            var tracker = RequireTrackedOwner(prefab, savedInstanceId);
            var building = Required<BuildingComplete>("building"); var occupy = Required<OccupyArea>("occupy-area");
            Required<Reservoir>("reservoir"); Required<SmartReservoir>("smart");
            var consumer = Required<ConduitConsumer>("consumer"); var dispenser = Required<ConduitDispenser>("dispenser"); var marker = Required<OwnedReservoirOwnerMarker>("marker");
            var rotations = go.GetComponents<Rotatable>(); if (rotations.Length > 1) throw new InvalidOperationException("Duplicate native orientation components.");
            var orientation = 0;
            if (rotations.Length == 1) { var rotation = Required<Rotatable>("orientation"); orientation = (int)rotation.Orientation; }
            if (go.GetComponents<Building>().Length != 1) throw new InvalidOperationException("Duplicate native Building roles.");
            var def = building.Def; var anchor = Grid.PosToCell(go);
            if (def == null || !Grid.IsValidCell(anchor) || source.Width != Grid.WidthInCells || source.Height != Grid.HeightInCells)
                throw new InvalidOperationException("Native footprint definition/anchor/grid differs from the supplied immutable geometry.");
            var identity = new OwnedReservoirFootprintIdentity(SaveLoader.Instance.GameInfo.colonyGuid, savedInstanceId, prefab.PrefabTag.Name, def.PrefabID,
                anchor, Grid.WorldIdx[anchor], orientation, def.WidthInCells, def.HeightInCells, (int)def.ObjectLayer, (int)construction.ElementID,
                construction.Mass, storage.capacityKg, go.activeInHierarchy, prefab.pendingDestruction, prefab.conflicted, storage.IsEndOfLife(),
                storage.items != null && storage.items.Count == 0, new OwnedReservoirOffset(def.UtilityInputOffset.x, def.UtilityInputOffset.y),
                new OwnedReservoirOffset(def.UtilityOutputOffset.x, def.UtilityOutputOffset.y), (int)def.InputConduitType, (int)def.OutputConduitType,
                globalTrackerMatches: true);
            var cells = new List<OwnedReservoirFootprintCell>();
            foreach (var offset in OwnedReservoirFootprintValidation.PinnedOffsets())
            {
                var cell = OwnedReservoirFootprintValidation.CellAt(source, anchor, OwnedReservoirFootprintValidation.Rotate(offset, orientation));
                var coreWorld = source.TrySampleCell(cell, out var geometry) ? geometry!.WorldId : -1;
                cells.Add(new OwnedReservoirFootprintCell(cell, Grid.WorldIdx[cell], coreWorld, Grid.Solid[cell], Classify(Grid.Objects[cell, (int)ObjectLayer.Building], go)));
            }
            OwnedReservoirFootprintPort Port(bool input)
            {
                var offset = input ? identity.InputOffset : identity.OutputOffset;
                var cell = OwnedReservoirFootprintValidation.CellAt(source, anchor, OwnedReservoirFootprintValidation.Rotate(offset, orientation));
                return new OwnedReservoirFootprintPort(input, cell, (int)(input ? ConsumerCell.GetValue(consumer) : DispenserCell.GetValue(dispenser))!,
                    (int)(input ? consumer.conduitType : dispenser.conduitType), Classify(Grid.Objects[cell, (int)ObjectLayer.GasConduitConnection], go),
                    Grid.Objects[cell, (int)ObjectLayer.GasConduit] != null);
            }
            if (occupy.objectLayers == null || occupy.objectLayers.Length > 64) throw new InvalidOperationException("Native occupancy layer observation exceeds its bound.");
            var layers = new int[occupy.objectLayers.Length]; for (var i = 0; i < layers.Length; ++i) layers[i] = (int)occupy.objectLayers[i];
            var result = new OwnedReservoirFootprintObservation(source, container.HasValue ? source.SampleContainer(container.Value) : null, identity, marker.ReadRecord(),
                Offsets(def.PlacementOffsets), (int[]?)BuildingCache.GetValue(building), Offsets(occupy._UnrotatedOccupiedCellsOffsets),
                Offsets(occupy._RotatedOccupiedCellsOffsets), occupy.GetOccupiedGridCells(), layers, occupy.ApplyToCells, cells, new[] { Port(true), Port(false) }, components);
            foreach (var reference in references)
            {
                var actual = go.GetComponents(reference.GetType());
                if (reference == null || actual.Length != 1 || !ReferenceEquals(actual[0], reference) || !reference.isSpawned)
                    throw new InvalidOperationException("Native component identity changed during read-only footprint capture.");
            }
            if (!ReferenceEquals(building.Def, def) || go.GetComponents<Building>().Length != 1 || go.GetComponents<Rotatable>().Length != rotations.Length ||
                Grid.PosToCell(go) != anchor || (int)building.Orientation != orientation ||
                (rotations.Length == 0 ? 0 : (int)rotations[0].Orientation) != orientation)
                throw new InvalidOperationException("Native geometry changed during read-only footprint capture.");
            if (!ReferenceEquals(RequireTrackedOwner(prefab, savedInstanceId), tracker))
                throw new InvalidOperationException("Native saved-ID tracker changed during read-only footprint capture.");
            return result;
        }
        private static KPrefabIDTracker RequireTrackedOwner(KPrefabID prefab, int savedInstanceId)
        {
            // The native Get() method creates an absent singleton. Read its
            // pinned field instead, so failed inspection cannot initialize it.
            var tracker = TrackerInstance.GetValue(null) as KPrefabIDTracker;
            if (tracker == null || ReferenceEquals(prefab, null) || savedInstanceId < 0 || prefab.InstanceID != savedInstanceId ||
                !ReferenceEquals(tracker.GetInstance(savedInstanceId), prefab))
                throw new InvalidOperationException("Native saved ID does not resolve to the exact observed owner.");
            return tracker;
        }
        private static IReadOnlyList<OwnedReservoirOffset> Offsets(CellOffset[] offsets)
        {
            if (offsets == null || offsets.Length > 64) throw new InvalidOperationException("Native occupancy offsets are absent or exceed the bounded observation contract.");
            var copy = new OwnedReservoirOffset[offsets.Length]; for (var i = 0; i < copy.Length; ++i) copy[i] = new OwnedReservoirOffset(offsets[i].x, offsets[i].y); return copy;
        }
        private static OwnedReservoirOccupant Classify(GameObject value, GameObject owner)
            => value == null ? OwnedReservoirOccupant.Absent : ReferenceEquals(value, owner) ? OwnedReservoirOccupant.SameOwner : OwnedReservoirOccupant.Other;
        private static FieldInfo RequireField(Type type, string name, Type expected)
        {
            var field = type.GetField(name, BindingFlags.Instance | BindingFlags.NonPublic);
            if (field == null || field.FieldType != expected) throw new NotSupportedException("Pinned native footprint field is unavailable: " + type.Name + "." + name);
            return field;
        }
    }
}
