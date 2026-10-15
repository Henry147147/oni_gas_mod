using System.Reflection;
using HarmonyLib;
using MixedAtmospheres.Core;
using MixedAtmospheres.Oni;

internal static unsafe partial class Program
{
    private static void IdleLeaseExact()
    {
        using var f = new Fixture(extraEmptyRow: true); var source = f.State.CapturePublication(); var bytes = f.State.Save();
        using var lease = f.Session.ReserveIdleSaveEligibility(source);
        Check(ReferenceEquals(lease.Source, source) && ReferenceEquals(lease.Snapshot, source.Snapshot) && lease.Snapshot.Containers.Count == 3);
        Check(lease.Owners.Count == 2 && lease.Owners[0].Native.SavedInstanceId == 0 && lease.Owners[1].Allocation.Inventory.TotalMassKg == 0);
        Check(lease.Owners.All(row => ReferenceEquals(row.Allocation, source.Snapshot.SampleContainer(row.Owner.Container))));
        Check(lease.SaveCoreCheckpoint().SequenceEqual(bytes) && ReferenceEquals(source, f.State.CapturePublication()));
        Check(f.HostA.Notifications == 0 && f.HostB.Notifications == 0 && source.Snapshot.SampleContainer(f.Extra).Inventory.TotalMassKg == 0);
        var reads = f.HostA.EligibilityReads; lease.EnsureCurrent(); Check(f.HostA.EligibilityReads == reads);
        Reject<NotSupportedException>(() => ((IList<OwnedReservoirEligibilityObservation>)lease.Owners)[0] = lease.Owners[1]);
        Check(Task.Run(() => ReferenceEquals(source, f.State.CapturePublication())).Result);
    }
    private static void IdleLeaseExclusion()
    {
        using var f = new Fixture(); SetBridge("active", f.Session); var source = f.State.CapturePublication(); var bytes = f.State.Save();
        using var lease = f.Session.ReserveIdleSaveEligibility(source);
        Reject(() => f.Move(1)); Reject(() => f.Session.UnbindEmpty(f.LeaseB)); Reject(() => f.Session.Bind(new Host("extra"), f.B));
        using var tx = f.State.BeginTransaction(); tx.TransferContainerToContainer(f.A, f.B, .25);
        using var prepared = tx.PrepareFrame(2, .4); Reject(prepared.ReserveWriter); Reject(() => prepared.Commit());
        Reject(() => f.State.ReservePublication(source)); Reject<OwnedFluidSaveBlockedException>(() => OwnedGasReservoirBridge.SavePrefix());
        Check(OwnedGasReservoirBridge.SaveIsBlocked && bytes.SequenceEqual(f.State.Save()) && f.Session.ReadCompleted(f.LeaseA).Inventory.TotalMassKg == 4);
        var storage = FakeNativeBinding(f, f.LeaseA); Reject<OwnedFluidSaveBlockedException>(() => OwnedGasReservoirBridge.StorageSerializePrefix(storage));
        lease.Dispose(); using var next = f.Move(1); f.Publish(next); f.Finish(next);
        Check(f.Session.ReadCompleted(f.LeaseB).Inventory.TotalMassKg == 1);
    }
    private static void IdleLeaseSources()
    {
        using var f = new Fixture(); var old = f.State.CapturePublication();
        using (var move = f.Move(1)) { f.Publish(move); f.Finish(move); }
        Reject(() => f.Session.ReserveIdleSaveEligibility(old));
        var clone = ColonyFluidState.Restore(f.State.Save(), f.Catalog); Check(clone.CaptureSnapshot().ColonyId == f.State.CaptureSnapshot().ColonyId);
        Reject(() => f.Session.ReserveIdleSaveEligibility(clone.CapturePublication())); Reject<ArgumentNullException>(() => f.Session.ReserveIdleSaveEligibility(null!));
        using var staged = f.Move(.5); Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()));
        f.Publish(staged); Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()));
        f.Session.BeginConsumption(staged); f.HostA.AfterNotification = _ => Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()));
        f.Session.NotifyActors(staged); Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()));
        f.Session.AcknowledgeSnapshotConsumption(staged); f.Session.AcknowledgeConsumption(staged);
        using var valid = f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()); valid.EnsureCurrent();
    }
    private static void IdleLeaseDomain()
    {
        using (var f = new Fixture(worldMetadata: true))
        { Check(f.State.CaptureSnapshot().SampleCell(2).Inventory.TotalMassKg == 0); Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication())); }
        using (var f = new Fixture(unboundMetadata: true))
        { Check(f.State.CaptureSnapshot().Inventories.Count == 0); Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication())); }
        using (var f = new Fixture())
        { f.Session.UnbindEmpty(f.LeaseB); Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication())); }
        using (var f = new Fixture(extraEmptyRow: true))
        { f.Session.Bind(new Host("third"), f.Extra); Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication())); }
        using (var f = new Fixture(capacityB: .5)) Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()));
        using (var f = new Fixture())
        {
            using var evacuation = f.Session.Prepare(new[] { OwnedContainerTransferOperation.EvacuateToCell(f.LeaseA, 2) }, 2, .4, f.Model);
            f.Publish(evacuation); f.Finish(evacuation); Check(f.Session.WorldRecoveryRequired);
            Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()));
        }
    }
    private static void IdleLeaseWriterAndCallbacks()
    {
        using var f = new Fixture(); var source = f.State.CapturePublication();
        using (var tx = f.State.BeginTransaction()) using (var candidate = tx.PrepareFrame(2, .4))
        { candidate.ReserveWriter(); Reject(() => f.Session.ReserveIdleSaveEligibility(source)); Check(!f.Session.HasIdleSaveLease); }
        var coordinator = (ExperimentalFluidFrameCoordinator)typeof(OwnedFluidActorSession).GetField("coordinator", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(f.Session)!;
        var callback = coordinator.RegisterCallback(FluidCallbackKind.Emitted, 42); Reject(() => f.Session.ReserveIdleSaveEligibility(source));
        coordinator.UnregisterCallback(callback);
        using var lease = f.Session.ReserveIdleSaveEligibility(source); lease.EnsureCurrent();
    }
    private static void IdleLeaseAcknowledgedIdentity()
    {
        using var f = new Fixture(); var source = f.State.CapturePublication(); var map = (System.Collections.IDictionary)typeof(OwnedFluidActorSession).GetField("bindings", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(f.Session)!;
        var binding = map[f.LeaseA.Token]!; var field = binding.GetType().GetField("Acknowledged", BindingFlags.Instance | BindingFlags.NonPublic)!;
        var original = field.GetValue(binding); field.SetValue(binding, ColonyFluidState.Restore(f.State.Save(), f.Catalog).CaptureSnapshot().SampleContainer(f.A));
        Reject(() => f.Session.ReserveIdleSaveEligibility(source)); field.SetValue(binding, original);
        using var valid = f.Session.ReserveIdleSaveEligibility(source); valid.EnsureCurrent();
    }
    private static void IdleLeaseReentry()
    {
        using var f = new Fixture(); var source = f.State.CapturePublication(); var checks = 0;
        f.HostA.BeforeEligibility = () =>
        {
            Reject(() => f.Session.ReserveIdleSaveEligibility(source)); Reject(() => f.Move(1));
            Reject(() => f.Session.UnbindEmpty(f.LeaseB)); Reject(() => f.Session.Bind(new Host("extra"), f.B));
            Check(Task.Run(() => ReferenceEquals(source, f.State.CapturePublication())).Wait(TimeSpan.FromSeconds(5))); ++checks;
        };
        using var lease = f.Session.ReserveIdleSaveEligibility(source); Check(checks == 1);
        f.HostA.BeforeEligibility = () => { lease.EnsureCurrent(); Reject(lease.RevalidateNativeOwners); };
        lease.RevalidateNativeOwners(); lease.EnsureCurrent(); Check(f.HostA.Notifications == 0);
    }
    private static void IdleLeaseRetireDuringAcquire()
    {
        using var f = new Fixture(); var bytes = f.State.Save();
        f.HostA.BeforeEligibility = () => f.Session.ForceDetach(f.LeaseB, "B destroyed during A eligibility validation");
        Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()));
        Check(f.HostA.EligibilityReads == 1 && f.HostB.EligibilityReads == 0 && !f.Session.HasIdleSaveLease);
        Check(bytes.SequenceEqual(f.State.Save()) && f.Session.Orphans.Single().Owner.Container.Equals(f.B));
        using var released = f.State.ReservePublication(f.State.CapturePublication());
    }
    private static void IdleLeaseEndDuringValidation()
    {
        using var f = new Fixture(); using var copy = OwnedGameDataSnapshot.Capture(f.Native.Pointer, 2, 2);
        using var lease = f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()); var oldNative = f.Session.Current!.NativeSnapshot;
        var readsB = f.HostB.EligibilityReads; f.HostA.BeforeEligibility = () => f.Session.EndSession("ended inside native eligibility read");
        Reject(lease.RevalidateNativeOwners);
        Check(lease.AbortReason == OwnedReservoirLeaseAbortReason.SessionEnded && f.HostB.EligibilityReads == readsB && !f.Session.HasIdleSaveLease);
        Reject(lease.EnsureCurrent); Reject<ObjectDisposedException>(() => oldNative.AssertEquivalentTo(f.Native.Pointer)); copy.AssertEquivalentTo(f.Native.Pointer);
        using var released = f.State.ReservePublication(f.State.CapturePublication());
    }
    private static void IdleLeaseForcedCleanup()
    {
        using var f = new Fixture(); using var lease = f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication());
        var storage = FakeNativeBinding(f, f.LeaseA); OwnedGasReservoirBridge.ForcedCleanup(storage, "actual host cleanup eligibility abort");
        Check(lease.AbortReason == OwnedReservoirLeaseAbortReason.OwnerDetached && !f.Session.HasIdleSaveLease && OwnedGasReservoirBridge.BindingCount == 0);
        Check(f.Session.Orphans.Single().Owner.Equals(f.LeaseA) && OwnedGasReservoirBridge.SaveIsBlocked);
        Reject(() => lease.SaveCoreCheckpoint()); using var released = f.State.ReservePublication(f.State.CapturePublication());
    }
    private static void IdleLeaseValidationFailure()
    {
        using var f = new Fixture(); var bytes = f.State.Save(); f.HostA.EligibilityValid = false;
        Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()));
        Check(!f.Session.HasIdleSaveLease && f.HostB.EligibilityReads == 0 && bytes.SequenceEqual(f.State.Save()));
        f.HostA.EligibilityValid = true;
        using var lease = f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()); f.HostB.NativeEmpty = false;
        Reject(lease.RevalidateNativeOwners); Check(!f.Session.HasIdleSaveLease && lease.AbortReason == OwnedReservoirLeaseAbortReason.NativeValidationFailed);
        using var released = f.State.ReservePublication(f.State.CapturePublication());
    }
    private static void IdleLeaseSuccessor()
    {
        using var f = new Fixture(); var source = f.State.CapturePublication(); var old = f.Session.ReserveIdleSaveEligibility(source);
        var readsB = f.HostB.EligibilityReads; f.HostA.BeforeEligibility = old.Dispose; Reject(old.RevalidateNativeOwners);
        Check(f.HostB.EligibilityReads == readsB && !f.Session.HasIdleSaveLease); f.HostA.BeforeEligibility = null;
        using var successor = f.Session.ReserveIdleSaveEligibility(source); old.Dispose(); successor.EnsureCurrent();
        Reject(old.EnsureCurrent); Reject(() => old.SaveCoreCheckpoint()); Reject(() => f.Move(1));
        successor.Dispose(); using var next = f.Move(1);
    }
    private static void IdleLeaseObservationChanges()
    {
        foreach (var native in new[]
        {
            new OwnedReservoirNativeObservation(9,"GasReservoir",0,1,0,4,400.125,1000),
            new OwnedReservoirNativeObservation(1,"GasReservoir",0,2,0,4,400.125,1000),
            new OwnedReservoirNativeObservation(1,"GasReservoir",0,1,1,4,400.125,1000),
            new OwnedReservoirNativeObservation(1,"GasReservoir",0,1,0,5,400.125,1000),
            new OwnedReservoirNativeObservation(1,"GasReservoir",0,1,0,4,401,1000)
        })
        {
            using var f = new Fixture(); using var lease = f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication());
            f.HostB.Observation = native; Reject(lease.RevalidateNativeOwners);
            Check(lease.AbortReason == OwnedReservoirLeaseAbortReason.NativeValidationFailed && !f.Session.HasIdleSaveLease);
        }
        using (var f = new Fixture())
        {
            f.HostB.Observation = new OwnedReservoirNativeObservation(0,"GasReservoir",0,1,0,4,400.125,1000);
            Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()));
            f.HostB.Observation = new OwnedReservoirNativeObservation(1,"GasReservoir",1,1,0,4,400.125,1000);
            Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()));
        }
    }
    private static void IdleLeaseThread()
    {
        using var f = new Fixture(); using var lease = f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()); var reads = f.HostA.EligibilityReads;
        Task.Run(() =>
        {
            Reject(lease.EnsureCurrent); Reject(lease.RevalidateNativeOwners); Reject(() => lease.SaveCoreCheckpoint()); Reject(lease.Dispose);
            Reject(() => f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication()));
        }).GetAwaiter().GetResult();
        Check(f.HostA.EligibilityReads == reads); lease.EnsureCurrent();
    }
    private static void IdleLeaseNativeContract()
    {
        var native = typeof(OwnedGasReservoirBridge).GetNestedType("NativeHost", BindingFlags.NonPublic)!;
        Check(typeof(IOwnedFluidActorSaveEligibilityHost).IsAssignableFrom(native));
        var method = native.GetMethod("CaptureIdleSaveObservation")!;
        var il = PatchProcessor.GetOriginalInstructions(method); var calls = il.Where(i => i.operand is MethodBase).Select(i => ((MethodBase)i.operand).Name).ToList();
        foreach (var name in new[] { "ValidateNativeState", "GetComponents", "RequireCapturedComponent", "get_IsConnected", "PosToCell", "IsValidCell", "TrySampleCell", "get_Orientation", "get_Mass" })
            Check(calls.Contains(name), "Native eligibility omitted " + name);
        Check(calls.Count(name => name == "RequireNativeOwnership") == 2 && calls.Count(name => name == "get_IsConnected") == 2);
        var fields = il.Where(i => i.operand is FieldInfo).Select(i => ((FieldInfo)i.operand).Name).ToHashSet();
        foreach (var field in new[] { "InstanceID", "PrefabTag", "Owner", "capacityKg", "conduitType", "WorldIdx", "ElementID", "Reservoir", "Smart", "Consumer", "Dispenser", "OrientationComponent", "Lifetime" }) Check(fields.Contains(field), "Native eligibility omitted " + field);
        var componentCheck = native.GetMethod("RequireCapturedComponent", BindingFlags.NonPublic | BindingFlags.Instance)!;
        var componentIl = PatchProcessor.GetOriginalInstructions(componentCheck);
        var componentCalls = componentIl.Where(i => i.operand is MethodBase).Select(i => ((MethodBase)i.operand).Name).ToList();
        foreach (var name in new[] { "GetComponents", "get_isSpawned", "get_enabled" }) Check(componentCalls.Contains(name), "Exact component check omitted " + name);
        // The compiler lowers Object.ReferenceEquals to a reference comparison,
        // rather than an ordinary call. Inspect its exact array-element inputs.
        var exactComparison = false;
        for (var i = 0; i + 2 < componentIl.Count; ++i)
            if (componentIl[i].opcode == System.Reflection.Emit.OpCodes.Ldelem_Ref && componentIl[i + 1].opcode == System.Reflection.Emit.OpCodes.Ldarg_2 &&
                (componentIl[i + 2].opcode == System.Reflection.Emit.OpCodes.Ceq || componentIl[i + 2].opcode == System.Reflection.Emit.OpCodes.Bne_Un ||
                 componentIl[i + 2].opcode == System.Reflection.Emit.OpCodes.Bne_Un_S || componentIl[i + 2].opcode == System.Reflection.Emit.OpCodes.Beq || componentIl[i + 2].opcode == System.Reflection.Emit.OpCodes.Beq_S)) exactComparison = true;
        Check(exactComparison, "Native role did not compare actual[0] to the exact captured component reference.");
        var constructorFields = PatchProcessor.GetOriginalInstructions(native.GetConstructors(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic).Single()).Where(i => i.operand is FieldInfo).Select(i => ((FieldInfo)i.operand).Name).ToHashSet();
        foreach (var field in new[] { "Reservoir", "Smart", "Consumer", "Dispenser", "OrientationComponent" }) Check(constructorFields.Contains(field), "Bind did not capture " + field);
        var bindFields = PatchProcessor.GetOriginalInstructions(AccessTools.Method(typeof(OwnedGasReservoirBridge), nameof(OwnedGasReservoirBridge.BindNativeEmptyReservoir)))
            .Where(i => i.operand is FieldInfo).Select(i => ((FieldInfo)i.operand).Name).ToHashSet(); Check(bindFields.Contains("Lifetime"));
        Check(typeof(OwnedReservoirIdleSaveLease).GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic).All(property => !property.PropertyType.IsPointer));
        Check(typeof(OwnedReservoirEligibilityObservation).GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic).All(property => property.Name != "PersistentOwnerToken"));
    }
    private static void IdleLeaseUnbindReentry()
    {
        using var f = new Fixture(); var source = f.State.CapturePublication(); OwnedReservoirIdleSaveLease? acquired = null;
        f.HostB.BeforeBasicValidation = () =>
        {
            f.HostB.BeforeBasicValidation = null;
            acquired = f.Session.ReserveIdleSaveEligibility(source);
        };
        Reject(() => f.Session.UnbindEmpty(f.LeaseB));
        Check(acquired != null && f.Session.BindingCount == 2 && f.Session.ReadCompleted(f.LeaseB).Inventory.TotalMassKg == 0);
        acquired!.EnsureCurrent(); acquired.Dispose(); f.Session.UnbindEmpty(f.LeaseB); Check(f.Session.BindingCount == 1);
    }
    private static void IdleLeaseBridgeIdentity()
    {
        using var f = new Fixture(); FakeNativeBinding(f, f.LeaseA); FakeNativeBinding(f, f.LeaseB); SetBridge("installed", true);
        try
        {
            // These deliberately incomplete native objects must be rejected on
            // managed host identity before any Unity getter can execute.
            Reject(() => OwnedGasReservoirBridge.ReserveIdleSaveEligibility(f.State.CapturePublication()));
            Check(!f.Session.HasIdleSaveLease && f.HostA.EligibilityReads == 0 && f.HostB.EligibilityReads == 0);
            using var lease = f.Session.ReserveIdleSaveEligibility(f.State.CapturePublication());
            Reject<ArgumentException>(() => OwnedGasReservoirBridge.EndSession("")); lease.EnsureCurrent();
        }
        finally { SetBridge("installed", false); }
    }
}
