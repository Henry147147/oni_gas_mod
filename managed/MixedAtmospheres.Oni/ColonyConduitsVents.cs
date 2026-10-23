using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Fresh explicit-control gas vents only. Liquid falling particles and the
    // durable component image must pass before production activation.
    internal static class ColonyPipeVents
    {
        internal sealed class State
        {
            internal readonly ConduitConsumer Consumer;internal readonly Storage Storage;internal readonly Exhaust Exhaust;
            internal readonly PrimaryElement Body;internal readonly ConduitFlow Flow;internal readonly int Cell;
            internal readonly List<ulong> Buffers=new List<ulong>();internal readonly HashSet<ulong> Pending=new HashSet<ulong>();
            internal readonly SortedDictionary<int,PipeAmount> BodyCompetition=new SortedDictionary<int,PipeAmount>(),BodyExternal=new SortedDictionary<int,PipeAmount>();
            internal PipeBodyGerms LastBody;
            internal int Exchanges;internal string LastExchange="no surface exchange";
            internal State(ConduitConsumer consumer,Exhaust exhaust,ConduitFlow flow,int cell)
            {Consumer=consumer;Storage=consumer.storage;Exhaust=exhaust;Body=exhaust.GetComponent<PrimaryElement>();Flow=flow;Cell=cell;LastBody=ReadBody(Body);}
            internal PipeInventory Inventory
            {get{var result=PipeInventory.Empty;foreach(var id in Buffers)result=result.Joined(ColonyPipePorts.OwnedInventory(id));return result;}}
        }
        internal sealed class Scope
        {
            internal readonly State State;internal readonly Scope? Previous;internal int Writes;internal SimHashes Element;internal float Mass;
            internal Scope(State state,Scope? previous){State=state;Previous=previous;}
        }
        private sealed class Identity : IEqualityComparer<Storage>
        {public bool Equals(Storage? a,Storage? b)=>ReferenceEquals(a,b);public int GetHashCode(Storage value)=>RuntimeHelpers.GetHashCode(value);}
        private static readonly Dictionary<Storage,State> States=new Dictionary<Storage,State>(new Identity());
        private static readonly Dictionary<ulong,State> Requests=new Dictionary<ulong,State>();
        private static readonly FieldInfo Input=AccessTools.Field(typeof(ConduitConsumer),"utilityCell"),
            Recently=AccessTools.Field(typeof(Exhaust),"recentlyExhausted"),LastElement=AccessTools.Field(typeof(Exhaust),"lastElementEmmited");
        [ThreadStatic] private static Scope? consuming;
        private static bool exchangeControl;
        internal static bool Enabled { get; private set; }
        internal static int BoundVents=>States.Count;
        internal static void StartControl()
        {
            if(Enabled)return;if(!ColonyPipePorts.Enabled)throw new InvalidOperationException("Mixed vents require precise pipe ports.");
            uint capability;
            try{capability=OniNativeColony.ActorBatchVersion();}
            catch(EntryPointNotFoundException error){throw new NotSupportedException("Mixed vents require atomic native actor batches before activation.",error);}
            if(capability<1)throw new NotSupportedException("Mixed vents require atomic native actor batches before activation.");
            Enabled=true;
        }
        internal static void BeginExchangeControl()=>exchangeControl=true;
        internal static void Reset(){Enabled=false;exchangeControl=false;States.Clear();Requests.Clear();consuming=null;}
        internal static Scope? BeginConsume(ConduitConsumer consumer,ConduitFlow flow)
        {
            if(!Enabled||flow.conduitType!=ConduitType.Gas)return null;
            var exhaust=consumer.GetComponent<Exhaust>();if(exhaust==null||consumer.GetComponent<Vent>()==null||consumer.storage==null)return null;
            var cell=(int)Input.GetValue(consumer);if(!ColonyConduits.Owns(flow,cell))return null;
            if(!States.TryGetValue(consumer.storage,out var state))
            {
                if(consumer.storage.items.Count!=0)throw new NotSupportedException("Mixed vent activation cannot adopt native storage packets.");
                state=new State(consumer,exhaust,flow,cell);States.Add(state.Storage,state);MixtureInspection.SetConduitStorageReader(InspectStorage);
            }
            if(!ReferenceEquals(state.Consumer,consumer)||state.Cell!=cell||state.Flow!=flow)throw new InvalidOperationException("Mixed vent port identity changed.");
            var scope=new Scope(state,consuming);consuming=scope;return scope;
        }
        internal static void EndConsume(Scope? scope,Exception? error)
        {
            if(scope==null)return;
            if(!ReferenceEquals(consuming,scope))throw new InvalidOperationException("Mixed vent consumer scopes are unbalanced.");
            consuming=scope.Previous;
            if(error==null&&scope.Writes!=0)throw new InvalidOperationException("Mixed vent consumption missed its native storage notification.");
        }
        internal static bool Remove(ConduitFlow flow,int cell,float requested,ref ConduitFlow.ConduitContents result)
        {
            if(consuming==null)return false;var state=consuming.State;
            if(state.Flow!=flow||state.Cell!=cell)return false;
            if(consuming.Writes!=0)throw new InvalidOperationException("A vent consumed a second packet before its storage notification.");
            var room=new PipeAmount(state.Storage.capacityKg).Minus(state.Inventory.Total);
            if(room.Sign<=0||!ColonyConduits.TryRead(flow,cell,out var source)||source.Count==0){result=ConduitFlow.ConduitContents.Empty;return true;}
            var amount=(float)Math.Min(requested,Math.Min(room.Value,source.Total.Value));
            while(amount>0&&(new PipeAmount(amount).Minus(room).Sign>0||new PipeAmount(amount).Minus(source.Total).Sign>0))
                amount=BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(amount)-1);
            if(!(amount>0)){result=ConduitFlow.ConduitContents.Empty;return true;}
            var id=ColonyPipePorts.HoldForPort(flow,cell,amount);var parcel=ColonyPipePorts.BufferInventory(id);
            if(id==0||parcel.Count==0)throw new InvalidOperationException("An admitted vent request produced no owned inventory.");
            state.Buffers.Add(id);Consolidate(state);result=ColonyConduits.Compatibility(parcel);
            consuming.Writes=1;consuming.Element=result.element;consuming.Mass=result.mass;return true;
        }
        internal static bool SuppressStorage(Storage storage,SimHashes element,float mass)
        {
            if(consuming==null||consuming.Writes==0||!ReferenceEquals(consuming.State.Storage,storage))return false;
            if(element!=consuming.Element||mass!=consuming.Mass)throw new InvalidOperationException("Vent notification attempted an unexpected storage write.");
            --consuming.Writes;Changed(consuming.State);return true;
        }
        internal static bool ReadMass(Storage storage,out float mass)
        {if(Enabled&&States.TryGetValue(storage,out var state)){mass=(float)state.Inventory.Mass;return true;}mass=0;return false;}
        internal static MixturePipeReading? InspectStorage(Storage storage)=>Enabled&&States.TryGetValue(storage,out var state)?
            ColonyPipeInspection.Describe(state.Inventory,"Vent contents"):ColonyPipePumps.InspectStorage(storage);
        internal static PipeInventory Inventory(Storage storage)=>States.TryGetValue(storage,out var state)?state.Inventory:PipeInventory.Empty;
        internal static string ControlDetails(Storage storage)=>States.TryGetValue(storage,out var state)?
            $"{state.Exchanges} exchanges; {state.LastExchange}; held {state.Inventory.Mass:R} kg; {state.Pending.Count} pending":"vent not bound";
        internal static bool Emit(Exhaust exhaust)
        {
            if(!Enabled)return false;var storage=exhaust.GetComponent<Storage>();if(storage==null||!States.TryGetValue(storage,out var state))return false;
            // Sim200ms still evaluates native operational/overpressure flags
            // before reaching this hook. UpdateEmission's own checks remain.
            var world=Grid.PosToCell(exhaust);if(state.Consumer.ConsumptionRate==0||Grid.Solid[world])return true;
            Consolidate(state);var id=state.Buffers.FirstOrDefault(local=>!state.Pending.Contains(local)&&ColonyPipePorts.BufferInventory(local).Count>0);
            if(id==0)return true;var original=ColonyPipePorts.BufferInventory(id);
            foreach(var part in original.Parts)if(!ElementLoader.FindElementByHash(ColonyConduits.ElementHash(part.Id)).IsGas)return true;
            var before=ReadBody(state.Body);Add(state.BodyExternal,PipeSurfaceGermPlan.ObservedChange(state.LastBody,before));state.LastBody=before;
            var plan=PipeSurfaceGermPlan.Prepare(original,before,Supports);
            ColonyPipePorts.ReplaceBufferGerms(id,plan.Fluid);
            ulong[] jobs;
            try{jobs=ColonyPipePorts.DispatchBuffer(id,(uint)world);}
            catch(ColonyPipePorts.AdmissionRejected){ColonyPipePorts.ReplaceBufferGerms(id,original);return true;}
            if(jobs.Length==0){ColonyPipePorts.ReplaceBufferGerms(id,original);return true;}
            state.Buffers.Remove(id);
            foreach(var job in jobs){state.Buffers.Add(job);state.Pending.Add(job);Requests.Add(job,state);}
            if(plan.OutOfBody.Count>0)state.Body.ModifyDiseaseCount(-plan.OutOfBody.Count,"Mixed Atmospheres vent transfer");
            foreach(var incoming in plan.IntoBody)state.Body.AddDisease((byte)incoming.Id,incoming.Count,"Mixed Atmospheres vent transfer");
            var after=ReadBody(state.Body);var competition=plan.NativeCompetition(before,after);Add(state.BodyCompetition,competition);state.LastBody=after;++state.Exchanges;
            if(exchangeControl)state.LastExchange=$"native body {before.Id}/{before.Count} -> {after.Id}/{after.Count}; fluid transfer [{string.Join(", ",plan.FluidTransfer.Select(p=>$"{p.Id}:{p.Amount.Value:R}"))}]; competition [{string.Join(", ",competition.Select(p=>$"{p.Id}:{p.Amount.Value:R}"))}]";
            ColonyConduits.BookGermExchange(plan.FluidTransfer);
            Recently.SetValue(exhaust,true);var element=ColonyConduits.ElementHash(original.Dominant);
            if((SimHashes)LastElement.GetValue(exhaust)!=element){LastElement.SetValue(exhaust,element);exhaust.Trigger(-793429877,ElementLoader.FindElementByHash(element));}
            Changed(state);return true;
        }
        internal static void Completed(ulong localId,PipePortSettlement settled)
        {
            if(!Requests.TryGetValue(localId,out var state))return;Requests.Remove(localId);state.Pending.Remove(localId);
            var vent=state.Exhaust==null?null:state.Exhaust.GetComponent<Vent>();
            if(vent!=null)foreach(var part in settled.WorldInventory.Parts)vent.UpdateVentedMass(ColonyConduits.ElementHash(part.Id),(float)part.Mass);
            state.Buffers.RemoveAll(id=>!state.Pending.Contains(id)&&ColonyPipePorts.BufferInventory(id).Count==0);Consolidate(state);Changed(state);
        }
        private static bool Supports(int id)
        {if(id<0||id>=byte.MaxValue)return false;try{return Db.Get().Diseases[(byte)id]!=null;}catch(ArgumentOutOfRangeException){return false;}catch(IndexOutOfRangeException){return false;}}
        private static PipeBodyGerms ReadBody(PrimaryElement body)=>new PipeBodyGerms(body.DiseaseIdx,body.DiseaseCount);
        private static void Add(IDictionary<int,PipeAmount> account,IEnumerable<PipePart> values)
        {foreach(var part in values){account.TryGetValue(part.Id,out var before);var amount=before.Plus(part.Amount);if(amount.Sign==0)account.Remove(part.Id);else account[part.Id]=amount;}}
        private static void Consolidate(State state)
        {
            var complete=state.Buffers.Where(id=>!state.Pending.Contains(id)).ToArray();if(complete.Length<2)return;
            var keep=ColonyPipePorts.MergeBuffers(complete);state.Buffers.RemoveAll(id=>!state.Pending.Contains(id)&&id!=keep);
        }
        private static void Changed(State state)
        {state.Storage.Trigger((int)GameHashes.OnStorageChange,null);state.Storage.OnStorageChange?.Invoke(null!);}
    }
    [HarmonyPatch(typeof(ConduitConsumer),"Consume")]
    internal static class ColonyMixedVentConsumePatch
    {
        private static void Prefix(ConduitConsumer __instance,ConduitFlow __1,ref ColonyPipeVents.Scope? __state)=>__state=ColonyPipeVents.BeginConsume(__instance,__1);
        private static Exception? Finalizer(Exception? __exception,ColonyPipeVents.Scope? __state)
        {ColonyPipeVents.EndConsume(__state,__exception);return __exception;}
    }
    [HarmonyPatch(typeof(Exhaust),"UpdateEmission")]
    internal static class ColonyMixedVentEmissionPatch
    {private static bool Prefix(Exhaust __instance)=>!ColonyPipeVents.Emit(__instance);}
    [HarmonyPatch(typeof(ConduitConsumer),"get_stored_mass")]
    internal static class ColonyMixedVentStoredMassPatch
    {
        private static bool Prefix(ConduitConsumer __instance,ref float __result)
        {if(__instance.storage==null||!ColonyPipeVents.ReadMass(__instance.storage,out var mass))return true;__result=mass;return false;}
    }
}
