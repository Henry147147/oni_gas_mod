using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static readonly List<GameObject> scheduleBuildings=new List<GameObject>();
        private static bool ScheduleAt(int at,string label)=>Once(at+(recorded.TryGetValue("schedule setup delay",out var delay)?(int)delay:0),label);
        private static byte[] ScheduleBytes(PipeNativeScheduleState state)
        {using(var stream=new MemoryStream()){using(var writer=new BinaryWriter(stream))state.Write(writer,ColonyConduits.Geometry);return stream.ToArray();}}
        // A reversible same-boundary diagnostic. No simulation time passes
        // between perturbing and restoring the native scheduling state.
        private static void RunPipeSchedule(ulong ownerFrame)
        {
            var frame=checked((int)ownerFrame);var gas=Game.Instance.gasConduitFlow;var liquid=Game.Instance.liquidConduitFlow;
            if(Once(0,"native pipe scheduling arena")){scheduleBuildings.Clear();Arena((x,y)=>(SimHashes.Oxygen,1.2f,300f));}
            if(Once(5,"component and state-machine pipe callbacks"))
            {
                scheduleBuildings.Add(Build(GasPumpConfig.ID,Cell(1,0)));
                scheduleBuildings.Add(Build(LiquidReservoirConfig.ID,Cell(5,0)));
                scheduleBuildings.Add(Build(ContactConductivePipeBridgeConfig.ID,Cell(1,4)));
                scheduleBuildings[0].GetComponent<ConduitDispenser>().SetOnState(false);
            }
            if(Once(7,"independent gas and liquid scheduling networks"))
            {
                var panel=scheduleBuildings[2].GetSMI<ContactConductivePipeBridge.Instance>();
                Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,new List<int>{PumpOutlet(scheduleBuildings[0])},new List<int>{Cell(7,3)});
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{InputCell(scheduleBuildings[1])},
                    new List<int>{OutputCell(scheduleBuildings[1])},new List<int>{panel.inputCell},new List<int>{panel.outputCell},new List<int>{Cell(7,5)});
            }
            if(frame>=8&&step<=8)
            {
                ColonyConduitsSchedule.FlushTopology();
                if(!gas.HasConduit(Cell(7,3))||!liquid.HasConduit(Cell(7,5)))
                {Current.Stage="waiting for scheduling topology";if(frame>=30){Record("scheduling topology became ready",false,"readiness expired");Finish();}return;}
                recorded["schedule setup delay"]=frame-8;
            }
            if(ScheduleAt(8,"own finite mixed scheduling packets"))
            {
                ColonyPipeRuntime.Start();
                ColonyConduits.SupplyControlInventory(liquid,Cell(7,5),Liquid(3,1,300,17));
                ColonyConduits.SupplyControlInventory(gas,Cell(7,3),PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.Oxygen),.5,300)
                    .Joined(PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.CarbonDioxide),.25,300)));
            }
            if(ScheduleAt(11,"reversible native scheduling roundtrip"))
            {
                var source=ColonyConduitsSchedule.Capture();var bytes=ScheduleBytes(source);var changed=source.Copied(ColonyConduits.Geometry);
                Record("both native clocks and all active empty/nonempty rows are captured",source.Flows.Count==2&&source.Flows.All(f=>f.Cells.Count>1)&&
                    source.Flows.Sum(f=>f.Cells.Count(c=>c.Current.Initial+c.Current.Added-c.Current.Removed>0))>=2,
                    string.Join("; ",source.Flows.Select(f=>$"phase{f.Phase}: {f.Cells.Count} cells, {f.Networks.Count} networks, {f.Updaters.Count} callbacks, elapsed{f.Elapsed:R}")));
                Record("component and state-machine callbacks have persistent identities",source.Flows.SelectMany(f=>f.Updaters).Any(u=>u.Kind==1)&&
                    source.Flows.SelectMany(f=>f.Updaters).Any(u=>u.Kind==2),string.Join(", ",source.Flows.SelectMany(f=>f.Updaters).Select(u=>u.TargetType+"."+u.Method)));
                foreach(var flow in changed.Flows)
                {
                    flow.Elapsed+=.03125f;flow.UpdatersDirty=!flow.UpdatersDirty;
                    flow.Networks.Reverse();foreach(var network in flow.Networks)Array.Reverse(network);
                    flow.Updaters.Reverse();
                    foreach(var cell in flow.Cells){cell.Source^=1;cell.Pull^=2;cell.Target^=4;cell.Initial.Added+=.125f;}
                }
                try
                {
                    ColonyConduitsSchedule.Restore(changed);
                    Record("native clocks staging routing networks and callbacks accept exact changed bits",ScheduleBytes(ColonyConduitsSchedule.Capture()).SequenceEqual(ScheduleBytes(changed)),"same physical boundary; no time advanced");
                }
                finally {ColonyConduitsSchedule.Restore(source);}
                Record("native scheduling restores byte-exactly before continuation",ScheduleBytes(ColonyConduitsSchedule.Capture()).SequenceEqual(bytes),$"{bytes.Length} bytes");
                Record("scheduling roundtrip changes no physical account",ColonyConduits.Audit(out var detail),detail);
            }
            if(ScheduleAt(40,"positive-time continuation after scheduling restore"))
            {Record("restored native scheduling continues with closed accounts",ColonyConduits.Audit(out var detail)&&ColonyOwner.Status.State==1,detail);Finish();}
        }
    }
}
