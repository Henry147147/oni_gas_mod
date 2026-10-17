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
        private static bool LiveBreathingRequired=>ColonyRuntimeValidation.Settings?.VerifyLiveBreathing==true;
        internal static bool HoldLiveBreathingRestorePause=>LiveBreathingRequired && ColonyRestoreMode && colonyLoaded && liveBreather==null;
        private static OxygenBreather? liveBreather;
        private sealed class LiveBreathingRequest
        {
            internal TransferCommand Command;
            internal Receipt? Receipt;
        }
        private static readonly Dictionary<int,LiveBreathingRequest> liveBreathingPending=new Dictionary<int,LiveBreathingRequest>();
        private static readonly List<object> liveBreathingEvidence=new List<object>();
        private static int liveBreathingRequests,liveBreathingReceipts,liveBreathingCallbacks,liveBreathingPositiveCallbacks;
        private static double liveBreathingRequestedMass,liveBreathingDeliveredMass,liveBreathingCallbackMass;
        private static double liveBreathingInitialEnergy,liveBreathingInitialScale;
        private static double[] liveBreathingInitialMasses=Array.Empty<double>();
        private static ulong liveBreathingStartedFrame;
        private static bool liveBreathingTeleported,liveBreathingVerified;
        private static int liveBreathingActiveCallback=-1;
        private static bool IsLiveBreatherObject(GameObject value)=>LiveBreathingRequired && liveBreather!=null &&
            (value==liveBreather.gameObject || value.transform.IsChildOf(liveBreather.transform));
        private static bool BindLiveBreather(bool restoring)
        {
            if(liveBreather!=null) return true;
            var candidates=UnityEngine.Object.FindObjectsByType<OxygenBreather>(FindObjectsSortMode.None)
                .Where(b=>b.prefabID!=null && b.GetComponent<MinionIdentity>()!=null && b.GetCurrentGasProvider() is GasBreatherFromWorldProvider &&
                    b.ConsumptionRate>0 && !b.prefabID.HasTag(GameTags.Dead) && (!restoring || OwnsPublicCell(Grid.PosToCell(b))))
                .OrderBy(b=>b.ConsumptionRate).ThenBy(b=>b.GetInstanceID()).ToArray();
            if(restoring && candidates.Length==0) return false;
            Require(candidates.Length>0 && (!restoring || candidates.Length==1),"The live breathing scene requires one saved organic duplicant inside its owned room.");
            NativeLiveBreathingCallbackObservation.RequireInstalled();
            liveBreather=candidates[0];liveBreathingStartedFrame=latest.CompletedFrames;
            liveBreathingInitialEnergy=PhysicalEnergy;
            liveBreathingInitialScale=cells.Sum(c=>Math.Abs(c.InternalEnergy)+c.ResolvedKineticEnergy);
            liveBreathingInitialMasses=Enumerable.Range(0,Ids.Length).Select(k=>cells.Sum(c=>c.Mass(k))).ToArray();
            liveBreather.onBreathableGasConsumed+=OnLiveBreathingCallback;
            if(!restoring)
            {
                var nav=liveBreather.GetComponent<Navigator>() ?? throw new InvalidOperationException("Live breathing duplicant has no normal navigator.");
                nav.Stop();liveBreather.transform.position=Grid.CellToPosCBC(target+1,Grid.SceneLayer.Move);
                nav.SetCurrentNavType(NavType.Floor);nav.UpdateProbe(true);liveBreathingTeleported=true;
            }
            Require(OwnsPublicCell(Grid.PosToCell(liveBreather)),"Live breathing duplicant is outside the owned room.");return true;
        }
        private static void ObserveLiveBreathingRequests(TransferCommand[] commands)
        {
            if(!LiveBreathingRequired) return;
            foreach(var command in commands)
            {
                if(command.Kind!=2) continue;
                Require(liveBreather!=null && command.Species==1 && command.Flags==(1U|(3U<<8)) && command.Callback>=0 && command.MassKg>0,
                    "The live breathing scene received an unsupported consumption request.");
                var handle=new HandleVector<Game.ComplexCallbackInfo<Sim.MassConsumedCallback>>.Handle{index=command.Callback};
                var callback=Game.Instance.massConsumedCallbackManager.GetItem(handle);
                Require(liveBreather!=null && ReferenceEquals(callback.callbackData,liveBreather) && callback.debugInfo=="GasBreatherFromWorldProvider" &&
                    command.Species==1 && command.Flags==(1U|(3U<<8)) && command.Callback>=0 && command.MassKg>0,
                    "Owned breathing request is not the tracked duplicant's actual world-provider callback.");
                Require(!liveBreathingPending.ContainsKey(command.Callback),"The live breather reused an undelivered callback handle.");
                liveBreathingPending.Add(command.Callback,new LiveBreathingRequest{Command=command});
                ++liveBreathingRequests;liveBreathingRequestedMass+=command.MassKg;
            }
        }
        private static void ObserveLiveBreathingReceipt(Receipt receipt)
        {
            if(!LiveBreathingRequired || receipt.Kind!=2) return;
            Require(liveBreathingPending.TryGetValue(receipt.Callback,out var pending) && pending!=null && !pending.Receipt.HasValue,
                "Live breathing receipt has no unique accepted duplicant request.");
            var command=pending!.Command;
            Require(receipt.Species==1 && receipt.PublicCell==command.PublicCell && receipt.Session==command.Session &&
                receipt.CompletedFrame>command.ExpectedFrame && receipt.MassKg>=0 && receipt.MassKg<=command.MassKg,
                "Live breathing receipt differs from its accepted source request.");
            pending.Receipt=receipt;++liveBreathingReceipts;liveBreathingDeliveredMass+=receipt.MassKg;
        }
        private static void OnLiveBreathingCallback(SimHashes element,float mass,float temperature,byte disease,int diseaseCount)
        {
            try
            {
                if(liveBreathingActiveCallback<0) return;
                Require(liveBreathingPending.TryGetValue(liveBreathingActiveCallback,out var pending) && pending!=null && pending.Receipt.HasValue,
                    "Actual breather callback has no active published owned receipt.");
                var receipt=pending!.Receipt!.Value;var command=pending.Command;
                Require((float)receipt.MassKg==mass && (mass==0 ? temperature==0 : (float)receipt.Temperature==temperature) &&
                    element==SimHashes.Oxygen && disease==255 && diseaseCount==0,
                    "Actual duplicant callback differs from its published physical receipt.");
                liveBreathingPending.Remove(liveBreathingActiveCallback);liveBreathingActiveCallback=-1;
                ++liveBreathingCallbacks;if(mass>0) ++liveBreathingPositiveCallbacks;
                liveBreathingCallbackMass+=mass;
                liveBreathingEvidence.Add(new {Request=command,Receipt=receipt,CallbackMassKg=mass,CallbackTemperature=temperature,
                    Element=(int)element,Disease=disease,DiseaseCount=diseaseCount});
            }
            catch(Exception caught) {Fail(caught);}
        }
        internal static bool BeforeLiveBreathingCallback(Sim.MassConsumedCallback value,object data)
        {
            try
            {
                if(!LiveBreathingRequired || liveBreather==null || !ReferenceEquals(data,liveBreather) || !liveBreathingPending.ContainsKey(value.callbackIdx)) return false;
                Require(liveBreathingActiveCallback==-1 && liveBreathingPending[value.callbackIdx].Receipt.HasValue,
                    "Duplicant callback arrived before its native receipt or reentered another delivery.");
                liveBreathingActiveCallback=value.callbackIdx;return true;
            }
            catch(Exception caught) {Fail(caught);throw;}
        }
        internal static void AfterLiveBreathingCallback(bool tracked)
        {
            try {if(tracked) Require(liveBreathingActiveCallback==-1,"The original world-provider callback did not reach the actual breather event.");}
            catch(Exception caught) {Fail(caught);throw;}
        }
        private static bool LiveBreathingDrained=>liveBreather!=null && liveBreathingPositiveCallbacks>0 && liveBreathingPending.Count==0 &&
            transferStatus.PendingBatches==0 && transferStatus.UnacknowledgedReceipts==0;
        private static void VerifyLiveBreathing()
        {
            Require(LiveBreathingDrained && liveBreather!=null && OwnsPublicCell(Grid.PosToCell(liveBreather)) &&
                liveBreathingRequests==liveBreathingReceipts && liveBreathingReceipts==liveBreathingCallbacks &&
                liveBreathingDeliveredMass==liveBreathingCallbackMass && liveBreathingDeliveredMass>0 &&
                liveBreathingDeliveredMass<=liveBreathingRequestedMass && liveBreathingCallbacks>=(ColonyRestoreMode ? 1 : 4),
                "Actual duplicant requests, native receipts and original game callbacks do not form one drained positive consumption chain.");
            var expected=liveBreathingInitialEnergy+heatStatus.CommittedHeatJoules+transferStatus.MaterialEnergyChange+
                transferStatus.KineticEnergyChange+gravityStatus.CommittedBoundaryPotential;
            var scale=liveBreathingInitialScale+cells.Sum(c=>Math.Abs(c.InternalEnergy)+c.ResolvedKineticEnergy)+
                Math.Abs(heatStatus.CommittedHeatJoules)+Math.Abs(transferStatus.MaterialEnergyChange)+Math.Abs(transferStatus.KineticEnergyChange);
            Require(Math.Abs(PhysicalEnergy-expected)<=128*2.2204460492503131e-16*scale,"Live breathing failed the complete fluid U+K boundary account.");
            for(var k=0;k<Ids.Length;++k)
            {
                var current=cells.Sum(c=>c.Mass(k));var initial=liveBreathingInitialMasses[k];var delta=transferStatus.MassChangesKg[k];
                Require(Math.Abs(current-initial-delta)<=128*2.2204460492503131e-16*(Math.Abs(current)+Math.Abs(initial)+Math.Abs(delta)),
                    "Live breathing lost an unconsumed constituent or positive trace.");
            }
            liveBreathingVerified=true;
        }
        private static void ResetLiveBreathing()
        {
            if(liveBreather!=null) liveBreather.onBreathableGasConsumed-=OnLiveBreathingCallback;
            liveBreather=null;liveBreathingPending.Clear();liveBreathingEvidence.Clear();
            liveBreathingRequests=liveBreathingReceipts=liveBreathingCallbacks=liveBreathingPositiveCallbacks=0;
            liveBreathingRequestedMass=liveBreathingDeliveredMass=liveBreathingCallbackMass=liveBreathingInitialEnergy=liveBreathingInitialScale=0;
            liveBreathingInitialMasses=Array.Empty<double>();liveBreathingStartedFrame=0;liveBreathingTeleported=liveBreathingVerified=false;
            liveBreathingActiveCallback=-1;
        }
        private static object LiveBreathingReport()=>new
        {
            Required=LiveBreathingRequired,Verified=liveBreathingVerified,Source="actual-OxygenBreather-Sim200ms-world-provider",
            OxygenElementId=(int)SimHashes.Oxygen,
            StartedFrame=liveBreathingStartedFrame,SourcePlacement=liveBreathingTeleported,RestoredWithoutTeleport=ColonyRestoreMode && liveBreather!=null && !liveBreathingTeleported,
            BreatherInstance=liveBreather==null ? 0 : liveBreather.GetInstanceID(),Requests=liveBreathingRequests,Receipts=liveBreathingReceipts,
            Callbacks=liveBreathingCallbacks,PositiveCallbacks=liveBreathingPositiveCallbacks,Outstanding=liveBreathingPending.Count,
            RequestedMassKg=liveBreathingRequestedMass,DeliveredMassKg=liveBreathingDeliveredMass,CallbackMassKg=liveBreathingCallbackMass,
            Evidence=liveBreathingEvidence.ToArray(),GameClockUnchanged=true,ArtificialBreathingTicks=0,CompleteMetabolismVerified=false
        };
    }
    [HarmonyPatch(typeof(GasBreatherFromWorldProvider),"OnSimConsumeCallback")]
    internal static class NativeLiveBreathingCallbackObservation
    {
        internal static void RequireInstalled()
        {
            var info=Harmony.GetPatchInfo(AccessTools.Method(typeof(GasBreatherFromWorldProvider),"OnSimConsumeCallback"));
            if(info?.Prefixes.Any(p=>p.PatchMethod==AccessTools.Method(typeof(NativeLiveBreathingCallbackObservation),nameof(Prefix)))!=true ||
                info?.Postfixes.Any(p=>p.PatchMethod==AccessTools.Method(typeof(NativeLiveBreathingCallbackObservation),nameof(Postfix)))!=true)
                throw new InvalidOperationException("The actual world-provider callback observer is not installed.");
        }
        private static void Prefix(Sim.MassConsumedCallback mass_cb_info,object data,ref bool __state)
        {__state=NativeSealedDomainValidation.BeforeLiveBreathingCallback(mass_cb_info,data);}
        private static void Postfix(bool __state)=>NativeSealedDomainValidation.AfterLiveBreathingCallback(__state);
    }
}
