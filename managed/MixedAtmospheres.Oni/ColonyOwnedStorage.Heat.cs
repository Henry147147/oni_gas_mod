using System;
using System.Collections.Generic;
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
            internal uint Reserved;
            internal float Capacity,Before,After;
            internal uint Reserved2;
        }
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_version",CallingConvention=CallingConvention.Cdecl)]
        private static extern uint StorageHeatVersion();
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_register",CallingConvention=CallingConvention.Cdecl)]
        private static extern int RegisterStorageHeat(int handle,float published,float capacity);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_expect_data",CallingConvention=CallingConvention.Cdecl)]
        private static extern int DeclareStorageHeatData(int handle,float temperature,float capacity);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_copy",CallingConvention=CallingConvention.Cdecl)]
        private static extern int CopyStorageHeat([Out]NativeStorageHeatReceipt[] output,uint capacity,out uint count);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_ack",CallingConvention=CallingConvention.Cdecl)]
        private static extern int AckStorageHeat(ulong through);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_storage_heat_reset",CallingConvention=CallingConvention.Cdecl)]
        private static extern int ResetStorageHeat();
        private static readonly NativeStorageHeatReceipt[] storageHeatReceipts=new NativeStorageHeatReceipt[32768];
        private static ulong storageHeatAcknowledged;
        internal static long NativeHeatReceipts {get;private set;}
        private static void RequireStorageHeat()
        {
            uint version;
            try {version=StorageHeatVersion();}
            catch(EntryPointNotFoundException error)
            {throw new NotSupportedException("Exact storage requires a matching native heat receipt bundle.",error);}
            if(Marshal.SizeOf<NativeStorageHeatReceipt>()!=48||version!=1)
                throw new NotSupportedException("This native bundle has no verified exact-storage heat receipt boundary.");
        }
        private static void ClearStorageHeat()
        {
            if(Enabled)OniNativeColony.Check(ResetStorageHeat());
            NativeHeatReceipts=0;storageHeatAcknowledged=0;
        }
        // Native chunks can cool after a publication but before a queued
        // absolute reset. Read those completed segments, never infer work by
        // subtracting a requested temperature from a later published value.
        private static void CreditNativeStorageHeat()
        {
            OniNativeColony.Check(CopyStorageHeat(storageHeatReceipts,(uint)storageHeatReceipts.Length,out var count));
            if(count==0)return;
            var amounts=new Dictionary<Host,PipeAmount>();
            var byHandle=new Dictionary<int,(Host Host,Projection View)>();
            foreach(var host in Hosts.Values)foreach(var view in host.Projections.Values)
                if(view.Handle>=0)byHandle.Add(view.Handle,(host,view));
            var sequence=storageHeatAcknowledged;
            for(var i=0;i<count;++i)
            {
                var receipt=storageHeatReceipts[i];
                if(receipt.Size!=48||receipt.Version!=1||receipt.Reserved!=0||receipt.Reserved2!=0||
                    receipt.Sequence!=checked(sequence+1)||!byHandle.TryGetValue(receipt.Handle,out var found))
                    throw new InvalidOperationException("An exact-storage heat receipt has invalid framing, ordering or ownership.");
                var work=PipeContactWork.IntoFluid(receipt.Capacity,receipt.Before,receipt.After).Negated();
                amounts.TryGetValue(found.Host,out var previous);amounts[found.Host]=previous.Plus(work);
                sequence=receipt.Sequence;
            }
            // A malformed/thermodynamically invalid credit cannot consume part
            // of the native receipt prefix or mutate just the first store.
            var changes=new List<OwnedFluidStore.HeatChange>();
            foreach(var pair in amounts)changes.Add(pair.Key.Store.PrepareHeat(pair.Value,ColonyConduits.Material));
            foreach(var change in changes)change.Owner.Commit(change);
            OniNativeColony.Check(AckStorageHeat(sequence));
            storageHeatAcknowledged=sequence;NativeHeatReceipts+=count;
        }
    }
}
