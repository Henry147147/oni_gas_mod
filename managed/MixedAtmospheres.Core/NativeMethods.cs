using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace MixedAtmospheres.Core
{
    internal sealed class WorldHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        internal WorldHandle(IntPtr pointer) : base(true) { SetHandle(pointer); }
        protected override bool ReleaseHandle() { NativeMethods.Destroy(handle); return true; }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeConfig
    {
        internal uint Width, Height;
        internal double CellSize, Depth, Gravity, Diffusivity;
        internal uint Conduction, OpenTop;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeSample
    {
        internal double Mass, Temperature, Pressure, Energy, VelocityX, VelocityY;
        internal uint Wall, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeAtmosphere
    {
        internal double MinimumOxygenFraction, MaximumOxygenFraction, MinimumOxygenPressure, MaximumCo2Fraction, MaximumCo2Pressure;
        internal uint HardMode, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeBreathing
    {
        internal double Pressure, OxygenFraction, OxygenPressure, Co2Fraction, Co2Pressure;
        internal uint Failures, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeStep { internal double Seconds; internal ulong Substeps; }

    internal static class NativeMethods
    {
        private const string Library = "mixed_atmospheres";
        [DllImport(Library, EntryPoint = "ma_abi_version", CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint AbiVersion();
        [DllImport(Library, EntryPoint = "ma_last_error", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr LastError();
        [DllImport(Library, EntryPoint = "ma_create_world", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Create(in NativeConfig config, out IntPtr world);
        [DllImport(Library, EntryPoint = "ma_destroy_world", CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Destroy(IntPtr world);
        [DllImport(Library, EntryPoint = "ma_get_config", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int GetConfig(WorldHandle world, out NativeConfig config);
        [DllImport(Library, EntryPoint = "ma_species_count", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SpeciesCount(WorldHandle world, out uint count);
        [DllImport(Library, EntryPoint = "ma_species_id", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SpeciesId(WorldHandle world, uint index, [Out] byte[]? output, ulong capacity, out ulong required);
        [DllImport(Library, EntryPoint = "ma_set_cell", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SetCell(WorldHandle world, uint x, uint y, [In] double[] masses, uint count, double temperature, double velocityX, double velocityY);
        [DllImport(Library, EntryPoint = "ma_set_wall", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SetWall(WorldHandle world, uint x, uint y, uint wall);
        [DllImport(Library, EntryPoint = "ma_get_cell", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int GetCell(WorldHandle world, uint x, uint y, out NativeSample sample, [Out] double[] masses, uint capacity);
        [DllImport(Library, EntryPoint = "ma_assess_atmosphere", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Assess(WorldHandle world, uint x, uint y, in NativeAtmosphere settings, out NativeBreathing output);
        [DllImport(Library, EntryPoint = "ma_step", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Step(WorldHandle world, double seconds, ulong budget, out NativeStep result);
        [DllImport(Library, EntryPoint = "ma_totals", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Totals(WorldHandle world, [Out] double[] masses, uint count, out double energy, [Out] double[] escaped, out double escapedEnergy);
        [DllImport(Library, EntryPoint = "ma_save", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Save(WorldHandle world, [Out] byte[]? output, ulong capacity, out ulong required);
        [DllImport(Library, EntryPoint = "ma_load", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Load([In] byte[] input, ulong size, out IntPtr output);

        internal static void Check(int status)
        {
            if (status == 0) return;
            var message = Marshal.PtrToStringUTF8(LastError()) ?? "Unknown native failure";
            throw new FluidSimulationException(status, message);
        }
    }

    public sealed class FluidSimulationException : Exception
    {
        public int Status { get; }
        internal FluidSimulationException(int status, string message) : base(message) { Status = status; }
    }
}
