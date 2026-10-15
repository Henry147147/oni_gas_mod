using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using MixedAtmospheres.Core;

namespace MixedAtmospheres.Oni
{
    internal enum FluidCallbackKind { Generic, Consumed, Emitted }

    internal readonly struct FluidFrameTicket : IEquatable<FluidFrameTicket>
    {
        internal Guid Session { get; }
        internal long Sequence { get; }
        internal FluidFrameTicket(Guid session, long sequence) { Session = session; Sequence = sequence; }
        public bool Equals(FluidFrameTicket other) => Session == other.Session && Sequence == other.Sequence;
        public override bool Equals(object? other) => other is FluidFrameTicket ticket && Equals(ticket);
        public override int GetHashCode() => Session.GetHashCode() ^ Sequence.GetHashCode();
    }

    internal readonly struct FluidCallbackLease
    {
        internal Guid Session { get; }
        internal FluidCallbackKind Kind { get; }
        internal int Index { get; }
        internal long Generation { get; }
        internal FluidCallbackLease(Guid session, FluidCallbackKind kind, int index, long generation)
        { Session = session; Kind = kind; Index = index; Generation = generation; }
    }

    internal sealed class FluidBatchMessage
    {
        private readonly byte[] payload;
        internal int Message { get; }
        internal int? DisplacementCell { get; }
        internal ReadOnlySpan<byte> Payload => payload;
        internal FluidBatchMessage(int message, ReadOnlySpan<byte> payload, int? displacementCell = null)
        {
            if (payload.Length > 4096) throw new ArgumentException("Unbounded experimental message payload.");
            Message = message; this.payload = payload.ToArray(); DisplacementCell = displacementCell;
        }
    }

    internal sealed class PreparedFluidBatch : IDisposable
    {
        internal readonly ExperimentalFluidFrameCoordinator Owner;
        internal readonly PreparedFluidCommit Commit;
        internal readonly IReadOnlyList<FluidMutationCommit> Effects;
        internal readonly IReadOnlyList<FluidCallbackLease> Leases;
        internal readonly IReadOnlyList<FluidNativeOperation> NativeOperations;
        private readonly Sim.CallbackInfo[] callbacks;
        private readonly Sim.MassConsumedCallback[] consumed;
        private readonly Sim.MassEmittedCallback[] emitted;
        internal OwnedGameDataSnapshot? Candidate;
        internal bool DispatchStarted, NativeAcknowledged, Published, Closed;
        internal FluidFrameTicket Ticket { get; }
        internal ColonyFrameSnapshot Snapshot => Commit.Snapshot;
        internal PreparedFluidBatch(ExperimentalFluidFrameCoordinator owner, PreparedFluidCommit commit, FluidFrameTicket ticket,
            List<FluidMutationCommit> effects, List<FluidCallbackLease> leases, List<FluidNativeOperation> native)
        {
            Owner = owner; Commit = commit; Ticket = ticket;
            Effects = new ReadOnlyCollection<FluidMutationCommit>(effects); Leases = new ReadOnlyCollection<FluidCallbackLease>(leases);
            NativeOperations = new ReadOnlyCollection<FluidNativeOperation>(native);
            var genericCount = 0; var consumedCount = 0; var emittedCount = 0;
            foreach (var effect in effects)
            {
                genericCount = checked(genericCount + effect.Callbacks.Length);
                consumedCount = checked(consumedCount + effect.ConsumedCallbacks.Length);
                emittedCount = checked(emittedCount + effect.EmittedCallbacks.Length);
            }
            callbacks = genericCount == 0 ? Array.Empty<Sim.CallbackInfo>() : new Sim.CallbackInfo[genericCount];
            consumed = consumedCount == 0 ? Array.Empty<Sim.MassConsumedCallback>() : new Sim.MassConsumedCallback[consumedCount];
            emitted = emittedCount == 0 ? Array.Empty<Sim.MassEmittedCallback>() : new Sim.MassEmittedCallback[emittedCount];
            var genericOffset = 0; var consumedOffset = 0; var emittedOffset = 0;
            foreach (var effect in effects)
            {
                effect.Callbacks.CopyTo(callbacks.AsSpan(genericOffset)); genericOffset += effect.Callbacks.Length;
                effect.ConsumedCallbacks.CopyTo(consumed.AsSpan(consumedOffset)); consumedOffset += effect.ConsumedCallbacks.Length;
                effect.EmittedCallbacks.CopyTo(emitted.AsSpan(emittedOffset)); emittedOffset += effect.EmittedCallbacks.Length;
            }
        }
        // One append per channel avoids repeatedly copying a growing native
        // event buffer. The same immutable outbox is reused on snapshot retry.
        internal void AppendCallbacks(OwnedSnapshotOverlay overlay)
        {
            overlay.AppendEvents<Sim.CallbackInfo>(SnapshotEventChannel.CallbackInfo, callbacks);
            overlay.AppendEvents<Sim.MassConsumedCallback>(SnapshotEventChannel.MassConsumedCallbacks, consumed);
            overlay.AppendEvents<Sim.MassEmittedCallback>(SnapshotEventChannel.MassEmittedCallbacks, emitted);
        }
        public void Dispose() => Owner.Cancel(this);
    }

    // Native memory stays alive through consumption acknowledgement and until
    // the next publication, because Game.simData retains event pointers between
    // updates. Acknowledgement attests all old readers have finished; consumers
    // may not retain/use pointers across the next publication or Dispose.
    // This is an in-session record, never a save format.
    internal sealed class FluidFramePublication
    {
        internal FluidFrameTicket Ticket { get; }
        internal ColonyFrameSnapshot Snapshot { get; }
        internal OwnedGameDataSnapshot NativeSnapshot { get; }
        internal IReadOnlyList<FluidMutationCommit> Effects { get; }
        internal FluidFramePublication(PreparedFluidBatch batch, OwnedGameDataSnapshot native)
        {
            Ticket = batch.Ticket; Snapshot = batch.Snapshot; NativeSnapshot = native;
            var effects = new List<FluidMutationCommit>(batch.Effects.Count);
            foreach (var effect in batch.Effects) effects.Add(effect.ForPublication(Snapshot));
            Effects = new ReadOnlyCollection<FluidMutationCommit>(effects);
        }
    }

    /// <summary>
    /// Unhooked protocol experiment. It executes no native operation or callback.
    /// A caller supplies stable native observations and an explicitly pure snapshot
    /// projection, then attests consumption. It supplies no physics, container
    /// adapters, native rollback or persistence/crash-recovery protocol.
    /// Native source freshness and exclusive native dispatch are caller contracts:
    /// GameDataUpdate.numFramesProcessed is not a unique source epoch.
    /// </summary>
    internal sealed unsafe class ExperimentalFluidFrameCoordinator : IDisposable
    {
        private readonly object sync = new object();
        private readonly ColonyFluidState state;
        private readonly ExperimentalFluidMessageRouter router;
        private readonly Guid session = Guid.NewGuid();
        private readonly Dictionary<(FluidCallbackKind kind, int index), FluidCallbackLease> callbacks = new Dictionary<(FluidCallbackKind, int), FluidCallbackLease>();
        private long nextSequence, nextGeneration;
        private PreparedFluidBatch? pending;
        private FluidFramePublication? published;
        private bool awaitingConsumption, consuming, buildingSnapshot, disposed;
        private string consumptionFault = "", nativeFault = "";
        internal string ConsumptionFault { get { lock (sync) return consumptionFault; } }
        internal string NativeRecoveryRequired { get { lock (sync) return nativeFault; } }
        internal int RegistrationCount { get { lock (sync) return callbacks.Count; } }
        internal bool HasUnacknowledgedPublication { get { lock (sync) return awaitingConsumption; } }
        internal FluidFramePublication? Current { get { lock (sync) { RequireOpen(); ValidateAuthority(); return published; } } }

        internal ExperimentalFluidFrameCoordinator(ColonyFluidState state, ExperimentalFluidMessageRouter router)
        { this.state = state ?? throw new ArgumentNullException(nameof(state)); this.router = router ?? throw new ArgumentNullException(nameof(router)); }

        internal FluidCallbackLease RegisterCallback(FluidCallbackKind kind, int index)
        {
            lock (sync)
            {
                RequireUsable();
                if (!Enum.IsDefined(typeof(FluidCallbackKind), kind) || index < 0) throw new ArgumentOutOfRangeException(nameof(index));
                var key = (kind, index);
                if (callbacks.ContainsKey(key) || (awaitingConsumption && HasLease(pending!, kind, index)))
                    throw new InvalidOperationException("Callback index is registered or still belongs to an unacknowledged publication.");
                if (callbacks.Count == 65536) throw new InvalidOperationException("Experimental callback registration limit reached.");
                var lease = new FluidCallbackLease(session, kind, index, checked(++nextGeneration)); callbacks.Add(key, lease); return lease;
            }
        }

        internal void UnregisterCallback(FluidCallbackLease lease)
        {
            lock (sync)
            {
                RequireOpen(); ValidateLease(lease); callbacks.Remove((lease.Kind, lease.Index));
                if (pending != null && HasLease(pending, lease.Kind, lease.Index))
                {
                    if (awaitingConsumption) consumptionFault = "A callback registration was retired before publication consumption was acknowledged.";
                    else if (pending.DispatchStarted) nativeFault = "Callback registration changed after native dispatch began; native recovery is required.";
                }
            }
        }

        internal PreparedFluidBatch PrepareBatch(IEnumerable<FluidBatchMessage> messages, long completedFrame, double simulationTimeSeconds)
        {
            lock (sync)
            {
                RequireUsable();
                if (messages == null) throw new ArgumentNullException(nameof(messages));
                if (pending != null || awaitingConsumption) throw new InvalidOperationException("Finish or cancel the prior frame before preparing another.");
                var effects = new List<FluidMutationCommit>(); var leases = new List<FluidCallbackLease>(); var native = new List<FluidNativeOperation>();
                using var transaction = state.BeginTransaction();
                foreach (var message in messages)
                {
                    if (message == null || effects.Count == 4096) throw new ArgumentException("Null message or experimental batch limit exceeded.");
                    var effect = router.StageInto(transaction, message.Message, message.Payload, message.DisplacementCell);
                    effects.Add(effect); if (effect.NativeOperation != null) native.Add(effect.NativeOperation);
                    foreach (var callback in effect.Callbacks) leases.Add(FindLease(FluidCallbackKind.Generic, callback.callbackIdx));
                    foreach (var callback in effect.ConsumedCallbacks) leases.Add(FindLease(FluidCallbackKind.Consumed, callback.callbackIdx));
                    foreach (var callback in effect.EmittedCallbacks) leases.Add(FindLease(FluidCallbackKind.Emitted, callback.callbackIdx));
                }
                var once = new HashSet<(FluidCallbackKind, int)>();
                foreach (var lease in leases)
                    if (lease.Kind != FluidCallbackKind.Emitted && !once.Add((lease.Kind, lease.Index)))
                        throw new InvalidOperationException("A one-shot callback registration was used more than once in the batch.");
                var candidate = transaction.PrepareFrame(completedFrame, simulationTimeSeconds);
                try
                {
                    candidate.ReserveWriter();
                    pending = new PreparedFluidBatch(this, candidate, new FluidFrameTicket(session, checked(++nextSequence)), effects, leases, native);
                    return pending;
                }
                catch { candidate.Dispose(); throw; }
            }
        }

        // This is the ONLY way to expose native intents. The whole batch and
        // exclusive custom writer reservation already passed before this call.
        internal IReadOnlyList<FluidNativeOperation> BeginNativeDispatch(PreparedFluidBatch batch)
        {
            lock (sync)
            {
                RequirePending(batch); ValidateLeases(batch); batch.Commit.EnsureCurrent();
                if (batch.DispatchStarted || batch.NativeOperations.Count == 0) throw new InvalidOperationException("Native dispatch is empty or already started.");
                batch.DispatchStarted = true; return batch.NativeOperations;
            }
        }

        // Caller attests ALL exported operations completed. Verify the final
        // observed geometry, including ordered repeated changes to one cell.
        internal void AcknowledgeNative(PreparedFluidBatch batch, Sim.GameDataUpdate* observed)
        {
            lock (sync)
            {
                RequirePending(batch); ValidateLeases(batch);
                if (!batch.DispatchStarted || batch.NativeAcknowledged) throw new InvalidOperationException("No unacknowledged native dispatch.");
                ValidateNativeGeometry(batch, observed); batch.NativeAcknowledged = true;
            }
        }

        internal void ReportNativeFailure(PreparedFluidBatch batch, string reason)
        {
            lock (sync)
            {
                RequirePending(batch);
                if (!batch.DispatchStarted) throw new InvalidOperationException("No native operation was dispatched.");
                nativeFault = Reason(reason);
            }
        }

        internal void PrepareSnapshot(PreparedFluidBatch batch, Sim.GameDataUpdate* source,
            Action<OwnedSnapshotOverlay, ColonyFrameSnapshot> project, bool allowMissingTextures = false)
        {
            lock (sync)
            {
                RequirePending(batch); ValidateLeases(batch); batch.Commit.EnsureCurrent();
                if (project == null) throw new ArgumentNullException(nameof(project));
                if (buildingSnapshot) throw new InvalidOperationException("Snapshot projection cannot recursively construct another snapshot.");
                if (batch.Candidate != null) throw new InvalidOperationException("A merged candidate is already prepared.");
                if (batch.NativeOperations.Count != 0 && !batch.NativeAcknowledged) throw new InvalidOperationException("Native operations have not been acknowledged.");
                ValidateNativeGeometry(batch, source);
                // Capture cleans up all candidate buffers on an allocation or
                // projection failure. Pending state/effects/reservation survive.
                buildingSnapshot = true;
                try
                {
                    var candidate = OwnedGameDataSnapshot.Capture(source, batch.Snapshot.Width, batch.Snapshot.Height, overlay =>
                    {
                        project(overlay, batch.Snapshot);
                        batch.AppendCallbacks(overlay);
                    }, allowMissingTextures);
                    try
                    {
                        // Even an incorrectly reentrant projection cannot leave
                        // an orphan candidate after cancel/retirement.
                        RequirePending(batch); ValidateLeases(batch); batch.Commit.EnsureCurrent();
                        ValidateCallbackOwnership(batch, candidate.Pointer); batch.Candidate = candidate;
                    }
                    catch { candidate.Dispose(); throw; }
                }
                finally { buildingSnapshot = false; }
            }
        }

        internal FluidFramePublication Publish(PreparedFluidBatch batch)
        {
            lock (sync)
            {
                RequirePending(batch); ValidateLeases(batch);
                if (batch.Candidate == null) throw new InvalidOperationException("Prepare the merged owned snapshot before publishing.");
                var publication = new FluidFramePublication(batch, batch.Candidate);
                // Core allocates its paired holder before committing one reference.
                // The only companion is this already built immutable publication.
                _ = batch.Commit.Commit(publication, retainReservationUntilDisposed: true);
                var previous = published; published = publication;
                batch.Candidate = null; batch.Published = true; awaitingConsumption = true;
                previous?.NativeSnapshot.Dispose();
                return publication;
            }
        }

        internal FluidFramePublication BeginConsumption(FluidFrameTicket ticket)
        {
            lock (sync)
            {
                RequireDelivery(ticket);
                if (consuming || consumptionFault.Length != 0) throw new InvalidOperationException("Consumption is already in progress or ambiguous; it is never automatically replayed.");
                ValidateLeases(pending!); consuming = true; return published!;
            }
        }

        internal void ReportConsumptionFault(FluidFrameTicket ticket, string reason)
        {
            lock (sync) { RequireDelivery(ticket); if (!consuming) throw new InvalidOperationException("Consumption has not started."); consumptionFault = Reason(reason); }
        }

        internal void AcknowledgeConsumption(FluidFrameTicket ticket)
        {
            lock (sync)
            {
                RequireDelivery(ticket);
                if (!consuming || consumptionFault.Length != 0) throw new InvalidOperationException("Ambiguous consumption needs explicit resolution; acknowledgement requires consumption to have started.");
                FinishConsumption();
            }
        }

        // Caller must settle/attest an ambiguous external outcome itself. This
        // never reruns callbacks, refunds mass or claims external rollback.
        internal void ResolveConsumptionFault(FluidFrameTicket ticket, string resolution)
        {
            lock (sync) { RequireDelivery(ticket); if (consumptionFault.Length == 0) throw new InvalidOperationException("No ambiguous consumption to resolve."); _ = Reason(resolution); FinishConsumption(); }
        }

        private void FinishConsumption()
        {
            foreach (var lease in pending!.Leases)
                if (lease.Kind != FluidCallbackKind.Emitted && callbacks.TryGetValue((lease.Kind, lease.Index), out var current) && current.Generation == lease.Generation)
                    callbacks.Remove((lease.Kind, lease.Index));
            pending.Closed = true; pending.Commit.Dispose(); pending = null;
            awaitingConsumption = false; consuming = false; consumptionFault = "";
        }

        internal void Cancel(PreparedFluidBatch batch)
        {
            lock (sync)
            {
                if (batch.Closed) return;
                if (!ReferenceEquals(pending, batch)) throw new InvalidOperationException("Foreign prepared frame.");
                if (batch.Published) throw new InvalidOperationException("Published effects require explicit consumption acknowledgement.");
                if (batch.DispatchStarted) { nativeFault = "Native dispatch may have changed the world; cancellation requires external native recovery."; return; }
                CancelBeforeDispatch(batch);
            }
        }

        // Explicit recovery acknowledgement is needed even if dispatch only
        // partly ran. Caller restores/recreates native state before releasing.
        internal void AcknowledgeNativeRecovery(PreparedFluidBatch batch, string resolution)
        {
            lock (sync)
            {
                RequireOpen();
                if (!ReferenceEquals(pending, batch) || batch.Published || nativeFault.Length == 0) throw new InvalidOperationException("No pending native recovery.");
                _ = Reason(resolution); CancelBeforeDispatch(batch); nativeFault = "";
            }
        }

        private void CancelBeforeDispatch(PreparedFluidBatch batch)
        { batch.Candidate?.Dispose(); batch.Candidate = null; batch.Commit.Dispose(); batch.Closed = true; pending = null; }
        private static bool HasLease(PreparedFluidBatch batch, FluidCallbackKind kind, int index)
        { foreach (var lease in batch.Leases) if (lease.Kind == kind && lease.Index == index) return true; return false; }
        private FluidCallbackLease FindLease(FluidCallbackKind kind, int index)
            => callbacks.TryGetValue((kind, index), out var lease) ? lease : throw new InvalidOperationException("Callback is not explicitly registered for this session.");
        private void ValidateLease(FluidCallbackLease lease)
        {
            if (lease.Session != session || !callbacks.TryGetValue((lease.Kind, lease.Index), out var current) || current.Generation != lease.Generation)
                throw new InvalidOperationException("Callback registration is stale, retired or belongs to another session.");
        }
        private void ValidateLeases(PreparedFluidBatch batch) { foreach (var lease in batch.Leases) ValidateLease(lease); }
        private static string Reason(string text) => string.IsNullOrWhiteSpace(text) || text.Length > 4096 ? throw new ArgumentException("An explicit bounded outcome description is required.") : text;
        private void RequireOpen() { if (disposed) throw new ObjectDisposedException(nameof(ExperimentalFluidFrameCoordinator)); }
        private void ValidateAuthority()
        {
            var current = state.CapturePublication();
            if (published != null && (!ReferenceEquals(current.Companion, published) || !ReferenceEquals(current.Snapshot, published.Snapshot)))
                throw new InvalidOperationException("Another publisher replaced this coordinator's coherent state/snapshot pair; start a new explicit ownership session.");
        }
        private void RequireUsable() { RequireOpen(); ValidateAuthority(); if (nativeFault.Length != 0) throw new InvalidOperationException("Native recovery is required: " + nativeFault); }
        private void RequirePending(PreparedFluidBatch batch)
        { RequireUsable(); if (!ReferenceEquals(batch.Owner, this) || !ReferenceEquals(pending, batch) || batch.Closed || batch.Published) throw new InvalidOperationException("Prepared frame is foreign, closed or already published."); }
        private void RequireDelivery(FluidFrameTicket ticket)
        { RequireUsable(); if (!awaitingConsumption || published == null || !published.Ticket.Equals(ticket)) throw new InvalidOperationException("Publication ticket is stale, foreign or already acknowledged."); }
        private static void ValidateNativeGeometry(PreparedFluidBatch batch, Sim.GameDataUpdate* snapshot)
        {
            if (snapshot == null || snapshot->elementIdx == null) throw new ArgumentException("A stable native snapshot is required.");
            var expected = new Dictionary<int, ushort>();
            foreach (var operation in batch.NativeOperations) if (operation.ExpectedElementIndex.HasValue) expected[operation.CellIndex] = operation.ExpectedElementIndex.Value;
            foreach (var item in expected)
                if (snapshot->elementIdx[item.Key] != item.Value) throw new InvalidOperationException("Native geometry has not acknowledged the complete ordered batch.");
        }

        private void ValidateCallbackOwnership(PreparedFluidBatch batch, Sim.GameDataUpdate* snapshot)
        {
            var expected = new Dictionary<(FluidCallbackKind, int), int>(); var observed = new Dictionary<(FluidCallbackKind, int), int>();
            foreach (var lease in batch.Leases) Increment(expected, (lease.Kind, lease.Index));
            for (var i = 0; i < snapshot->numCallbackInfo; ++i) Count(FluidCallbackKind.Generic, snapshot->callbackInfo[i].callbackIdx);
            for (var i = 0; i < snapshot->numMassConsumedCallbacks; ++i) Count(FluidCallbackKind.Consumed, snapshot->massConsumedCallbacks[i].callbackIdx);
            for (var i = 0; i < snapshot->numMassEmittedCallbacks; ++i) Count(FluidCallbackKind.Emitted, snapshot->massEmittedCallbacks[i].callbackIdx);
            foreach (var item in observed)
                if (!expected.TryGetValue(item.Key, out var count) || count != item.Value) throw new InvalidOperationException("Native/projection callbacks overlap the explicitly owned outbox.");
            foreach (var item in expected)
                if (!observed.TryGetValue(item.Key, out var count) || count != item.Value) throw new InvalidOperationException("Owned callback records were not preserved in the merged snapshot.");
            void Count(FluidCallbackKind kind, int index) { var key = (kind, index); if (callbacks.ContainsKey(key)) Increment(observed, key); }
            static void Increment(Dictionary<(FluidCallbackKind, int), int> counts, (FluidCallbackKind, int) key)
            { counts.TryGetValue(key, out var count); counts[key] = checked(count + 1); }
        }

        public void Dispose()
        {
            lock (sync)
            {
                if (disposed) return;
                // Shutdown discards the whole in-session protocol. It is not a
                // native recovery acknowledgement and cannot resume this owner.
                if (pending != null) { pending.Candidate?.Dispose(); pending.Commit.Dispose(); pending.Closed = true; pending = null; }
                published?.NativeSnapshot.Dispose(); published = null; callbacks.Clear(); disposed = true;
            }
        }
    }
}
