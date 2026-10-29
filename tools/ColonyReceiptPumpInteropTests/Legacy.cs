using System;using System.Text;using System.Runtime.InteropServices;
namespace MixedAtmospheres.Oni {internal static unsafe class OniNativeColony {internal const uint AbiVersion=1;
[StructLayout(LayoutKind.Sequential)]
        internal struct Status
        {
            public uint Size, Version, State, Species;
            public ulong Session, CompletedFrames, NativeTicket, Substeps, RejectedAttempts, StartThread, SolverThread;
            public ulong CommittedTransfers, CommittedHeat, ReceiptsPending;
            public ulong OpenedCells, ClosedCells;
            public double TimeSeconds, LastFrameSeconds, LastAdvanceMilliseconds, MaximumAdvanceMilliseconds;
            public double MaximumRelativeMassError, MaximumRelativeEnergyError;
            public double ReservoirKg, SpaceSinkKg, InjectedKg, ExtractedKg;
            public double NativeHeatJoules, NativeMassInKg, NativeMassOutKg;
            public ulong NativeChanges;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 6)] public double[] StageMilliseconds;
            public double EvaporatedKg, CondensedKg;
            public ulong SkippedRelaxations, ReservedCount;
            public double ReleasedKg, ConvertedKg, SettledKg;
            public double TraceSinkKg, TraceSinkJoules;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)] private byte[] ErrorBytes;
            public string Error => Encoding.UTF8.GetString(ErrorBytes ?? Array.Empty<byte>()).TrimEnd('\0');
            internal static Status Empty => new Status { Size = 600, Version = AbiVersion, ErrorBytes = new byte[256], StageMilliseconds = new double[6] };
        }
[StructLayout(LayoutKind.Sequential)]
        internal struct Receipt { public ulong Sequence, CompletedFrame; public int Callback; public uint Kind, NativeIndex, PublicCell; public float MassKg, TemperatureK; public uint Disease; public int DiseaseCount; }
internal static void Check(int result){if(result!=0)throw new InvalidOperationException("native result"+result);}
[DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_colony_status",CallingConvention=CallingConvention.Cdecl)]internal static extern int CopyStatus(ref Status status);
[DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_colony_copy_receipts",CallingConvention=CallingConvention.Cdecl)]private static extern int CopyReceiptsPinned(Receipt* values,uint capacity,out uint count);
internal static int CopyReceipts(Receipt[] values,out uint count){fixed(Receipt* ptr=values)return CopyReceiptsPinned(ptr,(uint)values.Length,out count);}
[DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_colony_ack_receipts",CallingConvention=CallingConvention.Cdecl)]internal static extern int AckReceipts(ulong through);
}}
