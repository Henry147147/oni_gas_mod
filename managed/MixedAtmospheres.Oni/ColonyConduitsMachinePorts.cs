using System;
using System.Collections.Generic;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Machine pipe ports (docs/mixed-pipes.md#machine-ports). Every
    // ConduitConsumer/ConduitDispenser on an owned pipe cell keeps its native
    // gates, rates and storage; only the exchanged material changes. An input
    // port takes rate*dt of the cell's whole mixture in proportion: species the
    // consumer's capacity tag accepts go into its storage as native items, the
    // rest is stored or dumped like a vanilla wrong element and never deleted.
    // Storage holds native float items; the exact difference between the pipe
    // side and the observed native storage change is booked per species,
    // joule and germ identity in StorageBoundary.
    internal static class ColonyMachinePorts
    {
        internal sealed class Account
        {
            internal readonly SortedDictionary<int,PipeAmount> Mass=new SortedDictionary<int,PipeAmount>(),Germs=new SortedDictionary<int,PipeAmount>();
            internal PipeAmount Energy;
            internal void AddMass(int id,PipeAmount amount)=>Add(Mass,id,amount);
            internal void AddGerms(int id,PipeAmount amount)=>Add(Germs,id,amount);
            internal PipeAmount MassOf(int id)=>Mass.TryGetValue(id,out var value)?value:PipeAmount.Zero;
            internal PipeAmount GermsOf(int id)=>Germs.TryGetValue(id,out var value)?value:PipeAmount.Zero;
            private static void Add(SortedDictionary<int,PipeAmount> values,int id,PipeAmount amount)
            {values.TryGetValue(id,out var before);var after=before.Plus(amount);if(after.Sign==0)values.Remove(id);else values[id]=after;}
            internal string Describe()
            {
                var mass=PipeAmount.Zero;foreach(var value in Mass.Values)mass=mass.Plus(value);
                var germs=PipeAmount.Zero;foreach(var value in Germs.Values)germs=germs.Plus(value);
                return $"mass {mass.Value:R} kg over {Mass.Count} species, energy {Energy.Value:R} J, germs {germs.Value:R} over {Germs.Count} identities";
            }
        }
        // One native item a parcel species becomes: float mass/temperature and
        // the merged integer germ identity (vanilla SimUtil rule).
        internal readonly struct NativeItem
        {
            internal readonly PipePart Part;internal readonly Element Element;internal readonly float Mass,Temperature;internal readonly byte Disease;internal readonly int Count;
            internal NativeItem(PipePart part,Element element,float mass,float temperature,byte disease,int count)
            {Part=part;Element=element;Mass=mass;Temperature=temperature;Disease=disease;Count=count;}
        }
        private readonly struct Observed
        {
            internal readonly float Mass,Temperature;internal readonly byte Disease;internal readonly int Count;
            internal Observed(PrimaryElement? item){if(item==null){Mass=0;Temperature=0;Disease=byte.MaxValue;Count=0;}else{Mass=item.Mass;Temperature=item.Temperature;Disease=item.DiseaseIdx;Count=item.DiseaseCount;}}
            internal int GermsOf(int id)=>Disease==id?Count:0;
        }
        private sealed class ConditionerAdd
        {
            internal ConduitFlow Flow=null!;internal int Cell;internal PrimaryElement Item=null!;internal Observed Before;internal float Accepted;internal int Predicted;internal PipeInventory Incoming=PipeInventory.Empty;
        }
        private sealed class ConditionerScope
        {
            internal readonly AirConditioner Conditioner;internal readonly Storage Storage;internal readonly ConditionerScope? Previous;internal readonly List<ConditionerAdd> Adds=new List<ConditionerAdd>();
            internal ConditionerScope(AirConditioner conditioner,Storage storage,ConditionerScope? previous){Conditioner=conditioner;Storage=storage;Previous=previous;}
        }
        private static readonly FieldInfo ConsumerCell=AccessTools.Field(typeof(ConduitConsumer),"utilityCell"),ConsumerBuilding=AccessTools.Field(typeof(ConduitConsumer),"building"),
            DispenserCell=AccessTools.Field(typeof(ConduitDispenser),"utilityCell"),DispenserBuilding=AccessTools.Field(typeof(ConduitDispenser),"building"),
            DispenserOperation=AccessTools.Field(typeof(ConduitDispenser),"operational"),ConditionerStorage=AccessTools.Field(typeof(AirConditioner),"storage");
        private static readonly MethodInfo Satisfied=AccessTools.PropertySetter(typeof(ConduitConsumer),"IsSatisfied"),
            InputCell=AccessTools.Method(typeof(ConduitConsumer),"GetInputCell"),OutputCell=AccessTools.Method(typeof(ConduitDispenser),"GetOutputCell"),
            Suitable=AccessTools.Method(typeof(ConduitDispenser),"FindSuitableElement"),DispenserFlow=AccessTools.Method(typeof(ConduitDispenser),"GetConduitManager");
        private static readonly Func<ConduitConsumer,float> Space=(Func<ConduitConsumer,float>)Delegate.CreateDelegate(typeof(Func<ConduitConsumer,float>),AccessTools.PropertyGetter(typeof(ConduitConsumer),"space_remaining_kg"));
        private static readonly Func<ConduitConsumer,float> Rate=(Func<ConduitConsumer,float>)Delegate.CreateDelegate(typeof(Func<ConduitConsumer,float>),AccessTools.PropertyGetter(typeof(ConduitConsumer),"ConsumptionRate"));
        private const int DoBuildingDamage=-794517298,OnStorageChanged=-1697596308,OnStorageItemChanged=2051543657,EmptiedConduit=580035959;
        [ThreadStatic] private static ConditionerScope? conditioner;
        internal static bool Enabled { get; private set; }
        internal static PipeInventory ToStorage { get; private set; }=PipeInventory.Empty;
        internal static PipeInventory FromStorage { get; private set; }=PipeInventory.Empty;
        internal static PipeInventory Dumped { get; private set; }=PipeInventory.Empty;
        internal static Account StorageBoundary { get; private set; }=new Account();
        internal static long Consumed { get; private set; }
        internal static long Dispensed { get; private set; }
        internal static long Dumps { get; private set; }
        internal static long DamageEvents { get; private set; }
        private static readonly Dictionary<int,PipeInventory> DumpedByCell=new Dictionary<int,PipeInventory>();
        internal static PipeInventory DumpedAt(int buildingCell)=>DumpedByCell.TryGetValue(buildingCell,out var value)?value:PipeInventory.Empty;
        internal static string Report=>$"machine ports: {Consumed} intakes, {Dispensed} outputs, {Dumps} wrong-element dumps ({Dumped.Mass:R} kg), {DamageEvents} damage events; to storage {ToStorage.Mass:R} kg, from storage {FromStorage.Mass:R} kg; storage boundary {StorageBoundary.Describe()}";

        internal static void StartControl()
        {
            if(Enabled)return;
            if(!ColonyConduits.Active||!ColonyPipePorts.Enabled||!ColonyPipeTeardown.Enabled)throw new InvalidOperationException("Machine pipe ports require owned pipes, precise ports and conservative spills.");
            Enabled=true;
        }
        internal static void Reset()
        {
            Enabled=false;conditioner=null;ToStorage=FromStorage=Dumped=PipeInventory.Empty;StorageBoundary=new Account();DumpedByCell.Clear();
            Consumed=Dispensed=Dumps=DamageEvents=0;
        }

        internal static void RequireQuiescent(){if(conditioner!=null)throw new InvalidOperationException("A conditioner output is open at the pipe save point.");}
        internal static void Capture(PipeRuntimeImage image)
        {
            image.ToStorage=ToStorage;image.FromStorage=FromStorage;image.Dumped=Dumped;
            image.BoundaryMass=new SortedDictionary<int,PipeAmount>(StorageBoundary.Mass);image.BoundaryGerms=new SortedDictionary<int,PipeAmount>(StorageBoundary.Germs);image.BoundaryEnergy=StorageBoundary.Energy;
            foreach(var item in DumpedByCell)image.DumpedByCell.Add(item.Key,item.Value);
            image.Consumed=Consumed;image.Dispensed=Dispensed;image.Dumps=Dumps;image.DamageEvents=DamageEvents;
        }
        internal static void Restore(PipeRuntimeImage image)
        {
            ToStorage=image.ToStorage;FromStorage=image.FromStorage;Dumped=image.Dumped;
            StorageBoundary=new Account{Energy=image.BoundaryEnergy};
            foreach(var item in image.BoundaryMass)StorageBoundary.Mass.Add(item.Key,item.Value);foreach(var item in image.BoundaryGerms)StorageBoundary.Germs.Add(item.Key,item.Value);
            foreach(var item in image.DumpedByCell)DumpedByCell.Add(item.Key,item.Value);
            Consumed=image.Consumed;Dispensed=image.Dispensed;Dumps=image.Dumps;DamageEvents=image.DamageEvents;
        }

        // ConduitConsumer.Consume on an owned input cell. Returns false when
        // the native method should run (inactive, vent-owned or unowned cell).
        internal static bool Consume(ConduitConsumer consumer,float dt,ConduitFlow flow)
        {
            if(!Enabled||consumer.storage==null||flow.conduitType!=ConduitType.Gas&&flow.conduitType!=ConduitType.Liquid)return false;
            if(ColonyPipeVents.Handles(consumer,flow))return false;
            var building=(Building)ConsumerBuilding.GetValue(consumer);
            var moves=building!=null&&building.Def.CanMove;
            var cell=moves?(int)InputCell.Invoke(consumer,new object[]{flow.conduitType}):(int)ConsumerCell.GetValue(consumer);
            if(!ColonyConduits.Owns(flow,cell))return false;
            Satisfied.Invoke(consumer,new object[]{false});consumer.consumedLastTick=false;
            if(moves)ConsumerCell.SetValue(consumer,cell);
            if(!consumer.IsConnected)return true;
            var contents=ColonyConduits.ReadSynced(flow,cell);
            if(!((float)contents.Mass>0))return true;
            Satisfied.Invoke(consumer,new object[]{true});
            if(!consumer.alwaysConsume&&!consumer.operational.MeetsRequirements(consumer.OperatingRequirement))return true;
            var request=Mathf.Min(Rate(consumer)*dt,Space(consumer));
            Discover(contents);
            if(!(request>0))return true;
            var any=consumer.capacityTag==GameTags.Any;var store=consumer.wrongElementResult==ConduitConsumer.WrongElementResult.Store;
            var wanted=PipeAmount.Zero;
            foreach(var part in contents.Parts)if(Stores(part.Id,flow,consumer.capacityTag,any,store))wanted=wanted.Plus(part.Amount);
            // Space limits what enters storage; the dumped part rides along in
            // the cell's proportions, as a vanilla wrong packet would.
            double take=Math.Min(request,contents.Mass);
            if(wanted.Sign>0)take=Math.Min(take,request*contents.Mass/wanted.Value);
            var parcel=ColonyConduits.TakeForPort(flow,cell,null,take);
            if(parcel.Count==0)return true;
            consumer.consumedLastTick=true;consumer.lastConsumedElement=ColonyConduits.ElementHash(parcel.Dominant);++Consumed;
            var stored=PipeInventory.Empty;var dumped=parcel;
            foreach(var part in parcel.Parts)
                if(Stores(part.Id,flow,consumer.capacityTag,any,store))
                {var split=dumped.Split(double.MaxValue,ColonyConduits.Material,part.Id);stored=stored.Joined(split.Parcel);dumped=split.Remaining;}
            var wrong=PipeAmount.Zero;
            foreach(var part in parcel.Parts)
                if(!any&&!ElementOf(part.Id).HasTag(consumer.capacityTag))wrong=wrong.Plus(part.Amount);
            // Vanilla damages a building for each wrong packet it consumes; a
            // trace of another species in a mixture does not count as one.
            if(!any&&wrong.Value>=ColonySensors.TraceFraction*parcel.Mass)
            {
                ++DamageEvents;
                consumer.BoxingTrigger(DoBuildingDamage,new BuildingHP.DamageSourceInfo{damage=1,source=STRINGS.BUILDINGS.DAMAGESOURCES.BAD_INPUT_ELEMENT,popString=STRINGS.UI.GAMEOBJECTEFFECTS.DAMAGE_POPS.WRONG_ELEMENT});
            }
            if(stored.Count>0)Deliver(consumer.storage,stored,consumer.keepZeroMassObject);
            if(dumped.Count>0)Dump(dumped,flow,cell,Grid.PosToCell(consumer));
            return true;
        }
        private static bool Stores(int id,ConduitFlow flow,Tag tag,bool any,bool store)
        {
            var element=ElementOf(id);
            if(flow.conduitType==ConduitType.Liquid?!element.IsLiquid:!element.IsGas)return false; // a foreign phase never enters storage
            return any||store||element.HasTag(tag);
        }
        private static Element ElementOf(int id)=>ElementLoader.FindElementByHash(ColonyConduits.ElementHash(id));
        private static void Discover(PipeInventory contents)
        {
            foreach(var part in contents.Parts)
            {
                var element=ElementOf(part.Id);
                if(!DiscoveredResources.Instance.IsDiscovered(element.tag))DiscoveredResources.Instance.Discover(element.tag,element.materialCategory);
            }
        }

        // Exact owned parcel -> native storage items, one per species.
        internal static void Deliver(Storage storage,PipeInventory parcel,bool keepZeroMass)
        {
            var observedEnergy=PipeAmount.Zero;var observedGerms=new SortedDictionary<int,PipeAmount>();
            foreach(var item in NativeItems(parcel))
            {
                var material=ColonyConduits.Material(item.Part.Id);var existing=storage.FindPrimaryElement(item.Element.id);var before=new Observed(existing);
                PrimaryElement? result=null;
                if(item.Mass>0)
                    result=item.Element.IsLiquid?storage.AddLiquid(item.Element.id,item.Mass,item.Temperature,item.Disease,item.Count,keepZeroMass,false):
                        storage.AddGasChunk(item.Element.id,item.Mass,item.Temperature,item.Disease,item.Count,keepZeroMass,false);
                var after=new Observed(result??existing);
                StorageBoundary.AddMass(item.Part.Id,item.Part.Amount.Minus(new PipeAmount(after.Mass)).Plus(new PipeAmount(before.Mass)));
                observedEnergy=observedEnergy.Plus(Energy(material,after)).Minus(Energy(material,before));
                ObserveGerms(observedGerms,before,after,1);
            }
            StorageBoundary.Energy=StorageBoundary.Energy.Plus(parcel.Energy).Minus(observedEnergy);
            BookGerms(parcel,observedGerms,1);
            ToStorage=ToStorage.Joined(parcel);
        }
        // Germs follow mass: each identity is shared over species by mass and
        // merged per item with vanilla's SimUtil rule. Whatever an item cannot
        // carry is booked by the caller from the observed native change.
        internal static List<NativeItem> NativeItems(PipeInventory parcel)
        {
            var items=new List<NativeItem>(parcel.Count);
            if(parcel.Count==0)return items;
            var temperature=(float)parcel.Temperature(ColonyConduits.Material);var total=parcel.Mass;var largest=0;
            for(var i=1;i<parcel.Parts.Length;++i)if(parcel.Parts[i].Mass>parcel.Parts[largest].Mass)largest=i;
            var shares=new double[parcel.Parts.Length,parcel.Germs.Length];
            for(var g=0;g<parcel.Germs.Length;++g)
            {
                var count=Math.Floor(parcel.Germs[g].Mass);var assigned=0.0;
                for(var i=0;i<parcel.Parts.Length;++i)
                    if(i!=largest){shares[i,g]=Math.Min(count-assigned,Math.Floor(count*(parcel.Parts[i].Mass/total)));assigned+=shares[i,g];}
                shares[largest,g]=Math.Max(0,count-assigned);
            }
            for(var i=0;i<parcel.Parts.Length;++i)
            {
                var part=parcel.Parts[i];byte disease=byte.MaxValue;var germs=0;
                for(var g=0;g<parcel.Germs.Length;++g)
                {
                    var id=parcel.Germs[g].Id;var share=(int)Math.Min(int.MaxValue,shares[i,g]);
                    if(share<=0||id<0||id>=byte.MaxValue)continue;
                    var merged=Klei.SimUtil.CalculateFinalDiseaseInfo(disease,germs,(byte)id,share);disease=merged.idx;germs=merged.count;
                }
                items.Add(new NativeItem(part,ElementOf(part.Id),(float)part.Mass,temperature,disease,germs));
            }
            return items;
        }
        private static PipeAmount Energy(PipeMaterial material,Observed item)=>item.Mass>0&&item.Temperature>0?PipeInventory.Single(material,item.Mass,item.Temperature).Energy:PipeAmount.Zero;
        private static void ObserveGerms(SortedDictionary<int,PipeAmount> observed,Observed before,Observed after,int sign)
        {
            void Add(int id,double amount){if(id==byte.MaxValue||amount==0)return;observed.TryGetValue(id,out var value);observed[id]=value.Plus(new PipeAmount(sign*amount));}
            Add(after.Disease,after.Count);Add(before.Disease,-(double)before.Count);
        }
        // Booked germs = pipe-side quantity minus observed native change.
        private static void BookGerms(PipeInventory pipeSide,SortedDictionary<int,PipeAmount> observed,int sign)
        {
            var ids=new SortedSet<int>(observed.Keys);foreach(var germ in pipeSide.Germs)ids.Add(germ.Id);
            foreach(var id in ids)
            {
                var pipe=PipeAmount.Zero;foreach(var germ in pipeSide.Germs)if(germ.Id==id)pipe=germ.Amount;
                observed.TryGetValue(id,out var change);
                var booked=sign>0?pipe.Minus(change):change.Minus(pipe);
                if(booked.Sign!=0)StorageBoundary.AddGerms(id,booked);
            }
        }

        // Wrong elements leave as an exact owned spill at the building cell.
        internal static void Dump(PipeInventory parcel,ConduitFlow flow,int pipeCell,int worldCell)
        {
            if(parcel.Count==0)return;
            var id=ColonyPipePorts.HoldInventory(flow.conduitType==ConduitType.Gas?(byte)1:(byte)2,pipeCell,parcel);
            ColonyPipeTeardown.HoldSpill(id,worldCell);
            Dumped=Dumped.Joined(parcel);++Dumps;
            DumpedByCell[worldCell]=DumpedAt(worldCell).Joined(parcel);
        }

        // ConduitDispenser.Dispense on an owned output cell (vanilla logic;
        // the moved germ count is applied to both sides from one value).
        internal static bool Dispense(ConduitDispenser dispenser)
        {
            if(!Enabled||dispenser.storage==null||ColonyPipePumps.Owns(dispenser.storage))return false;
            var flow=(ConduitFlow)DispenserFlow.Invoke(dispenser,null);
            if(flow.conduitType!=ConduitType.Gas&&flow.conduitType!=ConduitType.Liquid)return false;
            var building=(Building)DispenserBuilding.GetValue(dispenser);var moves=building!=null&&building.Def.CanMove;
            var cell=moves?(int)OutputCell.Invoke(dispenser,new object[]{flow.conduitType}):(int)DispenserCell.GetValue(dispenser);
            if(!ColonyConduits.Owns(flow,cell))return false;
            var operational=(Operational)DispenserOperation.GetValue(dispenser);
            if((operational==null||!operational.IsOperational)&&!dispenser.alwaysDispense)return true;
            if(moves)DispenserCell.SetValue(dispenser,cell);
            var item=(PrimaryElement)Suitable.Invoke(dispenser,null);
            if(item==null){dispenser.empty=true;return true;}
            item.KeepZeroMassObject=true;dispenser.empty=false;
            var accepted=ColonyConduits.Acceptable(flow,cell,item.Mass,item.ElementID);
            if(!(accepted>0)){dispenser.blocked=true;return true;}
            var before=new Observed(item);
            var moved=before.Disease==byte.MaxValue?0:(int)(accepted/before.Mass*before.Count);
            var incoming=PipeInventory.Single(ColonyConduits.MaterialOf(item.ElementID),accepted,before.Temperature,before.Disease==byte.MaxValue?0:before.Disease,moved);
            ColonyConduits.ReceiveExact(flow,cell,incoming);
            if(moved>0)item.ModifyDiseaseCount(-moved,"ConduitDispenser.ConduitUpdate");
            item.Mass-=accepted;
            BookTake(item,before,incoming);++Dispensed;
            dispenser.storage.Trigger(OnStorageChanged,item.gameObject);dispenser.storage.Trigger(OnStorageItemChanged,item.gameObject);
            return true;
        }
        // Native item -> owned pipe: book observed item loss minus pipe gain.
        private static void BookTake(PrimaryElement item,Observed before,PipeInventory incoming)
        {
            var after=new Observed(item);var id=ColonyConduits.MaterialOf(item.ElementID).Id;var material=ColonyConduits.Material(id);
            StorageBoundary.AddMass(id,new PipeAmount(before.Mass).Minus(new PipeAmount(after.Mass)).Minus(incoming.Total));
            StorageBoundary.Energy=StorageBoundary.Energy.Plus(Energy(material,before)).Minus(Energy(material,after)).Minus(incoming.Energy);
            var observed=new SortedDictionary<int,PipeAmount>();ObserveGerms(observed,before,after,-1);
            BookGerms(incoming,observed,-1);
            FromStorage=FromStorage.Joined(incoming);
        }

        // AirConditioner.UpdateState adds a reheated/cooled storage item to its
        // output pipe and then debits the item itself.
        internal static void BeginConditioner(AirConditioner instance)
        {
            if(!Enabled)return;
            conditioner=new ConditionerScope(instance,(Storage)ConditionerStorage.GetValue(instance),conditioner);
        }
        internal static void EndConditioner(AirConditioner instance,Exception? error)
        {
            var scope=conditioner;if(scope==null||!ReferenceEquals(scope.Conditioner,instance))return;
            conditioner=scope.Previous;if(error!=null)return;
            foreach(var add in scope.Adds)
            {
                // The native caller computed its own germ debit; credit the
                // pipe with exactly what the item actually lost.
                var after=new Observed(add.Item);var actual=add.Before.Disease==byte.MaxValue?0:add.Before.GermsOf(add.Before.Disease)-after.GermsOf(add.Before.Disease);
                var incoming=add.Incoming;
                if(actual!=add.Predicted&&add.Before.Disease!=byte.MaxValue)
                {
                    var correction=actual-add.Predicted;
                    ColonyConduits.AdjustGerms(add.Flow,add.Cell,add.Before.Disease,correction);
                    incoming=incoming.WithDiseaseDelta(add.Before.Disease,correction);
                }
                BookTake(add.Item,add.Before,incoming);++Dispensed;
            }
        }
        internal static bool ExternalAdd(ConduitFlow flow,int cell,SimHashes element,float mass,float temperature,byte disease,int germs,ref float result)
        {
            var scope=conditioner;if(!Enabled||scope==null||scope.Storage==null)return false;
            PrimaryElement? item=null;
            foreach(var candidate in scope.Storage.items)
            {
                var primary=candidate==null?null:candidate.GetComponent<PrimaryElement>();
                if(primary!=null&&primary.ElementID==element&&primary.Mass==mass&&primary.DiseaseIdx==disease&&primary.DiseaseCount==germs){item=primary;break;}
            }
            if(item==null)throw new InvalidOperationException("A conditioner output does not match one of its storage items.");
            var accepted=ColonyConduits.Acceptable(flow,cell,mass,element);
            result=accepted;if(!(accepted>0))return true;
            var fraction=accepted/mass;var predicted=disease==byte.MaxValue?0:(int)((float)germs*fraction);
            var incoming=PipeInventory.Single(ColonyConduits.MaterialOf(element),accepted,temperature,disease==byte.MaxValue?0:disease,predicted);
            ColonyConduits.ReceiveExact(flow,cell,incoming);
            scope.Adds.Add(new ConditionerAdd{Flow=flow,Cell=cell,Item=item,Before=new Observed(item),Accepted=accepted,Predicted=predicted,Incoming=incoming});
            return true;
        }

        // EmptyConduitWorkable: a duplicant empties one owned pipe cell into
        // one native chunk per species at the cell.
        internal static bool EmptyContents(EmptyConduitWorkable workable,Conduit conduit,ref float elapsed)
        {
            if(!Enabled)return false;
            var cell=Grid.PosToCell(workable.transform.GetPosition());var flow=Conduit.GetFlowManager(conduit.type);
            if(flow==null||!ColonyConduits.Owns(flow,cell))return false;
            var parcel=ColonyConduits.TakeForPort(flow,cell,null,double.MaxValue);elapsed=0;
            if(!((float)parcel.Mass>0)&&parcel.Count==0)return true;
            var created=Chunks(parcel,cell);
            if(created>0&&workable.worker!=null)workable.worker.Trigger(EmptiedConduit,null);
            return true;
        }
        // An exact owned parcel becomes one native chunk per species at a cell
        // (pipe emptying; liquid storage of a destroyed pump or vent).
        internal static int Chunks(PipeInventory parcel,int cell)
        {
            var observedEnergy=PipeAmount.Zero;var observedGerms=new SortedDictionary<int,PipeAmount>();var created=0;
            var position=Grid.CellToPosCCC(cell,Grid.SceneLayer.Ore);
            foreach(var item in NativeItems(parcel))
            {
                Observed after=default;
                if(item.Mass>0)
                {
                    IChunkManager manager=item.Element.IsGas?(IChunkManager)GasSourceManager.Instance:LiquidSourceManager.Instance;
                    var chunk=manager.CreateChunk(item.Element.id,item.Mass,item.Temperature,item.Disease,item.Count,position);
                    after=new Observed(chunk.GetComponent<PrimaryElement>());++created;
                }
                else after=new Observed(null);
                StorageBoundary.AddMass(item.Part.Id,item.Part.Amount.Minus(new PipeAmount(after.Mass)));
                observedEnergy=observedEnergy.Plus(Energy(ColonyConduits.Material(item.Part.Id),after));
                ObserveGerms(observedGerms,new Observed(null),after,1);
            }
            StorageBoundary.Energy=StorageBoundary.Energy.Plus(parcel.Energy).Minus(observedEnergy);BookGerms(parcel,observedGerms,1);
            ToStorage=ToStorage.Joined(parcel);
            return created;
        }
        // Vanilla Storage.DropAll on destruction vents gases and drops liquids
        // as items: an owned buffer follows the same split.
        internal static void Release(ulong bufferId,int cell)
        {
            var inventory=ColonyPipePorts.BufferInventory(bufferId);if(inventory.Count==0)return;
            var gas=ColonyPipePorts.SplitBuffer(bufferId,species=>ColonyConduits.MaterialPhase(species)!=2);
            if(gas!=0)ColonyPipeTeardown.HoldSpill(gas,cell);
            var liquid=ColonyPipePorts.TakeBuffer(bufferId);
            if(liquid.Count>0)Chunks(liquid,cell);
        }
    }

    [HarmonyPatch(typeof(ConduitConsumer),"Consume")]
    internal static class ColonyMachineInputPortPatch
    {private static bool Prefix(ConduitConsumer __instance,float __0,ConduitFlow __1)=>!ColonyMachinePorts.Consume(__instance,__0,__1);}

    [HarmonyPatch(typeof(AirConditioner),"UpdateState")]
    internal static class ColonyMachineConditionerPatch
    {
        private static void Prefix(AirConditioner __instance)=>ColonyMachinePorts.BeginConditioner(__instance);
        private static Exception? Finalizer(AirConditioner __instance,Exception? __exception){ColonyMachinePorts.EndConditioner(__instance,__exception);return __exception;}
    }

    [HarmonyPatch(typeof(EmptyConduitWorkable),"EmptyContents")]
    internal static class ColonyMachineEmptyPipePatch
    {
        private static readonly FieldInfo Conduit=AccessTools.Field(typeof(EmptyConduitWorkable),"conduit"),Elapsed=AccessTools.Field(typeof(EmptyConduitWorkable),"elapsedTime");
        private static bool Prefix(EmptyConduitWorkable __instance)
        {
            var elapsed=(float)Elapsed.GetValue(__instance);
            if(!ColonyMachinePorts.EmptyContents(__instance,(Conduit)Conduit.GetValue(__instance),ref elapsed))return true;
            Elapsed.SetValue(__instance,elapsed);return false;
        }
    }
}
