using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

internal static partial class ColonyCoordinatorTests
{
    private static readonly NativeWorldPlacement[] Placements = { new NativeWorldPlacement(9, 2, 2, 9, 2), new NativeWorldPlacement(7, 2, 2, 3, 2) };
    private static CoupledWorldConfiguration Configuration() => new CoupledWorldConfiguration
    {
        Width = 16, Height = 12, Gravity = 0, Diffusivity = 0, Viscosity = 0, ThermalConduction = false, MaximumTimestepSeconds = .001
    };
    private static NativeColonyImport Import(bool uniform = false, bool vacuum = false, bool water = false, bool unknown = false, bool chemistry = false)
    {
        var phases = new Dictionary<int, NativeMatterPhase> { [1] = NativeMatterPhase.Vacuum, [2] = NativeMatterPhase.Gas,
            [3] = NativeMatterPhase.Gas, [4] = NativeMatterPhase.Solid, [5] = NativeMatterPhase.Liquid, [6] = NativeMatterPhase.Gas,
            [7] = NativeMatterPhase.Gas, [8] = NativeMatterPhase.Gas };
        var bindings = new Dictionary<int, NativeFluidBinding> { [2] = new NativeFluidBinding("oxygen", NativeMatterPhase.Gas),
            [3] = new NativeFluidBinding("nitrogen", NativeMatterPhase.Gas), [5] = new NativeFluidBinding("water", NativeMatterPhase.Liquid),
            [6] = new NativeFluidBinding("unsupported-vapor", NativeMatterPhase.Gas), [7] = new NativeFluidBinding("carbon_dioxide", NativeMatterPhase.Gas),
            [8] = new NativeFluidBinding("chlorine", NativeMatterPhase.Gas) };
        byte[] Payload(NativeWorldPlacement placement)
        {
            const int count = 16; var raw = new byte[29 + 36 * count];
            new byte[] { 83, 73, 77, 83, 65, 86, 69, 0 }.CopyTo(raw, 0);
            void Int(int offset, int value) => BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(offset, 4), value);
            void Float(int offset, float value) => Int(offset, BitConverter.SingleToInt32Bits(value));
            Int(8, 15); Int(12, 4); Int(16, 4); Int(20, placement.OffsetX); Int(24, placement.OffsetY); raw[28] = 1;
            for (var i = 0; i < count; ++i) { Int(29 + i * 16, 4); Float(29 + i * 16 + 4, 300); Float(29 + i * 16 + 8, 1000); Int(29 + 24 * count + i * 12, 1); }
            for (var y = 0; y < 2; ++y) for (var x = 0; x < 2; ++x)
            {
                var i = x + 1 + (y + 1) * 4; var offset = 29 + i * 16;
                Int(offset, uniform ? 3 : (x + y) % 2 == 0 ? 2 : 3);
                if (chemistry) Int(offset, new[] { 3, 2, 7, 8 }[x + 2 * y]);
                Float(offset + 4, uniform ? 300 : 298 + x * 4 + y * 2); Float(offset + 8, uniform ? 1 : 1.4f + x * .7f);
                Float(offset + 12, .125f * (x + y + 1));
                Int(29 + 16 * count + i * 8, i == 5 ? int.MinValue : -42); Int(29 + 16 * count + i * 8 + 4, 20 + i);
            }
            if (placement.WorldId == 9 && (vacuum || unknown))
            {
                const int i = 10; Int(29 + i * 16, vacuum ? 1 : 6);
                if (vacuum) { raw.AsSpan(29 + i * 16 + 4, 12).Clear(); raw.AsSpan(29 + 16 * count + i * 8, 8).Clear(); }
            }
            if (placement.WorldId == 7 && water) { Int(29 + 5 * 16, 5); Float(29 + 5 * 16 + 8, 997); }
            var envelope = new byte[raw.Length + 4]; BinaryPrimitives.WriteInt32LittleEndian(envelope, raw.Length); raw.CopyTo(envelope, 4); return envelope;
        }
        return NativeColonyImport.StageNewColony(16, 12, Placements,
            Placements.Select(p => new NativeWorldSource(p.WorldId, Payload(p))).ToArray(), phases, bindings, 1);
    }
    internal static void Run(Action<string, Action> test, Action<bool, string> check, Action<Action> reject, Action<double, double, double> near)
    {
        test("colony admission binds all native worlds, geometry and metadata without exposing a writer", () =>
        {
            var configuration = Configuration(); using var simulation = CoupledColonySimulation.Create(Import(), configuration); var frame = simulation.CaptureFrame();
            check(frame.CompletedFrame == 0 && frame.SimulationTimeSeconds == 0 && frame.Worlds.Count == 2 && frame.Projection.Revision == 1,
                "Initial paired publication differs");
            configuration.Width = 99; check(frame.Projection.Width == 16, "Caller configuration changed admitted geometry");
            var cell = frame.SampleCell(3 + 2 * 16); check(cell.WorldId == 7 && cell.PhysicalState.DiseaseCounts[int.MinValue] == 25 &&
                cell.PhysicalState.RadiationQuantity == .125, "Native placement or metadata changed");
            check(!frame.TrySampleCell(0, out _), "Unowned grid gap was admitted"); reject(() => frame.SampleCell(0));
            reject(() => ((IDictionary<int, CoupledWorldSnapshot>)frame.Worlds).Clear());
            near(cell.PhysicalState.PressurePa, frame.Projection.SampleCell(cell.GlobalCellIndex).GasPressurePa, cell.PhysicalState.PressurePa);
        });
        test("one global frame advances every world once and retains physical U, composition and metadata", () =>
        {
            using var simulation = CoupledColonySimulation.Create(Import(), Configuration()); var before = simulation.CaptureFrame();
            var after = simulation.AdvanceFrame(1, 1.0 / 1024); check(after.CompletedFrame == 1 && after.SimulationTimeSeconds == 1.0 / 1024,
                "Global frame clock differs");
            check(after.Worlds.Values.All(world => world.Revision == 1 && world.CompletedTimeSeconds == after.SimulationTimeSeconds) && after.Diagnostics.Count == 2,
                "A world was skipped or advanced more than once");
            foreach (var id in before.Worlds.Values.SelectMany(world => world.Cells).SelectMany(cell => cell.MassesKg.Keys).Distinct())
            {
                var a = before.Worlds.Values.SelectMany(world => world.Cells).Sum(cell => cell.MassesKg.TryGetValue(id, out var mass) ? mass : 0);
                var b = after.Worlds.Values.SelectMany(world => world.Cells).Sum(cell => cell.MassesKg.TryGetValue(id, out var mass) ? mass : 0); near(a, b, Math.Max(a, b));
            }
            foreach (var identity in before.Worlds.Values.SelectMany(world => world.Cells).SelectMany(cell => cell.DiseaseCounts.Keys).Distinct())
            {
                var a = before.Worlds.Values.SelectMany(world => world.Cells).Sum(cell => cell.DiseaseCounts.TryGetValue(identity, out var count) ? count : 0);
                var b = after.Worlds.Values.SelectMany(world => world.Cells).Sum(cell => cell.DiseaseCounts.TryGetValue(identity, out var count) ? count : 0); near(a, b, Math.Max(a, b));
            }
            foreach (var error in after.InternalEnergyProjectionErrorsJoules)
                check(after.Projection.SampleCell(error.Key).Inventory.InternalEnergyJoules - after.SampleCell(error.Key).PhysicalState.InternalEnergyJoules == error.Value,
                    "Projected U error was hidden or assigned as physical energy");
            var next = simulation.AdvanceFrame(2, 1.0 / 1024);
            check(next.Worlds.Values.All(world => world.Revision == 2) && before.Worlds.Values.All(world => world.Revision == 0), "Physics was reinitialized from a projection or old frame changed");
            reject(() => simulation.AdvanceFrame(2, .001)); check(ReferenceEquals(next, simulation.CaptureFrame()), "Duplicate region/frame call published");
        });
        test("later-world budget rejection cannot publish an earlier completed candidate", () =>
        {
            using var simulation = CoupledColonySimulation.Create(Import(uniform: true), Configuration()); var before = simulation.CaptureFrame();
            var rejected = false;
            try { simulation.AdvanceFrame(1, .001, 1); }
            catch (CoupledColonyAdvanceException error) { rejected = error.AttemptedFrame == 1 && error.CompletedWorldCandidates == 1; }
            check(rejected && ReferenceEquals(before, simulation.CaptureFrame()) && before.Worlds.Values.All(world => world.Revision == 0), "Partial all-world frame escaped");
            var after = simulation.AdvanceFrame(1, .001, 2); check(after.Diagnostics.Values.All(value => value.Substeps == 1 && value.RejectedSubsteps == 0), "Shared attempt accounting differs");
        });
        test("unsupported native cells and missing pure-liquid pressure reject before admission", () =>
        {
            reject(() => CoupledColonySimulation.Create(Import(vacuum: true), Configuration()).Dispose());
            reject(() => CoupledColonySimulation.Create(Import(unknown: true), Configuration()).Dispose());
            reject(() => CoupledColonySimulation.Create(Import(water: true), Configuration()).Dispose());
            var global = 3 + 2 * 16;
            using var water = CoupledColonySimulation.Create(Import(water: true), Configuration(), new Dictionary<int, double> { [global] = 100000 });
            check(water.CaptureFrame().SampleCell(global).PhysicalState.PressurePa == 100000 && water.CaptureFrame().Projection.SampleCell(global).GasPressurePa == 0,
                "Liquid mechanical and gas pressure were conflated");
            reject(() => CoupledColonySimulation.Create(Import(), Configuration(), new Dictionary<int, double> { [global] = 1 }).Dispose());
        });
        test("paired frame readers and retained physical samples survive retirement and disposal", () =>
        {
            var simulation = CoupledColonySimulation.Create(Import(uniform: true), Configuration()); var first = simulation.CaptureFrame();
            for (var n = 1; n <= 3; ++n) simulation.AdvanceFrame(n, .001, 2);
            var last = simulation.CaptureFrame();
            Task.WhenAll(Enumerable.Range(0, 8).Select(_ => Task.Run(() =>
            {
                var captured = simulation.CaptureFrame();
                check(captured.Worlds.Values.All(world => world.CompletedTimeSeconds == captured.SimulationTimeSeconds), "Mixed frame read");
            }))).GetAwaiter().GetResult();
            simulation.Dispose(); simulation.Dispose();
            check(first.Worlds.Values.All(world => world.Revision == 0) && last.Worlds.Values.All(world => world.Revision == 3) &&
                first.SampleCell(3 + 2 * 16).PhysicalState.DiseaseCounts[int.MinValue] == 25, "Retirement damaged managed frame data");
            reject(() => simulation.CaptureFrame()); reject(() => simulation.AdvanceFrame(4, .001));
        });
    }
}
