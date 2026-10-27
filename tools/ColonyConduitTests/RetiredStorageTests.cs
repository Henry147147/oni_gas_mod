using System;
using System.IO;
using System.Linq;
using MixedAtmospheres.Oni;
using MixedAtmospheres.Core;

internal static class RetiredStorageTests
{
    static PipeMaterial Material(int id)=>new PipeMaterial(id,1000,0);
    static void Check(bool value,string why){if(!value)throw new Exception(why);}
    static byte[] Encode(RetiredFluidStorage history)
    {using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);history.Write(writer);return stream.ToArray();}
    static void Reject(Action action)
    {
        try{action();}catch(Exception error)when(error is InvalidDataException||error is InvalidOperationException||error is EndOfStreamException){return;}
        throw new Exception("invalid completed storage history accepted");
    }
    internal static RetiredFluidStorage Completed(byte phase=2)
    {
        var store=new OwnedFluidStore(new StoredFluidAddress(173,0),phase,PipeInventory.Single(Material(2),1,300,0,.25));
        store.Commit(store.PrepareAdmission(PipeInventory.Single(Material(2),1,350,0,1e308),10,Material));
        store.AddHeat(new PipeAmount(-1),Material);
        store.CommitCulture(0,new GermPopulationStep(store.Inventory.Germs[0].Amount.Plus(new PipeAmount(.5)),new PipeAmount(.5),new GermPopulationClock(true,0)),
            new StoredGermContext(17,int.MinValue,.75f,false));
        Reject(()=>RetiredFluidStorage.From(store));
        store.Commit(store.PrepareWithdrawal(double.MaxValue,Material));
        Check(store.Clocks.Count==0&&store.CultureContexts.Count==0&&store.Audit(),"drain did not close completed storage");
        return RetiredFluidStorage.From(store);
    }
    internal static void Run()
    {
        var history=Completed();var bytes=Encode(history);
        using(var stream=new MemoryStream(bytes))using(var reader=new BinaryReader(stream))
        {
            var cold=RetiredFluidStorage.Read(reader);
            Check(cold.Audit()&&Encode(cold).SequenceEqual(bytes)&&cold.Heat.Units==-CoupledAccountQuantity.FromDouble(1).Binary64Units&&
                cold.Births[0].Units==CoupledAccountQuantity.FromDouble(.5).Binary64Units&&cold.Initial.Masses[2].Value==1&&cold.Admitted.Masses[2].Value==1&&
                cold.Withdrawn.Masses[2].Value==2,"completed history lost initial/admitted/withdrawn/heat/culture quantities");
        }
        var joined=history.Joined(history).Joined(history);
        Check(joined.Audit()&&joined.Admitted.Germs[0].Units==CoupledAccountQuantity.FromDouble(1e308).Binary64Units*3&&
            Encode(joined).SequenceEqual(Encode(history.Joined(history.Joined(history)))),"phase aggregation overflowed or changed exact history");
        Reject(()=>history.Joined(Completed(1)));
        Reject(()=>RetiredFluidStorage.From(new OwnedFluidStore(new StoredFluidAddress(1,0),2,PipeInventory.Empty)));
        Reject(()=>new OwnedFluidStorageImage(Array.Empty<OwnedFluidStore>(),new[]{history,history}));
        var gas=Completed(1);
        byte[] Section(int count,params RetiredFluidStorage[] histories)
        {using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);writer.Write(0);writer.Write(count);foreach(var item in histories)item.Write(writer);return stream.ToArray();}
        OwnedFluidStorageImage ReadSection(byte[] image)
        {using var stream=new MemoryStream(image);using var reader=new BinaryReader(stream);return OwnedFluidStorageImage.Read(reader,true);}
        var both=ReadSection(Section(2,gas,history));
        Check(both.Stores.Length==0&&both.Retired.Select(item=>item.Phase).SequenceEqual(new byte[]{1,2}),"completed phase list lost its canonical identities");
        Reject(()=>ReadSection(Section(0)));Reject(()=>ReadSection(Section(3)));
        Reject(()=>ReadSection(Section(2,history,gas)));Reject(()=>ReadSection(Section(2,history,history)));
        foreach(var cut in new[]{0,1,bytes.Length/2,bytes.Length-1})
            Reject(()=>{using var stream=new MemoryStream(bytes.Take(cut).ToArray());using var reader=new BinaryReader(stream);RetiredFluidStorage.Read(reader);});
        var invalid=(byte[])bytes.Clone();invalid[0]=3;
        Reject(()=>{using var stream=new MemoryStream(invalid);using var reader=new BinaryReader(stream);RetiredFluidStorage.Read(reader);});
        // A valid framing with a missing heat credit must fail the physical audit.
        using(var stream=new MemoryStream())
        {
            using(var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true))
            {writer.Write(history.Phase);history.Initial.Write(writer);history.Admitted.Write(writer);history.Withdrawn.Write(writer);
                default(StorageJournalAmount).Write(writer);writer.Write(history.Births.Count);foreach(var pair in history.Births){writer.Write(pair.Key);pair.Value.Write(writer);}writer.Write(0);}
            stream.Position=0;using var reader=new BinaryReader(stream);Reject(()=>RetiredFluidStorage.Read(reader));
        }
        Console.WriteLine("PASS completed storage: exact wide phase aggregation, heat/culture/initial history and cold bytes; nonempty, pristine, foreign-phase, corrupt and truncated records refuse");
    }
}
