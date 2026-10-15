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
    private static MixturePresentationModel Model(FluidMaterialCatalog catalog)
        => new MixturePresentationModel(catalog, new[]
        {
            new MixtureSpeciesStyle("oxygen", "Oxygen", new MixtureSrgbColor(0.4, 0.75, 0.95)),
            new MixtureSpeciesStyle("nitrogen", "Nitrogen", new MixtureSrgbColor(0.65, 0.65, 0.75)),
            new MixtureSpeciesStyle("co2", "Carbon dioxide", new MixtureSrgbColor(0.25, 0.25, 0.25)),
            new MixtureSpeciesStyle("chlorine", "Chlorine", new MixtureSrgbColor(0.65, 0.8, 0.3)),
            new MixtureSpeciesStyle("water", "Water", new MixtureSrgbColor(0.1, 0.3, 0.9))
        }, new AtmosphereSettings(), "oxygen", "co2", new Dictionary<string, double> { ["chlorine"] = 1e-9 });
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
