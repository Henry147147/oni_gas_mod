using System;
using System.Runtime.InteropServices;
using System.Text;

namespace MixedAtmospheres.Oni
{
    // Layout-only client shared by Unity and the isolated licensed-native host.
    internal static class OniNativeDomain
    {
        private const string Library = "mixed_atmospheres_oni";
        [StructLayout(LayoutKind.Sequential)]
        internal struct Configuration
        {
            public uint Size, Version, Flags, GridWidth, X, Y, Width, Height, Scenario, Reserved;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Status
        {
            public uint Size, Version, State, Cells;
            public ulong Session, CompletedFrames, NativeTicket, Substeps, RejectedAttempts, SolverThread, StartThread;
            public double TimeSeconds, LastTimestepSeconds, MaximumRelativeMassError, MaximumRelativeEnergyError;
            public double InitialOxygenKg, CurrentOxygenKg, PressurePa, LiquidMassKg;
            public ulong SuppressedPressure, SuppressedLiquid, SuppressedTemperature, SuppressedTransition, SuppressedPostProcess;
            public ulong SuppressedGas;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)]
            private byte[] ErrorBytes;
            public string Error => Encoding.UTF8.GetString(ErrorBytes ?? Array.Empty<byte>()).TrimEnd('\0');
            internal static Status Empty => new Status { Size = 440, Version = 1, ErrorBytes = new byte[256] };
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Cell
        {
            public double Nitrogen, Oxygen, Helium, Water, InternalEnergy, Pressure, Temperature, LiquidFraction;
            public double ResolvedKineticEnergy, MomentumX, MomentumY;
            public double Mass(int index) => index == 0 ? Nitrogen : index == 1 ? Oxygen : index == 2 ? Helium : Water;
            public double TotalMass => Nitrogen + Oxygen + Helium + Water;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Species
        {
            public double MolarMass, Cv, Offset, Density;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct HeatCommand
        {
            public uint Size, Version;
            public ulong Session, ExpectedFrame;
            public uint PublicCell, Source;
            public double Joules, MaximumTemperature;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct HeatStatus
        {
            public uint Size, Version, State, PendingBatches;
            public ulong Session, AcceptedBatches, CommittedBatches;
            public double CommittedHeatJoules, PendingHeatJoules;
            internal static HeatStatus Empty => new HeatStatus { Size = 56, Version = 1 };
        }
        [DllImport(Library, EntryPoint = "ma_oni_domain_heat_abi_version", CallingConvention = CallingConvention.Cdecl)] internal static extern uint HeatVersion();
        [DllImport(Library, EntryPoint = "ma_oni_domain_queue_heat", CallingConvention = CallingConvention.Cdecl)] internal static extern int QueueHeat([In] HeatCommand[] commands, uint count, ref HeatStatus status);
        [DllImport(Library, EntryPoint = "ma_oni_domain_heat_status", CallingConvention = CallingConvention.Cdecl)] internal static extern int CopyHeat(ref HeatStatus status);
        [DllImport(Library, EntryPoint = "ma_oni_domain_abi_version", CallingConvention = CallingConvention.Cdecl)] internal static extern uint Version();
        [DllImport(Library, EntryPoint = "ma_oni_domain_species", CallingConvention = CallingConvention.Cdecl)] internal static extern int Definition(uint index, out Species value);
        [DllImport(Library, EntryPoint = "ma_oni_domain_start", CallingConvention = CallingConvention.Cdecl)] internal static extern int Begin(ref Configuration configuration, ref Status status);
        [DllImport(Library, EntryPoint = "ma_oni_domain_copy", CallingConvention = CallingConvention.Cdecl)] internal static extern int Copy(ref Status status, [Out] Cell[] values, uint capacity);
        [DllImport(Library, EntryPoint = "ma_oni_domain_stop", CallingConvention = CallingConvention.Cdecl)] internal static extern int Stop(ref Status status);
        [DllImport(Library, EntryPoint = "ma_oni_last_error", CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr LastError();
    }
}
