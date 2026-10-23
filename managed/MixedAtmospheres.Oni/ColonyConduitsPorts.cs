using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    // Explicit boundary-control runtime only. Live pumps/vents and the save
    // writer remain gated until their component state and durable buffers are
    // included in a separately versioned runtime image.
    internal static class ColonyPipePorts
    {
        private sealed class Pending
        {
            internal readonly PipePortJob Job;internal readonly PipePortReceiptGroup Receipts=new PipePortReceiptGroup();
            internal List<OniNativeColony.Receipt>? Legacy;
            internal Pending(PipePortJob job){Job=job;}
        }
        private sealed class Buffer
        {
            internal readonly byte Phase;internal readonly int Cell;internal PipeInventory Inventory;
            internal Buffer(byte phase,int cell,PipeInventory inventory){Phase=phase;Cell=cell;Inventory=inventory;}
        }
        private static readonly SortedDictionary<ulong,Pending> Jobs=new SortedDictionary<ulong,Pending>();
        private static readonly SortedDictionary<ulong,Buffer> Buffers=new SortedDictionary<ulong,Buffer>();
        private static readonly HashSet<ulong> Seen=new HashSet<ulong>();
        private static SortedDictionary<ulong,PipeAmount> passiveRemainders=new SortedDictionary<ulong,PipeAmount>(),passiveExchange=new SortedDictionary<ulong,PipeAmount>();
        private static PipeScaledAccount kinetic=new PipeScaledAccount(),potential=new PipeScaledAccount(),momentumX=new PipeScaledAccount(),momentumY=new PipeScaledAccount();
        private static ulong nextLocal=1,nextNative=1;
        private static ActorBatch? actorBatch;
        internal sealed class ActorBatch : IDisposable
        {
            private readonly ulong firstLocal=nextLocal,firstNative=nextNative;
            private readonly List<(Pending Pending,uint Index)> prepared=new List<(Pending,uint)>();
            private bool committed,disposed;
            internal ActorBatch()
            {
                Require();if(actorBatch!=null)throw new InvalidOperationException("Nested actor input transactions are unsupported.");
                actorBatch=this;
            }
            internal ulong Extract(List<OniNativeColony.Transfer> ordered,int pipeCell,byte phase,uint worldCell,double mass,uint radius)
            {
                if(disposed||committed||!ReferenceEquals(actorBatch,this))throw new InvalidOperationException("Actor input transaction is closed.");
                var job=new PipePortJob(nextLocal,nextNative,pipeCell,phase,worldCell,0,phase==1?3u:4u,radius,mass,0,PipeInventory.Empty);
                var pending=new Pending(job);prepared.Add((pending,checked((uint)ordered.Count)));Jobs.Add(job.NativeToken,pending);
                ordered.Add(Transfer(job));checked{++nextLocal;++nextNative;}return job.LocalId;
            }
            internal void Commit(List<OniNativeColony.Transfer> ordered)
            {
                if(disposed||committed||!ReferenceEquals(actorBatch,this))throw new InvalidOperationException("Actor input transaction is closed.");
                if(prepared.Count==0)
                    OniNativeColony.Check(OniNativeColony.QueueTransfers(ordered.ToArray(),(uint)ordered.Count));
                else
                {
                    var jobs=prepared.Select(p=>new OniNativeColony.ActorBatchJob{TransferIndex=p.Index,Token=p.Pending.Job.NativeToken}).ToArray();
                    OniNativeColony.Check(OniNativeColony.QueueActorBatch(ordered.ToArray(),(uint)ordered.Count,jobs,(uint)jobs.Length,
                        Array.Empty<OniNativeColony.PipePassive>(),0));
                }
                committed=true;
                foreach(var item in prepared)ColonyPipePumps.Accepted(item.Pending.Job.LocalId);
            }
            public void Dispose()
            {
                if(disposed)return;disposed=true;
                if(!committed)
                {
                    foreach(var item in prepared){Jobs.Remove(item.Pending.Job.NativeToken);ColonyPipePumps.Aborted(item.Pending.Job.LocalId);}
                    nextLocal=firstLocal;nextNative=firstNative;
                }
                if(ReferenceEquals(actorBatch,this))actorBatch=null;
            }
        }
        private static OniNativeColony.Transfer Transfer(PipePortJob job)=>new OniNativeColony.Transfer
        {PublicCell=job.WorldCell,NativeIndex=job.NativeIndex,Kind=job.Kind,Radius=job.Radius,Callback=-1,MassKg=job.RequestedMass,TemperatureK=job.Temperature};
        internal static bool Enabled { get; private set; }
        internal static PipeInventory FromWorld { get; private set; }=PipeInventory.Empty;
        internal static PipeInventory ToWorld { get; private set; }=PipeInventory.Empty;
        internal static PipeAmount HeatRemainder { get; private set; }
        internal static int PendingCount=>Jobs.Count;
        internal static PipeInventory Held
        {
            get{var held=PipeInventory.Empty;foreach(var job in Jobs.Values)held=held.Joined(job.Job.Reserved);foreach(var buffer in Buffers.Values)held=held.Joined(buffer.Inventory);return held;}
        }
        internal static IEnumerable<ulong> PassiveIdentities=>passiveRemainders.Keys.Union(passiveExchange.Keys);
        internal static PipeAmount PassiveRemainder(ulong id)=>passiveRemainders.TryGetValue(id,out var amount)?amount:PipeAmount.Zero;
        internal static PipeAmount PassiveExchange(ulong id)=>passiveExchange.TryGetValue(id,out var amount)?amount:PipeAmount.Zero;
        internal static string Report=>$"ports pending {Jobs.Count}, buffers {Buffers.Count}, from world {FromWorld.Mass:R} kg, to world {ToWorld.Mass:R} kg, retained boundary heat {HeatRemainder.Value:R} J, passive remainders {passiveRemainders.Count}, mechanical bins {kinetic.Bins}/{potential.Bins}/{momentumX.Bins}/{momentumY.Bins}";
        internal static void StartControl()
        {
            if(Enabled)return;
            if(!ColonyConduits.Active||OniNativeColony.PreciseReceiptVersion()<2)throw new NotSupportedException("Precise mixed-pipe boundary control is unavailable.");
            OniNativeColony.Check(OniNativeColony.EnablePreciseReceipts(1));
            if(ReadState(1).State!=0)throw new InvalidOperationException("New pipe control cannot replace an existing native token history.");
            Enabled=true;
        }
        internal static void Reset()
        {
            Enabled=false;actorBatch=null;ColonyPipePumps.Reset();Jobs.Clear();Buffers.Clear();Seen.Clear();nextLocal=nextNative=1;
            FromWorld=ToWorld=PipeInventory.Empty;HeatRemainder=PipeAmount.Zero;
            passiveRemainders.Clear();passiveExchange.Clear();kinetic=new PipeScaledAccount();potential=new PipeScaledAccount();momentumX=new PipeScaledAccount();momentumY=new PipeScaledAccount();
        }
        internal static ulong HoldForPort(ConduitFlow flow,int cell,double mass)
        {
            Require();if(actorBatch!=null)throw new InvalidOperationException("A new port buffer cannot cross an open actor transaction.");
            var inventory=ColonyConduits.TakeForPort(flow,cell,null,mass);if(inventory.Count==0)return 0;
            var id=nextLocal++;Buffers.Add(id,new Buffer(flow.conduitType==ConduitType.Gas?(byte)1:(byte)2,cell,inventory));return id;
        }
        internal static ulong Extract(int pipeCell,byte phase,uint worldCell,double mass,uint radius)
        {
            Require();return Submit(new PipePortJob(nextLocal,nextNative,pipeCell,phase,worldCell,0,phase==1?3u:4u,radius,mass,0,PipeInventory.Empty));
        }
        internal static ulong Inject(ConduitFlow flow,int pipeCell,uint worldCell,int species,double mass,uint radius=0)
        {
            Require();var reservation=ColonyConduits.TakeForPort(flow,pipeCell,species,mass);
            if(reservation.Count==0)return 0;
            try
            {
                var passives=new List<PipeReceiptPassive>();
                foreach(var germ in reservation.Germs)
                {
                    var amount=Floor(germ.Amount);if(amount>0)passives.Add(new PipeReceiptPassive(checked((ulong)germ.Id+1),amount));
                }
                return Submit(new PipePortJob(nextLocal,nextNative,pipeCell,flow.conduitType==ConduitType.Gas?(byte)1:(byte)2,worldCell,
                    (uint)species,1,radius,Floor(reservation.Total),reservation.Temperature(ColonyConduits.Material),reservation,passives));
            }
            catch
            {
                // Once admission was attempted, retain the reservation in its
                // pending record and fail closed. Do not replay an ambiguously
                // accepted native job by restoring its source inventory.
                if(!Jobs.Values.Any(p=>ReferenceEquals(p.Job.Reserved,reservation)))
                {
                    var remaining=ColonyConduits.ReceivePortBuffer(flow,pipeCell,reservation);
                    if(remaining.Count>0)Buffers.Add(nextLocal++,new Buffer(flow.conduitType==ConduitType.Gas?(byte)1:(byte)2,pipeCell,remaining));
                }
                throw;
            }
        }
        private static double Floor(PipeAmount amount)
        {
            var result=amount.Value;
            while(result>0&&new PipeAmount(result).Minus(amount).Sign>0)result=BitConverter.Int64BitsToDouble(BitConverter.DoubleToInt64Bits(result)-1);
            return result;
        }
        private static ulong Submit(PipePortJob job)
        {
            if(actorBatch!=null)throw new InvalidOperationException("An individual pipe job cannot cross an open actor transaction.");
            var transfer=Transfer(job);
            var passives=job.Passives.Select(p=>new OniNativeColony.PipePassive{Identity=p.Identity,Quantity=p.Quantity}).ToArray();
            // Allocate managed bookkeeping before native acceptance. A failed
            // request must never consume a contiguous native token.
            var pending=new Pending(job);Jobs.Add(job.NativeToken,pending);
            var result=OniNativeColony.QueuePipeJob(job.NativeToken,in transfer,passives,(uint)passives.Length,out var state,out var last);
            OniNativeColony.Check(result);
            if(state!=1||last!=0)throw new InvalidOperationException("New pipe token was not queued exactly once.");
            checked{++nextLocal;++nextNative;}return job.LocalId;
        }
        // Called before legacy callbacks and acknowledgement. Repeated copies
        // of the same still-unacknowledged prefix cannot credit a parcel twice.
        internal static bool Receipt(in OniNativeColony.Receipt receipt)
        {
            if(!Enabled)return false;if(Seen.Contains(receipt.Sequence))return true;
            OniNativeColony.Check(OniNativeColony.ReceiptJob(receipt.Sequence,out var token));if(token==0)return false;
            if(!Jobs.TryGetValue(token,out var pending))throw new InvalidDataException("Native pipe outcome has no durable reservation.");
            OniNativeColony.Check(OniNativeColony.CopyPreciseReceipt(receipt.Sequence,null,0,out var count));
            if(count<28||count>ColonyConduitCheckpoint.MaximumBytes)throw new InvalidDataException("Native pipe outcome has no bounded precise sidecar.");
            var bytes=new byte[count];OniNativeColony.Check(OniNativeColony.CopyPreciseReceipt(receipt.Sequence,bytes,count,out var copied));
            if(copied!=count)throw new InvalidDataException("Native pipe outcome changed while copying.");
            var parsed=PipeReceipt.Decode(bytes,receipt.Sequence);pending.Receipts.Add(parsed);
            if(ColonyPipePumps.IsPumpJob(pending.Job.LocalId))(pending.Legacy??=new List<OniNativeColony.Receipt>()).Add(receipt);
            var native=ReadState(token);
            if(native.State!=2||native.LastReceipt<receipt.Sequence)throw new InvalidDataException("Native pipe outcome is not a committed token group.");
            if(native.LastReceipt==receipt.Sequence)
            {
                var settled=PipePortSettlement.Calculate(pending.Job,pending.Receipts.Complete(native,receipt.Sequence),ColonyConduits.Material);
                Credit(pending.Job,settled);Jobs.Remove(token);
                ColonyPipePumps.Completed(pending.Job.LocalId,pending.Legacy);
            }
            Seen.Add(receipt.Sequence);return true;
        }
        private static void Credit(PipePortJob job,PipePortSettlement result)
        {
            var from=job.Kind==1?FromWorld:FromWorld.Joined(result.WorldInventory);
            var to=job.Kind==1?ToWorld.Joined(result.WorldInventory):ToWorld;
            var retainedHeat=HeatRemainder.Plus(result.Heat);
            var remainders=new SortedDictionary<ulong,PipeAmount>(passiveRemainders);var exchange=new SortedDictionary<ulong,PipeAmount>(passiveExchange);
            foreach(var passive in result.PassiveRemainders)Add(remainders,passive.Identity,passive.Quantity);
            foreach(var passive in result.WorldPassives)Add(exchange,passive.Identity,job.Kind==1?passive.Quantity.Negated():passive.Quantity);
            var k=kinetic.WithAdded(result.Kinetic);var g=potential.WithAdded(result.Potential);
            var jx=momentumX.WithAdded(result.MomentumX);var jy=momentumY.WithAdded(result.MomentumY);
            if(result.Inventory.Count>0)Buffers.Add(job.LocalId,new Buffer(job.Phase,job.PipeCell,result.Inventory));
            FromWorld=from;ToWorld=to;HeatRemainder=retainedHeat;passiveRemainders=remainders;passiveExchange=exchange;
            kinetic=k;potential=g;momentumX=jx;momentumY=jy;
        }
        internal static void Acknowledged(ulong through)
        {
            if(!Enabled||Seen.Count==0)return;
            Seen.RemoveWhere(sequence=>sequence<=through);
        }
        internal static bool IsPending(ulong localId)=>Jobs.Values.Any(p=>p.Job.LocalId==localId);
        internal static PipeInventory BufferInventory(ulong localId)=>Buffers.TryGetValue(localId,out var buffer)?buffer.Inventory:PipeInventory.Empty;
        internal static void PlaceBuffer(ulong localId)
        {
            Require();if(!Buffers.TryGetValue(localId,out var buffer))return;
            foreach(var part in buffer.Inventory.Parts)
            {
                var element=ElementLoader.FindElementByHash(ColonyConduits.ElementHash(part.Id));
                if(buffer.Phase==1?!element.IsGas:!element.IsLiquid)return; // quantum-cleanup foreign phase stays owned in the buffer
            }
            var remaining=ColonyConduits.ReceivePortBuffer(Conduit.GetFlowManager(buffer.Phase==1?ConduitType.Gas:ConduitType.Liquid),buffer.Cell,buffer.Inventory);
            if(remaining.Count==0)Buffers.Remove(localId);else buffer.Inventory=remaining;
        }
        // Caller must already have paused submissions, waited, and drained/Acked
        // all committed receipts. This method validates, it does not establish
        // the game save barrier or persist the still-owned port buffers.
        internal static PipeBoundaryState ValidateSealedBoundary()
        {
            Require();if(actorBatch!=null)throw new InvalidOperationException("Actor input transaction must finish before a pipe save barrier.");
            var state=new PipeBoundaryState(HeatRemainder,kinetic,potential,momentumX,momentumY,nextLocal,nextNative,Jobs.Values.Select(p=>p.Job),
                passiveRemainders.Select(p=>new PipePassiveBalance(p.Key,p.Value)));
            PipePortSaveBoundary.Validate(state,ReadState);return state;
        }
        private static PipeNativeJobState ReadState(ulong token)
        {OniNativeColony.Check(OniNativeColony.PipeJobState(token,out var state,out var last));return new PipeNativeJobState(state,last);}
        private static void Add(IDictionary<ulong,PipeAmount> values,ulong id,PipeAmount quantity)
        {values.TryGetValue(id,out var before);var after=before.Plus(quantity);if(after.Sign==0)values.Remove(id);else values[id]=after;}
        private static void Require(){if(!Enabled)throw new InvalidOperationException("Precise pipe boundary control is not active.");}
    }
}
