using System;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    // Read-only native publications are separate from the latest owner used
    // for input reservations. Each lease names the actual exported GameData.
    internal static class OniNativePublication
    {
        private const string Library = "mixed_atmospheres_oni";
        [StructLayout(LayoutKind.Sequential)]
        internal struct Info
        {
            public uint Size, Version, State, Reserved;
            public ulong Token, Session, CompletedFrame, NativeEpoch;
            public uint Cells, Reserved2;
            public double TimeSeconds;
            internal static Info Empty => new Info { Size = 64, Version = 1 };
        }
        [DllImport(Library, EntryPoint = "ma_oni_domain_publication_abi_version", CallingConvention = CallingConvention.Cdecl)]
        internal static extern uint Version();
        [DllImport(Library, EntryPoint = "ma_oni_domain_publication_acquire", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Acquire(IntPtr nativeUpdate, ref Info info);
        [DllImport(Library, EntryPoint = "ma_oni_domain_publication_copy", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Copy(ulong token, ref OniNativeDomain.Status status, [Out] OniNativeDomain.Cell[] cells, uint capacity,
            ref OniNativeDomain.ChunkHeatStatus heat, ref OniNativeDomain.SolidHeatEndpoint endpoint, ref OniNativeDomain.SolidCellState solid);
        [DllImport(Library, EntryPoint = "ma_oni_domain_publication_release", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Release(ulong token);
        [DllImport(Library, EntryPoint = "ma_oni_domain_publication_cells", CallingConvention = CallingConvention.Cdecl)]
        internal static extern int Cells(ulong token, out IntPtr cells, out uint count);
    }
}
