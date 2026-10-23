using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct ColonyParcelContext
    {
        public uint Size, Version, Flags, Reserved;
        public ulong Session;
        public uint GridWidth, GridHeight, OffsetX, OffsetY, Width, Height;
        public double CellWidthMetres, CellHeightMetres, GravityMetresPerSecondSquared;
        internal static ColonyParcelContext Empty => new ColonyParcelContext { Size=72,Version=1 };
    }
}
