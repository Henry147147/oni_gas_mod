using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Fixed before the first measurement: the pump arena (gas O2/CO2 left
        // of a wall, water/polluted water lower right). Frame 5: gas pump (1,0)
        // and liquid pump (5,0); frame 7: one isolated outlet pipe cell each,
        // so outlets fill and the pumps keep owned storage. Readiness by frame
        // 30 shifts later stages. Stage 40: both pumps hold material; destroy
        // them. Stage 60: the gas storage vented into the world, the liquid
        // storage became chunks (vanilla DropAll), no pump state, spill or job
        // remains, every pipe account closes.
        private static readonly List<GameObject> retirePumps=new List<GameObject>();
        private static bool RetireAt(int at,string label)=>Once(at+(recorded.TryGetValue("retire setup delay",out var delay)?(int)delay:0),label);
        private static void MixedPipeRetire(int frame)
        {
            if(Once(0,"retire pump chambers"))
            {
                retirePumps.Clear();
                Arena((x,y)=>x==4?(SimHashes.Unobtanium,20000f,300f):x<4?((x+y)%2==0?SimHashes.Oxygen:SimHashes.CarbonDioxide,1.2f,300f):
                    (y<=2?((x+y)%2==0?SimHashes.Water:SimHashes.DirtyWater,500f,300f):(SimHashes.Oxygen,1.2f,300f)));
            }
            if(Once(5,"build gas and liquid pumps")){retirePumps.Add(Build(GasPumpConfig.ID,Cell(1,0)));retirePumps.Add(Build(LiquidPumpConfig.ID,Cell(5,0)));}
            if(Once(7,"isolated pump outlets"))
            {
                Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,new List<int>{PumpOutlet(retirePumps[0])});
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{PumpOutlet(retirePumps[1])});
            }
            if(frame>=8&&step<=8)
            {
                if(!Game.Instance.gasConduitFlow.HasConduit(PumpOutlet(retirePumps[0]))||!Game.Instance.liquidConduitFlow.HasConduit(PumpOutlet(retirePumps[1])))
                {
                    Current.Stage="waiting for pump outlet topology";
                    if(frame>=30){Record("pump outlet topology became ready",false,"readiness expired");Finish();}
                    return;
                }
                recorded["retire setup delay"]=frame-8;
            }
            if(RetireAt(8,"mixed pipes own the pumps"))ColonyPipeRuntime.Start();
            if(RetireAt(40,"destroy pumps holding owned storage"))
            {
                var gas=ColonyPipePumps.Inventory(retirePumps[0].GetComponent<Storage>());var liquid=ColonyPipePumps.Inventory(retirePumps[1].GetComponent<Storage>());
                recorded["retire gas"]=gas.Mass;recorded["retire liquid"]=liquid.Mass;
                recorded["retire world"]=ColonyPipePorts.ToWorld.Mass;recorded["retire storage"]=ColonyMachinePorts.ToStorage.Mass;
                Record("both pumps hold owned storage before destruction",gas.Count>0&&liquid.Count>0&&ColonyPipePumps.BoundPumps==2,
                    $"gas {gas.Mass:R} kg ({gas.Count} species), liquid {liquid.Mass:R} kg ({liquid.Count} species); {ColonyPipePumps.BoundPumps} pumps");
                foreach(var pump in retirePumps)pump.GetComponent<Deconstructable>().ForceDestroyAndGetMaterials();
            }
            if(RetireAt(60,"released pump storage and accounts"))
            {
                var world=ColonyPipePorts.ToWorld.Mass-recorded["retire world"];var chunks=ColonyMachinePorts.ToStorage.Mass-recorded["retire storage"];
                Record("a destroyed gas pump vents its storage and a liquid pump drops chunks",world>=recorded["retire gas"]-1e-9&&chunks>=recorded["retire liquid"]-1e-9&&
                    ColonyPipePumps.BoundPumps==0&&ColonyPipeTeardown.ActiveSpills==0&&ColonyPipePorts.PendingCount==0,
                    $"world +{world:R} kg (held gas {recorded["retire gas"]:R}), chunks +{chunks:R} kg (held liquid {recorded["retire liquid"]:R}); {ColonyPipePumps.BoundPumps} pumps, {ColonyPipeTeardown.ActiveSpills} spills, {ColonyPipePorts.PendingCount} pending");
                Record("pump retirement closes every pipe account",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
    }
}
