using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Threading;
using MixedAtmospheres.Core;

namespace MixedAtmospheres.Oni
{
    // These identities never alias a replacement native object, another colony,
    // or a new allocation with the same text ID. They are not a save format.
    internal readonly struct OwnedFluidActorLease : IEquatable<OwnedFluidActorLease>
    {
        internal Guid Session { get; }
        internal Guid Token { get; }
        internal string OwnerId { get; }
        internal FluidContainerHandle Container { get; }
        internal OwnedFluidActorLease(Guid session, Guid token, string ownerId, FluidContainerHandle container)
        { Session = session; Token = token; OwnerId = ownerId; Container = container; }
        public bool Equals(OwnedFluidActorLease other) => Session == other.Session && Token == other.Token &&
            string.Equals(OwnerId, other.OwnerId, StringComparison.Ordinal) && Container.Equals(other.Container);
        public override bool Equals(object? other) => other is OwnedFluidActorLease lease && Equals(lease);
        public override int GetHashCode() => Session.GetHashCode() ^ Token.GetHashCode() ^ Container.GetHashCode();
    }

    internal sealed class OwnedContainerTransferOperation
    {
        internal OwnedFluidActorLease Source { get; }
        internal OwnedFluidActorLease Destination { get; }
        internal double MassKg { get; }
        internal bool TransferAll { get; }
        internal int? EvacuationCell { get; }
        internal OwnedContainerTransferOperation(OwnedFluidActorLease source, OwnedFluidActorLease destination, double massKg)
        {
            if (double.IsNaN(massKg) || double.IsInfinity(massKg) || massKg < 0) throw new ArgumentOutOfRangeException(nameof(massKg));
            Source = source; Destination = destination; MassKg = massKg;
        }
        private OwnedContainerTransferOperation(OwnedFluidActorLease source, OwnedFluidActorLease destination)
        { Source = source; Destination = destination; TransferAll = true; }
        private OwnedContainerTransferOperation(OwnedFluidActorLease source, int ownedCoreCell)
        { Source = source; EvacuationCell = ownedCoreCell; TransferAll = true; }
        internal static OwnedContainerTransferOperation All(OwnedFluidActorLease source, OwnedFluidActorLease destination)
            => new OwnedContainerTransferOperation(source, destination);
        // Explicit Core-owned-world destination only. This never infers a
        // native vent cell or dispatches a native world emission.
        internal static OwnedContainerTransferOperation EvacuateToCell(OwnedFluidActorLease source, int ownedCoreCell)
            => new OwnedContainerTransferOperation(source, ownedCoreCell);
        internal FluidBatchCommand Command => EvacuationCell.HasValue ? FluidBatchCommand.AllContainerToCell(Source.Container, EvacuationCell.Value) :
            TransferAll ? FluidBatchCommand.AllContainerToContainer(Source.Container, Destination.Container) :
            FluidBatchCommand.ContainerToContainer(Source.Container, Destination.Container, MassKg);
    }

    internal sealed class OwnedFluidActorReceipt
    {
        internal OwnedFluidActorLease Owner { get; }
        internal FluidFrameTicket Ticket { get; }
        internal long Revision { get; }
        internal FluidContainerSnapshot Before { get; }
        internal FluidContainerSnapshot After { get; }
        internal OwnedFluidActorReceipt(OwnedFluidActorLease owner, FluidFrameTicket ticket, long revision,
            FluidContainerSnapshot before, FluidContainerSnapshot after)
        { Owner = owner; Ticket = ticket; Revision = revision; Before = before; After = after; }
    }

    internal sealed class OwnedFluidActorOrphan
    {
        internal OwnedFluidActorLease Owner { get; }
        internal string Reason { get; }
        internal long Revision { get; }
        internal long DetachmentRevision { get; }
        internal FluidContainerSnapshot? Allocation { get; }
        internal OwnedFluidActorOrphan(OwnedFluidActorLease owner, string reason, ColonyFrameSnapshot snapshot)
            : this(owner, reason, snapshot.Revision, snapshot) { }
        private OwnedFluidActorOrphan(OwnedFluidActorLease owner, string reason, long detachedRevision, ColonyFrameSnapshot snapshot)
        { Owner = owner; Reason = reason; DetachmentRevision = detachedRevision; Revision = snapshot.Revision; snapshot.TrySampleContainer(owner.Container, out var allocation); Allocation = allocation; }
        internal OwnedFluidActorOrphan Refresh(ColonyFrameSnapshot snapshot) => new OwnedFluidActorOrphan(Owner, Reason, DetachmentRevision, snapshot);
    }

    // Implementations validate their native surface before mutation and publish
    // external notifications only after the immutable frame is committed.
    // There is no delegate that can mutate a Core transaction.
    internal interface IOwnedFluidActorHost
    {
        string OwnerId { get; }
        void ValidateNativeState(FluidContainerSnapshot allocation);
        void NotifyChanged(OwnedFluidActorReceipt receipt);
    }

    internal sealed class OwnedFluidActorBatch : IDisposable
    {
        internal readonly OwnedFluidActorSession Owner;
        internal readonly ReadOnlyCollection<OwnedFluidActorReceipt> Receipts;
        internal readonly ReadOnlyCollection<int> WorldRecoveryCells;
        internal bool Published, ConsumptionStarted, NotificationAttempted, NotificationsCompleted, SnapshotAcknowledged, Closed;
        internal string Fault = "";
        internal FluidFramePublication? Publication;
        internal FluidFrameTicket Ticket { get; }
        internal ColonyFrameSnapshot Snapshot { get; }
        internal MixturePresentationPublication PreparedPresentation { get; }
        internal MixturePresentationFrame Presentation => PreparedPresentation.Frame;
        internal OwnedFluidActorBatch(OwnedFluidActorSession owner, PreparedFluidBatch prepared, List<OwnedFluidActorReceipt> receipts, List<int> worldRecoveryCells)
        {
            Owner = owner; Ticket = prepared.Ticket; Snapshot = prepared.Snapshot;
            PreparedPresentation = prepared.PreparedPresentation ?? throw new InvalidOperationException("No prepared actor presentation.");
            Receipts = new ReadOnlyCollection<OwnedFluidActorReceipt>(receipts);
            WorldRecoveryCells = new ReadOnlyCollection<int>(worldRecoveryCells);
        }
        public void Dispose() => Owner.Cancel(this);
    }

    /// <summary>
    /// Opt-in, main-thread actor protocol. The private coordinator cannot be
    /// bypassed through this API. Native flow, packet adoption and persistence
    /// are deliberately absent. Forced detachment never retires Core inventory.
    /// </summary>
    internal sealed unsafe partial class OwnedFluidActorSession : IDisposable
    {
        private sealed class Binding
        {
            internal readonly OwnedFluidActorLease Lease;
            internal readonly IOwnedFluidActorHost Host;
            internal FluidContainerSnapshot Acknowledged;
            internal Binding(OwnedFluidActorLease lease, IOwnedFluidActorHost host, FluidContainerSnapshot acknowledged)
            { Lease = lease; Host = host; Acknowledged = acknowledged; }
        }
        private readonly Guid session = Guid.NewGuid();
        private readonly int thread = Thread.CurrentThread.ManagedThreadId;
        private readonly ColonyFluidState state;
        private readonly ExperimentalFluidFrameCoordinator coordinator;
        private readonly Dictionary<Guid, Binding> bindings = new Dictionary<Guid, Binding>();
        private readonly List<OwnedFluidActorOrphan> orphans = new List<OwnedFluidActorOrphan>();
        private OwnedFluidActorBatch? pending;
        private PreparedFluidBatch? pendingPrepared;
        private OwnedFluidActorBatch? retainedReaders;
        private FluidFramePublication? current;
        private bool closed, preparing, notifying;
        private IReadOnlyList<int> worldRecoveryCells = Array.Empty<int>();
        internal string Fault { get; private set; } = "";
        internal int BindingCount => bindings.Count;
        internal bool HasPendingBatch => pending != null;
        internal bool IsClosed => closed;
        internal bool UnresolvedConsumptionAtTeardown { get; private set; }
        internal bool HasRetainedSnapshotReaders => retainedReaders != null;
        internal FluidFrameTicket? RetainedSnapshotTicket => retainedReaders?.Ticket;
        // Sticky in-session obligation: NoWorldProjection never makes an
        // evacuated custom world inventory persistent in a native save.
        internal bool WorldRecoveryRequired => worldRecoveryCells.Count != 0;
        internal IReadOnlyList<int> WorldRecoveryCells => worldRecoveryCells;
        internal IReadOnlyList<OwnedFluidActorOrphan> Orphans
        {
            get
            {
                var snapshot = state.CaptureSnapshot(); var result = new List<OwnedFluidActorOrphan>(orphans.Count);
                foreach (var orphan in orphans) result.Add(orphan.Refresh(snapshot));
                return result.AsReadOnly();
            }
        }
        internal ColonyFrameSnapshot RecoverySnapshot => state.CaptureSnapshot();
        internal FluidFramePublication? Current { get { RequireReady(); return current; } }

        internal OwnedFluidActorSession(ColonyFluidState state, ExperimentalFluidMessageRouter router, bool explicitPrivateOptIn)
        {
            if (!explicitPrivateOptIn) throw new InvalidOperationException("Owned actors require explicit private experimental activation.");
            this.state = state ?? throw new ArgumentNullException(nameof(state));
            coordinator = new ExperimentalFluidFrameCoordinator(state, router ?? throw new ArgumentNullException(nameof(router)));
        }

        internal OwnedFluidActorLease Bind(IOwnedFluidActorHost host, FluidContainerHandle handle)
        {
            RequireReady(); RequireNoIdleSaveLease();
            if (host == null) throw new ArgumentNullException(nameof(host));
            var ownerId = host.OwnerId;
            if (string.IsNullOrWhiteSpace(ownerId) || ownerId.Length > 512) throw new ArgumentException("A bounded native owner identity is required.");
            if (pending != null || preparing || notifying || current == null) throw new InvalidOperationException("Acknowledge a completed publication outside staging/notification before binding actors.");
            var allocation = current.Snapshot.SampleContainer(handle);
            foreach (var binding in bindings.Values)
                if (binding.Lease.OwnerId == ownerId || binding.Lease.Container.Equals(handle)) throw new InvalidOperationException("Owner or allocation is already bound.");
            host.ValidateNativeState(allocation);
            RequireReady(); RequireNoIdleSaveLease();
            var lease = new OwnedFluidActorLease(session, Guid.NewGuid(), ownerId, handle);
            bindings.Add(lease.Token, new Binding(lease, host, allocation)); return lease;
        }

        internal FluidContainerSnapshot ReadCompleted(OwnedFluidActorLease lease)
        {
            RequireReady(); var binding = RequireLease(lease);
            return (current ?? throw new InvalidOperationException("No completed actor frame.")).Snapshot.SampleContainer(binding.Lease.Container);
        }

        internal OwnedFluidActorBatch Prepare(IEnumerable<OwnedContainerTransferOperation> operations, long completedFrame,
            double simulationTimeSeconds, MixturePresentationModel model)
        {
            RequireReady(); RequireNoIdleSaveLease();
            if (operations == null) throw new ArgumentNullException(nameof(operations));
            if (model == null) throw new ArgumentNullException(nameof(model));
            if (pending != null || preparing || notifying) throw new InvalidOperationException("Complete the previous actor batch first.");
            preparing = true;
            PreparedFluidBatch? prepared = null;
            try
            {
                ValidateNativeBindings();
                var commands = new List<FluidBatchCommand>(); var affected = new List<Binding>(); var seen = new HashSet<Guid>();
                var recoveryCells = new List<int>(worldRecoveryCells);
                var basis = state.CaptureSnapshot();
                foreach (var operation in operations)
                {
                    if (operation == null || commands.Count == 4096) throw new ArgumentException("Null operation or actor batch limit exceeded.");
                    var source = RequireLease(operation.Source);
                    if (seen.Add(source.Lease.Token)) affected.Add(source);
                    if (!operation.EvacuationCell.HasValue)
                    {
                        var destination = RequireLease(operation.Destination);
                        if (source.Lease.Container.Equals(destination.Lease.Container)) throw new ArgumentException("Actor transfer requires distinct allocations.");
                        if (seen.Add(destination.Lease.Token)) affected.Add(destination);
                    }
                    else if (!recoveryCells.Contains(operation.EvacuationCell.Value)) recoveryCells.Add(operation.EvacuationCell.Value);
                    commands.Add(operation.Command);
                }
                RequireReady(); ValidateNativeBindings();
                prepared = coordinator.PrepareCommands(commands, completedFrame, simulationTimeSeconds);
                coordinator.PreparePresentation(prepared, model);
                var receipts = new List<OwnedFluidActorReceipt>(affected.Count);
                foreach (var binding in affected)
                    receipts.Add(new OwnedFluidActorReceipt(binding.Lease, prepared.Ticket, prepared.Snapshot.Revision,
                        basis.SampleContainer(binding.Lease.Container), prepared.Snapshot.SampleContainer(binding.Lease.Container)));
                pending = new OwnedFluidActorBatch(this, prepared, receipts, recoveryCells); pendingPrepared = prepared; return pending;
            }
            catch { prepared?.Dispose(); throw; }
            finally { preparing = false; }
        }

        internal void PrepareSnapshot(OwnedFluidActorBatch batch, Sim.GameDataUpdate* native, bool allowMissingTextures = false)
        {
            RequirePending(batch); ValidateNativeBindings(); ValidateReceiptLeases(batch);
            // Actor-only moves project no world change and append no fabricated
            // legacy mass callback. Every licensed native channel is deep owned.
            coordinator.PrepareSnapshot(Prepared(batch), native, NoWorldProjection, allowMissingTextures);
        }
        private static void NoWorldProjection(OwnedSnapshotOverlay overlay, ColonyFrameSnapshot snapshot) { }

        internal FluidFramePublication Publish(OwnedFluidActorBatch batch)
        {
            RequirePending(batch); ValidateNativeBindings(); ValidateReceiptLeases(batch);
            var publication = coordinator.Publish(Prepared(batch));
            batch.Publication = publication; batch.Published = true; current = publication;
            worldRecoveryCells = batch.WorldRecoveryCells; return publication;
        }

        internal FluidFramePublication BeginConsumption(OwnedFluidActorBatch batch)
        {
            RequirePublished(batch); ValidateNativeBindings(); ValidateReceiptLeases(batch);
            var publication = coordinator.BeginConsumption(batch.Ticket); batch.ConsumptionStarted = true; return publication;
        }

        internal void NotifyActors(OwnedFluidActorBatch batch)
        {
            RequirePublished(batch);
            if (!batch.ConsumptionStarted || batch.NotificationAttempted || batch.Fault.Length != 0) throw new InvalidOperationException("Actor notifications require first, unambiguous consumption.");
            ValidateNativeBindings(); ValidateReceiptLeases(batch);
            batch.NotificationAttempted = true; notifying = true;
            try
            {
                foreach (var receipt in batch.Receipts)
                {
                    RequireLease(receipt.Owner).Host.NotifyChanged(receipt);
                    if (batch.Fault.Length != 0 || Fault.Length != 0) throw new InvalidOperationException("Owner changed during notification.");
                }
                batch.NotificationsCompleted = true;
            }
            catch (Exception error)
            {
                batch.Fault = "Actor notification outcome is ambiguous: " + error.Message;
                // A callback can end the complete session. Preserve its orphan
                // outcome without calling a disposed coordinator or replaying.
                if (!closed && ReferenceEquals(pending, batch)) coordinator.ReportConsumptionFault(batch.Ticket, batch.Fault);
                throw;
            }
            finally { notifying = false; }
        }

        // Caller attests the copied native snapshot was consumed and no old
        // pointer reader remains. This is separate from actor event delivery.
        internal void AcknowledgeSnapshotConsumption(OwnedFluidActorBatch batch)
        {
            RequirePublished(batch);
            if (!batch.ConsumptionStarted || batch.SnapshotAcknowledged) throw new InvalidOperationException("Snapshot consumption is absent or already acknowledged.");
            batch.SnapshotAcknowledged = true;
        }

        internal void AcknowledgeConsumption(OwnedFluidActorBatch batch)
        {
            RequirePublished(batch); ValidateNativeBindings(); ValidateReceiptLeases(batch);
            if (!batch.NotificationsCompleted || !batch.SnapshotAcknowledged || batch.Fault.Length != 0)
                throw new InvalidOperationException("Acknowledge both snapshot consumption and completed actor notifications.");
            coordinator.AcknowledgeConsumption(batch.Ticket);
            foreach (var binding in bindings.Values) binding.Acknowledged = batch.Snapshot.SampleContainer(binding.Lease.Container);
            batch.Closed = true; pending = null; pendingPrepared = null;
        }

        internal void ReportConsumptionFault(OwnedFluidActorBatch batch, string reason)
        {
            RequirePublished(batch);
            if (!batch.ConsumptionStarted) throw new InvalidOperationException("Consumption has not started.");
            reason = Reason(reason); batch.Fault = reason; coordinator.ReportConsumptionFault(batch.Ticket, reason);
        }

        internal void ResolveConsumptionFault(OwnedFluidActorBatch batch, string externalResolution)
        {
            RequirePublished(batch);
            if (batch.Fault.Length == 0) throw new InvalidOperationException("No ambiguous actor outcome to resolve.");
            if (!batch.SnapshotAcknowledged) throw new InvalidOperationException("Acknowledge native snapshot readers before settling an actor callback fault.");
            coordinator.ResolveConsumptionFault(batch.Ticket, Reason(externalResolution));
            // No event is replayed and no inventory is refunded. Explicit
            // settlement is not evidence that every actor saw its callback.
            batch.Closed = true; pending = null; pendingPrepared = null;
        }

        internal void UnbindEmpty(OwnedFluidActorLease lease)
        {
            RequireReady(); RequireNoIdleSaveLease(); var binding = RequireLease(lease);
            if (pending != null || preparing || notifying) throw new InvalidOperationException("An actor batch is not acknowledged.");
            var allocation = ReadCompleted(lease); binding.Host.ValidateNativeState(allocation);
            RequireReady(); RequireNoIdleSaveLease();
            if (!ReferenceEquals(binding, RequireLease(lease))) throw new InvalidOperationException("Actor binding changed during unbind validation.");
            if (!Empty(allocation.Inventory) || !ReferenceEquals(binding.Acknowledged, allocation))
                throw new InvalidOperationException("Conservative evacuation must be published and acknowledged before normal unbind.");
            bindings.Remove(lease.Token); // The existing empty Core allocation remains; no implicit retirement.
        }

        internal void ForceDetach(OwnedFluidActorLease lease, string reason)
        {
            CheckThread(); CheckOpen(); var binding = RequireLease(lease); reason = Reason(reason);
            var save = AbortIdleSaveForCleanup(OwnedReservoirLeaseAbortReason.OwnerDetached);
            try
            {
                var orphan = new OwnedFluidActorOrphan(lease, reason, state.CaptureSnapshot());
                orphans.Add(orphan); bindings.Remove(binding.Lease.Token); Fault = "A native owner detached; its Core allocation is retained for recovery: " + reason;
                if (pending != null && pending.Published && pending.ConsumptionStarted && pending.Fault.Length == 0)
                { pending.Fault = Fault; coordinator.ReportConsumptionFault(pending.Ticket, Fault); }
            }
            finally { FinishIdleSaveCleanup(save); }
        }

        internal void Cancel(OwnedFluidActorBatch batch)
        {
            CheckThread();
            if (batch.Closed) return;
            if (!ReferenceEquals(batch.Owner, this) || !ReferenceEquals(batch, pending)) throw new InvalidOperationException("Foreign actor batch.");
            if (batch.Published) throw new InvalidOperationException("Published actor effects require consumption acknowledgement or explicit fault resolution.");
            Prepared(batch).Dispose(); batch.Closed = true; pending = null; pendingPrepared = null;
        }

        internal void EndSession(string reason)
        {
            CheckThread(); if (closed) return; reason = Reason(reason);
            var save = AbortIdleSaveForCleanup(OwnedReservoirLeaseAbortReason.SessionEnded);
            // Record allocations before ending pointer/callback ownership. No
            // Core container is deleted, vented, refunded or silently retired.
            try
            {
                var snapshot = state.CaptureSnapshot(); var represented = new HashSet<FluidContainerHandle>();
                foreach (var orphan in orphans) represented.Add(orphan.Owner.Container);
                foreach (var binding in bindings.Values)
                {
                    orphans.Add(new OwnedFluidActorOrphan(binding.Lease, reason, snapshot)); represented.Add(binding.Lease.Container);
                }
                foreach (var allocation in snapshot.Containers.Values)
                    if (!represented.Contains(allocation.Handle) && !Empty(allocation.Inventory))
                        orphans.Add(new OwnedFluidActorOrphan(new OwnedFluidActorLease(session, Guid.NewGuid(), "unbound:" + allocation.Id, allocation.Handle),
                            reason + " Allocation had no bound native owner.", snapshot));
            }
            finally
            {
                bindings.Clear(); Fault = reason;
                if (pending != null && pending.Published)
                {
                    // Teardown is not a reader acknowledgement or successful
                    // actor-outbox delivery. Keep unresolved delivery sticky.
                    UnresolvedConsumptionAtTeardown = true;
                    if (!pending.SnapshotAcknowledged) retainedReaders = pending;
                }
                try
                {
                    if (retainedReaders == null) coordinator.Dispose();
                    if (pending != null) pending.Closed = true;
                    pending = null; pendingPrepared = null; closed = true;
                }
                finally { FinishIdleSaveCleanup(save); }
            }
        }
        // Terminal reader attestation releases owned native memory only. It
        // never clears the unresolved-outbox/save obligation or resumes actors.
        internal void AcknowledgeEndedSessionSnapshotReaders(FluidFrameTicket ticket)
        {
            CheckThread();
            if (!closed || retainedReaders == null || !retainedReaders.Ticket.Equals(ticket))
                throw new InvalidOperationException("No matching ended-session snapshot readers remain.");
            retainedReaders.SnapshotAcknowledged = true; coordinator.Dispose(); retainedReaders = null;
        }
        public void Dispose() => EndSession("Owned actor session disposed; recover retained allocations explicitly.");
        private static bool Empty(FluidInventory inventory) => inventory.MassesKg.Count == 0 && inventory.DiseaseCounts.Count == 0 && inventory.RadiationQuantity == 0;
        internal static bool InventoryIsEmpty(FluidInventory inventory) => Empty(inventory);
        private static string Reason(string reason) => string.IsNullOrWhiteSpace(reason) || reason.Length > 4096 ? throw new ArgumentException("A bounded explicit reason is required.") : reason;
        private void CheckThread() { if (Thread.CurrentThread.ManagedThreadId != thread) throw new InvalidOperationException("Owned actor operations require the activation thread."); }
        private void CheckOpen() { if (closed) throw new ObjectDisposedException(nameof(OwnedFluidActorSession)); }
        private void ValidateAuthority()
        {
            try
            {
                if (current != null && !ReferenceEquals(coordinator.Current, current)) throw new InvalidOperationException("Actor publication authority changed.");
                if (current != null && !ReferenceEquals(state.CapturePublication().Companion, current)) throw new InvalidOperationException("Another publisher replaced the actor state.");
            }
            catch (InvalidOperationException error) { Fault = "Completed actor publication is unavailable: " + error.Message; throw; }
        }
        private void RequireReady()
        { CheckThread(); CheckOpen(); ValidateAuthority(); if (Fault.Length != 0) throw new InvalidOperationException(Fault); }
        private Binding RequireLease(OwnedFluidActorLease lease)
        {
            if (lease.Session != session || !bindings.TryGetValue(lease.Token, out var binding) || !binding.Lease.Equals(lease))
                throw new InvalidOperationException("Actor lease is stale, foreign, detached or replaced.");
            return binding;
        }
        private void ValidateNativeBindings()
        {
            var snapshot = state.CaptureSnapshot();
            foreach (var binding in bindings.Values) binding.Host.ValidateNativeState(snapshot.SampleContainer(binding.Lease.Container));
        }
        private void ValidateReceiptLeases(OwnedFluidActorBatch batch)
        { foreach (var receipt in batch.Receipts) RequireLease(receipt.Owner); }
        private void RequirePending(OwnedFluidActorBatch batch)
        {
            RequireReady();
            if (!ReferenceEquals(batch.Owner, this) || !ReferenceEquals(batch, pending) || batch.Closed || batch.Published)
                throw new InvalidOperationException("Actor batch is foreign, closed or already published.");
        }
        private PreparedFluidBatch Prepared(OwnedFluidActorBatch batch)
        {
            if (!ReferenceEquals(batch, pending) || pendingPrepared == null) throw new InvalidOperationException("No pending private coordinator batch.");
            return pendingPrepared;
        }
        private void RequirePublished(OwnedFluidActorBatch batch)
        {
            CheckThread(); CheckOpen(); ValidateAuthority();
            if (!ReferenceEquals(batch.Owner, this) || !ReferenceEquals(batch, pending) || batch.Closed || !batch.Published)
                throw new InvalidOperationException("Actor publication is foreign, absent or acknowledged.");
        }
    }
}
