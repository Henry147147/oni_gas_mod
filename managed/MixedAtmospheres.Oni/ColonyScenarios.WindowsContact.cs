using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private sealed class ContactCase
        {
            internal ConduitFlow Flow=null!;
            internal PrimaryElement Body=null!;
            internal int Cell;
            internal bool Liquid,Insulated;
            internal double Mass;
            internal float BodyTemperature,FluidTemperature;
            internal long Comparisons;
            internal float MinimumCapacity=float.MaxValue,MaximumCapacity;
        }
        private static readonly List<ContactCase> contactCases=new List<ContactCase>();
        // Platform-neutral actual native contacts; no replacement of the
        // established hot/cold equilibrium fixtures or their acceptance bounds.
        private static void WindowsContact(int frame)
        {
            if(Once(0,"isolate contact-law matrix"))
            {
                contactCases.Clear();Arena((x,y)=>(SimHashes.Vacuum,0f,300f));
            }
            if(Once(5,"build separated radiant and insulated contacts"))
            {
                for(var phase=0;phase<2;++phase)
                    for(var i=0;i<6;++i)
                    {
                        var liquid=phase==1;var insulated=i>=3;
                        var cell=Cell(1+2*(i%3),1+2*(i/3));
                        var id=liquid?(insulated?InsulatedLiquidConduitConfig.ID:LiquidConduitRadiantConfig.ID):
                            (insulated?InsulatedGasConduitConfig.ID:GasConduitRadiantConfig.ID);
                        Pipe(liquid?Game.Instance.liquidConduitSystem:Game.Instance.gasConduitSystem,id,new List<int>{cell});
                        contactCases.Add(new ContactCase {Cell=cell,Liquid=liquid,Insulated=insulated,
                            Flow=liquid?Game.Instance.liquidConduitFlow:Game.Instance.gasConduitFlow,
                            Body=Grid.Objects[cell,(int)(liquid?ObjectLayer.LiquidConduit:ObjectLayer.GasConduit)].GetComponent<PrimaryElement>(),
                            Mass=(i%3==0?1e-7:i%3==1?.001:liquid?9.0:.9),
                            BodyTemperature=i%2==0?350:300,FluidTemperature=i%2==0?300:350});
                    }
            }
            if(frame>=8&&step<=8)
            {
                if(contactCases.Any(row=>!row.Flow.HasConduit(row.Cell)))
                {
                    Current.Stage="waiting for contact matrix topology";
                    if(frame>=30){Record("contact matrix topology ready",false,"readiness expired");Finish();}return;
                }
                recorded["thermal setup delay"]=frame-8;
            }
            if(ThermalAt(8,"set native contact body temperatures"))
                foreach(var row in contactCases)row.Body.Temperature=row.BodyTemperature;
            if(ThermalAt(14,"seed native contact capacity range"))
            {
                ColonyConduits.StartControl();ColonyPipeContactHeat.StartControl();
                foreach(var row in contactCases)
                {
                    var material=ColonyConduits.MaterialOf(row.Liquid?SimHashes.Water:SimHashes.Oxygen);
                    ColonyConduits.SupplyControlInventory(row.Flow,row.Cell,PipeInventory.Single(material,row.Mass,row.FluidTemperature,255,0));
                }
                recorded["contact mismatches"]=ColonyPipeContactHeat.Mismatches;
                ColonyPipeContactHeat.ValidationObserved=(flow,cell,input)=>
                {
                    var row=contactCases.FirstOrDefault(item=>ReferenceEquals(item.Flow,flow)&&item.Cell==cell);
                    if(row==null)return;
                    ++row.Comparisons;row.MinimumCapacity=Math.Min(row.MinimumCapacity,input.FluidCapacity);
                    row.MaximumCapacity=Math.Max(row.MaximumCapacity,input.FluidCapacity);
                };
            }
            if(ThermalAt(100,"verify every actual contact carrier"))
            {
                ColonyPipeContactHeat.ValidationObserved=null;
                foreach(var row in contactCases)
                    Record((row.Liquid?"liquid":"gas")+" "+(row.Insulated?"insulated":"radiant")+" "+row.Mass.ToString("R")+" kg actual carrier bits",
                        row.Comparisons>=10,$"{row.Comparisons} bit matches; initial fluid/body {row.FluidTemperature:R}/{row.BodyTemperature:R} K; fluid capacity {row.MinimumCapacity:R}..{row.MaximumCapacity:R} kJ/K");
                Record("contact matrix has no carrier mismatches",ColonyPipeContactHeat.Mismatches==recorded["contact mismatches"],ColonyPipeContactHeat.Report);
                Record("contact matrix closes every pipe account",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
    }
}
