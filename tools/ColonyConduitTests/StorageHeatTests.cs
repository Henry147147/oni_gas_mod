using System;
using System.IO;
using System.Linq;
using MixedAtmospheres.Oni;

internal static class StorageHeatTests
{
    private static readonly PipeMaterial Water=new PipeMaterial(1,1000,0);
    private static PipeMaterial Material(int id)=>id==1?Water:throw new ArgumentException();
    private static void Check(bool condition,string message){if(!condition)throw new Exception(message);}
    private static byte[] Image(OwnedFluidStore store)
    {using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);store.Write(writer);return stream.ToArray();}
    private static void Refuse(Action action)
    {try{action();}catch(Exception error)when(error is InvalidOperationException||error is ArgumentException){return;}throw new Exception("Invalid prepared storage heat was accepted.");}
    internal static void Run()
    {
        var first=new OwnedFluidStore(new StoredFluidAddress(1,0),2,PipeInventory.Single(Water,1,300));
        var second=new OwnedFluidStore(new StoredFluidAddress(2,0),2,PipeInventory.Single(Water,1,300));
        var empty=new OwnedFluidStore(new StoredFluidAddress(3,0),2,PipeInventory.Empty);
        Check(empty.PristineEmpty&&!first.PristineEmpty,"Empty scheduling rebind mistook a held inventory for an untouched host.");
        empty.Commit(empty.PrepareAdmission(PipeInventory.Single(Water,1e-240,300),1,Material));
        empty.Commit(empty.PrepareWithdrawal(1,Material));
        Check(empty.Inventory.Count==0&&!empty.PristineEmpty,"Drained trace storage erased its continuation history at the empty-host gate.");
        var before=Image(first);var other=Image(second);
        var pending=first.PrepareHeat(new PipeAmount(-1000),Material);
        Check(Image(first).SequenceEqual(before),"Preparing a heat credit changed physical storage.");
        Refuse(()=>second.PrepareHeat(new PipeAmount(-1e9),Material));
        Check(Image(first).SequenceEqual(before)&&Image(second).SequenceEqual(other),"A refused heat batch changed an earlier store.");
        first.Commit(pending);Check(first.Inventory.Temperature(Material)==299&&first.Audit(),"Prepared heat did not enter physical inventory and journal together.");
        var committed=Image(first);Refuse(()=>first.Commit(pending));Refuse(()=>second.Commit(pending));
        Check(Image(first).SequenceEqual(committed)&&Image(second).SequenceEqual(other),"Stale/foreign prepared heat changed a store.");
        var stale=first.PrepareHeat(new PipeAmount(-1),Material);
        first.Commit(first.PrepareWithdrawal(.125,Material));var transferred=Image(first);
        Refuse(()=>first.Commit(stale));Check(Image(first).SequenceEqual(transferred),"A heat receipt ignored an intervening transfer.");
        // Actual native cooling can continue after publication and before an
        // absolute projection reset. Its extra segment is physical heat; the
        // reset itself is excluded. Different capacities stay separate.
        var cooling=PipeContactWork.IntoFluid(18,300,299.5f).Negated()
            .Plus(PipeContactWork.IntoFluid(18,299.5f,299).Negated())
            .Plus(PipeContactWork.IntoFluid(7,310,309).Negated());
        Check(cooling.Minus(new PipeAmount(-25000)).Sign==0,"Delayed native heat segment/capacity work was rounded or omitted.");
        second.Commit(second.PrepareHeat(cooling,Material));
        Check(second.Inventory.Temperature(Material)==275&&second.Heat.Value==-25000&&second.Audit(),"Complete native heat did not close exact storage.");
        using var stream=new MemoryStream(Image(second));using var reader=new BinaryReader(stream);
        var cold=OwnedFluidStore.Read(reader);Check(Image(cold).SequenceEqual(Image(second)),"Native heat credit/journal changed during cold encoding.");
        var trace=new PipeAmount(1e-240);
        second.AddHeat(trace,Material);cold.AddHeat(trace,Material);
        Check(Image(cold).SequenceEqual(Image(second))&&cold.Audit(),"Stored heat continuation discarded a sub-temperature-resolution receipt.");
        second.Commit(second.PrepareWithdrawal(double.MaxValue,Material));
        var drainedBytes=Image(second);
        using var drainedStream=new MemoryStream(drainedBytes);using var drainedReader=new BinaryReader(drainedStream);
        var drained=OwnedFluidStore.Read(drainedReader);
        bool CompleteHistory(OwnedFluidStore store)=>store.Inventory.Count==0&&store.Heat.Units==second.Heat.Units&&
            store.Withdrawn.Masses.Count==1&&store.Withdrawn.Masses[1].Units==StorageJournalAmount.From(new PipeAmount(1)).Units;
        Check(CompleteHistory(drained)&&!drained.PristineEmpty&&drained.Audit()&&Image(drained).SequenceEqual(drainedBytes),
            "A drained cold store lost the heat/withdrawal history behind its empty native view.");
        // Inject the old dangerous substitution: a fresh empty host is valid
        // and audits, but is not the same completed physical history.
        var lostHistory=new OwnedFluidStore(second.Address,second.Phase,PipeInventory.Empty);
        Check(lostHistory.Audit()&&!CompleteHistory(lostHistory),"Drained-history oracle accepted a fresh empty-host substitution.");
        Console.WriteLine("PASS exact storage heat settlement: atomic preparation, stale/foreign refusal, delayed cooling across resets, changed capacities and trace-preserving cold continuation");
    }
}
