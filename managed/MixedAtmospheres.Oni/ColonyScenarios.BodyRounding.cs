using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static PrimaryElement? roundingBody;
        private static PipeInventory roundingInitial=PipeInventory.Empty;
        private static ColonyBodyHeatProbe.Sample roundingProbe;
        private const float RoundingBodyTemperature=299.999420166015625f, RoundingFluidTemperature=299.3900146484375f;
        private static bool RoundingAt(int at,string label)=>Once(at+(recorded.TryGetValue("body rounding delay",out var delay)?(int)delay:0),label);
        private static int RoundingCell=>Cell(3,3);
        private static int RoundingHandle=>GameComps.StructureTemperatures.GetPayload(GameComps.StructureTemperatures.GetHandle(roundingBody!.gameObject)).simHandleCopy;
        private static void MixedPipeBodyRounding(int frame)
        {
            var flow=Game.Instance.liquidConduitFlow;
            if(Once(0,"isolated near-ULP construction contact"))
            {roundingBody=null;roundingInitial=PipeInventory.Empty;roundingProbe=default;Arena((x,y)=>(SimHashes.Vacuum,0f,300f));}
            if(Once(5,"ordinary finite liquid conduit body"))
            {
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{RoundingCell});
                roundingBody=Grid.Objects[RoundingCell,(int)ObjectLayer.LiquidConduit].GetComponent<PrimaryElement>();
            }
            if(frame>=8&&step<=8)
            {
                if(!flow.HasConduit(RoundingCell))
                {Current.Stage="waiting for near-ULP pipe topology";if(frame>=30){Record("near-ULP topology became ready",false,"readiness expired");Finish();}return;}
                recorded["body rounding delay"]=frame-8;
            }
            if(RoundingAt(8,"set measured native body endpoint"))roundingBody!.Temperature=RoundingBodyTemperature;
            if(RoundingAt(12,"zero-energy native registration probe"))
            {
                if(!Sim.IsValidHandle(RoundingHandle))throw new InvalidOperationException("Near-ULP construction has no native body handle.");
                SimMessages.ModifyBuildingEnergy(RoundingHandle,0f,0f,10000f);
            }
            if(RoundingAt(16,"supply a finite near-ULP liquid contact"))
            {
                var found=ColonyBodyHeatProbe.TryReadZeroWork(RoundingHandle,out roundingProbe);
                Record("zero-energy probe observes actual registered capacity without changing body temperature",found&&roundingProbe.Valid&&roundingProbe.Unchanged&&
                    roundingProbe.Before==RoundingBodyTemperature&&roundingBody!.Temperature==RoundingBodyTemperature,
                    $"found {found}; native frame {roundingProbe.Frame}; body {roundingProbe.Before:R}->{roundingProbe.After:R} K; {roundingProbe.Area} cells * {roundingProbe.PerCellCapacity:R} kJ/K");
                if(!found||!roundingProbe.Valid||!roundingProbe.Unchanged||roundingProbe.Before!=RoundingBodyTemperature){Finish();return;}
                ColonyConduits.StartControl();ColonyPipeContactHeat.StartControl();
                roundingInitial=PipeInventory.Single(ColonyConduits.MaterialOf(SimHashes.Water),1,RoundingFluidTemperature);
                ColonyConduits.SupplyControlInventory(flow,RoundingCell,roundingInitial);
                recorded["body rounding contacts"]=ColonyPipeContactHeat.Contacts;
            }
            if(RoundingAt(36,"settle actual native contact work"))
            {Current.Stage="waiting for end-of-frame native body settlement";Game.Instance.StartCoroutine(FinishBodyRounding());}
        }
        private static IEnumerator FinishBodyRounding()
        {
            yield return new WaitForEndOfFrame();
            try
            {
                // Run outside StepTheSim, through the same barrier as an
                // ordinary save. A second zero-energy probe verifies that
                // the settled endpoint still has the original registration.
                ColonySaveBarrier.Settle();
                SimMessages.ModifyBuildingEnergy(RoundingHandle,0f,0f,10000f);ColonySaveBarrier.Settle();
                var observed=ColonyBodyHeatProbe.TryReadZeroWork(RoundingHandle,out var endpoint);
                Record("native save barrier publishes a later unchanged zero-energy body endpoint",observed&&endpoint.Valid&&endpoint.Unchanged&&endpoint.Frame>roundingProbe.Frame&&
                    endpoint.Area==roundingProbe.Area&&endpoint.PerCellCapacity==roundingProbe.PerCellCapacity&&roundingBody!.Temperature==endpoint.After,
                    $"frame {roundingProbe.Frame}->{endpoint.Frame}; endpoint {endpoint.Before:R}->{endpoint.After:R} K; published {roundingBody!.Temperature:R} K; {ColonySaveBarrier.LastSynchronization}");
                ColonyConduits.TryRead(Game.Instance.liquidConduitFlow,RoundingCell,out var inventory);
                var represented=PipeContactWork.IntoFluid(roundingProbe.Capacity,roundingProbe.Before,endpoint.After);
                var gain=inventory.Energy.Minus(roundingInitial.Energy);var residual=gain.Minus(represented);
                Record("isolated near-ULP body and owned liquid exchange exactly the same represented work",observed&&endpoint.Valid&&residual.Sign==0,
                    $"native body gives {represented.Value:R} J; fluid gains {gain.Value:R} J; residual {residual.Value:R} J");
                Record("near-ULP control executes validated contacts with physical mass retained",ColonyPipeContactHeat.Contacts>recorded["body rounding contacts"]&&inventory.Mass==1,
                    $"{ColonyPipeContactHeat.Report}; held {inventory.Mass:R} kg");
                Record("near-ULP contact closes every exact pipe account",ColonyConduits.Audit(out var detail),detail);
            }
            catch(Exception error){Record("near-ULP body save settlement completes",false,error.ToString());}
            Finish();
        }
    }
}
