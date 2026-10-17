using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using MixedAtmospheres.Oni;

// Pure adapter/pixel-policy tests against the licensed declarations. These do
// not create Unity objects, call Time, install Harmony or start native physics.
internal static class MixtureRuntimeTests
{
    internal static void Run(Action<string, System.Action> test, Action<bool> check, Action<System.Action> reject)
    {
        test("no presentation provider leaves native rendering inactive", () =>
        {
            MixturePresentationRuntime.ResetProviders();
            check(!MixturePresentationRuntime.HasProvider && MixturePresentationRuntime.AcquireFrame() == null);
            check(!MixturePresentationRuntime.TryGetCell(0, out _));
        });
        test("fixture ownership is explicit copied and stable for a Unity frame", () =>
        {
            var setup = Setup();
            var input = new Dictionary<int, FluidInventory> { [1] = Air(setup.catalog) };
            MixturePresentationRuntime.EnableFixtureFrame(2, 2, input); input.Clear();
            var first = MixturePresentationRuntime.AcquireFrameFor(10)!;
            check(first.IsFixture && first.HasWorld && first.Width == 2 && first.Height == 2);
            check(first.TryGetCell(1, out var air) && air!.IsBreathable && !first.TryGetCell(0, out _));
            MixturePresentationRuntime.EnableFixtureFrame(2, 2, new Dictionary<int, FluidInventory> { [2] = FluidInventory.Empty });
            check(ReferenceEquals(first, MixturePresentationRuntime.AcquireFrameFor(10)) && first.TryGetCell(1, out _));
            var next = MixturePresentationRuntime.AcquireFrameFor(11)!;
            check(!ReferenceEquals(first, next) && !next.TryGetCell(1, out _) && next.TryGetCell(2, out var vacuum) && vacuum!.GasMassKg == 0);
            reject(() => MixturePresentationRuntime.EnableFixtureFrame(2, 2, new Dictionary<int, FluidInventory> { [4] = FluidInventory.Empty }));
        });
        test("appearance replacement preserves the pinned model until next frame", () =>
        {
            var setup = Setup();
            MixturePresentationRuntime.EnableFixtureFrame(2, 2, new Dictionary<int, FluidInventory> { [0] = Air(setup.catalog) });
            var first = MixturePresentationRuntime.AcquireFrameFor(20)!;
            MixturePresentationRuntime.Configure(setup.model.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false, PatternContrast = 0 }));
            check(ReferenceEquals(first, MixturePresentationRuntime.AcquireFrameFor(20)));
            check(first.Model.VisualOptions.PatternEnabled && !MixturePresentationRuntime.CurrentModel!.VisualOptions.PatternEnabled);
            var next = MixturePresentationRuntime.AcquireFrameFor(21)!;
            check(next.TryGetCell(0, out var reading) && !reading!.PatternEnabled);
            check(next.PublicationGeneration > first.PublicationGeneration);
        });
        test("completed source pins its exact prepared views and refreshes only on the presentation thread", () =>
        {
            var setup = Setup(); var state = State(setup.catalog); FluidContainerHandle handle;
            using (var tx = state.BeginTransaction())
            { handle = tx.CreateContainer("prepared:tank", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas)); tx.TransferCellToContainer(0, handle, .25); tx.CommitFrame(1, .2); }
            var prepared = MixturePresentationPublication.Prepare(state.CaptureSnapshot(), setup.model); var calls = 0;
            MixturePresentationRuntime.BindCompletedSource(() => { ++calls; return prepared; });
            check(calls == 1 && MixturePresentationRuntime.InventoryProviderCount == 0);
            var first = MixturePresentationRuntime.AcquireFrameFor(22)!;
            check(calls == 2 && !first.IsFixture && ReferenceEquals(first.Model, prepared.Model) && first.Revision == prepared.Source.Revision &&
                first.TryGetCell(0, out var cell) && ReferenceEquals(cell, prepared.Frame.SampleCell(0)) &&
                first.TryResolveInventory(handle, out var contents) && ReferenceEquals(contents, prepared.Frame.SampleContainer(handle)));
            var old = prepared;
            using (var tx = state.BeginTransaction()) { tx.TransferCellToContainer(0, handle, .25); tx.CommitFrame(2, .4); }
            prepared = MixturePresentationPublication.Prepare(state.CaptureSnapshot(), setup.model);
            check(ReferenceEquals(first, MixturePresentationRuntime.AcquireFrameFor(22)) && calls == 2);
            using (MixturePresentationRuntime.EnterWorkerBatch(first))
                check(Task.Run(() => ReferenceEquals(first, MixturePresentationRuntime.AcquireFrame()) &&
                    MixturePresentationRuntime.TryResolveInventory(handle, out var reading) && reading!.TotalMassKg == .25).GetAwaiter().GetResult());
            check(calls == 2);
            var next = MixturePresentationRuntime.AcquireFrameFor(23)!;
            check(calls == 3 && next.TryResolveInventory(handle, out var changed) && changed!.TotalMassKg == .5 &&
                ReferenceEquals(changed, prepared.Frame.SampleContainer(handle)) && old.Frame.SampleContainer(handle).TotalMassKg == .25 &&
                next.Revision > first.Revision);
            reject(() => Task.Run(() => MixturePresentationRuntime.AcquireFrameFor(24)).GetAwaiter().GetResult());
            check(calls == 3);
        });
        test("container-only completed source keeps exact owned views without claiming native world cells", () =>
        {
            var setup = Setup(); var state = State(setup.catalog); FluidContainerHandle handle;
            using (var tx = state.BeginTransaction())
            { handle = tx.CreateContainer("container-only:tank", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas)); tx.TransferAllCellToContainer(0, handle); tx.CommitFrame(1, .2); }
            var prepared = MixturePresentationPublication.Prepare(state.CaptureSnapshot(), setup.model);
            MixturePresentationRuntime.BindCompletedSource(() => prepared, includeWorldCells: false);
            var frame = MixturePresentationRuntime.AcquireFrameFor(120)!;
            check(MixturePresentationRuntime.HasProvider && !MixturePresentationRuntime.HasWorldProvider && !frame.HasWorld &&
                frame.Revision == prepared.Source.Revision && ReferenceEquals(frame.Model, prepared.Model));
            var resolved = frame.TryResolveInventory(handle, out var contents);
            check(!frame.TryGetCell(0, out _) && !frame.TryGetCell(1, out _) &&
                resolved && ReferenceEquals(contents, prepared.Frame.SampleContainer(handle)) &&
                frame.TryResolveInventory(handle.Id, out var byId) && ReferenceEquals(contents, byId));
            check(prepared.Frame.TrySampleCell(0, out var vacuum) && vacuum!.Failures == BreathingFailure.Vacuum && contents!.TotalMassKg == 1);
        });
        test("container-only scope survives publication appearance changes and worker frame pinning", () =>
        {
            var setup = Setup(); var state = State(setup.catalog); FluidContainerHandle handle;
            using (var tx = state.BeginTransaction())
            { handle = tx.CreateContainer("container-only:moving", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas)); tx.TransferAllCellToContainer(0, handle); tx.CommitFrame(1, .2); }
            var prepared = MixturePresentationPublication.Prepare(state.CaptureSnapshot(), setup.model); var calls = 0;
            MixturePresentationRuntime.BindCompletedSource(() => { ++calls; return prepared; }, includeWorldCells: false);
            var first = MixturePresentationRuntime.AcquireFrameFor(121)!;
            using (var tx = state.BeginTransaction()) { tx.TransferContainerToCell(handle, 0, .25); tx.CommitFrame(2, .4); }
            prepared = MixturePresentationPublication.Prepare(state.CaptureSnapshot(), setup.model);
            MixturePresentationRuntime.Configure(setup.model.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false, PatternContrast = 0 }));
            using (MixturePresentationRuntime.EnterWorkerBatch(first))
                check(Task.Run(() => ReferenceEquals(first, MixturePresentationRuntime.AcquireFrame()) && !first.HasWorld &&
                    !MixturePresentationRuntime.TryGetCell(0, out _) && first.TryResolveInventory(handle, out var old) && old!.TotalMassKg == 1).GetAwaiter().GetResult());
            check(calls == 2);
            var next = MixturePresentationRuntime.AcquireFrameFor(122)!;
            check(calls == 3 && !next.HasWorld && !MixturePresentationRuntime.HasWorldProvider && !next.TryGetCell(0, out _) &&
                next.TryResolveInventory(handle, out var changed) && changed!.TotalMassKg == .75 && !changed.PatternEnabled &&
                next.Revision == prepared.Source.Revision && prepared.Frame.SampleCell(0).TotalMassKg == .25);
        });
        test("retiring container-only binding restores default world projection without reviving its worker frame", () =>
        {
            var setup = Setup(); var state = State(setup.catalog);
            var prepared = MixturePresentationPublication.Prepare(state.CaptureSnapshot(), setup.model);
            MixturePresentationRuntime.BindCompletedSource(() => prepared, isFixture: true, includeWorldCells: false);
            var retired = MixturePresentationRuntime.AcquireFrameFor(123)!;
            MixturePresentationRuntime.ClearFixtures();
            check(!MixturePresentationRuntime.HasProvider && !MixturePresentationRuntime.HasWorldProvider);
            MixturePresentationRuntime.Configure(setup.model); MixturePresentationRuntime.Publish(state.CaptureSnapshot());
            var direct = MixturePresentationRuntime.AcquireFrameFor(124)!;
            check(direct.HasWorld && MixturePresentationRuntime.HasWorldProvider && direct.TryGetCell(0, out var oxygen) && oxygen!.TotalMassKg == 1);
            reject(() => MixturePresentationRuntime.EnterWorkerBatch(retired));
            MixturePresentationRuntime.ResetProviders();
            MixturePresentationRuntime.BindCompletedSource(() => prepared);
            check(MixturePresentationRuntime.HasWorldProvider && MixturePresentationRuntime.AcquireFrameFor(125)!.TryGetCell(0, out _));
        });
        test("bound publication failures preserve the last completed view and reject independent writers", () =>
        {
            var setup = Setup(); var state = State(setup.catalog);
            var initial = MixturePresentationPublication.Prepare(state.CaptureSnapshot(), setup.model);
            using (var tx = state.BeginTransaction()) { tx.Transfer(0, 1, .25); tx.CommitFrame(2, .4); }
            var newer = MixturePresentationPublication.Prepare(state.CaptureSnapshot(), setup.model);
            MixturePresentationPublication? supplied = newer;
            MixturePresentationRuntime.BindCompletedSource(() => supplied);
            var first = MixturePresentationRuntime.AcquireFrameFor(24)!;
            supplied = null; reject(() => MixturePresentationRuntime.AcquireFrameFor(25));
            check(ReferenceEquals(first, MixturePresentationRuntime.AcquireFrameFor(24)) && MixturePresentationRuntime.CurrentRevision == newer.Source.Revision);
            supplied = initial; reject(() => MixturePresentationRuntime.AcquireFrameFor(25));
            supplied = MixturePresentationPublication.Prepare(State(setup.catalog).CaptureSnapshot(), setup.model);
            reject(() => MixturePresentationRuntime.AcquireFrameFor(25));
            check(ReferenceEquals(first, MixturePresentationRuntime.AcquireFrameFor(24)));
            reject(() => MixturePresentationRuntime.Publish(newer.Source));
            reject(() => MixturePresentationRuntime.RegisterInventory("independent", () => Air(setup.catalog), 1));
            reject(() => MixturePresentationRuntime.EnableFixtureFrame(2, 2, new Dictionary<int, FluidInventory>()));
            reject(() => MixturePresentationRuntime.Configure(Model(setup.catalog)));
            supplied = newer;
            check(MixturePresentationRuntime.AcquireFrameFor(25)!.Revision == newer.Source.Revision && MixturePresentationRuntime.InventoryProviderCount == 0);
        });
        test("bound visual choices follow new authority policy without changing physical readings", () =>
        {
            var setup = Setup(); var state = State(setup.catalog);
            var prepared = MixturePresentationPublication.Prepare(state.CaptureSnapshot(), setup.model);
            MixturePresentationRuntime.BindCompletedSource(() => prepared);
            var first = MixturePresentationRuntime.AcquireFrameFor(26)!;
            check(first.TryGetCell(0, out var before) && !before!.IsBreathable && before.PatternEnabled);
            var colourOnly = setup.model.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false, PatternContrast = 0 });
            check(colourOnly.IsVisualVariantOf(setup.model) && !Model(setup.catalog).IsVisualVariantOf(setup.model));
            MixturePresentationRuntime.Configure(colourOnly);
            check(ReferenceEquals(first, MixturePresentationRuntime.AcquireFrameFor(26)));
            var changed = MixturePresentationRuntime.AcquireFrameFor(27)!;
            check(changed.TryGetCell(0, out var visual) && !visual!.PatternEnabled && visual.TotalPressurePa == before!.TotalPressurePa &&
                visual.TotalMassKg == before.TotalMassKg && visual.TemperatureKelvin == before.TemperatureKelvin && visual.Failures == before.Failures);
            var newPolicy = Model(setup.catalog, new AtmosphereSettings { HardMode = false });
            prepared = MixturePresentationPublication.Prepare(state.CaptureSnapshot(), newPolicy);
            var next = MixturePresentationRuntime.AcquireFrameFor(28)!;
            check(next.TryGetCell(0, out var policyReading) && policyReading!.IsBreathable && !policyReading.PatternEnabled &&
                policyReading.TotalPressurePa == before!.TotalPressurePa && next.Model.IsVisualVariantOf(newPolicy) && !next.Model.IsVisualVariantOf(setup.model));
            reject(() => MixturePresentationRuntime.Configure(colourOnly));
            check(first.TryGetCell(0, out var retained) && ReferenceEquals(retained, before) && !retained!.IsBreathable);
        });
        test("completed source cannot resurrect a reset session from a callback", () =>
        {
            var setup = Setup(); var publication = MixturePresentationPublication.Prepare(State(setup.catalog).CaptureSnapshot(), setup.model);
            var reset = false;
            MixturePresentationRuntime.BindCompletedSource(() => { if (reset) MixturePresentationRuntime.ResetProviders(); return publication; });
            var first = MixturePresentationRuntime.AcquireFrameFor(29)!; reset = true;
            reject(() => MixturePresentationRuntime.AcquireFrameFor(30));
            check(!MixturePresentationRuntime.HasProvider && !MixturePresentationRuntime.HasCachedFrame && first.TryGetCell(0, out _));
            reject(() => MixturePresentationRuntime.EnterWorkerBatch(first));
            MixturePresentationRuntime.BindCompletedSource(() => publication);
            check(MixturePresentationRuntime.AcquireFrameFor(30)!.HasWorld);
        });
        test("completed source binding validates before publishing and rejects recursive capture", () =>
        {
            var setup = Setup(); var publication = MixturePresentationPublication.Prepare(State(setup.catalog).CaptureSnapshot(), setup.model);
            reject(() => MixturePresentationRuntime.BindCompletedSource(() => null));
            reject(() => MixturePresentationRuntime.BindCompletedSource(() =>
            { MixturePresentationRuntime.BindCompletedSource(() => publication); return publication; }));
            reject(() => MixturePresentationRuntime.BindCompletedSource(() =>
            { MixturePresentationRuntime.Configure(setup.model); return publication; }));
            check(!MixturePresentationRuntime.HasProvider && !MixturePresentationRuntime.HasCachedFrame);
            MixturePresentationRuntime.BindCompletedSource(() => publication, true);
            var first = MixturePresentationRuntime.AcquireFrameFor(31)!;
            reject(() => MixturePresentationRuntime.BindCompletedSource(() => publication));
            check(first.IsFixture && MixturePresentationRuntime.IsFixture);
            var lease = MixturePresentationRuntime.EnterWorkerBatch(first);
            MixturePresentationRuntime.ClearFixtures(); lease.Dispose();
            check(!MixturePresentationRuntime.HasProvider && !MixturePresentationRuntime.HasCachedFrame && MixturePresentationRuntime.AcquireFrameFor(31) == null);
            reject(() => MixturePresentationRuntime.EnterWorkerBatch(first));
        });
        test("completed source requires a clean prior frame and releases its callback on reset", () =>
        {
            var setup = Setup(); var publication = MixturePresentationPublication.Prepare(State(setup.catalog).CaptureSnapshot(), setup.model);
            MixturePresentationRuntime.RegisterInventory("old", () => Air(setup.catalog), 1);
            var old = MixturePresentationRuntime.AcquireFrameFor(32)!; MixturePresentationRuntime.UnregisterInventory("old");
            reject(() => MixturePresentationRuntime.BindCompletedSource(() => publication));
            check(ReferenceEquals(old, MixturePresentationRuntime.AcquireFrameFor(32)));
            MixturePresentationRuntime.ResetProviders();
            var captured = BindCapturedCompletedSource(publication);
            MixturePresentationRuntime.ResetProviders();
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
            check(!captured.IsAlive && !MixturePresentationRuntime.HasProvider && !MixturePresentationRuntime.HasCachedFrame);
        });
        test("inventory adapters capture once per Unity frame with explicit failures", () =>
        {
            var setup = Setup(); var calls = 0; var alive = true;
            MixturePresentationRuntime.RegisterInventory("tank", () => { calls++; return Air(setup.catalog); }, 1, () => alive);
            MixturePresentationRuntime.RegisterInventory("broken", () => throw new InvalidOperationException("adapter unavailable"), 1);
            var first = MixturePresentationRuntime.AcquireFrameFor(30)!;
            check(calls == 1 && first.TryResolveInventory("tank", out var reading) && reading!.IsBreathable);
            check(!first.TryResolveInventory("broken", out _) && first.InventoryErrors["broken"] == "adapter unavailable");
            check(ReferenceEquals(first, MixturePresentationRuntime.AcquireFrameFor(30)) && calls == 1);
            MixturePresentationRuntime.AcquireFrameFor(31); check(calls == 2);
            alive = false;
            var dead = MixturePresentationRuntime.AcquireFrameFor(32)!;
            check(calls == 2 && !dead.TryResolveInventory("tank", out _) && !dead.InventoryErrors.ContainsKey("tank"));
        });
        test("worker lease shares frame and model without Unity or callback access", () =>
        {
            var setup = Setup(); var calls = 0;
            MixturePresentationRuntime.EnableFixtureFrame(2, 2, new Dictionary<int, FluidInventory> { [0] = Air(setup.catalog) });
            MixturePresentationRuntime.RegisterInventory("tank", () => { calls++; return Air(setup.catalog); }, 1);
            var first = MixturePresentationRuntime.AcquireFrameFor(40)!;
            using (MixturePresentationRuntime.EnterWorkerBatch(first))
            {
                var correct = Task.Run(() => MixturePresentationRuntime.TryGetCell(0, out var reading) && reading!.IsBreathable &&
                    ReferenceEquals(first, MixturePresentationRuntime.AcquireFrame()) &&
                    MixturePresentationRuntime.TryResolveInventory("tank", out _)).GetAwaiter().GetResult();
                check(correct && calls == 1);
                using (MixturePresentationRuntime.EnterWorkerBatch(first))
                    check(Task.Run(() => ReferenceEquals(first, MixturePresentationRuntime.AcquireFrame())).GetAwaiter().GetResult());
                check(Task.Run(() => ReferenceEquals(first, MixturePresentationRuntime.AcquireFrame())).GetAwaiter().GetResult());
            }
            reject(() => Task.Run(() => MixturePresentationRuntime.AcquireFrame()).GetAwaiter().GetResult());
            reject(() => Task.Run(() => MixturePresentationRuntime.EnterWorkerBatch(first)).GetAwaiter().GetResult());
            check(calls == 1);
        });
        test("authoritative snapshots stay separate from fixtures and stale inventories", () =>
        {
            var setup = Setup(); var state = State(setup.catalog); var before = state.CaptureSnapshot();
            MixturePresentationRuntime.Publish(before);
            MixturePresentationRuntime.RegisterInventory("stale", () => new MixtureContainerSample("stale", Air(setup.catalog), 1, before.Revision + 1));
            var first = MixturePresentationRuntime.AcquireFrameFor(50)!;
            check(first.HasWorld && !first.IsFixture && first.TryGetCell(0, out _) && !first.TryGetCell(3, out _));
            check(first.InventoryErrors.ContainsKey("stale"));
            MixturePresentationRuntime.Publish(before);
            var unchanged = MixturePresentationRuntime.AcquireFrameFor(51)!;
            check(first.PublicationGeneration == unchanged.PublicationGeneration);
            reject(() => MixturePresentationRuntime.EnableFixtureFrame(2, 2, new Dictionary<int, FluidInventory>()));
            using (var transaction = state.BeginTransaction())
            { transaction.Inject(0, Air(setup.catalog), "runtime-test"); transaction.Commit(); }
            var next = state.CaptureSnapshot(); MixturePresentationRuntime.Publish(next);
            reject(() => MixturePresentationRuntime.Publish(before));
            check(MixturePresentationRuntime.AcquireFrameFor(52)!.Revision == next.Revision);
            var wrong = Model(new FluidMaterialCatalog("wrong-version", setup.catalog.Materials.Values));
            reject(() => MixturePresentationRuntime.Configure(wrong));
            check(ReferenceEquals(MixturePresentationRuntime.CurrentModel, setup.model));
        });
        test("owned containers resolve from the same world frame without independent providers", () =>
        {
            var setup = Setup(); var state = State(setup.catalog);
            FluidContainerHandle handle;
            using (var tx = state.BeginTransaction())
            {
                tx.Inject(0, setup.catalog.CreateInventory(new Dictionary<string, double>
                    { ["nitrogen"] = 1e-16, ["chlorine"] = 1e-8 }, 300), "trace-test");
                handle = tx.CreateContainer("owned:tank", new FluidContainerDefinition(.2, 5, FluidContainerPhases.Gas));
                tx.TransferCellToContainer(0, handle, .25); tx.CommitFrame(7, 1.4);
            }
            MixturePresentationRuntime.Publish(state.CaptureSnapshot());
            var frame = MixturePresentationRuntime.AcquireFrameFor(100)!;
            check(MixturePresentationRuntime.InventoryProviderCount == 0 && frame.Inventories.Count == 0 && frame.Revision == state.CaptureSnapshot().Revision);
            check(frame.TryResolveInventory(handle, out var reading) && Math.Abs(reading!.TotalMassKg - .25) < 1e-14 && reading.Revision == frame.Revision &&
                reading.Constituents.Any(row => row.MaterialId == "chlorine" && row.MassKg > 0) && reading.Warnings.Count > 0);
            check(frame.TryResolveInventory(handle.Id, out var byId) && ReferenceEquals(reading, byId) && frame.TryGetCell(0, out var cell) && cell!.Revision == reading!.Revision);
            using (MixturePresentationRuntime.EnterWorkerBatch(frame))
                check(Task.Run(() => MixturePresentationRuntime.TryResolveInventory(handle, out var worker) && ReferenceEquals(worker, reading) &&
                    !MixturePresentationRuntime.TryResolveIndependentInventory(handle.Id, out _)).GetAwaiter().GetResult());
        });
        test("conserved visual fixtures retain the shared snapshot path and explicit fixture lifecycle", () =>
        {
            var setup = Setup(); var state = State(setup.catalog);
            MixturePresentationRuntime.PublishConservedFixture(state.CaptureSnapshot());
            var frame = MixturePresentationRuntime.AcquireFrameFor(105)!;
            check(frame.IsFixture && MixturePresentationRuntime.IsFixture && frame.HasWorld);
            reject(() => MixturePresentationRuntime.Publish(state.CaptureSnapshot()));
            MixturePresentationRuntime.Configure(setup.model.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false }));
            check(MixturePresentationRuntime.AcquireFrameFor(106)!.IsFixture);
            MixturePresentationRuntime.ClearFixtures();
            check(!MixturePresentationRuntime.HasWorldProvider && !MixturePresentationRuntime.IsFixture &&
                MixturePresentationRuntime.AcquireFrameFor(107) == null && frame.TryGetCell(0, out _));
        });
        test("owned runtime lookups reject replaced handles while old completed frames remain immutable", () =>
        {
            var setup = Setup(); var state = State(setup.catalog); FluidContainerHandle first, replacement;
            using (var tx = state.BeginTransaction())
            { first = tx.CreateContainer("reused", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas)); tx.TransferCellToContainer(0, first, .25); tx.Commit(); }
            MixturePresentationRuntime.Publish(state.CaptureSnapshot()); var old = MixturePresentationRuntime.AcquireFrameFor(110)!;
            check(old.TryResolveInventory(first, out var oldReading) && oldReading!.TotalMassKg == .25);
            using (var tx = state.BeginTransaction())
            {
                tx.RetireContainerToCell(first, 0);
                replacement = tx.CreateContainer(first.Id, new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas));
                tx.TransferCellToContainer(0, replacement, .5); tx.Commit();
            }
            MixturePresentationRuntime.Publish(state.CaptureSnapshot());
            check(ReferenceEquals(old, MixturePresentationRuntime.AcquireFrameFor(110)));
            var next = MixturePresentationRuntime.AcquireFrameFor(111)!;
            check(!next.TryResolveInventory(first, out _) && next.TryResolveInventory(replacement, out var reading) && reading!.TotalMassKg == .5);
            check(old.TryResolveInventory(first, out var retained) && ReferenceEquals(retained, oldReading) && !old.TryResolveInventory(replacement, out _));
            check(!next.TryResolveInventory(default(FluidContainerHandle), out _));
        });
        test("independent adapters cannot shadow an owned container in either registration order", () =>
        {
            var setup = Setup(); var state = State(setup.catalog); var before = state.CaptureSnapshot();
            MixturePresentationRuntime.Publish(before);
            var captures = 0;
            MixturePresentationRuntime.RegisterInventory("tank", () => { ++captures; return Air(setup.catalog); }, 1);
            var prior = MixturePresentationRuntime.AcquireFrameFor(120)!; check(captures == 1);
            FluidContainerHandle handle;
            using (var tx = state.BeginTransaction())
            { handle = tx.CreateContainer("tank", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas)); tx.TransferCellToContainer(0, handle, .25); tx.Commit(); }
            reject(() => MixturePresentationRuntime.Publish(state.CaptureSnapshot()));
            check(ReferenceEquals(prior, MixturePresentationRuntime.AcquireFrameFor(120)) && !prior.TryResolveInventory(handle, out _));
            MixturePresentationRuntime.UnregisterInventory("tank"); MixturePresentationRuntime.Publish(state.CaptureSnapshot());
            reject(() => MixturePresentationRuntime.RegisterInventory("tank", () => { ++captures; return Air(setup.catalog); }, 1));
            var next = MixturePresentationRuntime.AcquireFrameFor(121)!;
            check(captures == 1 && MixturePresentationRuntime.InventoryProviderCount == 0 && next.TryResolveInventory(handle, out var reading) && reading!.TotalMassKg == .25);
        });
        test("runtime owned lookup takes precedence even over a malformed independent frame entry", () =>
        {
            var setup = Setup(); var state = State(setup.catalog); FluidContainerHandle handle;
            using (var tx = state.BeginTransaction())
            { handle = tx.CreateContainer("tank", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas)); tx.TransferCellToContainer(0, handle, .25); tx.Commit(); }
            var snapshot = state.CaptureSnapshot(); var world = setup.model.CaptureFrame(snapshot);
            var fake = setup.model.Describe(Air(setup.catalog), 1, snapshot.Revision, "independent:wrong");
            var frame = new RuntimeMixturePresentationFrame(130, 1, 0, setup.model, world, null, 2, 2, snapshot.Revision, false,
                new Dictionary<string, MixturePresentation> { [handle.Id] = fake }, new Dictionary<string, string>());
            check(frame.TryResolveInventory(handle.Id, out var actual) && !ReferenceEquals(actual, fake) && actual!.TotalMassKg == .25);
            check(frame.TryResolveInventory(handle, out var exact) && ReferenceEquals(exact, actual));
        });
        test("owned container handles survive custom checkpoint reload in one presentation session", () =>
        {
            var setup = Setup(); var state = State(setup.catalog); FluidContainerHandle handle;
            using (var tx = state.BeginTransaction())
            { handle = tx.CreateContainer("tank", new FluidContainerDefinition(1, 10, FluidContainerPhases.Gas)); tx.TransferCellToContainer(0, handle, .25); tx.Commit(); }
            MixturePresentationRuntime.Publish(state.CaptureSnapshot()); var first = MixturePresentationRuntime.AcquireFrameFor(140)!;
            var restored = ColonyFluidState.Restore(state.Save(), setup.catalog);
            MixturePresentationRuntime.Publish(restored.CaptureSnapshot()); var next = MixturePresentationRuntime.AcquireFrameFor(141)!;
            check(first.TryResolveInventory(handle, out var old) && next.TryResolveInventory(handle, out var current) &&
                old!.TotalMassKg == current!.TotalMassKg && old.Revision == current.Revision);
        });
        test("a different colony cannot replace a live presentation session without teardown", () =>
        {
            var setup = Setup(); var state = State(setup.catalog); MixturePresentationRuntime.Publish(state.CaptureSnapshot());
            var first = MixturePresentationRuntime.AcquireFrameFor(150)!;
            reject(() => MixturePresentationRuntime.Publish(State(setup.catalog).CaptureSnapshot()));
            check(ReferenceEquals(first, MixturePresentationRuntime.AcquireFrameFor(150)));
        });
        test("reentrant provider publication cannot mix completed world and inventory revisions", () =>
        {
            var setup = Setup(); var state = State(setup.catalog); var before = state.CaptureSnapshot();
            MixturePresentationRuntime.Publish(before);
            using (var tx = state.BeginTransaction()) { tx.Inject(0, Air(setup.catalog), "next"); tx.Commit(); }
            var next = state.CaptureSnapshot();
            var replacement = setup.model.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false });
            MixturePresentationRuntime.RegisterInventory("first", () => new MixtureContainerSample("first", Air(setup.catalog), 1, before.Revision));
            MixturePresentationRuntime.RegisterInventory("reentrant", () =>
            {
                MixturePresentationRuntime.Publish(next); MixturePresentationRuntime.Configure(replacement);
                return new MixtureContainerSample("reentrant", Air(setup.catalog), 1, before.Revision);
            });
            var captured = MixturePresentationRuntime.AcquireFrameFor(160)!;
            check(captured.Revision == before.Revision && ReferenceEquals(captured.Model, setup.model) &&
                captured.Inventories.Count == 2 && captured.Inventories.Values.All(value => value.Revision == captured.Revision));
            check(captured.TryGetCell(0, out var cell) && cell!.Revision == before.Revision);
            var following = MixturePresentationRuntime.AcquireFrameFor(161)!;
            check(following.Revision == next.Revision && ReferenceEquals(following.Model, replacement) &&
                following.Inventories.Count == 0 && following.InventoryErrors.Count == 2);
        });
        test("teardown from an inventory callback cannot republish a retired session", () =>
        {
            var setup = Setup();
            MixturePresentationRuntime.RegisterInventory("teardown", () =>
            {
                MixturePresentationLifecycle.EndSession();
                return new MixtureContainerSample("teardown", Air(setup.catalog), 1, 0);
            });
            var retiredCalls = 0;
            MixturePresentationRuntime.RegisterInventory("retired", () => { ++retiredCalls; return Air(setup.catalog); }, 1);
            reject(() => MixturePresentationRuntime.AcquireFrameFor(170));
            check(retiredCalls == 0 && !MixturePresentationRuntime.HasCachedFrame && !MixturePresentationRuntime.HasProvider && MixturePresentationRuntime.CurrentModel == null);
            setup = Setup(); MixturePresentationRuntime.Publish(State(setup.catalog).CaptureSnapshot());
            check(MixturePresentationRuntime.AcquireFrameFor(171) != null);
        });
        test("recursive provider capture is explicitly rejected without poisoning later frames", () =>
        {
            var setup = Setup();
            MixturePresentationRuntime.RegisterInventory("recursive", () =>
            {
                reject(() => MixturePresentationRuntime.AcquireFrameFor(181));
                return new MixtureContainerSample("recursive", Air(setup.catalog), 1, 0);
            });
            check(MixturePresentationRuntime.AcquireFrameFor(180)!.Inventories.Count == 1);
            check(MixturePresentationRuntime.AcquireFrameFor(182)!.Inventories.Count == 1);
        });
        test("a reentrant liveness callback cannot retire a replacement provider with the same ID", () =>
        {
            var setup = Setup(); var calls = 0;
            MixturePresentationRuntime.RegisterInventory("reused-adapter", () => throw new Exception("Retired adapter was captured."), 1, () =>
            {
                MixturePresentationRuntime.RegisterInventory("reused-adapter", () => { ++calls; return Air(setup.catalog); }, 1);
                return false;
            });
            check(MixturePresentationRuntime.AcquireFrameFor(190)!.Inventories.Count == 0 && MixturePresentationRuntime.InventoryProviderCount == 1);
            check(MixturePresentationRuntime.AcquireFrameFor(191)!.Inventories.Count == 1 && calls == 1);
        });
        test("normal-world density opacity is independent of physical pressure", () =>
        {
            var setup = Setup(); var options = new MixtureWorldRendering.OpacityParameters(15, 200, 0.1);
            var cold = setup.model.Describe(Air(setup.catalog), 1);
            var hot = setup.model.Describe(Air(setup.catalog, 600, 0.5), 1);
            var a = MixtureWorldRendering.Sample(cold, 0, 0, options);
            var b = MixtureWorldRendering.Sample(hot, 0, 0, options);
            check(Math.Abs(a.PhysicalPressurePa - b.PhysicalPressurePa) < 1e-8 && a.Opacity > b.Opacity);
            var light = setup.model.Describe(setup.catalog.CreateInventory(new Dictionary<string, double> { ["nitrogen"] = 40 * 0.028 }, 300), 1);
            var heavy = setup.model.Describe(setup.catalog.CreateInventory(new Dictionary<string, double> { ["chlorine"] = 40 * 0.0709 }, 300), 1);
            check(Math.Abs(light.TotalPressurePa - heavy.TotalPressurePa) < 1e-8);
            check(MixtureWorldRendering.DensityOpacity(light, options) < MixtureWorldRendering.DensityOpacity(heavy, options));
            check(a.Alpha == 255 && b.Alpha == 255 && a.Hazard == 0 && b.Hazard == 0);
        });
        test("wet world pixels combine actual gas visibility and compressed liquid volume without changing pressure or hazards", () =>
        {
            var setup = Setup(); var parameters = new MixtureWorldRendering.OpacityParameters(15, 200, .1);
            foreach (var liquidVolume in new[] { .2, .7 })
            {
                var gasVolume = 1 - liquidVolume;
                var rows = new[]
                {
                    new MixtureThermodynamicConstituent("oxygen", NativeMatterPhase.Gas, 8.4 * .032 * gasVolume, .032, .21 * gasVolume),
                    new MixtureThermodynamicConstituent("nitrogen", NativeMatterPhase.Gas, 31.6 * .028 * gasVolume, .028, .79 * gasVolume),
                    new MixtureThermodynamicConstituent("water", NativeMatterPhase.Liquid, 1250 * liquidVolume, .018, liquidVolume)
                };
                var pressure = 40 * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300;
                var physical = new MixtureThermodynamicReading(rows, rows.Sum(r => r.MassKg), 1e6, 0,
                    300, pressure, pressure, 1, gasVolume, liquidVolume, 0, "pixel-test", "actual-volume");
                var view = setup.model.Describe(physical); var pixel = MixtureWorldRendering.Sample(view, 1.25, 2.5, parameters);
                var gasOpacity = Math.Max(.1, Math.Clamp((view.GasDensityKgPerCubicMetre * 101.3 - 15) / 185, 0, 1));
                var expected = (byte)Math.Round(255 * (gasOpacity * gasVolume + liquidVolume), MidpointRounding.AwayFromZero);
                var nominal = (byte)Math.Round(255 * (gasOpacity * (1 - liquidVolume * 1.25) + liquidVolume * 1.25), MidpointRounding.AwayFromZero);
                var colour = view.WorldFluidColorAt(1.25, 2.5);
                check(view.HasPhaseVolumeAppearance && pixel.Opacity == expected && pixel.Opacity != nominal);
                check(pixel.R == (byte)Math.Round(colour.R * 255, MidpointRounding.AwayFromZero) &&
                    pixel.G == (byte)Math.Round(colour.G * 255, MidpointRounding.AwayFromZero) &&
                    pixel.B == (byte)Math.Round(colour.B * 255, MidpointRounding.AwayFromZero));
                check(pixel.Alpha == 255 && pixel.Hazard == 0 && pixel.PhysicalPressurePa == pressure && view.IsBreathable);
                check(pixel.Opacity > MixtureWorldRendering.DensityOpacity(view, parameters));
            }
        });
        test("legacy wet world pixels retain gas-only opacity and colour without actual EOS phase volumes", () =>
        {
            var setup = Setup(); var parameters = new MixtureWorldRendering.OpacityParameters(15, 200, .1);
            var masses = Air(setup.catalog).MassesKg.ToDictionary(p => p.Key, p => p.Value); masses["water"] = 250;
            var view = setup.model.Describe(setup.catalog.CreateInventory(masses, 300), 1);
            var pixel = MixtureWorldRendering.Sample(view, 1.25, 2.5, parameters); var gas = view.ColorAt(1.25, 2.5);
            check(!view.HasPhaseVolumeAppearance && pixel.Opacity == MixtureWorldRendering.DensityOpacity(view, parameters));
            check(pixel.R == (byte)Math.Round(gas.R * 255, MidpointRounding.AwayFromZero) &&
                pixel.B == (byte)Math.Round(gas.B * 255, MidpointRounding.AwayFromZero));
        });
        test("trace hazard stays visible independently of tint or texture contrast", () =>
        {
            var setup = Setup(); var safe = Air(setup.catalog);
            var contaminated = setup.catalog.CreateInventory(new Dictionary<string, double>
            { ["oxygen"] = 8.4 * 0.032, ["nitrogen"] = 31.6 * 0.028, ["chlorine"] = 1e-6 * 0.0709 }, 300);
            var options = new MixtureWorldRendering.OpacityParameters(15, 200, 0.1);
            var clean = MixtureWorldRendering.Sample(setup.model.Describe(safe, 1), 2, 3, options);
            var dirty = MixtureWorldRendering.Sample(setup.model.Describe(contaminated, 1), 2, 3, options);
            check(clean.Hazard == 0 && dirty.Hazard == 255 && dirty.Alpha == 255);
            check(Math.Abs(clean.R - dirty.R) <= 1 && Math.Abs(clean.G - dirty.G) <= 1 && Math.Abs(clean.B - dirty.B) <= 1);
            var plainModel = setup.model.WithVisualOptions(new MixtureVisualOptions { PatternEnabled = false, PatternContrast = 0 });
            var plain = MixtureWorldRendering.Sample(plainModel.Describe(contaminated, 1), 2, 3, options);
            check(plain.Hazard == dirty.Hazard && plain.PhysicalPressurePa == dirty.PhysicalPressurePa && plain.Opacity == dirty.Opacity);
        });
        test("vacuum and liquid pixels have zero gas alpha opacity and danger", () =>
        {
            var setup = Setup(); var parameters = new MixtureWorldRendering.OpacityParameters(15, 200, 0.1);
            foreach (var inventory in new[] { FluidInventory.Empty,
                setup.catalog.CreateInventory(new Dictionary<string, double> { ["water"] = 100 }, 300) })
            {
                var pixel = MixtureWorldRendering.Sample(setup.model.Describe(inventory, 1), 1, 1, parameters);
                check(pixel.R == 0 && pixel.G == 0 && pixel.B == 0 && pixel.Alpha == 0 && pixel.Opacity == 0 && pixel.Hazard == 0);
            }
            reject(() => new MixtureWorldRendering.OpacityParameters(200, 15, 0.1));
        });
        test("real teardown clears providers and old leases cannot repin a prior colony", () =>
        {
            var setup = Setup();
            MixturePresentationRuntime.EnableFixtureFrame(2, 2, new Dictionary<int, FluidInventory> { [0] = Air(setup.catalog) });
            MixturePresentationRuntime.RegisterInventory("previous-colony", () => Air(setup.catalog), 1);
            var oldFrame = MixturePresentationRuntime.AcquireFrameFor(60)!;
            var oldLease = MixturePresentationRuntime.EnterWorkerBatch(oldFrame);
            MixturePresentationLifecycle.EndSession();
            check(!MixturePresentationRuntime.HasProvider && MixturePresentationRuntime.CurrentModel == null && MixturePresentationRuntime.AcquireFrame() == null);
            var replacement = Setup();
            MixturePresentationRuntime.EnableFixtureFrame(3, 2, new Dictionary<int, FluidInventory> { [4] = Air(replacement.catalog) });
            var next = MixturePresentationRuntime.AcquireFrameFor(60)!;
            reject(() => MixturePresentationRuntime.EnterWorkerBatch(oldFrame));
            using (MixturePresentationRuntime.EnterWorkerBatch(next))
            {
                oldLease.Dispose();
                check(Task.Run(() => ReferenceEquals(next, MixturePresentationRuntime.AcquireFrame()) &&
                    !MixturePresentationRuntime.TryGetCell(0, out _) && MixturePresentationRuntime.TryGetCell(4, out _)).GetAwaiter().GetResult());
            }
            check(next.Width == 3 && !next.TryResolveInventory("previous-colony", out _));
            check(oldFrame.TryGetCell(0, out _));
            MixturePresentationLifecycle.EndSession(); MixturePresentationLifecycle.EndSession();
            check(!MixturePresentationRuntime.HasProvider && MixturePresentationRuntime.CurrentModel == null);
        });
        test("teardown releases registered provider closures", () =>
        {
            var setup = Setup(); var captured = RegisterCapturedProvider(setup.catalog);
            check(captured.IsAlive);
            MixturePresentationRuntime.AcquireFrameFor(70);
            MixturePresentationLifecycle.EndSession();
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
            check(!captured.IsAlive);
        });
        test("one cleanup failure cannot retain providers or skip another service", () =>
        {
            Setup(); var called = false;
            System.Action failure = () => throw new InvalidOperationException("Injected cleanup failure");
            System.Action other = () => called = true;
            MixturePresentationRuntime.SessionEnding += failure; MixturePresentationRuntime.SessionEnding += other;
            try
            {
                MixturePresentationLifecycle.EndSession();
                check(called && MixturePresentationRuntime.CurrentModel == null && !MixturePresentationRuntime.HasProvider &&
                    MixturePresentationRuntime.LastShutdownError.Contains("Injected cleanup failure"));
            }
            finally { MixturePresentationRuntime.SessionEnding -= failure; MixturePresentationRuntime.SessionEnding -= other; }
            MixturePresentationLifecycle.EndSession(); check(MixturePresentationRuntime.LastShutdownError.Length == 0);
        });
        test("teardown targets real native and game cleanup but excludes texture recreation", () =>
        {
            var targets = MixturePresentationLifecycle.TargetMethods().ToArray();
            check(targets.Length == 2 && targets.Any(item => item.DeclaringType == typeof(Sim) && item.Name == nameof(Sim.Shutdown)) &&
                targets.Any(item => item.DeclaringType == typeof(Game) && item.Name == "DestroyInstances") &&
                targets.All(item => item.DeclaringType != typeof(PropertyTextures) && item.GetMethodBody() != null));
        });
        test("custom overlays inherit native oxygen status visibility without warning fallback", () =>
        {
            var field = typeof(StatusItem).GetField("overlayBitfieldMap", BindingFlags.Static | BindingFlags.NonPublic)!;
            var map = (Dictionary<HashedString, StatusItem.StatusItemOverlays>)field.GetValue(null)!;
            var before = map.ToArray(); MixtureOverlays.RegisterStatusItemOverlays(); MixtureOverlays.RegisterStatusItemOverlays();
            foreach (MixtureOverlayKind kind in Enum.GetValues(typeof(MixtureOverlayKind)))
                check(map.TryGetValue(MixtureOverlays.Id(kind), out var actual) && actual == StatusItem.StatusItemOverlays.None &&
                    StatusItem.GetStatusItemOverlayBySimViewMode(MixtureOverlays.Id(kind)) == actual);
            foreach (var original in before) check(map[original.Key] == original.Value);
        });
        test("shutdown observations require explicit presentation mode and report post-cleanup state", () =>
        {
            var directory = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "mixture-shutdown-" + Guid.NewGuid().ToString("N"));
            var enabled = false;
            try
            {
                MixturePresentationLifecycle.EnableShutdownObservation(directory, () => enabled,
                    () => MixturePresentationRuntime.CurrentModel == null && !MixturePresentationRuntime.HasProvider);
                Setup(); MixturePresentationLifecycle.EndSession("mode-disabled");
                check(!System.IO.Directory.Exists(directory));
                var setup = Setup(); enabled = true;
                MixturePresentationRuntime.EnableFixtureFrame(2, 2, new Dictionary<int, FluidInventory> { [0] = Air(setup.catalog) });
                MixturePresentationRuntime.RegisterInventory("observed-tank", () => Air(setup.catalog), 1);
                MixturePresentationRuntime.AcquireFrameFor(80);
                MixturePresentationLifecycle.EndSession("Sim.Shutdown");
                MixturePresentationLifecycle.EndSession("Game.DestroyInstances");
                if (MixturePresentationLifecycle.LastShutdownObservationError.Length != 0)
                    throw new InvalidOperationException(MixturePresentationLifecycle.LastShutdownObservationError);
                using var report = System.Text.Json.JsonDocument.Parse(System.IO.File.ReadAllText(System.IO.Path.Combine(directory, "presentation-shutdown.json")));
                var root = report.RootElement; var observations = root.GetProperty("Observations");
                check(root.GetProperty("Passed").GetBoolean() && observations.GetArrayLength() == 2);
                var first = observations[0]; var before = first.GetProperty("Before"); var after = first.GetProperty("After");
                check(first.GetProperty("Boundary").GetString() == "Sim.Shutdown" && before.GetProperty("HasProvider").GetBoolean() &&
                    before.GetProperty("HasModel").GetBoolean() && before.GetProperty("HasCachedFrame").GetBoolean() &&
                    before.GetProperty("InventoryProviderCount").GetInt32() == 1);
                check(!after.GetProperty("HasProvider").GetBoolean() && !after.GetProperty("HasModel").GetBoolean() &&
                    !after.GetProperty("HasCachedFrame").GetBoolean() && after.GetProperty("InventoryProviderCount").GetInt32() == 0 &&
                    first.GetProperty("OwnedBindingsCleared").GetBoolean() && MixturePresentationLifecycle.LastShutdownObservationError.Length == 0);
            }
            finally
            {
                MixturePresentationLifecycle.DisableShutdownObservation();
                if (System.IO.Directory.Exists(directory)) System.IO.Directory.Delete(directory, true);
            }
        });
        test("shutdown observation errors cannot block provider and model retirement", () =>
        {
            var directory = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "mixture-shutdown-" + Guid.NewGuid().ToString("N"));
            try
            {
                MixturePresentationLifecycle.EnableShutdownObservation(directory, () => throw new InvalidOperationException("Injected observer failure"), () => true);
                Setup(); MixturePresentationLifecycle.EndSession();
                check(!MixturePresentationRuntime.HasProvider && MixturePresentationRuntime.CurrentModel == null &&
                    MixturePresentationLifecycle.LastShutdownObservationError.Contains("Injected observer failure") && !System.IO.Directory.Exists(directory));
            }
            finally { MixturePresentationLifecycle.DisableShutdownObservation(); }
        });
        test("shutdown evidence reports uncleared inspection bindings as a failure", () =>
        {
            var directory = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "mixture-shutdown-" + Guid.NewGuid().ToString("N"));
            try
            {
                MixturePresentationLifecycle.EnableShutdownObservation(directory, () => true, () => false);
                Setup(); MixturePresentationLifecycle.EndSession();
                using var report = System.Text.Json.JsonDocument.Parse(System.IO.File.ReadAllText(System.IO.Path.Combine(directory, "presentation-shutdown.json")));
                check(!report.RootElement.GetProperty("Passed").GetBoolean() &&
                    !report.RootElement.GetProperty("Observations")[0].GetProperty("OwnedBindingsCleared").GetBoolean());
            }
            finally
            {
                MixturePresentationLifecycle.DisableShutdownObservation();
                if (System.IO.Directory.Exists(directory)) System.IO.Directory.Delete(directory, true);
            }
        });
        MixturePresentationRuntime.ResetProviders();
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference BindCapturedCompletedSource(MixturePresentationPublication publication)
    {
        var token = new object(); var reference = new WeakReference(token);
        MixturePresentationRuntime.BindCompletedSource(() => { GC.KeepAlive(token); return publication; });
        return reference;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference RegisterCapturedProvider(FluidMaterialCatalog catalog)
    {
        var token = new object(); var reference = new WeakReference(token);
        MixturePresentationRuntime.RegisterInventory("captured-owner", () => { GC.KeepAlive(token); return Air(catalog); }, 1);
        return reference;
    }

    private static (FluidMaterialCatalog catalog, MixturePresentationModel model) Setup()
    {
        MixturePresentationRuntime.ResetProviders();
        var catalog = new FluidMaterialCatalog("runtime-presentation-test", new[]
        {
            new FluidMaterial("oxygen", NativeMatterPhase.Gas, 0.032, 658),
            new FluidMaterial("nitrogen", NativeMatterPhase.Gas, 0.028, 743),
            new FluidMaterial("co2", NativeMatterPhase.Gas, 0.044, 650),
            new FluidMaterial("chlorine", NativeMatterPhase.Gas, 0.0709, 300),
            new FluidMaterial("water", NativeMatterPhase.Liquid, 0.018, 4184, 1000)
        });
        var model = Model(catalog); MixturePresentationRuntime.Configure(model); return (catalog, model);
    }
    private static MixturePresentationModel Model(FluidMaterialCatalog catalog, AtmosphereSettings? settings = null)
        => new MixturePresentationModel(catalog, new[]
        {
            new MixtureSpeciesStyle("oxygen", "Oxygen", new MixtureSrgbColor(0.4, 0.75, 0.95)),
            new MixtureSpeciesStyle("nitrogen", "Nitrogen", new MixtureSrgbColor(0.65, 0.65, 0.75)),
            new MixtureSpeciesStyle("co2", "Carbon dioxide", new MixtureSrgbColor(0.25, 0.25, 0.25)),
            new MixtureSpeciesStyle("chlorine", "Chlorine", new MixtureSrgbColor(0.65, 0.8, 0.3)),
            new MixtureSpeciesStyle("water", "Water", new MixtureSrgbColor(0.1, 0.3, 0.9))
        }, settings ?? new AtmosphereSettings(), "oxygen", "co2", new Dictionary<string, double> { ["chlorine"] = 1e-9 });
    private static FluidInventory Air(FluidMaterialCatalog catalog, double temperature = 300, double multiplier = 1)
        => catalog.CreateInventory(new Dictionary<string, double>
        { ["oxygen"] = 8.4 * 0.032 * multiplier, ["nitrogen"] = 31.6 * 0.028 * multiplier }, temperature);

    private static ColonyFluidState State(FluidMaterialCatalog catalog)
    {
        var raw = new byte[29 + 36 * 16];
        System.Text.Encoding.ASCII.GetBytes("SIMSAVE\0").CopyTo(raw, 0);
        Put(8, 15); Put(12, 4); Put(16, 4);
        for (var cell = 0; cell < 16; ++cell) { Put(29 + 16 * cell, 1); Put(29 + 24 * 16 + cell * 12, 1); }
        Put(29 + 16 * 5, 2); Put(29 + 16 * 5 + 4, BitConverter.SingleToInt32Bits(300));
        Put(29 + 16 * 5 + 8, BitConverter.SingleToInt32Bits(1));
        Put(29 + 16 * 10, 4);
        var prefixed = new byte[raw.Length + 4]; BinaryPrimitives.WriteInt32LittleEndian(prefixed, raw.Length); raw.CopyTo(prefixed, 4);
        var imported = NativeColonyImport.StageNewColony(2, 2, new[] { new NativeWorldPlacement(0, 2, 2, 0, 0) },
            new[] { new NativeWorldSource(0, prefixed) },
            new Dictionary<int, NativeMatterPhase> { [1] = NativeMatterPhase.Vacuum, [2] = NativeMatterPhase.Gas, [4] = NativeMatterPhase.Solid },
            new Dictionary<int, NativeFluidBinding> { [2] = new NativeFluidBinding("oxygen", NativeMatterPhase.Gas) }, 1);
        return ColonyFluidState.Import(imported, catalog);
        void Put(int offset, int value) => BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(offset), value);
    }
}
