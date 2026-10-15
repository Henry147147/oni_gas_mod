using System;
using System.Collections.Generic;
using System.Linq;
using MixedAtmospheres.Core;

internal static class TransactionStagingTests
{
    internal static void Run(Action<string, Action> test, Action<bool> check, Action<Action> reject,
        Func<ColonyFluidState> create, Func<FluidMaterialCatalog> catalog, int gas, int empty, int solid)
    {
        void Poisoned(ColonyFluidState state, int failedWrite, Action<FluidTransaction> operation)
        {
            var before = state.CapturePublication(); var bytes = state.Save(); using var tx = state.BeginTransaction();
            tx.InjectStagedWriteFailureForTesting(failedWrite); var injected = false;
            try { operation(tx); } catch (StagedFluidWriteException) { injected = true; }
            check(injected); reject(() => tx.Commit()); reject(() => tx.PrepareFrame(1, 0.2));
            reject(() => tx.SampleCell(gas)); reject(() => tx.ApplyHeat(gas, 1, "cannot-resume-poisoned-transaction"));
            check(ReferenceEquals(before, state.CapturePublication()) && bytes.SequenceEqual(state.Save()));
            // Failure belongs to this transaction; readers/new transactions remain usable.
            using var fresh = state.BeginTransaction(); check(fresh.SampleCell(gas).Inventory.TotalMassKg == before.Snapshot.SampleCell(gas).Inventory.TotalMassKg);
        }
        (ColonyFluidState state, FluidContainerHandle a, FluidContainerHandle b) Containers()
        {
            var state = create(); using var tx = state.BeginTransaction();
            var definition = new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas | FluidContainerPhases.Liquid);
            var a = tx.CreateContainer("a", definition); var b = tx.CreateContainer("b", definition);
            tx.TransferCellToContainer(gas, a, 0.5); tx.Commit(); return (state, a, b);
        }

        test("every container transfer and evacuation closes after either staged endpoint write fails", () =>
        {
            for (var operation = 0; operation < 11; ++operation)
                for (var failedWrite = 1; failedWrite <= 2; ++failedWrite)
                {
                    var f = Containers(); var selected = operation;
                    Poisoned(f.state, failedWrite, tx =>
                    {
                        switch (selected)
                        {
                            case 0: tx.TransferCellToContainer(gas, f.b, 0.25); break;
                            case 1: tx.TransferCellToContainer(gas, f.b, "oxygen", 0.25); break;
                            case 2: tx.TransferAllCellToContainer(gas, f.b); break;
                            case 3: tx.TransferContainerToCell(f.a, empty, 0.25); break;
                            case 4: tx.TransferContainerToCell(f.a, empty, "oxygen", 0.25); break;
                            case 5: tx.TransferAllContainerToCell(f.a, empty); break;
                            case 6: tx.TransferContainerToContainer(f.a, f.b, 0.25); break;
                            case 7: tx.TransferContainerToContainer(f.a, f.b, "oxygen", 0.25); break;
                            case 8: tx.TransferAllContainerToContainer(f.a, f.b); break;
                            case 9: tx.RetireContainerToCell(f.a, empty); break;
                            case 10: tx.RetireContainerToContainer(f.a, f.b); break;
                        }
                    });
                }
        });
        test("legacy cell transfers and three-write solid displacement cannot publish after a staged write failure", () =>
        {
            for (var failedWrite = 1; failedWrite <= 2; ++failedWrite)
            {
                Poisoned(create(), failedWrite, tx => tx.Transfer(gas, empty, 0.25));
                Poisoned(create(), failedWrite, tx => tx.TransferSpecies(gas, empty, "oxygen", 0.25));
            }
            for (var failedWrite = 1; failedWrite <= 3; ++failedWrite)
            {
                var state = create(); var hash = state.CaptureSnapshot().SampleCell(solid).NativeElementHash;
                Poisoned(state, failedWrite, tx => tx.DisplaceAndSetSolid(gas, hash, empty));
            }
        });
        test("cell source sink and heat writes close before a partial external ledger can commit", () =>
        {
            var parcel = catalog().CreateInventory(new Dictionary<string, double> { ["oxygen"] = 0.25 }, 300);
            Poisoned(create(), 1, tx => tx.Inject(empty, parcel, "injected-source"));
            Poisoned(create(), 1, tx => tx.Extract(gas, 0.25, "injected-sink"));
            Poisoned(create(), 1, tx => tx.ExtractSpecies(gas, "oxygen", 0.25, "injected-species-sink"));
            Poisoned(create(), 1, tx => tx.ApplyHeat(gas, 1000, "injected-heat"));
        });
        test("container registration retirement and native geometry writes also poison on insertion failure", () =>
        {
            var definition = new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas);
            Poisoned(create(), 1, tx => tx.CreateContainer("new", definition));
            var state = create(); FluidContainerHandle handle;
            using (var tx = state.BeginTransaction()) { handle = tx.CreateContainer("empty", definition); tx.Commit(); }
            Poisoned(state, 1, tx => tx.RetireContainer(handle));
            Poisoned(create(), 1, tx => tx.ClearSolid(solid));
            var emptyState = create(); var hash = emptyState.CaptureSnapshot().SampleCell(solid).NativeElementHash;
            Poisoned(emptyState, 1, tx => tx.SetSolid(empty, hash));
        });
        test("pre-write validation stays recoverable and does not consume the staged-write fault boundary", () =>
        {
            var state = create(); var before = state.Save(); using var tx = state.BeginTransaction();
            var handle = tx.CreateContainer("small", new FluidContainerDefinition(1, 0.25, FluidContainerPhases.Gas));
            reject(() => tx.InjectStagedWriteFailureForTesting(0)); reject(() => tx.InjectStagedWriteFailureForTesting(4));
            tx.InjectStagedWriteFailureForTesting(1);
            reject(() => tx.TransferCellToContainer(gas, handle, 0.5));
            check(tx.SampleCell(gas).Inventory.TotalMassKg == 2 && tx.SampleContainer(handle).Inventory.TotalMassKg == 0);
            var injected = false;
            try { tx.TransferCellToContainer(gas, handle, 0.125); } catch (StagedFluidWriteException) { injected = true; }
            check(injected); reject(() => tx.Commit()); check(before.SequenceEqual(state.Save()));
        });
    }
}
