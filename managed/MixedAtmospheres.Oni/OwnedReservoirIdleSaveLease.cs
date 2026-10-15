using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using MixedAtmospheres.Core;

namespace MixedAtmospheres.Oni
{
    internal enum OwnedReservoirLeaseAbortReason { None, NativeValidationFailed, OwnerDetached, SessionEnded }

    internal sealed class OwnedReservoirComponentObservation
    {
        internal string Role { get; }
        internal bool Enabled { get; }
        internal bool IsSpawned { get; }
        internal OwnedReservoirComponentObservation(string role, bool enabled, bool isSpawned)
        { Role = role; Enabled = enabled; IsSpawned = isSpawned; }
    }

    // Read-only observation, not a persisted owner identity or native save permit.
    internal sealed class OwnedReservoirNativeObservation
    {
        internal int SavedInstanceId { get; }
        internal string PrefabId { get; }
        internal int WorldId { get; }
        internal int AnchorCell { get; }
        internal int Orientation { get; }
        internal int ConstructionElementId { get; }
        internal double ConstructionMassKg { get; }
        internal double CapacityKg { get; }
        internal IReadOnlyList<OwnedReservoirComponentObservation> Components { get; }
        internal OwnedReservoirNativeObservation(int savedInstanceId, string prefabId, int worldId, int anchorCell,
            int orientation, int constructionElementId, double constructionMassKg, double capacityKg,
            IReadOnlyList<OwnedReservoirComponentObservation>? components = null)
        {
            if (savedInstanceId < 0 || prefabId != "GasReservoir" || worldId < 0 || anchorCell < 0 ||
                (orientation != 0 && orientation != 1 && orientation != 2 && orientation != 3 && orientation != 5 && orientation != 6) ||
                double.IsNaN(constructionMassKg) || double.IsInfinity(constructionMassKg) || constructionMassKg < 0 ||
                (double)(float)constructionMassKg != constructionMassKg || capacityKg != 1000)
                throw new InvalidOperationException("Unsupported native reservoir eligibility observation.");
            SavedInstanceId = savedInstanceId; PrefabId = prefabId; WorldId = worldId; AnchorCell = anchorCell;
            Orientation = orientation; ConstructionElementId = constructionElementId; ConstructionMassKg = constructionMassKg; CapacityKg = capacityKg;
            var captured = new OwnedReservoirComponentObservation[components?.Count ?? 0];
            for (var i = 0; i < captured.Length; ++i) captured[i] = components![i];
            Components = new ReadOnlyCollection<OwnedReservoirComponentObservation>(captured);
        }
        internal bool SameAs(OwnedReservoirNativeObservation other) => SavedInstanceId == other.SavedInstanceId &&
            PrefabId == other.PrefabId && WorldId == other.WorldId && AnchorCell == other.AnchorCell && Orientation == other.Orientation &&
            ConstructionElementId == other.ConstructionElementId && ConstructionMassKg == other.ConstructionMassKg && CapacityKg == other.CapacityKg;
    }

    // Implemented by the actual private NativeHost and controlled managed test
    // hosts. The bridge entry additionally verifies its exact native host table.
    internal interface IOwnedFluidActorSaveEligibilityHost
    {
        OwnedReservoirNativeObservation CaptureIdleSaveObservation(FluidContainerSnapshot allocation, ColonyFrameSnapshot source);
    }

    internal sealed class OwnedReservoirEligibilityObservation
    {
        internal OwnedFluidActorLease Owner { get; }
        internal FluidContainerSnapshot Allocation { get; }
        internal OwnedReservoirNativeObservation Native { get; }
        internal double VolumeCubicMetres => Allocation.Definition.VolumeCubicMetres;
        internal OwnedReservoirEligibilityObservation(OwnedFluidActorLease owner, FluidContainerSnapshot allocation, OwnedReservoirNativeObservation native)
        { Owner = owner; Allocation = allocation; Native = native; }
    }

    // Pins only immutable managed publication identity. No borrowed native
    // pointer, persistent token, serializer permit or ordinary-save bypass.
    internal sealed class OwnedReservoirIdleSaveLease : IDisposable
    {
        private readonly OwnedFluidActorSession owner;
        private readonly ColonyPublicationReservation reservation;
        private readonly OwnedFluidActorLease[] actors;
        private readonly IOwnedFluidActorHost[] hosts;
        private readonly object[] bindings;
        private IReadOnlyList<OwnedReservoirEligibilityObservation>? observations;
        private bool closed, releaseDeferred, reservationReleased;
        internal ColonyFramePublication Source => reservation.Source;
        internal ColonyFrameSnapshot Snapshot => reservation.Snapshot;
        internal IReadOnlyList<OwnedReservoirEligibilityObservation> Owners => observations ?? throw new InvalidOperationException("Eligibility acquisition has not completed.");
        internal OwnedReservoirLeaseAbortReason AbortReason { get; private set; }
        internal OwnedReservoirIdleSaveLease(OwnedFluidActorSession owner, ColonyPublicationReservation reservation,
            OwnedFluidActorLease[] actors, IOwnedFluidActorHost[] hosts, object[] bindings)
        { this.owner = owner; this.reservation = reservation; this.actors = actors; this.hosts = hosts; this.bindings = bindings; }
        internal OwnedFluidActorLease ActorAt(int index) => actors[index];
        internal IOwnedFluidActorHost HostAt(int index) => hosts[index];
        internal object BindingAt(int index) => bindings[index];
        internal void CheckReservation()
        { if (closed) throw new InvalidOperationException("The idle save eligibility lease was released or aborted."); reservation.EnsureCurrent(); }
        internal void CheckObservation(int index, OwnedReservoirNativeObservation native)
        { if (observations != null && !observations[index].Native.SameAs(native)) throw new InvalidOperationException("A native owner observation changed after eligibility acquisition."); }
        internal void CompleteObservations(OwnedReservoirEligibilityObservation[] values)
        { if (observations == null) observations = new ReadOnlyCollection<OwnedReservoirEligibilityObservation>(values); }
        internal void MarkAborted(OwnedReservoirLeaseAbortReason reason, bool deferRelease)
        {
            if (!closed) { closed = true; AbortReason = reason; }
            releaseDeferred |= deferRelease;
        }
        internal void Release(bool completingCleanup = false)
        {
            closed = true;
            if (reservationReleased || (releaseDeferred && !completingCleanup)) return;
            reservation.Dispose(); reservationReleased = true;
        }
        internal bool ReleaseDeferred => releaseDeferred && !reservationReleased;
        internal void EnsureCurrent() => owner.EnsureIdleSaveLease(this);
        internal void RevalidateNativeOwners() => owner.RevalidateIdleSaveLease(this);
        internal byte[] SaveCoreCheckpoint()
        {
            RevalidateNativeOwners(); var bytes = reservation.SaveCheckpoint(); RevalidateNativeOwners(); return bytes;
        }
        public void Dispose() => owner.ReleaseIdleSaveLease(this);
    }

    internal sealed unsafe partial class OwnedFluidActorSession
    {
        private OwnedReservoirIdleSaveLease? idleSaveLease;
        private bool acquiringIdleSave, validatingIdleSave;
        internal bool HasIdleSaveLease => idleSaveLease != null;
        internal void RequireExactHostBinding(OwnedFluidActorLease actor, IOwnedFluidActorHost host)
        {
            RequireReady();
            if (!ReferenceEquals(RequireLease(actor).Host, host)) throw new InvalidOperationException("The native host differs from the exact bound actor instance.");
        }

        internal OwnedReservoirIdleSaveLease ReserveIdleSaveEligibility(ColonyFramePublication expected)
        {
            RequireReady(); RequireNoIdleSaveLease();
            if (expected == null) throw new ArgumentNullException(nameof(expected));
            RequireIdleSaveDomain(expected);
            acquiringIdleSave = true;
            ColonyPublicationReservation? reservation = null; OwnedReservoirIdleSaveLease? candidate = null;
            try
            {
                var actors = new OwnedFluidActorLease[2]; var hosts = new IOwnedFluidActorHost[2]; var capturedBindings = new object[2]; var i = 0;
                foreach (var binding in bindings.Values)
                {
                    if (!(binding.Host is IOwnedFluidActorSaveEligibilityHost)) throw new InvalidOperationException("A bound actor supplies no explicit idle save eligibility surface.");
                    actors[i] = binding.Lease; hosts[i] = binding.Host; capturedBindings[i++] = binding;
                }
                reservation = state.ReservePublication(expected);
                candidate = new OwnedReservoirIdleSaveLease(this, reservation, actors, hosts, capturedBindings); idleSaveLease = candidate;
                RevalidateIdleSaveLease(candidate); return candidate;
            }
            catch
            {
                if (candidate != null) AbortIdleSaveValidation(candidate); else reservation?.Dispose();
                throw;
            }
            finally { acquiringIdleSave = false; }
        }

        private void RequireNoIdleSaveLease()
        {
            if (idleSaveLease != null || acquiringIdleSave || validatingIdleSave)
                throw new InvalidOperationException("An idle save eligibility lease is acquiring, validating or held.");
        }
        private void RequireIdleSaveDomain(ColonyFramePublication expected)
        {
            RequireReady();
            if (pending != null || preparing || notifying || retainedReaders != null || UnresolvedConsumptionAtTeardown ||
                orphans.Count != 0 || WorldRecoveryRequired || coordinator.HasUnacknowledgedPublication ||
                coordinator.RegistrationCount != 0 || coordinator.ConsumptionFault.Length != 0 || coordinator.NativeRecoveryRequired.Length != 0)
                throw new InvalidOperationException("Idle eligibility requires acknowledged, fault-free, zero-outbox ownership with no recovery obligations.");
            if (current == null || !ReferenceEquals(expected, state.CapturePublication()) || !ReferenceEquals(expected.Companion, current) ||
                !ReferenceEquals(current.Snapshot, expected.Snapshot) || current.PreparedPresentation == null ||
                !ReferenceEquals(current.PreparedPresentation.Source, expected.Snapshot))
                throw new InvalidOperationException("Idle eligibility requires the exact completed Core, companion and presentation publication.");
            if (bindings.Count != 2 || expected.Snapshot.Inventories.Count != 0)
                throw new InvalidOperationException("Idle reservoir eligibility requires exactly two owners and an empty Core world domain.");
            var handles = new HashSet<FluidContainerHandle>();
            foreach (var binding in bindings.Values)
            {
                var allocation = expected.Snapshot.SampleContainer(binding.Lease.Container);
                if (!handles.Add(allocation.Handle) || !ReferenceEquals(binding.Acknowledged, allocation) ||
                    allocation.Definition.MaximumMassKg != 1000 || allocation.Definition.AllowedPhases != FluidContainerPhases.Gas)
                    throw new InvalidOperationException("Every exact reservoir allocation must match its acknowledged gas-only 1000 kg definition.");
            }
            foreach (var allocation in expected.Snapshot.Containers.Values)
                if (!Empty(allocation.Inventory) && !handles.Contains(allocation.Handle)) throw new InvalidOperationException("A nonempty Core allocation lacks an exact acknowledged native owner.");
        }
        internal void EnsureIdleSaveLease(OwnedReservoirIdleSaveLease lease)
        {
            CheckThread();
            if (!ReferenceEquals(idleSaveLease, lease)) throw new InvalidOperationException("The idle save lease is absent, foreign or replaced.");
            lease.CheckReservation(); RequireIdleSaveDomain(lease.Source);
            for (var i = 0; i < 2; ++i)
            {
                var binding = RequireLease(lease.ActorAt(i));
                if (!ReferenceEquals(binding, lease.BindingAt(i)) || !ReferenceEquals(binding.Host, lease.HostAt(i)))
                    throw new InvalidOperationException("An exact native actor binding changed during idle eligibility.");
            }
        }
        internal void RevalidateIdleSaveLease(OwnedReservoirIdleSaveLease lease)
        {
            EnsureIdleSaveLease(lease);
            if (validatingIdleSave) throw new InvalidOperationException("Recursive idle native-owner validation is unsupported.");
            validatingIdleSave = true;
            try
            {
                var observations = new OwnedReservoirEligibilityObservation[2]; var ids = new HashSet<int>(); var anchors = new HashSet<int>();
                for (var i = 0; i < 2; ++i)
                {
                    EnsureIdleSaveLease(lease);
                    var allocation = lease.Snapshot.SampleContainer(lease.ActorAt(i).Container);
                    var native = ((IOwnedFluidActorSaveEligibilityHost)lease.HostAt(i)).CaptureIdleSaveObservation(allocation, lease.Snapshot);
                    // A read-only validator is an external call. Stop before the
                    // next host if it detached, released or ended this session.
                    EnsureIdleSaveLease(lease);
                    if (native == null || !ids.Add(native.SavedInstanceId) || !anchors.Add(native.AnchorCell) ||
                        !lease.Snapshot.TrySampleCell(native.AnchorCell, out var cell) || cell!.WorldId != native.WorldId)
                        throw new InvalidOperationException("Native owner identities or declared world anchors are invalid or duplicate.");
                    lease.CheckObservation(i, native);
                    observations[i] = new OwnedReservoirEligibilityObservation(lease.ActorAt(i), allocation, native);
                }
                EnsureIdleSaveLease(lease); lease.CompleteObservations(observations);
            }
            catch { AbortIdleSaveValidation(lease); throw; }
            finally { validatingIdleSave = false; }
        }
        private void AbortIdleSaveValidation(OwnedReservoirIdleSaveLease lease)
        {
            lease.MarkAborted(OwnedReservoirLeaseAbortReason.NativeValidationFailed, false); lease.Release();
            if (ReferenceEquals(idleSaveLease, lease) && !lease.ReleaseDeferred) idleSaveLease = null;
        }
        internal void ReleaseIdleSaveLease(OwnedReservoirIdleSaveLease lease)
        {
            CheckThread(); lease.Release();
            if (ReferenceEquals(idleSaveLease, lease) && !lease.ReleaseDeferred) idleSaveLease = null;
        }
        private OwnedReservoirIdleSaveLease? AbortIdleSaveForCleanup(OwnedReservoirLeaseAbortReason reason)
        {
            var lease = idleSaveLease; lease?.MarkAborted(reason, true); return lease;
        }
        private void FinishIdleSaveCleanup(OwnedReservoirIdleSaveLease? lease)
        {
            if (lease == null) return; lease.Release(true);
            if (ReferenceEquals(idleSaveLease, lease)) idleSaveLease = null;
        }
    }
}
