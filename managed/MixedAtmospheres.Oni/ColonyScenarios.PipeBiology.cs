using System;
using System.Collections.Generic;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static int[] pipeBiologyIds=Array.Empty<int>();
        private static int pipeBiologyFood,pipeBiologySlime;
        private static void MixedPipeAllGerms(int frame)
        {
            if(Once(0,"all-ID owned pipe biology arena"))Arena((x,y)=>(SimHashes.Oxygen,1f,300f));
            if(Once(5,"isolated gas and liquid culture cells"))
            {
                Pipe(Game.Instance.gasConduitSystem,GasConduitConfig.ID,new List<int>{Cell(1,3)},new List<int>{Cell(3,3)},new List<int>{Cell(5,3)},new List<int>{Cell(6,3)});
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{Cell(3,4)});
            }
            if(Once(10,"seed all native identities and wide fractional minorities"))
            {
                ColonyConduits.StartControl();
                pipeBiologyFood=Db.Get().Diseases.GetIndex(Db.Get().Diseases.FoodGerms.id);pipeBiologySlime=Db.Get().Diseases.GetIndex(Db.Get().Diseases.SlimeGerms.id);
                pipeBiologyIds=Db.Get().Diseases.resources.Select(disease=>(int)Db.Get().Diseases.GetIndex(disease.id)).OrderBy(id=>id).ToArray();
                PipeInventory Single(SimHashes hash,double mass,double t,int disease=0,double count=0)
                    =>PipeInventory.Single(ColonyConduits.MaterialOf(hash),mass,t,disease,count);
                PipeInventory EveryIdentity(PipeInventory fluid)=>PipeInventory.Conserved(fluid.Parts.ToArray(),fluid.Energy,
                    pipeBiologyIds.Select(id=>new PipePart(id,new PipeAmount(1000.25+id*.125))).ToArray());
                var flow=Game.Instance.gasConduitFlow;
                ColonyConduits.SupplyControlInventory(flow,Cell(1,3),Single(SimHashes.Oxygen,.5,280,pipeBiologyFood,16777219));
                ColonyConduits.SupplyControlInventory(flow,Cell(3,3),EveryIdentity(Single(SimHashes.Oxygen,.25,293.15).Joined(Single(SimHashes.ContaminatedOxygen,.25,293.15))));
                var wide=Single(SimHashes.Oxygen,.5,280);
                ColonyConduits.SupplyControlInventory(flow,Cell(5,3),PipeInventory.Conserved(wide.Parts.ToArray(),wide.Energy,
                    new[]{new PipePart(pipeBiologyFood,new PipeAmount(1e30).Plus(new PipeAmount(1e-240))),new PipePart(pipeBiologySlime,new PipeAmount(1e-240))}.OrderBy(part=>part.Id).ToArray()));
                ColonyConduits.SupplyControlInventory(flow,Cell(6,3),Single(SimHashes.Oxygen,.5,900,pipeBiologySlime,1));
                ColonyConduits.SupplyControlInventory(Game.Instance.liquidConduitFlow,Cell(3,4),EveryIdentity(Single(SimHashes.Water,.5,293.15).Joined(Single(SimHashes.DirtyWater,.5,293.15))));
                var rhsChecks=0;var rhsOkay=true;var maxError=0.0;
                foreach(var id in pipeBiologyIds)
                foreach(var hash in new[]{SimHashes.Oxygen,SimHashes.ContaminatedOxygen,SimHashes.Water,SimHashes.DirtyWater})
                foreach(var mass in new[]{0f,.5f,5f})foreach(var count in new[]{1,1000,1000000})
                {
                    var growth=Db.Get().Diseases[(byte)id].elemGrowthInfo[ElementLoader.FindElementByHash(hash).idx];
                    var native=growth.CalculateDiseaseCountDelta(count,mass,.2f);
                    var expected=ColonyNativeGermEffects.ExpectedMaterialChange(new PipeAmount(count),growth,mass,.2f).Value;
                    var error=Math.Abs(expected-native);maxError=Math.Max(maxError,error);++rhsChecks;
                    rhsOkay&=!float.IsNaN(native)&&!float.IsInfinity(native)&&error<=Math.Max(1e-9,Math.Abs(native)*1.3e-7);
                }
                Record("expected-population material RHS agrees with public native effects",rhsOkay,$"{rhsChecks} cases, max absolute difference {maxError:R}; bound max(1e-9,1.3e-7*abs(native RHS)).");
                ColonyPipeBiologyOracle.Start(flow,Cell(1,3));ColonyPipeBiology.StartControl();
                Record("all native disease identities have independent gas/liquid cultures",pipeBiologyIds.Length>=2,"Native identities "+string.Join(",",pipeBiologyIds)+"; mixed phases each carry all IDs with fractional counts.");
            }
            if(Once(80,"verify native parity and exact all-ID biological audit"))
            {
                var report=ColonyPipeBiology.Report;
                Record("single-material owned culture matches original native trajectory",ColonyPipeBiologyOracle.Comparisons>=20&&ColonyPipeBiologyOracle.Mismatches==0,
                    $"{ColonyPipeBiologyOracle.Comparisons} original-native comparisons, {ColonyPipeBiologyOracle.Mismatches} mismatches; "+ColonyPipeBiologyOracle.Last);
                Record("all-ID cultures run on native simulation cadence",ColonyPipeBiology.Ticks>=20&&ColonyPipeBiology.ExpectedIdentities>=40,report);
                Record("each native identity has a measured biological change",pipeBiologyIds.All(id=>ColonyPipeBiology.Died(id).Sign>0||ColonyPipeBiology.Born(id).Sign>0),report);
                ColonyConduits.TryRead(Game.Instance.gasConduitFlow,Cell(5,3),out var wide);
                PipeAmount Quantity(PipeInventory inventory,int id){foreach(var part in inventory.Germs)if(part.Id==id)return part.Amount;return PipeAmount.Zero;}
                Record("wide population remains above native integer storage",Quantity(wide,pipeBiologyFood).Value>int.MaxValue&&Quantity(wide,pipeBiologyFood).Value<1e30,
                    $"food count {Quantity(wide,pipeBiologyFood).Value:R}; tiny count {Quantity(wide,pipeBiologySlime).Value:R}");
                Record("fractional minority remains present below native projection",Quantity(wide,pipeBiologySlime).Sign>0&&Quantity(wide,pipeBiologySlime).Value<1e-240,
                    $"slimelung {Quantity(wide,pipeBiologySlime).Value:R}; native reader may project only the dominant ID");
                ColonyConduits.TryRead(Game.Instance.gasConduitFlow,Cell(6,3),out var hot);
                Record("native integer extinction retires its exact culture",hot.Germs.Length==0,"Hot single-germ cell has "+hot.Germs.Length+" identities.");
                Record("all constituent heat and per-ID birth/death accounts close",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
    }
}
