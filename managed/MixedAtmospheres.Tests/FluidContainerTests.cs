using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using MixedAtmospheres.Core;

internal static class FluidContainerTests
{
    private const FluidContainerPhases Both = FluidContainerPhases.Gas | FluidContainerPhases.Liquid;

    internal static void Run(Action<string, Action> test, Action<bool> check, Action<Action> reject,
        Func<ColonyFluidState> create, Func<ColonyFluidState> createMetadataOnly, Func<FluidMaterialCatalog> catalog,
        int gas, int water, int empty, int solid)
    {
        void Near(double actual, double expected) => check(double.IsFinite(actual) && Math.Abs(actual - expected) <= Math.Max(double.Epsilon, Math.Abs(expected) * 1e-12));
        void Same(FluidInventory actual, FluidInventory expected, bool exact = false)
        {
            check(actual.MassesKg.Count == expected.MassesKg.Count && actual.DiseaseCounts.Count == expected.DiseaseCounts.Count);
            void Equal(double left, double right) { if (exact) check(left == right); else Near(left, right); }
            foreach (var entry in expected.MassesKg) Equal(actual.MassesKg[entry.Key], entry.Value);
            foreach (var entry in expected.DiseaseCounts) Equal(actual.DiseaseCounts[entry.Key], entry.Value);
            Equal(actual.TotalEnergyJoules, expected.TotalEnergyJoules); Equal(actual.MomentumX, expected.MomentumX);
            Equal(actual.MomentumY, expected.MomentumY); Equal(actual.RadiationQuantity, expected.RadiationQuantity);
        }
        FluidContainerDefinition Definition(double volume = 1, double capacity = 5000, FluidContainerPhases phases = Both) => new(volume, capacity, phases);
        FluidInventory Parcel(string id, double mass) => catalog().CreateInventory(new Dictionary<string, double> { [id] = mass }, 300);
        FluidContainerHandle Register(ColonyFluidState state, string id = "tank", FluidContainerDefinition? definition = null)
        { using var tx = state.BeginTransaction(); var handle = tx.CreateContainer(id, definition ?? Definition()); tx.Commit(); return handle; }
        void InternalBalance(ColonyFrameSnapshot actual, ColonyFrameSnapshot before)
        {
            Same(actual.Totals(), before.Totals()); Same(actual.InitialInventory, before.InitialInventory, true);
            Same(actual.SuppliedInventory, before.SuppliedInventory, true); Same(actual.RemovedInventory, before.RemovedInventory, true);
            check(actual.ExternalHeatJoules == before.ExternalHeatJoules);
        }

        test("owned containers publish empty immutable definitions with exact allocation handles", () =>
        {
            var state = create(); var before = state.CaptureSnapshot(); var bytes = state.Save();
            using var tx = state.BeginTransaction(); var definition = Definition(2, 3, FluidContainerPhases.Gas);
            var handle = tx.CreateContainer("reservoir:1", definition);
            check(handle.ColonyId == before.ColonyId && handle.Generation == 1 && handle.AllocationToken != Guid.Empty);
            check(tx.SampleContainer(handle).Inventory.TotalMassKg == 0 && !before.TrySampleContainer(handle, out _));
            check(bytes.SequenceEqual(state.Save())); reject(() => tx.CreateContainer("reservoir:1", definition));
            var frame = tx.Commit(); var container = frame.SampleContainer(handle);
            check(frame.Containers.Count == 1 && ReferenceEquals(container.Definition, definition) && container.Definition.MaximumMassKg == 3);
            check(before.Containers.Count == 0 && frame.ColonyId == before.ColonyId);
            reject(() => ((IDictionary<string, FluidContainerSnapshot>)frame.Containers).Add("injected", container));
            reject(() => frame.SampleContainer(default)); InternalBalance(frame, before);
        });
        test("ordered cell container and container transfers conserve all owned inventories in one frame", () =>
        {
            var state = create();
            using (var seed = state.BeginTransaction())
            {
                seed.Inject(gas, catalog().CreateInventory(new Dictionary<string, double> { ["nitrogen"] = 1e-16 }, 300, 3, -4,
                    new Dictionary<int, double> { [999] = 8 }, 7), "trace-seed"); seed.Commit();
            }
            var before = state.CaptureSnapshot(); var source = before.SampleCell(gas).Inventory;
            using var tx = state.BeginTransaction(); var a = tx.CreateContainer("a", Definition()); var b = tx.CreateContainer("b", Definition());
            tx.TransferAllCellToContainer(gas, a); Same(tx.SampleContainer(a).Inventory, source, true);
            tx.TransferContainerToContainer(a, b, 0.5); tx.TransferContainerToCell(b, empty, 0.25);
            check(tx.SampleCell(gas).Inventory.TotalMassKg == 0 && tx.SampleContainer(a).Inventory.MassesKg["nitrogen"] > 0);
            check(tx.SampleCell(empty).Inventory.MassesKg["nitrogen"] > 0 && tx.SampleContainer(b).Inventory.MassesKg["nitrogen"] > 0);
            check(!before.TrySampleContainer(a, out _) && state.CaptureSnapshot().SampleCell(empty).Inventory.TotalMassKg == 0);
            var frame = tx.CommitFrame(9, 1.8); InternalBalance(frame, before);
            check(frame.CompletedFrame == 9 && frame.WorldTotals().TotalMassKg < frame.Totals().TotalMassKg);
            Same(frame.SampleCell(gas).Inventory, FluidInventory.Empty, true);
        });
        test("failed container capacity phase and geometry admission leave prior staged changes intact", () =>
        {
            var state = create(); using var tx = state.BeginTransaction(); var handle = tx.CreateContainer("small", Definition(capacity: 0.25, phases: FluidContainerPhases.Gas));
            reject(() => tx.TransferCellToContainer(gas, handle, 0.5));
            check(tx.SampleCell(gas).Inventory.TotalMassKg == 2 && tx.SampleContainer(handle).Inventory.TotalMassKg == 0);
            tx.TransferCellToContainer(gas, handle, 0.25);
            var staged = tx.SampleContainer(handle).Inventory;
            reject(() => tx.TransferCellToContainer(gas, handle, 0.25)); reject(() => tx.TransferCellToContainer(water, handle, 0.01));
            reject(() => tx.TransferAllContainerToCell(handle, solid)); reject(() => tx.RetireContainerToCell(handle, solid));
            Same(tx.SampleContainer(handle).Inventory, staged, true); check(tx.SampleCell(gas).Inventory.TotalMassKg == 1.75);
            var frame = tx.Commit(); check(frame.SampleContainer(handle).Inventory.TotalMassKg == 0.25);
        });
        test("every positive trace participates in container phase admission", () =>
        {
            var state = create(); using var tx = state.BeginTransaction();
            tx.Inject(gas, Parcel("water", 1e-16), "liquid-trace"); tx.Inject(water, Parcel("nitrogen", 1e-16), "gas-trace");
            var gasOnly = tx.CreateContainer("gas", Definition(phases: FluidContainerPhases.Gas));
            var liquidOnly = tx.CreateContainer("liquid", Definition(phases: FluidContainerPhases.Liquid));
            reject(() => tx.TransferAllCellToContainer(gas, gasOnly)); reject(() => tx.TransferAllCellToContainer(water, liquidOnly));
            check(tx.SampleCell(gas).Inventory.MassesKg["water"] == 1e-16 && tx.SampleCell(water).Inventory.MassesKg["nitrogen"] == 1e-16);
            check(tx.SampleContainer(gasOnly).Inventory.TotalMassKg == 0 && tx.SampleContainer(liquidOnly).Inventory.TotalMassKg == 0);
        });
        test("nominal liquid volume and gameplay mass capacity remain independent of gas pressure", () =>
        {
            var state = create(); using var tx = state.BeginTransaction();
            var liquid = tx.CreateContainer("nominal-full-liquid", Definition(0.1, 5000));
            tx.TransferAllCellToContainer(water, liquid); Near(tx.SampleContainer(liquid).LiquidVolumeFraction, 1);
            reject(() => tx.TransferCellToContainer(gas, liquid, "oxygen", 1e-16));
            check(tx.SampleContainer(liquid).Inventory.TotalMassKg == 100);
            var gasTank = tx.CreateContainer("high-pressure", Definition(0.001, 10, FluidContainerPhases.Gas));
            tx.TransferAllCellToContainer(gas, gasTank);
            check(tx.SampleContainer(gasTank).GasPressurePa > 1000000 && tx.SampleContainer(gasTank).Inventory.TotalMassKg == 2);
            var frame = tx.Commit(); check(frame.SampleContainer(liquid).Definition.MaximumMassKg == 5000);
        });
        test("selective transfer uses its source container volume rather than world cell volume", () =>
        {
            var state = create(); using var tx = state.BeginTransaction(); var big = tx.CreateContainer("big", Definition(3, 3000));
            var other = tx.CreateContainer("other", Definition(1, 1000));
            tx.Inject(water, Parcel("water", 900), "fill-first-cell"); tx.TransferAllCellToContainer(water, big);
            tx.Inject(water, Parcel("water", 1000), "fill-second-cell"); tx.TransferAllCellToContainer(water, big);
            check(tx.SampleContainer(big).Inventory.TotalMassKg == 2000);
            tx.TransferContainerToCell(big, empty, "water", 200); tx.TransferContainerToContainer(big, other, "water", 500);
            Near(tx.SampleContainer(big).TemperatureKelvin, 300); check(tx.SampleContainer(big).Inventory.TotalMassKg == 1300);
            check(tx.SampleCell(empty).Inventory.TotalMassKg == 200 && tx.SampleContainer(other).Inventory.TotalMassKg == 500);
            tx.Commit(); check(state.Save().SequenceEqual(ColonyFluidState.Restore(state.Save(), catalog()).Save()));
        });
        test("selective internal container transfer preserves other species and apportions energy momentum and metadata", () =>
        {
            var state = create();
            using (var seed = state.BeginTransaction())
            {
                seed.Extract(gas, 2, "replace-source"); seed.Inject(gas, catalog().CreateInventory(
                    new Dictionary<string, double> { ["oxygen"] = 2, ["nitrogen"] = 1e-16 }, 300, 3, -4,
                    new Dictionary<int, double> { [777] = 8 }, 6), "moving-mixture"); seed.Commit();
            }
            var before = state.CaptureSnapshot(); using var tx = state.BeginTransaction();
            var a = tx.CreateContainer("a", Definition()); var b = tx.CreateContainer("b", Definition()); tx.TransferAllCellToContainer(gas, a);
            tx.TransferContainerToContainer(a, b, "oxygen", 1);
            check(tx.SampleContainer(a).Inventory.MassesKg["nitrogen"] == 1e-16 && !tx.SampleContainer(b).Inventory.MassesKg.ContainsKey("nitrogen"));
            var moved = tx.SampleContainer(b).Inventory; check(moved.MassesKg["oxygen"] == 1 && moved.MomentumX == 3 && moved.MomentumY == -4);
            check(moved.DiseaseCounts[777] == 4 && moved.RadiationQuantity == 3);
            tx.TransferContainerToCell(b, empty, "oxygen", 0.5); tx.TransferCellToContainer(empty, a, "oxygen", 0.5);
            InternalBalance(tx.Commit(), before);
        });
        test("retirement evacuates conservatively or rejects before losing any active inventory", () =>
        {
            var state = create(); var before = state.CaptureSnapshot(); using var tx = state.BeginTransaction();
            var source = tx.CreateContainer("source", Definition()); var blocked = tx.CreateContainer("blocked", Definition(capacity: 0.25));
            tx.TransferAllCellToContainer(gas, source); var inventory = tx.SampleContainer(source).Inventory;
            reject(() => tx.RetireContainer(source)); reject(() => tx.RetireContainerToContainer(source, blocked));
            Same(tx.SampleContainer(source).Inventory, inventory, true);
            var destination = tx.CreateContainer("destination", Definition()); tx.RetireContainerToContainer(source, destination);
            reject(() => tx.SampleContainer(source)); Same(tx.SampleContainer(destination).Inventory, inventory, true);
            var replacement = tx.CreateContainer("source", Definition()); check(replacement.Generation > source.Generation && replacement.AllocationToken != source.AllocationToken);
            tx.RetireContainer(replacement); tx.RetireContainerToCell(destination, empty); InternalBalance(tx.Commit(), before);
            check(state.CaptureSnapshot().Containers.Count == 1 && state.CaptureSnapshot().SampleCell(empty).Inventory.TotalMassKg == 2);
        });
        test("zero-mass disease and radiation can evacuate but cannot silently retire", () =>
        {
            var state = createMetadataOnly(); var before = state.CaptureSnapshot(); var metadata = before.SampleCell(gas).Inventory;
            check(metadata.TotalMassKg == 0 && metadata.DiseaseCounts.Count != 0 && metadata.RadiationQuantity != 0);
            using var tx = state.BeginTransaction(); var a = tx.CreateContainer("a", Definition(capacity: 0)); var b = tx.CreateContainer("b", Definition(capacity: 0));
            tx.TransferAllCellToContainer(gas, a); Same(tx.SampleContainer(a).Inventory, metadata, true);
            reject(() => tx.RetireContainer(a)); tx.RetireContainerToContainer(a, b); reject(() => tx.RetireContainer(b));
            tx.RetireContainerToCell(b, empty); var after = tx.Commit();
            check(after.Containers.Count == 0); Same(after.SampleCell(empty).Inventory, metadata, true); InternalBalance(after, before);
        });
        test("aborted provisional container handles never alias later same-ID allocations", () =>
        {
            var state = create(); FluidContainerHandle leaked;
            using (var aborted = state.BeginTransaction()) leaked = aborted.CreateContainer("same-id", Definition());
            var active = Register(state, "same-id");
            check(leaked.Generation == active.Generation && leaked.ColonyId == active.ColonyId && leaked.AllocationToken != active.AllocationToken);
            check(!state.CaptureSnapshot().TrySampleContainer(leaked, out _) && state.CaptureSnapshot().TrySampleContainer(active, out _));
            using var tx = state.BeginTransaction(); reject(() => tx.SampleContainer(leaked)); reject(() => tx.TransferCellToContainer(gas, leaked, 1));
        });
        test("stale prepared allocation handles cannot ABA after a competing world-only commit", () =>
        {
            var state = create(); using var stale = state.BeginTransaction(); var leaked = stale.CreateContainer("same-id", Definition());
            using var candidate = stale.PrepareFrame(1, 0.2);
            using (var fresh = state.BeginTransaction()) { fresh.Transfer(gas, empty, 0.25); fresh.Commit(); }
            reject(() => candidate.Commit()); var active = Register(state, "same-id");
            check(leaked.Generation == active.Generation && leaked.AllocationToken != active.AllocationToken);
            check(!state.CaptureSnapshot().TrySampleContainer(leaked, out _) && !candidate.Snapshot.TrySampleContainer(active, out _));
        });
        test("container handles reject another colony while saved same-colony identities survive restore", () =>
        {
            var first = create(); var second = create(); var a = Register(first, "same-id"); var b = Register(second, "same-id");
            check(a.ColonyId != b.ColonyId && a.Generation == b.Generation);
            check(!first.CaptureSnapshot().TrySampleContainer(b, out _));
            using (var tx = first.BeginTransaction()) reject(() => tx.SampleContainer(b));
            var restored = ColonyFluidState.Restore(first.Save(), catalog()); check(restored.CaptureSnapshot().SampleContainer(a).Handle.Equals(a));
            using var resumed = restored.BeginTransaction(); resumed.TransferCellToContainer(gas, a, 0.5); resumed.Commit();
        });
        test("old container snapshots and competing transaction failures preserve a coherent paired publication", () =>
        {
            var state = create(); var handle = Register(state); var before = state.CapturePublication();
            using var stale = state.BeginTransaction(); stale.TransferCellToContainer(gas, handle, 1);
            using (var fresh = state.BeginTransaction()) { fresh.TransferCellToContainer(gas, handle, 0.5); fresh.Commit(); }
            var bytes = state.Save(); reject(() => stale.Commit()); check(bytes.SequenceEqual(state.Save()));
            check(before.Snapshot.SampleContainer(handle).Inventory.TotalMassKg == 0);
            using var next = state.BeginTransaction(); next.TransferContainerToCell(handle, empty, 0.25);
            using var prepared = next.PrepareFrame(4, 0.8); prepared.ReserveWriter();
            check(state.CaptureSnapshot().SampleCell(empty).Inventory.TotalMassKg == 0);
            var companion = new object(); var paired = prepared.Commit(companion, retainReservationUntilDisposed: true);
            check(ReferenceEquals(paired.Companion, companion) && paired.Snapshot.SampleContainer(handle).Inventory.TotalMassKg == 0.25 && paired.Snapshot.SampleCell(empty).Inventory.TotalMassKg == 0.25);
        });
        test("v3 container checkpoint roundtrip and continuation preserve every allocation definition and conserved inventory", () =>
        {
            var state = create(); FluidContainerHandle a, b;
            using (var tx = state.BeginTransaction())
            {
                a = tx.CreateContainer("alpha", Definition(2, 10, FluidContainerPhases.Gas)); b = tx.CreateContainer("bravo", Definition(0.5, 200, FluidContainerPhases.Liquid));
                tx.TransferAllCellToContainer(gas, a); tx.TransferAllCellToContainer(water, b); tx.CommitFrame(8, 1.6);
            }
            var bytes = state.Save(); var culture = CultureInfo.CurrentCulture;
            try
            {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("de-DE");
                var restored = ColonyFluidState.Restore(bytes, catalog()); check(bytes.SequenceEqual(restored.Save()));
                var sample = restored.CaptureSnapshot().SampleContainer(a); check(sample.Handle.Equals(a) && sample.Definition.VolumeCubicMetres == 2 && sample.Definition.MaximumMassKg == 10 && sample.Definition.AllowedPhases == FluidContainerPhases.Gas);
                Same(sample.Inventory, state.CaptureSnapshot().SampleContainer(a).Inventory, true);
                foreach (var authority in new[] { state, restored })
                { using var tx = authority.BeginTransaction(); tx.TransferContainerToCell(a, empty, 0.5); tx.TransferContainerToCell(b, water, "water", 25); tx.CommitFrame(9, 1.8); }
                check(state.Save().SequenceEqual(restored.Save())); InternalBalance(restored.CaptureSnapshot(), state.CaptureSnapshot());
            }
            finally { CultureInfo.CurrentCulture = culture; }
        });
        test("retired container generation high-water survives an empty-table checkpoint", () =>
        {
            var state = create(); var old = Register(state);
            using (var tx = state.BeginTransaction()) { tx.RetireContainer(old); tx.Commit(); }
            var restored = ColonyFluidState.Restore(state.Save(), catalog()); var current = Register(restored);
            check(current.Generation > old.Generation && current.AllocationToken != old.AllocationToken && !restored.CaptureSnapshot().TrySampleContainer(old, out _));
        });
        test("container full transfers and checkpoint preserve minimum-positive traces and fractional disease quantities", () =>
        {
            var state = create(); FluidContainerHandle a, b;
            using (var tx = state.BeginTransaction())
            {
                tx.Extract(gas, 2, "replace-trace-source");
                tx.Inject(gas, catalog().CreateInventory(new Dictionary<string, double> { ["oxygen"] = 2, ["nitrogen"] = double.Epsilon },
                    300, diseaseCounts: new Dictionary<int, double> { [314] = 0.125 }, radiationQuantity: 0.25), "minimum-positive-trace");
                a = tx.CreateContainer("a", Definition()); b = tx.CreateContainer("b", Definition());
                tx.TransferAllCellToContainer(gas, a); tx.TransferAllContainerToContainer(a, b); tx.Commit();
            }
            var restored = ColonyFluidState.Restore(state.Save(), catalog()); var inventory = restored.CaptureSnapshot().SampleContainer(b).Inventory;
            check(inventory.MassesKg["nitrogen"] == double.Epsilon && inventory.DiseaseCounts[314] == 0.125 && inventory.RadiationQuantity == 0.25);
            using (var tx = restored.BeginTransaction())
            {
                tx.TransferContainerToContainer(b, a, "oxygen", 1);
                check(tx.SampleContainer(a).Inventory.DiseaseCounts[314] == 0.0625 && tx.SampleContainer(b).Inventory.DiseaseCounts[314] == 0.0625);
                check(tx.SampleContainer(b).Inventory.MassesKg["nitrogen"] == double.Epsilon);
                tx.RetireContainerToCell(b, empty); tx.RetireContainerToCell(a, gas); tx.Commit();
            }
            Same(restored.CaptureSnapshot().Totals(), state.CaptureSnapshot().Totals(), true);
        });
        test("exhausted persisted container allocation counter rejects before changing the transaction", () =>
        {
            var state = create(); var bytes = state.Save(); var section = ContainerOffsets(bytes).section;
            BinaryPrimitives.WriteInt64LittleEndian(bytes.AsSpan(section + 16), long.MaxValue); Rehash(bytes);
            var restored = ColonyFluidState.Restore(bytes, catalog()); var before = restored.Save(); using var tx = restored.BeginTransaction();
            reject(() => tx.CreateContainer("cannot-allocate", Definition())); reject(() => tx.CreateContainer("cannot-allocate", Definition()));
            check(before.SequenceEqual(restored.Save())); tx.Transfer(gas, empty, 0.25); tx.Commit();
            check(restored.CaptureSnapshot().Containers.Count == 0 && restored.CaptureSnapshot().SampleCell(empty).Inventory.TotalMassKg == 0.25);
        });
        test("invalid container policies handles numeric transfers and same-allocation transfers reject without mutation", () =>
        {
            reject(() => Definition(0)); reject(() => Definition(double.NaN)); reject(() => Definition(capacity: double.PositiveInfinity));
            reject(() => Definition(capacity: -1)); reject(() => Definition(phases: 0)); reject(() => Definition(phases: (FluidContainerPhases)8));
            var state = create(); using var tx = state.BeginTransaction(); var a = tx.CreateContainer("a", Definition()); var b = tx.CreateContainer("b", Definition());
            reject(() => tx.CreateContainer(" ", Definition())); reject(() => tx.CreateContainer("a", Definition()));
            foreach (var bad in new[] { 0, -1, double.NaN, double.PositiveInfinity }) reject(() => tx.TransferCellToContainer(gas, a, bad));
            reject(() => tx.TransferCellToContainer(gas, a, (string)null!, 1)); reject(() => tx.TransferCellToContainer(gas, a, "unknown", 1));
            tx.TransferCellToContainer(gas, a, 1); var source = tx.SampleContainer(a).Inventory;
            reject(() => tx.TransferContainerToContainer(a, a, 0.5)); reject(() => tx.RetireContainerToContainer(a, a));
            reject(() => tx.TransferContainerToContainer(a, b, "nitrogen", 1)); reject(() => tx.TransferContainerToCell(default, empty, 1));
            Same(tx.SampleContainer(a).Inventory, source, true); check(tx.SampleContainer(b).Inventory.TotalMassKg == 0);
        });
        test("checksum-valid v3 container metadata corruption and legacy v2 checkpoints reject strictly", () =>
        {
            var state = create();
            using (var tx = state.BeginTransaction())
            {
                var a = tx.CreateContainer("alpha", Definition(2, 10)); var b = tx.CreateContainer("bravo", Definition(2, 200));
                tx.TransferAllCellToContainer(gas, a); tx.TransferAllCellToContainer(water, b); tx.Commit();
            }
            var bytes = state.Save(); var offsets = ContainerOffsets(bytes); var first = offsets.records[0]; var last = offsets.records[1];
            void Bad(Action<byte[]> change) { var damaged = (byte[])bytes.Clone(); change(damaged); Rehash(damaged); reject(() => ColonyFluidState.Restore(damaged, catalog())); }
            Bad(data => data.AsSpan(offsets.section, 16).Clear());
            Bad(data => BinaryPrimitives.WriteInt64LittleEndian(data.AsSpan(offsets.section + 16), 1));
            Bad(data => BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(offsets.section + 24), 65537));
            Bad(data => BinaryPrimitives.WriteInt64LittleEndian(data.AsSpan(first.generation), 0));
            Bad(data => data.AsSpan(first.token, 16).Clear());
            Bad(data => data.AsSpan(first.token, 16).CopyTo(data.AsSpan(last.token, 16)));
            Bad(data => BinaryPrimitives.WriteInt64LittleEndian(data.AsSpan(last.generation), 1));
            Bad(data => data.AsSpan(first.identity, 5).CopyTo(data.AsSpan(last.identity, 5)));
            Bad(data => Double(data, last.volume, double.NaN)); Bad(data => Double(data, last.volume, 0.05));
            Bad(data => Double(data, last.capacity, 99)); Bad(data => Double(data, first.capacity, -1));
            Bad(data => BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(first.phases), 0));
            Bad(data => BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(last.phases), (int)FluidContainerPhases.Gas));
            Bad(data => Double(data, last.firstMass, 150)); // Admissible volume/capacity; whole-colony ledger must still reject.
            var legacy = new byte[offsets.section + 32]; bytes.AsSpan(0, offsets.section).CopyTo(legacy);
            BinaryPrimitives.WriteInt32LittleEndian(legacy.AsSpan(8), 2); Rehash(legacy); reject(() => ColonyFluidState.Restore(legacy, catalog()));
            check(bytes.SequenceEqual(state.Save()));
        });
    }

    private readonly record struct RecordOffsets(int identity, int generation, int token, int volume, int capacity, int phases, int firstMass);
    private static (int section, List<RecordOffsets> records) ContainerOffsets(byte[] data)
    {
        using var reader = new BinaryReader(new MemoryStream(data)); reader.BaseStream.Position = 12;
        SkipIdentity(reader); reader.BaseStream.Position += 32 + 12 + 8 + 8 + 8 + 8;
        var solids = reader.ReadInt32(); reader.BaseStream.Position += solids * 4; var worlds = reader.ReadInt32();
        for (var i = 0; i < worlds; ++i)
        {
            reader.ReadInt32(); var width = reader.ReadInt32(); var height = reader.ReadInt32(); reader.BaseStream.Position += 8 + width * height * 4;
        }
        SkipInventory(reader); SkipInventory(reader); SkipInventory(reader); reader.ReadDouble();
        var cells = reader.ReadInt32(); for (var i = 0; i < cells; ++i) { reader.ReadInt32(); SkipInventory(reader); }
        var section = (int)reader.BaseStream.Position; reader.BaseStream.Position += 24; var count = reader.ReadInt32(); var result = new List<RecordOffsets>();
        for (var i = 0; i < count; ++i)
        {
            var identity = (int)reader.BaseStream.Position + 4; SkipIdentity(reader); var generation = (int)reader.BaseStream.Position;
            reader.BaseStream.Position += 8 + 16; var volume = (int)reader.BaseStream.Position; reader.BaseStream.Position += 8 + 8 + 4;
            var inventory = reader.BaseStream.Position; var massCount = reader.ReadInt32();
            var firstMass = -1; if (massCount != 0) { SkipIdentity(reader); firstMass = (int)reader.BaseStream.Position; }
            reader.BaseStream.Position = inventory; SkipInventory(reader);
            result.Add(new RecordOffsets(identity, generation, generation + 8, volume, volume + 8, volume + 16, firstMass));
        }
        return (section, result);
    }
    private static void SkipIdentity(BinaryReader reader) { var bytes = reader.ReadInt32(); reader.BaseStream.Position += bytes; }
    private static void SkipInventory(BinaryReader reader)
    {
        var count = reader.ReadInt32(); for (var i = 0; i < count; ++i) { SkipIdentity(reader); reader.ReadDouble(); }
        reader.BaseStream.Position += 32; var diseases = reader.ReadInt32(); reader.BaseStream.Position += diseases * 12;
    }
    private static void Double(byte[] data, int offset, double value) => BinaryPrimitives.WriteInt64LittleEndian(data.AsSpan(offset), BitConverter.DoubleToInt64Bits(value));
    private static void Rehash(byte[] data) { using var sha = SHA256.Create(); sha.ComputeHash(data, 0, data.Length - 32).CopyTo(data, data.Length - 32); }
}
