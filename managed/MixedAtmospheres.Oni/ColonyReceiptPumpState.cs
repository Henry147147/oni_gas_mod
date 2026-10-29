using System;
namespace MixedAtmospheres.Oni
{
    // Main-thread execution metadata only. No physical or checkpoint state.
    internal sealed class ColonyReceiptPumpState
    {
        private ulong epoch=1, nextLease, activeLease;
        private Suspension? suspended;
        private bool hasPublication, hasAttempt;
        private ulong firstFrame;
        private int publicationUnityFrame, attemptedUnityFrame;
        internal ulong Generation=>epoch;
        internal ulong PublicationSerial {get;private set;}
        internal ulong Session {get;private set;}
        internal ulong PublishedFrame {get;private set;}
        internal ulong ObservedThrough {get;private set;}
        internal bool Armed {get;private set;}
        internal bool Suspended=>suspended!=null;
        internal long Attempts, Runs, Delivered, BudgetStops, GuardStops, AmbiguousStops, PrefixStops, Reentries;
        internal ulong MaximumObservedNativePending;
        internal long TotalTicks, MaximumTicks;
        internal void Reset()
        {
            unchecked{++epoch;}activeLease=0;suspended=null;hasPublication=hasAttempt=Armed=false;
            Session=PublishedFrame=ObservedThrough=firstFrame=PublicationSerial=0;
            Attempts=Runs=Delivered=BudgetStops=GuardStops=AmbiguousStops=PrefixStops=Reentries=0;
            MaximumObservedNativePending=0;TotalTicks=MaximumTicks=0;
        }
        internal void Published(ulong session,ulong frame,ulong observedThrough,int unityFrame)
        {
            if(!hasPublication)
            {hasPublication=true;Session=session;firstFrame=frame;}
            else if(Session!=session)
            {Reset();hasPublication=true;Session=session;firstFrame=frame;}
            if(frame<PublishedFrame)throw new InvalidOperationException("Receipt publication clock went backwards.");
            unchecked{++PublicationSerial;}PublishedFrame=frame;ObservedThrough=Math.Max(ObservedThrough,observedThrough);publicationUnityFrame=unityFrame;
            // A positive continuation proves load/startup readiness passed.
            Armed|=frame>firstFrame;
        }
        internal bool Eligible(ulong sequence,ulong frame,uint kind)=>
            sequence!=0 && sequence<=ObservedThrough && frame<=PublishedFrame && kind==5;
        internal bool TryEnter(out Lease lease)
        {
            if(activeLease!=0){++Reentries;lease=default;return false;}
            unchecked{++nextLease;if(nextLease==0)++nextLease;}activeLease=nextLease;
            lease=new Lease(this,epoch,activeLease);return true;
        }
        internal bool TryPump(int unityFrame,out Lease lease)
        {
            ++Attempts;
            if(!Armed || Suspended || unityFrame==publicationUnityFrame || hasAttempt&&unityFrame==attemptedUnityFrame)
            {++GuardStops;lease=default;return false;}
            if(!TryEnter(out lease))return false;
            hasAttempt=true;attemptedUnityFrame=unityFrame;++Runs;return true;
        }
        internal bool Current(in Lease lease)=>lease.Owner==this && lease.Epoch==epoch && lease.Id==activeLease && activeLease!=0;
        internal void Finish(in Lease lease){if(Current(lease))activeLease=0;}
        internal readonly struct Lease:IDisposable
        {
            internal readonly ColonyReceiptPumpState? Owner;
            internal readonly ulong Epoch,Id;
            internal Lease(ColonyReceiptPumpState owner,ulong generation,ulong id){Owner=owner;Epoch=generation;Id=id;}
            public void Dispose()=>Owner?.Finish(this);
        }
        internal IDisposable Suspend()
        {var next=new Suspension(this,suspended);suspended=next;return next;}
        private sealed class Suspension:IDisposable
        {
            private readonly ColonyReceiptPumpState owner;
            internal readonly Suspension? Previous;
            private bool disposed;
            internal Suspension(ColonyReceiptPumpState owner, Suspension? previous){this.owner=owner;Previous=previous;}
            public void Dispose()
            {
                if(disposed)return;disposed=true;if(!ReferenceEquals(owner.suspended,this))return;
                do{owner.suspended=owner.suspended!.Previous;}while(owner.suspended!=null&&owner.suspended.disposed);
            }
        }
        // Admission Attempts begin after the top-level owner/lifecycle guards;
        // PrefixStops combines sequence/frame fences and non-byproduct heads.
        internal object Report()=>new{AttemptScope="after owner/lifecycle guards",Armed,Session,PublishedFrame,ObservedThrough,Attempts,Runs,Delivered,BudgetStops,GuardStops,AmbiguousStops,PrefixStops,Reentries,MaximumObservedNativePending,TotalTicks,MaximumTicks};
    }
}
