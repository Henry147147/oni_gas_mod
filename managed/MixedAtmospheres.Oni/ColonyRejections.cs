using System;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    // The game kernel's "rejected attempts" by criterion (docs/performance-acceptance.md):
    // diagnostic only, never saved. Failures are errors of the step; swept-volume
    // refinements are ordinary adaptive time stepping.
    internal static class ColonyRejections
    {
        private const string Library = "mixed_atmospheres_oni";
        [DllImport(Library, EntryPoint = "ma_oni_colony_rejection_counts", CallingConvention = CallingConvention.Cdecl)]
        private static extern int Counts([Out] ulong[] counts);

        internal static object Report()
        {
            try
            {
                var counts = new ulong[4];
                OniNativeColony.Check(Counts(counts));
                return new
                {
                    Available = true, PressureSolveFailures = counts[0], UnconvergedPressure = counts[1],
                    NegativeOrNonFinitePressure = counts[2], SweptVolumeRefinements = counts[3]
                };
            }
            catch (EntryPointNotFoundException) { return new { Available = false }; }
        }
    }
}
