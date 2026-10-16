using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace MixedAtmospheres.Core
{
    internal sealed class CoupledWorldHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        internal CoupledWorldHandle(IntPtr pointer) : base(true) { SetHandle(pointer); }
        protected override bool ReleaseHandle() { CoupledNativeMethods.Destroy(handle); return true; }
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct CoupledNativeConfig
    {
        internal uint Size, Version, Width, Height;
        internal double CellSize, Depth, Gravity, Diffusivity, Viscosity, MaximumTimestep, MaximumOutgoingFraction;
        internal uint Conduction, Preconditioner, Model, Reserved;
        internal ulong Reserved2;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct CoupledNativeSpecies
    {
        internal double MolarMass, Cv, EnergyOffset, Density;
        internal uint Phase, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct CoupledNativeInput
    {
        internal double InternalEnergy, MomentumX, MomentumY, LiquidPressure;
        internal uint Wall, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct CoupledNativeCell
    {
        internal double InternalEnergy, MomentumX, MomentumY, Pressure, Temperature, LiquidFraction, Mass;
        internal uint Wall, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct CoupledNativeSnapshot
    {
        internal ulong Revision;
        internal double Time;
        internal ulong Cells, MassValues;
        internal uint Species, Reserved;
        internal double InternalEnergy, KineticEnergy, PotentialEnergy;
        internal ulong Reserved2, Reserved3;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct CoupledNativeStep
    {
        internal double Seconds, CompletedTime;
        internal ulong Revision, Substeps, RejectedSubsteps, PressureIterations, NonlinearIterations, ThermalIterations;
        internal double MaximumLinearResidualRatio, MaximumVolumeResidual, MaximumConservationError;
        internal double InitialProjectionHeat, FinalProjectionHeat, PressureWorkRoundoff;
        internal ulong Reserved, Reserved2;
    }
    internal static class CoupledNativeMethods
    {
        private const string Library = "mixed_atmospheres_coupled";
        [DllImport(Library, EntryPoint = "ma_coupled_abi_version", CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint AbiVersion();
        [DllImport(Library, EntryPoint = "ma_coupled_last_error", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr LastError();
        [DllImport(Library, EntryPoint = "ma_coupled_species_count", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SpeciesCount(out uint count);
        [DllImport(Library, EntryPoint = "ma_coupled_species_definition", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SpeciesDefinition(uint index, out CoupledNativeSpecies species);
        [DllImport(Library, EntryPoint = "ma_coupled_species_id", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SpeciesId(uint index, [Out] byte[]? output, ulong capacity, out ulong required);
        [DllImport(Library, EntryPoint = "ma_coupled_create", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Create(in CoupledNativeConfig config, [In] CoupledNativeInput[] cells, ulong cellCount,
            [In] double[] masses, ulong massCount, out IntPtr world);
        [DllImport(Library, EntryPoint = "ma_coupled_destroy", CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Destroy(IntPtr world);
        [DllImport(Library, EntryPoint = "ma_coupled_get_config", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Config(CoupledWorldHandle world, out CoupledNativeConfig config);
        [DllImport(Library, EntryPoint = "ma_coupled_advance", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Advance(CoupledWorldHandle world, double seconds, ulong budget, out IntPtr candidate, out CoupledNativeStep result);
        [DllImport(Library, EntryPoint = "ma_coupled_copy_snapshot", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Snapshot(CoupledWorldHandle world, out CoupledNativeSnapshot snapshot,
            [Out] CoupledNativeCell[] cells, ulong cellCapacity, [Out] double[] masses, ulong massCapacity);
        internal static void Check(int status)
        {
            if (status == 0) return;
            throw new FluidSimulationException(status, Marshal.PtrToStringUTF8(LastError()) ?? "Unknown coupled native failure");
        }
        internal static void CheckAbi()
        {
            if (IntPtr.Size != 8 || Marshal.SizeOf<CoupledNativeConfig>() != 96 || Marshal.SizeOf<CoupledNativeSpecies>() != 40 ||
                Marshal.SizeOf<CoupledNativeInput>() != 40 || Marshal.SizeOf<CoupledNativeCell>() != 64 ||
                Marshal.SizeOf<CoupledNativeSnapshot>() != 80 || Marshal.SizeOf<CoupledNativeStep>() != 128 || AbiVersion() != 1)
                throw new NotSupportedException("Coupled native ABI or structure layout differs.");
        }
    }
}
