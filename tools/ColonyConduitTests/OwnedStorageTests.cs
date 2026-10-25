using System;
using System.IO;
using System.Linq;
using MixedAtmospheres.Oni;

internal static class OwnedStorageTests
{
    static PipeMaterial Material(int id)=>new PipeMaterial(id,id==1?1000:2000,id==3?1e6:0);
    static byte[] Image(OwnedFluidStore store){using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);store.Write(writer);return stream.ToArray();}
    static void Check(bool value,string why){if(!value)throw new Exception(why);}
    internal static void Run()
    {
        var water=PipeInventory.Single(Material(1),3,300,0,.25);var dirty=PipeInventory.Single(Material(2),1,400,1,1e-240);var mixture=water.Joined(dirty);
        var tank=new OwnedFluidStore(new StoredFluidAddress(42,0),2,PipeInventory.Empty);
        var admission=tank.PrepareAdmission(mixture,5000,Material);tank.Commit(admission);
        Check(admission.Outside.Count==0&&tank.Inventory.Count==2&&tank.Audit(),"admission did not retain the physical mixture");
        foreach(var pass in new[]{1,2})
        {
            var move=tank.PrepareWithdrawalTo(PipeInventory.Empty,double.MaxValue,2,Material);Check(move.Parcel.SpeciesMass(1)==1.5&&move.Parcel.SpeciesMass(2)==.5,"reservoir fractionated a 75/25 withdrawal");tank.Commit(move);Check(tank.Audit(),"withdrawal failed physical audit");
        }
        Check(tank.Inventory.Count==0,"full withdrawal left bulk fluid");
        var traced=mixture.Joined(PipeInventory.Single(Material(3),1e-240,350,2,.125));
        var tiny=new OwnedFluidStore(new StoredFluidAddress(43,1),2,PipeInventory.Empty);tiny.Commit(tiny.PrepareAdmission(traced,5000,Material));
        var image=Image(tiny);using var stream=new MemoryStream(image);using var reader=new BinaryReader(stream);var cold=OwnedFluidStore.Read(reader);
        Check(Image(cold).SequenceEqual(image)&&cold.Inventory.SpeciesMass(3)==1e-240&&cold.Inventory.Germs.Length==3,"tiny mass or all-ID passives were only journalled");
        var withdrawal=cold.PrepareWithdrawal(double.MaxValue,Material);cold.Commit(withdrawal);
        Check(withdrawal.Parcel.SpeciesMass(3)==1e-240&&withdrawal.Parcel.Germs.Length==3&&cold.Inventory.Count==0&&cold.Audit(),"cold full withdrawal failed to release tiny/passive physical inventory");
        var reagent=new OwnedFluidStore(new StoredFluidAddress(44,0),2,traced);var selected=reagent.PrepareWithdrawal(.75,Material,1);reagent.Commit(selected);
        Check(selected.Parcel.Count==1&&selected.Parcel.SpeciesMass(1)==.75&&reagent.Inventory.SpeciesMass(2)==1&&reagent.Inventory.SpeciesMass(3)==1e-240&&reagent.Audit(),"recipe selection changed unrelated constituents");
        var prepared=reagent.PrepareWithdrawal(.1,Material);reagent.AddHeat(new PipeAmount(1),Material);
        try{reagent.Commit(prepared);throw new Exception("stale reservation accepted");}catch(InvalidOperationException){}
        Console.WriteLine("PASS exact storage: successive 75/25 withdrawals, selective recipe withdrawal and stale reservation refusal");
        Console.WriteLine("PASS exact storage cold roundtrip: 1e-240kg and fractional/all-ID passives physically withdraw from actual saved inventory");

        var culture=new OwnedFluidStore(new StoredFluidAddress(44,0),2,water);
        culture.CommitCulture(0,new GermPopulationStep(new PipeAmount(.5),new PipeAmount(.25),new GermPopulationClock(true,0)),new StoredGermContext(17,int.MinValue,.75f,false));
        Check(culture.Audit()&&culture.Clocks[0].Expected&&culture.Births[0].Value==.25,"owned culture growth failed actual inventory audit");
        using(var memory=new MemoryStream(Image(culture)))using(var input=new BinaryReader(memory))
        {
            var restored=OwnedFluidStore.Read(input);Check(restored.Clocks[0].Expected&&restored.CultureContexts[0].Element==17&&restored.CultureContexts[0].Overpopulation==int.MinValue&&
                restored.CultureContexts[0].TagGrowthBase==.75f&&restored.Audit(),"owned culture clock/cache or growth journal changed after save");
            restored.Commit(restored.PrepareWithdrawal(double.MaxValue,Material));
            Check(restored.Clocks.Count==0&&restored.CultureContexts.Count==0&&restored.Audit(),"empty owned storage retained an extinct culture clock/cache");
        }
        var before=Image(culture);
        try{culture.CommitCulture(0,new GermPopulationStep(new PipeAmount(1),new PipeAmount(.1),new GermPopulationClock(true,0)));throw new Exception("inconsistent culture step accepted");}
        catch(InvalidOperationException){}
        Check(Image(culture).SequenceEqual(before),"invalid culture step partially changed owned storage");
        try{culture.AddHeat(culture.Inventory.Energy.Negated(),Material);throw new Exception("nonpositive storage temperature accepted");}
        catch(InvalidOperationException){}
        Check(Image(culture).SequenceEqual(before),"invalid body heat partially changed owned storage");
        using(var memory=new MemoryStream())
        {
            using(var output=new BinaryWriter(memory,System.Text.Encoding.UTF8,true))new OwnedFluidStorageImage(new[]{tiny,tank}).Write(output);
            var bytes=memory.ToArray();memory.Position=0;using var input=new BinaryReader(memory);var restored=OwnedFluidStorageImage.Read(input);
            Check(restored.Stores[0].Address.Prefab==42&&restored.Stores[1].Address.Ordinal==1,"owned storage persistent identity order changed");
            using var second=new MemoryStream();using var encoded=new BinaryWriter(second);restored.Write(encoded);
            Check(second.ToArray().SequenceEqual(bytes),"owned storage section changed on cold reencoding");
        }
        void Reject(Action action){try{action();}catch(Exception error)when(error is InvalidDataException||error is EndOfStreamException){return;}throw new Exception("invalid owned storage image accepted");}
        Reject(()=>new StoredFluidAddress(-1,0));Reject(()=>new StoredFluidAddress(1,256));
        Reject(()=>new OwnedFluidStorageImage(new[]{tank,tank}));
        Reject(()=>{using var memory=new MemoryStream(Image(tiny)[..^1]);using var input=new BinaryReader(memory);OwnedFluidStore.Read(input);});
        Console.WriteLine("PASS exact storage biology/codec: persistent component identities, clocks and physical growth journals; malformed or inconsistent state refuses atomically");
        var random=new Random(71);
        for(var i=0;i<10000;++i)
        {
            var count=random.Next(1,10_000_000);var residue=(float)(random.NextDouble()*1.98-.99);var delta=(float)((random.NextDouble()-.5)*count);
            var accumulated=delta+residue;var change=(int)accumulated;var actual=Math.Max(0,count+change);
            Check(ColonyStorageGermRounding.TryStep(count,residue,delta,out var next)&&next.Population==actual&&
                BitConverter.SingleToInt32Bits(next.Rounding)==BitConverter.SingleToInt32Bits(actual==0?0:accumulated-(float)change),"storage complete-delta accumulation differs from native order");
        }
        var negativeZero=BitConverter.Int32BitsToSingle(int.MinValue);
        Check(ColonyStorageGermRounding.TryStep(10,negativeZero,negativeZero,out var signedZero)&&BitConverter.SingleToInt32Bits(signedZero.Rounding)==int.MinValue,
            "storage complete-delta accumulation inserted a temperature zero and lost the native signed residue");
        Check(!ColonyStorageGermRounding.TryStep(int.MaxValue,.5f,1024,out _),"native stored culture overflow was silently truncated");
        Console.WriteLine("PASS storage germ rounding: 10000 complete-delta native-order cases, signed-zero residue, extinction and explicit wide-count refusal");
    }
}
