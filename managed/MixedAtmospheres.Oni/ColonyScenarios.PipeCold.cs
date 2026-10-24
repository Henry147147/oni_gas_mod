using System;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        // Separate fixed fixture; mixed-pipe-thermal keeps its original inputs.
        private static readonly SimHashes[] ColdGasElements={SimHashes.Hydrogen,SimHashes.Oxygen,SimHashes.ContaminatedOxygen,SimHashes.CarbonDioxide,SimHashes.ChlorineGas};
        private static readonly double[] ColdGasMasses={.06,.01,.01,.01,.01};
        private static void MixedPipeCold(int frame)
        {
            if(Once(0,"isolated cold-body thermal arena"))Arena((x,y)=>(SimHashes.Vacuum,0f,300f));
            if(Once(5,"build cold finite radiant body"))
            {
                Pipe(Game.Instance.gasConduitSystem,"GasConduitRadiant",new System.Collections.Generic.List<int>{ThermalCell});
                mixedThermalBody=Grid.Objects[ThermalCell,(int)ObjectLayer.GasConduit].GetComponent<PrimaryElement>();
            }
            if(frame>=8&&step<=8)
            {
                if(!Game.Instance.gasConduitFlow.HasConduit(ThermalCell))
                {
                    Current.Stage="waiting for cold native pipe topology";
                    if(frame>=30){Record("cold topology became ready",false,"readiness expired");Finish();}return;
                }
                recorded["thermal setup delay"]=frame-8;
            }
            if(ThermalAt(8,"set cold finite native body"))mixedThermalBody!.Temperature=300;
            if(ThermalAt(14,"seed five hot gases with differing Cv"))
            {
                ColonyConduits.StartControl();var inventory=PipeInventory.Empty;var fluidCv=0.0;
                for(var i=0;i<ColdGasElements.Length;++i)
                {
                    var material=ColonyConduits.MaterialOf(ColdGasElements[i]);
                    inventory=inventory.Joined(PipeInventory.Single(material,ColdGasMasses[i],600,255,0));fluidCv+=ColdGasMasses[i]*material.HeatCapacity;
                }
                ColonyConduits.SupplyControlInventory(Game.Instance.gasConduitFlow,ThermalCell,inventory);
                ColonyPipeContactHeat.StartControl();var body=mixedThermalBody!;
                var bodyCv=(double)body.GetComponent<Building>().Def.MassForTemperatureModification*body.Element.specificHeatCapacity*1000;
                recorded["cold body Cv"]=bodyCv;recorded["cold fluid Cv"]=fluidCv;recorded["cold body T"]=body.Temperature;
                recorded["cold U"]=inventory.Energy.Value;recorded["cold exchange"]=ColonyConduits.ExchangedHeatJoules;
                recorded["cold equilibrium"]=(bodyCv*body.Temperature+fluidCv*600)/(bodyCv+fluidCv);recorded["cold bounded"]=1;
                Record("cold body and five-gas capacities are finite",bodyCv>0&&fluidCv>0&&inventory.Count==5&&body.Temperature==300,
                    $"flags {ColonyOwner.Config.Flags}; body Cv {bodyCv:R}, fluid Cv {fluidCv:R} J/K; equilibrium {recorded["cold equilibrium"]:R} K");
            }
            if(recorded.ContainsKey("cold U"))
            {
                ColonyConduits.TryRead(Game.Instance.gasConduitFlow,ThermalCell,out var observed);var fluid=observed.Temperature(ColonyConduits.Material);var body=(double)mixedThermalBody!.Temperature;
                if((double.IsNaN(fluid)||double.IsInfinity(fluid))||fluid<299.9999||fluid>600.0001||body<299.9999||body>600.0001)recorded["cold bounded"]=0;
                Current.Stage=$"cold-body observation: body {body:R}, mixture {fluid:R}, truth {recorded["cold equilibrium"]:R} K";
            }
            if(ThermalAt(600,"cold finite-body heat and equilibrium"))
            {
                ColonyConduits.TryRead(Game.Instance.gasConduitFlow,ThermalCell,out var inventory);var body=(double)mixedThermalBody!.Temperature;var fluid=inventory.Temperature(ColonyConduits.Material);
                var bodyHeat=recorded["cold body Cv"]*(body-recorded["cold body T"]);var fluidHeat=inventory.Energy.Value-recorded["cold U"];
                double Ulp(float value)=>Math.Abs((double)BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(value)+1)-value);
                var bound=2*recorded["cold body Cv"]*(Ulp((float)body)+Ulp((float)recorded["cold body T"]))+1e-7*Math.Max(1,Math.Abs(fluidHeat));
                Record("cold body receives exactly the fluid heat",Math.Abs(bodyHeat+fluidHeat)<=bound,
                    $"body {bodyHeat:R}, fluid {fluidHeat:R}, booked {ColonyConduits.ExchangedHeatJoules-recorded["cold exchange"]:R} J; residual {bodyHeat+fluidHeat:R}; endpoint bound {bound:R}");
                Record("cold body and five gases reach analytic equilibrium",Math.Abs(body-recorded["cold equilibrium"])<=.1&&Math.Abs(fluid-recorded["cold equilibrium"])<=.1,
                    $"body {body:R}, mixture {fluid:R}, truth {recorded["cold equilibrium"]:R} K; {ColonyPipeContactHeat.Report}");
                Record("cold contact obeys endpoint temperature bounds",recorded["cold bounded"]==1,"both endpoints remain within 300..600 K with 0.0001 K readout allowance");
                Record("cold contact retains all five exact masses",inventory.Count==5&&ColdGasElements.Select((e,i)=>inventory.SpeciesMass(ColonyConduits.MaterialOf(e).Id)==ColdGasMasses[i]).All(same=>same),$"{inventory.Count} constituents, {inventory.Mass:R} kg");
                Record("cold contact closes every pipe account",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
    }
}
