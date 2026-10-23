using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using MixedAtmospheres.Oni;

static class PortSettlementTests
{
    static readonly PipeMaterial A=new(1,1000,0),B=new(2,2000,2e6);
    static PipeMaterial Material(int id)=>id==1?A:id==2?B:throw new InvalidDataException("Unknown species");
    static void Check(bool ok,string why){if(!ok)throw new Exception(why);}
    static void Exact(PipeAmount a,PipeAmount b,string why)=>Check(a.Minus(b).Sign==0,why);
    static void Reject(Action action){try{action();}catch(InvalidDataException){return;}throw new Exception("Invalid job settlement accepted");}
    static PipeReceipt Receipt(ulong sequence,params PipeReceiptParcel[] parcels)=>new(){Sequence=sequence,CompletedFrame=17,WireVersion=2,Parcels=parcels};
    static PipeReceiptParcel Parcel(bool inject,int id,double mass,double energy,params PipeReceiptPassive[] passives)=>new(){Injected=inject,NativeIndex=(uint)id,PublicCell=35,Mass=mass,Temperature=300,InternalEnergy=energy,Passives=passives};
    static PipeAmount Germ(PipeInventory inventory,ulong id)=>inventory.Germs.ToArray().Where(g=>(ulong)g.Id+1==id).Select(g=>g.Amount).Aggregate(PipeAmount.Zero,(a,b)=>a.Plus(b));
    static PipeAmount Remainder(PipePortSettlement settled,ulong id)=>settled.PassiveRemainders.Where(p=>p.Identity==id).Select(p=>p.Quantity).Aggregate(PipeAmount.Zero,(a,b)=>a.Plus(b));
    static PipePortJob Inject(PipeInventory reservation,params PipeReceiptPassive[] supplied)=>new(1,1,35,1,35,1,1,0,reservation.Mass,300,reservation,supplied);
    static byte[] Image(PipeInventory inventory){using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);inventory.Write(writer);return stream.ToArray();}
    static byte[] Image(PipeScaledAccount account){using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);account.Write(writer);return stream.ToArray();}
    internal static void Run()
    {
        foreach(var sample in new[]{(before:1.0,quantity:1e-240,after:1.0),(before:1e-240,quantity:1e30,after:1e30),(before:1.0,quantity:.25,after:1.25)})
        {
            var reserve=PipeInventory.Single(A,1,300,0,sample.quantity);var job=Inject(reserve,new PipeReceiptPassive(1,sample.quantity));
            var parcel=Parcel(true,1,1,reserve.Energy.Value,new PipeReceiptPassive(1,sample.quantity));
            parcel.Observations=new[]{new PipeReceiptPassiveObservation(1,sample.before,sample.after)};
            var result=PipePortSettlement.Calculate(job,new[]{Receipt(1,parcel)},Material);
            Check(result.Inventory.Count==0,"Full injection retained mass");Exact(result.Heat,PipeAmount.Zero,"Full injection energy mismatch");
            var actual=new PipeAmount(sample.after).Minus(new PipeAmount(sample.before));
            Exact(result.WorldPassives.Select(p=>p.Quantity).Aggregate(PipeAmount.Zero,(a,b)=>a.Plus(b)),actual,"World-side passive account differs from committed observations");
            Exact(result.WorldInventory.Total,reserve.Total,"World-side injection mass changed");
            Exact(Germ(reserve,1),actual.Plus(Remainder(result,1)),"Injection lost tiny/huge/fractional passive at native double boundary");
        }
        var undispatched=PipeInventory.Single(A,1,300,0,.5).WithDiseaseDelta(0,1e-240);
        var delivered=Parcel(true,1,1,300000,new PipeReceiptPassive(1,.5));delivered.Observations=new[]{new PipeReceiptPassiveObservation(1,0,.5)};
        var full=PipePortSettlement.Calculate(Inject(undispatched,new PipeReceiptPassive(1,.5)),new[]{Receipt(1,delivered)},Material);
        Check(full.Inventory.Count==0,"Massless remainder should be in named account");
        Exact(Remainder(full,1),new PipeAmount(1e-240),"Undispatched fractional passive vanished at final mass injection");
        var reserved=PipeInventory.Single(A,1,300,0,1);var partial=Parcel(true,1,.25,75000.125,new PipeReceiptPassive(1,.25));
        partial.Observations=new[]{new PipeReceiptPassiveObservation(1,0,.25)};
        var settled=PipePortSettlement.Calculate(Inject(reserved,new PipeReceiptPassive(1,1)),new[]{Receipt(1,partial)},Material);
        Exact(settled.Inventory.Total,new PipeAmount(.75),"Partial injection mass remainder changed");
        Exact(settled.Inventory.Energy.Plus(settled.Heat).Plus(new PipeAmount(partial.InternalEnergy)),reserved.Energy,"Actual committed U is not authoritative");
        Exact(Germ(settled.Inventory,1).Plus(new PipeAmount(.25)).Plus(Remainder(settled,1)),Germ(reserved,1),"Partial injection passive closure changed");
        var zero=PipePortSettlement.Calculate(Inject(reserved,new PipeReceiptPassive(1,1)),new[]{Receipt(1,Parcel(true,1,0,0))},Material);
        Check(Image(zero.Inventory).SequenceEqual(Image(reserved))&&zero.Heat.Sign==0&&zero.PassiveRemainders.Length==0,"Refused injection changed durable reservation");

        var retainedTrace=PipeInventory.Single(A,1,300).Joined(PipeInventory.Single(A,1e-240,300));
        var traceInjection=PipePortSettlement.Calculate(Inject(retainedTrace),new[]{Receipt(1,Parcel(true,1,1,300000))},Material);
        Exact(traceInjection.Inventory.Total,new PipeAmount(1e-240),"Injection rounded away undispatched trace mass");
        Check(traceInjection.Inventory.Temperature(Material)==300,"Injection rounded away retained trace heat");
        Exact(traceInjection.Inventory.Energy.Plus(traceInjection.Heat).Plus(new PipeAmount(300000)),retainedTrace.Energy,"Trace injection energy account failed");

        var extraction=new PipePortJob(1,1,35,1,35,0,3,2,1,0,PipeInventory.Empty);
        var trace=Parcel(false,1,1e-240,3e-235,new PipeReceiptPassive(1,1e-240));trace.Observations=new[]{new PipeReceiptPassiveObservation(1,1,1)};
        var second=Parcel(false,2,.5,1.3e6,new PipeReceiptPassive(2,.25));second.Observations=new[]{new PipeReceiptPassiveObservation(2,1,.75)};
        var exponent=BigInteger.Parse("10000000000000000000000000000000000000000");trace.MomentumX=new PipeScaledValue(.75,exponent);
        var receipts=new[]{Receipt(1,trace),Receipt(3,second)};
        var group=new PipePortReceiptGroup();group.Add(receipts[0]);
        try{group.Complete(new PipeNativeJobState(2,3),1);throw new Exception("First parcel settled an unfinished token");}catch(InvalidOperationException){}
        group.Add(receipts[1]);Reject(()=>group.Add(receipts[0]));
        var extracted=PipePortSettlement.Calculate(extraction,group.Complete(new PipeNativeJobState(2,3),3),Material);
        Check(extracted.Inventory.Count==2,"Quantum cleanup or main extraction constituent lost");
        Exact(extracted.Inventory.Total,new PipeAmount(.5).Plus(new PipeAmount(1e-240)),"Tiny extracted mass lost beside bulk");
        Exact(extracted.Inventory.Energy,new PipeAmount(trace.InternalEnergy).Plus(new PipeAmount(second.InternalEnergy)),"Extraction U reconstructed from temperature");
        Exact(Germ(extracted.Inventory,1).Plus(Remainder(extracted,1)),PipeAmount.Zero,"Extraction representation debt lost");
        Exact(Germ(extracted.Inventory,2).Plus(Remainder(extracted,2)),new PipeAmount(.25),"Fractional extraction passive lost");
        var expectedMomentum=new PipeScaledAccount();expectedMomentum.Add(trace.MomentumX);
        Check(Image(extracted.MomentumX).SequenceEqual(Image(expectedMomentum)),"Huge mechanical exponent projected to double");
        var opposite=new PipeScaledAccount();opposite.Add(new PipeScaledValue(-.75,exponent));
        Check(extracted.MomentumX.WithAdded(opposite).IsZero&&Image(extracted.MomentumX).SequenceEqual(Image(expectedMomentum)),"Mechanical account merge changed its source or lost exact cancellation");
        var replay=PipePortSettlement.Calculate(extraction,receipts,Material);
        Check(Image(extracted.Inventory).SequenceEqual(Image(replay.Inventory))&&Image(extracted.MomentumX).SequenceEqual(Image(replay.MomentumX)),"Outcome replay changed exact bytes");
        var unknown=Parcel(true,2,.25,75000);Reject(()=>PipePortSettlement.Calculate(Inject(reserved,new PipeReceiptPassive(1,1)),new[]{Receipt(1,unknown)},Material));
        var unobserved=Parcel(true,1,.25,75000,new PipeReceiptPassive(1,.25));Reject(()=>PipePortSettlement.Calculate(Inject(reserved,new PipeReceiptPassive(1,1)),new[]{Receipt(1,unobserved)},Material));
        var excessive=Parcel(true,1,2,600000);Reject(()=>PipePortSettlement.Calculate(Inject(reserved),new[]{Receipt(1,excessive)},Material));
        var stolen=Parcel(true,1,.25,75000,new PipeReceiptPassive(2,.25));stolen.Observations=new[]{new PipeReceiptPassiveObservation(2,0,.25)};
        Reject(()=>PipePortSettlement.Calculate(Inject(reserved),new[]{Receipt(1,stolen)},Material));
        Reject(()=>PipePortSettlement.Calculate(extraction,new[]{Receipt(3,second),Receipt(1,trace)},Material));
        Reject(()=>PipePortSettlement.Calculate(extraction,new[]{Receipt(1,trace),Receipt(1,second)},Material));

        PipePortJob Job(ulong token)=>new(token,token,35,1,35,0,3,0,1,0,PipeInventory.Empty);
        PipeBoundaryState State(ulong next,params PipePortJob[] jobs)=>new(PipeAmount.Zero,new(),new(),new(),new(),10,next,jobs);
        var calls=0;PipeNativeJobState Native(ulong token){++calls;return new(token<2?3u:token<5?1u:0u,0);}
        PipePortSaveBoundary.Validate(State(5,Job(2),Job(3),Job(4)),Native);Check(calls==5,"Suffix validation scanned retired history");
        Reject(()=>PipePortSaveBoundary.Validate(State(5,Job(3),Job(4)),Native)); // missing first
        Reject(()=>PipePortSaveBoundary.Validate(State(5,Job(2),Job(4)),Native)); // missing middle
        Reject(()=>PipePortSaveBoundary.Validate(State(5,Job(2),Job(3)),Native)); // missing last
        Reject(()=>PipePortSaveBoundary.Validate(State(4,Job(2),Job(3)),Native)); // stale native watermark
        Reject(()=>PipePortSaveBoundary.Validate(State(6,Job(2),Job(3),Job(4)),Native));
        Reject(()=>PipePortSaveBoundary.Validate(State(5),Native));
        PipePortSaveBoundary.Validate(State(5),token=>new(token<5?3u:0u,0));
        Reject(()=>PipePortSaveBoundary.Validate(State(5,Job(2),Job(3),Job(4)),token=>token==3?new PipeNativeJobState(2,7):Native(token)));
        Console.WriteLine("PASS pipe committed settlement: grouped outcomes, actual U and tiny/huge passive remainders, durable reservations, exact mechanics and sealed native suffix adversaries");
    }
}
