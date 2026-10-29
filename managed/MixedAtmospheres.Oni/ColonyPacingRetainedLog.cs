using System;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal readonly struct ColonyRetainState
    {
        internal readonly ColonyRetainedToken Token;
        internal readonly ulong Session;
        internal ColonyRetainState(ColonyRetainedToken token,ulong session){Token=token;Session=session;}
    }
    internal static class ColonyPacingRetainedLog
    {
        internal static readonly bool Enabled=Environment.GetEnvironmentVariable("MA_COLONY_PACING_CREDIT")!="0";
        private static readonly bool Diagnostics=Environment.GetEnvironmentVariable("MA_COLONY_PACING_CREDIT_LOG")=="1";
        private static readonly ColonyPacingRetained counter=new ColonyPacingRetained(2,1f/30f);
        private static ulong session,bucket,firstFrame,lastFrame;
        private static int speed=-1;
        private static bool window,resetAfterCall;
        private static double openingOwed;
        private static bool Observe()
        {
            var active=ColonyOwner.Active;
            var status=active?ColonyOwner.Status:default;
            var screen=SpeedControlScreen.Instance;
            var mode=screen==null?-1:screen.GetSpeed();
            if(!active||status.Session!=session)
            {
                counter.Reset();Emit();session=active?status.Session:0;speed=mode;window=false;
            }
            var eligible=active&&status.State==1&&screen!=null&&!screen.IsPaused&&
                !ColonyLoadRefusal.Refused&&!ColonyCallbackRequests.Pending&&!ColonyOwnerPausedResave.Holding&&!ColonyContinuationProbe.Holding;
            if(mode!=speed||!eligible)
            {
                if(counter.Depth==0)counter.Reset();else resetAfterCall=true;
                speed=mode;
            }
            return eligible;
        }
        internal static ColonyRetainState Begin(ref float dt)
        {
            var eligible=Observe();
            if(!ColonyOwner.Active)return default;
            var frame=ColonyOwner.Status.CompletedFrames+1;
            var nextBucket=(frame-1)/50;
            if(!window||(bucket!=nextBucket&&counter.Depth==0))
            {Emit();window=true;bucket=nextBucket;firstFrame=lastFrame=frame;openingOwed=counter.Owed;}
            firstFrame=Math.Min(firstFrame,frame);lastFrame=Math.Max(lastFrame,frame);
            // Zero-time callbacks/lifecycle calls are never repayment chances.
            if(!(dt>0)){if(counter.Depth==0)counter.Reset();eligible=false;}
            return new ColonyRetainState(counter.Prepare(ref dt,eligible),session);
        }
        internal static void Pacing(float before,float after,bool returned,bool busy)=>counter.Pacing(before,after,returned,busy);
        internal static void Admission(float dt,bool originalWillRun)=>counter.Admission(dt,originalWillRun);
        internal static void Complete(ColonyRetainState state,float dt,bool originalRan)
        {
            if(state.Token.Depth==0)return;
            if(!ColonyOwner.Active||ColonyOwner.Status.Session!=state.Session){counter.Reset();return;}
            var eligible=Observe();
            counter.Commit(state.Token,dt,originalRan,eligible);
            if(counter.Depth==0&&resetAfterCall){counter.Reset();resetAfterCall=false;}
        }
        internal static void Finish(ColonyRetainState state,float dt,bool originalRan,Exception? error)
        {
            if(error!=null){counter.Exception(state.Token);if(counter.Depth==0)resetAfterCall=false;}
            else Complete(state,dt,originalRan);
        }
        internal static void SaveBoundary()
        {if(!Enabled)return;if(counter.Depth==0)counter.Reset();else resetAfterCall=true;}
        internal static void AfterUpdate()
        {
            if(!Enabled)return;
            Observe();
            if(window&&counter.Depth==0&&ColonyOwner.Active&&ColonyOwner.Status.CompletedFrames>=(bucket+1)*50)
            {Emit();window=false;}
        }
        internal static void Reset(){if(!Enabled)return;counter.Reset();Emit();session=0;speed=-1;window=resetAfterCall=false;}
        private static void Emit()
        {
            var value=counter.Drain();
            if(value.Calls==0&&value.Resets==0&&value.Errors==0&&value.Exceptions==0)return;
            var balance=openingOwed+value.Offered+value.UnexpectedAdded-value.Admitted-counter.Owed-value.Dropped;
            if(value.Errors!=0||value.Exceptions!=0||value.Overflows!=0)
                Debug.LogError(FormattableString.Invariant($"[Mixed Atmospheres] retention accounting failed: faults {value.Errors} exceptions {value.Exceptions} overflow {value.Overflows}"));
            if(Diagnostics)Debug.Log(FormattableString.Invariant($"[Mixed Atmospheres] pacing retained session {session} from_frame {firstFrame} through_frame {lastFrame} bound_s {counter.OwedLimit:R} extra_cap_s {counter.ExtraLimit:R} calls {value.Calls} offered_s {value.Offered:R} admitted_s {value.Admitted:R} opening_owed_s {openingOwed:R} owed_s {counter.Owed:R} dropped_s {value.Dropped:R} ignored_s {value.IgnoredSeconds:R} reset_s {value.ResetSeconds:R} repaid_s {value.Repaid:R} max_owed_s {value.MaximumOwed:R} proposed_extra_s {value.ProposedExtra:R} repayment_offers {value.RepaymentOffers} unclipped_loans {value.UnclippedLoans} clipped_loans {value.ClippedLoans} unexpected_added_s {value.UnexpectedAdded:R} balance_s {balance:R} busy {value.Busy} clipped {value.Clipped} held {value.Held} ignored {value.Ignored} vetoed {value.Vetoed} resets {value.Resets} exceptions {value.Exceptions} overflow {value.Overflows} accounting_faults {value.Errors}"));
            openingOwed=counter.Owed;
        }
    }
    [HarmonyPatch(typeof(Game),"SimEveryTick")]
    internal static class ColonyPacingRetainPatch
    {
        private static bool Prepare()=>ColonyPacingRetainedLog.Enabled;
        // Existing First lifecycle/readiness/refusal guards run before this;
        // the existing normal-priority busy fence runs after it.
        [HarmonyPriority(Priority.High)]
        private static void Prefix(ref float __0,out ColonyRetainState __state)=>__state=ColonyPacingRetainedLog.Begin(ref __0);
        [HarmonyPriority(Priority.Last)]
        private static Exception? Finalizer(float __0,bool __runOriginal,Exception? __exception,ColonyRetainState __state)
        {ColonyPacingRetainedLog.Finish(__state,__0,__runOriginal,__exception);return __exception;}
    }
    [HarmonyPatch(typeof(Game),"SimEveryTick")]
    internal static class ColonyPacingRetainAdmissionPatch
    {
        private static bool Prepare()=>ColonyPacingRetainedLog.Enabled;
        // Runs after every prefix in this mod, including the busy fence.
        [HarmonyPriority(Priority.Last-100)]
        private static void Prefix(float __0,bool __runOriginal)=>ColonyPacingRetainedLog.Admission(__0,__runOriginal);
    }
    [HarmonyPatch(typeof(SaveLoader),nameof(SaveLoader.Save),new[]{typeof(string),typeof(bool),typeof(bool)})]
    internal static class ColonyPacingRetainSavePatch
    {
        private static bool Prepare()=>ColonyPacingRetainedLog.Enabled;
        [HarmonyPriority(Priority.First)]
        private static void Prefix()=>ColonyPacingRetainedLog.SaveBoundary();
    }
}
