using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Text.Json;
using MixedAtmospheres.Core;
using MixedAtmospheres.Oni;
using static FluidRouterTests;

// Protocol tests use the licensed game's exact struct declarations and owned
// synthetic buffers. They execute no simulator, Harmony hook, or actor callback.
internal static unsafe class FluidFrameCoordinatorTests
{
    internal static void Run(Action<string, System.Action> test, Action<bool> check, Action<System.Action> reject)
    {
        test("one batch consumes its preceding emission with one coherent publication", () =>
        {
            using var f = new Fixture(); var before = f.State.CapturePublication();
            f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 17);
            f.Coordinator.RegisterCallback(FluidCallbackKind.Consumed, 18);
            var payload = Emission(2, 1, 0.5f, 300, 17, 0, 4);
            var emission = new FluidBatchMessage(EmitMass, payload); payload.AsSpan().Fill(0xff);
            using var batch = f.Coordinator.PrepareBatch(new[] { emission, Message(ConsumeMass, Consumption(2, 1, 0.25f, 18)) }, 7, 1.4);
            check(ReferenceEquals(before, f.State.CapturePublication()));
            check(batch.Snapshot.SampleCell(2).Inventory.MassesKg["oxygen"] == 0.25 && batch.Snapshot.SampleCell(2).Inventory.DiseaseCounts[1234] == 2);
            check(batch.Snapshot.SuppliedInventory.MassesKg["oxygen"] == 0.5 && batch.Snapshot.RemovedInventory.MassesKg["oxygen"] == 0.25);
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project);
            var publication = f.Coordinator.Publish(batch); var paired = f.State.CapturePublication();
            check(ReferenceEquals(paired.Snapshot, publication.Snapshot) && ReferenceEquals(paired.Companion, publication));
            check(publication.Snapshot.Revision == 1 && publication.Snapshot.CompletedFrame == 7 && publication.NativeSnapshot.Pointer->mass[2] == 0.25f);
            check(publication.NativeSnapshot.Pointer->numMassEmittedCallbacks == 1 && publication.NativeSnapshot.Pointer->numMassConsumedCallbacks == 1);
            check(publication.NativeSnapshot.Pointer->massConsumedCallbacks[0].diseaseCount == 2);
            check(publication.Effects.All(effect => ReferenceEquals(effect.Snapshot, publication.Snapshot)));
            f.Finish(publication); check(f.Coordinator.RegistrationCount == 1);
        });
        test("late malformed batch payload rolls back fluid geometry ledgers and all native intents", () =>
        {
            using var f = new Fixture(); var before = f.State.Save();
            reject(() => f.Coordinator.PrepareBatch(new[]
            {
                Message(ModifyCell, Modification(2, 3, 1000, 300, replacement: 1)),
                Message(EmitMass, new byte[23])
            }, 1, 0.2));
            check(before.SequenceEqual(f.State.Save()) && f.Coordinator.Current == null);
            using var next = f.Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), 1, 0.2);
            check(!next.Snapshot.SampleCell(2).IsSolid);
        });
        test("unregistered callback fails the entire staged batch before publication", () =>
        {
            using var f = new Fixture(); var before = f.State.Save();
            reject(() => f.Coordinator.PrepareBatch(new[] { Message(EmitMass, Emission(2, 1, 0.5f, 300, 123)) }, 1, 0.2));
            check(before.SequenceEqual(f.State.Save()));
            reject(() => f.Coordinator.RegisterCallback((FluidCallbackKind)100, 1));
            reject(() => f.Coordinator.RegisterCallback(FluidCallbackKind.Generic, -1));
            using var tx = Create().state.BeginTransaction(); reject(() => f.Router.StageInto(tx, ModifyEnergy, Heat(0, 1, 400)));
        });
        test("ordered native dig then custom emission waits for acknowledgement and suppresses duplicate native callback", () =>
        {
            using var f = new Fixture(); f.Coordinator.RegisterCallback(FluidCallbackKind.Generic, 42);
            var bytes = f.State.Save(); var nativePayload = Modification(3, 0, 0, 0, callback: 42, replacement: 1);
            using var batch = f.Coordinator.PrepareBatch(new[]
            {
                Message(ModifyCell, nativePayload), Message(EmitMass, Emission(3, 1, 0.5f, 300, -1))
            }, 1, 0.2);
            check(batch.Snapshot.SampleCell(3).Inventory.TotalMassKg == 0.5 && bytes.SequenceEqual(f.State.Save()));
            using (var competing = f.State.BeginTransaction()) reject(() => competing.Commit());
            reject(() => f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project));
            var intents = f.Coordinator.BeginNativeDispatch(batch);
            check(intents.Count == 1 && BinaryPrimitives.ReadInt32LittleEndian(intents[0].Payload.Slice(4)) == -1);
            check(BinaryPrimitives.ReadInt32LittleEndian(nativePayload.AsSpan(4)) == 42);
            reject(() => f.Coordinator.BeginNativeDispatch(batch));
            reject(() => f.Coordinator.AcknowledgeNative(batch, f.Source.Pointer));
            f.Source.Pointer->elementIdx[3] = 0; f.Coordinator.AcknowledgeNative(batch, f.Source.Pointer);
            reject(() => f.Coordinator.AcknowledgeNative(batch, f.Source.Pointer));
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project);
            var publication = f.Coordinator.Publish(batch);
            check(publication.NativeSnapshot.Pointer->elementIdx[3] == 1 && publication.NativeSnapshot.Pointer->numCallbackInfo == 1);
            check(!publication.Snapshot.SampleCell(3).IsSolid && bytes.SequenceEqual(ColonyFluidState.Restore(bytes, f.Catalog).Save()));
            f.Finish(publication);
        });
        test("overfilled displacement rejects the whole batch including earlier supply", () =>
        {
            using var f = new Fixture(); var before = f.State.Save();
            reject(() => f.Coordinator.PrepareBatch(new[]
            {
                Message(EmitMass, Emission(1, 2, 900, 300, -1)),
                new FluidBatchMessage(ModifyCell, Modification(0, 3, 1000, 300, replacement: 2), displacementCell: 1)
            }, 1, 0.2));
            check(before.SequenceEqual(f.State.Save()) && !f.State.CaptureSnapshot().SampleCell(0).IsSolid);
            using var replacement = f.State.BeginTransaction(); replacement.Transfer(0, 2, 0.25); replacement.Commit();
        });
        test("solid placement displaces every trace energy momentum disease and radiation before publication", () =>
        {
            using var f = new Fixture();
            using (var seed = f.State.BeginTransaction())
            {
                seed.Extract(0, 2, "replace-test-input");
                seed.Inject(0, f.Catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = 2, ["nitrogen"] = 1e-16 },
                    300, 3, 4, new Dictionary<int, double> { [1234] = 8 }, 7), "moving-trace-input"); seed.Commit();
            }
            var before = f.State.CaptureSnapshot(); var inventory = before.SampleCell(0).Inventory;
            using var batch = f.Coordinator.PrepareBatch(new[]
            { new FluidBatchMessage(ModifyCell, Modification(0, 3, 1000, 300, replacement: 2), 2) }, 1, 0.2);
            check(before.SampleCell(2).Inventory.TotalMassKg == 0 && batch.Snapshot.SampleCell(0).IsSolid);
            Same(check, batch.Snapshot.SampleCell(2).Inventory, inventory);
            f.Coordinator.BeginNativeDispatch(batch); f.Source.Pointer->elementIdx[0] = 3;
            f.Coordinator.AcknowledgeNative(batch, f.Source.Pointer); f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project);
            var publication = f.Coordinator.Publish(batch);
            Same(check, publication.Snapshot.Totals(), before.Totals());
            Same(check, publication.Snapshot.SuppliedInventory, before.SuppliedInventory);
            Same(check, publication.Snapshot.RemovedInventory, before.RemovedInventory);
            f.Finish(publication);
        });
        test("selective staged consumption retains trace and its exact conserved removed parcel", () =>
        {
            using var f = new Fixture();
            using (var seed = f.State.BeginTransaction())
            {
                seed.Inject(0, f.Catalog.CreateInventory(new Dictionary<string, double> { ["nitrogen"] = 1e-16 }, 300,
                    diseaseCounts: new Dictionary<int, double> { [1234] = 8 }, radiationQuantity: 4), "trace-seed"); seed.Commit();
            }
            var before = f.State.CaptureSnapshot().SampleCell(0).Inventory;
            using var batch = f.Coordinator.PrepareBatch(new[] { Message(ConsumeMass, Consumption(0, 1, 1, -1)) }, 1, 0.2);
            var remaining = batch.Snapshot.SampleCell(0).Inventory; var removed = batch.Effects[0].RemovedParcel;
            check(remaining.MassesKg["nitrogen"] == 1e-16 && !removed.MassesKg.ContainsKey("nitrogen") && removed.MassesKg["oxygen"] == 1);
            check(remaining.DiseaseCounts[1234] == 4 && removed.DiseaseCounts[1234] == 4 && removed.RadiationQuantity == 2);
            check(remaining.TotalEnergyJoules + removed.TotalEnergyJoules == before.TotalEnergyJoules);
            check(batch.Snapshot.RemovedInventory.TotalEnergyJoules == removed.TotalEnergyJoules);
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project); f.Finish(f.Coordinator.Publish(batch));
        });
        test("unregister and handle reuse invalidate old generation before dispatch without erasing new registration", () =>
        {
            using var f = new Fixture(); var old = f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 9);
            using var batch = f.Coordinator.PrepareBatch(new[] { Message(EmitMass, Emission(2, 1, 0.25f, 300, 9)) }, 1, 0.2);
            f.Coordinator.UnregisterCallback(old); var replacement = f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 9);
            check(replacement.Generation != old.Generation); reject(() => f.Coordinator.UnregisterCallback(old));
            reject(() => f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project)); batch.Dispose();
            using var next = f.Coordinator.PrepareBatch(new[] { Message(EmitMass, Emission(2, 1, 0.5f, 300, 9)) }, 1, 0.2);
            f.Coordinator.PrepareSnapshot(next, f.Source.Pointer, Project); f.Finish(f.Coordinator.Publish(next));
            check(f.Coordinator.RegistrationCount == 1);
        });
        test("retiring an unacknowledged callback faults consumption and prevents handle reuse until explicit resolution", () =>
        {
            using var f = new Fixture(); var lease = f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 9);
            using var batch = f.Coordinator.PrepareBatch(new[] { Message(EmitMass, Emission(2, 1, 0.25f, 300, 9)) }, 1, 0.2);
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project); var publication = f.Coordinator.Publish(batch);
            f.Coordinator.UnregisterCallback(lease); check(f.Coordinator.ConsumptionFault.Length != 0);
            reject(() => f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 9));
            reject(() => f.Coordinator.BeginConsumption(publication.Ticket)); reject(() => f.Coordinator.AcknowledgeConsumption(publication.Ticket));
            f.Coordinator.ResolveConsumptionFault(publication.Ticket, "Caller settled retired callback without invoking its former actor.");
            var replacement = f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 9);
            check(replacement.Generation != lease.Generation && f.State.CaptureSnapshot().SampleCell(2).Inventory.TotalMassKg == 0.25);
        });
        test("callback side effect followed by throw remains ambiguous and is never automatically replayed", () =>
        {
            using var f = new Fixture(); f.Coordinator.RegisterCallback(FluidCallbackKind.Generic, 1);
            using var batch = f.Coordinator.PrepareBatch(new[] { Message(ModifyCell, Modification(2, 1, 0.25f, 300, callback: 1)) }, 1, 0.2);
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project); var publication = f.Coordinator.Publish(batch);
            var calls = 0; f.Coordinator.BeginConsumption(publication.Ticket);
            try { ++calls; throw new InvalidOperationException("Actor changed storage then failed."); }
            catch (InvalidOperationException error) { f.Coordinator.ReportConsumptionFault(publication.Ticket, error.Message); }
            reject(() => f.Coordinator.BeginConsumption(publication.Ticket)); reject(() => f.Coordinator.AcknowledgeConsumption(publication.Ticket));
            reject(batch.Dispose); reject(() => f.Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), 2, 0.4));
            using (var tx = f.State.BeginTransaction()) reject(() => tx.Commit());
            check(calls == 1 && f.Coordinator.HasUnacknowledgedPublication);
            f.Coordinator.ResolveConsumptionFault(publication.Ticket, "Actor outcome inspected and accepted once.");
            check(calls == 1 && f.Coordinator.RegistrationCount == 0);
            reject(() => f.Coordinator.ResolveConsumptionFault(publication.Ticket, "Repeated acknowledgement"));
        });
        test("publication and consumption acknowledgements are session sequenced and reject duplicates", () =>
        {
            using var f = new Fixture(); using var other = new Fixture();
            using var batch = f.Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), 1, 0.2);
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project); var publication = f.Coordinator.Publish(batch);
            reject(() => f.Coordinator.Publish(batch)); reject(() => other.Coordinator.BeginConsumption(publication.Ticket));
            reject(() => f.Coordinator.AcknowledgeConsumption(publication.Ticket));
            f.Coordinator.BeginConsumption(publication.Ticket); reject(() => f.Coordinator.BeginConsumption(publication.Ticket));
            f.Coordinator.AcknowledgeConsumption(publication.Ticket); reject(() => f.Coordinator.AcknowledgeConsumption(publication.Ticket));
            using var next = f.Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), 2, 0.4);
            f.Coordinator.PrepareSnapshot(next, f.Source.Pointer, Project); var second = f.Coordinator.Publish(next);
            check(second.Ticket.Session == publication.Ticket.Session && second.Ticket.Sequence == publication.Ticket.Sequence + 1);
            reject(() => f.Coordinator.BeginConsumption(publication.Ticket)); f.Finish(second);
        });
        test("late malformed snapshot and injected allocation failure retain old pointer and permit one clean retry", () =>
        {
            using var f = new Fixture(); var first = f.PublishEmpty(1); f.Finish(first);
            var pointer = (IntPtr)first.NativeSnapshot.Pointer; var before = f.State.Save();
            f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 7);
            using var batch = f.Coordinator.PrepareBatch(new[] { Message(EmitMass, Emission(2, 1, 0.5f, 300, 7)) }, 2, 0.4);
            var count = f.Source.Pointer->numRadiationConsumedCallbacks; f.Source.Pointer->numRadiationConsumedCallbacks = -1;
            reject(() => f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project)); f.Source.Pointer->numRadiationConsumedCallbacks = count;
            OwnedSnapshotOverlay? retained = null; var failed = false;
            try
            {
                f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, (overlay, _) =>
                { retained = overlay; overlay.Masses[2] = 123; throw new OutOfMemoryException("Injected failure after owned buffer allocation."); });
            }
            catch (OutOfMemoryException) { failed = true; }
            check(failed && batch.Candidate == null && before.SequenceEqual(f.State.Save()));
            check((IntPtr)f.Coordinator.Current!.NativeSnapshot.Pointer == pointer && first.NativeSnapshot.Pointer->mass[2] == 0);
            reject(() => { _ = retained!.Masses.Length; });
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project); var next = f.Coordinator.Publish(batch);
            check(next.NativeSnapshot.Pointer->mass[2] == 0.5f && next.NativeSnapshot.Pointer->numMassEmittedCallbacks == 1);
            reject(() => { _ = first.NativeSnapshot.Pointer; }); f.Finish(next);
        });
        test("native or projector callback overlap rejects unpublished snapshot without duplicating outbox on retry", () =>
        {
            using var f = new Fixture(); f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 8);
            using var batch = f.Coordinator.PrepareBatch(new[] { Message(EmitMass, Emission(2, 1, 0.5f, 300, 8)) }, 1, 0.2);
            f.Source.Pointer->numMassEmittedCallbacks = 1; f.Source.Pointer->massEmittedCallbacks[0].callbackIdx = 8;
            reject(() => f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project)); f.Source.Pointer->numMassEmittedCallbacks = 0;
            reject(() => f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, (overlay, snapshot) =>
            {
                Project(overlay, snapshot); overlay.AppendEvents<Sim.MassEmittedCallback>(SnapshotEventChannel.MassEmittedCallbacks,
                    new[] { new Sim.MassEmittedCallback { callbackIdx = 8 } });
            }));
            check(f.State.CaptureSnapshot().Revision == 0 && batch.Candidate == null);
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project); var publication = f.Coordinator.Publish(batch);
            check(publication.NativeSnapshot.Pointer->numMassEmittedCallbacks == 1); f.Finish(publication);
        });
        test("consumed pointer survives acknowledgement and failed next candidate until next successful publication", () =>
        {
            using var f = new Fixture(); var first = f.PublishEmpty(1); f.Finish(first);
            var pointer = (IntPtr)first.NativeSnapshot.Pointer;
            using (var abandoned = f.Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), 2, 0.4))
            { f.Coordinator.PrepareSnapshot(abandoned, f.Source.Pointer, Project); }
            check((IntPtr)first.NativeSnapshot.Pointer == pointer && ReferenceEquals(f.Coordinator.Current, first));
            var next = f.PublishEmpty(2); reject(() => { _ = first.NativeSnapshot.Pointer; }); f.Finish(next);
            f.Coordinator.Dispose(); reject(() => { _ = next.NativeSnapshot.Pointer; });
        });
        test("native cancellation or partial failure requires external recovery before reservation can release", () =>
        {
            using var f = new Fixture(); var before = f.State.Save();
            using var batch = f.Coordinator.PrepareBatch(new[] { Message(ModifyCell, Modification(2, 3, 1000, 300, replacement: 1)) }, 1, 0.2);
            f.Coordinator.BeginNativeDispatch(batch); f.Coordinator.ReportNativeFailure(batch, "Native operation may have partly completed.");
            check(f.Coordinator.NativeRecoveryRequired.Length != 0 && before.SequenceEqual(f.State.Save()));
            reject(() => f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project));
            using (var tx = f.State.BeginTransaction()) reject(() => tx.Commit());
            batch.Dispose(); check(!batch.Closed);
            f.Coordinator.AcknowledgeNativeRecovery(batch, "Caller restored the native checkpoint at its synchronization boundary.");
            check(batch.Closed && f.Coordinator.NativeRecoveryRequired.Length == 0 && before.SequenceEqual(f.State.Save()));
            reject(() => f.Coordinator.AcknowledgeNativeRecovery(batch, "Duplicate recovery"));
            using var next = f.State.BeginTransaction(); next.Commit();
        });
        test("ordered repeated native geometry checks the final state and still requires every intent acknowledgement", () =>
        {
            using var f = new Fixture();
            using var batch = f.Coordinator.PrepareBatch(new[]
            {
                Message(ModifyCell, Modification(2, 3, 1000, 300, replacement: 1)),
                Message(ModifyCell, Modification(2, 0, 0, 0, replacement: 1)), Message(ModifyEnergy, Heat(3, 1, 400))
            }, 1, 0.2);
            var operations = f.Coordinator.BeginNativeDispatch(batch);
            check(operations.Count == 3 && operations[0].ExpectedElementIndex == 3 && operations[1].ExpectedElementIndex == 0 && !operations[2].ExpectedElementIndex.HasValue);
            f.Coordinator.AcknowledgeNative(batch, f.Source.Pointer); f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project);
            var publication = f.Coordinator.Publish(batch); check(!publication.Snapshot.SampleCell(2).IsSolid); f.Finish(publication);
        });
        test("callback retirement after native dispatch requires recovery and cannot reuse the old prepared generation", () =>
        {
            using var f = new Fixture(); var old = f.Coordinator.RegisterCallback(FluidCallbackKind.Generic, 42);
            using var batch = f.Coordinator.PrepareBatch(new[] { Message(ModifyCell, Modification(2, 3, 1000, 300, 42, 1)) }, 1, 0.2);
            f.Coordinator.BeginNativeDispatch(batch); f.Coordinator.UnregisterCallback(old);
            check(f.Coordinator.NativeRecoveryRequired.Length != 0);
            reject(() => f.Coordinator.RegisterCallback(FluidCallbackKind.Generic, 42));
            reject(() => f.Coordinator.AcknowledgeNative(batch, f.Source.Pointer));
            f.Coordinator.AcknowledgeNativeRecovery(batch, "Native world restored before reusing actor callback handle.");
            var replacement = f.Coordinator.RegisterCallback(FluidCallbackKind.Generic, 42);
            check(replacement.Generation != old.Generation && f.State.CaptureSnapshot().Revision == 0);
        });
        test("one shot callback duplication rejects while retained emitter records preserve message order", () =>
        {
            using var f = new Fixture(); f.Coordinator.RegisterCallback(FluidCallbackKind.Generic, 10);
            reject(() => f.Coordinator.PrepareBatch(new[]
            {
                Message(ModifyCell, Modification(2, 1, 0.25f, 300, 10)), Message(ModifyCell, Modification(2, 1, 0.5f, 300, 10))
            }, 1, 0.2));
            check(f.State.CaptureSnapshot().Revision == 0);
            f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 11);
            using var batch = f.Coordinator.PrepareBatch(new[]
            {
                Message(EmitMass, Emission(2, 1, 0.25f, 300, 11)), Message(EmitMass, Emission(2, 1, 0.5f, 300, 11))
            }, 1, 0.2);
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project); var publication = f.Coordinator.Publish(batch);
            check(publication.NativeSnapshot.Pointer->numMassEmittedCallbacks == 2 && publication.NativeSnapshot.Pointer->massEmittedCallbacks[0].mass == 0.25f && publication.NativeSnapshot.Pointer->massEmittedCallbacks[1].mass == 0.5f);
            f.Finish(publication); check(f.Coordinator.RegistrationCount == 2);
        });
        test("batch and payload limits reject without retaining partially prepared work", () =>
        {
            using var f = new Fixture(); var before = f.State.Save();
            reject(() => new FluidBatchMessage(EmitMass, new byte[4097]));
            var noChange = Message(ModifyCell, Modification(2, 0, 0, 0));
            reject(() => f.Coordinator.PrepareBatch(Enumerable.Repeat(noChange, 4097), 1, 0.2));
            check(before.SequenceEqual(f.State.Save()));
            using var batch = f.Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), 1, 0.2);
            reject(() => f.Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), 1, 0.2));
        });
        test("snapshot projection reads authority without holding its publication lock", () =>
        {
            using var f = new Fixture(); using var batch = f.Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), 1, 0.2);
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, (overlay, snapshot) =>
            {
                var read = Task.Run(f.State.CaptureSnapshot); check(read.Wait(TimeSpan.FromSeconds(5)) && read.Result.Revision == 0);
                Project(overlay, snapshot);
            });
            f.Finish(f.Coordinator.Publish(batch));
        });
        test("a competing post-consumption publisher invalidates stale coordinator snapshot identity", () =>
        {
            using var f = new Fixture(); var publication = f.PublishEmpty(1); f.Finish(publication);
            using (var tx = f.State.BeginTransaction())
            {
                tx.Transfer(0, 2, 0.25); using var prepared = tx.PrepareFrame(2, 0.4);
                // Even copying the old companion cannot disguise a different revision.
                prepared.Commit(publication);
            }
            reject(() => { _ = f.Coordinator.Current; });
            reject(() => f.Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), 3, 0.6));
            check(publication.NativeSnapshot.Pointer->mass[2] == 0 && f.State.CaptureSnapshot().SampleCell(2).Inventory.TotalMassKg == 0.25);
        });
        test("reentrant projection cancellation and recursive capture cannot retain an orphan candidate", () =>
        {
            using var f = new Fixture(); using var batch = f.Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), 1, 0.2);
            reject(() => f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, (_, _) =>
                f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project)));
            check(batch.Candidate == null && !batch.Closed);
            reject(() => f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, (_, _) => batch.Dispose()));
            check(batch.Candidate == null && batch.Closed && f.State.CaptureSnapshot().Revision == 0);
            var publication = f.PublishEmpty(1); f.Finish(publication);
        });
    }

    internal static void Benchmark()
    {
        // This measures only the bounded protocol on four synthetic cells. It
        // does not time native dispatch, actors, a fluid timestep or colony copy.
        const int pairs = 512, frames = 64;
        using var f = new Fixture(); f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 17);
        var messages = new FluidBatchMessage[pairs * 2];
        for (var i = 0; i < pairs; ++i)
        {
            messages[i * 2] = Message(EmitMass, Emission(0, 1, 0.125f, 300, 17));
            messages[i * 2 + 1] = Message(ConsumeMass, Consumption(0, 1, 0.125f, 1000 + i));
        }
        void RunFrame(long frame)
        {
            for (var i = 0; i < pairs; ++i) f.Coordinator.RegisterCallback(FluidCallbackKind.Consumed, 1000 + i);
            using var batch = f.Coordinator.PrepareBatch(messages, frame, frame * 0.2);
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project); var publication = f.Coordinator.Publish(batch);
            if (publication.NativeSnapshot.Pointer->numMassConsumedCallbacks != pairs || publication.NativeSnapshot.Pointer->numMassEmittedCallbacks != pairs)
                throw new InvalidOperationException("Benchmark lost callback records.");
            f.Finish(publication);
        }
        RunFrame(1); GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        var allocated = GC.GetAllocatedBytesForCurrentThread(); var timer = Stopwatch.StartNew();
        for (var frame = 2; frame < frames + 2; ++frame) RunFrame(frame);
        timer.Stop(); allocated = GC.GetAllocatedBytesForCurrentThread() - allocated;
        var snapshot = f.State.CaptureSnapshot();
        if (snapshot.SampleCell(0).Inventory.TotalMassKg != 2 || f.Coordinator.RegistrationCount != 1 || f.Coordinator.HasUnacknowledgedPublication)
            throw new InvalidOperationException("Benchmark leaked inventory or callback registrations.");
        Console.WriteLine("FRAME_COORDINATOR_BENCHMARK " + JsonSerializer.Serialize(new
        {
            Scenario = "synthetic 4-cell protocol; registration+stage+owned copy+paired publish+explicit ack; no native/actor/physics execution",
            Frames = frames, MessagesPerFrame = messages.Length, CallbackRecordsPerFrame = pairs * 2,
            MeanMillisecondsPerFrame = timer.Elapsed.TotalMilliseconds / frames,
            ManagedBytesPerFrame = allocated / frames,
            LastOwnedNativeBytes = f.Coordinator.Current!.NativeSnapshot.CapturedBytes,
            RetainedRegistrations = f.Coordinator.RegistrationCount,
            RetainedPublicationCount = 1
        }));
    }

    private static FluidBatchMessage Message(int message, byte[] payload) => new FluidBatchMessage(message, payload);
    private static void Same(Action<bool> check, FluidInventory actual, FluidInventory expected)
    {
        check(actual.MassesKg.SequenceEqual(expected.MassesKg) && actual.DiseaseCounts.SequenceEqual(expected.DiseaseCounts));
        check(actual.TotalEnergyJoules == expected.TotalEnergyJoules && actual.MomentumX == expected.MomentumX && actual.MomentumY == expected.MomentumY && actual.RadiationQuantity == expected.RadiationQuantity);
    }

    private static void Project(OwnedSnapshotOverlay overlay, ColonyFrameSnapshot snapshot)
    {
        for (var cell = 0; cell < 4; ++cell)
        {
            var sample = snapshot.SampleCell(cell); if (sample.IsSolid) continue;
            overlay.Masses[cell] = (float)sample.Inventory.TotalMassKg; overlay.Temperatures[cell] = (float)sample.TemperatureKelvin;
            overlay.Elements[cell] = sample.DominantMaterialId switch { "oxygen" => 1, "water" => 2, "nitrogen" => 4, _ => 0 };
        }
    }

    private sealed class Fixture : IDisposable
    {
        internal ColonyFluidState State { get; }
        internal FluidMaterialCatalog Catalog { get; }
        internal ExperimentalFluidMessageRouter Router { get; }
        internal ExperimentalFluidFrameCoordinator Coordinator { get; }
        internal Program.NativeFixture Source { get; } = new Program.NativeFixture();
        internal Fixture()
        {
            (State, Catalog, Router) = Create(); Coordinator = new ExperimentalFluidFrameCoordinator(State, Router);
            Source.Pointer->numCallbackInfo = Source.Pointer->numMassConsumedCallbacks = Source.Pointer->numMassEmittedCallbacks = 0;
            Source.Pointer->elementIdx[0] = 1; Source.Pointer->elementIdx[1] = 2; Source.Pointer->elementIdx[2] = 0; Source.Pointer->elementIdx[3] = 3;
        }
        internal FluidFramePublication PublishEmpty(long frame)
        {
            var batch = Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), frame, frame * 0.2);
            Coordinator.PrepareSnapshot(batch, Source.Pointer, Project); return Coordinator.Publish(batch);
        }
        internal void Finish(FluidFramePublication publication) { Coordinator.BeginConsumption(publication.Ticket); Coordinator.AcknowledgeConsumption(publication.Ticket); }
        public void Dispose() { Coordinator.Dispose(); Source.Dispose(); }
    }
}
