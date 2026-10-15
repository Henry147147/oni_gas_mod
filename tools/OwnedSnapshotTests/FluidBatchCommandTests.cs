using System;
using System.Collections.Generic;
using System.Linq;
using MixedAtmospheres.Core;
using MixedAtmospheres.Oni;
using static FluidRouterTests;

internal static unsafe partial class FluidFrameCoordinatorTests
{
    private static void RunCommandTests(Action<string, System.Action> test, Action<bool> check, Action<System.Action> reject)
    {
        test("closed commands interleave native geometry, supply, container moves and consumption in one ordered frame", () =>
        {
            using var f = new Fixture(); var handle = AddContainer(f, "buffer");
            var before = f.State.CaptureSnapshot(); var bytes = f.State.Save();
            f.Coordinator.RegisterCallback(FluidCallbackKind.Generic, 42);
            f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 17);
            f.Coordinator.RegisterCallback(FluidCallbackKind.Consumed, 18);
            using var batch = f.Coordinator.PrepareCommands(new[]
            {
                Native(ModifyCell, Modification(3, 0, 0, 0, 42, 1)),
                Native(EmitMass, Emission(3, 1, .5f, 300, 17, 0, 4)),
                FluidBatchCommand.AllCellToContainer(3, handle),
                FluidBatchCommand.ContainerToCell(handle, 3, "oxygen", .125),
                Native(ConsumeMass, Consumption(3, 1, .125f, 18)),
                FluidBatchCommand.AllContainerToCell(handle, 2)
            }, 1, .2);
            check(batch.CommandCount == 6 && batch.Effects.Count == 3 && batch.NativeOperations.Count == 1);
            check(bytes.SequenceEqual(f.State.Save()) && before.SampleCell(3).IsSolid && !batch.Snapshot.SampleCell(3).IsSolid);
            check(Empty(batch.Snapshot.SampleContainer(handle).Inventory) && Empty(batch.Snapshot.SampleCell(3).Inventory));
            check(batch.Snapshot.SampleCell(2).Inventory.MassesKg["oxygen"] == .375 && batch.Snapshot.SampleCell(2).Inventory.DiseaseCounts[1234] == 3);
            check(batch.Snapshot.SuppliedInventory.MassesKg["oxygen"] == .5 && batch.Snapshot.RemovedInventory.MassesKg["oxygen"] == .125);
            check(batch.Snapshot.SuppliedInventory.DiseaseCounts[1234] == 4 && batch.Snapshot.RemovedInventory.DiseaseCounts[1234] == 1);
            var model = CommandModel(f.Catalog); f.Coordinator.PreparePresentation(batch, model);
            var prepared = batch.Presentation!;
            reject(() => f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project));
            f.Coordinator.BeginNativeDispatch(batch); f.Source.Pointer->elementIdx[3] = 0;
            f.Coordinator.AcknowledgeNative(batch, f.Source.Pointer); f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project);
            var publication = f.Coordinator.Publish(batch);
            check(ReferenceEquals(publication.Presentation, prepared) && ReferenceEquals(publication.PresentationModel, model));
            check(publication.Presentation!.ColonyId == publication.Snapshot.ColonyId && publication.Presentation.Revision == publication.Snapshot.Revision);
            check(publication.NativeSnapshot.Pointer->mass[2] == .375f && publication.NativeSnapshot.Pointer->mass[3] == 0);
            check(publication.NativeSnapshot.Pointer->numCallbackInfo == 1 && publication.NativeSnapshot.Pointer->numMassEmittedCallbacks == 1 &&
                publication.NativeSnapshot.Pointer->numMassConsumedCallbacks == 1 && publication.NativeSnapshot.Pointer->massConsumedCallbacks[0].diseaseCount == 1);
            check(ReferenceEquals(f.State.CapturePublication().Companion, publication)); f.Finish(publication);
        });
        test("all nine internal command forms preserve whole-colony inventories and external ledgers", () =>
        {
            using var f = new Fixture(); var a = AddContainer(f, "a"); var b = AddContainer(f, "b");
            using (var seed = f.State.BeginTransaction())
            {
                seed.Extract(0, 2, "replace-input");
                seed.Inject(0, f.Catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = 2, ["nitrogen"] = 1e-16 },
                    300, 3, 4, new Dictionary<int, double> { [1234] = 7.25 }, 11.5), "moving-input"); seed.Commit();
            }
            var before = f.State.CaptureSnapshot(); var original = before.SampleCell(0).Inventory;
            using var batch = f.Coordinator.PrepareCommands(new[]
            {
                FluidBatchCommand.CellToContainer(0, a, .5), FluidBatchCommand.CellToContainer(0, a, "oxygen", .25), FluidBatchCommand.AllCellToContainer(0, a),
                FluidBatchCommand.ContainerToContainer(a, b, .5), FluidBatchCommand.ContainerToContainer(a, b, "oxygen", .25), FluidBatchCommand.AllContainerToContainer(a, b),
                FluidBatchCommand.ContainerToCell(b, 2, .5), FluidBatchCommand.ContainerToCell(b, 2, "oxygen", .25), FluidBatchCommand.AllContainerToCell(b, 2)
            }, 1, .2);
            check(batch.CommandCount == 9 && batch.Effects.Count == 0 && batch.NativeOperations.Count == 0);
            check(Empty(batch.Snapshot.SampleCell(0).Inventory) && Empty(batch.Snapshot.SampleContainer(a).Inventory) && Empty(batch.Snapshot.SampleContainer(b).Inventory));
            Conserved(check, batch.Snapshot.SampleCell(2).Inventory, original); Conserved(check, batch.Snapshot.Totals(), before.Totals());
            Same(check, batch.Snapshot.SuppliedInventory, before.SuppliedInventory); Same(check, batch.Snapshot.RemovedInventory, before.RemovedInventory);
            check(batch.Snapshot.ExternalHeatJoules == before.ExternalHeatJoules && Empty(before.SampleCell(2).Inventory));
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project); f.Finish(f.Coordinator.Publish(batch));
        });
        test("all commands retain minimum positive traces and exact moving metadata without a source sink", () =>
        {
            using var f = new Fixture(); var a = AddContainer(f, "a"); var b = AddContainer(f, "b");
            using (var seed = f.State.BeginTransaction())
            {
                seed.Extract(0, 2, "replace-input");
                seed.Inject(0, f.Catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = 2, ["nitrogen"] = double.Epsilon },
                    300, 3, 4, new Dictionary<int, double> { [1234] = 7.25 }, 11.5), "trace-input"); seed.Commit();
            }
            var before = f.State.CaptureSnapshot();
            using var batch = f.Coordinator.PrepareCommands(new[] { FluidBatchCommand.AllCellToContainer(0, a),
                FluidBatchCommand.AllContainerToContainer(a, b), FluidBatchCommand.AllContainerToCell(b, 2) }, 1, .2);
            Same(check, batch.Snapshot.SampleCell(2).Inventory, before.SampleCell(0).Inventory);
            check(batch.Snapshot.SampleCell(2).Inventory.MassesKg["nitrogen"] == double.Epsilon);
            Same(check, batch.Snapshot.SuppliedInventory, before.SuppliedInventory); Same(check, batch.Snapshot.RemovedInventory, before.RemovedInventory);
        });
        test("all commands evacuate massless disease and radiation exactly", () =>
        {
            using var f = new Fixture(); var a = AddContainer(f, "a"); var b = AddContainer(f, "b");
            using (var seed = f.State.BeginTransaction())
            {
                seed.Inject(2, new FluidInventory(new Dictionary<string, double>(), 0, diseaseCounts: new Dictionary<int, double> { [1234] = .25 },
                    radiationQuantity: 1.5), "metadata-input"); seed.Commit();
            }
            var before = f.State.CaptureSnapshot();
            using var batch = f.Coordinator.PrepareCommands(new[] { FluidBatchCommand.AllCellToContainer(2, a),
                FluidBatchCommand.AllContainerToContainer(a, b), FluidBatchCommand.AllContainerToCell(b, 2) }, 1, .2);
            Same(check, batch.Snapshot.SampleCell(2).Inventory, before.SampleCell(2).Inventory);
            check(Empty(batch.Snapshot.SampleContainer(a).Inventory) && Empty(batch.Snapshot.SampleContainer(b).Inventory));
            Same(check, batch.Snapshot.Totals(), before.Totals()); Same(check, batch.Snapshot.SuppliedInventory, before.SuppliedInventory);
        });
        test("late stale foreign and default handles discard earlier native world and container staging", () =>
        {
            using var f = new Fixture(); using var foreign = new Fixture(); var stale = AddContainer(f, "reused");
            using (var retire = f.State.BeginTransaction()) { retire.RetireContainer(stale); retire.Commit(); }
            var replacement = AddContainer(f, "reused"); var target = AddContainer(f, "target"); var other = AddContainer(foreign, "foreign");
            f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 17); var before = f.State.Save();
            foreach (var invalid in new[] { stale, other, default(FluidContainerHandle) })
            {
                reject(() => f.Coordinator.PrepareCommands(new[] { Native(ModifyCell, Modification(3, 0, 0, 0, replacement: 1)),
                    Native(EmitMass, Emission(2, 1, .5f, 300, 17)), FluidBatchCommand.AllCellToContainer(2, target),
                    FluidBatchCommand.ContainerToContainer(target, invalid, .25) }, 1, .2));
                check(before.SequenceEqual(f.State.Save()) && f.Coordinator.Current == null && f.Coordinator.RegistrationCount == 1);
            }
            using var batch = f.Coordinator.PrepareCommands(new[] { FluidBatchCommand.CellToContainer(0, replacement, .25) }, 1, .2);
            check(batch.NativeOperations.Count == 0 && batch.Snapshot.SampleCell(3).IsSolid && Empty(batch.Snapshot.SampleContainer(target).Inventory));
        });
        test("capacity forbidden phase and volume failures roll back every preceding command", () =>
        {
            using var f = new Fixture(); var good = AddContainer(f, "good");
            var capacity = AddContainer(f, "capacity", new FluidContainerDefinition(1, .125, FluidContainerPhases.Gas));
            var phase = AddContainer(f, "phase", new FluidContainerDefinition(1, 1000, FluidContainerPhases.Liquid));
            var volume = AddContainer(f, "volume", new FluidContainerDefinition(.01, 1000, FluidContainerPhases.Liquid));
            var before = f.State.Save();
            foreach (var destination in new[] { capacity, phase, volume })
            {
                var failingCell = destination.Equals(volume) ? 1 : 2;
                reject(() => f.Coordinator.PrepareCommands(new[] { FluidBatchCommand.CellToContainer(0, good, .25),
                    Native(ModifyCell, Modification(3, 0, 0, 0, replacement: 1)), Native(EmitMass, Emission(2, 1, .5f, 300, -1)),
                    FluidBatchCommand.AllCellToContainer(failingCell, destination) }, 1, .2));
                check(before.SequenceEqual(f.State.Save()) && f.Coordinator.Current == null && f.State.CaptureSnapshot().SampleCell(3).IsSolid);
            }
        });
        test("late malformed native payload rolls back successful container transfers", () =>
        {
            using var f = new Fixture(); var handle = AddContainer(f, "a"); var before = f.State.Save();
            reject(() => f.Coordinator.PrepareCommands(new[] { FluidBatchCommand.AllCellToContainer(0, handle),
                FluidBatchCommand.ContainerToCell(handle, 2, .5), Native(EmitMass, new byte[23]) }, 1, .2));
            check(before.SequenceEqual(f.State.Save()));
            using var batch = f.Coordinator.PrepareCommands(Array.Empty<FluidBatchCommand>(), 1, .2);
            check(batch.Snapshot.SampleCell(0).Inventory.TotalMassKg == 2 && Empty(batch.Snapshot.SampleContainer(handle).Inventory));
        });
        test("presentation preparation fails before publication and retries the same container frame through snapshot allocation failure", () =>
        {
            using var f = new Fixture(); var handle = AddContainer(f, "a"); var old = f.PublishEmpty(1); f.Finish(old);
            var oldPointer = (IntPtr)old.NativeSnapshot.Pointer; var oldPair = f.State.CapturePublication(); var bytes = f.State.Save();
            f.Coordinator.RegisterCallback(FluidCallbackKind.Emitted, 17);
            using var batch = f.Coordinator.PrepareCommands(new[] { Native(EmitMass, Emission(2, 1, .5f, 300, 17)),
                FluidBatchCommand.AllCellToContainer(2, handle), FluidBatchCommand.ContainerToCell(handle, 0, "oxygen", .125) }, 2, .4);
            var wrong = CommandModel(new FluidMaterialCatalog("incompatible-command-test", f.Catalog.Materials.Values));
            reject(() => f.Coordinator.PreparePresentation(batch, wrong));
            check(batch.Presentation == null && batch.PresentationModel == null && ReferenceEquals(f.State.CapturePublication(), oldPair));
            var model = CommandModel(f.Catalog); f.Coordinator.PreparePresentation(batch, model); var view = batch.Presentation!;
            reject(() => f.Coordinator.PreparePresentation(batch, model));
            check(view.SampleContainer(handle).TotalMassKg == .375 && view.SampleCell(0).TotalMassKg == 2.125 && bytes.SequenceEqual(f.State.Save()));
            var failed = false;
            try { f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, (_, _) => throw new OutOfMemoryException("Injected merged snapshot allocation failure.")); }
            catch (OutOfMemoryException) { failed = true; }
            check(failed);
            check(batch.Candidate == null && ReferenceEquals(batch.Presentation, view) && ReferenceEquals(f.State.CapturePublication(), oldPair));
            check((IntPtr)old.NativeSnapshot.Pointer == oldPointer);
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project); var publication = f.Coordinator.Publish(batch);
            check(ReferenceEquals(publication.Presentation, view) && ReferenceEquals(publication.PresentationModel, model));
            check(ReferenceEquals(publication.PreparedPresentation, batch.PreparedPresentation) && ReferenceEquals(publication.PreparedPresentation!.Source, publication.Snapshot));
            var pair = f.State.CapturePublication(); check(ReferenceEquals(pair.Companion, publication) && ReferenceEquals(pair.Snapshot, publication.Snapshot));
            check(view.ColonyId == pair.Snapshot.ColonyId && view.Revision == pair.Snapshot.Revision && view.CompletedFrame == pair.Snapshot.CompletedFrame &&
                view.MaterialModelFingerprint == pair.Snapshot.MaterialModelFingerprint && view.MaterialModelVersion == pair.Snapshot.MaterialModelVersion);
            check(publication.NativeSnapshot.Pointer->mass[0] == 2.125f && publication.NativeSnapshot.Pointer->numMassEmittedCallbacks == 1);
            reject(() => f.Coordinator.Publish(batch)); reject(() => { _ = old.NativeSnapshot.Pointer; });
            using (var competing = f.State.BeginTransaction()) reject(() => competing.Commit());
            f.Coordinator.BeginConsumption(publication.Ticket); reject(() => f.Coordinator.BeginConsumption(publication.Ticket));
            f.Coordinator.AcknowledgeConsumption(publication.Ticket); reject(() => f.Coordinator.AcknowledgeConsumption(publication.Ticket));
            check(!f.Coordinator.HasUnacknowledgedPublication && view.SampleContainer(handle).TotalMassKg == .375);
        });
        test("presentation must prepare before native dispatch while legacy publication stays optional", () =>
        {
            using var f = new Fixture();
            using var batch = f.Coordinator.PrepareCommands(new[] { Native(ModifyCell, Modification(3, 0, 0, 0, replacement: 1)) }, 1, .2);
            f.Coordinator.BeginNativeDispatch(batch); reject(() => f.Coordinator.PreparePresentation(batch, CommandModel(f.Catalog)));
            check(batch.Presentation == null); f.Source.Pointer->elementIdx[3] = 0; f.Coordinator.AcknowledgeNative(batch, f.Source.Pointer);
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project); var publication = f.Coordinator.Publish(batch);
            check(publication.Presentation == null && publication.PresentationModel == null); f.Finish(publication);
        });
        test("the 4096 command limit counts native messages and every internal move direction", () =>
        {
            using var f = new Fixture(); var a = AddContainer(f, "a"); var b = AddContainer(f, "b");
            var commands = new[] { Native(ModifyCell, Modification(2, 0, 0, 0)), FluidBatchCommand.AllCellToContainer(2, a),
                FluidBatchCommand.AllContainerToContainer(a, b), FluidBatchCommand.AllContainerToCell(b, 2) };
            var before = f.State.Save();
            reject(() => f.Coordinator.PrepareCommands(Enumerable.Range(0, 4097).Select(i => commands[i % commands.Length]), 1, .2));
            check(before.SequenceEqual(f.State.Save()));
            using var batch = f.Coordinator.PrepareCommands(Enumerable.Range(0, 4096).Select(i => commands[i % commands.Length]), 1, .2);
            check(batch.CommandCount == 4096 && batch.Effects.Count == 1024 && batch.NativeOperations.Count == 0);
            Same(check, batch.Snapshot.Totals(), f.State.CaptureSnapshot().Totals());
            f.Coordinator.PrepareSnapshot(batch, f.Source.Pointer, Project); f.Finish(f.Coordinator.Publish(batch));
        });
        test("null commands and throwing enumeration leave no partially prepared frame", () =>
        {
            using var f = new Fixture(); var handle = AddContainer(f, "a"); var before = f.State.Save();
            reject(() => FluidBatchCommand.NativeMessage(null!)); reject(() => f.Coordinator.PrepareCommands(null!, 1, .2));
            reject(() => f.Coordinator.PrepareCommands(new[] { FluidBatchCommand.AllCellToContainer(0, handle), null! }, 1, .2));
            IEnumerable<FluidBatchCommand> Throwing()
            { yield return FluidBatchCommand.AllCellToContainer(0, handle); throw new InvalidOperationException("Producer failed after staging."); }
            reject(() => f.Coordinator.PrepareCommands(Throwing(), 1, .2)); check(before.SequenceEqual(f.State.Save()));
            using var batch = f.Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), 1, .2); check(batch.CommandCount == 0);
        });
        test("command enumeration cannot recursively prepare or leak a reservation after session disposal", () =>
        {
            using var f = new Fixture(); var before = f.State.Save();
            IEnumerable<FluidBatchCommand> Reentrant()
            {
                reject(() => f.Coordinator.PrepareBatch(Array.Empty<FluidBatchMessage>(), 1, .2));
                yield return Native(ModifyCell, Modification(2, 0, 0, 0));
            }
            using (var batch = f.Coordinator.PrepareCommands(Reentrant(), 1, .2)) check(batch.CommandCount == 1);
            IEnumerable<FluidBatchCommand> Disposing()
            {
                yield return Native(EmitMass, Emission(2, 1, .5f, 300, -1)); f.Coordinator.Dispose();
            }
            reject(() => f.Coordinator.PrepareCommands(Disposing(), 1, .2)); check(before.SequenceEqual(f.State.Save()));
            using var transaction = f.State.BeginTransaction(); transaction.Commit();
        });
    }

    private static FluidBatchCommand Native(int message, byte[] payload) => FluidBatchCommand.NativeMessage(new FluidBatchMessage(message, payload));
    private static bool Empty(FluidInventory inventory) => inventory.MassesKg.Count == 0 && inventory.DiseaseCounts.Count == 0 &&
        inventory.RadiationQuantity == 0 && inventory.TotalEnergyJoules == 0 && inventory.MomentumX == 0 && inventory.MomentumY == 0;
    private static FluidContainerHandle AddContainer(Fixture f, string id, FluidContainerDefinition? definition = null)
    {
        using var tx = f.State.BeginTransaction();
        var handle = tx.CreateContainer(id, definition ?? new FluidContainerDefinition(1, 1000, FluidContainerPhases.Gas | FluidContainerPhases.Liquid));
        tx.Commit(); return handle;
    }
    private static MixturePresentationModel CommandModel(FluidMaterialCatalog catalog)
        // Two gas IDs exist in the router fixture. This tests frame identity,
        // not atmosphere calibration; no live breathing policy uses this model.
        => new MixturePresentationModel(catalog, catalog.Materials.Values.Select(material =>
            new MixtureSpeciesStyle(material.Id, material.Id, new MixtureSrgbColor(.3, .4, .5))), new AtmosphereSettings(), "oxygen", "nitrogen");
    private static void Conserved(Action<bool> check, FluidInventory actual, FluidInventory expected)
    {
        void Close(double a, double b) => check(Math.Abs(a - b) <= Math.Max(double.Epsilon, 2e-12 * Math.Max(Math.Abs(a), Math.Abs(b))));
        check(actual.MassesKg.Count == expected.MassesKg.Count && actual.DiseaseCounts.Count == expected.DiseaseCounts.Count);
        foreach (var entry in expected.MassesKg) { check(actual.MassesKg.TryGetValue(entry.Key, out var value) && value > 0); Close(value, entry.Value); }
        foreach (var entry in expected.DiseaseCounts) { check(actual.DiseaseCounts.TryGetValue(entry.Key, out var value) && value > 0); Close(value, entry.Value); }
        Close(actual.TotalEnergyJoules, expected.TotalEnergyJoules); Close(actual.MomentumX, expected.MomentumX); Close(actual.MomentumY, expected.MomentumY);
        Close(actual.RadiationQuantity, expected.RadiationQuantity);
    }
}
