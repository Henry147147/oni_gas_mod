using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

internal static class OwnedContainerPresentationTests
{
    internal static void Run(Action<string, Action> test, Action<bool> check, Action<Action> reject,
        Func<ColonyFluidState> fixture, Func<MixturePresentationModel> model, FluidMaterialCatalog catalog)
    {
        test("owned cell and container presentation comes from one completed authority snapshot without a provider", () =>
        {
            var state = fixture(); FluidContainerHandle handle;
            using (var tx = state.BeginTransaction())
            {
                handle = tx.CreateContainer("pipe:1", new FluidContainerDefinition(0.2, 10, FluidContainerPhases.Gas));
                tx.TransferCellToContainer(5, handle, 0.5); tx.CommitFrame(7, 1.4);
            }
            var snapshot = state.CaptureSnapshot(); var frame = model().CaptureFrame(snapshot); var tank = frame.SampleContainer(handle);
            check(frame.ColonyId == snapshot.ColonyId && frame.OwnedContainerHandles["pipe:1"].Equals(handle));
            check(frame.Revision == snapshot.Revision && frame.CompletedFrame == 7 && tank.Revision == frame.Revision && tank.SourceId == "pipe:1");
            check(tank.TotalMassKg == 0.5 && frame.SampleCell(5).TotalMassKg == 1.5 && tank.TotalPressurePa == snapshot.SampleContainer(handle).GasPressurePa);
            check(ReferenceEquals(tank, frame.SampleContainer(handle)) && ReferenceEquals(tank, frame.SampleContainer("pipe:1")));
            using (var tx = state.BeginTransaction()) { tx.TransferContainerToCell(handle, 6, 0.25); tx.CommitFrame(8, 1.6); }
            var newer = model().CaptureFrame(state.CaptureSnapshot());
            check(tank.TotalMassKg == 0.5 && newer.SampleContainer(handle).TotalMassKg == 0.25 && newer.SampleCell(6).TotalMassKg == 0.25);
            reject(() => ((IDictionary<string, FluidContainerHandle>)frame.OwnedContainerHandles).Clear());
        });
        test("container presentation enforces full allocation identity across retirement aborts and unrelated colonies", () =>
        {
            var state = fixture(); FluidContainerHandle old;
            using (var tx = state.BeginTransaction()) { old = tx.CreateContainer("tank", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas)); tx.Commit(); }
            var oldFrame = model().CaptureFrame(state.CaptureSnapshot()); FluidContainerHandle replacement;
            using (var tx = state.BeginTransaction())
            { tx.RetireContainer(old); replacement = tx.CreateContainer("tank", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas)); tx.Commit(); }
            var current = model().CaptureFrame(state.CaptureSnapshot());
            check(oldFrame.TrySampleContainer(old, out _) && !oldFrame.TrySampleContainer(replacement, out _));
            check(!current.TrySampleContainer(old, out _) && current.TrySampleContainer(replacement, out _));
            using var aborted = state.BeginTransaction(); var provisional = aborted.CreateContainer("unpublished", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas));
            check(!current.TrySampleContainer(provisional, out _) && !current.TrySampleContainer(default(FluidContainerHandle), out _));
            reject(() => current.SampleContainer(old));
            var foreign = fixture(); FluidContainerHandle foreignHandle;
            using (var tx = foreign.BeginTransaction()) { foreignHandle = tx.CreateContainer("tank", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas)); tx.Commit(); }
            check(!current.TrySampleContainer(foreignHandle, out _));
        });
        test("independent display fixtures cannot override a container owned by the completed snapshot", () =>
        {
            var state = fixture(); FluidContainerHandle handle;
            using (var tx = state.BeginTransaction()) { handle = tx.CreateContainer("owned", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas)); tx.TransferCellToContainer(5, handle, 1); tx.Commit(); }
            var snapshot = state.CaptureSnapshot(); var presentation = model();
            reject(() => presentation.CaptureFrame(snapshot, new[] { new MixtureContainerSample("owned", FluidInventory.Empty, 1, snapshot.Revision) }));
            var frame = presentation.CaptureFrame(snapshot, new[] { new MixtureContainerSample("fixture", FluidInventory.Empty, 1, snapshot.Revision) });
            check(frame.Containers.Count == 2 && frame.OwnedContainerHandles.Count == 1 && frame.SampleContainer(handle).TotalMassKg == 1);
            check(!frame.OwnedContainerHandles.ContainsKey("fixture"));
        });
        test("owned container inspection preserves liquid separation and trace hazards through persistence and worker reads", () =>
        {
            var state = fixture(); FluidContainerHandle handle;
            var parcel = catalog.CreateInventory(new Dictionary<string, double>
                { ["oxygen"] = 0.25, ["nitrogen"] = 1, ["chlorine"] = 1e-4, ["water"] = 100 }, 300);
            using (var tx = state.BeginTransaction())
            {
                tx.Inject(6, parcel, "owned-container-test-input");
                handle = tx.CreateContainer("reservoir", new FluidContainerDefinition(2, 200, FluidContainerPhases.Gas | FluidContainerPhases.Liquid));
                tx.TransferAllCellToContainer(6, handle); tx.CommitFrame(5, 1);
            }
            var restored = ColonyFluidState.Restore(state.Save(), catalog); var snapshot = restored.CaptureSnapshot();
            var frame = model().CaptureFrame(snapshot); var view = frame.SampleContainer(handle);
            check(view.TotalPressurePa == snapshot.SampleContainer(handle).GasPressurePa && view.Failures.HasFlag(BreathingFailure.Contaminant));
            check(view.Constituents.Single(row => row.Phase == NativeMatterPhase.Liquid).MaterialId == "water" && view.GasConstituents.Count == 3);
            check(view.Constituents.Single(row => row.MaterialId == "chlorine").MassKg == 1e-4 && view.Warnings.Any(warning => warning.Contains("Chlorine", StringComparison.Ordinal)));
            Parallel.For(0, 32, _ =>
            {
                check(ReferenceEquals(view, frame.SampleContainer(handle)) && view.Revision == frame.Revision && view.Constituents.Count == 4);
                check(view.DisplayName.Contains("mixture", StringComparison.Ordinal) && view.GasConstituents.All(row => row.GasMoleFraction.HasValue));
            });
        });
    }
}
