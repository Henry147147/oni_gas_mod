using System;
using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using UnityEngine;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static NativeActorCheckpointImage? restoredActorImage,lastSavedActorImage,currentActorImage;
        private static string currentActorImageHash="";
        private static int restoredZeroTimeEmitterConfigurations;
        private static bool persistentFixtureStarted,persistentFixtureVerified,restoredActorBindingsVerified,restoredActorsVerified;
        private static readonly List<GameObject> persistentFixtureObjects=new List<GameObject>();
        private static bool PersistentActorValidationRequired=>ColonyRuntimeValidation.Settings?.VerifyPersistentActors==true;
        private static IEnumerable<KeyValuePair<(uint kind,int handle),ActorBinding>> PersistentBindings=>
            actorBindings.Where(pair=>pair.Value.Configured && !pair.Value.Removed && pair.Value.PersistentOwner!=0);

        private static void ObservePersistentConsumer(ActorBinding binding)
        {
            if(binding.PersistentObserverAttached || binding.PersistentOwner==0 || !(binding.Owner is ElementConsumer consumer)) return;
            consumer.OnElementConsumed+=sample=>
            {
                if(binding.Removed) throw new InvalidOperationException("A retired persistent consumer received mass.");
                ++binding.PersistentConsumerCallbacks;binding.PersistentConsumerMass+=sample.mass;
            };
            binding.PersistentObserverAttached=true;
        }
        private static void RecordPersistentReceipt(ActorBinding binding,Receipt receipt)
        {
            if(binding.PersistentOwner==0 || receipt.Kind!=3 || receipt.MassKg<=0) return;
            ++binding.PersistentExpectedCallbacks;binding.PersistentExpectedMass+=receipt.MassKg;
        }
        private static void StartPersistentActorFixture()
        {
            if(persistentFixtureStarted) {ObservePersistentEmitterStatus();return;}
            Require(PersistentBindings.Count()==0,"Persistent actor fixture requires a fresh owner.");
            for(var role=0;role<3;++role)
            {
                var prefab=Assets.GetPrefab(new Tag(PersistentFluidActorConfig.Id(Liquid,role)));
                Require(prefab!=null,"Persistent actor validation prefab was not registered.");
                var instance=Util.KInstantiate(prefab,Grid.CellToPosCCC(target+role,Grid.SceneLayer.Ore));
                persistentFixtureObjects.Add(instance);instance.SetActive(true);
            }
            persistentFixtureStarted=true;
        }
        private static void ObservePersistentEmitterStatus()
        {
            if(Game.Instance==null || currentSnapshot==null || Game.Instance.simData.emittedMassEntries==null ||
                Game.Instance.simData.emittedMassEntries!=currentSnapshot.Pointer->emittedMassEntries) return;
            foreach(var pair in PersistentBindings)
            {
                if(pair.Key.kind!=2) continue;
                var index=pair.Key.handle&0x00ffffff;
                if(index>=currentSnapshot.Pointer->numEmittedMassEntries) continue;
                if(Game.Instance.simData.emittedMassEntries[index].mass>0) pair.Value.PersistentEmitterStatusObserved=true;
            }
        }
        private static void VerifyPersistentActorFixture()
        {
            ObservePersistentEmitterStatus();var bindings=PersistentBindings.ToArray();
            Require(persistentFixtureStarted && persistentFixtureObjects.Count==3 && bindings.Length==3 && actorStatus.LiveActors==3 && PendingActorRestores()==0,
                "The persistent source fixture did not retain all three real registered actors.");
            VerifyPersistentCallbacks(bindings);
            Require(bindings.Count(pair=>pair.Key.kind==1)==1 && bindings.Where(pair=>pair.Key.kind==1).All(pair=>pair.Value.PersistentConsumerCallbacks>10) &&
                bindings.Where(pair=>pair.Key.kind==2).Any(pair=>pair.Value.PersistentEmitterStatusObserved) &&
                bindings.Where(pair=>pair.Key.kind==2).Any(pair=>pair.Value.Owner is ElementEmitter emitter && emitter.isEmitterBlocked),
                "Persistent source consumer, native emission slot or blocked emitter was not exercised.");
            Require(TryCapturePhysicalCheckpoint(out var image),"Live persistent actors did not produce a drained complete snapshot.");
            var saved=NativeActorCheckpointImage.Parse(image);
            Require(saved.Actors.Count==3 && (NaturalAutosaveRequired || saved.Actors.Any(a=>a.Kind==1 && a.Remainder>0)) && saved.Actors.Any(a=>a.Kind==2 && a.Elapsed>0) &&
                saved.Actors.Any(a=>a.Kind==2 && a.Blocked),"Source snapshot omitted actor timing, consumer remainder or blocked state.");
            currentActorImage=saved;currentActorImageHash=CheckpointHash(image);
            persistentFixtureVerified=true;
        }
        private static void VerifyPersistentCallbacks(KeyValuePair<(uint kind,int handle),ActorBinding>[] bindings)
        {
            foreach(var pair in bindings)
            {
                var b=pair.Value;
                Require(b.Owner!=null && b.PersistentOwner>0 && b.PersistentPrefab!=0,"Persistent actor has no live saved object.");
                if(pair.Key.kind==1) Require(b.PersistentObserverAttached && b.PersistentConsumerCallbacks==b.PersistentExpectedCallbacks &&
                    b.PersistentConsumerMass==b.PersistentExpectedMass,"Persistent consumer callbacks lost, repeated or changed an admitted parcel.");
            }
        }
        private static void VerifyRestoredPersistentActors()
        {
            if(restoredActorBindingsVerified || restoredActorImage==null) return;
            Require(PendingActorRestores()==0,"Saved actors are still waiting for their real component registrations.");
            var bindings=PersistentBindings.ToArray();
            Require(bindings.Length==restoredActorImage.Actors.Count && actorStatus.LiveActors==bindings.Length,
                "Restored scene actor count differs from its saved snapshot.");
            foreach(var a in restoredActorImage.Actors)
            {
                var candidates=bindings.Where(pair=>pair.Key.kind==a.Kind && pair.Value.PersistentOwner==a.Owner &&
                    pair.Value.PersistentPrefab==a.Prefab && pair.Value.PersistentComponent==a.Component).ToArray();
                Require(candidates.Length==1,"Saved actor identity has no unique restored component.");
                var b=candidates[0].Value;
                Require(b.Cell==a.Cell && b.Species==a.Species && b.Owner!=null &&
                    (int)AccessTools.Field(typeof(SimComponent),"simHandle").GetValue(b.Owner)==candidates[0].Key.handle,
                    "Restored persistent actor is bound to the wrong live native handle.");
                // ONI recreates this nonserialized logical property on spawn.
                // Restore it without emitting another blocked/unblocked edge.
                if(b.Owner is ElementEmitter emitter)
                    AccessTools.PropertySetter(typeof(ElementEmitter),nameof(ElementEmitter.isEmitterBlocked)).Invoke(emitter,new object[]{a.Blocked});
            }
            Require(latest.CompletedFrames==0 && TryCapturePhysicalCheckpoint(out var image) && CheckpointHash(image)==loadedSourceHash,
                "Binding restored actors advanced time or changed saved timing/configuration.");
            restoredActorBindingsVerified=true;
        }
        private static void FlushRestoredEmitterConfigurations()
        {
            if(restoredActorImage==null || PendingActorRestores()==0) return;
            // Unlike ElementConsumer.OnSimRegistered, ElementEmitter leaves its
            // first configuration to SimComponent's dirty-state Sim200ms pass.
            // The initial pause intentionally withholds managed sim subticks.
            // Flush only this real component's configuration, with zero time,
            // after ONI has delivered its genuine registration callback.
            foreach(var pair in actorBindings.ToArray())
            {
                var b=pair.Value;
                if(pair.Key.kind!=2 || b.Configured || b.Removed || !(b.Owner is ElementEmitter emitter) ||
                    (int)AccessTools.Field(typeof(SimComponent),"simHandle").GetValue(emitter)!=pair.Key.handle) continue;
                var command=new ActorCommand {Handle=pair.Key.handle,Kind=2};
                if(!PersistentCommand(command,b,out var identity)) continue;
                var saved=restoredActorImage.Actors.SingleOrDefault(a=>a.Kind==2 && a.Owner==identity.Owner &&
                    a.Prefab==identity.Prefab && a.Component==identity.Component);
                if(saved==null) continue;
                var offset=emitter.outputElement.outputElementOffset;
                var cell=Grid.OffsetCell(Grid.PosToCell(emitter.transform.position),(int)offset.x,(int)offset.y);
                Require(cell==saved.Cell,"A restored emitter moved outside its saved point boundary.");
                var before=PendingActorRestores();var frame=latest.CompletedFrames;var time=latest.TimeSeconds;
                emitter.Sim200ms(0f);
                Require(b.Configured && PendingActorRestores()+1==before && latest.CompletedFrames==frame && latest.TimeSeconds==time,
                    "The restored emitter's real zero-time configuration did not match its saved state.");
                ++restoredZeroTimeEmitterConfigurations;
            }
        }
        private static void VerifyRestoredPersistentActorCompletion()
        {
            if(restoredActorImage==null) return;
            ObservePersistentEmitterStatus();var bindings=PersistentBindings.ToArray();VerifyPersistentCallbacks(bindings);
            Require(restoredActorBindingsVerified && PendingActorRestores()==0 && bindings.Length==restoredActorImage.Actors.Count &&
                actorStatus.LiveActors==bindings.Length && exactReplayComparisons>0 &&
                bindings.Where(pair=>pair.Key.kind==1).All(pair=>pair.Value.PersistentConsumerCallbacks>0),
                "Restored actors did not resume through the real game callbacks and exact physical comparison.");
            Require(TryCapturePhysicalCheckpoint(out var image) && CheckpointHash(image)==CheckpointHash(restoredExpected),
                "Restored actor completion has no matching complete physical snapshot.");
            currentActorImage=NativeActorCheckpointImage.Parse(image);currentActorImageHash=CheckpointHash(image);
            restoredActorsVerified=true;
        }
        private static void RecordSavedPersistentActors(byte[] image)
        {
            lastSavedActorImage=NativeActorCheckpointImage.HasMagic(image) ? NativeActorCheckpointImage.Parse(image) : null;
            currentActorImage=lastSavedActorImage;currentActorImageHash=lastSavedActorImage==null ? "" : CheckpointHash(image);
        }
        private static object[]? ActorFacts(IReadOnlyList<NativeActorCheckpointImage.Actor>? records)=>records?.Select(a=>(object)new {
            a.Owner,a.Prefab,a.Component,a.Kind,a.Cell,a.Species,a.Mass,a.Interval,a.Temperature,a.MaximumDensity,a.Elapsed,a.Remainder,a.Blocked}).ToArray();
        private static object PersistentActorsReport()
        {
            var records=lastSavedActorImage?.Actors ?? restoredActorImage?.Actors;
            return new {
                Required=PersistentActorValidationRequired || records!=null,
                Source="stable-prefab-object-and-component/native-handle-remap",
                SourceFixtureRequired=PersistentActorValidationRequired,SourceFixtureVerified=persistentFixtureVerified,
                Restored=restoredActorImage!=null,RestoredBindingsVerified=restoredActorBindingsVerified,RestoredContinuationVerified=restoredActorsVerified,
                ZeroTimeEmitterConfigurations=restoredZeroTimeEmitterConfigurations,
                PendingRestores=active ? PendingActorRestores() : 0,Native=actorStatus,
                SavedActors=ActorFacts(records),CurrentActors=ActorFacts(currentActorImage?.Actors),CurrentImageSha256=currentActorImageHash,
                BoundObjects=PersistentBindings.Select(pair=>new {pair.Value.PersistentOwner,pair.Value.PersistentPrefab,pair.Value.PersistentComponent,
                    Kind=pair.Key.kind,Handle=pair.Key.handle,UnityInstance=pair.Value.Owner==null ? 0 : pair.Value.Owner.GetInstanceID(),
                    pair.Value.Cell,pair.Value.Species,pair.Value.PersistentExpectedCallbacks,pair.Value.PersistentConsumerCallbacks,
                    pair.Value.PersistentExpectedMass,pair.Value.PersistentConsumerMass,pair.Value.PersistentEmitterStatusObserved,
                    EmitterBlocked=pair.Value.Owner is ElementEmitter emitter && emitter.isEmitterBlocked}).ToArray(),
                UnsavedActors=actorBindings.Count(pair=>pair.Value.Configured && !pair.Value.Removed && pair.Value.PersistentOwner==0),
                WholeColonyFluidAuthority=false
            };
        }
    }

    // Explicit development prefabs with the normal SaveLoadRoot/KPrefabID
    // lifecycle. They represent accounted point boundaries, not powered pumps,
    // metabolism or closed machine conversions. No menu recipe is registered.
    public sealed class PersistentFluidActorConfig : IMultiEntityConfig
    {
        internal static string Id(bool water,int role)=>"MixedAtmospheres.Persistent"+(water ? "Water" : "Gas")+role;
        public List<GameObject> CreatePrefabs()
        {
            var result=new List<GameObject>();if(!NativeSealedDomainValidation.IsMode) return result;
            foreach(var water in new[]{false,true}) for(var role=0;role<3;++role)
            {
                var root=EntityTemplates.CreateEntity(Id(water,role),"Persistent fluid boundary validation");
                root.AddOrGet<SaveLoadRoot>();var element=water ? SimHashes.Water : SimHashes.Oxygen;
                if(role==0)
                {
                    var consumer=root.AddOrGet<ElementConsumer>();consumer.elementToConsume=element;consumer.configuration=ElementConsumer.Configuration.Element;
                    consumer.consumptionRadius=0;consumer.consumptionRate=.05f;consumer.showInStatusPanel=false;consumer.showDescriptor=false;
                    consumer.ignoreActiveChanged=true;consumer.storeOnConsume=false;
                }
                else
                {
                    var emitter=root.AddOrGet<ElementEmitter>();emitter.emitRange=0;emitter.emissionFrequency=.7f;emitter.showDescriptor=false;
                    emitter.maxPressure=role==1 ? 10000 : .01f;
                    emitter.outputElement=new ElementConverter.OutputElement(role==1 ? .03125f : .015625f,element,300,outputElementOffsety:0);
                }
                result.Add(root);
            }
            return result;
        }
        public void OnPrefabInit(GameObject inst) { }
        public void OnSpawn(GameObject inst)
        {
            var consumer=inst.GetComponent<ElementConsumer>();if(consumer!=null) consumer.EnableConsumption(true);
            var emitter=inst.GetComponent<ElementEmitter>();if(emitter!=null) emitter.SetEmitting(true);
        }
    }
}
