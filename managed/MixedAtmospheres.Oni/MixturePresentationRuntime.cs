using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // A read-only presentation binding. Only explicitly supplied authoritative
    // snapshots/inventories or disposable visual fixtures enter this adapter.
    // Grid's dominant element is never interpreted as a mixture inventory.
    internal static class MixturePresentationRuntime
    {
        private static readonly object Sync = new object();
        private static readonly Dictionary<string, InventoryRegistration> Registrations = new Dictionary<string, InventoryRegistration>(StringComparer.Ordinal);
        private static MixturePresentationModel? model;
        private static ColonyFrameSnapshot? source;
        private static MixturePresentationFrame? world;
        private static FixtureSource? fixture;
        private static RuntimeMixturePresentationFrame? current;
        private static RuntimeMixturePresentationFrame? workerReadFrame;
        private static long generation, builds, resetEpoch;
        private static int? presentationThread;
        private static bool capturing;
        private static bool sourceIsFixture;
        private static bool includeWorldCells = true;
        private static Func<MixturePresentationPublication?>? completedSource;
        private static MixturePresentationPublication? completedPublication;
        private static MixtureVisualOptions? visualOverride;
        internal static event System.Action? SessionEnding;
        internal static string LastShutdownError { get; private set; } = "";

        internal static MixturePresentationModel? CurrentModel { get { lock (Sync) return model; } }
        internal static bool HasProvider { get { lock (Sync) return world != null || fixture != null || Registrations.Count != 0; } }
        internal static bool HasWorldProvider { get { lock (Sync) return (world != null && includeWorldCells) || fixture != null; } }
        internal static int InventoryProviderCount { get { lock (Sync) return Registrations.Count; } }
        internal static bool HasCachedFrame { get { lock (Sync) return current != null || workerReadFrame != null; } }
        internal static bool IsActive => HasProvider;
        internal static long PresentationBuildCount { get { lock (Sync) return builds; } }
        internal static long CurrentRevision { get { lock (Sync) return current?.Revision ?? world?.Revision ?? fixture?.Revision ?? 0; } }
        internal static bool IsFixture { get { lock (Sync) return fixture != null || sourceIsFixture; } }

        // The model owns cloned settings/palette/options. Replacing it prepares
        // new world readings atomically; an already pinned Unity frame retains
        // its old model until the next presentation frame.
        internal static void Configure(MixturePresentationModel replacement)
        {
            if (replacement == null) throw new ArgumentNullException(nameof(replacement));
            lock (Sync)
            {
                if (completedSource != null)
                {
                    if (model == null || !replacement.IsVisualVariantOf(model))
                        throw new InvalidOperationException("Material and breathing definitions must change through the completed publication source.");
                    var options = replacement.VisualOptions;
                    var variant = completedPublication!.Model.WithVisualOptions(options);
                    var prepared = variant.CaptureFrame(completedPublication.Source);
                    visualOverride = options; model = variant; world = prepared; ++generation;
                    return;
                }
                var nextWorld = source == null ? null : replacement.CaptureFrame(source);
                var nextFixture = fixture?.WithModel(replacement);
                model = replacement; world = nextWorld; fixture = nextFixture;
                if (!presentationThread.HasValue) presentationThread = Thread.CurrentThread.ManagedThreadId;
                ++generation;
            }
        }

        // The callback returns a prepared immutable companion from one atomic
        // authority publication. It runs only on the presentation thread, once
        // per Unity frame; workers only see the pinned completed result.
        internal static void BindCompletedSource(Func<MixturePresentationPublication?> capture, bool isFixture = false, bool includeWorldCells = true)
        {
            if (capture == null) throw new ArgumentNullException(nameof(capture));
            lock (Sync)
            {
                if (capturing || source != null || fixture != null || current != null || workerReadFrame != null || Registrations.Count != 0 || completedSource != null)
                    throw new InvalidOperationException("Reset existing providers before binding a completed publication source.");
                if (presentationThread.HasValue && presentationThread != Thread.CurrentThread.ManagedThreadId)
                    throw new InvalidOperationException("Only the presentation thread may bind completed publications.");
                var epoch = resetEpoch; var observedGeneration = generation;
                capturing = true;
                try
                {
                    var initial = capture() ?? throw new InvalidOperationException("A completed prepared presentation is required before binding.");
                    if (epoch != resetEpoch || observedGeneration != generation)
                        throw new InvalidOperationException("Presentation state changed during publication-source binding.");
                    completedSource = capture; completedPublication = initial; visualOverride = null;
                    source = initial.Source; model = initial.Model; world = initial.Frame; sourceIsFixture = isFixture;
                    // A containers-only authority carries geometry for native
                    // owner identity, without owning those native world cells.
                    MixturePresentationRuntime.includeWorldCells = includeWorldCells;
                    presentationThread = Thread.CurrentThread.ManagedThreadId; ++generation;
                }
                finally { capturing = false; }
            }
        }

        private static void RefreshCompletedSource()
        {
            var capture = completedSource; if (capture == null) return;
            var epoch = resetEpoch; var observedGeneration = generation;
            var next = capture() ?? throw new InvalidOperationException("The bound source has no completed prepared presentation.");
            if (epoch != resetEpoch || observedGeneration != generation || !ReferenceEquals(capture, completedSource))
                throw new InvalidOperationException("Presentation state changed while reading its completed publication source.");
            if (ReferenceEquals(next, completedPublication)) return;
            if (source != null && (next.Source.ColonyId != source.ColonyId || next.Source.Revision < source.Revision ||
                next.Source.CompletedFrame < source.CompletedFrame))
                throw new InvalidOperationException("A bound source cannot replace its colony or regress its completed frame.");
            var nextModel = visualOverride == null ? next.Model : next.Model.WithVisualOptions(visualOverride);
            var nextWorld = visualOverride == null ? next.Frame : nextModel.CaptureFrame(next.Source);
            // All preparation succeeds before replacing this UI view. Nothing
            // here commits or rolls back the authority/actor publication.
            completedPublication = next; source = next.Source; model = nextModel; world = nextWorld; ++generation;
        }

        internal static void Publish(ColonyFrameSnapshot authoritative)
            => Publish(authoritative, false);

        // Uses the production snapshot projection while retaining the private
        // on-screen fixture banner. This owns no native simulation state.
        internal static void PublishConservedFixture(ColonyFrameSnapshot authoritative)
            => Publish(authoritative, true);

        private static void Publish(ColonyFrameSnapshot authoritative, bool isFixture)
        {
            if (authoritative == null) throw new ArgumentNullException(nameof(authoritative));
            lock (Sync)
            {
                if (model == null) throw new InvalidOperationException("Configure the mixture presentation model before publishing state.");
                if (completedSource != null) throw new InvalidOperationException("The completed publication source owns presentation state.");
                if (fixture != null) throw new InvalidOperationException("Clear visual fixtures before publishing authoritative colony state.");
                if (source != null && sourceIsFixture != isFixture)
                    throw new InvalidOperationException("Reset the presentation session before changing fixture ownership.");
                if (ReferenceEquals(authoritative, source)) return;
                if (source != null && authoritative.ColonyId != source.ColonyId)
                    throw new InvalidOperationException("Reset the presentation session before publishing a different colony.");
                if (source != null && (authoritative.CompletedFrame < source.CompletedFrame || authoritative.Revision < source.Revision))
                    throw new InvalidOperationException("Presentation cannot publish an older colony frame.");
                foreach (var id in authoritative.Containers.Keys)
                    if (Registrations.ContainsKey(id)) throw new InvalidOperationException("An independent inventory adapter conflicts with owned container " + id + ".");
                var prepared = model.CaptureFrame(authoritative);
                source = authoritative; world = prepared; sourceIsFixture = isFixture; includeWorldCells = true; ++generation;
            }
        }

        // Fixtures are explicit test-owned inventories at global cell indices.
        // They alter appearance/readouts only and never native/custom physics.
        internal static void EnableFixtureFrame(int width, int height, IReadOnlyDictionary<int, FluidInventory> cells,
            double volumeCubicMetres = 1)
        {
            if (cells == null) throw new ArgumentNullException(nameof(cells));
            if (width <= 0 || height <= 0 || (long)width * height > 16_777_216 ||
                double.IsNaN(volumeCubicMetres) || double.IsInfinity(volumeCubicMetres) || volumeCubicMetres <= 0)
                throw new ArgumentOutOfRangeException(nameof(width));
            lock (Sync)
            {
                if (model == null) throw new InvalidOperationException("Configure the mixture presentation model before fixtures.");
                if (source != null) throw new InvalidOperationException("Fixtures cannot replace an authoritative colony provider.");
                var copy = new Dictionary<int, FluidInventory>();
                foreach (var cell in cells)
                {
                    if (cell.Key < 0 || cell.Key >= (long)width * height || cell.Value == null)
                        throw new ArgumentException("Fixture cell lies outside its explicitly supplied geometry.");
                    copy.Add(cell.Key, cell.Value);
                }
                fixture = new FixtureSource(width, height, copy, volumeCubicMetres, checked(generation + 1), model);
                includeWorldCells = true;
                ++generation;
            }
        }

        internal static void ClearFixtures()
        {
            lock (Sync)
            {
                var wasFixture = fixture != null || sourceIsFixture;
                fixture = null;
                if (sourceIsFixture)
                {
                    source = null; world = null; sourceIsFixture = false;
                    completedSource = null; completedPublication = null; visualOverride = null;
                    includeWorldCells = true;
                }
                if (wasFixture) { current = null; Volatile.Write(ref workerReadFrame, null); ++resetEpoch; }
                ++generation;
            }
        }

        internal static void RegisterInventory(string id, Func<MixtureContainerSample> snapshot, Func<bool>? isAlive = null)
        {
            if (string.IsNullOrWhiteSpace(id) || id.Length > 256) throw new ArgumentException("A stable inventory identity is required.", nameof(id));
            if (snapshot == null) throw new ArgumentNullException(nameof(snapshot));
            lock (Sync)
            {
                if (completedSource != null) throw new InvalidOperationException("Independent inventory adapters cannot join a completed publication source.");
                if (world != null && world.OwnedContainerHandles.ContainsKey(id))
                    throw new InvalidOperationException("An independent adapter cannot replace owned container " + id + ".");
                Registrations[id] = new InventoryRegistration(id, snapshot, isAlive); ++generation;
            }
        }

        internal static void RegisterInventory(string id, Func<FluidInventory> inventory, double volumeCubicMetres, Func<bool>? isAlive = null)
        {
            if (inventory == null) throw new ArgumentNullException(nameof(inventory));
            if (double.IsNaN(volumeCubicMetres) || double.IsInfinity(volumeCubicMetres) || volumeCubicMetres <= 0)
                throw new ArgumentOutOfRangeException(nameof(volumeCubicMetres));
            RegisterInventory(id, () => new MixtureContainerSample(id, inventory(), volumeCubicMetres, source?.Revision ?? 0), isAlive);
        }

        internal static void UnregisterInventory(string id) { lock (Sync) { if (Registrations.Remove(id)) ++generation; } }

        internal static RuntimeMixturePresentationFrame? AcquireFrame()
        {
            // GlobalJobManager completes a texture/overlay batch before the
            // main-thread scope exits. Its workers inherit this immutable
            // publication without touching Unity Time or invoking adapters.
            var pinned = Volatile.Read(ref workerReadFrame);
            if (pinned != null) return pinned;
            // No Unity call at all when the mod has no explicit provider.
            if (!HasProvider) return null;
            lock (Sync)
                if (presentationThread.HasValue && presentationThread != Thread.CurrentThread.ManagedThreadId)
                    throw new InvalidOperationException("A presentation worker requires a main-thread batch lease.");
            return AcquireFrameFor(Time.frameCount);
        }

        internal static IDisposable EnterWorkerBatch(RuntimeMixturePresentationFrame frame)
        {
            if (frame == null) throw new ArgumentNullException(nameof(frame));
            lock (Sync)
            {
                if (!presentationThread.HasValue || presentationThread != Thread.CurrentThread.ManagedThreadId)
                    throw new InvalidOperationException("Only the presentation thread may pin worker batches.");
                if (frame.SessionEpoch != resetEpoch)
                    throw new InvalidOperationException("A worker batch cannot reactivate a frame from a retired presentation session.");
                var previous = Volatile.Read(ref workerReadFrame);
                Volatile.Write(ref workerReadFrame, frame);
                return new WorkerReadScope(previous, frame, resetEpoch);
            }
        }

        // Explicit Unity-frame index also permits engine-independent lifetime
        // tests. Inventory callbacks are captured ONCE on this presentation
        // thread, never from texture/overlay worker threads.
        internal static RuntimeMixturePresentationFrame? AcquireFrameFor(int unityFrame)
        {
            lock (Sync)
            {
                if (current != null && current.UnityFrame == unityFrame) return current;
                if (model == null || (world == null && fixture == null && Registrations.Count == 0)) return null;
                var thread = Thread.CurrentThread.ManagedThreadId;
                if (presentationThread.HasValue && presentationThread != thread)
                    throw new InvalidOperationException("Presentation frames and inventory adapters must be captured on one main thread.");
                if (capturing) throw new InvalidOperationException("An inventory adapter cannot recursively capture a new presentation frame.");
                presentationThread = thread;
                capturing = true;
                try
                {
                RefreshCompletedSource();
                // Adapters are external code and may reenter Publish/Configure.
                // Every projection in this Unity frame retains these exact
                // completed inputs, even if a newer publication arrives.
                var capturedModel = model!; var capturedWorld = world; var capturedFixture = fixture; var capturedSource = source;
                var capturedGeneration = generation; var capturedEpoch = resetEpoch; var capturedIsFixture = sourceIsFixture;
                var capturedWorldCells = includeWorldCells;
                var inventories = new Dictionary<string, MixturePresentation>(StringComparer.Ordinal);
                var errors = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (var registration in Registrations.Values.ToArray())
                {
                    if (capturedEpoch != resetEpoch) throw new InvalidOperationException("The presentation session ended during an inventory capture.");
                    try
                    {
                        var alive = registration.IsAlive == null || registration.IsAlive();
                        if (capturedEpoch != resetEpoch) throw new InvalidOperationException("The presentation session ended during an inventory capture.");
                        if (!alive)
                        {
                            if (Registrations.TryGetValue(registration.Id, out var registered) && ReferenceEquals(registered, registration))
                                Registrations.Remove(registration.Id);
                            continue;
                        }
                        var sample = registration.Capture();
                        if (capturedEpoch != resetEpoch) throw new InvalidOperationException("The presentation session ended during an inventory capture.");
                        if (sample == null || sample.Id != registration.Id)
                            throw new InvalidOperationException("Inventory adapter returned a different or missing identity.");
                        if (capturedSource != null && sample.Revision != capturedSource.Revision)
                            throw new InvalidOperationException("Inventory adapter belongs to a different authoritative colony revision.");
                        inventories.Add(registration.Id, capturedModel.Describe(sample.Inventory, sample.VolumeCubicMetres, sample.Revision, sample.Id));
                    }
                    catch (Exception error)
                    {
                        // Explicit registered ownership never silently becomes
                        // a vanilla pure packet when its adapter fails.
                        errors[registration.Id] = error.Message;
                    }
                }
                if (capturedEpoch != resetEpoch) throw new InvalidOperationException("The presentation session ended during an inventory capture.");
                current = new RuntimeMixturePresentationFrame(unityFrame, capturedGeneration, capturedEpoch, capturedModel, capturedWorld,
                    capturedFixture?.Presentations, capturedFixture?.Width ?? capturedWorld?.Width ?? 0, capturedFixture?.Height ?? capturedWorld?.Height ?? 0,
                    capturedFixture?.Revision ?? capturedWorld?.Revision ?? 0, capturedFixture != null || capturedIsFixture, inventories, errors, capturedWorldCells);
                ++builds; return current;
                }
                finally { capturing = false; }
            }
        }

        internal static bool TryGetCell(int globalCell, out MixturePresentation? reading)
        {
            var frame = AcquireFrame();
            if (frame != null) return frame.TryGetCell(globalCell, out reading);
            reading = null; return false;
        }

        internal static bool TryResolveInventory(string id, out MixturePresentation? reading)
        {
            var frame = AcquireFrame();
            if (frame != null) return frame.TryResolveInventory(id, out reading);
            reading = null; return false;
        }

        internal static bool TryResolveInventory(FluidContainerHandle handle, out MixturePresentation? reading)
        {
            var frame = AcquireFrame();
            if (frame != null) return frame.TryResolveInventory(handle, out reading);
            reading = null; return false;
        }

        internal static bool TryResolveIndependentInventory(string id, out MixturePresentation? reading)
        {
            var frame = AcquireFrame();
            if (frame != null) return frame.Inventories.TryGetValue(id, out reading);
            reading = null; return false;
        }

        internal static bool TryGetInventoryError(string id, out string? error)
        {
            var frame = AcquireFrame();
            if (frame != null && frame.InventoryErrors.TryGetValue(id, out var message)) { error = message; return true; }
            error = null; return false;
        }

        internal static void ResetProviders()
        {
            lock (Sync)
            {
                source = null; world = null; fixture = null; current = null; Registrations.Clear();
                sourceIsFixture = false;
                completedSource = null; completedPublication = null; visualOverride = null;
                includeWorldCells = true;
                Volatile.Write(ref workerReadFrame, null);
                presentationThread = null; ++generation; ++resetEpoch;
            }
        }

        internal static void Shutdown()
        {
            ResetProviders();
            lock (Sync) model = null;
            // Subscribers are static app-lifetime services. Each owns its
            // resources; one cleanup failure cannot prevent the rest or stop
            // the original native shutdown from running.
            var errors = new List<string>();
            var callbacks = SessionEnding;
            if (callbacks != null)
                foreach (System.Action callback in callbacks.GetInvocationList())
                    try { callback(); } catch (Exception error) { errors.Add(error.ToString()); }
            LastShutdownError = string.Join("\n", errors);
        }

        private sealed class WorkerReadScope : IDisposable
        {
            private readonly RuntimeMixturePresentationFrame? previous;
            private readonly long epoch;
            private RuntimeMixturePresentationFrame? owned;
            internal WorkerReadScope(RuntimeMixturePresentationFrame? previous, RuntimeMixturePresentationFrame owned, long epoch)
            { this.previous = previous; this.owned = owned; this.epoch = epoch; }
            public void Dispose()
            {
                lock (Sync)
                {
                    if (owned == null) return;
                    // A teardown invalidates every old lease. In particular,
                    // disposing one after a new session must never repin old
                    // inventories, even when Unity frame indices coincide.
                    if (epoch != resetEpoch) { owned = null; return; }
                    if (presentationThread != Thread.CurrentThread.ManagedThreadId)
                        throw new InvalidOperationException("The presentation thread must retire its worker lease.");
                    if (!ReferenceEquals(Volatile.Read(ref workerReadFrame), owned))
                        throw new InvalidOperationException("Presentation worker leases must retire in nested order.");
                    Volatile.Write(ref workerReadFrame, previous); owned = null;
                }
            }
        }

        private sealed class InventoryRegistration
        {
            internal readonly string Id;
            internal readonly Func<MixtureContainerSample> Capture;
            internal readonly Func<bool>? IsAlive;
            internal InventoryRegistration(string id, Func<MixtureContainerSample> capture, Func<bool>? isAlive)
            { Id = id; Capture = capture; IsAlive = isAlive; }
        }

        private sealed class FixtureSource
        {
            internal readonly int Width, Height;
            internal readonly long Revision;
            internal readonly double Volume;
            internal readonly IReadOnlyDictionary<int, FluidInventory> Inventories;
            internal readonly IReadOnlyDictionary<int, MixturePresentation> Presentations;
            internal FixtureSource(int width, int height, IReadOnlyDictionary<int, FluidInventory> inventories, double volume,
                long revision, MixturePresentationModel model)
            {
                Width = width; Height = height; Inventories = inventories; Volume = volume; Revision = revision;
                var readings = new Dictionary<int, MixturePresentation>();
                foreach (var cell in inventories)
                    readings.Add(cell.Key, model.Describe(cell.Value, volume, revision, "fixture:cell:" + cell.Key));
                Presentations = new ReadOnlyDictionary<int, MixturePresentation>(readings);
            }
            internal FixtureSource WithModel(MixturePresentationModel model) => new FixtureSource(Width, Height, Inventories, Volume, Revision, model);
        }
    }

    internal sealed class RuntimeMixturePresentationFrame
    {
        private readonly MixturePresentationFrame? world;
        private readonly bool includeWorldCells;
        private readonly IReadOnlyDictionary<int, MixturePresentation>? fixtures;
        internal int UnityFrame { get; }
        internal long PublicationGeneration { get; }
        internal long SessionEpoch { get; }
        internal long Revision { get; }
        internal bool IsFixture { get; }
        internal int Width { get; }
        internal int Height { get; }
        internal MixturePresentationModel Model { get; }
        internal IReadOnlyDictionary<string, MixturePresentation> Inventories { get; }
        internal IReadOnlyDictionary<string, string> InventoryErrors { get; }
        internal bool HasWorld => (world != null && includeWorldCells) || fixtures != null;

        internal RuntimeMixturePresentationFrame(int unityFrame, long generation, long sessionEpoch, MixturePresentationModel model,
            MixturePresentationFrame? world, IReadOnlyDictionary<int, MixturePresentation>? fixtures, int width, int height,
            long revision, bool fixture, Dictionary<string, MixturePresentation> inventories, Dictionary<string, string> errors, bool includeWorldCells = true)
        {
            UnityFrame = unityFrame; PublicationGeneration = generation; SessionEpoch = sessionEpoch; Model = model; this.world = world; this.fixtures = fixtures;
            Width = width; Height = height; Revision = revision; IsFixture = fixture;
            this.includeWorldCells = includeWorldCells;
            Inventories = new ReadOnlyDictionary<string, MixturePresentation>(inventories);
            InventoryErrors = new ReadOnlyDictionary<string, string>(errors);
        }

        internal bool TryGetCell(int globalCell, out MixturePresentation? reading)
        {
            if (fixtures != null) return fixtures.TryGetValue(globalCell, out reading);
            if (world != null && includeWorldCells) return world.TrySampleCell(globalCell, out reading);
            reading = null; return false;
        }

        internal bool TryResolveInventory(string id, out MixturePresentation? reading)
        {
            if (world != null && world.TrySampleContainer(id, out reading)) return true;
            return Inventories.TryGetValue(id, out reading);
        }

        internal bool TryResolveInventory(FluidContainerHandle handle, out MixturePresentation? reading)
        {
            if (world != null) return world.TrySampleContainer(handle, out reading);
            reading = null; return false;
        }
    }
}
