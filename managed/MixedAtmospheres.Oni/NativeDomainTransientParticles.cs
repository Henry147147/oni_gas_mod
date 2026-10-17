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
        private sealed class TrackedCarbonParticle
        {
            internal readonly ulong Identity;
            internal readonly uint Origin;
            internal readonly CO2 Particle;
            internal TrackedCarbonParticle(ulong identity,uint origin,CO2 particle)
            {Identity=identity;Origin=origin;Particle=particle;}
        }
        private static readonly Dictionary<CO2,TrackedCarbonParticle> transientCarbon=new Dictionary<CO2,TrackedCarbonParticle>();
        private static ulong nextTransientIdentity=1;
        private static NativeTransientParticleImage? pendingTransientParticles,lastSavedTransientParticles;
        private static bool restoringTransientParticles,transientParticlesRestored;
        private static int transientParticleCaptures,transientParticleRestoredCount,transientParticleNativeRootChecks;
        private static string restoredTransientHash="";
        private static bool TransientParticleValidationRequired=>ColonyRuntimeValidation.Settings?.VerifyTransientParticles==true;
        private static bool SavedParticleSubnormal=>ColonyRuntimeValidation.Settings?.SubnormalCarbonParticle==true;
        private static float SavedParticleMass=>SavedParticleSubnormal ? NativeBinary32.FromBits(1) : .03125f;
        private static double SavedParticleMassKg=>NativeBinary32.ToDouble(SavedParticleMass);
        private const float SavedParticleTemperature=333.25f;
        private static CO2? savedParticleFixture;
        private static ulong savedParticleFixtureIdentity;
        private static int savedParticleDepositReceipts;
        private static Receipt? savedParticleDeposit;
        private static bool savedParticleSeeded,savedParticleDepositVerified;
        private static bool savedParticleRetired;
        private static bool savedParticleReuseObserved;
        private static int savedParticleRetirementFrame=-1;
        private static double savedParticleInitialCarbon;
        internal static bool HoldTransientRestorePause=>ColonyRestoreMode && colonyLoaded && pendingTransientParticles!=null && !transientParticlesRestored;

        private static void SeedPausedTransientParticle()
        {
            if(!TransientParticleValidationRequired) return;
            Require(!ColonyRestoreMode && !savedParticleSeeded && SpeedControlScreen.Instance.IsPaused,
                "Transient particle source requires one paused ordinary save.");
            savedParticleFixture=CO2Manager.instance.SpawnCO2(Grid.CellToPosCCC(Grid.XYToCell(x+1,y+1),Grid.SceneLayer.Front),
                SavedParticleMass,SavedParticleTemperature,true);
            Require(transientCarbon.TryGetValue(savedParticleFixture,out var tracked),"Original CO2 factory did not bind its source parcel.");
            savedParticleFixtureIdentity=tracked!.Identity;savedParticleSeeded=true;
        }
        private static void ObserveSavedParticleDeposit(Receipt receipt)
        {
            if(!TransientParticleValidationRequired || !ColonyRestoreMode || receipt.Kind!=1 || receipt.Callback!=-1 ||
                receipt.Species!=4 || receipt.MassKg!=SavedParticleMassKg || receipt.Temperature!=SavedParticleTemperature) return;
            Require(savedParticleDepositReceipts==0,"Restored CO2 parcel was deposited more than once.");
            ++savedParticleDepositReceipts;savedParticleDeposit=receipt;
        }
        private static void VerifySavedParticleDeposit()
        {
            if(!TransientParticleValidationRequired) return;
            Require(ColonyRestoreMode && transientParticlesRestored && savedParticleFixture!=null && savedParticleFixtureIdentity>0 &&
                savedParticleDepositReceipts==1 && savedParticleDeposit.HasValue && savedParticleRetired && savedParticleRetirementFrame>=0,
                "Saved native CO2 parcel did not restore, deposit and leave its original manager exactly once.");
            if(SavedParticleSubnormal) {
                var current=cells.Sum(cell=>cell.CarbonDioxide);
                Require(savedParticleInitialCarbon==0 && current>0 &&
                    Math.Abs(current-SavedParticleMassKg)<=128*2.2204460492503131e-16*(current+SavedParticleMassKg),
                    "Saved smallest-float CO2 parcel did not remain positive after original deposition and fluid transport.");
            }
            savedParticleDepositVerified=true;
        }

        internal static void ObserveTransientCarbonSpawn(CO2 particle)
        {
            if(savedParticleRetired && ReferenceEquals(particle,savedParticleFixture)) savedParticleReuseObserved=true;
            // A pooled component can represent many successive parcels. Remove
            // its previous incarnation even when this spawn is outside scope.
            RetireTrackedParticle(particle);
            if(!active || arming || restoringTransientParticles) return;
            var origin=Grid.PosToCell(particle);
            if(!OwnsPublicCell(origin)) return;
            if(transientCarbon.Count>=NativeTransientParticleImage.MaximumParticles) PruneTransientParticles();
            Require(transientCarbon.Count<NativeTransientParticleImage.MaximumParticles,"Tracked CO2 pool exceeds its bounded save capacity.");
            Require(nextTransientIdentity<ulong.MaxValue,"Transient CO2 identity range exhausted.");
            transientCarbon.Add(particle,new TrackedCarbonParticle(nextTransientIdentity++,checked((uint)origin),particle));
        }
        private static NativeTransientParticle ReadTransientParticle(TrackedCarbonParticle tracked)
        {
            var particle=tracked.Particle;
            // The game factory registers this pool only with CO2Manager. If
            // a future prefab gains a native save root, reject double ownership.
            Require(particle.GetComponent<SaveLoadRoot>()==null,"Native CO2 prefab now has its own save root; duplicate persistence is not supported.");
            ++transientParticleNativeRootChecks;
            var position=particle.transform.GetPosition();var velocity=particle.velocity;
            var animation=particle.GetComponent<KBatchedAnimController>() ?? throw new InvalidOperationException("Native CO2 animation component is missing.");
            return new NativeTransientParticle(tracked.Identity,tracked.Origin,animation.FlipX ? 1U : 0U,
                new[]{position.x,position.y,position.z,velocity.x,velocity.y,velocity.z,
                    particle.mass,particle.temperature,particle.lifetimeRemaining,animation.Rotation});
        }
        private static void PruneTransientParticles()
        {
            Require(transientCarbon.Count==0 || CO2Manager.instance!=null,"Tracked CO2 parcels lost their normal manager before saving.");
            var live=CO2Manager.instance==null ? new HashSet<CO2>() : new HashSet<CO2>(CarbonParticles());
            foreach(var dead in transientCarbon.Keys.Where(p=>p==null || !live.Contains(p)).ToArray()) RetireTrackedParticle(dead);
        }
        private static void RetireTrackedParticle(CO2 particle)
        {
            if(transientCarbon.TryGetValue(particle,out var tracked) && tracked.Identity==savedParticleFixtureIdentity &&
                savedParticleFixtureIdentity>0 && !savedParticleRetired) {
                savedParticleRetired=true;savedParticleRetirementFrame=UnityEngine.Time.frameCount;
            }
            transientCarbon.Remove(particle);
        }
        internal static void ObserveTransientParticleManagerUpdate()
        {if(active && !arming && transientCarbon.Count>0) PruneTransientParticles();}
        private static NativeTransientParticleImage CaptureTransientParticles(Configuration geometry)
        {
            PruneTransientParticles();
            var image=NativeTransientParticleImage.Create(transientCarbon.Values.Select(ReadTransientParticle).ToArray(),geometry);
            image.ValidateGrid(checked((uint)Grid.WidthInCells),checked((uint)Grid.HeightInCells));
            ++transientParticleCaptures;return image;
        }
        private static bool SavedTransientParticlesUnchanged(Configuration geometry)=>lastSavedTransientParticles!=null &&
            CaptureTransientParticles(geometry).CopyEncodedData().AsSpan().SequenceEqual(lastSavedTransientParticles.CopyEncodedData());
        private static bool RestoreTransientParticles()
        {
            if(transientParticlesRestored) return true;
            var image=pendingTransientParticles;
            if(image==null) {Require(!TransientParticleValidationRequired,"Selected particle test save has no transient parcel.");transientParticlesRestored=true;return true;}
            if(CO2Manager.instance==null) return false;
            Require(latest.CompletedFrames==0,"Transient CO2 restore occurred after positive physical time.");
            savedParticleInitialCarbon=cells.Sum(cell=>cell.CarbonDioxide);
            Require(transientCarbon.Count==0 && !CarbonParticles().Any(p=>OwnsPublicCell(Grid.PosToCell(p))),
                "A native CO2 parcel already occupies the restored domain before custom particle binding.");
            // Instantiate through the original pool so normal update, deposit,
            // animation and retirement callbacks retain their game ownership.
            restoringTransientParticles=true;
            try {
                foreach(var item in image.Particles) {
                    var particle=CO2Manager.instance.SpawnCO2(new Vector3(item.X,item.Y,item.Z),item.MassKg,item.Temperature,(item.Flags&1)!=0,item.Rotation);
                    Require(particle.GetComponent<SaveLoadRoot>()==null,"Restored native CO2 prefab has an unsupported save root.");
                    particle.transform.SetPosition(new Vector3(item.X,item.Y,item.Z));
                    particle.velocity=new Vector3(item.VelocityX,item.VelocityY,item.VelocityZ);
                    particle.lifetimeRemaining=item.LifetimeRemaining;
                    particle.GetComponent<KBatchedAnimController>().Rotation=item.Rotation;
                    transientCarbon.Add(particle,new TrackedCarbonParticle(item.Identity,item.OriginCell,particle));
                    nextTransientIdentity=Math.Max(nextTransientIdentity,item.Identity+1);
                    if(TransientParticleValidationRequired && BitConverter.SingleToInt32Bits(item.MassKg)==BitConverter.SingleToInt32Bits(SavedParticleMass) && item.Temperature==SavedParticleTemperature) {
                        Require(savedParticleFixture==null,"Selected particle test save has duplicate fixture parcels.");
                        savedParticleFixture=particle;savedParticleFixtureIdentity=item.Identity;
                    }
                }
                var actual=CaptureTransientParticles(replayGeometry);
                Require(actual.CopyEncodedData().AsSpan().SequenceEqual(image.CopyEncodedData()),"Native CO2 factory restore changed a saved parcel field.");
                transientParticleRestoredCount=image.Particles.Count;restoredTransientHash=CheckpointHash(actual.CopyEncodedData());
                transientParticlesRestored=true;return true;
            }
            finally {restoringTransientParticles=false;}
        }
        private static void ResetTransientParticles()
        {
            transientCarbon.Clear();nextTransientIdentity=1;pendingTransientParticles=lastSavedTransientParticles=null;
            restoringTransientParticles=transientParticlesRestored=false;
            transientParticleCaptures=transientParticleRestoredCount=transientParticleNativeRootChecks=0;restoredTransientHash="";
            savedParticleFixture=null;savedParticleFixtureIdentity=0;savedParticleDepositReceipts=0;savedParticleDeposit=null;
            savedParticleSeeded=savedParticleDepositVerified=false;
            savedParticleRetired=savedParticleReuseObserved=false;savedParticleRetirementFrame=-1;savedParticleInitialCarbon=0;
        }
        private static object TransientParticlesReport()=>new {
            Format="MACO2P01",Version=1,MaximumParticles=NativeTransientParticleImage.MaximumParticles,
            Source="original-CO2Manager.SpawnCO2-pool",Captures=transientParticleCaptures,
            SavedCount=lastSavedTransientParticles?.Particles.Count??0,
            SavedSha256=lastSavedTransientParticles==null ? "" : CheckpointHash(lastSavedTransientParticles.CopyEncodedData()),
            RestoreRequired=pendingTransientParticles!=null,Restored=transientParticlesRestored,RestoredCount=transientParticleRestoredCount,
            RestoredSha256=restoredTransientHash,NativeSaveRootChecks=transientParticleNativeRootChecks,
            IdentityOwner="mod-particle-incarnation",UsesOriginalGameFactory=true,WholeColonyParticlePersistence=false,
            Fixture=new {Required=TransientParticleValidationRequired,Seeded=savedParticleSeeded,Identity=savedParticleFixtureIdentity,
                MassKg=SavedParticleMassKg,TemperatureK=SavedParticleTemperature,SubnormalInput=SavedParticleSubnormal,
                InputMassBits=unchecked((uint)BitConverter.SingleToInt32Bits(SavedParticleMass)),DepositReceipts=savedParticleDepositReceipts,
                InitialCarbonDioxideMassKg=savedParticleInitialCarbon,CurrentCarbonDioxideMassKg=cells.Sum(cell=>cell.CarbonDioxide),
                ParticleRetired=savedParticleRetired,RetirementUnityFrame=savedParticleRetirementFrame,
                ParticleReusedAfterRetirement=savedParticleReuseObserved,
                ComponentCurrentlyActive=savedParticleRetired && savedParticleFixture!=null && CO2Manager.instance!=null && CarbonParticles().Contains(savedParticleFixture),
                Deposit=savedParticleDeposit,DepositVerified=savedParticleDepositVerified}};
    }

    [HarmonyPatch(typeof(CO2Manager),nameof(CO2Manager.SpawnCO2),new[]{typeof(Vector3),typeof(float),typeof(float),typeof(bool),typeof(float)})]
    internal static class NativeTransientCarbonSpawnPatch
    {
        private static void Postfix(CO2 __result)=>NativeSealedDomainValidation.ObserveTransientCarbonSpawn(__result);
    }
}
