using System;
using System.Collections.Generic;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Unity-thread registry. Static debris is deliberately absent: its actual
    // transfers and thermal contacts wake regions at the native input boundary.
    internal static class ColonyActivity
    {
        private sealed class Entry
        {
            internal readonly Component Component;
            internal readonly Building? Building;
            internal readonly OccupyArea? Occupancy;
            internal readonly bool Living, Plant;
            internal readonly ISecondaryInput[] Inputs;
            internal readonly ISecondaryOutput[] Outputs;
            internal readonly ConduitConsumer[] Consumers;
            internal readonly ConduitDispenser[] Dispensers;
            internal uint[] Previous = Array.Empty<uint>();
            internal Entry(Component component)
            {
                Component = component; var go = component.gameObject;
                Building = go.GetComponent<Building>(); Occupancy = go.GetComponent<OccupyArea>();
                Living = go.GetComponent<Brain>() != null || go.GetComponent<MinionIdentity>() != null;
                Plant = go.GetComponent<Uprootable>() != null || go.GetComponent<Growing>() != null || go.HasTag(GameTags.Plant);
                Inputs = go.GetComponents<ISecondaryInput>(); Outputs = go.GetComponents<ISecondaryOutput>();
                Consumers = go.GetComponents<ConduitConsumer>(); Dispensers = go.GetComponents<ConduitDispenser>();
            }
        }
        private static readonly Dictionary<int, Entry> entries = new Dictionary<int, Entry>();
        private static readonly List<int> dead = new List<int>();
        private static readonly Dictionary<int, GameObject> retired = new Dictionary<int, GameObject>();
        private static readonly ColonyActivityCells census = new ColonyActivityCells();
        private static readonly ColonyActivityFootprint footprint = new ColonyActivityFootprint();
        private static readonly ConduitType[] conduitTypes = { ConduitType.Gas, ConduitType.Liquid, ConduitType.Solid };
        private static readonly bool control = Environment.GetEnvironmentVariable("MA_COLONY_ACTIVITY_CONTROL") == "1";
        private static readonly SortedDictionary<ulong, ColonyActivitySnapshot> pendingChecks = new SortedDictionary<ulong, ColonyActivitySnapshot>();
        private static readonly AccessTools.FieldRef<Building, int[]> buildingCells = AccessTools.FieldRefAccess<Building, int[]>("placementCells");
        private static readonly AccessTools.FieldRef<OccupyArea, int[]> occupiedCells = AccessTools.FieldRefAccess<OccupyArea, int[]>("occupiedGridCells");
        private static readonly AccessTools.FieldRef<ConduitConsumer, int> consumerCell = AccessTools.FieldRefAccess<ConduitConsumer, int>("utilityCell");
        private static readonly AccessTools.FieldRef<ConduitDispenser, int> dispenserCell = AccessTools.FieldRefAccess<ConduitDispenser, int>("utilityCell");
        private static bool seeded;
        private static ulong session;
        private static ColonyActivitySnapshot? latestCensus;
        private static ulong stagedSequence;
        private static double stagedSeconds;
        internal static long ReplayMigratedActors { get; private set; }
        internal static long StagedFrames { get; private set; }
        internal static long StagedDeliveries { get; private set; }
        internal static bool Active { get; private set; }
        internal static long QueuedFrames { get; private set; }
        internal static long ClaimedFrames { get; private set; }
        internal static long LifecycleRefreshes { get; private set; }
        internal static int ActorCount { get; private set; }
        internal static int LivingActors { get; private set; }
        internal static int Buildings { get; private set; }
        internal static int Plants { get; private set; }
        internal static int InactiveActors { get; private set; }
        internal static int ActorCells { get; private set; }
        internal static int TouchedCells { get; private set; }
        internal static ulong LastSubmittedSequence { get; private set; }
        internal static long MaskChecks { get; private set; }
        internal static long MaskCellsChecked { get; private set; }
        internal static long MaskBusy { get; private set; }
        internal static long DroppedChecks { get; private set; }

        internal static void Reset()
        {
            entries.Clear(); dead.Clear(); retired.Clear(); census.Reset(); seeded = false; session = 0; Active = false; latestCensus = null; stagedSequence = 0; stagedSeconds = 0; ReplayMigratedActors = StagedFrames = StagedDeliveries = 0;
            QueuedFrames = ClaimedFrames = LifecycleRefreshes = 0; ActorCount = ActorCells = TouchedCells = LivingActors = Buildings = Plants = InactiveActors = 0; LastSubmittedSequence = 0;
            pendingChecks.Clear(); MaskChecks = MaskCellsChecked = MaskBusy = DroppedChecks = 0;
        }
        internal static void Started(uint flags, ulong ownerSession)
        {
            Active = (flags & OniNativeColony.GameActivity) != 0; session = ownerSession;
            if (Active && OniNativeColony.ActivityVersion() != 1)
                throw new NotSupportedException("This native bundle does not support the region activity frame contract.");
        }
        private static bool Real(Component component)
        {
            if (component == null) return false;
            var prefab = component.GetComponent<KPrefabID>();
            return prefab != null && prefab.InstanceID >= 0 && ReferenceEquals(KPrefabIDTracker.Get().GetInstance(prefab.InstanceID), prefab);
        }
        internal static void Register(Component? component, bool actor = false)
        {
            if (component == null) return;
            var go = component.gameObject; var identity = go.GetInstanceID();
            if (entries.ContainsKey(identity) || retired.ContainsKey(identity) || !Real(component)) return;
            if (!actor && go.GetComponent<Building>() == null && go.GetComponent<Brain>() == null &&
                go.GetComponent<MinionIdentity>() == null && go.GetComponent<Uprootable>() == null &&
                go.GetComponent<Growing>() == null && !go.HasTag(GameTags.Plant)) return;
            entries.Add(identity, new Entry(component));
        }
        internal static void Spawned(Component component)
        { retired.Remove(component.gameObject.GetInstanceID()); Register(component); }
        internal static void Retire(Component component)
        {
            if (component == null) return;
            var id = component.gameObject.GetInstanceID();
            if (!entries.Remove(id)) return;
            retired[id] = component.gameObject; census.Retire(id);
        }
        private static void SeedAndUnion()
        {
            dead.Clear(); foreach (var pair in retired) if (pair.Value == null) dead.Add(pair.Key);
            foreach (var id in dead) retired.Remove(id);
            if (!seeded)
            {
                foreach (var element in UnityEngine.Object.FindObjectsByType<PrimaryElement>(FindObjectsInactive.Include, FindObjectsSortMode.None)) Register(element);
                seeded = true;
            }
            // These authoritative registries also cover actors whose primary
            // element is absent or whose inactive prefab spawned during load.
            foreach (var actor in Components.LiveMinionIdentities.Items) Register(actor, true);
            foreach (var actor in Components.Brains.Items) Register(actor, true);
            foreach (var building in Components.BuildingCompletes.Items) Register(building, true);
            foreach (var plant in Components.Uprootables.Items) Register(plant, true);
            ColonyActors.RegisterActivityComponents();
        }
        internal static void BeginFootprint(ColonyActivityFootprint target)
        {
            var config = ColonyOwner.Config;
            target.Begin(Grid.WidthInCells, Grid.HeightInCells);
            target.Clip((int)config.OffsetX, (int)config.OffsetY, (int)config.Width, (int)config.Height);
        }
        private static bool InOwnerRectangle(int cell)
        {
            if (!Grid.IsValidCell(cell)) return false;
            var config = ColonyOwner.Config; var x = cell % Grid.WidthInCells; var y = cell / Grid.WidthInCells;
            return x>=config.OffsetX && y>=config.OffsetY && x<config.OffsetX+config.Width && y<config.OffsetY+config.Height;
        }
        private static void Offset(int anchor, CellOffset offset, int orientation = 0) => footprint.Offset(anchor, offset.x, offset.y, orientation);
        private static ColonyActivitySnapshot Capture()
        {
            SeedAndUnion(); census.Begin(Grid.WidthInCells, Grid.HeightInCells); dead.Clear();
            LivingActors = Buildings = Plants = InactiveActors = 0;
            foreach (var pair in entries)
            {
                var e = pair.Value; if (e.Component == null) { dead.Add(pair.Key); continue; }
                if (e.Living) ++LivingActors; if (e.Building != null) ++Buildings; if (e.Plant) ++Plants;
                if (!e.Component.gameObject.activeInHierarchy) ++InactiveActors;
                var anchor = Grid.PosToCell(e.Component.transform.position);
                BeginFootprint(footprint); footprint.Add(anchor);
                if (e.Living)
                {
                    footprint.Radius(anchor, 1);
                    foreach (var offset in GasBreatherFromWorldProvider.DEFAULT_BREATHABLE_OFFSETS) Offset(anchor, offset);
                }
                if (e.Occupancy != null)
                {
                    var offsets = e.Occupancy.OccupiedCellsOffsets;
                    if (offsets != null) foreach (var offset in offsets) Offset(anchor, offset);
                    var occupied = occupiedCells(e.Occupancy); if (occupied != null) foreach (var cell in occupied) footprint.Add(cell);
                }
                foreach (var consumer in e.Consumers) if (consumer.isSpawned && consumer.conduitType != ConduitType.None) footprint.Add(consumerCell(consumer));
                foreach (var dispenser in e.Dispensers) if (dispenser.isSpawned && dispenser.conduitType != ConduitType.None) footprint.Add(dispenserCell(dispenser));
                var building = e.Building;
                if (building != null && building.Def != null)
                {
                    var placed = buildingCells(building); if (placed != null) foreach (var cell in placed) footprint.Add(cell);
                    var definition = building.Def; var orientation = (int)building.Orientation;
                    foreach (var offset in definition.PlacementOffsets) Offset(anchor, offset, orientation);
                    if (definition.InputConduitType != ConduitType.None) Offset(anchor, definition.UtilityInputOffset, orientation);
                    if (definition.OutputConduitType != ConduitType.None) Offset(anchor, definition.UtilityOutputOffset, orientation);
                    if (definition.RequiresPowerInput) Offset(anchor, definition.PowerInputOffset, orientation);
                    if (definition.RequiresPowerOutput) Offset(anchor, definition.PowerOutputOffset, orientation);
                    foreach (var input in e.Inputs)
                        foreach (var type in conduitTypes)
                            if (input.HasSecondaryConduitType(type)) Offset(anchor, input.GetSecondaryConduitOffset(type), orientation);
                    foreach (var output in e.Outputs)
                        foreach (var type in conduitTypes)
                            if (output.HasSecondaryConduitType(type)) Offset(anchor, output.GetSecondaryConduitOffset(type), orientation);
                }
                e.Previous = footprint.Finish(e.Previous);
                if (control) ValidateGameFootprint(e);
                census.Observe(pair.Key, e.Previous);
            }
            foreach (var id in dead) entries.Remove(id);
            ColonyActors.CaptureActivity(census, footprint);
            var snapshot = census.Finish(); ActorCount = census.ActorCount; ActorCells = snapshot.Actors.Length; TouchedCells = snapshot.Touched.Length;
            return snapshot;
        }
        private static void ValidateGameFootprint(Entry entry)
        {
            if (!(entry.Component is KMonoBehaviour behaviour) || !behaviour.isSpawned) return;
            // The prefab lifetime precedes OccupyArea's native registration
            // during game startup/load. Its cache can also be absent when
            // the area does not apply itself to Grid.Objects.
            if (entry.Occupancy != null && entry.Occupancy.isSpawned && entry.Occupancy.GetOccupiedGridCells() is int[] occupied)
                foreach (var cell in occupied) Require(cell, "occupied area");
            if (entry.Building != null && buildingCells(entry.Building) is int[] placed)
                foreach (var cell in placed) Require(cell, "building placement");
            foreach (var consumer in entry.Consumers) if (consumer.isSpawned && consumer.conduitType != ConduitType.None) Require(consumerCell(consumer), "input port");
            foreach (var dispenser in entry.Dispensers) if (dispenser.isSpawned && dispenser.conduitType != ConduitType.None) Require(dispenserCell(dispenser), "output port");
            void Require(int cell, string source)
            {
                if (InOwnerRectangle(cell) && Array.BinarySearch(entry.Previous, (uint)cell) < 0)
                    throw new InvalidOperationException("Actor census omitted a native " + source + " cell " + cell + " on " + entry.Component.name + ".");
            }
        }
        // Native owns sequence allocation. Restored snapshots already contain
        // actor transfers and pump jobs: claim them without generating either.
        internal static void QueueFrame(float seconds)
        {
            if (!Active) { ColonyActors.QueueFrame(seconds); return; }
            if (!(seconds > 0))
            {
                // A paused resave can migrate registrations before the first
                // positive claim. Count that same restore-delivery work too.
                var paused = OniNativeColony.ActivityState.Empty; OniNativeColony.Check(OniNativeColony.CopyActivity(ref paused));
                if (paused.PendingFrames != 0) ReplayMigratedActors += ColonyActors.TakeNativeForReplay();
                ColonyActors.QueueFrame(seconds); Refresh(); return;
            }
            var state = OniNativeColony.ActivityState.Empty; OniNativeColony.Check(OniNativeColony.CopyActivity(ref state));
            ColonyActivityDelivery.Validate(session, seconds, state.Session, state.PendingFrames, state.PendingHeadSequence, state.PendingHeadSeconds);
            if (stagedSequence != 0)
            {
                ColonyActivityDelivery.Validate(session, seconds, state.Session, 1, stagedSequence, stagedSeconds);
                if (state.PendingFrames != 0) throw new InvalidOperationException("A staged live frame cannot also be a restored frame.");
                _ = ColonyActors.TakeNativeForReplay(); stagedSequence = 0; ++StagedDeliveries; return;
            }
            if (state.PendingFrames != 0)
            {
                ReplayMigratedActors += ColonyActors.TakeNativeForReplay();
                OniNativeColony.Check(OniNativeColony.ClaimActivityFrame(session, state.PendingHeadSequence, seconds));
                LastSubmittedSequence = state.PendingHeadSequence; ++ClaimedFrames; return;
            }
            ColonyActors.QueueFrame(seconds);
            var snapshot = Capture();
            OniNativeColony.Check(OniNativeColony.QueueActivity(session, seconds, snapshot, out var sequence));
            latestCensus = snapshot;
            if (control)
            {
                if (pendingChecks.Count >= 128)
                { using var oldest = pendingChecks.GetEnumerator(); oldest.MoveNext(); pendingChecks.Remove(oldest.Current.Key); ++DroppedChecks; }
                pendingChecks.Add(sequence, snapshot);
            }
            LastSubmittedSequence = sequence; ++QueuedFrames;
        }
        // Private lifecycle scenario only: prepare exactly the next native
        // frame before a normal save, then deliver that same frame once the
        // uninterrupted game resumes. A load instead claims its saved packet.
        internal static ulong StageNextFrameForSave(float seconds)
        {
            var settings = ColonyRuntimeValidation.Settings;
            if (!Active || settings == null || Array.IndexOf(settings.Scenario.Split(','), "activity-lifecycle") < 0 || stagedSequence != 0)
                throw new InvalidOperationException("Pending activity staging is only available to its private lifecycle scenario.");
            var state = OniNativeColony.ActivityState.Empty; OniNativeColony.Check(OniNativeColony.CopyActivity(ref state));
            if (state.PendingFrames != 0 || state.NextSubmissionSequence != state.LastCommittedSequence + 1)
                throw new InvalidOperationException("Finish earlier activity delivery before staging another frame.");
            QueueFrame(seconds); stagedSequence = LastSubmittedSequence; stagedSeconds = seconds; ++StagedFrames; return stagedSequence;
        }
        internal static void Refresh()
        {
            if (!Active) return;
            var snapshot = Capture();
            // Repeated zero-time publication rounds must settle: publishing
            // an unchanged census cannot create another lifecycle input.
            if (!ColonyActivityDelivery.NeedsRefresh(snapshot, latestCensus)) return;
            pendingChecks.Clear(); // A zero-time census can replace the mask at the same completed frame.
            OniNativeColony.Check(OniNativeColony.QueueActivity(session, 0, snapshot, out _)); latestCensus = snapshot; ++LifecycleRefreshes;
        }
        internal static void ValidatePublication(OniNativeColony.Status published)
        {
            if (!Active || !control || pendingChecks.Count == 0) return;
            var state = OniNativeColony.ActivityState.Empty; OniNativeColony.Check(OniNativeColony.CopyActivity(ref state));
            if (state.Session != published.Session || !pendingChecks.TryGetValue(state.LastCommittedSequence, out var snapshot)) return;
            var expectedActors = ColonyActivityMask.Project(snapshot.Actors, Grid.WidthInCells, Grid.HeightInCells, ColonyOwner.PublishedActivityFluid);
            var all = ColonyActivityMask.Project(snapshot.Touched, Grid.WidthInCells, Grid.HeightInCells, ColonyOwner.PublishedActivityFluid);
            all.UnionWith(expectedActors); foreach (var cell in snapshot.Actors) all.Add(cell); foreach (var cell in snapshot.Touched) all.Add(cell);
            var cells = new uint[Math.Min(4096, all.Count)]; var rows = new OniNativeColony.ActivityCell[cells.Length]; var count = 0;
            foreach (var cell in all)
            {
                cells[count++] = cell;
                if (count == cells.Length) { if (!ValidateBatch()) return; count = 0; }
            }
            if (count != 0 && !ValidateBatch()) return;
            ++MaskChecks;
            var obsolete = new List<ulong>(); foreach (var key in pendingChecks.Keys) if (key <= state.LastCommittedSequence) obsolete.Add(key);
            foreach (var key in obsolete) pendingChecks.Remove(key);
            bool ValidateBatch()
            {
                var output = OniNativeColony.Status.Empty;
                var result = OniNativeColony.SampleActivity(published.Session, published.CompletedFrames, cells, (uint)count, ref output, rows);
                if (result == 3) { ++MaskBusy; return false; }
                OniNativeColony.Check(result);
                for (var i = 0; i < count; ++i)
                {
                    var expected = expectedActors.Contains(cells[i]);
                    if (rows[i].PublicCell != cells[i] || (rows[i].Actors != 0) != expected)
                        throw new InvalidOperationException("The published region activity mask differs from its submitted actor census at cell " + cells[i] + ".");
                    ++MaskCellsChecked;
                }
                return true;
            }
        }
        internal static object Report() => new { Active, QueuedFrames, ClaimedFrames, ReplayMigratedActors, StagedFrames, StagedDeliveries, StagedSequence = stagedSequence, LifecycleRefreshes, ActorCount, LivingActors, Buildings, Plants, InactiveActors, ActorCells, TouchedCells, LastSubmittedSequence, MaskChecks, MaskCellsChecked, MaskBusy, DroppedChecks };
    }

    [HarmonyPatch(typeof(PrimaryElement), "OnSpawn")]
    internal static class ColonyActivitySpawnPatch
    { private static void Postfix(PrimaryElement __instance) => ColonyActivity.Spawned(__instance); }
    [HarmonyPatch(typeof(PrimaryElement), "OnCleanUp")]
    internal static class ColonyActivityCleanupPatch
    { private static void Prefix(PrimaryElement __instance) => ColonyActivity.Retire(__instance); }
    // The prefab lifetime also covers registered actors without a primary
    // element. Duplicate primary/prefab notifications are idempotent.
    [HarmonyPatch(typeof(KPrefabID), "OnSpawn")]
    internal static class ColonyActivityPrefabSpawnPatch
    { private static void Postfix(KPrefabID __instance) => ColonyActivity.Spawned(__instance); }
    [HarmonyPatch(typeof(KPrefabID), "OnCleanUp")]
    internal static class ColonyActivityPrefabCleanupPatch
    { private static void Prefix(KPrefabID __instance) => ColonyActivity.Retire(__instance); }
}
