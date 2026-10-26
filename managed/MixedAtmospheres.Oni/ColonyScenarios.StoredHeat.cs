using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyScenarios
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct StoredHeatRegion
        {
            internal uint Size,Version;
            internal ulong Session,Frame;
            internal double Time,U,K,G,Thermal,Formation,Capacity,Mass;
        }
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_colony_sample_region_energy",CallingConvention=CallingConvention.Cdecl)]
        private static extern int StoredRegionEnergy(ulong session,ulong frame,[In]uint[] cells,uint count,ref StoredHeatRegion result);
        private static PipeAmount storedHeatInitial;
        private static StorageJournalAmount storedHeatInitialWork;
        private static double storedHeatScale,storedHeatMass;
        private static int[] storedHeatBodies=Array.Empty<int>();
        private static GameObject? heatReservoir;
        private static bool StoredHeatAt(int at,string label)=>Once(at+(recorded.TryGetValue("stored heat setup delay",out var delay)?(int)delay:0),label);
        private static unsafe (PipeAmount Energy,double Scale,double Mass,int[] Bodies,StoredHeatRegion Region) StoredHeatSnapshot(string endpoint)
        {
            var trace=Environment.GetEnvironmentVariable("MA_ONI_BODY_HEAT_TRACE");
            if(string.IsNullOrEmpty(trace))throw new InvalidOperationException("Closed stored heat requires the explicit registered-body trace probe.");
            // Deliberately zero energy: the original native message preserves
            // every positive fixture body temperature while exposing its actual
            // registered capacity. Repeat only zero-time publication if an
            // earlier queued body message or stale view has not settled yet.
            for(var attempt=0;attempt<8;++attempt)
            {
                try{ColonySaveBarrier.Settle();}finally{ColonySaveBarrier.SaveFinished();}
                var buildings=UnityEngine.Object.FindObjectsByType<Building>(FindObjectsSortMode.None).Where(building=>InArena(Grid.PosToCell(building))).ToArray();
                var handles=new Dictionary<int,Building>();
                var previousFrames=new Dictionary<int,ulong>();
                foreach(var building in buildings)
                {
                    var payload=GameComps.StructureTemperatures.GetPayload(GameComps.StructureTemperatures.GetHandle(building.gameObject));
                    if(!payload.enabled||payload.bypass||!Sim.IsValidHandle(payload.simHandleCopy)||payload.pendingEnergyModifications!=0||payload.TotalEnergyProducedKW!=0)
                        throw new InvalidOperationException("A thermal fixture body is unregistered, bypassed, has a source or has pending heat.");
                    handles.Add(payload.simHandleCopy,building);
                    previousFrames[payload.simHandleCopy]=ColonyBodyHeatProbe.TryReadZeroWork(payload.simHandleCopy,out var previous)?previous.Frame:0;
                    SimMessages.ModifyBuildingEnergy(payload.simHandleCopy,0f,0f,10000f);
                }
                try{ColonySaveBarrier.Settle();}finally{ColonySaveBarrier.SaveFinished();}
                ColonyOwnedStorage.Published();
                var observed=new Dictionary<int,ColonyBodyHeatProbe.Sample>();var matched=true;
                foreach(var pair in handles)
                {
                    if(!ColonyBodyHeatProbe.TryReadZeroWork(pair.Key,out var body)||body.Frame<=previousFrames[pair.Key]||!body.Valid||!body.Unchanged)
                    {matched=false;break;}
                    var published=Game.Instance.simData.buildingTemperatures[Sim.GetHandleIndex(pair.Key)];
                    if(published.handle!=pair.Key||BitConverter.SingleToInt32Bits(published.temperature)!=BitConverter.SingleToInt32Bits(body.After)){matched=false;break;}
                    observed.Add(pair.Key,body);
                }
                if(!matched)continue;
                var status=OniNativeColony.Status.Empty;OniNativeColony.Check(OniNativeColony.CopyStatus(ref status));
                var cells=Enumerable.Range(0,Height).SelectMany(y=>Enumerable.Range(0,Width).Select(x=>(uint)Cell(x,y))).ToArray();
                var region=new StoredHeatRegion{Size=88,Version=1};
                if(Marshal.SizeOf<StoredHeatRegion>()!=88)throw new InvalidOperationException("Stored heat region ABI differs.");
                OniNativeColony.Check(StoredRegionEnergy(status.Session,status.CompletedFrames,cells,(uint)cells.Length,ref region));
                if(region.Session!=status.Session||region.Frame!=status.CompletedFrames)throw new InvalidOperationException("Stored heat region raced its endpoint.");
                var energy=new PipeAmount(region.U).Plus(new PipeAmount(region.K)).Plus(new PipeAmount(region.G));
                var scale=Math.Abs(region.U)+Math.Abs(region.K)+Math.Abs(region.G);var bodies=new List<int>();var construction=PipeAmount.Zero;
                foreach(var pair in handles)
                {
                    var actual=observed[pair.Key];var temperature=actual.After;
                    var capacity=actual.Capacity;
                    var bodyEnergy=PipeContactWork.IntoFluid(capacity,temperature,1).Plus(PipeContactWork.Kilojoules(capacity));
                    energy=energy.Plus(bodyEnergy);scale+=Math.Abs(bodyEnergy.Value);bodies.Add(pair.Value.GetComponent<KPrefabID>().InstanceID);
                    construction=construction.Plus(bodyEnergy);
                    var identity=pair.Value.GetComponent<KPrefabID>().InstanceID;
                    var label="stored heat body capacity "+identity;
                    var bits=BitConverter.SingleToInt32Bits(capacity);
                    if(recorded.TryGetValue(label,out var previous)&&previous!=bits)throw new InvalidOperationException("A thermal fixture body's actual registered capacity changed.");
                    recorded[label]=bits;
                    var areaLabel="stored heat body area "+identity;var cellLabel="stored heat body per-cell capacity "+identity;
                    var perCellBits=BitConverter.SingleToInt32Bits(actual.PerCellCapacity);
                    if(recorded.TryGetValue(areaLabel,out var oldArea)&&oldArea!=actual.Area||recorded.TryGetValue(cellLabel,out var oldPerCell)&&oldPerCell!=perCellBits)
                        throw new InvalidOperationException("A thermal fixture body's actual registration geometry changed.");
                    recorded[areaLabel]=actual.Area;recorded[cellLabel]=perCellBits;
                }
                FloorArenas.RequireNoLooseActors(x0,y0,Width,Height);
                foreach(var primary in UnityEngine.Object.FindObjectsByType<PrimaryElement>(FindObjectsSortMode.None))
                    if(InArena(Grid.PosToCell(primary))&&primary.GetComponent<Building>()==null&&!ColonyOwnedStorage.IsProjection(primary))
                    {
                        var prefab=primary.GetComponent<KPrefabID>();var thermal=primary.sttOptimizationHook;var pickup=primary.GetComponent<Pickupable>();
                        throw new InvalidOperationException($"An unaccounted actor or loose body entered the closed thermal fixture: object {primary.gameObject.name}, prefab {prefab?.PrefabTag.Name}, cell {Grid.PosToCell(primary)}, element {primary.ElementID}, mass {primary.Mass:R}kg, spawned {primary.isSpawned}, active {primary.gameObject.activeInHierarchy}, thermal {(thermal==null?-1:thermal.SimHandle)}, pickup storage {pickup?.storage?.gameObject.name}, parent {primary.transform.parent?.name}.");
                    }
                var store=ColonyOwnedStorage.Inventory(heatReservoir!.GetComponent<Storage>());
                energy=energy.Plus(store.Energy);scale+=Math.Abs(store.Energy.Value);
                ColonyOwnedStorage.TraceHeatEndpoint(heatReservoir.GetComponent<Storage>(),endpoint,region.Session,region.Frame,region.U,region.K,region.G,construction,energy);
                return(energy,scale,region.Mass,bodies.OrderBy(id=>id).ToArray(),region);
            }
            throw new InvalidOperationException("Zero-energy body probes did not reach a stable registered/published thermal endpoint.");
        }
        // A separate closed balance, using the operand bound already used by
        // NativeBodyHeatControls. No other source, sink or physical inventory
        // changes during the fixed stage8->30 warm hold. Projections are not a
        // second physical energy inventory.
        private static void MixedStorageHeat(int frame)
        {
            var flow=Game.Instance.liquidConduitFlow;
            if(Once(0,"closed storage heat arena"))
            {
                heatReservoir=null;storedHeatInitial=PipeAmount.Zero;storedHeatScale=storedHeatMass=0;storedHeatBodies=Array.Empty<int>();
                Arena((x,y)=>(SimHashes.Oxygen,1.2f,300f));
            }
            if(Once(4,"clear spawned world actors before construction"))
                FloorArenas.ClearLooseActors(x0,y0,Width,Height,out _,"closed storage heat preparation");
            if(Once(5,"passive reservoir body"))
            {
                heatReservoir=Build(LiquidReservoirConfig.ID,Cell(4,0));
                AccessTools.Field(typeof(ConduitConsumer),"isOn").SetValue(heatReservoir.GetComponent<ConduitConsumer>(),false);
                heatReservoir.GetComponent<ConduitDispenser>().isOn=false;
            }
            if(Once(7,"empty reservoir input and output pipes"))
            {
                FloorArenas.ClearLooseActors(x0,y0,Width,Height,out _,"closed storage heat preparation");
                Pipe(Game.Instance.liquidConduitSystem,LiquidConduitConfig.ID,new List<int>{InputCell(heatReservoir!)},new List<int>{OutputCell(heatReservoir!)});
            }
            if(frame>=8&&step<=8)
            {
                if(!flow.HasConduit(InputCell(heatReservoir!))||!flow.HasConduit(OutputCell(heatReservoir!)))
                {Current.Stage="waiting for closed storage heat topology";if(frame>=30){Record("closed storage heat topology became ready",false,"readiness expired");Finish();}return;}
                recorded["stored heat setup delay"]=frame-8;
            }
            if(StoredHeatAt(8,"fixed warm admission and settled energy source"))
            {
                ColonyPipeRuntime.Start();ColonyOwnedStorage.StartControl();
                ColonyConduits.SupplyControlInventory(flow,InputCell(heatReservoir!),Liquid(6,2,350,0));
                ColonyMachinePorts.Consume(heatReservoir!.GetComponent<ConduitConsumer>(),1,flow);
                Current.Stage="waiting for the closed storage heat source endpoint";
                Game.Instance.StartCoroutine(CaptureStoredHeatSource());
            }
            if(StoredHeatAt(30,"closed fluid store and construction-body energy"))
            {
                Current.Stage="waiting for the closed storage heat final endpoint";
                Game.Instance.StartCoroutine(FinishStoredHeat());
            }
        }
        private static IEnumerator CaptureStoredHeatSource()
        {
            yield return new WaitForEndOfFrame();
            try
            {
                var source=StoredHeatSnapshot("source");storedHeatInitial=source.Energy;storedHeatScale=source.Scale;storedHeatMass=source.Mass;storedHeatBodies=source.Bodies;
                storedHeatInitialWork=ColonyOwnedStorage.Heat(heatReservoir!.GetComponent<Storage>());
                Record("closed fixture starts with only passive registered bodies and insulated walls",storedHeatBodies.Length==3&&
                    ElementLoader.FindElementByHash(SimHashes.Unobtanium).thermalConductivity==0&&ColonyOwnedStorage.Inventory(heatReservoir.GetComponent<Storage>()).Mass==8,
                    $"bodies {storedHeatBodies.Length}, initial total {source.Energy.Value:R}J, region frame {source.Region.Frame}");
            }
            catch(Exception error){Record("closed stored heat source endpoint settles",false,error.ToString());Finish();}
        }
        private static IEnumerator FinishStoredHeat()
        {
            yield return new WaitForEndOfFrame();
            try
            {
                var final=StoredHeatSnapshot("final");var storage=heatReservoir!.GetComponent<Storage>();
                var residual=final.Energy.Minus(storedHeatInitial);var work=ColonyOwnedStorage.Heat(storage).Minus(storedHeatInitialWork);var bound=128*2.2204460492503131e-16*(storedHeatScale+final.Scale+Math.Abs(work.Value));
                Record("closed fixture retains all fluid mass and registered body identities",Math.Abs(final.Mass-storedHeatMass)<=128*2.2204460492503131e-16*(Math.Abs(final.Mass)+Math.Abs(storedHeatMass))&&storedHeatBodies.SequenceEqual(final.Bodies)&&
                    ColonyOwnedStorage.Inventory(storage).Mass==8&&ColonyOwnedStorage.Audit(storage),$"world mass {storedHeatMass:R}->{final.Mass:R}kg, bodies {final.Bodies.Length}");
                Record("actual native contacts transfer nonzero heat into the exact stored-fluid energy",work.Sign<0&&ColonyOwnedStorage.RegisteredProjections(storage)==2,
                    $"store heat in measured window {work.Value:R}J, thermal bodies {ColonyOwnedStorage.RegisteredProjections(storage)}, native heat receipts {ColonyOwnedStorage.NativeHeatReceipts}");
                Record("world plus exact store plus native construction bodies conserve U+K+G",Math.Abs(residual.Value)<=bound,
                    $"residual {residual.Value:R}J, operand bound {bound:R}J, world U/K/G {final.Region.U:R}/{final.Region.K:R}/{final.Region.G:R}J");
                Finish();
            }
            catch(Exception error){Record("closed stored heat final endpoint settles",false,error.ToString());Finish();}
        }
    }
}
