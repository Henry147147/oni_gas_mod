using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Explicit-control all-identity storage biology. Native headers
    // are masked only for projection items, then restored before publication.
    internal static partial class ColonyOwnedStorage
    {
        private static bool storageBiologyEnabled;
        internal static void ResetBiology(){storageBiologyEnabled=false;StoredBiologyTicks=0;}
        internal static void StartBiologyControl()
        {
            if(!Enabled)throw new InvalidOperationException("Storage biology requires the explicit exact-storage control.");
            InitializeStoredCultureContexts(GameComps.DiseaseContainers);
            storageBiologyEnabled=true;
        }
        private static void InitializeStoredCultureContexts(DiseaseContainers manager)
        {
            var headers=(IList)StoredBiologyAccess.Headers.GetValue(manager);var payloads=(IList)StoredBiologyAccess.Payloads.GetValue(manager);
            var native=new Dictionary<PrimaryElement,(DiseaseHeader Header,DiseaseContainer Payload)>();
            for(var i=0;i<headers.Count;++i)
            {
                var header=(DiseaseHeader)headers[i];
                if(header.primaryElement!=null&&IsProjection(header.primaryElement))
                {
                    if(native.ContainsKey(header.primaryElement))throw new InvalidOperationException("A stored projection has duplicate native culture rows.");
                    native.Add(header.primaryElement,(header,(DiseaseContainer)payloads[i]));
                }
            }
            var prepared=new List<PreparedCulture>();
            foreach(var host in Hosts.Values)
            {
                var inventory=host.Store.Inventory;
                foreach(var germ in inventory.Germs)
                {
                    var clockPresent=host.Store.Clocks.TryGetValue(germ.Id,out var clock);
                    var contextPresent=host.Store.CultureContexts.TryGetValue(germ.Id,out var context);
                    if(clockPresent!=contextPresent)throw new InvalidOperationException("A stored culture has incomplete authoritative clock/cache state.");
                    if(clockPresent)continue;
                    var integer=germ.Mass<=int.MaxValue&&germ.Mass==Math.Floor(germ.Mass)&&germ.Amount.Minus(new PipeAmount(germ.Mass)).Sign==0;
                    var nativeMass=(float)inventory.Mass;
                    var expected=!integer||!(nativeMass>0)||inventory.Total.Minus(new PipeAmount(nativeMass)).Sign!=0;
                    context=new StoredGermContext((ushort)inventory.Dominant,0,1,false);var residue=0f;
                    if(!expected&&inventory.Count==1&&inventory.Germs.Length==1&&host.Projections.TryGetValue(inventory.Dominant,out var view))
                    {
                        if(native.TryGetValue(view.Item,out var row)&&row.Header.diseaseIdx==germ.Id&&row.Header.diseaseCount==(int)germ.Mass)
                        {
                            context=new StoredGermContext(row.Payload.elemIdx,row.Payload.overpopulationCount,row.Payload.instanceGrowthRate,true);
                            residue=row.Payload.accumulatedError;
                        }
                        else if(integer&&inventory.Germs.Length==1)
                            throw new InvalidOperationException("A native-compatible stored culture is not ready for exact cache capture.");
                    }
                    prepared.Add(new PreparedCulture{Host=host,Id=germ.Id,Context=context,Step=new GermPopulationStep(germ.Amount,PipeAmount.Zero,new GermPopulationClock(expected&&residue==0,residue))});
                }
            }
            foreach(var group in prepared.GroupBy(change=>change.Host))
                if((ulong)group.Count()>ulong.MaxValue-group.Key.Store.Revision)throw new InvalidOperationException("Storage context capture revision overflow.");
            foreach(var change in prepared)change.Host.Store.CommitCulture(change.Id,change.Step,change.Context);
        }
        internal static long StoredBiologyTicks {get;private set;}
        internal static bool CompareStoredNativeCulture(Storage exact,Storage original,int id,out string detail)
        {
            if(!Hosts.TryGetValue(exact,out var host)||!host.Store.Clocks.TryGetValue(id,out var clock)||!host.Store.CultureContexts.TryGetValue(id,out var context))
            {detail="authoritative culture is missing";return false;}
            var items=original.items.Where(item=>item!=null).Select(item=>item.GetComponent<PrimaryElement>()).Where(item=>item!=null&&item.Mass>0).ToArray();
            if(items.Length!=1){detail="native control does not have one physical item";return false;}
            var headers=(IList)StoredBiologyAccess.Headers.GetValue(GameComps.DiseaseContainers);var payloads=(IList)StoredBiologyAccess.Payloads.GetValue(GameComps.DiseaseContainers);
            for(var i=0;i<headers.Count;++i)
            {
                var header=(DiseaseHeader)headers[i];if(!ReferenceEquals(header.primaryElement,items[0]))continue;
                var actual=(DiseaseContainer)payloads[i];var population=PipeAmount.Zero;foreach(var germ in host.Store.Inventory.Germs)if(germ.Id==id)population=germ.Amount;
                var equal=header.diseaseIdx==id&&population.Minus(new PipeAmount(header.diseaseCount)).Sign==0&&!clock.Expected&&context.NativeSingle&&
                    BitConverter.SingleToInt32Bits(clock.NativeResidue)==BitConverter.SingleToInt32Bits(actual.accumulatedError)&&context.Element==actual.elemIdx&&
                    context.Overpopulation==actual.overpopulationCount&&BitConverter.SingleToInt32Bits(context.TagGrowthBase)==BitConverter.SingleToInt32Bits(actual.instanceGrowthRate)&&
                    BitConverter.SingleToInt32Bits((float)host.Store.Inventory.Temperature(ColonyConduits.Material))==BitConverter.SingleToInt32Bits(items[0].Temperature);
                detail=$"owned/native {population.Value:R}/{header.diseaseCount}, residues {clock.NativeResidue:R}/{actual.accumulatedError:R}, cache {context.Element}/{actual.elemIdx}, tag {context.TagGrowthBase:R}/{actual.instanceGrowthRate:R}, temperatures {host.Store.Inventory.Temperature(ColonyConduits.Material):R}/{items[0].Temperature:R}";return equal;
            }
            detail="native control culture row is not allocated";return false;
        }
        internal static bool VerifyStoredLogicalTags(out string detail)
        {
            var checkedRules=0;
            foreach(var host in Hosts.Values)foreach(var pair in host.Projections)
            {
                var logical=StoredTags(host,pair.Key);var actual=StoredBiologyAccess.Tags(pair.Value.Item.GetComponent<KPrefabID>());
                for(var id=0;id<Db.Get().Diseases.Count;++id)foreach(var over in new[]{false,true})
                {
                    var definition=Db.Get().Diseases[(byte)id];var expected=definition.GetGrowthRateForTags(logical,over);var original=definition.GetGrowthRateForTags(actual,over);
                    if(BitConverter.SingleToInt32Bits(expected)!=BitConverter.SingleToInt32Bits(original))
                    {detail=$"element {pair.Key}, disease {id}, overpopulation {over}: logical/native tag base {expected:R}/{original:R}";return false;}
                    ++checkedRules;
                }
            }
            detail=$"{checkedRules} actual native tag rules match logical tags for every identity and both population branches";return checkedRules>0;
        }
        internal static PipeAmount StoredCulturePopulation(Storage storage,int id)
        {
            if(!Hosts.TryGetValue(storage,out var host))throw new InvalidOperationException("No exact store for culture population.");
            foreach(var germ in host.Store.Inventory.Germs)if(germ.Id==id)return germ.Amount;return PipeAmount.Zero;
        }
        internal static bool StoredBiologyHasPhysicalWork(Storage storage)=>Hosts.TryGetValue(storage,out var host)&&
            (host.Store.Births.Values.Any(value=>value.Sign>0)||host.Store.Deaths.Values.Any(value=>value.Sign>0));
        internal static object StoredBiologyReport(Storage storage)
        {
            if(!Hosts.TryGetValue(storage,out var host))throw new InvalidOperationException("No exact store for biology report.");
            return new{Mass=host.Store.Inventory.Mass,Populations=host.Store.Inventory.Germs.ToArray().Select(germ=>new{germ.Id,Count=germ.Mass}).ToArray(),
                Clocks=host.Store.Clocks.Select(pair=>new{Id=pair.Key,pair.Value.Expected,pair.Value.NativeResidue}).ToArray(),
                Births=host.Store.Births.Select(pair=>new{Id=pair.Key,Units=pair.Value.Units.ToString()}).ToArray(),Deaths=host.Store.Deaths.Select(pair=>new{Id=pair.Key,Units=pair.Value.Units.ToString()}).ToArray(),
                host.Store.Revision,Audit=host.Store.Audit()};
        }
        private delegate float NativeStoredDelta(DiseaseHeader header,ref DiseaseContainer payload,Klei.AI.Disease disease,float seconds,bool radiation);
        private delegate float NativeStoredSubtotal(int count,ushort element,float mass,int environmentCell,float temperature,float tagBase,Klei.AI.Disease disease,float seconds,bool radiation);
        private delegate void NativeStoredEvaluate(DiseaseContainers manager,DiseaseHeader header,ref DiseaseContainer payload);
        private static class StoredBiologyAccess
        {
            internal static readonly NativeStoredDelta Delta=(NativeStoredDelta)Delegate.CreateDelegate(typeof(NativeStoredDelta),AccessTools.Method(typeof(DiseaseContainers),"CalculateDelta",
                new[]{typeof(DiseaseHeader),typeof(DiseaseContainer).MakeByRefType(),typeof(Klei.AI.Disease),typeof(float),typeof(bool)}));
            internal static readonly NativeStoredSubtotal Subtotal=(NativeStoredSubtotal)Delegate.CreateDelegate(typeof(NativeStoredSubtotal),AccessTools.Method(typeof(DiseaseContainers),"CalculateDelta",
                new[]{typeof(int),typeof(ushort),typeof(float),typeof(int),typeof(float),typeof(float),typeof(Klei.AI.Disease),typeof(float),typeof(bool)}));
            internal static readonly NativeStoredEvaluate Evaluate=(NativeStoredEvaluate)Delegate.CreateDelegate(typeof(NativeStoredEvaluate),AccessTools.Method(typeof(DiseaseContainers),"EvaluateGrowthConstants"));
            internal static readonly Func<KPrefabID,HashSet<Tag>> Tags=(Func<KPrefabID,HashSet<Tag>>)Delegate.CreateDelegate(typeof(Func<KPrefabID,HashSet<Tag>>),AccessTools.PropertyGetter(typeof(KPrefabID),"Tags"));
            internal static readonly FieldInfo Headers=AccessTools.Field(typeof(DiseaseContainers),"headers"),Payloads=AccessTools.Field(typeof(DiseaseContainers),"payloads"),
                Modifiers=AccessTools.Field(typeof(Storage),"defaultStoredItemModifers");
        }
        private sealed class PreparedCulture
        {
            internal Host Host=null!;internal int Id;internal GermPopulationStep Step;internal StoredGermContext Context;
        }
        internal sealed class BiologyScope
        {
            private readonly IList headers;
            private readonly (int Row,DiseaseHeader Header)[] originals;
            private readonly PreparedCulture[] changes;
            private readonly (Host Host,PipeInventory Inventory)[] owners;
            private bool finished;
            private BiologyScope(IList headers,(int,DiseaseHeader)[] originals,PreparedCulture[] changes,(Host,PipeInventory)[] owners)
            {this.headers=headers;this.originals=originals;this.changes=changes;this.owners=owners;}
            internal void Finish(bool success)
            {
                if(finished)return;
                var missing=false;
                foreach(var original in originals)
                {
                    var row=original.Row;
                    if(row>=headers.Count||!ReferenceEquals(((DiseaseHeader)headers[row]).primaryElement,original.Header.primaryElement))
                    {
                        row=-1;
                        for(var i=0;i<headers.Count;++i)
                            if(ReferenceEquals(((DiseaseHeader)headers[i]).primaryElement,original.Header.primaryElement))
                            {if(row>=0)throw new InvalidOperationException("A stored projection has duplicate live native rows during restoration.");row=i;}
                    }
                    if(row<0){missing=true;continue;}
                    headers[row]=original.Header;
                }
                finished=true;
                if(missing)throw new InvalidOperationException("A stored projection lost its native culture row during the original update.");
                if(!success)return;
                foreach(var owner in owners)
                    if(!ReferenceEquals(owner.Host.Store.Inventory,owner.Inventory))throw new InvalidOperationException("Owned storage changed during its biology preparation.");
                foreach(var change in changes)change.Host.Store.CommitCulture(change.Id,change.Step,change.Context);
                foreach(var owner in owners)Refresh(owner.Host);
                ++StoredBiologyTicks;
            }
            internal static BiologyScope Prepare(DiseaseContainers manager,float seconds)
            {
                Published();
                InitializeStoredCultureContexts(manager);
                var headers=(IList)StoredBiologyAccess.Headers.GetValue(manager);var payloads=(IList)StoredBiologyAccess.Payloads.GetValue(manager);
                var originals=new List<(int,DiseaseHeader)>();var native=new Dictionary<PrimaryElement,(DiseaseHeader,DiseaseContainer)>();
                for(var row=0;row<headers.Count;++row)
                {
                    var header=(DiseaseHeader)headers[row];
                    if(header.primaryElement==null||!IsProjection(header.primaryElement))continue;
                    if(native.ContainsKey(header.primaryElement))throw new InvalidOperationException("An owned storage projection has duplicate native cultures.");
                    native.Add(header.primaryElement,(header,(DiseaseContainer)payloads[row]));originals.Add((row,header));
                }
                var environments=CaptureStoredEnvironments();
                var owners=Hosts.Values.Select(host=>(host,host.Store.Inventory)).ToArray();var changes=new List<PreparedCulture>();
                foreach(var host in Hosts.Values)
                {
                    var inventory=host.Store.Inventory;
                    foreach(var germ in inventory.Germs)
                    {
                        if(germ.Id<0||germ.Id>=Db.Get().Diseases.Count)throw new InvalidOperationException("A physical stored germ identity has no disease rule.");
                        var definition=Db.Get().Diseases[(byte)germ.Id];
                        var clock=host.Store.Clocks.TryGetValue(germ.Id,out var savedClock)?savedClock:new GermPopulationClock(false,0);
                        var hasContext=host.Store.CultureContexts.TryGetValue(germ.Id,out var context);
                        var integer=germ.Mass<=int.MaxValue&&germ.Mass==Math.Floor(germ.Mass)&&germ.Amount.Minus(new PipeAmount(germ.Mass)).Sign==0;
                        var single=inventory.Count==1;
                        Projection? view=null;
                        if(single)host.Projections.TryGetValue(inventory.Parts[0].Id,out view);
                        if(!hasContext)throw new InvalidOperationException("A stored culture was not initialized before its positive tick.");
                        var temperature=(float)inventory.Temperature(ColonyConduits.Material);
                        var captured=hasContext&&context.NativeSingle&&single&&inventory.Germs.Length==1?(StoredGermContext?)context:null;
                        var nativeMass=(float)inventory.Mass;
                        if(!clock.Expected&&integer&&nativeMass>0&&inventory.Total.Minus(new PipeAmount(nativeMass)).Sign==0)
                        {
                            var delta=NativeStorageChange(host,(int)germ.Mass,definition,temperature,seconds,captured,environments[Grid.PosToCell(host.Storage)]);
                            if(!float.IsNaN(delta)&&!float.IsInfinity(delta)&&ColonyStorageGermRounding.TryStep((int)germ.Mass,clock.NativeResidue,delta,out var next))
                            {
                                var afterContext=captured??new StoredGermContext((ushort)inventory.Dominant,0,1,false);
                                if(captured.HasValue&&view!=null)
                                {
                                    var header=new DiseaseHeader{primaryElement=view.Item,diseaseIdx=(byte)germ.Id,diseaseCount=(int)germ.Mass};
                                    var payload=new DiseaseContainer{elemIdx=context.Element,overpopulationCount=context.Overpopulation,instanceGrowthRate=context.TagGrowthBase};
                                    if((header.diseaseCount>payload.overpopulationCount)!=(next.Population>payload.overpopulationCount))StoredBiologyAccess.Evaluate(manager,header,ref payload);
                                    afterContext=new StoredGermContext(payload.elemIdx,payload.overpopulationCount,payload.instanceGrowthRate,true);
                                }
                                var step=new GermPopulationStep(new PipeAmount(next.Population),new PipeAmount(next.Change),new GermPopulationClock(false,next.Rounding));
                                changes.Add(new PreparedCulture{Host=host,Id=germ.Id,Step=step,Context=afterContext});continue;
                            }
                        }
                        // Fractional/wide cultures keep expected-population mode
                        // even if a later rounded value happens to be integral.
                        var expected=ExpectedStorageChange(host,germ.Amount,definition,temperature,seconds,null,environments[Grid.PosToCell(host.Storage)])
                            .Plus(new PipeAmount(clock.NativeResidue));
                        if(expected.Sign<0&&germ.Amount.Plus(expected).Sign<=0)expected=germ.Amount.Negated();
                        var population=germ.Amount.Plus(expected);
                        if(population.Sign<0)throw new InvalidOperationException("Stored germ evolution overdraws its exact population.");
                        changes.Add(new PreparedCulture{Host=host,Id=germ.Id,Step=new GermPopulationStep(population,expected,new GermPopulationClock(true,0)),
                            Context=new StoredGermContext((ushort)inventory.Dominant,0,1,false)});
                    }
                }
                // Preflight every exact journal and revision before masking
                // native headers; a late invalid culture cannot leave an
                // earlier culture committed on the physical path.
                foreach(var group in changes.GroupBy(change=>change.Host))
                {
                    if((ulong)group.Count()>ulong.MaxValue-group.Key.Store.Revision)throw new InvalidOperationException("Owned storage biology revision overflow.");
                    foreach(var change in group)
                    {
                        if(GermPopulationStepMismatch(group.Key.Store.Inventory,change))throw new InvalidOperationException("Prepared stored culture does not match its physical population.");
                        _=group.Key.Store.Inventory.WithDiseaseDelta(change.Id,change.Step.Change);
                        var journal=change.Step.Change.Sign>0?group.Key.Store.Births:group.Key.Store.Deaths;
                        journal.TryGetValue(change.Id,out var before);_=before.Plus(change.Step.Change.Sign>0?change.Step.Change:change.Step.Change.Negated());
                    }
                }
                // Mutation starts only after every physical change is ready.
                foreach(var original in originals){var masked=original.Item2;masked.diseaseIdx=byte.MaxValue;headers[original.Item1]=masked;}
                return new BiologyScope(headers,originals.ToArray(),changes.ToArray(),owners);
            }
        }
        private static bool GermPopulationStepMismatch(PipeInventory inventory,PreparedCulture change)
        {
            var before=PipeAmount.Zero;foreach(var germ in inventory.Germs)if(germ.Id==change.Id){before=germ.Amount;break;}
            return change.Step.Population.Sign<0||before.Plus(change.Step.Change).Minus(change.Step.Population).Sign!=0;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct StoredEnvironmentRow{internal uint Cell,First,Count,Flags;internal double Temperature;}
        [StructLayout(LayoutKind.Sequential)]
        private struct StoredEnvironmentPart{internal uint Element,Reserved;internal double Mass;}
        private sealed class StoredEnvironment
        {internal int Cell;internal uint[] Elements=Array.Empty<uint>();internal double[] Masses=Array.Empty<double>();internal double Total,Maximum,Normalized;}
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_colony_sample",CallingConvention=CallingConvention.Cdecl)]
        private static extern int SampleStoredEnvironment([In]uint[] cells,uint count,out OniNativeColony.Status source,[Out]StoredEnvironmentRow[] rows,[Out]StoredEnvironmentPart[] parts,uint capacity,out uint required,out double booked);
        private static Dictionary<int,StoredEnvironment> CaptureStoredEnvironments()
        {
            var cells=Hosts.Values.Select(host=>Grid.PosToCell(host.Storage)).Distinct().OrderBy(cell=>cell).ToArray();
            if(cells.Length==0)return new Dictionary<int,StoredEnvironment>();
            for(var attempt=0;attempt<8;++attempt)
            {
                var result=new Dictionary<int,StoredEnvironment>();ulong session=0,frame=0;var retry=false;
                var status=OniNativeColony.Status.Empty;OniNativeColony.Check(OniNativeColony.CopyStatus(ref status));
                for(var start=0;start<cells.Length;start+=4096)
                {
                    var requested=cells.Skip(start).Take(Math.Min(4096,cells.Length-start)).Select(cell=>(uint)cell).ToArray();
                    var rows=new StoredEnvironmentRow[requested.Length];var parts=new StoredEnvironmentPart[checked(requested.Length*(int)status.Species)];
                    OniNativeColony.Check(SampleStoredEnvironment(requested,(uint)requested.Length,out var source,rows,parts,(uint)parts.Length,out var count,out _));
                    if(source.State!=1||source.Session!=status.Session)throw new InvalidOperationException("Stored biology world snapshot is unavailable.");
                    if(start==0){session=source.Session;frame=source.CompletedFrames;}
                    else if(source.Session!=session||source.CompletedFrames!=frame){retry=true;break;}
                    foreach(var row in rows)
                    {
                        if(row.First+row.Count>count)throw new InvalidOperationException("Stored biology environment rows leave their snapshot.");
                        var environment=new StoredEnvironment{Cell=(int)row.Cell,Elements=new uint[row.Count],Masses=new double[row.Count]};
                        for(var i=0;i<row.Count;++i)
                        {var part=parts[row.First+i];if(!(part.Mass>0)||double.IsInfinity(part.Mass))throw new InvalidOperationException("Invalid stored biology environment mass.");environment.Elements[i]=part.Element;environment.Masses[i]=part.Mass;environment.Total+=part.Mass;environment.Maximum=Math.Max(environment.Maximum,part.Mass);}
                        if(environment.Maximum>0)foreach(var mass in environment.Masses)environment.Normalized+=mass/environment.Maximum;
                        result.Add(environment.Cell,environment);
                    }
                }
                if(!retry)return result;
            }
            throw new InvalidOperationException("Stored biology could not capture one completed world epoch for all containers.");
        }
        private static PipeAmount EnvironmentWeight(PipeAmount quantity,StoredEnvironment environment,int index)=>
            double.IsInfinity(environment.Total)?quantity.WeightedPopulation(environment.Masses[index],environment.Maximum).WeightedPopulation(1,environment.Normalized):
                quantity.WeightedPopulation(environment.Masses[index],environment.Total);
        private static float StoredNativeExposure(int count,Klei.AI.Disease definition,float seconds,StoredEnvironment environment)
        {
            if(environment.Elements.Length==0)
            {var element=Grid.Solid[environment.Cell]?Grid.Element[environment.Cell].idx:ElementLoader.FindElementByHash(SimHashes.Vacuum).idx;return definition.elemExposureInfo[element].CalculateExposureDiseaseCountDelta(count,seconds);}
            var change=PipeAmount.Zero;
            for(var i=0;i<environment.Elements.Length;++i)
                change=change.Plus(EnvironmentWeight(new PipeAmount(definition.elemExposureInfo[environment.Elements[i]].CalculateExposureDiseaseCountDelta(count,seconds)),environment,i));
            return(float)change.Value;
        }
        private static float NativeStorageChange(Host host,int count,Klei.AI.Disease definition,float temperature,float seconds,StoredGermContext? captured,StoredEnvironment environment)
        {
            var inventory=host.Store.Inventory;var mass=(float)inventory.Mass;float change;
            if(captured.HasValue)
                change=StoredBiologyAccess.Subtotal(count,captured.Value.Element,mass,-1,temperature,captured.Value.TagGrowthBase,definition,seconds,false);
            else
            {
                var material=PipeAmount.Zero;var tags=PipeAmount.Zero;
                foreach(var part in inventory.Parts)
                {
                    var growth=definition.elemGrowthInfo[part.Id];
                    material=material.Plus(WeightedStored(new PipeAmount(growth.CalculateDiseaseCountDelta(count,mass,seconds)),part.Mass,inventory.Mass));
                    var over=count>growth.maxCountPerKG*mass;var tagBase=definition.GetGrowthRateForTags(StoredTags(host,part.Id),over);
                    var tagDelta=(float)count*UnityEngine.Mathf.Pow(tagBase,seconds)-(float)count;
                    tags=tags.Plus(WeightedStored(new PipeAmount(tagDelta),part.Mass,inventory.Mass));
                }
                change=(float)material.Value;
                var factor=Klei.AI.Disease.HalfLifeToGrowthRate(definition.CalculateTemperatureHalfLife(temperature),seconds);
                change+=(float)count*factor-(float)count;change+=(float)tags.Value;
            }
            change+=StoredNativeExposure(count,definition,seconds,environment);
            if(Sim.IsRadiationEnabled()&&Grid.Radiation[environment.Cell]>0)change-=Grid.Radiation[environment.Cell]*definition.radiationKillRate;
            return change;
        }
        private static PipeAmount ExpectedStoredMaterial(PipeAmount population,Klei.AI.DiseaseGrowthRules.ElemGrowthInfo growth,PipeAmount mass,float seconds)
        {
            if(float.IsNaN(growth.minCountPerKG)||float.IsNaN(growth.maxCountPerKG))throw new InvalidOperationException("Invalid stored disease density descriptor.");
            var below=growth.minCountPerKG>0&&(float.IsPositiveInfinity(growth.minCountPerKG)||population.Minus(mass.Times(growth.minCountPerKG)).Sign<0);
            if(below)return new PipeAmount(-growth.underPopulationDeathRate*seconds);
            var over=growth.maxCountPerKG<0||!float.IsPositiveInfinity(growth.maxCountPerKG)&&population.Minus(mass.Times(growth.maxCountPerKG)).Sign>0;
            var factor=Klei.AI.Disease.HalfLifeToGrowthRate(over?growth.overPopulationHalfLife:growth.populationHalfLife,seconds);
            return population.Times((double)factor-1);
        }
        private static PipeAmount WeightedStored(PipeAmount quantity,double mass,double total)
        {
            return quantity.WeightedPopulation(mass,total);
        }
        private static HashSet<Tag> StoredTags(Host host,int material)
        {
            var element=ElementLoader.elements[material];var prefab=Assets.GetPrefab(element.tag);
            if(prefab==null)throw new InvalidOperationException("A stored fluid has no native tag-service prefab.");
            var tags=new HashSet<Tag>(StoredBiologyAccess.Tags(prefab.GetComponent<KPrefabID>()));foreach(var tag in element.oreTags)tags.Add(tag);
            var modifiers=(List<Storage.StoredItemModifier>)StoredBiologyAccess.Modifiers.GetValue(host.Storage);
            if(modifiers.Contains(Storage.StoredItemModifier.Seal))tags.Add(GameTags.Sealed);
            if(modifiers.Contains(Storage.StoredItemModifier.Preserve))tags.Add(GameTags.Preserved);
            return tags;
        }
        private static PipeAmount ExpectedStorageChange(Host host,PipeAmount population,Klei.AI.Disease definition,float temperature,float seconds,StoredGermContext? captured,StoredEnvironment environment)
        {
            var inventory=host.Store.Inventory;var change=PipeAmount.Zero;
            foreach(var part in inventory.Parts)
            {
                var growth=definition.elemGrowthInfo[captured.HasValue?captured.Value.Element:part.Id];
                var material=ExpectedStoredMaterial(population,growth,inventory.Total,seconds);
                var over=float.IsNaN(growth.maxCountPerKG)||growth.maxCountPerKG<0||!float.IsPositiveInfinity(growth.maxCountPerKG)&&population.Minus(inventory.Total.Times(growth.maxCountPerKG)).Sign>0;
                var tagBase=captured.HasValue?captured.Value.TagGrowthBase:definition.GetGrowthRateForTags(StoredTags(host,part.Id),over);
                var tagFactor=UnityEngine.Mathf.Pow(tagBase,seconds);
                material=material.Plus(population.Times((double)tagFactor-1));
                change=change.Plus(WeightedStored(material,part.Mass,inventory.Mass));
            }
            var temperatureFactor=Klei.AI.Disease.HalfLifeToGrowthRate(definition.CalculateTemperatureHalfLife(temperature),seconds);
            change=change.Plus(population.Times((double)temperatureFactor-1));
            if(environment.Elements.Length>0)
                for(var i=0;i<environment.Elements.Length;++i)
                {
                    var factor=Klei.AI.Disease.HalfLifeToGrowthRate(definition.elemExposureInfo[environment.Elements[i]].populationHalfLife,seconds);
                    change=change.Plus(EnvironmentWeight(population.Times((double)factor-1),environment,i));
                }
            else
            {
                var element=Grid.Solid[environment.Cell]?Grid.Element[environment.Cell].idx:ElementLoader.FindElementByHash(SimHashes.Vacuum).idx;
                var factor=Klei.AI.Disease.HalfLifeToGrowthRate(definition.elemExposureInfo[element].populationHalfLife,seconds);
                change=change.Plus(population.Times((double)factor-1));
            }
            if(Sim.IsRadiationEnabled()&&Grid.Radiation[environment.Cell]>0)change=change.Minus(new PipeAmount(Grid.Radiation[environment.Cell]*definition.radiationKillRate));
            return change;
        }
        internal static BiologyScope? BeginBiology(DiseaseContainers manager,float seconds)
        {
            if(!Enabled||!storageBiologyEnabled||!(seconds>0)||!ReferenceEquals(manager,GameComps.DiseaseContainers))return null;
            return BiologyScope.Prepare(manager,seconds);
        }
    }
    [HarmonyPatch(typeof(DiseaseContainers),"Sim200ms")]
    internal static class ColonyOwnedStorageBiologyPatch
    {
        private static bool Prefix(DiseaseContainers __instance,float __0,ref ColonyOwnedStorage.BiologyScope? __state)
        {
            try{__state=ColonyOwnedStorage.BeginBiology(__instance,__0);return true;}
            catch(Exception error){ColonyLoadRefusal.Refuse(error);return false;}
        }
        private static void Postfix(ColonyOwnedStorage.BiologyScope? __state)
        {try{__state?.Finish(true);}catch(Exception error){ColonyLoadRefusal.Refuse(error);}}
        private static Exception? Finalizer(Exception? __exception,ColonyOwnedStorage.BiologyScope? __state)
        {
            try{__state?.Finish(false);}catch(Exception error){ColonyLoadRefusal.Refuse(error);if(__exception==null)return error;}
            if(__state!=null&&__exception!=null)ColonyLoadRefusal.Refuse(__exception);
            return __exception;
        }
    }
}
