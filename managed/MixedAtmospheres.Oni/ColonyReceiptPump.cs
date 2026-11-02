using System;
using HarmonyLib;
using UnityEngine;
namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class ColonyOwner
    {
        private static readonly bool receiptPumpEnabled=Environment.GetEnvironmentVariable("MA_COLONY_BYPRODUCT_PUMP")!="0";
        private static readonly bool receiptPumpDiagnostics=Environment.GetEnvironmentVariable("MA_COLONY_BYPRODUCT_PUMP_LOG")=="1";
        private static readonly ColonyReceiptPumpState receiptPump=new ColonyReceiptPumpState();
        private static readonly long ReceiptPumpBudgetTicks=(long)(2*System.Diagnostics.Stopwatch.Frequency/1000);
        private static readonly OniNativeColony.Receipt[] pumpReceipts=new OniNativeColony.Receipt[4096];
        internal static IDisposable SuspendReceiptPump()=>receiptPump.Suspend();
        internal static void ResetReceiptPump()=>receiptPump.Reset();
        internal static object? ReceiptPumpReport()=>receiptPumpDiagnostics?receiptPump.Report():null;
        private static bool PumpGuards()=>Active && !ColonyFault.Faulted && !ColonyLoadRefusal.Refused &&
            Game.Instance!=null && Game.Instance.GameStarted() && SpeedControlScreen.Instance!=null && !SpeedControlScreen.Instance.IsPaused &&
            Time.timeScale>0 && !ColonyOwnerPausedResave.Holding && !ColonyContinuationProbe.Holding &&
            !ColonyCallbackRequests.Pending && !ColonySavedParticles.Pending && !ColonyPipeSave.Pending &&
            !ColonyPipeRuntime.NativeBindingsPending && !ColonyPipeCultureState.NeedsReadyBarrier && !ColonyClusterRetirement.Pending;
        // The first unacknowledged receipt of `session`, or 0 when the native
        // session was replaced or holds none.
        private static ulong ReceiptPrefixHead(ulong session)
        {
            if(ColonyReceiptPumpNative.Read().Session!=session)return 0;
            OniNativeColony.Check(OniNativeColony.CopyReceipts(pumpReceipts,1,out var available));
            return available==0?0:pumpReceipts[0].Sequence;
        }
        internal static void PumpDeferredByproducts()
        {
            if(!receiptPumpEnabled || !PumpGuards() || !ColonySaveBarrier.CanRunLifecycle)return;
            if(!receiptPump.TryPump(Time.frameCount,out var lease))return;
            using(lease)
            {
                var begun=System.Diagnostics.Stopwatch.GetTimestamp();
                var session=receiptPump.Session;var frame=receiptPump.PublishedFrame;var fence=receiptPump.ObservedThrough;var publication=receiptPump.PublicationSerial;
                var entered=false;
                try
                {
                    ColonySaveBarrier.EnterStep();entered=true;
                    var native=ColonyReceiptPumpNative.Read();
                    receiptPump.MaximumObservedNativePending=Math.Max(receiptPump.MaximumObservedNativePending,native.Pending);
                    if(native.State!=1 || native.Session!=session || !receiptPump.Current(lease))return;
                    OniNativeColony.Check(OniNativeColony.CopyReceipts(pumpReceipts,(uint)pumpReceipts.Length,out var available));
                    var count=(int)Math.Min(available,(uint)pumpReceipts.Length);var through=0UL;var delivered=0;
                    for(var k=0;k<count;++k)
                    {
                        // A lease reset or fault inside a spawn stops the batch;
                        // the debris spawned so far is still acknowledged below.
                        if(!receiptPump.Current(lease) || ColonyFault.Faulted)break;
                        if(!PumpGuards() || receiptPump.Suspended || receiptPump.PublicationSerial!=publication){++receiptPump.GuardStops;break;}
                        if(delivered>0 && System.Diagnostics.Stopwatch.GetTimestamp()-begun>ReceiptPumpBudgetTicks)
                        {++receiptPump.BudgetStops;break;}
                        var r=pumpReceipts[k];
                        if(r.Sequence==0 || r.Sequence>fence || r.CompletedFrame>frame || r.Kind!=OniNativeColony.Byproduct)
                        {++receiptPump.PrefixStops;break;}
                        if(!ColonyReceiptPumpNative.PlainByproduct(r.Sequence) || !Grid.IsValidCell((int)r.PublicCell) ||
                            !InRect((int)r.PublicCell,out _) || !(r.MassKg>0) || float.IsInfinity(r.MassKg) ||
                            !(r.TemperatureK>0) || float.IsInfinity(r.TemperatureK) || ElementLoader.FindElementByHash((SimHashes)r.Callback)==null)
                        {++receiptPump.AmbiguousStops;break;}
                        SpawnByproduct(r);through=r.Sequence;++delivered;
                    }
                    if(through==0)return;
                    // Debris exists for every receipt through `through`: always
                    // acknowledge exactly that prefix while its own native session
                    // still holds it, even after a fault (a native fault does not
                    // stop ordinary publications from delivering receipts) or a
                    // lease reset. No callback code runs between this check and
                    // Ack; native append only extends the tail. Without the lease,
                    // a nested delivery may already have settled part of it.
                    var leased=receiptPump.Current(lease);
                    if(leased && ColonyReceiptPumpNative.Read().Session!=session)return;
                    if(!leased){var head=ReceiptPrefixHead(session);if(head==0 || head>through)return;}
                    OniNativeColony.Check(OniNativeColony.AckReceipts(through));
                    if(leased)receiptPump.Delivered+=delivered;
                }
                catch(Exception error)
                {
                    if(receiptPump.Current(lease))ColonyFault.Raise("deferred byproduct pump",error);
                }
                finally
                {
                    if(entered)ColonySaveBarrier.ExitStep();
                    var elapsed=System.Diagnostics.Stopwatch.GetTimestamp()-begun;
                    ColonyFrameLog.AddTicks(ColonyFrameLog.Part.ByproductPump,elapsed);
                    if(receiptPump.Current(lease)){receiptPump.TotalTicks+=elapsed;receiptPump.MaximumTicks=Math.Max(receiptPump.MaximumTicks,elapsed);}
                }
            }
        }
    }
    // Drop the old publication fence before loading/quitting can replace the
    // native session or world. No receipt or clock is touched by these hooks.
    [HarmonyPatch(typeof(PauseScreen),nameof(PauseScreen.TriggerQuitGame))]
    internal static class ColonyReceiptPumpQuitPatch
    {private static void Prefix()=>ColonyOwner.ResetReceiptPump();}
    [HarmonyPatch(typeof(SaveLoader),nameof(SaveLoader.LoadScene),new Type[]{})]
    internal static class ColonyReceiptPumpLoadPatch
    {private static void Prefix()=>ColonyOwner.ResetReceiptPump();}
}
