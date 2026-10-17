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
            internal static Status Empty => new Status { Size = 440, Version = 2, ErrorBytes = new byte[256] };
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Cell
        {
            public double Nitrogen, Oxygen, Helium, Water, CarbonDioxide, InternalEnergy, Pressure, Temperature, LiquidFraction;
            public double ResolvedKineticEnergy, MomentumX, MomentumY;
            public double Mass(int index) => index == 0 ? Nitrogen : index == 1 ? Oxygen : index == 2 ? Helium : index == 3 ? Water : index == 4 ? CarbonDioxide : throw new ArgumentOutOfRangeException(nameof(index));
            public double TotalMass => Nitrogen + Oxygen + Helium + Water + CarbonDioxide;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct InitialCell
        {
            public uint Size, Version;
            public double Nitrogen, Oxygen, Helium, Water, CarbonDioxide, Temperature;
            public double Mass(int index) => index == 0 ? Nitrogen : index == 1 ? Oxygen : index == 2 ? Helium : index == 3 ? Water : index == 4 ? CarbonDioxide : throw new ArgumentOutOfRangeException(nameof(index));
        }
        [DllImport(Library, EntryPoint = "ma_oni_domain_initial_inventory_abi_version", CallingConvention = CallingConvention.Cdecl)] internal static extern uint InitialInventoryVersion();
        [DllImport(Library, EntryPoint = "ma_oni_domain_start_from_cells", CallingConvention = CallingConvention.Cdecl)] internal static extern int BeginFromCells(ref Configuration configuration, [In] InitialCell[] cells, uint count, ref Status status);
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
        [StructLayout(LayoutKind.Sequential)]
        internal struct TransferCommand
        {
            public uint Size, Version;
            public ulong Session, ExpectedFrame;
            public uint PublicCell, Species, Kind, Flags;
            public int Callback;
            public uint Reserved;
            public double MassKg, Temperature;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Receipt
        {
            public ulong Sequence, Session, CompletedFrame;
            public uint PublicCell, Species;
            public int Callback;
            public uint Kind;
            public double MassKg, Temperature, InternalEnergy, KineticEnergy, MomentumX, MomentumY;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct TransferStatus
        {
            public uint Size, Version, State, PendingBatches;
            public ulong Session, AcceptedBatches, CommittedBatches, UnacknowledgedReceipts;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 5)] public double[] MassChangesKg;
            public double MaterialEnergyChange, KineticEnergyChange;
            internal static TransferStatus Empty => new TransferStatus { Size = 104, Version = 2, MassChangesKg = new double[5] };
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct ActorCommand
        {
            public uint Size, Version;
            public ulong Session, ExpectedFrame;
            public int Handle;
            public uint Kind, PublicCell, Species, Flags, Reserved;
            public double MassKg, Interval, Temperature, MaximumGasDensity;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct ActorStatus
        {
            public uint Size, Version, State, LiveActors;
            public ulong Session, CompletedFrame, AcceptedConfigurations, ActorSteps, ConsumerSamples, EmitterSamples;
            internal static ActorStatus Empty => new ActorStatus { Size = 64, Version = 1 };
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct ThermalStatus
        {
            public uint Size, Version, State, Enabled;
            public ulong Session, CompletedFrame, Stages, Contacts, Iterations, UnresolvedUpdates;
            public double RedistributedEnergy, EntropyChange;
            internal static ThermalStatus Empty => new ThermalStatus { Size = 80, Version = 1 };
        }
        [DllImport(Library, EntryPoint = "ma_oni_domain_thermal_abi_version", CallingConvention = CallingConvention.Cdecl)] internal static extern uint ThermalVersion();
        [DllImport(Library, EntryPoint = "ma_oni_domain_thermal_status", CallingConvention = CallingConvention.Cdecl)] internal static extern int CopyThermalStatus(ref ThermalStatus status);
        [StructLayout(LayoutKind.Sequential)]
        internal struct GravityStatus
        {
            public uint Size, Version, State, Enabled;
            public ulong Session, CompletedFrame;
            public double Gravity, InitialPotential, Potential, CommittedBoundaryPotential, PendingBoundaryPotential;
            public double BodyImpulseY, InternalWork, MaximumEnergyResidual;
            internal static GravityStatus Empty => new GravityStatus { Size = 96, Version = 1 };
        }
        [DllImport(Library, EntryPoint = "ma_oni_domain_gravity_abi_version", CallingConvention = CallingConvention.Cdecl)] internal static extern uint GravityVersion();
        [DllImport(Library, EntryPoint = "ma_oni_domain_gravity_status", CallingConvention = CallingConvention.Cdecl)] internal static extern int CopyGravityStatus(ref GravityStatus status);
        [DllImport(Library, EntryPoint = "ma_oni_domain_actor_abi_version", CallingConvention = CallingConvention.Cdecl)] internal static extern uint ActorVersion();
        [DllImport(Library, EntryPoint = "ma_oni_domain_configure_actor", CallingConvention = CallingConvention.Cdecl)] internal static extern int ConfigureActor(ref ActorCommand command, ref ActorStatus status);
        [DllImport(Library, EntryPoint = "ma_oni_domain_configure_actors", CallingConvention = CallingConvention.Cdecl)] internal static extern int ConfigureActors([In] ActorCommand[] commands, uint count, ref ActorStatus status);
        [DllImport(Library, EntryPoint = "ma_oni_domain_actor_status", CallingConvention = CallingConvention.Cdecl)] internal static extern int CopyActorStatus(ref ActorStatus status);
        [DllImport(Library, EntryPoint = "ma_oni_domain_checkpoint_abi_version", CallingConvention = CallingConvention.Cdecl)] internal static extern uint CheckpointVersion();
        [DllImport(Library, EntryPoint = "ma_oni_domain_checkpoint_copy", CallingConvention = CallingConvention.Cdecl)] internal static extern int CopyCheckpoint(ulong session, ulong frame, [Out] byte[]? output, uint capacity, out uint required);
        [DllImport(Library, EntryPoint = "ma_oni_domain_checkpoint_advance", CallingConvention = CallingConvention.Cdecl)] internal static extern int AdvanceCheckpoint([In] byte[] input, uint count, double seconds, [Out] byte[]? output, uint capacity, out uint required);
        [DllImport(Library, EntryPoint = "ma_oni_domain_checkpoint_validate", CallingConvention = CallingConvention.Cdecl)] internal static extern int ValidateCheckpoint(ref Configuration configuration, [In] byte[] input, uint count, double savedTime);
        [DllImport(Library, EntryPoint = "ma_oni_domain_restore", CallingConvention = CallingConvention.Cdecl)] internal static extern int RestoreCheckpoint(ref Configuration configuration, [In] byte[] input, uint count, double savedTime, ref Status result);
        [StructLayout(LayoutKind.Sequential)]
        internal struct PairStatus
        {
            public uint Size, Version, State, NativeBytes, PhysicalBytes, Reserved;
            public ulong Token, Session, CompletedFrame, NativeEpoch, CaptureThread;
            public Configuration Configuration;
            public double PhysicalTime;
            internal static PairStatus Empty => new PairStatus { Size = 112, Version = 1 };
        }
        [DllImport(Library, EntryPoint = "ma_oni_domain_pair_abi_version", CallingConvention = CallingConvention.Cdecl)] internal static extern uint PairVersion();
        [DllImport(Library, EntryPoint = "ma_oni_domain_pair_request", CallingConvention = CallingConvention.Cdecl)] internal static extern int RequestPair(ulong session, ulong frame, int x, int y, ref PairStatus result);
        [DllImport(Library, EntryPoint = "ma_oni_domain_pair_poll", CallingConvention = CallingConvention.Cdecl)] internal static extern int PollPair(ulong token, ref PairStatus result);
        [DllImport(Library, EntryPoint = "ma_oni_domain_pair_copy", CallingConvention = CallingConvention.Cdecl)] internal static extern int CopyPair(ulong token, [Out] byte[] native, uint nativeCapacity, [Out] byte[] physical, uint physicalCapacity);
        [DllImport(Library, EntryPoint = "ma_oni_domain_pair_copy", CallingConvention = CallingConvention.Cdecl)] internal static extern int CopyPairPointers(ulong token, IntPtr native, uint nativeCapacity, IntPtr physical, uint physicalCapacity);
        [DllImport(Library, EntryPoint = "ma_oni_domain_pair_release", CallingConvention = CallingConvention.Cdecl)] internal static extern int ReleasePair(ulong token);
        [DllImport(Library, EntryPoint = "ma_oni_domain_transfer_abi_version", CallingConvention = CallingConvention.Cdecl)] internal static extern uint TransferVersion();
        [DllImport(Library, EntryPoint = "ma_oni_domain_queue_transfers", CallingConvention = CallingConvention.Cdecl)] internal static extern int QueueTransfers([In] TransferCommand[] commands, uint count, ref TransferStatus status);
        [DllImport(Library, EntryPoint = "ma_oni_domain_transfer_status", CallingConvention = CallingConvention.Cdecl)] internal static extern int CopyTransferStatus(ref TransferStatus status);
        [DllImport(Library, EntryPoint = "ma_oni_domain_copy_receipts", CallingConvention = CallingConvention.Cdecl)] internal static extern int CopyReceipts(ref Status status, [Out] Cell[] cells, uint capacity, [Out] Receipt[] receipts, uint receiptCapacity, out uint count);
        [DllImport(Library, EntryPoint = "ma_oni_domain_ack_receipts", CallingConvention = CallingConvention.Cdecl)] internal static extern int AckReceipts(ulong session, ulong throughSequence);
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
