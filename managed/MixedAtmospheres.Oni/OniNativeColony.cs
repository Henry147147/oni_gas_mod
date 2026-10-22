using System;
using System.Runtime.InteropServices;
using System.Text;

namespace MixedAtmospheres.Oni
{
    // Client of the colony owner ABI (native/include/mixed_atmospheres/oni_colony.h).
    internal static class OniNativeColony
    {
        private const string Library = "mixed_atmospheres_oni";
        internal const uint AbiVersion = 1, PhysicsConduction = 1, GameFloor = 2;
        internal const uint Solid = 0, Fluid = 1, Space = 2;
        internal const uint Inject = 1, Extract = 2, ExtractGas = 3, ExtractLiquid = 4, Byproduct = 5, ConsumerFlag = 0x100;
        internal const uint ViewGas = 1, ViewLiquid = 2, ViewOwned = 4;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        internal struct Material
        {
            public uint Size, Version; public int ElementHash; public uint NativeIndex, Phase, Reserved;
            public double MolarMassKgPerMol, SpecificHeatJPerKgK, ConductivityWPerMK, DensityKgPerM3;
            public double LowTemperatureK, HighTemperatureK;
            public int LowTarget, HighTarget, LowByproduct, HighByproduct;
            public double LowByproductFraction, HighByproductFraction;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string Id;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Config
        {
            public uint Size, Version, Flags, VacuumIndex, GridWidth, GridHeight, OffsetX, OffsetY, Width, Height;
            public double GravityMetresPerSecondSquared, MaximumTimestepSeconds;
        }
        [StructLayout(LayoutKind.Sequential)]
        // Germs: Disease is ONI's disease index + 1, 0 for none (docs/colony-owner.md#germs).
        internal struct Cell { public uint NativeIndex, Kind; public float MassKg, TemperatureK; public uint Disease; public int DiseaseCount; }
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
        internal struct CellView { public uint ElementIndex, Flags; public float MassKg, TemperatureK, PressurePa, LiquidFraction; public uint Disease; public int DiseaseCount; }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Transfer { public uint PublicCell, NativeIndex, Kind, Radius; public int Callback; public uint Disease; public double MassKg, TemperatureK, DiseaseCount; }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Receipt { public ulong Sequence, CompletedFrame; public int Callback; public uint Kind, NativeIndex, PublicCell; public float MassKg, TemperatureK; public uint Disease; public int DiseaseCount; }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Heat { public uint PublicCell, Reserved; public double Joules, MaximumTemperatureK; }
        internal const uint Open = 1, Close = 2, BlockLiquid = 3, PassLiquid = 4, Clear = 5;
        [StructLayout(LayoutKind.Sequential)]
        internal struct Terrain { public uint PublicCell, Kind; }

        [StructLayout(LayoutKind.Sequential)]
        internal struct Species { public uint NativeIndex, Phase; public double MolarMassKgPerMol, CvJoulesPerKgKelvin, DensityKgPerCubicMetre, EnergyOffsetJoulesPerKg; }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Reading
        {
            public uint PublicCell, FirstConstituent, Constituents, Reserved;
            public double TemperatureKelvin, MechanicalPressurePa, GasPressurePa, InternalEnergyJoules, OccupiedVolumeCubicMetres, LiquidVolumeCubicMetres;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Constituent { public uint Species, Reserved; public double MassKg, PartialVolumeCubicMetres; }

        // ONI's disease byte (255: none) and the owner's germ identity.
        internal static uint Germ(byte diseaseIdx) => diseaseIdx == byte.MaxValue ? 0u : diseaseIdx + 1u;
        internal static byte DiseaseIdx(uint germ) => germ == 0 || germ > 255 ? byte.MaxValue : (byte)(germ - 1);
        internal static void CheckLayouts()
        {
            if (Marshal.SizeOf<Material>() != 168 || Marshal.SizeOf<Config>() != 56 || Marshal.SizeOf<Cell>() != 24 || Marshal.SizeOf<Status>() != 600 ||
                Marshal.SizeOf<CellView>() != 32 || Marshal.SizeOf<Transfer>() != 48 || Marshal.SizeOf<Receipt>() != 48 || Marshal.SizeOf<Heat>() != 24 ||
                Marshal.SizeOf<Species>() != 40 || Marshal.SizeOf<Reading>() != 64 || Marshal.SizeOf<Constituent>() != 24)
                throw new InvalidOperationException("Colony owner ABI layouts differ from the native library.");
            if (Version() != AbiVersion) throw new InvalidOperationException("Colony owner ABI version differs from the native library.");
        }

        [DllImport(Library, EntryPoint = "ma_oni_colony_abi_version", CallingConvention = CallingConvention.Cdecl)] internal static extern uint Version();
        [DllImport(Library, EntryPoint = "ma_oni_colony_start", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Start(ref Config config, [In] Material[] materials, uint materialCount, [In] Cell[] cells, uint cellCount, ref Status status);
        [DllImport(Library, EntryPoint = "ma_oni_colony_restore", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Restore(ref Config config, [In] Material[] materials, uint materialCount, [In] byte[] image, uint bytes, ref Status status);
        [DllImport(Library, EntryPoint = "ma_oni_colony_status", CallingConvention = CallingConvention.Cdecl)] internal static extern int CopyStatus(ref Status status);
        [DllImport(Library, EntryPoint = "ma_oni_colony_copy", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Copy(ref Status status, [Out] CellView[] views, uint capacity);
        [DllImport(Library, EntryPoint = "ma_oni_colony_composition", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Composition(uint cell, [Out] uint[] indices, [Out] double[] masses, uint capacity, out uint count);
        [DllImport(Library, EntryPoint = "ma_oni_colony_queue_transfers", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int QueueTransfers([In] Transfer[] transfers, uint count);
        [DllImport(Library, EntryPoint = "ma_oni_colony_queue_heat", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int QueueHeat([In] Heat[] heat, uint count);
        [DllImport(Library, EntryPoint = "ma_oni_colony_queue_terrain", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int QueueTerrain([In] Terrain[] terrain, uint count);
        [DllImport(Library, EntryPoint = "ma_oni_colony_copy_receipts", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int CopyReceipts([Out] Receipt[] receipts, uint capacity, out uint count);
        [DllImport(Library, EntryPoint = "ma_oni_colony_ack_receipts", CallingConvention = CallingConvention.Cdecl)] internal static extern int AckReceipts(ulong through);
        [DllImport(Library, EntryPoint = "ma_oni_colony_checkpoint", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Checkpoint([Out] byte[]? buffer, uint capacity, out uint bytes);
        [DllImport(Library, EntryPoint = "ma_oni_colony_stop", CallingConvention = CallingConvention.Cdecl)] internal static extern int Stop(ref Status status);
        [DllImport(Library, EntryPoint = "ma_oni_colony_active", CallingConvention = CallingConvention.Cdecl)] internal static extern uint Active();
        [DllImport(Library, EntryPoint = "ma_oni_colony_species", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SpeciesList([Out] Species[]? species, uint capacity, out uint count);
        [DllImport(Library, EntryPoint = "ma_oni_colony_readings", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Readings(out ulong frame, [Out] Reading[]? readings, uint capacity, out uint count,
            [Out] Constituent[]? constituents, uint constituentCapacity, out uint constituentCount);
        [DllImport(Library, EntryPoint = "ma_oni_colony_busy", CallingConvention = CallingConvention.Cdecl)] internal static extern uint Busy();
        [DllImport(Library, EntryPoint = "ma_oni_colony_liquid_density", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int LiquidDensity([In] Material[] materials, uint count, uint nativeIndex, double pressurePa, out double densityKgPerCubicMetre);
        [DllImport(Library, EntryPoint = "ma_oni_last_error", CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr LastError();
        internal static void Check(int result)
        {
            if (result != 0) throw new InvalidOperationException("Colony owner call failed (" + result + "): " + (Marshal.PtrToStringAnsi(LastError()) ?? ""));
        }
    }
}
