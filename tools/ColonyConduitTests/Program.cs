using System;
using System.IO;
using System.Linq;
using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Numerics;
using MixedAtmospheres.Core;
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
        byte[] AmountImage(PipeAmount amount){using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);amount.Write(writer);return stream.ToArray();}
        var operands=new[]{1.0,Math.ScaleB(1,-53),Math.ScaleB(1,-106),-.75,-Math.ScaleB(1,-54),1e200,-1e200,1e-240};
        var expectedAmount=operands.Aggregate(PipeAmount.Zero,(sum,value)=>sum.Plus(new PipeAmount(value)));
        var exactOracle=operands.Aggregate(default(CoupledAccountQuantity),(sum,value)=>sum.Add(CoupledAccountQuantity.FromDouble(value)));
        Check(exactOracle.TryGetRoundedValue(out var expectedRounded),"bad exact account oracle");
        var random=new Random(7);
        for(var repeat=0;repeat<128;++repeat)
        {
            var shuffled=(double[])operands.Clone();
            for(var i=shuffled.Length-1;i>0;--i){var other=random.Next(i+1);(shuffled[i],shuffled[other])=(shuffled[other],shuffled[i]);}
            var amount=shuffled.Aggregate(PipeAmount.Zero,(sum,value)=>sum.Plus(new PipeAmount(value)));
            Check(amount.Minus(expectedAmount).Sign==0,"parallel account reduction changed exact total");
            Check(AmountImage(amount).SequenceEqual(AmountImage(expectedAmount)),"parallel account reduction changed checkpoint atoms");
            Check(amount.Value==expectedRounded,"pipe expansion projection differs from exact integer oracle");
        }
        using(var stream=new MemoryStream())
        {
            using(var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true)){writer.Write(3);writer.Write(Math.ScaleB(1,-106));writer.Write(Math.ScaleB(1,-53));writer.Write(1.0);}
            var legacyAtoms=stream.ToArray();stream.Position=0;using var reader=new BinaryReader(stream);
            var legacyAmount=PipeAmount.Read(reader);Check(AmountImage(legacyAmount).SequenceEqual(legacyAtoms),"legacy expansion bytes changed");
            Check(legacyAmount.Value==Math.BitIncrement(1),"legacy half-ulp projection lost its decisive trace");
        }
        var threeTier=new PipeAmount(Math.BitIncrement(1)).Plus(new PipeAmount(Math.ScaleB(1,-53))).Plus(new PipeAmount(-Math.ScaleB(1,-106)));
        using(var stream=new MemoryStream(AmountImage(threeTier)))using(var reader=new BinaryReader(stream))
        {
            var cold=PipeAmount.Read(reader);
            Check(cold.Value==Math.BitIncrement(1)&&AmountImage(cold).SequenceEqual(AmountImage(threeTier)),"three-tier half-ulp continuation differs");
        }
        for(var sample=0;sample<32;++sample)
        {
            var values=new double[13];
            for(var i=0;i<12;i+=2){values[i]=Math.ScaleB(.5+.5*random.NextDouble(),random.Next(-900,900));values[i+1]=-values[i];}
            values[12]=Math.ScaleB(.5+.5*random.NextDouble(),random.Next(-900,900));
            var reference=values.Aggregate(default(CoupledAccountQuantity),(sum,value)=>sum.Add(CoupledAccountQuantity.FromDouble(value)));
            Check(reference.TryGetRoundedValue(out var projected),"wide scalar oracle overflow");
            byte[]? expected=null;
            for(var repeat=0;repeat<8;++repeat)
            {
                for(var i=values.Length-1;i>0;--i){var other=random.Next(i+1);(values[i],values[other])=(values[other],values[i]);}
                var total=values.Aggregate(PipeAmount.Zero,(sum,value)=>sum.Plus(new PipeAmount(value)));
                var bytes=AmountImage(total);expected??=bytes;
                Check(total.Value==projected&&expected.SequenceEqual(bytes),"wide scalar reduction/projection depends on order");
            }
        }
        Console.WriteLine("PASS pipe scalar determinism: permuted reductions, exact integer rounding oracle, half-ulp traces and legacy expansion bytes");
        var mixture = PipeInventory.Single(A, .4, 300, 7, 40).Joined(PipeInventory.Single(B, .6, 500, 8, 60));
        Near(mixture.Mass, 1); Near(mixture.Temperature(Material), 450);
        Check(mixture.Count == 2 && mixture.Germs.Length == 2, "one-cell composition lost");
        var filtered = PipeInventory.Transfer(mixture, PipeInventory.Empty, 1, .2, Material, 1);
        Near(filtered.Target.SpeciesMass(1), .2); Near(filtered.Target.SpeciesMass(2), 0);
        var passed = PipeInventory.Transfer(filtered.Source, PipeInventory.Empty, 1, .4, Material, 2);
        Near(passed.Target.SpeciesMass(1), 0); Near(passed.Target.SpeciesMass(2), .4);
        Account(mixture, passed.Source, passed.Target, filtered.Target);
        var selectedCapacity=PipeInventory.Single(B,.75,450);
        var blocked=PipeInventory.Transfer(mixture,selectedCapacity,1,1,Material,2);
        var remainder=PipeInventory.Transfer(blocked.Source,PipeInventory.Empty,1,1,Material,2,true);
        Near(blocked.Target.SpeciesMass(1),0);Near(blocked.Target.SpeciesMass(2),1);
        Near(remainder.Target.SpeciesMass(1),.4);Near(remainder.Target.SpeciesMass(2),0);Near(remainder.Source.SpeciesMass(2),.35);
        Account(mixture.Joined(selectedCapacity),blocked.Target,remainder.Source,remainder.Target);
        var allPassed=mixture.Split(2,Material,-1,true);
        Check(allPassed.Remaining.Count==0&&Save(allPassed.Parcel).SequenceEqual(Save(mixture)),"unselected full pass changed inventory");
        var sensorBoundary=PipeInventory.Single(A,1.0/1024,300).Joined(PipeInventory.Single(B,999.0/1024,300));
        Check(sensorBoundary.ContainsFraction(1,1e-3)&&mixture.ContainsFraction(1,1e-3),"minority or exact-threshold pipe sensor missed constituent");
        var sensorTrace=PipeInventory.Single(A,.0005,300).Joined(PipeInventory.Single(B,.9995,300));
        Check(!sensorTrace.ContainsFraction(1,1e-3)&&!sensorTrace.ContainsFraction(3,1e-3),"trace/absent constituent activated pipe sensor");

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
        var lastTrace = PipeInventory.Single(A,1,300).Joined(PipeInventory.Single(B,1e-240,300));
        foreach (var request in new[] { Math.BitDecrement(1), 1, Math.BitIncrement(1), .33333334f, .00000001f })
        {
            var flux = lastTrace.Split(request,Material);Account(lastTrace,flux.Remaining,flux.Parcel);
            var wanted = new PipeAmount(request);
            if (wanted.Minus(lastTrace.Total).Sign>0) wanted=lastTrace.Total;
            Check(flux.Parcel.Total.Minus(wanted).Sign==0,"native shared flux changed aggregate mass");
            Check(flux.Parcel.SpeciesMass(2)<=lastTrace.SpeciesMass(2),"bulk correction created trace mass");
        }
        var almostFull=PipeInventory.Single(A,Math.BitDecrement(1),300).Joined(PipeInventory.Single(B,1e-240,300));
        var capacityTest=PipeInventory.Transfer(lastTrace,almostFull,1,1,Material);
        Check(capacityTest.Target.Total.Minus(new PipeAmount(1)).Sign<=0,"rounded target mass exceeded exact capacity");
        Account(lastTrace.Joined(almostFull),capacityTest.Source,capacityTest.Target);
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
        foreach (var (offset,value) in new[]{(8,3),(68,int.MaxValue)})
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
        var enormous=BigInteger.Parse("922337203685477580812345");
        var mechanical=new PipeScaledAccount();
        foreach(var value in new[]{new PipeScaledValue(.5,enormous),new PipeScaledValue(.5,enormous),new PipeScaledValue(-1,enormous),
            new PipeScaledValue(1e308,0),new PipeScaledValue(1e-300,0),new PipeScaledValue(-1e308,0)})mechanical.Add(value);
        Check(!mechanical.IsZero,"scaled account dropped a small term");
        mechanical.Add(new PipeScaledValue(-1e-300,0));Check(mechanical.IsZero,"scaled cancellation differs");
        mechanical.Add(new PipeScaledValue(.75,enormous));mechanical.Add(new PipeScaledValue(-.625,-enormous));
        byte[] MechanicalImage(PipeScaledAccount value){using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);value.Write(writer);return stream.ToArray();}
        var mechanicalImage=MechanicalImage(mechanical);
        using(var stream=new MemoryStream(mechanicalImage))using(var reader=new BinaryReader(stream))
            Check(MechanicalImage(PipeScaledAccount.Read(reader,mechanicalImage.Length)).SequenceEqual(mechanicalImage),"scaled checkpoint changed huge exponents");
        var reservation=PipeInventory.Single(A,.25,300,0,.5);
        var prepared=new PipePortJob(1,0,35,1,36,1,1,0,.25,300,reservation,new[]{new PipeReceiptPassive(1,.5)});
        var queued=new PipePortJob(2,1,36,2,35,0,4,2,5,0,PipeInventory.Empty);
        var passiveResidual=new PipeAmount(.5).Plus(new PipeAmount(1e-240));
        var boundaryState=new PipeBoundaryState(new PipeAmount(1e-240),mechanical,new PipeScaledAccount(),new PipeScaledAccount(),new PipeScaledAccount(),3,2,new[]{prepared,queued},
            new[]{new PipePassiveBalance(0x100000000UL,passiveResidual)});
        var pipeV2=new ColonyConduitCheckpoint(geometry,hash,new[]{new PipeCellImage(1,35,PipeInventory.Single(A,1,300)),new PipeCellImage(2,36,PipeInventory.Single(B,1,300))},
            mixture,PipeInventory.Empty,PipeInventory.Empty,default,boundary:boundaryState).Encode();
        var replayV2=ColonyConduitCheckpoint.Decode(pipeV2,geometry,hash,id=>id==1?(byte)1:(byte)2);
        Check(replayV2.SchemaVersion==2&&replayV2.Encode().SequenceEqual(pipeV2),"durable boundary continuation differs");
        Check(replayV2.Boundary!.Jobs[0].NativeToken==0&&replayV2.Boundary.Jobs[1].NativeToken==1&&replayV2.Boundary.NextNativeToken==2,
            "prepared and already-native-queued jobs lost their distinction");
        Check(MechanicalImage(replayV2.Boundary.Kinetic).SequenceEqual(mechanicalImage)&&replayV2.Boundary.UnreleasedHeat.Minus(new PipeAmount(1e-240)).Sign==0,
            "boundary mechanics or unreleased heat changed");
        Check(replayV2.Boundary.PassiveRemainders.Single().Identity==0x100000000UL&&replayV2.Boundary.PassiveRemainders.Single().Quantity.Minus(passiveResidual).Sign==0,
            "full-identity passive remainder lost its tiny component");
        Check(BinaryPrimitives.ReadInt32LittleEndian(image.AsSpan(8))==1&&ColonyConduitCheckpoint.Decode(image,geometry,hash).Encode().SequenceEqual(image),"schema1 migrated");
        Reject(()=>new PipeBoundaryState(default,mechanical,mechanical,mechanical,mechanical,3,1,new[]{queued}));
        Reject(()=>new PipeBoundaryState(default,mechanical,mechanical,mechanical,mechanical,3,2,new[]{queued,queued}));
        Reject(()=>new PipePortJob(3,0,35,1,36,1,1,0,.5,300,reservation));
        Reject(()=>new PipePortJob(3,0,35,1,36,1,1,0,.25,300,reservation,new[]{new PipeReceiptPassive(2,.5),new PipeReceiptPassive(1,.5)}));
        Reject(()=>new PipePortJob(3,0,35,1,36,2,1,0,.25,300,reservation));
        Reject(()=>new PipePortJob(3,0,35,1,36,1,1,65,.25,300,reservation));
        Reject(()=>new PipePortJob(3,0,35,1,36,1,1,0,.25,300,reservation,new[]{new PipeReceiptPassive(1,.5001)}));
        Reject(()=>new PipePortJob(3,0,35,1,36,1,1,0,.25,300,reservation,new[]{new PipeReceiptPassive(2,.5)}));
        Reject(()=>new PipePortJob(3,0,35,1,36,1,1,0,.25,300,reservation.WithDiseaseDelta(0,-1e-240),new[]{new PipeReceiptPassive(1,.5)}));
        var outside=new PipePortJob(1,0,35,1,uint.MaxValue,1,1,0,.25,300,reservation);
        Reject(()=>new ColonyConduitCheckpoint(geometry,hash,Array.Empty<PipeCellImage>(),mixture,mixture,mixture,default,
            boundary:new PipeBoundaryState(default,mechanical,mechanical,mechanical,mechanical,2,1,new[]{outside})));
        Console.WriteLine("PASS pipe schema2: exact sparse mechanical/heat accounts, prepared/native-queued reservations and watermarks, schema1 preservation and invalid jobs");
        byte[] ReceiptImage(bool observed=false)
        {
            using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);
            writer.Write(System.Text.Encoding.ASCII.GetBytes(observed?"MARECP02":"MARECP01"));writer.Write(42UL);writer.Write(100UL);writer.Write(2u);
            foreach(var zero in new[]{false,true})
            {
                writer.Write(zero?2u:1u);writer.Write(zero?36u:35u);writer.Write(zero?1u:0u);
                writer.Write(zero?0:.4);writer.Write(zero?0:293.123456789);writer.Write(zero?0:123456.789);
                foreach(var quantity in new[]{.75,-.625,.5,-.25}){writer.Write(zero?0:quantity);PipeScaledValue.WriteExponent(writer,zero?0:enormous);}
                writer.Write(zero?0u:2u);
                if(!zero){writer.Write(1UL);writer.Write(.5);writer.Write(255UL);writer.Write(40.25);}
                if(observed)
                {
                    writer.Write(zero?0u:1u);
                    if(!zero){writer.Write(1UL);writer.Write(1e100);writer.Write(1e100);}
                }
            }
            return stream.ToArray();
        }
        var preciseImage=ReceiptImage();var precise=PipeReceipt.Decode(preciseImage,42);
        Check(precise.Sequence==42&&precise.CompletedFrame==100&&precise.Parcels.Length==2,"precise receipt identity/zero record lost");
        Check(precise.Parcels[0].PublicCell==35&&precise.Parcels[0].Mass==.4&&precise.Parcels[0].InternalEnergy==123456.789&&
            precise.Parcels[0].Passives[1].Quantity==40.25&&precise.Parcels[0].Kinetic.Exponent==enormous,"precise committed quantities rounded");
        Check(precise.Parcels[1].Injected&&precise.Parcels[1].Mass==0,"zero injected parcel omitted");
        var observedReceipt=PipeReceipt.Decode(ReceiptImage(true),42);
        var observation=observedReceipt.Parcels[0].Observations.Single();
        Check(observedReceipt.WireVersion==2&&observation.Before==1e100&&observation.After==1e100,"actual passive storage observation changed");
        var representationRemainder=new PipeAmount(observation.Before).Plus(new PipeAmount(.5)).Minus(new PipeAmount(observation.After));
        Check(representationRemainder.Minus(new PipeAmount(.5)).Sign==0,"native rounded-away passive increment lost its remainder account");
        Reject(()=>PipeReceipt.Decode(preciseImage,43));Reject(()=>PipeReceipt.Decode(preciseImage[..^1],42));
        Reject(()=>PipeReceipt.Decode(preciseImage.Concat(new byte[]{0}).ToArray(),42));
        var brokenReceipt=(byte[])preciseImage.Clone();BinaryPrimitives.WriteUInt32LittleEndian(brokenReceipt.AsSpan(24),uint.MaxValue);
        Reject(()=>PipeReceipt.Decode(brokenReceipt,42));
        brokenReceipt=(byte[])preciseImage.Clone();brokenReceipt[76]=(byte)'+';Reject(()=>PipeReceipt.Decode(brokenReceipt,42));
        Console.WriteLine("PASS precise pipe receipts: committed binary64 values, every passive/zero parcel, huge scaled mechanics, exact sparse accounts and malformed framing");
        PortSettlementTests.Run();
        SurfaceGermTests.Run();
        GermEvolutionTests.Run();
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
