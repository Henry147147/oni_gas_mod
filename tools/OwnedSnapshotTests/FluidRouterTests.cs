using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using MixedAtmospheres.Oni;

internal static class FluidRouterTests
{
    private const int ModifyCell = -1252920804, ModifyEnergy = 818320644, ConsumeMass = 1727657959, EmitMass = 797274363;
    internal static void Run(Action<string, System.Action> test, Action<bool> check, Action<System.Action> reject)
    {
        test("router stages fluid injection and generic callback without publication", () =>
        {
            var fixture = Create(); var before = fixture.state.Save();
            check(fixture.router.TryStage(ModifyCell, Modification(2, 1, 0.5f, 300, callback: 42), out var pending));
            using (pending!)
            {
                check(before.SequenceEqual(fixture.state.Save()));
                var committed = pending!.Commit();
                check(committed.Snapshot.SampleCell(2).Inventory.MassesKg["oxygen"] == 0.5 && committed.Callbacks.Length == 1 && committed.Callbacks[0].callbackIdx == 42);
                reject(() => pending.Commit());
            }
        });
        test("discarded staged mutation preserves inventory and source sink ledgers", () =>
        {
            var fixture = Create(); var before = fixture.state.Save();
            check(fixture.router.TryStage(ModifyCell, Modification(0, 1, -0.5f, 300), out var pending));
            pending!.Dispose(); check(before.SequenceEqual(fixture.state.Save()));
            reject(() => pending.Commit());
        });
        test("router replacement accounts for removed and supplied inventory", () =>
        {
            var fixture = Create();
            check(fixture.router.TryStage(ModifyCell, Modification(0, 4, 1, 300, replacement: 1), out var pending));
            using (pending!)
            {
                var after = pending!.Commit().Snapshot;
                check(after.SampleCell(0).Inventory.MassesKg.Count == 1 && after.SampleCell(0).Inventory.MassesKg["nitrogen"] == 1);
                check(after.RemovedInventory.MassesKg["oxygen"] == 2 && after.SuppliedInventory.MassesKg["nitrogen"] == 1);
                ColonyFluidState.Restore(fixture.state.Save(), fixture.catalog);
            }
        });
        test("unselected negative mass removes a bulk mixture with traces intact", () =>
        {
            var fixture = Create();
            using (var transaction = fixture.state.BeginTransaction())
            {
                transaction.Inject(0, fixture.catalog.CreateInventory(new Dictionary<string, double> { ["nitrogen"] = 1e-16 }, 300), "trace-input"); transaction.Commit();
            }
            check(fixture.router.TryStage(ModifyCell, Modification(0, 0, -0.5f, -1), out var pending));
            using (pending!)
            {
                var snapshot = pending!.Commit().Snapshot;
                check(snapshot.SampleCell(0).Inventory.MassesKg["nitrogen"] > 0 && snapshot.RemovedInventory.MassesKg["nitrogen"] > 0);
                check(snapshot.SampleCell(0).Inventory.TotalMassKg == 1.5);
            }
        });
        test("cell-local consumption selects constituent and supplies exact callback", () =>
        {
            var fixture = Create();
            using (var transaction = fixture.state.BeginTransaction())
            { transaction.Inject(0, fixture.catalog.CreateInventory(new Dictionary<string, double> { ["nitrogen"] = 1 }, 300), "nitrogen-input"); transaction.Commit(); }
            var before = fixture.state.Save();
            check(fixture.router.TryStage(ConsumeMass, Consumption(0, 1, 0.5f, 91), out var pending));
            using (pending!)
            {
                check(before.SequenceEqual(fixture.state.Save()));
                var committed = pending!.Commit();
                check(committed.ConsumedCallbacks.Length == 1 && committed.ConsumedCallbacks[0].callbackIdx == 91 &&
                    committed.ConsumedCallbacks[0].elemIdx == 1 && committed.ConsumedCallbacks[0].mass == 0.5f && committed.ConsumedCallbacks[0].temperature == 300);
                check(committed.Snapshot.SampleCell(0).Inventory.MassesKg["oxygen"] == 1.5 && committed.Snapshot.SampleCell(0).Inventory.MassesKg["nitrogen"] == 1);
            }
        });
        test("mass emission delivers success metadata only after conservation commit", () =>
        {
            var fixture = Create();
            check(fixture.router.TryStage(EmitMass, Emission(2, 1, 0.25f, 300, 17, 0, 4), out var pending));
            using (pending!)
            {
                var committed = pending!.Commit();
                check(committed.EmittedCallbacks.Length == 1 && committed.EmittedCallbacks[0].callbackIdx == 17 && committed.EmittedCallbacks[0].suceeded == 1);
                check(committed.EmittedCallbacks[0].mass == 0.25f && committed.EmittedCallbacks[0].diseaseIdx == 0 && committed.EmittedCallbacks[0].diseaseCount == 4);
                check(committed.Snapshot.SampleCell(2).Inventory.DiseaseCounts[1234] == 4 && committed.Snapshot.SuppliedInventory.MassesKg["oxygen"] == 0.25);
            }
        });
        test("cell heat converts kilojoules to an accountable joule ledger", () =>
        {
            var fixture = Create(); var initial = fixture.state.CaptureSnapshot().SampleCell(0).Inventory.TotalEnergyJoules;
            check(fixture.router.TryStage(ModifyEnergy, Heat(0, 1, 400), out var pending));
            using (pending!)
            {
                var snapshot = pending!.Commit().Snapshot;
                check(snapshot.SampleCell(0).Inventory.TotalEnergyJoules == initial + 1000 && snapshot.ExternalHeatJoules == 1000);
            }
            var saved = fixture.state.Save();
            reject(() => fixture.router.TryStage(ModifyEnergy, Heat(0, 1000, 301), out _));
            reject(() => fixture.router.TryStage(ModifyEnergy, Heat(2, 1, 400), out _));
            check(saved.SequenceEqual(fixture.state.Save()));
        });
        test("unsupported displacement radius outputs and malformed messages fail before changes", () =>
        {
            var fixture = Create(); var saved = fixture.state.Save();
            reject(() => fixture.router.TryStage(ModifyCell, Modification(0, 1, 1, 300, replacement: 2), out _));
            reject(() => fixture.router.TryStage(ModifyCell, Modification(0, 3, 1000, 300), out _));
            reject(() => fixture.router.TryStage(ModifyCell, Modification(3, 1, 1, 300), out _));
            reject(() => fixture.router.TryStage(ModifyCell, Modification(2, 0, 1, 300), out _));
            reject(() => fixture.router.TryStage(ConsumeMass, Consumption(0, 1, 1, 5, radius: 1), out _));
            reject(() => fixture.router.TryStage(EmitMass, Emission(1, 2, 1000, 300, 2), out _));
            reject(() => fixture.router.TryStage(EmitMass, new byte[23], out _));
            reject(() => fixture.router.TryStage(123456, Array.Empty<byte>(), out _));
            check(saved.SequenceEqual(fixture.state.Save()));
        });
        test("native nonfluid routes leave custom revision untouched", () =>
        {
            var fixture = Create(); var saved = fixture.state.Save();
            check(!fixture.router.TryStage(ModifyEnergy, Heat(3, 1, 400), out var heat) && heat == null);
            check(!fixture.router.TryStage(ModifyCell, Modification(3, 0, 0, 0, replacement: 1), out var dig) && dig == null);
            check(!fixture.router.TryStage(ModifyCell, Modification(2, 3, 1000, 300, replacement: 1), out var build) && build == null);
            check(saved.SequenceEqual(fixture.state.Save()));
        });
        test("fractional disease and unrepresentable callbacks retain authoritative input", () =>
        {
            var fixture = Create();
            using (var transaction = fixture.state.BeginTransaction())
            { transaction.Inject(2, fixture.catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = 1 }, 300, diseaseCounts: new Dictionary<int, double> { [1234] = 1 }), "diseased-input"); transaction.Commit(); }
            var saved = fixture.state.Save();
            reject(() => fixture.router.TryStage(ConsumeMass, Consumption(2, 1, 0.5f, 5), out _));
            check(saved.SequenceEqual(fixture.state.Save()));
            check(fixture.router.TryStage(ModifyEnergy, Heat(0, 1, 400), out var heating));
            using (heating!) heating!.Commit();
            saved = fixture.state.Save();
            reject(() => fixture.router.TryStage(ConsumeMass, Consumption(0, 1, 0.5f, 5), out _));
            check(saved.SequenceEqual(fixture.state.Save()));
        });
        test("stale routed transaction cannot publish or deliver duplicate callbacks", () =>
        {
            var fixture = Create();
            check(fixture.router.TryStage(ModifyCell, Modification(2, 1, 0.5f, 300, callback: 8), out var old));
            check(fixture.router.TryStage(ModifyCell, Modification(2, 1, 1, 300, callback: 9), out var fresh));
            using (fresh!) fresh!.Commit();
            var saved = fixture.state.Save();
            using (old!) reject(() => old!.Commit());
            check(saved.SequenceEqual(fixture.state.Save()));
        });
    }

    private static (ColonyFluidState state, FluidMaterialCatalog catalog, ExperimentalFluidMessageRouter router) Create()
    {
        var phases = new Dictionary<int, NativeMatterPhase>
        { [1] = NativeMatterPhase.Vacuum, [2] = NativeMatterPhase.Gas, [3] = NativeMatterPhase.Liquid, [4] = NativeMatterPhase.Solid };
        var bindings = new Dictionary<int, NativeFluidBinding>
        { [2] = new NativeFluidBinding("oxygen", NativeMatterPhase.Gas), [3] = new NativeFluidBinding("water", NativeMatterPhase.Liquid) };
        var raw = new byte[29 + 36 * 16];
        System.Text.Encoding.ASCII.GetBytes("SIMSAVE\0").CopyTo(raw, 0);
        Int(raw, 8, 15); Int(raw, 12, 4); Int(raw, 16, 4);
        for (var cell = 0; cell < 16; ++cell) { Int(raw, 29 + 16 * cell, 1); Int(raw, 29 + 24 * 16 + cell * 12, 1); }
        void Cell(int padded, int element, float mass, float temperature)
        { Int(raw, 29 + 16 * padded, element); Float(raw, 29 + 16 * padded + 4, temperature); Float(raw, 29 + 16 * padded + 8, mass); }
        Cell(5, 2, 2, 300); Cell(6, 3, 100, 300); Cell(10, 4, 1000, 300);
        var prefixed = new byte[raw.Length + 4]; Int(prefixed, 0, raw.Length); raw.CopyTo(prefixed, 4);
        var import = NativeColonyImport.StageNewColony(2, 2, new[] { new NativeWorldPlacement(0, 2, 2, 0, 0) },
            new[] { new NativeWorldSource(0, prefixed) }, phases, bindings, 1);
        var catalog = new FluidMaterialCatalog("router-test-model", new[]
        {
            new FluidMaterial("oxygen", NativeMatterPhase.Gas, 0.032, 658),
            new FluidMaterial("nitrogen", NativeMatterPhase.Gas, 0.028, 743),
            new FluidMaterial("water", NativeMatterPhase.Liquid, 0.018, 4184, 1000)
        });
        var state = ColonyFluidState.Import(import, catalog);
        var router = new ExperimentalFluidMessageRouter(state, catalog, new[]
        {
            new FluidMessageElementBinding(0, NativeMatterPhase.Vacuum),
            new FluidMessageElementBinding(1, NativeMatterPhase.Gas, "oxygen"),
            new FluidMessageElementBinding(2, NativeMatterPhase.Liquid, "water"),
            new FluidMessageElementBinding(3, NativeMatterPhase.Solid),
            new FluidMessageElementBinding(4, NativeMatterPhase.Gas, "nitrogen")
        }, new Dictionary<byte, int> { [0] = 1234 });
        return (state, catalog, router);
    }

    private static byte[] Modification(int cell, ushort element, float mass, float temperature, int callback = -1, byte replacement = 0)
    {
        var result = new byte[28]; Int(result, 0, cell); Int(result, 4, callback); Float(result, 8, temperature); Float(result, 12, mass);
        UShort(result, 20, element); result[22] = replacement; result[23] = byte.MaxValue; result[24] = 1; return result;
    }
    private static byte[] Consumption(int cell, ushort element, float mass, int callback, byte radius = 0)
    {
        var result = new byte[16]; Int(result, 0, cell); Int(result, 4, callback); Float(result, 8, mass); UShort(result, 12, element); result[14] = radius; return result;
    }
    private static byte[] Emission(int cell, ushort element, float mass, float temperature, int callback, byte diseaseIndex = byte.MaxValue, int diseaseCount = 0)
    {
        var result = new byte[24]; Int(result, 0, cell); Int(result, 4, callback); Float(result, 8, mass); Float(result, 12, temperature);
        Int(result, 16, diseaseCount); UShort(result, 20, element); result[22] = diseaseIndex; return result;
    }
    private static byte[] Heat(int cell, float kilojoules, float maximumTemperature)
    {
        var result = new byte[16]; Int(result, 0, cell); Float(result, 4, kilojoules); Float(result, 8, maximumTemperature); Int(result, 12, 1000); return result;
    }
    private static void Int(byte[] data, int offset, int value) => BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(offset), value);
    private static void UShort(byte[] data, int offset, ushort value) => BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(offset), value);
    private static void Float(byte[] data, int offset, float value) => Int(data, offset, BitConverter.SingleToInt32Bits(value));
}
