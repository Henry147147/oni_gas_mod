using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static readonly List<GameObject> mixedGasVents=new List<GameObject>();
        // Fixed before the first measurement: two sealed one-cell sites at
        // (2,2)/(5,2), initially 100kg O2/300K; lower each to .1kg at stage20.
        // Each inlet receives .375kg O2+.625kg CO2, with IDs0/1 carrying
        // 1000.25 / (2000.5+1e-240). Native bodies begin with IDs0/1, 100 each.
        // Stage20: no output while overpressure-blocked; stage60: all2kg in
        // world, both constituents/germ identities, empty ports, exact audits.
        private static void MixedPipeGasVents(int frame)
        {
            if(Once(0,"sealed overpressure vent sites"))
            {
                mixedGasVents.Clear();
                Arena((x,y)=>
                {
                    var distance=Math.Min(Math.Abs(x-2)+Math.Abs(y-2),Math.Abs(x-5)+Math.Abs(y-2));
                    return distance==0?(SimHashes.Oxygen,100f,300f):distance==1?(SimHashes.Unobtanium,20000f,300f):(SimHashes.Oxygen,1f,300f);
                });
            }
            if(Once(5,"build native gas vents"))
            {mixedGasVents.Add(Build(GasVentConfig.ID,Cell(2,2)));mixedGasVents.Add(Build(GasVentConfig.ID,Cell(5,2)));}
            if(Once(7,"connect initialized vent inlets"))
                foreach(var vent in mixedGasVents)Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,new List<int>{VentInlet(vent)});
            if(Once(10,"enable explicit owned vent control"))
            {
                ColonyConduits.StartControl();ColonyPipePorts.StartControl();ColonyPipeVents.StartControl();ColonyPipeVents.BeginExchangeControl();
                recorded["vent world mass before"]=ColonyPipePorts.ToWorld.Mass;
                for(var index=0;index<mixedGasVents.Count;++index)
                {
                    var vent=mixedGasVents[index];var body=vent.GetComponent<PrimaryElement>();
                    body.ModifyDiseaseCount(-int.MaxValue,"Mixed vent fixture");body.AddDisease((byte)index,100,"Mixed vent fixture");
                    var mixture=PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.Oxygen),.375,300,0,1000.25)
                        .Joined(PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.CarbonDioxide),.625,300,1,2000.5)).WithDiseaseDelta(1,1e-240);
                    ColonyConduits.SupplyControlInventory(Game.Instance.gasConduitFlow,VentInlet(vent),mixture);
                    Record($"vent {index} starts with one mixed cell and two germ identities",mixture.Count==2&&mixture.Germs.Length==2,$"{mixture.Mass:R} kg; body {body.DiseaseIdx}/{body.DiseaseCount}; strengths {Db.Get().Diseases[(byte)0].strength:R}/{Db.Get().Diseases[(byte)1].strength:R}");
                }
            }
            if(Once(20,"native overpressure closes vents then pressure is released"))
            {
                var held=PipeInventory.Empty;
                foreach(var vent in mixedGasVents)
                {
                    ColonyConduits.TryRead(Game.Instance.gasConduitFlow,VentInlet(vent),out var pipe);
                    held=held.Joined(pipe).Joined(ColonyPipeVents.Inventory(vent.GetComponent<Storage>()));
                    Record($"vent at {Grid.PosToCell(vent)} preserves native overpressure",vent.GetComponent<Vent>().IsBlocked,MachineState(vent));
                }
                Record("blocked vents release no mixture and retain complete inventory",ColonyPipePorts.ToWorld.Mass==recorded["vent world mass before"]&&held.Total.Minus(new PipeAmount(2)).Sign==0&&held.Count==2&&held.Germs.Length==2,$"held {held.Mass:R} kg; {held.Count} species/{held.Germs.Length} germ identities");
                Record("blocked vent accounts close",ColonyConduits.Audit(out var detail),detail);
                foreach(var vent in mixedGasVents)SimMessages.ReplaceElement(Grid.PosToCell(vent),SimHashes.Oxygen,CellEventLogger.Instance.SandBoxTool,.1f,300);
            }
            if(Once(60,"native open vents deliver every constituent and passive"))
            {
                var delivered=ColonyPipePorts.ToWorld;var a=ColonyConduits.MaterialOf(SimHashes.Oxygen).Id;var b=ColonyConduits.MaterialOf(SimHashes.CarbonDioxide).Id;
                Record("open vents deliver all mixed species",Math.Abs(delivered.Mass-recorded["vent world mass before"]-2)<1e-6&&Math.Abs(delivered.SpeciesMass(a)-.75)<1e-6&&Math.Abs(delivered.SpeciesMass(b)-1.25)<1e-6,$"{delivered.Mass:R}kg, O2 {delivered.SpeciesMass(a):R}, CO2 {delivered.SpeciesMass(b):R}");
                Record("vent outputs retain both germ identities",ColonyPipePorts.PassiveExchange(1).Sign<0&&ColonyPipePorts.PassiveExchange(2).Sign<0,
                    $"actual native world changes {ColonyPipePorts.PassiveExchange(1).Negated().Value:R}/{ColonyPipePorts.PassiveExchange(2).Negated().Value:R}; retained representation {ColonyPipePorts.PassiveRemainder(1).Value:R}/{ColonyPipePorts.PassiveRemainder(2).Value:R}");
                for(var index=0;index<mixedGasVents.Count;++index)
                {
                    var vent=mixedGasVents[index];var storage=vent.GetComponent<Storage>();
                    ColonyConduits.TryRead(Game.Instance.gasConduitFlow,VentInlet(vent),out var pipe);
                    Record($"vent {index} empties authoritative inlet and storage without native packets",pipe.Count==0&&ColonyPipeVents.Inventory(storage).Count==0&&storage.items.Count==0&&storage.MassStored()==0,ColonyPipeVents.ControlDetails(storage));
                    Record($"vent {index} keeps native blocked/readers coherent",!vent.GetComponent<Vent>().IsBlocked,$"{MachineState(vent)}; {ColonyPipeVents.ControlDetails(storage)}");
                }
                var clear=ColonyPipePorts.PendingCount==0;var closes=ColonyConduits.Audit(out var detail);
                Record("vent transfers close mass, heat and every passive account",clear&&closes,clear?detail:"native jobs remain pending; "+detail);Finish();
            }
        }
        private static int VentInlet(GameObject vent)=>(int)AccessTools.Field(typeof(ConduitConsumer),"utilityCell").GetValue(vent.GetComponent<ConduitConsumer>());
    }
}
