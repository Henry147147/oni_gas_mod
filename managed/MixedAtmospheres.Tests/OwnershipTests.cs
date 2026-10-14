using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using MixedAtmospheres.Core.Interop;

internal static class OwnershipTests
{
    internal static void Run(Action<string, Action> test, Action<bool> check, Action<Action> reject,
        Func<byte[]> nativeFixture, Dictionary<int, NativeMatterPhase> phases)
    {
        var bindings = new Dictionary<int, NativeFluidBinding>
        {
            [2] = new NativeFluidBinding("oxygen", NativeMatterPhase.Gas),
            [3] = new NativeFluidBinding("water", NativeMatterPhase.Liquid)
        };
        var placements = new[] { new NativeWorldPlacement(7, 2, 2, 11, 7), new NativeWorldPlacement(9, 2, 2, 20, 7) };
        byte[] Envelope(int x)
        {
            var raw = nativeFixture(); WriteInt(raw, 20, x);
            var result = new byte[4 + raw.Length]; WriteInt(result, 0, raw.Length); raw.CopyTo(result, 4);
            return result;
        }
        NativeWorldSource[] Sources() => new[] { new NativeWorldSource(7, Envelope(11)), new NativeWorldSource(9, Envelope(20)) };
        NativeColonyImport Stage(NativeWorldPlacement[] p, NativeWorldSource[] s) =>
            NativeColonyImport.StageNewColony(32, 16, p, s, phases, bindings, 1);

        test("cluster import stages reordered worlds and preserves every captured inventory", () =>
        {
            var sources = Sources(); var before = sources.Select(source => source.LengthPrefixedData.ToArray()).ToArray();
            var result = Stage(placements, sources.Reverse().ToArray());
            check(result.Width == 32 && result.Height == 16 && result.Worlds.Count == 2 && result.Fluids.Count == 4);
            check(result.Worlds.Select(world => world.Placement.WorldId).SequenceEqual(new[] { 7, 9 }));
            check(result.Fluids.Sum(cell => (double)cell.Inventory.MassKg) == 204);
            check(result.Fluids.Where(cell => cell.MaterialId == "oxygen").All(cell =>
                cell.Inventory.DiseaseHash == 123 && cell.Inventory.DiseaseCount == 456 && cell.Inventory.Radiation == 17));
            check(result.Fluids.Single(cell => cell.WorldId == 9 && cell.MaterialId == "oxygen").Inventory.X == 20);
            for (var i = 0; i < sources.Length; ++i)
            {
                check(sources[i].LengthPrefixedData.SequenceEqual(before[i]));
                var world = result.Worlds[i]; var data = world.LengthPrefixedData;
                check(BinaryPrimitives.ReadInt32LittleEndian(data) == data.Length - 4);
                check(NativeWorldBlob.Split(data.Slice(4), phases, 1).Fluids.Count == 0);
            }
        });
        test("cluster staging rejects a bad last world without modifying any caller buffers", () =>
        {
            var first = Envelope(11); var last = Envelope(20);
            WriteInt(last, 4 + 29 + 6 * 16, 999);
            var originalFirst = (byte[])first.Clone(); var originalLast = (byte[])last.Clone();
            var sources = new[] { new NativeWorldSource(7, first), new NativeWorldSource(9, last) };
            reject(() => Stage(placements, sources));
            check(first.SequenceEqual(originalFirst) && last.SequenceEqual(originalLast));
            check(sources[0].LengthPrefixedData.SequenceEqual(originalFirst) && sources[1].LengthPrefixedData.SequenceEqual(originalLast));
        });
        test("cluster import rejects missing duplicate unknown or overlapping worlds", () =>
        {
            var sources = Sources();
            reject(() => Stage(placements, sources.Take(1).ToArray()));
            reject(() => Stage(placements, new[] { sources[0], sources[0] }));
            reject(() => Stage(new[] { placements[0], placements[0] }, sources));
            reject(() => Stage(placements, new[] { sources[0], new NativeWorldSource(99, Envelope(20)) }));
            reject(() => Stage(new[] { placements[0], new NativeWorldPlacement(9, 2, 2, 12, 7) }, sources));
            reject(() => Stage(new[] { placements[0], new NativeWorldPlacement(9, 2, 2, int.MaxValue, 7) }, sources));
            reject(() => Stage(new[] { placements[0], new NativeWorldPlacement(9, 3, 2, 20, 7) }, sources));
            reject(() => Stage(Array.Empty<NativeWorldPlacement>(), Array.Empty<NativeWorldSource>()));
            reject(() => NativeColonyImport.StageNewColony(int.MaxValue, int.MaxValue, placements, sources, phases, bindings, 1));
            // Adjacent interiors are permitted even though serialized guard borders overlap.
            var adjacent = new[] { placements[0], new NativeWorldPlacement(9, 2, 2, 13, 7) };
            check(Stage(adjacent, new[] { sources[0], new NativeWorldSource(9, Envelope(13)) }).Worlds.Count == 2);
        });
        test("cluster import validates outer framing and both phase and material identity", () =>
        {
            foreach (var size in new[] { -1, 0, Envelope(20).Length, int.MaxValue })
            {
                var last = Envelope(20); WriteInt(last, 0, size);
                reject(() => Stage(placements, new[] { Sources()[0], new NativeWorldSource(9, last) }));
            }
            reject(() => Stage(placements, new[] { Sources()[0], new NativeWorldSource(9, nativeFixture()) }));
            var incomplete = new Dictionary<int, NativeFluidBinding> { [2] = bindings[2] };
            reject(() => NativeColonyImport.StageNewColony(32, 16, placements, Sources(), phases, incomplete, 1));
            incomplete[3] = new NativeFluidBinding("water", NativeMatterPhase.Gas);
            reject(() => NativeColonyImport.StageNewColony(32, 16, placements, Sources(), phases, incomplete, 1));
            incomplete[3] = default;
            reject(() => NativeColonyImport.StageNewColony(32, 16, placements, Sources(), phases, incomplete, 1));
            // Zero mass still carries element/disease metadata and needs a declared owner.
            var emptyFluid = Envelope(20); WriteFloat(emptyFluid, 4 + 29 + 6 * 16 + 8, 0);
            reject(() => NativeColonyImport.StageNewColony(32, 16, placements,
                new[] { Sources()[0], new NativeWorldSource(9, emptyFluid) }, phases, incomplete, 1));
        });
        test("staged import owns its buffers after callers mutate sources and exported copies", () =>
        {
            var first = Envelope(11); var original = (byte[])first.Clone();
            var source = new NativeWorldSource(7, first); first.AsSpan().Clear();
            check(source.LengthPrefixedData.SequenceEqual(original));
            var result = Stage(placements, new[] { source, Sources()[1] });
            var copy = result.Worlds[0].LengthPrefixedData.ToArray(); copy.AsSpan().Clear();
            bindings[2] = new NativeFluidBinding("changed", NativeMatterPhase.Gas);
            check(result.Fluids[0].MaterialId == "oxygen");
            check(result.Worlds[0].LengthPrefixedData[0] != 0);
            bindings[2] = new NativeFluidBinding("oxygen", NativeMatterPhase.Gas);
        });

        var tablePhases = new Dictionary<int, NativeMatterPhase>(phases) { [5] = NativeMatterPhase.Solid };
        byte[] Table()
        {
            var bytes = new byte[4 + 5 * NativeElementTable.RecordBytes + 7]; WriteInt(bytes, 0, 5);
            for (var i = 0; i < 5; ++i)
            {
                var row = 4 + i * NativeElementTable.RecordBytes;
                WriteInt(bytes, row, i + 1); WriteIndex(bytes, row + 8, (ushort)i); bytes[row + 10] = (byte)tablePhases[i + 1];
                foreach (var offset in new[] { 4, 6, 84, 86 }) WriteIndex(bytes, row + offset, ushort.MaxValue);
                WriteFloat(bytes, row + 56, 200); WriteFloat(bytes, row + 60, 400);
            }
            // Solid 4 freezes directly to liquid, heats to solid 5 + gas byproduct,
            // and sublimes into gas while leaving a solid conversion product.
            var solid = 4 + 3 * NativeElementTable.RecordBytes;
            WriteIndex(bytes, solid + 4, 2); WriteIndex(bytes, solid + 6, 4);
            WriteInt(bytes, solid + 76, 2); WriteFloat(bytes, solid + 80, 0.2f);
            WriteIndex(bytes, solid + 84, 1); WriteIndex(bytes, solid + 86, 4);
            foreach (var offset in new[] { 124, 128, 132, 136 }) WriteFloat(bytes, solid + offset, 0.5f);
            // A supported solid-only branch and opaque tail must remain unchanged.
            WriteIndex(bytes, 4 + 4 * NativeElementTable.RecordBytes + 6, 3);
            bytes.AsSpan(bytes.Length - 7).Fill(0xA5);
            return bytes;
        }
        test("native definitions suppress complete fluid-producing branches without changing solids", () =>
        {
            var source = Table(); var original = (byte[])source.Clone();
            var filtered = NativeElementTable.SuppressFluidProduction(source, tablePhases);
            check(filtered.Suppressed.Count == 3 && filtered.Suppressed.All(branch => branch.ElementHash == 4));
            var row = filtered.Data.Slice(4 + 3 * NativeElementTable.RecordBytes, NativeElementTable.RecordBytes);
            foreach (var offset in new[] { 4, 6, 84, 86 }) check(BinaryPrimitives.ReadUInt16LittleEndian(row.Slice(offset)) == ushort.MaxValue);
            check(ReadFloat(row, 56) == -float.MaxValue && ReadFloat(row, 60) == float.MaxValue);
            foreach (var offset in new[] { 80, 124, 128, 132, 136 }) check(ReadFloat(row, offset) == 0);
            check(filtered.Data.Slice(4 + 4 * NativeElementTable.RecordBytes).SequenceEqual(source.AsSpan(4 + 4 * NativeElementTable.RecordBytes)));
            check(filtered.Data.Slice(0, 4 + 3 * NativeElementTable.RecordBytes).SequenceEqual(source.AsSpan(0, 4 + 3 * NativeElementTable.RecordBytes)));
            check(source.SequenceEqual(original));
            var again = NativeElementTable.SuppressFluidProduction(filtered.Data, tablePhases);
            check(again.Suppressed.Count == 0 && again.Data.SequenceEqual(filtered.Data));
        });
        test("native definitions suppress sublimation when its conversion remnant is fluid", () =>
        {
            var table = Table(); var row = 4 + 4 * NativeElementTable.RecordBytes;
            WriteIndex(table, row + 84, 3); WriteIndex(table, row + 86, 2);
            var filtered = NativeElementTable.SuppressFluidProduction(table, tablePhases);
            check(filtered.Suppressed.Any(branch => branch.ElementHash == 5 && branch.Branch == NativeProductionBranch.SublimationOrConversion));
            check(BinaryPrimitives.ReadUInt16LittleEndian(filtered.Data.Slice(row + 84)) == ushort.MaxValue);
        });
        test("native definition validation rejects malformed late records before changing input", () =>
        {
            var row = 4 + 4 * NativeElementTable.RecordBytes;
            var corruptions = new Action<byte[]>[]
            {
                data => WriteIndex(data, row + 6, 1000),
                data => WriteIndex(data, row + 8, 3),
                data => WriteInt(data, row, 4),
                data => data[row + 10] = 1,
                data => WriteFloat(data, row + 80, float.NaN),
                data => WriteFloat(data, row + 80, -1),
                data => WriteFloat(data, row + 80, 1.1f),
                data => { WriteInt(data, row + 76, 999); WriteFloat(data, row + 80, 0.1f); },
                data => WriteFloat(data, row + 124, float.PositiveInfinity)
            };
            foreach (var corrupt in corruptions)
            {
                var source = Table(); corrupt(source); var before = (byte[])source.Clone();
                reject(() => NativeElementTable.SuppressFluidProduction(source, tablePhases));
                check(source.SequenceEqual(before));
            }
            reject(() => NativeElementTable.SuppressFluidProduction(Table().AsSpan(0, 100), tablePhases));
            reject(() => NativeElementTable.SuppressFluidProduction(Table(), phases));
        });
    }
    private static void WriteInt(byte[] data, int offset, int value) => BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(offset), value);
    private static void WriteIndex(byte[] data, int offset, ushort value) => BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(offset), value);
    private static void WriteFloat(byte[] data, int offset, float value) => WriteInt(data, offset, BitConverter.SingleToInt32Bits(value));
    private static float ReadFloat(ReadOnlySpan<byte> data, int offset) => BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(data.Slice(offset)));
}
