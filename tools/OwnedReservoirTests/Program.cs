using System.Buffers.Binary;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using HarmonyLib;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using MixedAtmospheres.Oni;

internal static unsafe partial class Program
{
    private static int Main(string[] args)
    {
        if (args.Length != 1) throw new ArgumentException("Pass the locally licensed ONI installation.");
        var managed = Path.Combine(Path.GetFullPath(args[0]), "OxygenNotIncluded_Data", "Managed");
        AssemblyLoadContext.Default.Resolving += (_, name) =>
        { var path = Path.Combine(managed, name.Name + ".dll"); return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null; };
        return Run();
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Run()
    {
        var tests = new (string, System.Action)[]
        {
            ("activation rejects missing explicit opt-in before game access", OptIn),
            ("initial paired snapshot and consumption are required before native owner binding", InitialPublication),
            ("bulk transfer publishes exact handles conserved mixed quantities and immutable receipts", Transfer),
            ("late invalid transfer rolls back the entire batch and releases writer", LateFailure),
            ("destination capacity failure preserves both inventories", Capacity),
            ("foreign session and detached actor leases reject", ForeignLeases),
            ("owner and allocation cannot be bound twice", DuplicateBindings),
            ("missing and duplicate snapshot/notification acknowledgements reject", AckOrder),
            ("malformed native snapshot retries without replay or custom publication", SnapshotRetry),
            ("native-state violation between prepare and publish leaves previous state", NativeChanged),
            ("notification exception cannot refund mass or replay callbacks", NotificationFault),
            ("forced detachment before publication retains old allocation and faults session", DetachBeforePublish),
            ("forced cleanup during operation enumeration cannot publish staged quantities", DetachDuringEnumeration),
            ("forced detachment during callback retains committed allocation and ambiguous outbox", DetachDuringCallback),
            ("session teardown inside callback stops later notifications without replay", EndDuringCallback),
            ("session shutdown retains committed orphan allocation without native refund", Shutdown),
            ("empty published teardown retains native readers and sticky consumption recovery", EmptyUnacknowledgedTeardown),
            ("normal unbind requires acknowledged whole-inventory evacuation", NormalUnbind),
            ("closed explicit Core-cell evacuation empties both reservoirs without changing native world", EvacuateToCoreCell),
            ("blocked Core-cell evacuation rejects the entire ordered batch", BlockedEvacuation),
            ("actor operations require activation thread", ThreadGuard),
            ("another publisher cannot silently replace completed actor authority", ForeignPublisher),
            ("installed licensed native storage port save and quit contracts are complete", GuardContracts),
            ("dormant guards do not own unregistered storage or saves", Dormant),
            ("save-first quit and autosave stop before native side effects", SaveGuards),
            ("unbound seeded allocations remain visible and block native saves after teardown", UnboundRecovery),
            ("orphan recovery observations refresh the exact current allocation revision", RefreshedRecovery),
            ("native mutation fences reject both owned source and destination", MutationGuards),
            ("Store rejects stale owned pickupable source before native item side effects", StoreSourceGuard),
            ("all licensed native transfer destinations are direct Storage and fenced at entry", TransferEndpointContracts),
            ("native mass capacity and emptiness queries use the completed full handle", CompletedQueries),
            ("binding lifetime uses forced cleanup without prefab-wide activation", LifetimeContract),
            ("native notification channels recheck ownership between every external call", NotificationChannelGuards),
            ("every conditional prefix matches actual licensed method and injected parameter signatures", PrefixSignatures),
            ("idle save lease pins exact acknowledged Core bytes and immutable observations", IdleLeaseExact),
            ("idle save lease blocks Core and actor writes without authorizing native saves", IdleLeaseExclusion),
            ("idle save eligibility rejects stale foreign and unacknowledged publications", IdleLeaseSources),
            ("idle save eligibility requires exactly two owners and complete containers-only domain", IdleLeaseDomain),
            ("idle save eligibility rejects registered callbacks and a competing prepared writer", IdleLeaseWriterAndCallbacks),
            ("idle save lease rejects equivalent but independently restored acknowledged allocations", IdleLeaseAcknowledgedIdentity),
            ("idle native validator reentry cannot bind transfer unbind or recursively acquire", IdleLeaseReentry),
            ("owner A retiring owner B stops validation before B and retains its allocation", IdleLeaseRetireDuringAcquire),
            ("session end during revalidation aborts before native companion disposal", IdleLeaseEndDuringValidation),
            ("forced native cleanup aborts eligibility and releases the managed reservation", IdleLeaseForcedCleanup),
            ("native validation failure releases the reservation without a partial lease", IdleLeaseValidationFailure),
            ("released save lease cannot validate or release a successor", IdleLeaseSuccessor),
            ("idle lease native observations remain exact on revalidation", IdleLeaseObservationChanges),
            ("idle lease operations require activation thread before native validation", IdleLeaseThread),
            ("reentrant ordinary unbind validation cannot remove an eligibility-pinned owner", IdleLeaseUnbindReentry),
            ("bridge eligibility rejects a native host table that differs from exact actor hosts", IdleLeaseBridgeIdentity),
            ("actual native eligibility repeats prefab components ports lifetime and world checks", IdleLeaseNativeContract)
        };
        var failed = 0;
        foreach (var (name, test) in tests)
        {
            try { test(); Console.WriteLine("PASS " + name); }
            catch (Exception error) { ++failed; Console.WriteLine("FAIL " + name + ": " + error); }
            finally { ResetBridge(); }
        }
        Console.WriteLine($"{tests.Length - failed}/{tests.Length} owned reservoir tests passed; no Unity simulator or UI executed.");
        return failed == 0 ? 0 : 1;
    }
    private static void Check(bool value, string message = "Assertion failed") { if (!value) throw new Exception(message); }
    private static T Reject<T>(System.Action action) where T : Exception
    { try { action(); } catch (T error) { return error; } throw new Exception("Expected " + typeof(T).Name); }
    private static void Reject(System.Action action) => Reject<InvalidOperationException>(action);

    private static void OptIn()
    {
        Reject(() => new OwnedFluidActorSession(null!, null!, false));
        Reject(() => OwnedGasReservoirBridge.BeginSession(null!, null!, false));
    }
    private static void InitialPublication()
    {
        using var f = new Fixture(false); Reject(() => f.Session.Bind(f.HostA, f.A));
        using var initial = f.Session.Prepare(Array.Empty<OwnedContainerTransferOperation>(), 1, .2, f.Model);
        f.Session.PrepareSnapshot(initial, f.Native.Pointer); var publication = f.Session.Publish(initial);
        Check(ReferenceEquals(publication.Presentation, initial.Presentation)); Reject(() => f.Session.Bind(f.HostA, f.A));
        f.Finish(initial); f.LeaseA = f.Session.Bind(f.HostA, f.A);
        Check(f.Session.ReadCompleted(f.LeaseA).Inventory.TotalMassKg == 4);
    }
    private static void Transfer()
    {
        using var f = new Fixture(); var before = f.State.Save(); var totals = f.State.CaptureSnapshot().Totals();
        using var batch = f.Move(1);
        Check(before.SequenceEqual(f.State.Save()) && batch.Receipts.Count == 2 && f.HostA.Notifications == 0);
        Check(batch.Snapshot.SampleContainer(f.A).Inventory.TotalMassKg == 3 && batch.Snapshot.SampleContainer(f.B).Inventory.TotalMassKg == 1);
        Check(batch.Snapshot.SampleContainer(f.B).Inventory.DiseaseCounts[1234] == 3.25 && batch.Snapshot.SampleContainer(f.B).Inventory.RadiationQuantity == 1.25);
        Same(totals, batch.Snapshot.Totals());
        f.Session.PrepareSnapshot(batch, f.Native.Pointer); var publication = f.Session.Publish(batch);
        publication.NativeSnapshot.AssertEquivalentTo(f.Native.Pointer);
        Check(ReferenceEquals(f.State.CapturePublication().Companion, publication) && ReferenceEquals(publication.Presentation, batch.Presentation));
        Check(f.Session.ReadCompleted(f.LeaseB).Inventory.TotalMassKg == 1 && f.HostB.Notifications == 0);
        f.Finish(batch); Check(f.HostA.Notifications == 1 && f.HostB.Notifications == 1);
        Same(totals, f.State.CaptureSnapshot().Totals());
        Check(batch.Receipts[0].Before.Inventory.TotalMassKg == 4 && batch.Receipts[0].After.Inventory.TotalMassKg == 3);
    }
    private static void LateFailure()
    {
        using var f = new Fixture(); var before = f.State.Save();
        Reject(() => f.Session.Prepare(new[] { new OwnedContainerTransferOperation(f.LeaseA, f.LeaseB, 1), new OwnedContainerTransferOperation(f.LeaseA, f.LeaseB, 100) }, 2, .4, f.Model));
        Check(before.SequenceEqual(f.State.Save()) && !f.Session.HasPendingBatch && f.HostA.Notifications == 0);
        using var next = f.Move(1);
    }
    private static void Capacity()
    {
        using var f = new Fixture(capacityB: .5); var before = f.State.Save();
        Reject(() => f.Move(1)); Check(before.SequenceEqual(f.State.Save()) && !f.Session.HasPendingBatch);
    }
    private static void ForeignLeases()
    {
        using var f = new Fixture(); using var other = new Fixture();
        Reject(() => f.Session.ReadCompleted(other.LeaseA)); Reject(() => f.Session.Prepare(new[] { new OwnedContainerTransferOperation(other.LeaseA, f.LeaseB, 1) }, 2, .4, f.Model));
        f.Session.ForceDetach(f.LeaseA, "test forced object removal"); Reject(() => f.Session.ReadCompleted(f.LeaseA));
        Check(f.Session.RecoverySnapshot.SampleContainer(f.A).Inventory.TotalMassKg == 4);
    }
    private static void DuplicateBindings()
    {
        using var f = new Fixture(); Reject(() => f.Session.Bind(new Host("different-owner"), f.A));
        Reject(() => f.Session.Bind(new Host(f.HostA.OwnerId), f.B));
    }
    private static void AckOrder()
    {
        using var f = new Fixture(); using var batch = f.Move(1); f.Publish(batch);
        Reject(() => f.Session.AcknowledgeConsumption(batch)); Reject(() => f.Session.NotifyActors(batch));
        f.Session.BeginConsumption(batch); Reject(() => f.Session.AcknowledgeConsumption(batch));
        f.Session.NotifyActors(batch); Reject(() => f.Session.NotifyActors(batch)); Reject(() => f.Session.AcknowledgeConsumption(batch));
        f.Session.AcknowledgeSnapshotConsumption(batch); Reject(() => f.Session.AcknowledgeSnapshotConsumption(batch));
        f.Session.AcknowledgeConsumption(batch); Reject(() => f.Session.AcknowledgeConsumption(batch));
    }
    private static void SnapshotRetry()
    {
        using var f = new Fixture(); var before = f.State.Save(); using var batch = f.Move(1);
        f.Native.Pointer->numCallbackInfo = -1; Reject<InvalidDataException>(() => f.Session.PrepareSnapshot(batch, f.Native.Pointer));
        Check(before.SequenceEqual(f.State.Save()) && f.HostA.Notifications == 0); f.Native.Pointer->numCallbackInfo = 0;
        f.Publish(batch); f.Finish(batch); Check(f.HostA.Notifications == 1);
    }
    private static void NativeChanged()
    {
        using var f = new Fixture(); var before = f.State.Save(); using var batch = f.Move(1);
        f.Session.PrepareSnapshot(batch, f.Native.Pointer); f.HostB.NativeEmpty = false;
        Reject(() => f.Session.Publish(batch)); Check(before.SequenceEqual(f.State.Save()));
        batch.Dispose(); f.HostB.NativeEmpty = true; using var retry = f.Move(1);
    }
    private static void NotificationFault()
    {
        using var f = new Fixture(); using var batch = f.Move(1); f.Publish(batch); f.Session.BeginConsumption(batch);
        f.HostA.AfterNotification = _ => throw new Exception("callback changed actor then failed");
        Reject<Exception>(() => f.Session.NotifyActors(batch));
        Check(f.HostA.Notifications == 1 && f.HostB.Notifications == 0 && f.State.CaptureSnapshot().SampleContainer(f.B).Inventory.TotalMassKg == 1);
        Reject(() => f.Session.NotifyActors(batch)); Reject(() => f.Session.AcknowledgeConsumption(batch));
        Reject(() => f.Session.ResolveConsumptionFault(batch, "premature resolution without reader acknowledgement"));
        f.Session.AcknowledgeSnapshotConsumption(batch);
        f.Session.ResolveConsumptionFault(batch, "test actor outcome settled externally; no callback replay");
        Check(f.HostA.Notifications == 1 && !f.Session.HasPendingBatch);
    }
    private static void DetachBeforePublish()
    {
        using var f = new Fixture(); using var batch = f.Move(1); f.Session.ForceDetach(f.LeaseA, "test destroyed before publish");
        Reject(() => f.Session.Publish(batch)); Check(f.State.CaptureSnapshot().SampleContainer(f.A).Inventory.TotalMassKg == 4);
        Check(f.Session.Orphans.Count == 1 && f.Session.Orphans[0].Allocation!.Inventory.TotalMassKg == 4); batch.Dispose();
    }
    private static void DetachDuringCallback()
    {
        using var f = new Fixture(); using var batch = f.Move(1); f.Publish(batch); f.Session.BeginConsumption(batch);
        f.HostA.AfterNotification = _ => f.Session.ForceDetach(f.LeaseA, "forced cleanup in callback");
        Reject(() => f.Session.NotifyActors(batch)); Check(f.Session.Orphans.Single().Allocation!.Inventory.TotalMassKg == 3);
        Check(f.State.CaptureSnapshot().SampleContainer(f.B).Inventory.TotalMassKg == 1); Reject(() => f.Session.NotifyActors(batch));
        f.Session.AcknowledgeSnapshotConsumption(batch);
        f.Session.ResolveConsumptionFault(batch, "retained allocation and stopped actor; no replay");
    }
    private static void DetachDuringEnumeration()
    {
        using var f = new Fixture(); var before = f.State.Save();
        IEnumerable<OwnedContainerTransferOperation> Operations()
        {
            yield return new OwnedContainerTransferOperation(f.LeaseA, f.LeaseB, 1);
            f.Session.ForceDetach(f.LeaseA, "owner destroyed during enumeration");
        }
        Reject(() => f.Session.Prepare(Operations(), 2, .4, f.Model));
        Check(before.SequenceEqual(f.State.Save()) && !f.Session.HasPendingBatch && f.Session.Orphans.Count == 1);
        using var writer = f.State.BeginTransaction(); writer.Commit();
    }
    private static void Shutdown()
    {
        using var f = new Fixture(); using var batch = f.Move(1); f.Publish(batch);
        f.Session.EndSession("native session ended before callbacks");
        Check(f.Session.IsClosed && f.Session.BindingCount == 0 && f.Session.Orphans.Count == 2 && f.Session.HasRetainedSnapshotReaders);
        batch.Publication!.NativeSnapshot.AssertEquivalentTo(f.Native.Pointer);
        Check(f.Session.RecoverySnapshot.SampleContainer(f.A).Inventory.TotalMassKg == 3 && f.Session.RecoverySnapshot.SampleContainer(f.B).Inventory.TotalMassKg == 1);
        Reject<ObjectDisposedException>(() => f.Session.ReadCompleted(f.LeaseA)); f.Session.EndSession("idempotent");
    }
    private static void EndDuringCallback()
    {
        using var f = new Fixture(); using var batch = f.Move(1); f.Publish(batch); f.Session.BeginConsumption(batch);
        f.HostA.AfterNotification = _ => f.Session.EndSession("session teardown inside actor notification");
        Reject(() => f.Session.NotifyActors(batch));
        Check(f.Session.IsClosed && f.HostA.Notifications == 1 && f.HostB.Notifications == 0 && batch.Closed && batch.Fault.Length != 0);
        Check(f.Session.RecoverySnapshot.SampleContainer(f.A).Inventory.TotalMassKg == 3 && f.Session.HasRetainedSnapshotReaders);
        batch.Publication!.NativeSnapshot.AssertEquivalentTo(f.Native.Pointer);
        batch.Dispose();
    }
    private static void EmptyUnacknowledgedTeardown()
    {
        using var f = new Fixture(false);
        using (var tx = f.State.BeginTransaction()) { tx.TransferAllContainerToCell(f.A, 0); tx.Commit(); }
        SetBridge("active", f.Session);
        using var batch = f.Session.Prepare(Array.Empty<OwnedContainerTransferOperation>(), 1, .2, f.Model);
        f.Publish(batch); f.Session.BeginConsumption(batch); var native = batch.Publication!.NativeSnapshot;
        OwnedGasReservoirBridge.EndSession("empty batch ended without consumption acknowledgement");
        Check(f.Session.IsClosed && f.Session.BindingCount == 0 && f.Session.HasRetainedSnapshotReaders && f.Session.UnresolvedConsumptionAtTeardown);
        native.AssertEquivalentTo(f.Native.Pointer);
        Check(OwnedGasReservoirBridge.SaveIsBlocked && OwnedGasReservoirBridge.UnresolvedConsumptionRecoveryRequired);
        f.Session.AcknowledgeEndedSessionSnapshotReaders(batch.Ticket);
        Check(!f.Session.HasRetainedSnapshotReaders && OwnedGasReservoirBridge.SaveIsBlocked);
        Reject<ObjectDisposedException>(() => native.AssertEquivalentTo(f.Native.Pointer));
        Reject(() => f.Session.AcknowledgeEndedSessionSnapshotReaders(batch.Ticket));
    }
    private static void NormalUnbind()
    {
        using var f = new Fixture(); Reject(() => f.Session.UnbindEmpty(f.LeaseA));
        using var batch = f.Session.Prepare(new[] { OwnedContainerTransferOperation.All(f.LeaseA, f.LeaseB) }, 2, .4, f.Model);
        f.Publish(batch); Reject(() => f.Session.UnbindEmpty(f.LeaseA)); f.Finish(batch); f.Session.UnbindEmpty(f.LeaseA);
        Check(f.Session.BindingCount == 1 && f.State.CaptureSnapshot().SampleContainer(f.A).Inventory.TotalMassKg == 0 && f.State.CaptureSnapshot().SampleContainer(f.B).Inventory.TotalMassKg == 4);
        Reject(() => f.Session.ReadCompleted(f.LeaseA));
    }
    private static void ThreadGuard()
    { using var f = new Fixture(); Task.Run(() => Reject(() => f.Session.ReadCompleted(f.LeaseA))).GetAwaiter().GetResult(); }
    private static void ForeignPublisher()
    { using var f = new Fixture(); using var tx = f.State.BeginTransaction(); tx.Commit(); Reject(() => f.Session.ReadCompleted(f.LeaseA)); }
    private static void EvacuateToCoreCell()
    {
        using var f = new Fixture(); var total = f.State.CaptureSnapshot().Totals();
        SetBridge("active", f.Session);
        using var batch = f.Session.Prepare(new[] { new OwnedContainerTransferOperation(f.LeaseA, f.LeaseB, 1),
            OwnedContainerTransferOperation.EvacuateToCell(f.LeaseA, 2), OwnedContainerTransferOperation.EvacuateToCell(f.LeaseB, 2) }, 2, .4, f.Model);
        Check(batch.Snapshot.SampleCell(2).Inventory.TotalMassKg == 4 && batch.Receipts.Count == 2);
        Same(total, batch.Snapshot.Totals()); f.Publish(batch);
        batch.Publication!.NativeSnapshot.AssertEquivalentTo(f.Native.Pointer);
        Reject(() => f.Session.UnbindEmpty(f.LeaseA)); f.Finish(batch);
        f.Session.UnbindEmpty(f.LeaseA); f.Session.UnbindEmpty(f.LeaseB);
        Check(f.Session.BindingCount == 0 && f.Session.Orphans.Count == 0);
        OwnedGasReservoirBridge.EndSession("normal acknowledged evacuation"); Check(f.Session.Orphans.Count == 0);
        Check(f.Session.RecoverySnapshot.SampleCell(2).Inventory.TotalMassKg == 4);
        Check(f.Session.WorldRecoveryRequired && f.Session.WorldRecoveryCells.SequenceEqual(new[] { 2 }) &&
            OwnedGasReservoirBridge.WorldRecoveryRequired && OwnedGasReservoirBridge.SaveIsBlocked);
        Reject<OwnedFluidSaveBlockedException>(() => OwnedGasReservoirBridge.SavePrefix());
    }
    private static void BlockedEvacuation()
    {
        using var f = new Fixture(); var before = f.State.Save();
        Reject<ArgumentOutOfRangeException>(() => f.Session.Prepare(new[] { new OwnedContainerTransferOperation(f.LeaseA, f.LeaseB, 1),
            OwnedContainerTransferOperation.EvacuateToCell(f.LeaseB, 999) }, 2, .4, f.Model));
        Check(before.SequenceEqual(f.State.Save()) && !f.Session.HasPendingBatch && f.HostA.Notifications == 0);
    }

    private static void GuardContracts()
    {
        var contracts = OwnedGasReservoirBridge.GuardContracts();
        foreach (var name in new[] { "Store", "AddElement", "AddGasChunk", "AddLiquid", "AddOre", "AddToPrimaryElement", "ConsumeAndGetDisease", "ConsumeIgnoringDisease", "ConsumeAllIgnoringDisease", "Transfer", "TransferMass", "TransferUnitMass", "Remove", "DropAll", "DropSome", "Serialize", "OnQueueDestroyObject" })
            Check(contracts.Any(item => item.target.DeclaringType == typeof(Storage) && item.target.Name == name), "Missing Storage guard: " + name);
        Check(contracts.Count(item => item.target.DeclaringType == typeof(Storage) && item.target.Name == "Transfer") == 3);
        Check(contracts.Any(item => item.target.DeclaringType == typeof(ConduitConsumer) && item.target.Name == "Consume"));
        Check(contracts.Any(item => item.target.DeclaringType == typeof(ConduitDispenser) && item.target.Name == "Dispense"));
        Check(contracts.Any(item => item.target.DeclaringType == typeof(Game) && item.target.Name == "StartDelayedSave"));
        Check(contracts.Count(item => item.target.DeclaringType == typeof(PauseScreen) && item.target.Name.Contains("QuitConfirm")) == 2);
        Check(contracts.All(item => item.target.GetMethodBody() != null && item.prefix.ReturnType == typeof(bool)));
    }
    private static void Dormant()
    {
        var method = AccessTools.Method(typeof(Storage), nameof(Storage.Store));
        Check(OwnedGasReservoirBridge.StorageMutationPrefix(null!, method) && OwnedGasReservoirBridge.StorePrefix(null!, null!));
        Check(OwnedGasReservoirBridge.StorageTransferPrefix(null!, Array.Empty<object>(), method));
        Check(!OwnedGasReservoirBridge.SaveIsBlocked && OwnedGasReservoirBridge.SavePrefix() && OwnedGasReservoirBridge.QuitPrefix(true));
    }
    private static void SaveGuards()
    {
        using var f = new Fixture(); SetBridge("active", f.Session); var reports = new List<string>(); OwnedGasReservoirBridge.FailureReporter = reports.Add;
        Check(OwnedGasReservoirBridge.SaveIsBlocked && !OwnedGasReservoirBridge.SaveUiPrefix() && !OwnedGasReservoirBridge.QuitPrefix(true));
        Check(OwnedGasReservoirBridge.QuitPrefix(false) && reports.Count == 2);
        Reject<OwnedFluidSaveBlockedException>(() => OwnedGasReservoirBridge.SavePrefix());
        OwnedGasReservoirBridge.EndSession("test forced teardown");
        Check(OwnedGasReservoirBridge.SaveIsBlocked && OwnedGasReservoirBridge.RecoveryOrphans.Count == 2 && OwnedGasReservoirBridge.RecoverySnapshot!.SampleContainer(f.A).Inventory.TotalMassKg == 4);
    }
    private static void MutationGuards()
    {
        using var f = new Fixture(); var storage = FakeNativeBinding(f, f.LeaseA);
        var method = AccessTools.Method(typeof(Storage), nameof(Storage.Store));
        Reject<OwnedFluidOperationRejectedException>(() => OwnedGasReservoirBridge.StorageMutationPrefix(storage, method));
        Reject<OwnedFluidOperationRejectedException>(() => OwnedGasReservoirBridge.StorePrefix(storage, null!));
        Reject<OwnedFluidOperationRejectedException>(() => OwnedGasReservoirBridge.StorageTransferPrefix(null!, new object[] { storage }, method));
        Check(f.State.CaptureSnapshot().SampleContainer(f.A).Inventory.TotalMassKg == 4);
    }
    private static void StoreSourceGuard()
    {
        using var f = new Fixture(); var owned = FakeNativeBinding(f, f.LeaseA);
        var unowned = (Storage)RuntimeHelpers.GetUninitializedObject(typeof(Storage));
        unowned.items = new List<UnityEngine.GameObject>();
        // Only the real managed auto-property is exercised here. This object
        // has no Unity component identity, transform, lifecycle or native body.
        var source = (Pickupable)RuntimeHelpers.GetUninitializedObject(typeof(Pickupable));
        source.storage = owned;
        var before = f.State.Save(); var rejections = OwnedGasReservoirBridge.RejectedMutationCount;
        var error = Reject<OwnedFluidOperationRejectedException>(() => OwnedGasReservoirBridge.RejectStoreSource(source));
        Check(error.Message.EndsWith("Store source", StringComparison.Ordinal));
        Check(OwnedGasReservoirBridge.RejectedMutationCount == rejections + 1 && ReferenceEquals(source.storage, owned));
        Check(owned.items.Count == 0 && unowned.items.Count == 0 && before.SequenceEqual(f.State.Save()));
        source.storage = unowned; OwnedGasReservoirBridge.RejectStoreSource(source);
        source.storage = null; OwnedGasReservoirBridge.RejectStoreSource(source); OwnedGasReservoirBridge.RejectStoreSource(null);
        Check(OwnedGasReservoirBridge.RejectedMutationCount == rejections + 1 && before.SequenceEqual(f.State.Save()));

        var prefix = typeof(OwnedGasReservoirBridge).GetMethod(nameof(OwnedGasReservoirBridge.StorePrefix), BindingFlags.NonPublic | BindingFlags.Static)!;
        var calls = PatchProcessor.GetOriginalInstructions(prefix).Where(instruction => instruction.operand is MethodInfo)
            .Select(instruction => (MethodInfo)instruction.operand).ToArray();
        var component = Array.FindIndex(calls, call => call.DeclaringType == typeof(UnityEngine.GameObject) && call.Name == "GetComponent" &&
            call.IsGenericMethod && call.GetGenericArguments().SequenceEqual(new[] { typeof(Pickupable) }));
        var sourceCheck = Array.FindIndex(calls, call => call.DeclaringType == typeof(OwnedGasReservoirBridge) && call.Name == nameof(OwnedGasReservoirBridge.RejectStoreSource));
        Check(component >= 0 && sourceCheck > component, "Store prefix does not resolve the actual item's Pickupable before checking its source storage.");
        Check(!calls.Any(call => call.DeclaringType == typeof(Storage) && call.Name == nameof(Storage.Store)), "Store guard entered the native body.");
    }
    private static void TransferEndpointContracts()
    {
        var contracts = OwnedGasReservoirBridge.GuardContracts().Where(contract => contract.target.DeclaringType == typeof(Storage) &&
            contract.target.Name is "Transfer" or "TransferMass" or "TransferUnitMass").ToArray();
        Check(contracts.Length == 5);
        var expected = new (string Name, Type[] Parameters)[]
        {
            ("Transfer", new[] { typeof(Storage), typeof(bool), typeof(bool) }),
            ("Transfer", new[] { typeof(Storage), typeof(Tag), typeof(float), typeof(bool), typeof(bool) }),
            ("Transfer", new[] { typeof(UnityEngine.GameObject), typeof(Storage), typeof(bool), typeof(bool) }),
            ("TransferMass", new[] { typeof(Storage), typeof(Tag), typeof(float), typeof(bool), typeof(bool), typeof(bool) }),
            ("TransferUnitMass", new[] { typeof(Storage), typeof(Tag), typeof(float), typeof(bool), typeof(bool), typeof(bool) })
        };
        using var f = new Fixture(); var source = FakeNativeBinding(f, f.LeaseA); var destination = FakeNativeBinding(f, f.LeaseB);
        var unowned = (Storage)RuntimeHelpers.GetUninitializedObject(typeof(Storage)); unowned.items = new List<UnityEngine.GameObject>();
        var before = f.State.Save();
        foreach (var signature in expected)
        {
            var contract = contracts.Single(item => item.target.Name == signature.Name &&
                item.target.GetParameters().Select(parameter => parameter.ParameterType).SequenceEqual(signature.Parameters));
            Check(contract.prefix.Name == nameof(OwnedGasReservoirBridge.StorageTransferPrefix));
            var arguments = signature.Parameters.Select(type => type.IsValueType ? Activator.CreateInstance(type) : null).ToArray();
            var destinationIndex = Array.IndexOf(signature.Parameters, typeof(Storage)); Check(destinationIndex >= 0);
            arguments[destinationIndex] = destination;
            Reject<OwnedFluidOperationRejectedException>(() => OwnedGasReservoirBridge.StorageTransferPrefix(unowned, arguments!, contract.target));
            arguments[destinationIndex] = unowned;
            Reject<OwnedFluidOperationRejectedException>(() => OwnedGasReservoirBridge.StorageTransferPrefix(source, arguments!, contract.target));
            Check(OwnedGasReservoirBridge.StorageTransferPrefix(unowned, arguments!, contract.target));
        }
        Check(source.items.Count == 0 && destination.items.Count == 0 && unowned.items.Count == 0 && before.SequenceEqual(f.State.Save()));
    }
    private static void UnboundRecovery()
    {
        using var f = new Fixture(false); SetBridge("active", f.Session);
        OwnedGasReservoirBridge.EndSession("session ended before initial publication or native binding");
        Check(OwnedGasReservoirBridge.SaveIsBlocked && OwnedGasReservoirBridge.RecoveryOrphans.Count == 1);
        Check(OwnedGasReservoirBridge.RecoveryOrphans[0].Owner.Container.Equals(f.A) && OwnedGasReservoirBridge.RecoveryOrphans[0].Owner.OwnerId.StartsWith("unbound:"));
        Reject<OwnedFluidSaveBlockedException>(() => OwnedGasReservoirBridge.SavePrefix());
    }
    private static void RefreshedRecovery()
    {
        using var f = new Fixture(); f.Session.ForceDetach(f.LeaseA, "forced removal"); var detached = f.Session.Orphans.Single();
        using (var tx = f.State.BeginTransaction()) { tx.TransferContainerToContainer(f.A, f.B, 1); tx.Commit(); }
        var current = f.Session.Orphans.Single();
        Check(current.DetachmentRevision == detached.Revision && current.Revision > detached.Revision && current.Allocation!.Inventory.TotalMassKg == 3);
        Check(detached.Allocation!.Inventory.TotalMassKg == 4);
    }
    private static void CompletedQueries()
    {
        using var f = new Fixture(); var storageA = FakeNativeBinding(f, f.LeaseA); var storageB = FakeNativeBinding(f, f.LeaseB);
        float value = -1; bool flag = true;
        Check(!OwnedGasReservoirBridge.MassPrefix(storageA, ref value) && value == 4);
        using var batch = f.Move(1); Check(!OwnedGasReservoirBridge.MassPrefix(storageA, ref value) && value == 4);
        f.Publish(batch); Check(!OwnedGasReservoirBridge.MassPrefix(storageA, ref value) && value == 3);
        Check(!OwnedGasReservoirBridge.MassPrefix(storageB, ref value) && value == 1);
        Check(!OwnedGasReservoirBridge.RemainingPrefix(storageA, ref value) && value == 997);
        Check(!OwnedGasReservoirBridge.EmptyPrefix(storageB, ref flag) && !flag); f.Finish(batch);
    }
    private static void LifetimeContract()
    {
        var lifetime = typeof(OwnedGasReservoirLifetime);
        Check(lifetime.GetMethod("OnForcedCleanUp", BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.DeclaredOnly) != null);
        Check(lifetime.GetMethod("OnPrefabInit", BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.DeclaredOnly) == null);
        Check(typeof(OwnedGasReservoirBridge).GetCustomAttributes(typeof(HarmonyPatch), false).Length == 0, "Bridge autoactivates through Harmony discovery.");
        var methods = typeof(OwnedGasReservoirBridge).GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static);
        foreach (var method in methods.Where(method => method.GetMethodBody() != null))
        {
            var calls = PatchProcessor.GetOriginalInstructions(method).Where(instruction => instruction.operand is MethodInfo).Select(instruction => (MethodInfo)instruction.operand);
            Check(!calls.Any(call => call.DeclaringType == typeof(PrimaryElement) && call.Name.StartsWith("set_")), "Bridge writes construction material.");
        }
    }
    private static void NotificationChannelGuards()
    {
        var type = typeof(OwnedGasReservoirBridge).GetNestedType("NativeHost", BindingFlags.NonPublic)!;
        var method = type.GetMethod("NotifyChanged", BindingFlags.Public | BindingFlags.Instance)!;
        var calls = PatchProcessor.GetOriginalInstructions(method).Where(instruction => instruction.operand is MethodBase).Select(instruction => ((MethodBase)instruction.operand).Name).ToList();
        var guards = calls.Select((name, index) => (name, index)).Where(item => item.name == "RequireNativeOwnership").Select(item => item.index).ToArray();
        var trigger = calls.IndexOf("Trigger"); var action = calls.IndexOf("Invoke"); var logic = calls.IndexOf("Sim200ms");
        Check(guards.Length == 4 && guards[0] < trigger && trigger < guards[1] && guards[1] < action && action < guards[2] && guards[2] < logic && logic < guards[3]);
    }
    private static Storage FakeNativeBinding(Fixture f, OwnedFluidActorLease lease)
    {
        SetBridge("active", f.Session);
        var storage = (Storage)RuntimeHelpers.GetUninitializedObject(typeof(Storage));
        storage.items = new List<UnityEngine.GameObject>();
        var nativeType = typeof(OwnedGasReservoirBridge).GetNestedType("NativeHost", BindingFlags.NonPublic)!;
        var host = RuntimeHelpers.GetUninitializedObject(nativeType); nativeType.GetField("Lease", BindingFlags.NonPublic | BindingFlags.Instance)!.SetValue(host, lease);
        nativeType.GetField("Storage", BindingFlags.NonPublic | BindingFlags.Instance)!.SetValue(host, storage);
        var dictionary = typeof(OwnedGasReservoirBridge).GetField("Hosts", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;
        dictionary.GetType().GetMethod("Add")!.Invoke(dictionary, new[] { storage, host }); return storage;
    }
    private static void PrefixSignatures()
    {
        // Shipped Harmony uses BinaryFormatter for its global patch table;
        // .NET 10 deliberately cannot execute that Mono-only installation path.
        // Validate every injected parameter against the licensed declarations.
        // Actual Unity installation remains the separate in-game acceptance.
        foreach (var contract in OwnedGasReservoirBridge.GuardContracts())
            foreach (var parameter in contract.prefix.GetParameters())
                Check(parameter.Name switch
                {
                    "__instance" => parameter.ParameterType.IsAssignableFrom(contract.target.DeclaringType!),
                    "__originalMethod" => parameter.ParameterType == typeof(MethodBase),
                    "__args" => parameter.ParameterType == typeof(object[]),
                    "__result" => parameter.ParameterType.IsByRef && parameter.ParameterType.GetElementType() == contract.target.ReturnType,
                    _ => contract.target.GetParameters().Any(native => native.Name == parameter.Name && native.ParameterType == parameter.ParameterType)
                }, "Prefix parameter contract differs: " + contract.target + " / " + parameter.Name);
    }
    private static void SetBridge(string name, object? value) => typeof(OwnedGasReservoirBridge).GetField(name, BindingFlags.NonPublic | BindingFlags.Static)!.SetValue(null, value);
    private static void ResetBridge()
    {
        SetBridge("active", null); SetBridge("lastEnded", null); OwnedGasReservoirBridge.FailureReporter = null;
        var dictionary = typeof(OwnedGasReservoirBridge).GetField("Hosts", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;
        dictionary.GetType().GetMethod("Clear")!.Invoke(dictionary, null);
    }
    private static void Same(FluidInventory a, FluidInventory b)
    {
        Check(a.TotalMassKg == b.TotalMassKg && a.TotalEnergyJoules == b.TotalEnergyJoules && a.MomentumX == b.MomentumX && a.MomentumY == b.MomentumY && a.RadiationQuantity == b.RadiationQuantity);
        Check(a.MassesKg.Count == b.MassesKg.Count && a.MassesKg.All(entry => b.MassesKg[entry.Key] == entry.Value));
        Check(a.DiseaseCounts.Count == b.DiseaseCounts.Count && a.DiseaseCounts.All(entry => b.DiseaseCounts[entry.Key] == entry.Value));
    }

    private sealed class Host : IOwnedFluidActorHost, IOwnedFluidActorSaveEligibilityHost
    {
        public string OwnerId { get; }
        internal bool NativeEmpty = true;
        internal int Notifications;
        internal System.Action<OwnedFluidActorReceipt>? AfterNotification;
        internal System.Action? BeforeEligibility;
        internal System.Action? BeforeBasicValidation;
        internal int EligibilityReads;
        internal bool EligibilityValid = true;
        internal OwnedReservoirNativeObservation? Observation;
        internal Host(string id) { OwnerId = id; }
        public void ValidateNativeState(FluidContainerSnapshot allocation)
        { BeforeBasicValidation?.Invoke(); if (!NativeEmpty) throw new InvalidOperationException("native contents unexpectedly changed"); }
        public void NotifyChanged(OwnedFluidActorReceipt receipt) { ++Notifications; AfterNotification?.Invoke(receipt); }
        public OwnedReservoirNativeObservation CaptureIdleSaveObservation(FluidContainerSnapshot allocation, ColonyFrameSnapshot source)
        {
            ++EligibilityReads; BeforeEligibility?.Invoke(); ValidateNativeState(allocation);
            if (!EligibilityValid) throw new InvalidOperationException("controlled native eligibility violation");
            return Observation ?? new OwnedReservoirNativeObservation(OwnerId == "a" ? 0 : 1, "GasReservoir", 0, OwnerId == "a" ? 0 : 1, 0, 4, 400.125, 1000);
        }
    }
    private sealed class Fixture : IDisposable
    {
        internal readonly ColonyFluidState State;
        internal readonly FluidMaterialCatalog Catalog;
        internal readonly MixturePresentationModel Model;
        internal readonly OwnedFluidActorSession Session;
        internal readonly NativeFixture Native = new();
        internal readonly FluidContainerHandle A, B, Extra;
        internal readonly Host HostA = new("a"), HostB = new("b");
        internal OwnedFluidActorLease LeaseA, LeaseB;
        internal Fixture(bool initialize = true, double capacityB = 1000, bool extraEmptyRow = false, bool worldMetadata = false, bool unboundMetadata = false)
        {
            var phases = new Dictionary<int, NativeMatterPhase> { [1] = NativeMatterPhase.Vacuum };
            var raw = new byte[29 + 36 * 16]; System.Text.Encoding.ASCII.GetBytes("SIMSAVE\0").CopyTo(raw, 0);
            void Int(int offset, int value) => BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(offset), value);
            Int(8, 15); Int(12, 4); Int(16, 4);
            for (var i = 0; i < 16; ++i) { Int(29 + 16 * i, 1); Int(29 + 24 * 16 + 12 * i, 1); }
            var prefixed = new byte[raw.Length + 4]; BinaryPrimitives.WriteInt32LittleEndian(prefixed, raw.Length); raw.CopyTo(prefixed, 4);
            var import = NativeColonyImport.StageNewColony(2, 2, new[] { new NativeWorldPlacement(0, 2, 2, 0, 0) }, new[] { new NativeWorldSource(0, prefixed) }, phases, new Dictionary<int, NativeFluidBinding>(), 1);
            Catalog = new FluidMaterialCatalog("owned-reservoir-tests", new[] { new FluidMaterial("oxygen", NativeMatterPhase.Gas, .032, 660), new FluidMaterial("carbon_dioxide", NativeMatterPhase.Gas, .044, 655) });
            State = ColonyFluidState.Import(import, Catalog);
            using (var tx = State.BeginTransaction())
            {
                tx.Inject(0, Catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = 3, ["carbon_dioxide"] = 1 }, 300,
                    2, 4, new Dictionary<int, double> { [1234] = 13 }, 5), "explicit owned test source");
                A = tx.CreateContainer("a", new FluidContainerDefinition(1, 1000, FluidContainerPhases.Gas)); B = tx.CreateContainer("b", new FluidContainerDefinition(1, capacityB, FluidContainerPhases.Gas));
                tx.TransferAllCellToContainer(0, A);
                if (extraEmptyRow) Extra = tx.CreateContainer("empty-unbound", new FluidContainerDefinition(.5, 2, FluidContainerPhases.Liquid));
                if (worldMetadata || unboundMetadata)
                {
                    tx.Inject(2, new FluidInventory(new Dictionary<string, double>(), 0, radiationQuantity: 1e-200), "explicit metadata eligibility fixture");
                    if (unboundMetadata)
                    {
                        Extra = tx.CreateContainer("unbound-metadata", new FluidContainerDefinition(1, 1000, FluidContainerPhases.Gas));
                        tx.TransferAllCellToContainer(2, Extra);
                    }
                }
                tx.Commit();
            }
            var router = new ExperimentalFluidMessageRouter(State, Catalog, new[] { new FluidMessageElementBinding(0, NativeMatterPhase.Vacuum), new FluidMessageElementBinding(1, NativeMatterPhase.Gas, "oxygen"), new FluidMessageElementBinding(2, NativeMatterPhase.Gas, "carbon_dioxide") }, new Dictionary<byte, int>());
            Model = new MixturePresentationModel(Catalog, Catalog.Materials.Keys.Select(id => new MixtureSpeciesStyle(id, id, new MixtureSrgbColor(.5, .5, .5))), new AtmosphereSettings(), "oxygen", "carbon_dioxide");
            Session = new OwnedFluidActorSession(State, router, true);
            if (initialize)
            {
                using var initial = Session.Prepare(Array.Empty<OwnedContainerTransferOperation>(), 1, .2, Model);
                Publish(initial); Finish(initial); LeaseA = Session.Bind(HostA, A); LeaseB = Session.Bind(HostB, B);
            }
        }
        internal OwnedFluidActorBatch Move(double mass) => Session.Prepare(new[] { new OwnedContainerTransferOperation(LeaseA, LeaseB, mass) }, 2, .4, Model);
        internal void Publish(OwnedFluidActorBatch batch) { Session.PrepareSnapshot(batch, Native.Pointer); Session.Publish(batch); }
        internal void Finish(OwnedFluidActorBatch batch) { Session.BeginConsumption(batch); Session.NotifyActors(batch); Session.AcknowledgeSnapshotConsumption(batch); Session.AcknowledgeConsumption(batch); }
        public void Dispose()
        {
            Session.Dispose();
            // All controlled test consumers have returned at this scope. The
            // explicit terminal acknowledgement is not an actor success ACK.
            if (Session.HasRetainedSnapshotReaders) Session.AcknowledgeEndedSessionSnapshotReaders(Session.RetainedSnapshotTicket!.Value);
            Native.Dispose();
        }
    }
    private sealed class NativeFixture : IDisposable
    {
        private readonly List<IntPtr> buffers = new();
        private IntPtr storage;
        internal Sim.GameDataUpdate* Pointer => (Sim.GameDataUpdate*)storage;
        internal NativeFixture()
        {
            storage = Marshal.AllocHGlobal(sizeof(Sim.GameDataUpdate)); *Pointer = default; Pointer->numFramesProcessed = 1;
            var type = typeof(Sim.GameDataUpdate);
            foreach (var field in type.GetFields(BindingFlags.Public | BindingFlags.Instance))
            {
                if (!field.FieldType.IsPointer && field.FieldType != typeof(IntPtr)) continue;
                if (type.GetField("num" + char.ToUpperInvariant(field.Name[0]) + field.Name.Substring(1)) != null) continue;
                var stride = field.FieldType.IsPointer ? Marshal.SizeOf(field.FieldType.GetElementType()!) : 1;
                var count = 4;
                if (field.Name == nameof(Sim.GameDataUpdate.propertyTextureFlow)) count *= 8;
                if (field.Name is nameof(Sim.GameDataUpdate.propertyTextureLiquid) or nameof(Sim.GameDataUpdate.propertyTextureLiquidData) or nameof(Sim.GameDataUpdate.propertyTextureMaterialData)) count *= 4;
                var pointer = Marshal.AllocHGlobal(stride * count); new Span<byte>((void*)pointer, stride * count).Clear(); buffers.Add(pointer);
                *(IntPtr*)((byte*)Pointer + (int)Marshal.OffsetOf<Sim.GameDataUpdate>(field.Name)) = pointer;
            }
        }
        public void Dispose() { foreach (var pointer in buffers) Marshal.FreeHGlobal(pointer); buffers.Clear(); if (storage != IntPtr.Zero) Marshal.FreeHGlobal(storage); storage = IntPtr.Zero; }
    }
}
