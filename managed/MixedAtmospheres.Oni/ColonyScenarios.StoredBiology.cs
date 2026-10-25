using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using HarmonyLib;
using UnityEngine;
namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        private static readonly GameObject?[] biologyReservoirs=new GameObject?[4];
        private static int biologyComparisons;
        private static long biologyInitialTick;
        private static bool biologySourceReady,biologySourcePending;
        private static bool BiologyAt(int at,string label)=>Once(at+(recorded.TryGetValue("storage biology setup delay",out var delay)?(int)delay:0),label);
        // Separate from transfer/heat acceptance: actual reservoirs at300K in
        // vacuum, native item insulation, one original control, real callbacks and no
        // manually advanced disease tick. Exact storage remains explicit only.
        private static void MixedOwnedStorageBiology(int frame)
        {
            var flow=Game.Instance.liquidConduitFlow;
            if(Once(0,"storage biology arena"))
            {
                Array.Clear(biologyReservoirs,0,biologyReservoirs.Length);biologyComparisons=0;biologyInitialTick=0;biologySourceReady=biologySourcePending=false;
                Arena((x,y)=>(SimHashes.Vacuum,0f,0f));
            }
            if(Once(4,"clear physical actors before biology construction"))FloorArenas.ClearLooseActors(x0,y0,Width,Height,out _,"storage biology preparation");
            if(Once(5,"paired original and exact biological reservoirs"))
                for(var i=0;i<4;++i)
                {
                    var building=Build(LiquidReservoirConfig.ID,Cell(2*i,0));biologyReservoirs[i]=building;
                    AccessTools.Field(typeof(ConduitConsumer),"isOn").SetValue(building.GetComponent<ConduitConsumer>(),false);building.GetComponent<ConduitDispenser>().isOn=false;
                    var storage=building.GetComponent<Storage>();
                    var modifiers=new List<Storage.StoredItemModifier>((List<Storage.StoredItemModifier>)AccessTools.Field(typeof(Storage),"defaultStoredItemModifers").GetValue(storage));
                    if(!modifiers.Contains(Storage.StoredItemModifier.Insulate))modifiers.Add(Storage.StoredItemModifier.Insulate);
                    storage.SetDefaultStoredItemModifiers(modifiers);
                }
            if(Once(7,"biological reservoir isolated prefab ports"))
            {
                FloorArenas.ClearLooseActors(x0,y0,Width,Height,out _,"storage biology preparation");
                var paths=new List<int>[] {new List<int>{InputCell(biologyReservoirs[0]!)},new List<int>{OutputCell(biologyReservoirs[0]!)},
                    new List<int>{InputCell(biologyReservoirs[2]!)},new List<int>{OutputCell(biologyReservoirs[2]!)},
                    new List<int>{InputCell(biologyReservoirs[3]!)},new List<int>{OutputCell(biologyReservoirs[3]!)}};
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,paths);
            }
            if(frame>=8&&step<=8)
            {
                if(new[]{0,2,3}.Any(i=>!flow.HasConduit(InputCell(biologyReservoirs[i]!))||!flow.HasConduit(OutputCell(biologyReservoirs[i]!))))
                {Current.Stage="waiting for storage biology topology";if(frame>=30){Record("storage biology topology becomes ready",false,"readiness expired");Finish();}return;}
                recorded["storage biology setup delay"]=frame-8;
            }
            if(!biologySourcePending&&!biologySourceReady&&BiologyAt(8,"native cache capture before first owned biological tick"))
            {
                biologySourcePending=true;
                Current.Stage="waiting for registered insulated biological source";
                SpeedControlScreen.Instance.Pause(false);Game.Instance.StartCoroutine(CaptureStoredBiologySource());
            }
            if(biologySourceReady&&step>8&&step<=40+(recorded.TryGetValue("storage biology setup delay",out var delay2)?(int)delay2:0))
            {
                var matched=ColonyOwnedStorage.CompareStoredNativeCulture(biologyReservoirs[0]!.GetComponent<Storage>(),biologyReservoirs[1]!.GetComponent<Storage>(),0,out var detail);
                if(!matched){Record("integer stored culture remains bit-identical to native original",false,detail);Finish();return;}
                ++biologyComparisons;
            }
            if(biologySourceReady&&BiologyAt(40,"biological native parity and physical per-ID audit"))
            {
                var ticks=ColonyOwnedStorage.StoredBiologyTicks-biologyInitialTick;
                Record("real native callbacks preserve exact integer culture parity",ticks>=20&&biologyComparisons>=20&&ColonyOwnedStorage.StoredCulturePopulation(biologyReservoirs[0]!.GetComponent<Storage>(),0).Minus(new PipeAmount(1_000_000)).Sign!=0,$"callbacks {ticks}, comparisons {biologyComparisons}");
                Record("fractional and wide expected cultures perform physical biological work",ColonyOwnedStorage.StoredBiologyHasPhysicalWork(biologyReservoirs[2]!.GetComponent<Storage>()),
                    ReportJson.SerializeLine(ColonyOwnedStorage.StoredBiologyReport(biologyReservoirs[2]!.GetComponent<Storage>())));
                using(var bytes=new MemoryStream())
                {
                    using(var writer=new BinaryWriter(bytes,System.Text.Encoding.UTF8,true))ColonyOwnedStorage.Capture().Write(writer);
                    var source=bytes.ToArray();bytes.Position=0;using var reader=new BinaryReader(bytes);var image=OwnedFluidStorageImage.Read(reader);
                    using var encoded=new MemoryStream();using(var writer=new BinaryWriter(encoded,System.Text.Encoding.UTF8,true))image.Write(writer);
                    Record("actual evolved biology DTO reencodes all inventories clocks caches and journals exactly",source.SequenceEqual(encoded.ToArray()),$"{source.Length}bytes; model codec only, no live game restore claim");
                }
                foreach(var i in new[]{0,2,3})
                {
                    var building=biologyReservoirs[i]!;var storage=building.GetComponent<Storage>();
                    Record("stored culture inventory and all-ID growth death journal close for reservoir "+i,ColonyOwnedStorage.Audit(storage),ReportJson.SerializeLine(ColonyOwnedStorage.StoredBiologyReport(storage)));
                    var before=ColonyOwnedStorage.Inventory(storage);ColonyMachinePorts.Dispense(building.GetComponent<ConduitDispenser>());var after=ColonyConduits.ReadSynced(flow,OutputCell(building));
                    byte[] InventoryBytes(PipeInventory value){using var buffer=new MemoryStream();using var writer=new BinaryWriter(buffer);value.Write(writer);return buffer.ToArray();}
                    Record("evolved physical mixture withdraws completely for reservoir "+i,InventoryBytes(before).SequenceEqual(InventoryBytes(after))&&ColonyOwnedStorage.Inventory(storage).Count==0&&ColonyOwnedStorage.Audit(storage),$"mass {before.Mass:R}/{after.Mass:R}, live identities {before.Germs.Length}/{after.Germs.Length}");
                }
                if(new[]{0,2,3}.All(i=>ColonyOwnedStorage.Inventory(biologyReservoirs[i]!.GetComponent<Storage>()).Count==0))ColonyOwnedStorage.ResetBiology();
                Record("storage biology retains all pipe accounts",ColonyConduits.Audit(out var detail),detail);Finish();
            }
        }
        private static IEnumerator CaptureStoredBiologySource()
        {
            yield return new WaitForEndOfFrame();
            var flow=Game.Instance.liquidConduitFlow;
            try
            {
                try{ColonySaveBarrier.Settle();}finally{ColonySaveBarrier.SaveFinished();}
                ColonyPipeRuntime.Start();ColonyOwnedStorage.StartControl();
                var water=ColonyConduits.MaterialOf(SimHashes.Water);var dirty=ColonyConduits.MaterialOf(SimHashes.DirtyWater);
                var parcels=new[]{PipeInventory.Single(water,3,300,0,1_000_000),
                    PipeInventory.Single(water,3,300,0,.25).Joined(PipeInventory.Single(dirty,1,300,1,1e-240)).WithDiseaseDelta(2,new PipeAmount(1e300)),
                    PipeInventory.Single(water,1e-240,300,0,1e-240)};
                for(var k=0;k<3;++k)
                {
                    var i=k==0?0:k+1;var building=biologyReservoirs[i]!;
                    ColonyConduits.SupplyControlInventory(flow,InputCell(building),parcels[k]);ColonyMachinePorts.Consume(building.GetComponent<ConduitConsumer>(),1,flow);
                }
                biologyReservoirs[1]!.GetComponent<Storage>().AddLiquid(SimHashes.Water,3,300,0,1_000_000,true,false);
                for(var zero=0;zero<2;++zero)
                    try{ColonySaveBarrier.Settle();}finally{ColonySaveBarrier.SaveFinished();}
                Record("biological carriers are insulated before the source",biologyReservoirs.All(building=>building!.GetComponent<Storage>().items.All(item=>
                    item==null||item.GetComponent<SimTemperatureTransfer>()==null||!item.GetComponent<SimTemperatureTransfer>().enabled)),"native item insulation; this is not thermal acceptance");
                ColonyOwnedStorage.StartBiologyControl();biologyInitialTick=ColonyOwnedStorage.StoredBiologyTicks;
                Record("exact reservoir withdrawal fixtures are operational and connected",new[]{0,2,3}.All(i=>biologyReservoirs[i]!.GetComponent<Operational>().IsOperational&&biologyReservoirs[i]!.GetComponent<ConduitDispenser>().IsConnected),
                    string.Join("; ",new[]{0,2,3}.Select(i=>$"reservoir {i}: operational {biologyReservoirs[i]!.GetComponent<Operational>().IsOperational}, connected {biologyReservoirs[i]!.GetComponent<ConduitDispenser>().IsConnected}")));
                Record("logical storage tags preserve every native disease tag rule",ColonyOwnedStorage.VerifyStoredLogicalTags(out var tags),tags);
                var exact=biologyReservoirs[0]!.GetComponent<Storage>();var original=biologyReservoirs[1]!.GetComponent<Storage>();
                Record("original cached material tag threshold and residue match at biological source",ColonyOwnedStorage.CompareStoredNativeCulture(exact,original,0,out var detail),detail);
                Record("fractional wide and tiny cultures enter actual authoritative stores",ColonyOwnedStorage.Inventory(biologyReservoirs[2]!.GetComponent<Storage>()).Germs.Length==3&&
                    ColonyOwnedStorage.Inventory(biologyReservoirs[3]!.GetComponent<Storage>()).Mass==1e-240,"three identities and a physical sub-float-only store");

                recorded["storage biology setup delay"]=ColonyFrame()-8;biologySourceReady=true;
            }
            catch(Exception error){ColonyLoadRefusal.Refuse(error);Record("biological source settles before positive time",false,error.ToString());Finish();}
            finally{SpeedControlScreen.Instance.Unpause(false);}
        }
    }
}
