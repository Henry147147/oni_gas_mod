using System;
using System.Runtime.InteropServices;
namespace MixedAtmospheres.Oni
{
    internal static class ColonyReceiptPumpNative
    {
        private const string Library="mixed_atmospheres_oni";
        [StructLayout(LayoutKind.Explicit,Size=600)]
        internal struct Reading
        {
            [FieldOffset(0)]internal uint Size;
            [FieldOffset(4)]internal uint Version;
            [FieldOffset(8)]internal uint State;
            [FieldOffset(16)]internal ulong Session;
            [FieldOffset(24)]internal ulong Frame;
            [FieldOffset(88)]internal ulong Pending;
            [FieldOffset(112)]internal double Seconds;
        }
        [DllImport(Library,EntryPoint="ma_oni_colony_status",CallingConvention=CallingConvention.Cdecl)]
        private static extern int Status(ref Reading value);
        [DllImport(Library,EntryPoint="ma_oni_colony_receipt_job",CallingConvention=CallingConvention.Cdecl)]
        internal static extern int Job(ulong sequence,out ulong token);
        [DllImport(Library,EntryPoint="ma_oni_colony_copy_precise_receipt",CallingConvention=CallingConvention.Cdecl)]
        private static extern int Precise(ulong sequence,IntPtr buffer,uint capacity,out uint count);
        [DllImport(Library,EntryPoint="ma_oni_colony_copy_moving_outcome",CallingConvention=CallingConvention.Cdecl)]
        private static extern int Moving(ulong sequence,IntPtr buffer,uint capacity,out uint count);
        private static readonly bool compatible=Compatible();
        private static bool Compatible()
        {
            if(Marshal.SizeOf<Reading>()!=600 || Marshal.SizeOf<OniNativeColony.Status>()!=600 ||
               Marshal.OffsetOf<OniNativeColony.Status>(nameof(OniNativeColony.Status.State)).ToInt32()!=8 ||
               Marshal.OffsetOf<OniNativeColony.Status>(nameof(OniNativeColony.Status.Session)).ToInt32()!=16 ||
               Marshal.OffsetOf<OniNativeColony.Status>(nameof(OniNativeColony.Status.CompletedFrames)).ToInt32()!=24 ||
               Marshal.OffsetOf<OniNativeColony.Status>(nameof(OniNativeColony.Status.ReceiptsPending)).ToInt32()!=88 ||
               Marshal.OffsetOf<OniNativeColony.Status>(nameof(OniNativeColony.Status.TimeSeconds)).ToInt32()!=112)
                throw new NotSupportedException("Receipt pump requires the matching600-byte native status ABI.");
            return true;
        }
        internal static Reading Read()
        {
            _=compatible;var value=new Reading{Size=600,Version=OniNativeColony.AbiVersion};
            OniNativeColony.Check(Status(ref value));return value;
        }
        internal static bool PlainByproduct(ulong sequence)
        {
            OniNativeColony.Check(Job(sequence,out var job));if(job!=0)return false;
            OniNativeColony.Check(Precise(sequence,IntPtr.Zero,0,out var precise));if(precise!=0)return false;
            OniNativeColony.Check(Moving(sequence,IntPtr.Zero,0,out var moving));return moving==0;
        }
    }
}
