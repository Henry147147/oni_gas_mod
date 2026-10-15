using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using MixedAtmospheres.Oni;

var passed = 0;
void Test(string name, Action test) { test(); ++passed; Console.WriteLine("PASS " + name); }
void Check(bool value) { if (!value) throw new InvalidOperationException("Assertion failed."); }
void Reject(Action action)
{
    try { action(); }
    catch (Exception error) when (error is InvalidDataException || error is ArgumentException || error is InvalidOperationException || error is NotSupportedException) { return; }
    throw new InvalidOperationException("Expected checkpoint rejection.");
}

Test("exact Core v3 checkpoint restores every trace and fractional metadata with full handles", () =>
{
    var f = new Fixture(); var core = f.State.Save();
    var checkpoint = f.Create(); var parsed = f.Parse(checkpoint.CopyEncodedData());
    Check(parsed.CoreData.SequenceEqual(core) && parsed.RestoreAuthority().Save().SequenceEqual(core));
    Check(parsed.Source.ColonyId == f.State.CaptureSnapshot().ColonyId && parsed.Source.Revision == 1);
    Check(parsed.Source.Inventories.Count == 0 && parsed.Source.Containers.Count == 3 && parsed.Owners.Count == 2);
    Check(parsed.Source.SampleContainer(f.A).Inventory.MassesKg["chlorine"] == 1e-200);
    Check(parsed.Source.SampleContainer(f.A).Inventory.DiseaseCounts[123] == .375 && parsed.Source.SampleContainer(f.A).Inventory.RadiationQuantity == .125);
    Check(parsed.Source.SampleContainer(f.A).Inventory.MomentumX == 12 && parsed.Source.SampleContainer(f.A).Inventory.MomentumY == -16);
    Check(parsed.Owners[0].SavedInstanceId == 0 && parsed.Owners[0].Container.Equals(f.A));
    Check(parsed.Owners[1].Container.Equals(f.B) && parsed.Source.SampleContainer(f.B).Inventory.TotalMassKg == 0);
    Check(parsed.Owners[0].ConstructionMassKg == (double)400.125f);
});
Test("owner enumeration order is canonical and returned state does not mutate checkpoint", () =>
{
    var f = new Fixture(); var checkpoint = f.Create(); var encoded = checkpoint.CopyEncodedData();
    Check(encoded.SequenceEqual(f.Create(f.Owners.Reverse()).CopyEncodedData()));
    encoded[0] ^= 1; Check(f.Parse(checkpoint.CopyEncodedData()).Source.Containers.Count == 3);
    using (var tx = checkpoint.RestoreAuthority().BeginTransaction()) { tx.TransferContainerToContainer(f.A, f.B, 1); tx.Commit(); }
    Check(checkpoint.Source.SampleContainer(f.B).Inventory.TotalMassKg == 0);
    Reject(() => ((IList<OwnedReservoirCheckpointOwner>)checkpoint.Owners)[0] = f.Owners[1]);
});
Test("build contract catalog and native colony mismatch are rejected", () =>
{
    var f = new Fixture(); var data = f.Create().CopyEncodedData();
    Reject(() => OwnedReservoirCheckpointV1.Parse(data, f.Catalog, new OwnedReservoirCheckpointCompatibility(744826, Fixture.HashA, Fixture.HashB), f.NativeColony));
    Reject(() => OwnedReservoirCheckpointV1.Parse(data, f.Catalog, new OwnedReservoirCheckpointCompatibility(744825, Fixture.HashB, Fixture.HashA), f.NativeColony));
    Reject(() => OwnedReservoirCheckpointV1.Parse(data, f.Catalog, f.Compatibility, Guid.NewGuid()));
    Reject(() => OwnedReservoirCheckpointV1.Parse(data, Fixture.CreateCatalog(659), f.Compatibility, f.NativeColony));
});
Test("declared nonidle and incomplete owner coverage are rejected", () =>
{
    var f = new Fixture(); Reject(() => f.Create(idle: new OwnedReservoirCheckpointIdleState(outboxCount: 1)));
    Reject(() => f.Create(new[] { f.Owners[0] })); Reject(() => f.Create(f.Owners.Concat(new[] { f.Owners[0] })));
    using (var tx = f.State.BeginTransaction()) { tx.TransferContainerToCell(f.A, 2, .25); tx.Commit(); }
    Reject(() => f.Create());
});
Test("every declared pending fault reader notification and recovery state is rejected", () =>
{
    var f = new Fixture();
    foreach (var idle in new[]
    {
        new OwnedReservoirCheckpointIdleState(pendingBatchCount: 1), new OwnedReservoirCheckpointIdleState(outboxCount: 1),
        new OwnedReservoirCheckpointIdleState(snapshotReaderCount: 1), new OwnedReservoirCheckpointIdleState(orphanCount: 1),
        new OwnedReservoirCheckpointIdleState(preparing: true), new OwnedReservoirCheckpointIdleState(notifying: true),
        new OwnedReservoirCheckpointIdleState(faulted: true), new OwnedReservoirCheckpointIdleState(worldRecoveryRequired: true),
        new OwnedReservoirCheckpointIdleState(consumptionAcknowledged: false)
    }) Reject(() => f.Create(idle: idle));
    Reject(() => new OwnedReservoirCheckpointIdleState(pendingBatchCount: -1));
    Reject(() => new OwnedReservoirCheckpointIdleState(outboxCount: -1));
    Reject(() => new OwnedReservoirCheckpointIdleState(snapshotReaderCount: -1));
    Reject(() => new OwnedReservoirCheckpointIdleState(orphanCount: -1));
});
Test("world metadata without mass cannot be hidden in a containers-only checkpoint", () =>
{
    var f = new Fixture(); using (var tx = f.State.BeginTransaction())
    { tx.Inject(2, new FluidInventory(new Dictionary<string, double>(), 0, diseaseCounts: new Dictionary<int, double> { [999] = 1e-200 }), "metadata-only-world"); tx.Commit(); }
    Check(f.State.CaptureSnapshot().SampleCell(2).Inventory.TotalMassKg == 0); Reject(() => f.Create());
});
Test("every nonempty allocation including tiny mass and massless metadata requires an owner", () =>
{
    foreach (var metadataOnly in new[] { false, true })
    {
        var f = new Fixture(); using (var tx = f.State.BeginTransaction())
        {
            var extra = tx.CreateContainer("unbound-nonempty", new FluidContainerDefinition(1, 1000, FluidContainerPhases.Gas));
            if (metadataOnly)
            {
                tx.Inject(2, new FluidInventory(new Dictionary<string, double>(), 0, radiationQuantity: 1e-200), "metadata-only-extra");
                tx.TransferAllCellToContainer(2, extra);
            }
            else tx.TransferContainerToContainer(f.A, extra, "chlorine", 1e-200);
            tx.Commit();
        }
        Check(f.State.CaptureSnapshot().Inventories.Count == 0); Reject(() => f.Create());
    }
});
Test("duplicate saved IDs owner tokens allocations and anchors reject before encoding", () =>
{
    var f = new Fixture();
    Reject(() => f.Create(new[] { f.Owners[0], f.Owner(f.B, 0, 7) }));
    Reject(() => f.Create(new[] { f.Owners[0], f.Owner(f.B, 1, 7, f.Owners[0].PersistentOwnerToken) }));
    Reject(() => f.Create(new[] { f.Owners[0], f.Owner(f.A, 1, 7) }));
    Reject(() => f.Create(new[] { f.Owners[0], f.Owner(f.B, 1, 0) }));
    Reject(() => f.Create(new[] { f.Owners[0], new Fixture().Owners[1] }));
    Reject(() => f.Create(new OwnedReservoirCheckpointOwner[] { f.Owners[0], null! }));
});
Test("owner declarations require exact admission geometry and native float mass", () =>
{
    var f = new Fixture();
    Reject(() => f.Owner(f.A, -1, 0)); Reject(() => f.Owner(f.A, 0, 0, Guid.Empty));
    Reject(() => f.Owner(default, 0, 0)); Reject(() => f.Owner(f.A, 0, 0, prefab: "LiquidReservoir"));
    Reject(() => f.Owner(f.A, 0, 0, orientation: 4)); Reject(() => f.Owner(f.A, 0, 0, orientation: 7));
    Reject(() => f.Owner(f.A, 0, -1)); Reject(() => f.Owner(f.A, 0, 0, world: -1));
    foreach (var value in new[] { double.NaN, double.PositiveInfinity, -1, .1, double.MaxValue })
        Reject(() => f.Owner(f.A, 0, 0, constructionMass: value));
    foreach (var value in new[] { 0, -1, double.NaN, double.PositiveInfinity }) Reject(() => f.Owner(f.A, 0, 0, volume: value));
    Reject(() => f.Owner(f.A, 0, 0, capacity: 999)); Reject(() => f.Owner(f.A, 0, 0, phases: FluidContainerPhases.Gas | FluidContainerPhases.Liquid));
    Reject(() => f.Create(new[] { f.Owner(f.A, 0, 0, volume: 11), f.Owners[1] }));
    Reject(() => f.Create(new[] { f.Owner(f.A, 0, 31, world: 9), f.Owners[1] }));
    Reject(() => f.Create(new[] { f.Owner(f.A, 0, 32), f.Owners[1] }));
    var exactFloat = (double).1f; var zeroMass = f.Owner(f.A, 0, 0, constructionMass: 0);
    Check(f.Create(new[] { f.Owner(f.A, 0, 0, constructionMass: exactFloat), f.Owners[1] }).Owners[0].ConstructionMassKg == exactFloat);
    Check(f.Create(new[] { zeroMass, f.Owners[1] }).Owners[0].ConstructionMassKg == 0);
});
Test("Core bytes and owner arrays are cloned before caller-controlled enumeration", () =>
{
    var f = new Fixture(); var data = f.State.Save(); var expected = (byte[])data.Clone();
    IEnumerable<OwnedReservoirCheckpointOwner> Rows()
    {
        yield return f.Owners[0]; data[0] ^= 1; yield return f.Owners[1];
    }
    var checkpoint = OwnedReservoirCheckpointV1.Create(data, f.Catalog, f.Compatibility, f.NativeColony, Rows(), new OwnedReservoirCheckpointIdleState());
    Check(checkpoint.CoreData.SequenceEqual(expected)); var original = f.Owners[0];
    var array = new[] { original, f.Owners[1] }; var copied = f.Create(array); array[0] = f.Owners[1];
    Check(ReferenceEquals(copied.Owners[0], original));
    using (var tx = f.State.BeginTransaction()) { tx.TransferContainerToContainer(f.A, f.B, 1); tx.Commit(); }
    Check(copied.Source.SampleContainer(f.B).Inventory.TotalMassKg == 0 && copied.CoreData.SequenceEqual(expected));
});
Test("contract identity inputs are explicit bounded hashes and normalized without policy inference", () =>
{
    foreach (var bad in new[] { "", "abc", new string('0', 64), new string('x', 64), Fixture.HashA + "0", null! })
        Reject(() => new OwnedReservoirCheckpointCompatibility(744825, bad, Fixture.HashB));
    Reject(() => new OwnedReservoirCheckpointCompatibility(0, Fixture.HashA, Fixture.HashB));
    var f = new Fixture(); var data = f.Create().CopyEncodedData();
    var upper = new OwnedReservoirCheckpointCompatibility(744825, Fixture.HashA.ToUpperInvariant(), Fixture.HashB.ToUpperInvariant());
    Check(OwnedReservoirCheckpointV1.Parse(data, f.Catalog, upper, f.NativeColony).Compatibility.AssemblyCSharpSha256 == Fixture.HashA);
    Reject(() => OwnedReservoirCheckpointV1.Create(f.State.Save(), f.Catalog, f.Compatibility, Guid.Empty, f.Owners, new OwnedReservoirCheckpointIdleState()));
    Reject(() => OwnedReservoirCheckpointV1.Parse(data, f.Catalog, f.Compatibility, Guid.Empty));
});
Test("checksum truncation unsupported version declared length and trailing data reject", () =>
{
    var f = new Fixture(); var data = f.Create().CopyEncodedData(); var corrupt = (byte[])data.Clone(); corrupt[^1] ^= 1; Reject(() => f.Parse(corrupt));
    foreach (var length in new[] { 0, 1, 199, data.Length - 1 }) Reject(() => f.Parse(data.AsSpan(0, length).ToArray()));
    void Changed(Action<byte[], Layout> mutate)
    { var modified = (byte[])data.Clone(); mutate(modified, new Layout(modified)); Rehash(modified); Reject(() => f.Parse(modified)); }
    Changed((bytes, _) => bytes[0] ^= 1); Changed((bytes, _) => Int(bytes, 8, 2));
    foreach (var length in new[] { 0L, -1L, long.MaxValue, data.Length - 1L, data.Length + 1L }) Changed((bytes, _) => Long(bytes, 12, length));
    var trailing = new byte[data.Length + 1]; data.AsSpan(0, data.Length - 32).CopyTo(trailing); Long(trailing, 12, trailing.Length); Rehash(trailing);
    Reject(() => f.Parse(trailing));
});
Test("recomputed checksum cannot authorize unknown idle protocol flags counts or owner count", () =>
{
    var f = new Fixture(); var data = f.Create().CopyEncodedData(); var layout = new Layout(data);
    foreach (var offset in Enumerable.Range(0, 6).Select(i => layout.Idle + 4 * i).Append(layout.OwnerCount))
    {
        var modified = (byte[])data.Clone(); Int(modified, offset, offset == layout.Idle ? 2 : 17); Rehash(modified); Reject(() => f.Parse(modified));
    }
});
Test("nested Core length checksum and format are validated independently", () =>
{
    var f = new Fixture(); var data = f.Create().CopyEncodedData(); var layout = new Layout(data);
    foreach (var length in new[] { -1L, 0, 79, long.MaxValue, layout.CoreLength - 1L, layout.CoreLength + 1L })
    { var modified = (byte[])data.Clone(); Long(modified, layout.CoreLengthField, length); Rehash(modified); Reject(() => f.Parse(modified)); }
    var badChecksum = (byte[])data.Clone(); badChecksum[layout.Core + layout.CoreLength - 1] ^= 1; Rehash(badChecksum); Reject(() => f.Parse(badChecksum));
    var badVersion = (byte[])data.Clone(); Int(badVersion, layout.Core + 8, 2);
    RehashSection(badVersion, layout.Core, layout.CoreLength); Rehash(badVersion); Reject(() => f.Parse(badVersion));
});
Test("owner handle lookup validates colony generation allocation token and full identity", () =>
{
    var f = new Fixture(); var data = f.Create().CopyEncodedData(); var row = new Layout(data).Rows[0];
    foreach (var mutate in new Action<byte[]>[]
    {
        bytes => Guid.NewGuid().ToByteArray().CopyTo(bytes, row.Colony), bytes => Long(bytes, row.Generation, 999),
        bytes => Guid.NewGuid().ToByteArray().CopyTo(bytes, row.Allocation), bytes => Array.Clear(bytes, row.Allocation, 16),
        bytes => bytes[row.ContainerIdData + 10] = (byte)'x'
    }) { var modified = (byte[])data.Clone(); mutate(modified); Rehash(modified); Reject(() => f.Parse(modified)); }
});
Test("forged owner fields cannot bypass duplicate admission geometry and float checks", () =>
{
    var f = new Fixture(); var data = f.Create().CopyEncodedData(); var layout = new Layout(data); var a = layout.Rows[0]; var b = layout.Rows[1];
    foreach (var mutate in new Action<byte[]>[]
    {
        bytes => Int(bytes, b.Instance, 0), bytes => Array.Copy(bytes, a.OwnerToken, bytes, b.OwnerToken, 16),
        bytes => Array.Clear(bytes, b.OwnerToken, 16), bytes => Int(bytes, b.Anchor, 0), bytes => Int(bytes, b.World, 9),
        bytes => Int(bytes, b.Anchor, 32), bytes => Int(bytes, b.Orientation, 4), bytes => Double(bytes, b.Volume, 10),
        bytes => Double(bytes, b.Capacity, 999), bytes => Int(bytes, b.Phase, 2), bytes => Double(bytes, b.ConstructionMass, .1),
        bytes => Double(bytes, b.ConstructionMass, double.NaN), bytes => bytes[b.PrefabData] = (byte)'X',
        bytes => Int(bytes, a.Instance, -1)
    }) { var modified = (byte[])data.Clone(); mutate(modified); Rehash(modified); Reject(() => f.Parse(modified)); }
});
Test("invalid identity lengths malformed UTF8 and modified catalog declaration reject", () =>
{
    var f = new Fixture(); var data = f.Create().CopyEncodedData(); var layout = new Layout(data); var row = layout.Rows[0];
    foreach (var offset in new[] { 104, row.PrefabLength, row.ContainerIdLength })
        foreach (var length in new[] { -1, 0, 1025, int.MaxValue })
        { var modified = (byte[])data.Clone(); Int(modified, offset, length); Rehash(modified); Reject(() => f.Parse(modified)); }
    var utf8 = (byte[])data.Clone(); utf8[row.ContainerIdData] = 0xff; Rehash(utf8); Reject(() => f.Parse(utf8));
    var hash = (byte[])data.Clone(); hash[layout.CatalogHash] ^= 1; Rehash(hash); Reject(() => f.Parse(hash));
    var version = (byte[])data.Clone(); version[108] ^= 1; Rehash(version); Reject(() => f.Parse(version));
});
Test("parsed input bytes are owned and restored authorities are independent", () =>
{
    var f = new Fixture(); var data = f.Create().CopyEncodedData(); var original = (byte[])data.Clone(); var parsed = f.Parse(data);
    Array.Clear(data); Check(parsed.CopyEncodedData().SequenceEqual(original));
    var a = parsed.RestoreAuthority(); var b = parsed.RestoreAuthority();
    using (var tx = a.BeginTransaction()) { tx.TransferContainerToContainer(f.A, f.B, 1); tx.Commit(); }
    Check(b.Save().AsSpan().SequenceEqual(parsed.CoreData) && parsed.Source.SampleContainer(f.B).Inventory.TotalMassKg == 0);
    Check(a.CaptureSnapshot().ColonyId == b.CaptureSnapshot().ColonyId && a.CaptureSnapshot().SampleContainer(f.A).Handle.Equals(f.A));
});
Test("noncanonical nested Core record order cannot masquerade as an exact checkpoint roundtrip", () =>
{
    var f = new Fixture(); var core = f.State.Save(); var pattern = new byte[44];
    Int(pattern, 0, 3); Int(pattern, 4, 8); System.Text.Encoding.UTF8.GetBytes("chlorine").CopyTo(pattern, 8); Double(pattern, 16, 1e-200);
    Int(pattern, 24, 8); System.Text.Encoding.UTF8.GetBytes("nitrogen").CopyTo(pattern, 28); Double(pattern, 36, 3);
    var offset = -1;
    for (var i = 0; i <= core.Length - pattern.Length; ++i)
        if (core.AsSpan(i, pattern.Length).SequenceEqual(pattern)) { offset = i + 4; break; }
    Check(offset >= 0); var first = core.AsSpan(offset, 20).ToArray(); core.AsSpan(offset + 20, 20).CopyTo(core.AsSpan(offset, 20)); first.CopyTo(core, offset + 20);
    Rehash(core); Check(!ColonyFluidState.Restore(core, f.Catalog).Save().SequenceEqual(core));
    Reject(() => OwnedReservoirCheckpointV1.Create(core, f.Catalog, f.Compatibility, f.NativeColony, f.Owners, new OwnedReservoirCheckpointIdleState()));
    var encoded = f.Create().CopyEncodedData(); var layout = new Layout(encoded); core.CopyTo(encoded, layout.Core); Rehash(encoded); Reject(() => f.Parse(encoded));
});
Console.WriteLine($"{passed} owned reservoir checkpoint tests passed");

static void Int(byte[] data, int offset, int value) => BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(offset), value);
static void Long(byte[] data, int offset, long value) => BinaryPrimitives.WriteInt64LittleEndian(data.AsSpan(offset), value);
static void Double(byte[] data, int offset, double value) => Long(data, offset, BitConverter.DoubleToInt64Bits(value));
static void Rehash(byte[] data) => RehashSection(data, 0, data.Length);
static void RehashSection(byte[] data, int offset, int count)
{ SHA256.HashData(data.AsSpan(offset, count - 32)).CopyTo(data, offset + count - 32); }

internal sealed class Layout
{
    internal int CatalogHash, Idle, CoreLengthField, Core, CoreLength, OwnerCount;
    internal RowLayout[] Rows;
    internal Layout(byte[] data)
    {
        CatalogHash = 108 + BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(104)); Idle = CatalogHash + 32;
        CoreLengthField = Idle + 24; Core = CoreLengthField + 8; CoreLength = checked((int)BinaryPrimitives.ReadInt64LittleEndian(data.AsSpan(CoreLengthField)));
        OwnerCount = Core + CoreLength; var position = OwnerCount + 4;
        Rows = new[] { new RowLayout(data, ref position), new RowLayout(data, ref position) };
    }
}
internal sealed class RowLayout
{
    internal int Instance, PrefabLength, PrefabData, OwnerToken, Colony, ContainerIdLength, ContainerIdData, Generation, Allocation;
    internal int World, Anchor, Orientation, ConstructionMass, Volume, Capacity, Phase;
    internal RowLayout(byte[] data, ref int position)
    {
        Instance = position; position += 4; PrefabLength = position; position += 4; PrefabData = position;
        position += BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(PrefabLength)); OwnerToken = position; position += 16;
        Colony = position; position += 16; ContainerIdLength = position; position += 4; ContainerIdData = position;
        position += BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(ContainerIdLength)); Generation = position; position += 8;
        Allocation = position; position += 16; World = position; position += 4; Anchor = position; position += 4;
        Orientation = position; position += 8; ConstructionMass = position; position += 8;
        Volume = position; position += 8; Capacity = position; position += 8; Phase = position; position += 4;
    }
}

internal sealed class Fixture
{
    internal const string HashA = "a58e04d0ffdf89b86fb28b71ad900625b3b539db30d67f8c6269f73a9f5ae599";
    internal const string HashB = "72d21f51cda136443bfa1a2f6eadb08167a4c508c3d03f558b8cdb60160f249c";
    internal readonly Guid NativeColony = Guid.NewGuid();
    internal readonly FluidMaterialCatalog Catalog = CreateCatalog();
    internal readonly OwnedReservoirCheckpointCompatibility Compatibility = new(744825, HashA, HashB);
    internal readonly ColonyFluidState State;
    internal readonly FluidContainerHandle A, B, Empty;
    internal readonly OwnedReservoirCheckpointOwner[] Owners;
    internal Fixture()
    {
        const int width = 10, height = 6, count = width * height;
        var raw = new byte[29 + 36 * count]; System.Text.Encoding.ASCII.GetBytes("SIMSAVE\0").CopyTo(raw, 0);
        void Int(int offset, int value) => BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(offset), value);
        Int(8, 15); Int(12, width); Int(16, height);
        for (var i = 0; i < count; ++i) { Int(29 + 16 * i, 1); Int(29 + 24 * count + 12 * i, 1); }
        var framed = new byte[raw.Length + 4]; BinaryPrimitives.WriteInt32LittleEndian(framed, raw.Length); raw.CopyTo(framed, 4);
        var import = NativeColonyImport.StageNewColony(8, 4, new[] { new NativeWorldPlacement(7, 8, 4, 0, 0) },
            new[] { new NativeWorldSource(7, framed) }, new Dictionary<int, NativeMatterPhase> { [1] = NativeMatterPhase.Vacuum, [4] = NativeMatterPhase.Solid },
            new Dictionary<int, NativeFluidBinding>(), 1);
        State = ColonyFluidState.Import(import, Catalog);
        using var tx = State.BeginTransaction();
        A = tx.CreateContainer("reservoir:a", new FluidContainerDefinition(10, 1000, FluidContainerPhases.Gas));
        B = tx.CreateContainer("reservoir:b", new FluidContainerDefinition(20, 1000, FluidContainerPhases.Gas));
        Empty = tx.CreateContainer("unbound-empty", new FluidContainerDefinition(.5, 2, FluidContainerPhases.Liquid));
        tx.Inject(0, Catalog.CreateInventory(new Dictionary<string, double> { ["oxygen"] = 1, ["nitrogen"] = 3, ["chlorine"] = 1e-200 },
            293.15, 3, -4, new Dictionary<int, double> { [123] = .375 }, .125), "explicit-checkpoint-fixture");
        tx.TransferAllCellToContainer(0, A); tx.CommitFrame(9, 1.8);
        Owners = new[] { Owner(A, 0, 0), Owner(B, 1, 7) };
    }
    internal OwnedReservoirCheckpointOwner Owner(FluidContainerHandle handle, int instance, int anchor, Guid? token = null,
        int world = 7, int orientation = 0, string prefab = "GasReservoir", double? volume = null, double capacity = 1000,
        FluidContainerPhases phases = FluidContainerPhases.Gas, double constructionMass = 400.125) =>
        new(instance, prefab, token ?? Guid.NewGuid(), handle, world, anchor, orientation, 4, constructionMass,
            volume ?? State.CaptureSnapshot().SampleContainer(handle).Definition.VolumeCubicMetres, capacity, phases);
    internal OwnedReservoirCheckpointV1 Create(IEnumerable<OwnedReservoirCheckpointOwner>? owners = null, OwnedReservoirCheckpointIdleState? idle = null) =>
        OwnedReservoirCheckpointV1.Create(State.Save(), Catalog, Compatibility, NativeColony, owners ?? Owners, idle ?? new OwnedReservoirCheckpointIdleState());
    internal OwnedReservoirCheckpointV1 Parse(byte[] data) => OwnedReservoirCheckpointV1.Parse(data, Catalog, Compatibility, NativeColony);
    internal static FluidMaterialCatalog CreateCatalog(double oxygenCv = 658) => new("owned-checkpoint-test-v1", new[]
    {
        new FluidMaterial("oxygen", NativeMatterPhase.Gas, .0319988, oxygenCv), new FluidMaterial("nitrogen", NativeMatterPhase.Gas, .0280134, 743),
        new FluidMaterial("chlorine", NativeMatterPhase.Gas, .070906, 355), new FluidMaterial("water", NativeMatterPhase.Liquid, .01801528, 4184, 1000)
    });
}
