using System;
using System.IO;
using System.Linq;
using System.Buffers.Binary;
using System.Security.Cryptography;
using MixedAtmospheres.Oni;

static class Program
{
    private static readonly PipeMaterial A = new(1, 1000, 0), B = new(2, 2000, 2e6);
    private static PipeMaterial Material(int id) => id == 1 ? A : id == 2 ? B : throw new ArgumentException();
    private static void Check(bool ok, string why) { if (!ok) throw new Exception(why); }
    private static void Near(double actual, double expected) => Check(Math.Abs(actual - expected) <= 1e-12 * Math.Max(1, Math.Abs(expected)), $"{actual:R} != {expected:R}");
    private static void Reject(Action action)
    {
        try { action(); } catch (Exception e) when (e is InvalidDataException || e is ArgumentException || e is EndOfStreamException) { return; }
        throw new Exception("Invalid pipe image accepted");
    }
    private static byte[] Save(PipeInventory state)
    {
        using var memory = new MemoryStream(); using var writer = new BinaryWriter(memory);
        state.Write(writer); return memory.ToArray();
    }
    private static PipeInventory Load(byte[] image)
    {
        using var memory = new MemoryStream(image); using var reader = new BinaryReader(memory);
        var state = PipeInventory.Read(reader); Check(memory.Position == memory.Length, "trailing pipe data"); return state;
    }
    private static void Account(PipeInventory original, params PipeInventory[] states)
    {
        foreach (var id in new[] { 1, 2 })
        {
            var left = original.Parts.ToArray().Where(p => p.Id == id).Select(p => p.Amount).Aggregate(PipeAmount.Zero, (a,b) => a.Plus(b));
            var right = states.SelectMany(s => s.Parts.ToArray()).Where(p => p.Id == id).Select(p => p.Amount).Aggregate(PipeAmount.Zero, (a,b) => a.Plus(b));
            Check(left.Minus(right).Sign == 0, "constituent account changed");
        }
        var energy = states.Aggregate(PipeAmount.Zero, (e,s) => e.Plus(s.Energy));
        Check(original.Energy.Minus(energy).Sign == 0, "energy account changed");
        foreach (var germ in original.Germs)
        {
            var total = states.SelectMany(s => s.Germs.ToArray()).Where(g => g.Id == germ.Id).Select(g => g.Amount).Aggregate(PipeAmount.Zero, (a,b) => a.Plus(b));
            Check(germ.Amount.Minus(total).Sign == 0, "passive account changed");
        }
    }
    static void Main(string[] args)
    {
        var mixture = PipeInventory.Single(A, .4, 300, 7, 40).Joined(PipeInventory.Single(B, .6, 500, 8, 60));
        Near(mixture.Mass, 1); Near(mixture.Temperature(Material), 450);
        Check(mixture.Count == 2 && mixture.Germs.Length == 2, "one-cell composition lost");
        var filtered = PipeInventory.Transfer(mixture, PipeInventory.Empty, 1, .2, Material, 1);
        Near(filtered.Target.SpeciesMass(1), .2); Near(filtered.Target.SpeciesMass(2), 0);
        var passed = PipeInventory.Transfer(filtered.Source, PipeInventory.Empty, 1, .4, Material, 2);
        Near(passed.Target.SpeciesMass(1), 0); Near(passed.Target.SpeciesMass(2), .4);
        Account(mixture, passed.Source, passed.Target, filtered.Target);

        var state = mixture; var destination = PipeInventory.Empty;
        for (var i = 0; i < 1000; ++i)
        {
            var moved = PipeInventory.Transfer(state, destination, .003, 1, Material);
            (state,destination) = (moved.Source,moved.Target);
            moved = PipeInventory.Transfer(destination, state, .002, 1, Material);
            (destination,state) = (moved.Source,moved.Target);
        }
        Account(mixture, state, destination);
        var restored = Load(Save(state)); Check(Save(state).SequenceEqual(Save(restored)), "checkpoint re-encoding differs");
        var continued = PipeInventory.Transfer(state, destination, .01, 1, Material);
        var replayed = PipeInventory.Transfer(restored, Load(Save(destination)), .01, 1, Material);
        Check(Save(continued.Source).SequenceEqual(Save(replayed.Source)) && Save(continued.Target).SequenceEqual(Save(replayed.Target)), "cold continuation differs");

        var trace = PipeInventory.Single(A, 1, 300).Joined(PipeInventory.Single(A, 1e-240, 300));
        var split = trace.Split(1, Material);
        Check(split.Remaining.Count == 1 && split.Remaining.Mass > 0, "sub-resolution mass was discarded");
        Near(split.Remaining.Temperature(Material), 300); Account(trace, split.Remaining, split.Parcel);
        var warmed = mixture.WithHeat(1600); Near(warmed.Temperature(Material), 451);
        var dumped = warmed.Split(2, Material); Check(dumped.Remaining.Count == 0, "destruction did not evacuate all contents");
        Check(Save(dumped.Parcel).SequenceEqual(Save(warmed)), "evacuation changed inventory");
        Console.WriteLine("PASS mixed pipe inventory: one-cell composition, selective capacity, exact flux/energy/germs, traces, heat, evacuation and cold continuation");

        var boundary = PipeInventory.Single(A, .4, 293.123456789, 7, 40.5).Joined(PipeInventory.Single(B, .6, 800.987654321, 8, 60.25));
        var takeA = boundary.SplitNative(1, 1, Material, 7);
        var takeB = takeA.Remaining.SplitNative(2, 1, Material, 8);
        Check(takeA.Parcel.Mass == (float)takeA.Parcel.Mass && takeB.Parcel.Mass == (float)takeB.Parcel.Mass, "native mass is not exact float");
        Check(takeA.Remaining.SpeciesMass(1) > 0 && takeB.Remaining.SpeciesMass(2) > 0, "native boundary erased a rounded remainder");
        Check(takeA.Parcel.Germs.ToArray().All(g=>g.Mass==(int)g.Mass), "native germ count is fractional");
        Account(boundary, takeB.Remaining, takeA.Parcel, takeB.Parcel);
        var complete = PipeInventory.Single(A, 1, 293.123456789, 7, 10.5);
        var rounded = complete.SplitNative(1, 1, Material, 7);
        Check(rounded.Remaining.Count > 0 && rounded.Remaining.Energy.Sign != 0 && rounded.Remaining.Germs.Length > 0,
            "full scalar request erased heat or germs that native storage cannot represent");
        Account(complete, rounded.Remaining, rounded.Parcel);
        var firstDisease = PipeInventory.Single(A, 1, 300, 0, 100);
        var firstDiseaseParcel = firstDisease.SplitNative(1, .5f, Material, 0);
        Check(firstDiseaseParcel.Parcel.Germs.Length == 1 && firstDiseaseParcel.Parcel.Germs[0].Id == 0,
            "native disease identity zero was treated as empty");
        Account(firstDisease, firstDiseaseParcel.Remaining, firstDiseaseParcel.Parcel);
        var unrepresentable = PipeInventory.Single(A, 1e-240, 300);
        var retained = unrepresentable.SplitNative(1, 1, Material);
        Check(retained.Parcel.Count == 0 && Save(retained.Remaining).SequenceEqual(Save(unrepresentable)), "float underflow deleted a pipe trace");
        Console.WriteLine("PASS native pipe ports: float mass/temperature and integer germ dispatch retain all undispatched primary quantities");

        var geometry = new PipeGeometry(16, 12, 2, 1, 10, 8); var hash = Enumerable.Range(0,32).Select(i => (byte)i).ToArray();
        var image = new ColonyConduitCheckpoint(geometry, hash,
            new[] { new PipeCellImage(1,35,mixture), new PipeCellImage(2,36,warmed) }, mixture, PipeInventory.Empty,
            PipeInventory.Empty, new PipeAmount(1600), new[] {new PipePart(7,new PipeAmount(-.5))}).Encode();
        var decoded = ColonyConduitCheckpoint.Decode(image, geometry, hash);
        Check(decoded.Encode().SequenceEqual(image), "pipe image continuation differs");
        Reject(() => ColonyConduitCheckpoint.Decode(image[..^1],geometry,hash));
        var corrupt=(byte[])image.Clone();corrupt[80]^=1;Reject(()=>ColonyConduitCheckpoint.Decode(corrupt,geometry,hash));
        foreach (var (offset,value) in new[]{(8,2),(68,int.MaxValue)})
        {
            corrupt=(byte[])image.Clone();BinaryPrimitives.WriteInt32LittleEndian(corrupt.AsSpan(offset),value);
            SHA256.HashData(corrupt.AsSpan(0,corrupt.Length-32)).CopyTo(corrupt.AsSpan(corrupt.Length-32));
            Reject(()=>ColonyConduitCheckpoint.Decode(corrupt,geometry,hash));
        }
        Reject(()=>ColonyConduitCheckpoint.Decode(image,new PipeGeometry(16,12,2,1,9,8),hash));
        Reject(()=>ColonyConduitCheckpoint.Decode(image,geometry,new byte[32]));
        Reject(()=>new ColonyConduitCheckpoint(geometry,hash,new[]{new PipeCellImage(1,35,mixture),new PipeCellImage(1,35,mixture)},mixture,mixture,mixture,default));
        Reject(()=>new ColonyConduitCheckpoint(geometry,hash,new[]{new PipeCellImage(3,35,mixture)},mixture,mixture,mixture,default));
        Reject(()=>ColonyConduitCheckpoint.Decode(image,geometry,hash,id=>1)); // liquid image cannot contain gas declarations

        var config=new byte[56];var owner=new byte[]{1,2,3,4};var legacy=ColonyPipePayload.Encode(config,owner,null);
        var expectedLegacy=System.Text.Encoding.ASCII.GetBytes("MACOLP01").Concat(BitConverter.GetBytes(56)).Concat(config).Concat(owner).ToArray();
        Check(legacy.SequenceEqual(expectedLegacy),"legacy payload bytes changed");
        var old=ColonyPipePayload.Decode(legacy);Check(old.Pipes==null && old.Owner.SequenceEqual(owner),"legacy payload migrated");
        var modern=ColonyPipePayload.Encode(config,owner,image);var parsed=ColonyPipePayload.Decode(modern);
        Check(parsed.Pipes!.SequenceEqual(image)&&ColonyPipePayload.Encode(parsed.Config,parsed.Owner,parsed.Pipes).SequenceEqual(modern),"mixed payload continuation differs");
        var invalid=(byte[])modern.Clone();BinaryPrimitives.WriteInt32LittleEndian(invalid.AsSpan(68),int.MaxValue);Reject(()=>ColonyPipePayload.Decode(invalid));
        invalid=modern.Concat(new byte[]{0}).ToArray();Reject(()=>ColonyPipePayload.Decode(invalid));

        var native=new byte[29+36*3*4];System.Text.Encoding.ASCII.GetBytes("SIMSAVE\0").CopyTo(native,0);
        BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(8),15);BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(12),3);BinaryPrimitives.WriteInt32LittleEndian(native.AsSpan(16),4);
        var envelope=ColonySaveEnvelope.Create(744825,3,42,hash,native,modern).CopyEncodedData();
        Check(ColonySaveEnvelope.Parse(envelope,744825,3,hash).CustomData.SequenceEqual(modern),"outer pipe payload missing");
        envelope[^34]^=1;
        try {ColonySaveEnvelope.Parse(envelope,744825,3,hash);throw new Exception("Outer hash ignored pipe contents");}
        catch(InvalidDataException){}
        Console.WriteLine("PASS pipe persistence: exact legacy bytes, mixed continuation, catalogue/geometry/schema bounds, corruption and root-envelope coverage");
        if (args.Contains("--benchmark"))
        {
            foreach (var seed in new[] { PipeInventory.Single(A, 1, 300), mixture })
            {
                var source = seed; var target = PipeInventory.Empty;
                for (var i = 0; i < 50000; ++i)
                {
                    var flux = PipeInventory.Transfer(source,target,.001,1,Material);
                    source=flux.Target;target=flux.Source;
                }
                var times = new double[5]; var allocations = new long[5];
                for (var repeat = 0; repeat < times.Length; ++repeat)
                {
                    GC.Collect(); var allocation = GC.GetAllocatedBytesForCurrentThread();
                    var clock = System.Diagnostics.Stopwatch.StartNew();
                    for (var i = 0; i < 20000; ++i)
                    {
                        var flux = PipeInventory.Transfer(source,target,.001,1,Material);
                        source=flux.Target;target=flux.Source;
                    }
                    clock.Stop(); times[repeat]=clock.Elapsed.TotalMilliseconds/20;
                    allocations[repeat]=(GC.GetAllocatedBytesForCurrentThread()-allocation)/20000;
                }
                Account(seed,source,target); Array.Sort(times);Array.Sort(allocations);
                Console.WriteLine($"MODEL {seed.Count} species: median {times[2]:F3} us/transfer, {allocations[2]} bytes/transfer (5 x 20000 moves after 50000 warm-up)");
            }
        }
    }
}
