using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    internal static partial class OniNativeColony
    {
        // One gas or liquid as the colony owner receives and saves it (168
        // bytes, native/include/mixed_atmospheres/oni_colony.h). Apart from
        // the ABI calls so that engine-free controls can read saved records
        // (ColonyCatalogueDifference, tools/DistributionTests).
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
    }
}
