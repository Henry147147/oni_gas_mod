using System;
using System.Collections.Generic;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    // A demolished conduit cannot erase a refused spill. Its inventory stays
    // in ordinary owned port buffers/jobs until the world accepts it.
    internal static class ColonyPipeTeardown
    {
        private sealed class Spill
        {
            internal readonly int Cell;
            internal readonly SortedSet<ulong> Ids=new SortedSet<ulong>();
            internal ulong NextFrame;
            internal Spill(int cell,ulong id,ulong nextFrame){Cell=cell;Ids.Add(id);NextFrame=nextFrame;}
        }
        private static readonly SortedDictionary<ulong,Spill> Spills=new SortedDictionary<ulong,Spill>();
        internal static bool Enabled { get; private set; }
        internal static int ActiveSpills=>Spills.Count;
        internal static long Attempts { get; private set; }
        internal static string LastAdmissionError { get; private set; }="";
        internal static void StartControl()
        {
            if(Enabled)return;
            if(!ColonyPipePorts.Enabled||OniNativeColony.ActorBatchVersion()<1)throw new NotSupportedException("Conservative pipe teardown requires atomic precise ports.");
            Enabled=true;MixtureInspection.SetCellSpillReader(ReadCell);
        }
        internal static void Reset()
        {Enabled=false;Spills.Clear();Attempts=0;LastAdmissionError="";MixtureInspection.SetCellSpillReader(null);}
        internal static bool Dump(ConduitFlow flow,int cell)
        {
            if(!Enabled)return false;
            var id=ColonyPipePorts.HoldForPort(flow,cell,double.MaxValue);
            if(id!=0)
            {
                var spill=new Spill(cell,id,ColonyOwner.Status.CompletedFrames);Spills.Add(id,spill);
                TryDispatch(spill,ColonyOwner.Status.CompletedFrames);
            }
            // The native dump received a by-value contents snapshot before
            // this hook. Suppress that scalar emission; its caller still
            // performs normal conduit cleanup after our complete reservation.
            return false;
        }
        // Any other owned parcel that must leave into the world (wrong-element
        // dumps, phase releases) uses the same retained, retried spill.
        internal static void HoldSpill(ulong id,int worldCell)
        {
            if(!Enabled)throw new InvalidOperationException("Conservative spills are not active.");
            if(id==0)return;
            var spill=new Spill(worldCell,id,ColonyOwner.Status.CompletedFrames);Spills.Add(id,spill);
            TryDispatch(spill,ColonyOwner.Status.CompletedFrames);
        }
        internal static void Capture(PipeRuntimeImage image)
        {
            foreach(var spill in Spills)image.Spills.Add(new PipeRuntimeImage.SpillRecord{Key=spill.Key,Cell=spill.Value.Cell,NextFrame=spill.Value.NextFrame,Ids=spill.Value.Ids.ToArray()});
            image.SpillAttempts=Attempts;
        }
        internal static void Restore(PipeRuntimeImage image)
        {
            Attempts=image.SpillAttempts;
            foreach(var record in image.Spills)
            {
                if(record.Ids.Length==0)throw new System.IO.InvalidDataException("A saved spill holds nothing.");
                var spill=new Spill(record.Cell,record.Ids[0],record.NextFrame);foreach(var id in record.Ids)spill.Ids.Add(id);Spills.Add(record.Key,spill);
            }
        }
        internal static void Tick(ulong frame)
        {
            if(!Enabled||ColonyOwner.Status.State!=1)return;
            foreach(var entry in Spills.ToArray())
            {
                var spill=entry.Value;
                foreach(var id in spill.Ids.ToArray())
                    if(!ColonyPipePorts.IsPending(id)&&ColonyPipePorts.BufferInventory(id).Count==0)spill.Ids.Remove(id);
                if(spill.Ids.Count==0){Spills.Remove(entry.Key);continue;}
                var completed=spill.Ids.Where(id=>!ColonyPipePorts.IsPending(id)).ToArray();
                if(completed.Length>1)
                {
                    var keep=ColonyPipePorts.MergeBuffers(completed);
                    foreach(var id in completed)if(id!=keep)spill.Ids.Remove(id);
                }
                if(frame>=spill.NextFrame)TryDispatch(spill,frame);
            }
        }
        internal static ulong[] DetachWorld(ColonyWorldRegion world)
        {
            var selected=Spills.Where(pair=>world.Contains(pair.Value.Cell%Grid.WidthInCells,pair.Value.Cell/Grid.WidthInCells)).ToArray();
            var ids=selected.SelectMany(pair=>pair.Value.Ids).Distinct().ToArray();
            if(ids.Any(ColonyPipePorts.IsPending))throw new InvalidOperationException("A retiring world's spill still has an outstanding receipt.");
            foreach(var pair in selected)Spills.Remove(pair.Key);
            return ids.Where(id=>ColonyPipePorts.BufferInventory(id).Count>0).ToArray();
        }
        internal static bool HasWorldSpill(ColonyWorldRegion world)=>Spills.Values.Any(spill=>world.Contains(spill.Cell%Grid.WidthInCells,spill.Cell/Grid.WidthInCells)&&
            spill.Ids.Any(id=>ColonyPipePorts.IsPending(id)||ColonyPipePorts.BufferInventory(id).Count>0));
        private static void TryDispatch(Spill spill,ulong frame)
        {
            spill.NextFrame=checked(frame+1);
            foreach(var id in spill.Ids.ToArray())
            {
                if(ColonyPipePorts.IsPending(id)||ColonyPipePorts.BufferInventory(id).Count==0)continue;
                ++Attempts;
                try
                {
                    var jobs=ColonyPipePorts.DispatchBuffer(id,checked((uint)spill.Cell),2);
                    if(ColonyPipePorts.BufferInventory(id).Count==0)spill.Ids.Remove(id);
                    foreach(var job in jobs)spill.Ids.Add(job);
                }
                catch(ColonyPipePorts.AdmissionRejected error)
                {
                    // Strong native admission left this buffer intact; retain
                    // it and retry on a later physics frame, never in a loop.
                    LastAdmissionError=error.Message;
                }
            }
        }
        internal static MixturePipeReading? ReadCell(int cell)
        {
            if(!Enabled)return null;
            var held=PipeInventory.Empty;
            foreach(var spill in Spills.Values)
                if(spill.Cell==cell)foreach(var id in spill.Ids)held=held.Joined(ColonyPipePorts.OwnedInventory(id));
            return held.Count==0?null:ColonyPipeInspection.Describe(held,"Fluid waiting for space");
        }
    }
}
