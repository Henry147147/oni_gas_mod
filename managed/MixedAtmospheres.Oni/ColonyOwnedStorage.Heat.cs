using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyOwnedStorage
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct NativeStorageHeatReceipt
        {
            internal uint Size,Version;
            internal ulong Sequence,Frame;
            internal int Handle;
            internal uint Kind;
            internal float Capacity,Before,After;
            internal uint Reserved2;
        }
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_version",CallingConvention=CallingConvention.Cdecl)]
        private static extern uint StorageHeatVersion();
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_register",CallingConvention=CallingConvention.Cdecl)]
        private static extern int RegisterStorageHeat(int handle,float published,float capacity);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_expect_data",CallingConvention=CallingConvention.Cdecl)]
        private static extern int DeclareStorageHeatData(int handle,float temperature,float capacity);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_expect_removal",CallingConvention=CallingConvention.Cdecl)]
        private static extern int DeclareStorageRemoval(int handle);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_copy",CallingConvention=CallingConvention.Cdecl)]
        private static extern int CopyStorageHeat([Out]NativeStorageHeatReceipt[] output,uint capacity,out uint count);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_ack",CallingConvention=CallingConvention.Cdecl)]
        private static extern int AckStorageHeat(ulong through);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_reset",CallingConvention=CallingConvention.Cdecl)]
        private static extern int ResetStorageHeat();
        private static readonly NativeStorageHeatReceipt[] storageHeatReceipts=new NativeStorageHeatReceipt[32768];
        private static ulong storageHeatAcknowledged;
        internal static long NativeHeatReceipts {get;private set;}
        internal static long NativeRetirements {get;private set;}
        private static void RequireStorageHeat()
        {
            uint version;
            try {version=StorageHeatVersion();}
            catch(EntryPointNotFoundException error)
            {throw new NotSupportedException("Exact storage requires a matching native heat receipt bundle.",error);}
            if(Marshal.SizeOf<NativeStorageHeatReceipt>()!=48||version!=2)
                throw new NotSupportedException("This native bundle has no verified exact-storage heat receipt boundary.");
        }
        private static void ClearStorageHeat()
        {
            if(Enabled)OniNativeColony.Check(ResetStorageHeat());
            NativeHeatReceipts=NativeRetirements=0;storageHeatAcknowledged=0;
        }
        // Native chunks can cool after a publication but before a queued
        // absolute reset. Read those completed segments, never infer work by
        // subtracting a requested temperature from a later published value.
        private static Dictionary<Host,PipeAmount> ReadNativeStorageHeat(out uint count,out ulong sequence,out List<(Host Host,Projection View)> retirements)
        {
            OniNativeColony.Check(CopyStorageHeat(storageHeatReceipts,(uint)storageHeatReceipts.Length,out count));
            var amounts=new Dictionary<Host,PipeAmount>();
            retirements=new List<(Host,Projection)>();
            sequence=storageHeatAcknowledged;
            if(count==0)return amounts;
            var byHandle=new Dictionary<int,(Host Host,Projection View)>();
            foreach(var host in Hosts.Values)foreach(var view in host.Projections.Values.Concat(host.Retiring))
                if(view.Handle!=-1)byHandle.Add(view.Handle,(host,view));
            for(var i=0;i<count;++i)
            {
                var receipt=storageHeatReceipts[i];
                if(receipt.Size!=48||receipt.Version!=2||receipt.Kind>1||receipt.Reserved2!=0||
                    receipt.Sequence!=checked(sequence+1)||!byHandle.TryGetValue(receipt.Handle,out var found))
                    throw new InvalidOperationException("An exact-storage heat receipt has invalid framing, ordering or ownership.");
                TraceNativeHeatReceipt(found.Host,receipt);
                if(receipt.Kind==1)
                {
                    if(!found.View.Retiring||found.View.Capacity!=0||receipt.Capacity!=0||
                        BitConverter.SingleToInt32Bits(receipt.Before)!=BitConverter.SingleToInt32Bits(receipt.After)||
                        retirements.Any(item=>ReferenceEquals(item.View,found.View)))
                        throw new InvalidOperationException("An exact-storage retirement receipt does not match an exhausted projection.");
                    retirements.Add(found);sequence=receipt.Sequence;continue;
                }
                var work=PipeContactWork.IntoFluid(receipt.Capacity,receipt.Before,receipt.After).Negated();
                amounts.TryGetValue(found.Host,out var previous);amounts[found.Host]=previous.Plus(work);
                sequence=receipt.Sequence;
            }
            return amounts;
        }
        // Read-only control observation: inspecting completed native work does
        // not credit it or consume receipts.
        internal static PipeAmount PendingNativeHeat(Storage storage)
        {
            if(!Enabled||!Hosts.TryGetValue(storage,out var host))return PipeAmount.Zero;
            var amounts=ReadNativeStorageHeat(out _,out _,out _);
            return amounts.TryGetValue(host,out var work)?work:PipeAmount.Zero;
        }
        private static void CreditNativeStorageHeat()
        {
            var amounts=ReadNativeStorageHeat(out var count,out var sequence,out var retirements);
            if(count==0)return;
            // A malformed/thermodynamically invalid credit cannot consume part
            // of the native receipt prefix or mutate just the first store.
            var changes=new List<OwnedFluidStore.HeatChange>();
            foreach(var pair in amounts)changes.Add(pair.Key.Store.PrepareHeat(pair.Value,ColonyConduits.Material));
            foreach(var change in changes)change.Owner.Commit(change);
            OniNativeColony.Check(AckStorageHeat(sequence));
            storageHeatAcknowledged=sequence;NativeHeatReceipts+=count-retirements.Count;NativeRetirements+=retirements.Count;
            foreach(var retired in retirements)retired.Host.Retiring.Remove(retired.View);
        }
    }
}
