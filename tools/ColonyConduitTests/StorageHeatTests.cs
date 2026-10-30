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
    private static void RegistrationBeforeWithdrawal()
    {
        var store=new OwnedFluidStore(new StoredFluidAddress(4,0),2,PipeInventory.Single(Water,2,300,0,.25));
        var initial=Image(store);var destination=PipeInventory.Empty;
        bool Dispense(bool ready)
        {
            var move=store.PrepareWithdrawalTo(ready,destination,double.MaxValue,10,Material);
            if(move==null)return false;
            destination=move.Outside;store.Commit(move);return true;
        }
        Check(!Dispense(OwnedFluidStore.NativeProjectionReady(2,true,-1))&&destination.Count==0&&Image(store).SequenceEqual(initial),
            "An unregistered projection let fluid escape before its initial native heat could be settled.");
        // The actual callback's f32 capacity is kJ/K. Two kg at 1000 J/kg/K
        // cool by exactly half a kelvin before registration is delivered.
        var work=PipeContactWork.IntoFluid(2,300,299.5f).Negated();
        Check(work.Minus(new PipeAmount(-1000)).Sign==0,"Registration work oracle disagrees with exact -1000 J.");
        store.AddHeat(work,Material);
        Check(Dispense(OwnedFluidStore.NativeProjectionReady(2,true,1))&&store.Inventory.Count==0&&destination.Mass==2&&destination.Temperature(Material)==299.5&&
            destination.Germs.Length==1&&destination.Germs[0].Amount.Minus(new PipeAmount(.25)).Sign==0&&
            destination.Energy.Minus(new PipeAmount(599000)).Sign==0&&store.Audit(),
            "Registered withdrawal lost its mass, late heat, culture or closed physical journal.");
        var insulated=new OwnedFluidStore(new StoredFluidAddress(6,0),2,PipeInventory.Single(Water,2,300,0,.25));
        var insulatedMove=insulated.PrepareWithdrawalTo(OwnedFluidStore.NativeProjectionReady(2,false,-1),
            PipeInventory.Empty,double.MaxValue,10,Material);
        Check(insulatedMove!=null,"An insulated carrier waited for a native thermal registration that cannot occur.");
        insulated.Commit(insulatedMove!);
        Check(insulated.Inventory.Count==0&&insulatedMove!.Outside.Mass==2&&
            insulatedMove.Outside.Energy.Minus(new PipeAmount(600000)).Sign==0&&
            insulatedMove.Outside.Germs[0].Amount.Minus(new PipeAmount(.25)).Sign==0&&insulated.Audit(),
            "Insulated withdrawal lost physical mass, energy, culture or its journal.");
        // The old ordering really is invalid: heat cannot be assigned to a
        // source whose complete fluid parcel has already left.
        var old=new OwnedFluidStore(new StoredFluidAddress(5,0),2,PipeInventory.Single(Water,2,300));
        old.Commit(old.PrepareWithdrawalTo(PipeInventory.Empty,double.MaxValue,10,Material));
        Refuse(()=>old.AddHeat(work,Material));
        Console.WriteLine("PASS native registration precedes withdrawal: pending transfer is inert, exact initial contact work travels with mass and germs, and old empty-source heat is refused");
    }
    private static void PreparedNativeEndpoints()
    {
        var store=new OwnedFluidStore(new StoredFluidAddress(7,0),2,PipeInventory.Single(Water,2,300,0,.25));
        // This is the late segment from a previously posted native frame, not
        // merely its registration callback. Settle it before making the parcel.
        store.AddHeat(PipeContactWork.IntoFluid(2,300,299.5f).Negated(),Material);
        var move=store.PrepareWithdrawalTo(PipeInventory.Empty,double.MaxValue,10,Material);
        var endpoints=new[]{new StorageThermalEndpoint(17,299.5f,0)};
        var targets=new StorageThermalTargets(endpoints);
        endpoints[0]=new StorageThermalEndpoint(17,300,2); // caller cannot alter the prepared batch
        Check(targets.Matches(17,299.5f,0),"The absolute UI write was not suppressed after the native endpoint had already been installed.");
        var transition=new StorageThermalTargets(new[]{new StorageThermalEndpoint(17,299.5f,1)},new[]{new StorageThermalEndpoint(17,300,2)},new[]{new StorageThermalEndpoint(17,299.25f,2)});
        Check(transition.Transition(17,300,1)&&transition.Transition(17,299.5f,2)&&transition.Transition(17,299.25f,1)&&transition.Transition(17,299.5f,1),
            "A legitimate separate mass/temperature setter tried to queue an intermediate native endpoint.");
        Refuse(()=>transition.Matches(17,300,1));
        Refuse(()=>transition.Transition(17,301,1));
        Refuse(()=>transition.Transition(17,299.5f,3));
        Refuse(()=>new StorageThermalTargets(new[]{new StorageThermalEndpoint(17,300,1)},Array.Empty<StorageThermalEndpoint>()));
        var before=Image(store);
        Refuse(()=>targets.Matches(17,300,2));
        Refuse(()=>targets.Matches(17,299.5f,BitConverter.Int32BitsToSingle(unchecked((int)0x80000000))));
        Refuse(()=>new StorageThermalTargets(new[]{new StorageThermalEndpoint(17,300,2),new StorageThermalEndpoint(17,299,2)}));
        Refuse(()=>new StorageThermalTargets(new[]{default(StorageThermalEndpoint)}));
        Check(Image(store).SequenceEqual(before)&&!targets.Matches(18,300,2),
            "A refused or foreign projection changed physical storage or suppressed an unowned native write.");
        store.Commit(move);
        Check(store.Inventory.Count==0&&move.Outside.Energy.Minus(new PipeAmount(599000)).Sign==0&&
            move.Outside.Mass==2&&move.Outside.Germs[0].Amount.Minus(new PipeAmount(.25)).Sign==0&&store.Audit(),
            "A fenced full withdrawal left native work behind or changed mass, germs or the physical journal.");
        var reused=new StorageThermalTargets(new[]{new StorageThermalEndpoint(unchecked((int)0x81000011),300,2)});
        Check(reused.Matches(unchecked((int)0x81000011),300,2)&&!reused.Matches(17,300,2),
            "A reused native slot was confused with its older handle generation.");
        Check(System.Runtime.InteropServices.Marshal.SizeOf<StorageThermalEndpoint>()==16,"The native transfer endpoint ABI changed.");
        Console.WriteLine("PASS fenced storage projection: late work travels with the full parcel; exact installed endpoints suppress only matching native writes; stale, malformed and reused-handle substitutions refuse");
    }
    private static void ValidateAdmissionBeforeCommit()
    {
        var store=new OwnedFluidStore(new StoredFluidAddress(8,0),2,PipeInventory.Single(Water,2,300));
        var incoming=PipeInventory.Single(Water,1,280,0,.25);
        var move=store.PrepareAdmission(incoming,10,Material);var before=Image(store);
        store.Validate(move);
        Check(Image(store).SequenceEqual(before),"Admission prevalidation changed the held inventory or journal.");
        var foreign=new OwnedFluidStore(new StoredFluidAddress(9,0),2,PipeInventory.Empty);
        Refuse(()=>foreign.Validate(move));
        Check(foreign.PristineEmpty&&Image(store).SequenceEqual(before),"Foreign admission validation changed a storage owner.");
        // Earlier native work can invalidate a prepared parcel without moving
        // source fluid. Validation must catch it before the paired pipe commit.
        store.AddHeat(new PipeAmount(-1000),Material);var settled=Image(store);
        Refuse(()=>store.Validate(move));
        Check(Image(store).SequenceEqual(settled),"A stale admission changed the settled storage endpoint.");
        move=store.PrepareAdmission(incoming,10,Material);store.Validate(move);store.Commit(move);
        Check(store.Inventory.Mass==3&&store.Inventory.Energy.Minus(new PipeAmount(879000)).Sign==0&&store.Audit(),
            "A re-prepared intake lost prior native work, incoming energy or its journal.");
        Console.WriteLine("PASS storage intake preparation: validation is inert, foreign and heat-stale moves refuse before commit, and re-prepared energy and journals close");
    }
    internal static void Run()
    {
        RegistrationBeforeWithdrawal();
        PreparedNativeEndpoints();
        ValidateAdmissionBeforeCommit();
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
