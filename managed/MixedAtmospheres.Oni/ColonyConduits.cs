using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // The native graph still schedules connections. Its scalar contents are a
    // compatibility view; this inventory owns every constituent and joule.
    // Activation remains restricted to an explicit harness control until all
    // world/storage boundaries and save continuation have been validated.
    internal static class ColonyConduits
    {
        private sealed class Cell
        {
            internal PipeInventory Inventory;
            internal ConduitFlow.ConduitContents Shadow;
            // The last native heat exchange booked here (named in a fault).
            internal double LastHeat;internal float LastShadowMass,LastShadowTemperature,LastNativeMass,LastNativeTemperature;internal bool LastContact;
            internal Cell(PipeInventory inventory, ConduitFlow.ConduitContents shadow) { Inventory=inventory; Shadow=shadow; }
            internal string Describe()=>$"previous projection {Shadow.mass:R} kg {Shadow.element} at {Shadow.temperature:R} K; last native heat {LastHeat:R} J " +
                $"({(LastContact?"contact":"temperature change")} {LastShadowTemperature:R} -> {LastNativeTemperature:R} K on {LastShadowMass:R} kg, native {LastNativeMass:R} kg)";
        }
        private sealed class Flow
        {
            internal readonly Cell?[] Cells;
            internal readonly float Capacity;
            internal Flow(int count,ConduitFlow flow) { Cells=new Cell?[count];Capacity=(float)AccessTools.Field(typeof(ConduitFlow),"MaxMass").GetValue(flow); }
        }
        private static readonly Dictionary<ConduitFlow,Flow> Flows=new Dictionary<ConduitFlow,Flow>();
        private static readonly Dictionary<int,PipeMaterial> Materials=new Dictionary<int,PipeMaterial>();
        private static readonly Dictionary<int,SimHashes> NativeElements=new Dictionary<int,SimHashes>();
        private static readonly Dictionary<SimHashes,int> NativeIndices=new Dictionary<SimHashes,int>();
        private static PipeGeometry geometry;
        private static PipeInventory initial=PipeInventory.Empty,supplied=PipeInventory.Empty,removed=PipeInventory.Empty;
        private static PipeAmount heat;
        private static readonly Dictionary<int,PipeAmount> DiseaseExchange=new Dictionary<int,PipeAmount>();
        private static readonly object Accounts=new object();
        [ThreadStatic] private static int projecting;
        [ThreadStatic] private static bool controlSeeding;
        [ThreadStatic] private static TransferScope? transferScope;
        internal sealed class TransferScope
        {
            internal readonly TransferScope? Previous;
            internal readonly ConduitFlow Flow;
            internal readonly int Primary,Secondary;
            internal int Source,Target=-1;internal float Amount;
            internal TransferScope(ConduitFlow flow,int primary,int secondary,TransferScope? previous)
            {Flow=flow;Primary=Source=primary;Secondary=secondary;Previous=previous;}
        }
        internal static bool Active { get; private set; }
        internal static double ExchangedHeatJoules=>heat.Value;
        internal static bool HasDiseaseExchange
        {get{foreach(var amount in DiseaseExchange.Values)if(amount.Sign!=0)return true;return false;}}
        private static readonly Action<ConduitFlow,int,ConduitFlow.ConduitContents> SetNative =
            (Action<ConduitFlow,int,ConduitFlow.ConduitContents>)Delegate.CreateDelegate(
                typeof(Action<ConduitFlow,int,ConduitFlow.ConduitContents>),AccessTools.Method(typeof(ConduitFlow),"SetContents"));
        private static readonly Func<ConduitFlow,int,ConduitFlow.ConduitContents> ReadGrid=MakeGridReader();
        private enum MassPublication { Staged,Added,Removed }
        private delegate void StageMass(ref ConduitFlow.ConduitContents value,float initial,float added);
        private delegate void RemoveMass(ref ConduitFlow.ConduitContents value,float removed);
        private static readonly Func<ConduitFlow.ConduitContents,float> Movable=MakeBudgetReader();
        private static readonly StageMass SetStages=MakeStageWriter();
        private static readonly RemoveMass RemoveNative=MakeRemoval();
        private static Func<ConduitFlow.ConduitContents,float> MakeBudgetReader()
        {
            var type=typeof(ConduitFlow.ConduitContents);var method=new DynamicMethod("MixedPipeBudget",typeof(float),new[]{type},typeof(ColonyConduits),true);
            var il=method.GetILGenerator();il.Emit(OpCodes.Ldarga_S,(byte)0);il.Emit(OpCodes.Ldfld,AccessTools.Field(type,"initial_mass"));
            il.Emit(OpCodes.Ldarga_S,(byte)0);il.Emit(OpCodes.Ldfld,AccessTools.Field(type,"removed_mass"));il.Emit(OpCodes.Sub);il.Emit(OpCodes.Ret);
            return (Func<ConduitFlow.ConduitContents,float>)method.CreateDelegate(typeof(Func<ConduitFlow.ConduitContents,float>));
        }
        private static StageMass MakeStageWriter()
        {
            var type=typeof(ConduitFlow.ConduitContents);var method=new DynamicMethod("MixedPipeStages",typeof(void),new[]{type.MakeByRefType(),typeof(float),typeof(float)},typeof(ColonyConduits),true);
            var il=method.GetILGenerator();il.Emit(OpCodes.Ldarg_0);il.Emit(OpCodes.Ldarg_1);il.Emit(OpCodes.Stfld,AccessTools.Field(type,"initial_mass"));
            il.Emit(OpCodes.Ldarg_0);il.Emit(OpCodes.Ldarg_2);il.Emit(OpCodes.Stfld,AccessTools.Field(type,"added_mass"));
            il.Emit(OpCodes.Ldarg_0);il.Emit(OpCodes.Ldc_R4,0f);il.Emit(OpCodes.Stfld,AccessTools.Field(type,"removed_mass"));il.Emit(OpCodes.Ret);
            return (StageMass)method.CreateDelegate(typeof(StageMass));
        }
        private static RemoveMass MakeRemoval()
        {
            var type=typeof(ConduitFlow.ConduitContents);var target=AccessTools.Method(type,"RemoveMass");
            var method=new DynamicMethod("MixedPipeRemoveStage",typeof(void),new[]{type.MakeByRefType(),typeof(float)},typeof(ColonyConduits),true);
            var il=method.GetILGenerator();il.Emit(OpCodes.Ldarg_0);il.Emit(OpCodes.Ldarg_1);il.Emit(OpCodes.Call,target);
            if(target.ReturnType!=typeof(void))il.Emit(OpCodes.Pop);il.Emit(OpCodes.Ret);
            return (RemoveMass)method.CreateDelegate(typeof(RemoveMass));
        }

        private static Func<ConduitFlow,int,ConduitFlow.ConduitContents> MakeGridReader()
        {
            var grid=AccessTools.Field(typeof(ConduitFlow),"grid");
            var node=grid.FieldType.GetElementType()!;
            var method=new DynamicMethod("MixedPipeReadGrid",typeof(ConduitFlow.ConduitContents),new[]{typeof(ConduitFlow),typeof(int)},typeof(ColonyConduits),true);
            var il=method.GetILGenerator();il.Emit(OpCodes.Ldarg_0);il.Emit(OpCodes.Ldfld,grid);il.Emit(OpCodes.Ldarg_1);
            il.Emit(OpCodes.Ldelema,node);il.Emit(OpCodes.Ldfld,AccessTools.Field(node,"contents"));il.Emit(OpCodes.Ret);
            return (Func<ConduitFlow,int,ConduitFlow.ConduitContents>)method.CreateDelegate(typeof(Func<ConduitFlow,int,ConduitFlow.ConduitContents>));
        }
        internal static void StartControl()
        {
            if (Active) return;
            Initialize();
            var config=ColonyOwner.Config;
            // Take initial inventory at a deterministic activation boundary,
            // never when an overlay happens to read a previously unseen pipe.
            foreach(var flow in Flows.Keys)
                for(var y=config.OffsetY;y<config.OffsetY+config.Height;++y)
                    for(var x=config.OffsetX;x<config.OffsetX+config.Width;++x)
                    {
                        var cell=checked((int)(y*config.GridWidth+x));
                        if(flow.HasConduit(cell))_=Ensure(flow,cell);
                    }
            Active=true;MixtureInspection.SetPipeReader(ColonyPipeInspection.Read);
        }
        internal static PipeGeometry Geometry=>geometry;
        // Restore needs the live catalogue/geometry before decoding the image.
        internal static void StartRestoring(){if(Active)throw new InvalidOperationException("Mixed pipes are already active.");Reset();Initialize();restoring=true;}
        private static bool restoring;
        internal static void RequireQuiescent()
        {
            if(transferScope!=null||controlSeeding||projecting!=0)throw new InvalidOperationException("A pipe transfer is open at the save point.");
        }
        // Saved identity of the pipe catalogue: ordinal, element hash, Cv and
        // energy offset of every owned material, in ordinal order.
        internal static byte[] CatalogueIdentity()
        {
            using var bytes=new System.IO.MemoryStream();using(var writer=new System.IO.BinaryWriter(bytes,System.Text.Encoding.UTF8,true))
            {
                var ordinals=new List<int>(Materials.Keys);ordinals.Sort();
                foreach(var ordinal in ordinals){var m=Materials[ordinal];writer.Write(ordinal);writer.Write((int)NativeElements[ordinal]);writer.Write(m.HeatCapacity);writer.Write(m.EnergyOffset);}
            }
            using var hash=System.Security.Cryptography.SHA256.Create();return hash.ComputeHash(bytes.ToArray());
        }
        internal static byte MaterialPhase(int ordinal)
        {var element=ElementLoader.FindElementByHash(NativeElements[ordinal]);return element.IsGas?(byte)1:element.IsLiquid?(byte)2:(byte)0;}
        // Saving: reconcile every owned cell with its native scalar first so
        // ONI serializes exactly the projection of the saved inventory.
        internal static (PipeCellImage[] Cells,PipeInventory Initial,PipeInventory Supplied,PipeInventory Removed,PipeAmount Heat,PipePart[] Diseases) Capture()
        {
            if(!Active)throw new InvalidOperationException("No mixed pipes to capture.");
            var cells=new List<PipeCellImage>();
            foreach(var flow in Flows)
                for(var i=0;i<flow.Value.Cells.Length;++i)
                    if(flow.Value.Cells[i] is Cell cell&&flow.Key.HasConduit(i))
                    {
                        SyncNative(flow.Key,i);Publish(flow.Key,i);
                        if(cell.Inventory.Count>0)cells.Add(new PipeCellImage(flow.Key.conduitType==ConduitType.Gas?(byte)1:(byte)2,i,cell.Inventory));
                    }
                    else if(flow.Value.Cells[i] is Cell orphan&&orphan.Inventory.Count>0)
                        throw new InvalidOperationException("An owned pipe inventory has no native conduit at a save point.");
            var diseases=new List<PipePart>();foreach(var item in DiseaseExchange)if(item.Value.Sign!=0)diseases.Add(new PipePart(item.Key,item.Value));
            return(cells.ToArray(),initial,supplied,removed,heat,diseases.ToArray());
        }
        // Loading: install the saved cells and require each native contents
        // ONI restored to equal the saved inventory's projection bit for bit.
        internal static void Restore(ColonyConduitCheckpoint image)
        {
            if(Active||!restoring)throw new InvalidOperationException("Pipe restore was not prepared.");
            restoring=false;
            try
            {
                if(!image.Geometry.Equals(geometry))throw new System.IO.InvalidDataException("Saved pipe geometry differs from the restored colony.");
                if(!((ReadOnlySpan<byte>)image.CatalogueHash).SequenceEqual(CatalogueIdentity()))throw new System.IO.InvalidDataException("Saved pipe catalogue differs from the installed game.");
                var gas=Game.Instance.gasConduitFlow;var liquid=Game.Instance.liquidConduitFlow;var saved=new HashSet<(ConduitFlow,int)>();
                foreach(var item in image.Cells)
                {
                    var flow=item.Phase==1?gas:liquid;
                    foreach(var part in item.Inventory.Parts)if(!Materials.ContainsKey(part.Id))throw new System.IO.InvalidDataException("Saved pipe constituent is not in the catalogue.");
                    // Conduit networks may rebuild after this point; the
                    // contents ONI deserialized are already in its grid.
                    var native=ReadRaw(flow,item.Cell);var cell=new Cell(item.Inventory,default);var expected=Project(cell,native,true);
                    if(!SameContents(native,expected))
                        throw new System.IO.InvalidDataException($"Restored conduit {item.Cell} differs from its saved mixture: native {native.element}/{native.mass:R}/{native.temperature:R}/{native.diseaseIdx}/{native.diseaseCount}, saved {expected.element}/{expected.mass:R}/{expected.temperature:R}/{expected.diseaseIdx}/{expected.diseaseCount}.");
                    cell.Shadow=native;Flows[flow].Cells[item.Cell]=cell;saved.Add((flow,item.Cell));
                }
                var config=ColonyOwner.Config;
                foreach(var flow in Flows.Keys)
                    for(var y=config.OffsetY;y<config.OffsetY+config.Height;++y)
                        for(var x=config.OffsetX;x<config.OffsetX+config.Width;++x)
                        {
                            var index=checked((int)(y*config.GridWidth+x));
                            if(saved.Contains((flow,index)))continue;
                            if(ReadRaw(flow,index).mass>0)throw new System.IO.InvalidDataException($"Restored conduit {index} holds material the saved pipes do not.");
                        }
                initial=image.Initial;supplied=image.Supplied;removed=image.Removed;heat=image.ExternalHeat;
                foreach(var item in image.DiseaseExchange)DiseaseExchange[item.Id]=item.Amount;
                Active=true;MixtureInspection.SetPipeReader(ColonyPipeInspection.Read);
            }
            catch{Reset();throw;}
        }
        private static bool SameContents(ConduitFlow.ConduitContents a,ConduitFlow.ConduitContents b)=>a.element==b.element&&
            BitConverter.SingleToInt32Bits(a.mass)==BitConverter.SingleToInt32Bits(b.mass)&&BitConverter.SingleToInt32Bits(a.temperature)==BitConverter.SingleToInt32Bits(b.temperature)&&
            a.diseaseIdx==b.diseaseIdx&&a.diseaseCount==b.diseaseCount&&BitConverter.SingleToInt32Bits(Movable(a))==BitConverter.SingleToInt32Bits(Movable(b));
        internal static long RestoredCells=>Flows.Values.Sum(f=>f.Cells.Count(c=>c!=null&&c.Inventory.Count>0));
        private static void Initialize()
        {
            if (!ColonyOwner.Active) throw new InvalidOperationException("Mixed pipes require an active colony owner.");
            uint precision;
            try{precision=OniNativeColony.PreciseReceiptVersion();}
            catch(EntryPointNotFoundException error){throw new NotSupportedException("Mixed pipes require native precise receipt version2 before activation.",error);}
            if(precision<2)throw new NotSupportedException("Mixed pipes require native precise receipt version2 before activation.");
            var config=ColonyOwner.Config;
            geometry=new PipeGeometry(config.GridWidth,config.GridHeight,config.OffsetX,config.OffsetY,config.Width,config.Height);
            OniNativeColony.Check(OniNativeColony.SpeciesList(null,0,out var count));
            var species=new OniNativeColony.Species[count];OniNativeColony.Check(OniNativeColony.SpeciesList(species,count,out count));
            foreach(var entry in species)
            {
                var element=ElementLoader.elements[(int)entry.NativeIndex];
                var ordinal=checked((int)entry.NativeIndex);
                Materials.Add(ordinal,new PipeMaterial(ordinal,entry.CvJoulesPerKgKelvin,entry.EnergyOffsetJoulesPerKg));
                NativeElements.Add(ordinal,element.id);NativeIndices.Add(element.id,ordinal);
            }
            var cells=checked((int)(config.GridWidth*config.GridHeight));
            Flows.Add(Game.Instance.gasConduitFlow,new Flow(cells,Game.Instance.gasConduitFlow));Flows.Add(Game.Instance.liquidConduitFlow,new Flow(cells,Game.Instance.liquidConduitFlow));
        }
        internal static void Reset()
        {
            if(!restoring)ColonyPipeRuntime.ResetStatus();
            restoring=false;
            ColonyPipeThermalProbe.Reset();
            ColonyGermEvolutionProbe.Reset();
            ColonyPipeBiology.Reset();
            ColonyPipeContactHeat.Reset();
            ColonyValveControl.Reset();
            PhaseEvents=0;Active=false;Flows.Clear();Materials.Clear();NativeElements.Clear();NativeIndices.Clear();DiseaseExchange.Clear();initial=supplied=removed=PipeInventory.Empty;heat=default;projecting=0;controlSeeding=false;transferScope=null;
            ColonyMachinePorts.Reset();ColonyPipeSpecialEdges.Reset();ColonyPipePorts.Reset();MixtureInspection.SetPipeReader(null);
        }
        internal static PipeMaterial Material(int id) => Materials.TryGetValue(id,out var value)?value:throw new InvalidOperationException("Unknown mixed pipe material: "+id);
        internal static PipeMaterial MaterialOf(SimHashes element)=>Material(NativeIndices[element]);
        internal static SimHashes ElementHash(int ordinal)=>NativeElements[ordinal];
        internal static bool HasRoom(ConduitFlow flow,int cell)=>TryRead(flow,cell,out var inventory)&&flow.HasConduit(cell)&&
            inventory.Total.Minus(new PipeAmount(Flows[flow].Capacity)).Sign<0;
        internal static PipeInventory TransferSelected(ConduitFlow flow,int source,int target,int selected,bool excluding=false,double maximum=double.MaxValue)
        {
            if(!Owns(flow,source)||!Owns(flow,target)||source==target||!flow.HasConduit(source)||!flow.HasConduit(target))return PipeInventory.Empty;
            SyncNative(flow,source);SyncNative(flow,target);
            var from=Ensure(flow,source);var to=Ensure(flow,target);
            var moved=PipeInventory.Transfer(from.Inventory,to.Inventory,Math.Min(maximum,(double)Flows[flow].Capacity),Flows[flow].Capacity,Material,selected,excluding);
            from.Inventory=moved.Source;to.Inventory=moved.Target;
            if(moved.Parcel.Count>0){Publish(flow,source,MassPublication.Removed);Publish(flow,target,(float)moved.Parcel.Mass>0?MassPublication.Added:MassPublication.Staged);}
            return moved.Parcel;
        }
        internal static void SupplyControlInventory(ConduitFlow flow,int cell,PipeInventory incoming)
        {
            if(!Owns(flow,cell)||!flow.HasConduit(cell))throw new InvalidOperationException($"Control source is not an owned pipe: phase {flow.conduitType}, cell {cell}, active {Active}, owned {Owns(flow,cell)}, conduit {flow.HasConduit(cell)}.");
            SyncNative(flow,cell);var target=Ensure(flow,cell);
            var joined=target.Inventory.Joined(incoming);
            if(joined.Total.Minus(new PipeAmount(Flows[flow].Capacity)).Sign>0)throw new InvalidOperationException("Exact control inventory exceeds its pipe capacity.");
            // Fixture initialization accepts an exact expansion, not a scalar
            // dispatch request; no low room atom may be left uninitialized.
            target.Inventory=joined;lock(Accounts)supplied=supplied.Joined(incoming);Publish(flow,cell,MassPublication.Added);
        }
        internal static PipeInventory TakeForPort(ConduitFlow flow,int cell,int? species,double maximum)
        {
            if(!Owns(flow,cell)||!flow.HasConduit(cell))throw new InvalidOperationException("Port source is not an owned pipe.");
            SyncNative(flow,cell);var source=Ensure(flow,cell);var split=source.Inventory.Split(maximum,Material,species);
            source.Inventory=split.Remaining;if(split.Parcel.Count>0)Publish(flow,cell,MassPublication.Removed);return split.Parcel;
        }
        internal static PipeInventory ReceivePortBuffer(ConduitFlow flow,int cell,PipeInventory buffer)
        {
            if(!Owns(flow,cell)||!flow.HasConduit(cell))return buffer;
            SyncNative(flow,cell);var target=Ensure(flow,cell);
            var moved=PipeInventory.Transfer(buffer,target.Inventory,double.MaxValue,Flows[flow].Capacity,Material);
            target.Inventory=moved.Target;if(moved.Parcel.Count>0)Publish(flow,cell,MassPublication.Added);return moved.Source;
        }
        // Machine ports: the synced authoritative cell, the float mass a
        // native scalar source may add, and exact receipt of that addition.
        internal static PipeInventory ReadSynced(ConduitFlow flow,int cell)
        {
            if(!Owns(flow,cell)||!flow.HasConduit(cell))return PipeInventory.Empty;
            SyncNative(flow,cell);return Ensure(flow,cell).Inventory;
        }
        internal static float Acceptable(ConduitFlow flow,int cell,float mass,SimHashes element)
        {
            if(!Owns(flow,cell)||!flow.HasConduit(cell)||!(mass>0)||!NativeIndices.ContainsKey(element))return 0;
            SyncNative(flow,cell);
            var room=new PipeAmount(Flows[flow].Capacity).Minus(Ensure(flow,cell).Inventory.Total);
            if(room.Sign<=0)return 0;
            var accepted=(float)Math.Min(mass,room.Value);
            while(accepted>0&&new PipeAmount(accepted).Minus(room).Sign>0)accepted=BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(accepted)-1);
            return accepted;
        }
        internal static double PortCapacity(ConduitFlow flow)=>Flows.TryGetValue(flow,out var state)?state.Capacity:
            throw new InvalidOperationException("An exact machine port has no owned conduit phase.");
        internal static void ReceiveExact(ConduitFlow flow,int cell,PipeInventory incoming)
        {
            if(!Owns(flow,cell)||!flow.HasConduit(cell))throw new InvalidOperationException("A machine port wrote outside an owned pipe.");
            SyncNative(flow,cell);var target=Ensure(flow,cell);var joined=target.Inventory.Joined(incoming);
            if(joined.Total.Minus(new PipeAmount(Flows[flow].Capacity)).Sign>0)throw new InvalidOperationException("A machine port exceeded its pipe capacity.");
            // Like native AddElement, the new mass is not movable this tick.
            target.Inventory=joined;Publish(flow,cell);
        }
        internal static void AdjustGerms(ConduitFlow flow,int cell,int disease,double delta,bool exchange=false)
        {
            if(delta==0)return;var target=Ensure(flow,cell);target.Inventory=target.Inventory.WithDiseaseDelta(disease,delta);
            if(exchange)lock(Accounts)BookGermExchange(new[]{new PipePart(disease,new PipeAmount(delta))});
            Publish(flow,cell);
        }
        // Heat a building exchanges directly with an owned cell's mixture.
        internal static void AddHeat(ConduitFlow flow,int cell,PipeAmount joules)
        {
            if(joules.Sign==0)return;var target=Ensure(flow,cell);target.Inventory=target.Inventory.WithHeat(joules);lock(Accounts)heat=heat.Plus(joules);Publish(flow,cell);
        }
        internal static PipeInventory TransferAll(ConduitFlow flow,int source,int target,double maximum)
        {
            if(!Owns(flow,source)||!Owns(flow,target)||source==target||!flow.HasConduit(source)||!flow.HasConduit(target))return PipeInventory.Empty;
            SyncNative(flow,source);SyncNative(flow,target);
            var from=Ensure(flow,source);var to=Ensure(flow,target);
            var moved=PipeInventory.Transfer(from.Inventory,to.Inventory,Math.Min(maximum,(double)Flows[flow].Capacity),Flows[flow].Capacity,Material);
            from.Inventory=moved.Source;to.Inventory=moved.Target;
            if(moved.Parcel.Count>0){Publish(flow,source,MassPublication.Removed);Publish(flow,target);}
            return moved.Parcel;
        }
        internal static bool Owns(ConduitFlow flow,int cell) => Active && geometry.Contains(cell) && Flows.ContainsKey(flow);
        internal static bool TryRead(ConduitFlow flow,int cell,out PipeInventory inventory)
        {
            if (!Owns(flow,cell)) { inventory=PipeInventory.Empty;return false; }
            inventory=Flows[flow].Cells[cell]?.Inventory??PipeInventory.Empty;return true;
        }
        internal static bool Audit(out string detail)
        {
            var held=PipeInventory.Empty;var cells=0;
            foreach(var flow in Flows)
                for(var i=0;i<flow.Value.Cells.Length;++i)
                    if(flow.Value.Cells[i] is Cell cell)
                    {
                        held=held.Joined(cell.Inventory);if(cell.Inventory.Count>0)++cells;
                    }
            held=held.Joined(ColonyPipePorts.Held);
            var left=initial.Joined(supplied).Joined(ColonyPipePorts.FromWorld).Joined(ColonyMachinePorts.FromStorage);
            var right=held.Joined(removed).Joined(ColonyPipePorts.ToWorld).Joined(ColonyMachinePorts.ToStorage);var okay=true;
            foreach(var material in Materials.Keys)
            {
                PipeAmount Amount(PipeInventory inventory)
                {foreach(var part in inventory.Parts)if(part.Id==material)return part.Amount;return default;}
                if(Amount(left).Minus(Amount(right)).Sign!=0)okay=false;
            }
            var energy=left.Energy.Plus(heat).Minus(right.Energy).Minus(ColonyPipePorts.HeatRemainder);okay&=energy.Sign==0;
            var diseaseIds=new HashSet<int>();foreach(var germ in left.Germs)diseaseIds.Add(germ.Id);
            foreach(var germ in right.Germs)diseaseIds.Add(germ.Id);foreach(var disease in DiseaseExchange.Keys)diseaseIds.Add(disease);
            foreach(var identity in ColonyPipePorts.PassiveIdentities)diseaseIds.Add(checked((int)(identity-1)));
            foreach(var disease in diseaseIds)
            {
                PipeAmount Germs(PipeInventory inventory)
                {foreach(var germ in inventory.Germs)if(germ.Id==disease)return germ.Amount;return default;}
                DiseaseExchange.TryGetValue(disease,out var exchange);
                if(Germs(left).Plus(exchange).Plus(ColonyPipePorts.PassiveExchange((ulong)disease+1)).Minus(Germs(right)).Minus(ColonyPipePorts.PassiveRemainder((ulong)disease+1)).Sign!=0)okay=false;
            }
            detail=$"{cells} occupied cells; held {held.Mass:R} kg; supplied {supplied.Mass:R} kg; removed {removed.Mass:R} kg; external heat {heat.Value:R} J; exact U residual {energy.Value:R} J; accounts exact {okay}";
            var diseases=new List<int>(DiseaseExchange.Keys);diseases.Sort();
            foreach(var disease in diseases)detail+=$"; germ {disease} exchange {DiseaseExchange[disease].Value:R}";
            if(ColonyPipePorts.Enabled)detail+="; "+ColonyPipePorts.Report;
            if(ColonyMachinePorts.Enabled)detail+="; "+ColonyMachinePorts.Report;
            return okay;
        }
        private static Cell Ensure(ConduitFlow flow,int cell)
        {
            var state=Flows[flow];var current=state.Cells[cell];if(current!=null)return current;
            var native=ReadRaw(flow,cell);
            var inventory=native.mass>0?PipeInventory.Single(MaterialOf(native.element),native.mass,native.temperature,native.diseaseIdx,native.diseaseCount):PipeInventory.Empty;
            current=new Cell(inventory,native);state.Cells[cell]=current;
            lock(Accounts) initial=initial.Joined(inventory);
            return current;
        }
        private static ConduitFlow.ConduitContents Project(Cell cell,ConduitFlow.ConduitContents contents,bool consolidate=false)
        {
            if(cell.Inventory.Count==0)return ConduitFlow.ConduitContents.Empty;
            // Preserve native initial/added/removed bookkeeping whenever its
            // rounded mass already represents the authoritative inventory.
            var mass=(float)cell.Inventory.Mass;
            if(mass==0)return ConduitFlow.ConduitContents.Empty; // inventory retains the unrepresentable trace
            var element=NativeElements[cell.Inventory.Dominant];
            if(consolidate)SetStages(ref contents,mass,0);
            else if(contents.mass!=mass)
            {
                // Correct the scalar mass without making newly received mass
                // movable earlier than native scheduling allows.
                if(!PipeMassStages.Split(mass,Movable(contents),out var budget,out var deferred))
                    throw new InvalidOperationException("Cannot project pipe mass without changing its movement budget.");
                SetStages(ref contents,budget,deferred);
            }
            // ONI's native pipes refuse a temperature outside (0, 10000] K; an
            // inventory projecting one is a fault, never a native crash.
            double temperature;
            try{temperature=cell.Inventory.Temperature(Material);}
            catch(InvalidOperationException error){throw new InvalidOperationException(error.Message+" "+cell.Describe(),error);}
            if(!(temperature<=PipeInventory.MaximumTemperature))
                throw new InvalidOperationException($"Pipe temperature {temperature:R} K exceeds {PipeInventory.MaximumTemperature} K: energy {cell.Inventory.Energy.Value:R} J, mass {cell.Inventory.Mass:R} kg; {cell.Describe()}.");
            contents.element=element;contents.temperature=(float)temperature;
            contents.diseaseIdx=byte.MaxValue;contents.diseaseCount=0;
            foreach(var germ in cell.Inventory.Germs)
                if(germ.Mass>=1 && germ.Mass>contents.diseaseCount)
                {contents.diseaseIdx=checked((byte)germ.Id);contents.diseaseCount=(int)Math.Min(int.MaxValue,germ.Mass);}
            return contents;
        }
        internal static ConduitFlow.ConduitContents Compatibility(PipeInventory inventory)=>Project(new Cell(inventory,default),default,true);
        internal static void BookGermExchange(IEnumerable<PipePart> changes)
        {
            foreach(var change in changes)
            {DiseaseExchange.TryGetValue(change.Id,out var before);DiseaseExchange[change.Id]=before.Plus(change.Amount);}
        }
        internal static IEnumerable<(ConduitFlow Flow,int Cell,PipeInventory Inventory)> BiologicalCells()
        {
            foreach(var flow in Flows)
                for(var index=0;index<flow.Value.Cells.Length;++index)
                    if(flow.Value.Cells[index] is Cell cell&&cell.Inventory.Count>0&&flow.Key.HasConduit(index))
                    {SyncNative(flow.Key,index);yield return(flow.Key,index,cell.Inventory);}
        }
        // Include empty pipes: their culture allocation mass and cached rule
        // still govern germs that arrive after the next load.
        internal static IEnumerable<(ConduitFlow Flow,int Cell)> NativeCultureCells()
        {
            foreach(var flow in Flows.Keys)
                for(var index=0;index<flow.soaInfo.NumEntries;++index)
                {
                    var conduit=flow.soaInfo.GetConduit(index);var cell=flow.soaInfo.GetCell(conduit.idx);
                    if(Owns(flow,cell))yield return(flow,cell);
                }
        }
        internal static void CommitBiology(ConduitFlow flow,int index,PipeInventory inventory,IEnumerable<PipePart> changes)
        {
            var cell=Ensure(flow,index);cell.Inventory=inventory;
            lock(Accounts)BookGermExchange(changes);
            Publish(flow,index);
        }
        private static void Publish(ConduitFlow flow,int index,MassPublication mode=MassPublication.Staged)
        {
            var cell=Ensure(flow,index);var contents=ReadGrid(flow,index);var mass=(float)cell.Inventory.Mass;
            if(mode==MassPublication.Removed&&contents.mass>mass)RemoveNative(ref contents,contents.mass-mass);
            var projection=Project(cell,contents,mode==MassPublication.Added);
            ++projecting;try{SetNative(flow,index,projection);}finally{--projecting;}
            cell.Shadow=projection;ColonyPipePumps.ProjectPipe(flow,index);
        }
        // Restore native staging after the topology flush. This writes only
        // the compatibility projection: no physical move, heat reconciliation,
        // ledger entry or pump notification may run while scheduling is bound.
        internal static void ValidateNativeProjection(ConduitFlow flow,int index,ConduitFlow.ConduitContents contents)
        {
            if(!Owns(flow,index)||!flow.HasConduit(index))
                throw new System.IO.InvalidDataException("Saved native staging has no owned live conduit.");
            // Unallocated empty managed cells are inspected without importing
            // a native packet or changing initial physical inventory/accounts.
            var cell=Flows[flow].Cells[index]??new Cell(PipeInventory.Empty,default);
            if(!SameContents(contents,Project(cell,contents)))
                throw new System.IO.InvalidDataException("Saved native staging does not project the exact pipe inventory.");
        }
        internal static void RestoreNativeProjection(ConduitFlow flow,int index,ConduitFlow.ConduitContents contents)
        {
            ValidateNativeProjection(flow,index,contents);
            var cell=Flows[flow].Cells[index]??new Cell(PipeInventory.Empty,default);
            ++projecting;try{SetNative(flow,index,contents);}finally{--projecting;}
            cell.Shadow=contents;Flows[flow].Cells[index]=cell;
        }
        internal static void ReadNative(ConduitFlow flow,int index,ref ConduitFlow.ConduitContents result)
        {
            if(projecting!=0 || !Owns(flow,index))return;
            if(transferScope is var scope&&scope!=null&&scope.Flow==flow&&(index==scope.Primary||index==scope.Secondary))scope.Source=index;
            // Readers project completed ownership only. They cannot change
            // heat, germs, accounts, native state or the physical trajectory.
            var cell=Flows[flow].Cells[index];
            if(cell!=null)result=Project(cell,result);
            else if(result.mass>0)throw new InvalidOperationException("A nonempty native pipe was not registered at a physical boundary.");
        }
        private static ConduitFlow.ConduitContents ReadRaw(ConduitFlow flow,int index)
        {
            ++projecting;try{return flow.GetContents(index);}finally{--projecting;}
        }
        private static void SyncNative(ConduitFlow flow,int index)
        {
            var cell=Ensure(flow,index);
            var result=ReadRaw(flow,index);
            var changed=false;
            var currentContact=ColonyPipeContactHeat.TryTake(flow,index,result,cell.Shadow,out var contactHeat);
            if(cell.Inventory.Count>0 && (currentContact||result.temperature!=cell.Shadow.temperature))
            {
                var element=ElementLoader.FindElementByHash(cell.Shadow.element);
                var exchanged=currentContact?contactHeat:new PipeAmount((double)cell.Shadow.mass*element.specificHeatCapacity*1000*((double)result.temperature-cell.Shadow.temperature));
                cell.LastHeat=exchanged.Value;cell.LastContact=currentContact;cell.LastShadowMass=cell.Shadow.mass;cell.LastShadowTemperature=cell.Shadow.temperature;
                cell.LastNativeMass=result.mass;cell.LastNativeTemperature=result.temperature;
                cell.Inventory=cell.Inventory.WithHeat(exchanged);lock(Accounts)heat=heat.Plus(exchanged);
                changed=true;
            }
            // Native disease growth/death is reconciled separately from moves;
            // every unprojected disease remains in the authoritative inventory.
            if(cell.Inventory.Count>0)
            {
                void DiseaseDelta(int disease,double delta)
                {
                    if(delta==0)return;
                    cell.Inventory=cell.Inventory.WithDiseaseDelta(disease,delta);changed=true;
                    lock(Accounts)
                    {
                        DiseaseExchange.TryGetValue(disease,out var before);
                        DiseaseExchange[disease]=before.Plus(new PipeAmount(delta));
                    }
                }
                if(cell.Shadow.diseaseIdx!=byte.MaxValue)
                    DiseaseDelta(cell.Shadow.diseaseIdx,(result.diseaseIdx==cell.Shadow.diseaseIdx?(double)result.diseaseCount:0)-cell.Shadow.diseaseCount);
                if(result.diseaseIdx!=byte.MaxValue&&result.diseaseIdx!=cell.Shadow.diseaseIdx)DiseaseDelta(result.diseaseIdx,result.diseaseCount);
            }
            result=Project(cell,result);cell.Shadow=result;
            if(changed)
            {
                ++projecting;try{SetNative(flow,index,result);}finally{--projecting;}
            }
        }
        internal static void BeginCell(ConduitFlow flow,int cell)
        {
            if(!Owns(flow,cell))return;
            SyncNative(flow,cell);Publish(flow,cell);
        }
        internal static void ThermalBoundary()
        {
            if(!Active)return;
            foreach(var flow in Flows)
                for(var i=0;i<flow.Value.Cells.Length;++i)
                    if(flow.Value.Cells[i] is Cell cell&&cell.Inventory.Count>0&&flow.Key.HasConduit(i))SyncNative(flow.Key,i);
            ColonyPipeTeardown.Tick(ColonyOwner.Status.CompletedFrames);
        }
        // Phase change in owned pipes (docs/mixed-pipes.md#phase-change). The
        // native check sees only the projected dominant element, so it is
        // skipped for owned cells. Any constituent above 10% of capacity
        // (vanilla's packet rule) that is past its own transition at the
        // mixture temperature raises vanilla's frozen/boiling conduit event:
        // the pipe takes damage and dumps its whole mixture into the world.
        private const int ConduitFrozen=-700727624,ConduitBoiling=-1152799878;
        internal static long PhaseEvents { get; set; }
        internal static void PhaseBoundary()
        {
            if(!Active||!ColonyPipeTeardown.Enabled)return;
            List<(ConduitFlow Flow,int Cell,int Event)>? events=null;
            foreach(var flow in Flows)
            {
                var threshold=.1*flow.Value.Capacity;
                for(var i=0;i<flow.Value.Cells.Length;++i)
                {
                    if(!(flow.Value.Cells[i] is Cell cell)||cell.Inventory.Count==0||!flow.Key.HasConduit(i))continue;
                    int kind;
                    try{kind=PhaseEvent(cell.Inventory,threshold);}
                    catch(InvalidOperationException){continue;} // a trace without a representable temperature has no phase event
                    if(kind!=0)(events??=new List<(ConduitFlow,int,int)>()).Add((flow.Key,i,kind));
                }
            }
            if(events==null)return;
            foreach(var item in events)
            {
                var go=item.Flow.soaInfo.GetConduitGO(item.Flow.GetConduit(item.Cell).idx);
                if(go==null)continue;
                ++PhaseEvents;go.Trigger(item.Event,null);
            }
        }
        internal static int PhaseEvent(PipeInventory inventory,double threshold)
        {
            var temperature=inventory.Temperature(Material);
            foreach(var part in inventory.Parts)
            {
                if(!(part.Mass>threshold))continue;
                var element=ElementLoader.FindElementByHash(NativeElements[part.Id]);
                if(temperature<element.lowTemp)return ConduitFrozen;
                if(temperature>element.highTemp)return ConduitBoiling;
            }
            return 0;
        }
        internal static bool SkipNativePhase(ConduitFlow flow,int conduit)=>Active&&Owns(flow,flow.soaInfo.GetCell(conduit));
        internal static IEnumerable<(ConduitFlow Flow,int Cell,PipeInventory Inventory,ConduitFlow.ConduitContents Native,ConduitFlow.ConduitContents Shadow)> ThermalProbeSamples()
        {
            foreach(var flow in Flows)
                for(var i=0;i<flow.Value.Cells.Length;++i)
                    if(flow.Value.Cells[i] is Cell cell&&cell.Inventory.Count>0&&flow.Key.HasConduit(i))
                        yield return(flow.Key,i,cell.Inventory,ReadRaw(flow.Key,i),cell.Shadow);
        }
        internal static ConduitFlow.ConduitContents ThermalProbeNative(ConduitFlow flow,int cell)=>ReadRaw(flow,cell);
        internal static bool Compatible(bool native,ConduitFlow flow,int source,int target)
        {
            var ownsSource=Owns(flow,source);var ownsTarget=Owns(flow,target);
            return ownsSource&&ownsTarget || !ownsSource&&!ownsTarget&&native;
        }
        internal static float Limit(ConduitFlow flow,int source,int target,float request)
        {
            var ownsSource=Owns(flow,source);var ownsTarget=Owns(flow,target);
            if(ownsSource!=ownsTarget)return 0;
            if(!ownsSource)return request;
            SyncNative(flow,target);
            var available=Ensure(flow,source).Inventory.Total;
            var room=new PipeAmount(Flows[flow].Capacity).Minus(Ensure(flow,target).Inventory.Total);
            if(available.Sign<=0||room.Sign<=0)return 0;
            request=Math.Min(request,(float)Math.Min(available.Value,room.Value));
            while(request>0 && (new PipeAmount(request).Minus(available).Sign>0 || new PipeAmount(request).Minus(room).Sign>0))
                request=BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(request)-1);
            return request;
        }
        internal static void Move(ConduitFlow flow,int source,int target,float amount)
        {
            if(!Owns(flow,source)||!Owns(flow,target))return;
            if(source==target)return;
            var from=Ensure(flow,source);var to=Ensure(flow,target);
            var transfer=PipeInventory.Transfer(from.Inventory,to.Inventory,amount,Flows[flow].Capacity,Material);
            if(transfer.Parcel.Mass!=amount)throw new InvalidOperationException("Native pipe move exceeded owned inventory/capacity.");
            from.Inventory=transfer.Source;to.Inventory=transfer.Target;
        }
        internal static void FinishCell(ConduitFlow flow,int cell) { if(Owns(flow,cell))Publish(flow,cell); }
        internal static void FinishTarget(ConduitFlow flow,int cell) { if(Owns(flow,cell))Publish(flow,cell); }
        internal static TransferScope? BeginTransfer(ConduitFlow flow,int source,int secondary=-1)
        {
            if(!Owns(flow,source))return null;
            var scope=new TransferScope(flow,source,secondary,transferScope);transferScope=scope;
            BeginCell(flow,source);if(Owns(flow,secondary))BeginCell(flow,secondary);return scope;
        }
        internal static void EndTransfer(TransferScope? scope,Exception? error)
        {
            if(scope==null)return;
            transferScope=scope.Previous;
            if(error==null&&scope.Target>=0)throw new InvalidOperationException("Native pipe edge accepted a staged transfer without removing its source.");
        }
        internal static float Seed(ConduitFlow flow,int cell,SimHashes element,float mass,float temperature,byte disease,int germs)
        {
            if(controlSeeding)throw new InvalidOperationException("Nested mixed-pipe seeding.");
            controlSeeding=true;try{return flow.AddElement(cell,element,mass,temperature,disease,germs);}finally{controlSeeding=false;}
        }
        internal static bool Add(ConduitFlow flow,int cell,SimHashes element,float mass,float temperature,byte disease,int germs,ref float result)
        {
            if(projecting!=0)return false;
            if(!Owns(flow,cell))
            {
                if(transferScope is var crossing&&crossing!=null&&crossing.Flow==flow){result=0;return true;}
                return false;
            }
            if(!flow.HasConduit(cell)||mass<=0){result=0;return true;}
            if(transferScope is var scope&&scope!=null&&scope.Flow==flow)
            {
                if(scope.Target>=0)throw new InvalidOperationException("Native pipe edge staged two transfers without a source removal.");
                result=Limit(flow,scope.Source,cell,mass);
                if(result>0){scope.Target=cell;scope.Amount=result;}
                return true;
            }
            if(!controlSeeding)
            {
                // Adapted scalar sources (conditioners) credit exactly what
                // they debit; any other external writer stays refused.
                if(ColonyMachinePorts.ExternalAdd(flow,cell,element,mass,temperature,disease,germs,ref result))return true;
                result=0;return true;
            }
            SyncNative(flow,cell);
            var current=Ensure(flow,cell);
            var incoming=PipeInventory.Single(MaterialOf(element),mass,temperature,disease,germs);
            var transfer=PipeInventory.Transfer(incoming,current.Inventory,mass,Flows[flow].Capacity,Material);
            // The native caller removes this same float. Retain a port request
            // remainder at the source rather than returning a rounded-up flux.
            var accepted=(float)transfer.Parcel.Mass;
            if(accepted>transfer.Parcel.Mass)accepted=BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(accepted)-1);
            transfer=PipeInventory.Transfer(incoming,current.Inventory,accepted,Flows[flow].Capacity,Material);
            current.Inventory=transfer.Target;lock(Accounts)supplied=supplied.Joined(transfer.Parcel);
            Publish(flow,cell,MassPublication.Added);result=accepted;return true;
        }
        internal static bool Remove(ConduitFlow flow,int cell,float mass,ref ConduitFlow.ConduitContents result)
        {
            if(projecting!=0||!Owns(flow,cell))return false;
            if(transferScope is var scope&&scope!=null&&scope.Flow==flow&&scope.Target>=0)
            {
                if(scope.Source!=cell||mass!=scope.Amount)throw new InvalidOperationException("Native pipe edge removed a different staged source/amount.");
                var parcel=Ensure(flow,cell).Inventory.Split(mass,Material).Parcel;
                Move(flow,cell,scope.Target,mass);Publish(flow,cell,MassPublication.Removed);Publish(flow,scope.Target,MassPublication.Added);scope.Target=-1;
                var shadow=new Cell(parcel,default);result=Project(shadow,default);return true;
            }
            // No float-only world/storage dispatch from an unfinished control.
            result=ConduitFlow.ConduitContents.Empty;return true;
        }
        internal static bool CanDump(ConduitFlow flow,int cell)=>!Owns(flow,cell)||ColonyPipeTeardown.Dump(flow,cell);
    }
    [HarmonyPatch(typeof(ConduitFlow),nameof(ConduitFlow.GetContents))]
    internal static class ColonyMixedPipeReadPatch
    {
        private static void Postfix(ConduitFlow __instance,int __0,ref ConduitFlow.ConduitContents __result)=>ColonyConduits.ReadNative(__instance,__0,ref __result);
    }
    [HarmonyPatch(typeof(ConduitFlow),nameof(ConduitFlow.AddElement))]
    internal static class ColonyMixedPipeAddPatch
    {
        private static bool Prefix(ConduitFlow __instance,int __0,SimHashes __1,float __2,float __3,byte __4,int __5,ref float __result)=>
            !ColonyConduits.Add(__instance,__0,__1,__2,__3,__4,__5,ref __result);
    }
    [HarmonyPatch(typeof(ConduitFlow),nameof(ConduitFlow.RemoveElement),new[]{typeof(int),typeof(float)})]
    internal static class ColonyMixedPipeRemovePatch
    {
        private static bool Prefix(ConduitFlow __instance,int __0,float __1,ref ConduitFlow.ConduitContents __result)=>
            !ColonyPipeVents.Remove(__instance,__0,__1,ref __result)&&!ColonyConduits.Remove(__instance,__0,__1,ref __result);
    }
    [HarmonyPatch(typeof(ConduitFlow),"DumpPipeContents")]
    internal static class ColonyMixedPipeDumpPatch
    {
        private static bool Prefix(ConduitFlow __instance,int __0)=>ColonyConduits.CanDump(__instance,__0);
    }
    [HarmonyPatch]
    internal static class ColonyMixedPipeThermalPatch
    {
        internal sealed class Scope
        {
            internal ColonyPipeThermalProbe.Scope? Probe;
            internal ColonyPipeContactHeat.Scope? Contact;
        }
        private static MethodBase TargetMethod()=>AccessTools.Method(AccessTools.TypeByName("ConduitTemperatureManager"),"Sim200ms");
        private static void Prefix(float __0,ref Scope? __state)
        {
            var contact=ColonyPipeContactHeat.Begin(__0);
            if(contact!=null)__state=new Scope{Contact=contact};
            var probe=ColonyPipeThermalProbe.Begin(__0);
            if(probe!=null){__state??=new Scope();__state.Probe=probe;}
        }
        private static void Postfix(float __0,Scope? __state)
        {
            ColonyPipeThermalProbe.AfterNative(__state?.Probe);__state?.Contact?.Validate();
            ColonyConduits.ThermalBoundary();__state?.Contact?.Complete();ColonyPipeThermalProbe.AfterBook(__state?.Probe);
            // Network rebuilds call the thermal update with dt 0; phase
            // damage is a timed thermal event and waits for a real tick.
            if(__0>0)ColonyConduits.PhaseBoundary();
        }
        private static Exception? Finalizer(Exception? __exception,Scope? __state)
        {
            try{ColonyPipeThermalProbe.EndScope(__state?.Probe);}
            finally{ColonyPipeContactHeat.End(__state?.Contact);}
            return __exception;
        }
    }
    [HarmonyPatch]
    internal static class ColonyMixedPipeNativePhasePatch
    {
        private static IEnumerable<MethodBase> TargetMethods()
        {yield return AccessTools.Method(typeof(ConduitFlow),"FreezeConduitContents");yield return AccessTools.Method(typeof(ConduitFlow),"MeltConduitContents");}
        private static bool Prefix(ConduitFlow __instance,int __0)=>!ColonyConduits.SkipNativePhase(__instance,__0);
    }
    [HarmonyPatch(typeof(ConduitFlow),"UpdateConduit")]
    internal static class ColonyMixedPipeFlowPatch
    {
        // This hook retains ONI's topology, pull directions, branch fairness,
        // capacities and packet scheduling. Only the owned payload moves here.
        // Match the supported build's field operations and fail closed on an
        // incompatible body instead of silently running scalar ownership.
        private static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions,MethodBase original)
        {
            var code=new List<CodeInstruction>(instructions);
            var locals=original.GetMethodBody()!.LocalVariables;
            if(locals.Count<28||locals[1].LocalType!=typeof(int)||locals[13].LocalType!=typeof(int)||locals[17].LocalType!=typeof(float))
                throw new InvalidOperationException("Unsupported ConduitFlow.UpdateConduit local layout.");
            var began=0;var target=0;var compared=0;var limited=0;var moved=0;var finished=0;
            var element=AccessTools.Field(typeof(ConduitFlow.ConduitContents),"element");
            for(var i=0;i<code.Count;++i)
            {
                var instruction=code[i];
                yield return instruction;
                if(Slot(instruction,true)==1)
                {
                    foreach(var item in Call(nameof(ColonyConduits.BeginCell),1))yield return item;++began;
                }
                if(Slot(instruction,true)==13)
                {
                    foreach(var item in Call(nameof(ColonyConduits.BeginCell),13))yield return item;++target;
                }
                if(instruction.opcode==OpCodes.Ceq && i>=4 && code[i-1].LoadsField(element) && code[i-4].LoadsField(element))
                {
                    yield return new CodeInstruction(OpCodes.Ldarg_0);
                    yield return new CodeInstruction(OpCodes.Ldloc_1);yield return new CodeInstruction(OpCodes.Ldloc_S,(byte)13);
                    yield return CodeInstruction.Call(typeof(ColonyConduits),nameof(ColonyConduits.Compatible));++compared;
                }
                if(Slot(instruction,true)==17)
                {
                    yield return new CodeInstruction(OpCodes.Ldarg_0);yield return new CodeInstruction(OpCodes.Ldloc_1);
                    yield return new CodeInstruction(OpCodes.Ldloc_S,(byte)13);yield return new CodeInstruction(OpCodes.Ldloc_S,(byte)17);
                    yield return CodeInstruction.Call(typeof(ColonyConduits),nameof(ColonyConduits.Limit));
                    yield return new CodeInstruction(OpCodes.Stloc_S,(byte)17);++limited;
                }
                if(instruction.opcode==OpCodes.Stfld && instruction.operand is FieldInfo field && field.Name=="contents" &&
                    i>0 && Slot(code[i-1],false)==14)
                {
                    // Target hydration happened before its native read above;
                    // no overwritten scalar packet is imported as new mass.
                    yield return new CodeInstruction(OpCodes.Ldarg_0);yield return new CodeInstruction(OpCodes.Ldloc_1);
                    yield return new CodeInstruction(OpCodes.Ldloc_S,(byte)13);yield return new CodeInstruction(OpCodes.Ldloc_S,(byte)17);
                    yield return CodeInstruction.Call(typeof(ColonyConduits),nameof(ColonyConduits.Move));
                    foreach(var item in Call(nameof(ColonyConduits.FinishTarget),13))yield return item;++moved;
                }
                if(instruction.opcode==OpCodes.Stfld && instruction.operand is FieldInfo sourceField && sourceField.Name=="contents" &&
                    i>0 && code[i-1].LoadsField(AccessTools.Field(AccessTools.Field(typeof(ConduitFlow),"grid").FieldType.GetElementType(),"contents")))
                {
                    foreach(var item in Call(nameof(ColonyConduits.FinishCell),1))yield return item;++finished;
                }
            }
            if(began!=1||target!=1||compared!=1||limited!=1||moved!=1||finished!=1)
                throw new InvalidOperationException($"Unsupported conduit move hooks: {began}/{target}/{compared}/{limited}/{moved}/{finished}.");
        }
        private static int Slot(CodeInstruction instruction,bool store)
        {
            var op=instruction.opcode;
            if(op==(store?OpCodes.Stloc_0:OpCodes.Ldloc_0))return 0;
            if(op==(store?OpCodes.Stloc_1:OpCodes.Ldloc_1))return 1;
            if(op==(store?OpCodes.Stloc_2:OpCodes.Ldloc_2))return 2;
            if(op==(store?OpCodes.Stloc_3:OpCodes.Ldloc_3))return 3;
            if(op!=(store?OpCodes.Stloc:OpCodes.Ldloc)&&op!=(store?OpCodes.Stloc_S:OpCodes.Ldloc_S))return -1;
            return instruction.operand is LocalVariableInfo local?local.LocalIndex:Convert.ToInt32(instruction.operand);
        }
        private static IEnumerable<CodeInstruction> Call(string method,int local)
        {
            yield return new CodeInstruction(OpCodes.Ldarg_0);yield return new CodeInstruction(OpCodes.Ldloc_S,(byte)local);
            yield return CodeInstruction.Call(typeof(ColonyConduits),method);
        }
    }
}
