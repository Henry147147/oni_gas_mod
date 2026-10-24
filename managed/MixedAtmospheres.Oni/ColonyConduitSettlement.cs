using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    internal readonly struct PipeNativeJobState
    {
        internal readonly uint State;internal readonly ulong LastReceipt;
        internal PipeNativeJobState(uint state,ulong lastReceipt){State=state;LastReceipt=lastReceipt;}
    }

    // Valid only after the submitter is sealed, Wait has completed, and every
    // committed outcome has been credited and acknowledged. Native pending
    // tokens then form one suffix; validating the boundaries avoids scanning
    // potentially years of retired token history.
    internal static class PipePortSaveBoundary
    {
        internal static void Validate(PipeBoundaryState saved,Func<ulong,PipeNativeJobState> read)
        {
            var queued=saved.Jobs.Where(j=>j.NativeToken!=0).OrderBy(j=>j.NativeToken).ToArray();
            var first=queued.Length==0?saved.NextNativeToken:queued[0].NativeToken;
            if((ulong)queued.Length!=saved.NextNativeToken-first)
                throw new InvalidDataException("Saved pending pipe jobs are not the native token suffix.");
            for(var i=0;i<queued.Length;++i)
            {
                if(queued[i].NativeToken!=first+(ulong)i)throw new InvalidDataException("Saved pending pipe job token is missing.");
                Require(read(queued[i].NativeToken),1);
            }
            if(first>1)Require(read(first-1),3);
            Require(read(saved.NextNativeToken),0);
        }
        private static void Require(PipeNativeJobState actual,uint expected)
        {
            if(actual.State!=expected||actual.LastReceipt!=0)
                throw new InvalidDataException("Saved pipe jobs do not match the sealed native queue.");
        }
    }

    // A job may have more outcomes than a single legacy copy buffer can hold.
    // Keep its partial receipt group transiently and do not settle on the first
    // parcel. Completion is established by the native last-outcome watermark.
    internal sealed class PipePortReceiptGroup
    {
        private readonly SortedDictionary<ulong,PipeReceipt> receipts=new SortedDictionary<ulong,PipeReceipt>();
        internal int Count=>receipts.Count;
        internal void Add(PipeReceipt receipt)
        {
            if(receipt.Sequence==0||receipts.ContainsKey(receipt.Sequence))throw new InvalidDataException("Duplicate pipe job receipt.");
            if(receipt.WireVersion!=2)throw new InvalidDataException("Pipe jobs require actual native passive observations.");
            receipts.Add(receipt.Sequence,receipt);
        }
        internal PipeReceipt[] Complete(PipeNativeJobState native,ulong copiedThrough)
        {
            if(native.State!=2||native.LastReceipt==0||copiedThrough<native.LastReceipt)
                throw new InvalidOperationException("Pipe job outcomes have not all been copied.");
            if(!receipts.ContainsKey(native.LastReceipt)||receipts.Keys.Last()!=native.LastReceipt)
                throw new InvalidDataException("Pipe job outcome watermark is missing or inconsistent.");
            return receipts.Values.ToArray();
        }
    }

    internal sealed class PipePortSettlement
    {
        // Injection: undispatched reservation. Extraction: actual committed
        // mixture, possibly including quantum-cleanup species. The caller keeps
        // this in a durable port buffer until the pipe can accept all of it.
        internal readonly PipeInventory Inventory;
        internal PipeInventory WorldInventory { get; private set; }=PipeInventory.Empty;
        internal PipePassiveBalance[] WorldPassives { get; private set; }=Array.Empty<PipePassiveBalance>();
        internal readonly PipeAmount Heat;
        internal readonly PipePassiveBalance[] PassiveRemainders;
        internal readonly PipeScaledAccount Kinetic=new PipeScaledAccount(),Potential=new PipeScaledAccount(),
            MomentumX=new PipeScaledAccount(),MomentumY=new PipeScaledAccount();

        private PipePortSettlement(PipeInventory inventory,PipeAmount heat,IEnumerable<PipePassiveBalance> passives)
        {Inventory=inventory;Heat=heat;PassiveRemainders=passives.Where(p=>p.Quantity.Sign!=0).OrderBy(p=>p.Identity).ToArray();}

        internal static PipePortSettlement Calculate(PipePortJob job,IReadOnlyList<PipeReceipt> outcomes,Func<int,PipeMaterial> material)
        {
            if(outcomes.Count==0)throw new InvalidDataException("Pipe job has no committed outcome.");
            var masses=new SortedDictionary<int,PipeAmount>();var germs=new SortedDictionary<ulong,PipeAmount>();
            var observed=new SortedDictionary<ulong,PipeAmount>();var energy=PipeAmount.Zero;var total=PipeAmount.Zero;
            var parcels=new List<PipeReceiptParcel>();var previous=0UL;var frame=outcomes[0].CompletedFrame;
            foreach(var receipt in outcomes)
            {
                if(receipt.WireVersion!=2||receipt.Sequence<=previous||receipt.CompletedFrame!=frame)
                    throw new InvalidDataException("Invalid grouped pipe outcome framing.");
                previous=receipt.Sequence;
                foreach(var parcel in receipt.Parcels)
                {
                    if(parcel.Injected!=(job.Kind==1))throw new InvalidDataException("Pipe job receipt direction mismatch.");
                    if(parcel.Mass>0)
                    {
                        if(parcel.NativeIndex>int.MaxValue||job.Kind==1&&parcel.NativeIndex!=job.NativeIndex)
                            throw new InvalidDataException("Pipe job receipt constituent mismatch.");
                        var id=(int)parcel.NativeIndex;material(id); // require a known material before crediting it
                        Add(masses,id,new PipeAmount(parcel.Mass));total=total.Plus(new PipeAmount(parcel.Mass));
                    }
                    else if(parcel.InternalEnergy!=0||parcel.Passives.Any(p=>p.Quantity!=0))
                        throw new InvalidDataException("Massless pipe receipt carries energy or passives.");
                    energy=energy.Plus(new PipeAmount(parcel.InternalEnergy));
                    foreach(var passive in parcel.Passives)
                    {
                        ValidateIdentity(passive.Identity);Add(germs,passive.Identity,new PipeAmount(passive.Quantity));
                    }
                    foreach(var observation in parcel.Observations)
                    {
                        ValidateIdentity(observation.Identity);
                        var change=new PipeAmount(observation.After).Minus(new PipeAmount(observation.Before));
                        Add(observed,observation.Identity,job.Kind==1?change:PipeAmount.Zero.Minus(change));
                    }
                    // Every supplied identity, even one rounded away in native
                    // storage, must have an actual before/after observation.
                    foreach(var passive in parcel.Passives)
                        if(passive.Quantity!=0&&!parcel.Observations.Any(o=>o.Identity==passive.Identity))
                            throw new InvalidDataException("Pipe passive has no native representation observation.");
                    parcels.Add(parcel);
                }
            }
            PipeInventory inventory;PipeAmount heat;
            var remainders=new SortedDictionary<ulong,PipeAmount>();
            if(job.Kind==1)
            {
                if(total.Minus(new PipeAmount(job.RequestedMass)).Sign>0||total.Minus(job.Reserved.Total).Sign>0)
                    throw new InvalidDataException("Native injection exceeds its pipe reservation.");
                var remaining=job.Reserved.Total.Minus(total);
                // Compute the smaller side at the reservation temperature and
                // leave its exact complement in the larger side. A rounded
                // ratio of one must not strip heat from a retained 1e-240 trace.
                var item=material((int)job.NativeIndex);var temperature=job.Reserved.Temperature(material);
                PipeAmount AtTemperature(PipeAmount amount)=>amount.Times(item.HeatCapacity*temperature).Plus(amount.Times(item.EnergyOffset));
                var consumedEnergy=total.Sign==0?PipeAmount.Zero:remaining.Sign==0?job.Reserved.Energy:
                    remaining.Minus(total).Sign<=0?job.Reserved.Energy.Minus(AtTemperature(remaining)):AtTemperature(total);
                heat=consumedEnergy.Minus(energy);
                var retainedGerms=new List<PipePart>();
                foreach(var germ in job.Reserved.Germs)
                {
                    var id=checked((ulong)germ.Id+1);germs.TryGetValue(id,out var supplied);
                    var reservedPassive=job.Passives.Where(p=>p.Identity==id).Select(p=>new PipeAmount(p.Quantity)).Aggregate(PipeAmount.Zero,(a,b)=>a.Plus(b));
                    if(supplied.Minus(reservedPassive).Sign>0)throw new InvalidDataException("Native injection exceeds its reserved passive request.");
                    var retained=germ.Amount.Minus(supplied);
                    if(retained.Sign<0)throw new InvalidDataException("Native injection exceeds its reserved passives.");
                    if(retained.Sign>0)
                    {
                        if(remaining.Sign>0)retainedGerms.Add(new PipePart(germ.Id,retained));
                        else Add(remainders,id,retained);
                    }
                }
                foreach(var id in germs.Keys)
                    if(!job.Reserved.Germs.ToArray().Any(g=>checked((ulong)g.Id+1)==id))
                        throw new InvalidDataException("Native injection contains an unreserved passive.");
                inventory=remaining.Sign==0?PipeInventory.Empty:PipeInventory.Conserved(
                    new[]{new PipePart((int)job.NativeIndex,remaining)},job.Reserved.Energy.Minus(consumedEnergy),retainedGerms.ToArray());
                foreach(var id in germs.Keys.Union(observed.Keys))
                {germs.TryGetValue(id,out var supplied);observed.TryGetValue(id,out var actual);Add(remainders,id,supplied.Minus(actual));}
            }
            else
            {
                heat=PipeAmount.Zero;
                var diseases=new List<PipePart>();
                foreach(var germ in germs)
                {
                    if(germ.Value.Sign==0)continue;
                    if(germ.Key>(ulong)int.MaxValue+1)throw new InvalidDataException("Pipe germ identity cannot be represented by the current inventory schema.");
                    diseases.Add(new PipePart((int)(germ.Key-1),germ.Value));
                }
                inventory=PipeInventory.Conserved(masses.Select(p=>new PipePart(p.Key,p.Value)).ToArray(),energy,diseases.ToArray());
                foreach(var id in germs.Keys.Union(observed.Keys))
                {germs.TryGetValue(id,out var credited);observed.TryGetValue(id,out var actual);Add(remainders,id,actual.Minus(credited));}
            }
            if(inventory.Count>0)inventory.Temperature(material);
            var result=new PipePortSettlement(inventory,heat,remainders.Select(p=>new PipePassiveBalance(p.Key,p.Value)));
            result.WorldInventory=PipeInventory.Conserved(masses.Select(p=>new PipePart(p.Key,p.Value)).ToArray(),energy,Array.Empty<PipePart>());
            result.WorldPassives=observed.Where(p=>p.Value.Sign!=0).Select(p=>new PipePassiveBalance(p.Key,p.Value)).ToArray();
            foreach(var parcel in parcels)
            {
                var sign=job.Kind==1?-1:1;
                result.Kinetic.Add(new PipeScaledValue(sign*parcel.Kinetic.Mantissa,parcel.Kinetic.Exponent));
                result.Potential.Add(new PipeScaledValue(sign*parcel.Potential.Mantissa,parcel.Potential.Exponent));
                result.MomentumX.Add(new PipeScaledValue(sign*parcel.MomentumX.Mantissa,parcel.MomentumX.Exponent));
                result.MomentumY.Add(new PipeScaledValue(sign*parcel.MomentumY.Mantissa,parcel.MomentumY.Exponent));
            }
            return result;
        }
        private static void ValidateIdentity(ulong identity)
        {if(identity==0||identity>0x100000000UL)throw new InvalidDataException("Unknown pipe passive identity.");}
        private static void Add<T>(IDictionary<T,PipeAmount> amounts,T id,PipeAmount quantity)
        {amounts.TryGetValue(id,out var before);amounts[id]=before.Plus(quantity);}
    }
}
