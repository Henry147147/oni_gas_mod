using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace MixedAtmospheres.Oni.Interop
{
    /// <summary>
    /// Face forces: the public interface another mod uses to push the fluid on a cell face of a
    /// Mixed Atmospheres colony, a fan for example. See docs/face-forces.md.
    ///
    /// A handle drives the face between a cell and one of its four neighbours with
    ///   F = A · stallPressurePa · clamp(1 − u / freeSpeedMps, 0, 1)
    /// along the step towards that neighbour, where u is the fluid's velocity through the face in
    /// that direction and A the solver's area of the face (1 m²). The force's impulse and work are
    /// booked in the colony's momentum and energy accounts; the work is what TakeWork returns.
    ///
    /// Names, parameter types and order are a contract: later versions add members and never
    /// change these. Only primitives cross it, so a mod can bind it by reflection without a
    /// compile-time reference. The members may be called from the game's main thread at any time,
    /// never wait for the solver, and throw only <see cref="ArgumentException"/> for arguments
    /// outside the ranges stated on <see cref="Set"/>.
    /// </summary>
    public static class FaceForces
    {
        /// <summary>The interface version: 1.</summary>
        public static int ApiVersion => 1;

        /// <summary>
        /// True while the colony owner runs: the current colony's fluids belong to Mixed Atmospheres
        /// and face forces are applied. False at the main menu, in a colony that keeps the game's
        /// own simulation, and after a fluid fault.
        /// </summary>
        public static bool Active
        {
            get
            {
                if (!Ready()) return false;
                try { return Native.Active() != 0; }
                catch (Exception error) when (NativeMissing(error)) { return Unavailable(); }
            }
        }

        /// <summary>
        /// A new handle (never 0) for one driven face, with no force yet. Returns 0 while not
        /// <see cref="Active"/>.
        /// </summary>
        public static int Open()
        {
            if (!Ready()) return 0;
            try { return unchecked((int)Native.Open()); }
            catch (Exception error) when (NativeMissing(error)) { Unavailable(); return 0; }
        }

        /// <summary>
        /// Drives the face between <paramref name="cell"/> (the game's grid cell index,
        /// Grid.PosToCell) and the cell at (<paramref name="dx"/>, <paramref name="dy"/>) from it,
        /// pushing that way, from the next solver frame until the next Set or Close of the handle.
        /// (dx, dy) is exactly one of (1,0), (-1,0), (0,1), (0,-1), y upwards.
        /// <paramref name="stallPressurePa"/> is 0 to 10 000 and <paramref name="freeSpeedMps"/>
        /// 0 to 100; with either 0 the handle applies no force and stays open.
        /// Returns false, changing nothing, when the handle is not known (the colony was unloaded
        /// or faulted since it was opened). A face that cannot be driven (a cell outside the
        /// colony, a solid neighbour, a face closed to flow) is not an error: Set returns true and
        /// no force acts while that lasts.
        /// </summary>
        /// <exception cref="ArgumentException">A step that is not a unit step, or a strength that is
        /// negative, not finite or above its limit.</exception>
        public static bool Set(int handle, int cell, int dx, int dy, double stallPressurePa, double freeSpeedMps)
        {
            if (Math.Abs((long)dx) + Math.Abs((long)dy) != 1)
                throw new ArgumentException("(dx, dy) must be one of (1,0), (-1,0), (0,1), (0,-1).");
            if (!(stallPressurePa >= 0 && stallPressurePa <= MaximumStallPressurePa))
                throw new ArgumentOutOfRangeException(nameof(stallPressurePa), "The stall pressure must be from 0 to 10000 Pa.");
            if (!(freeSpeedMps >= 0 && freeSpeedMps <= MaximumFreeSpeedMps))
                throw new ArgumentOutOfRangeException(nameof(freeSpeedMps), "The free speed must be from 0 to 100 m/s.");
            if (handle == 0 || !Ready()) return false;
            uint result;
            try { result = Native.Set(unchecked((uint)handle), cell, dx, dy, stallPressurePa, freeSpeedMps); }
            catch (Exception error) when (NativeMissing(error)) { Unavailable(); return false; }
            if (result == 2) throw new ArgumentException("The face force was refused as outside its ranges.");
            return result == 1;
        }

        /// <summary>Stops and forgets the handle. Unknown handles are ignored.</summary>
        public static void Close(int handle)
        {
            if (handle == 0 || !Ready()) return;
            try { Native.Close(unchecked((uint)handle)); }
            catch (Exception error) when (NativeMissing(error)) { Unavailable(); }
        }

        /// <summary>
        /// Joules the handle's force did on the fluid in the solver frames completed since the
        /// previous call for that handle (or since Open): the work the colony's energy ledger
        /// booked, negative when fluid is driven backwards through the face. 0 for an unknown handle.
        /// </summary>
        public static double TakeWork(int handle)
        {
            if (handle == 0 || !Ready()) return 0;
            try
            {
                var joules = Native.TakeWork(unchecked((uint)handle));
                return double.IsNaN(joules) || double.IsInfinity(joules) ? 0 : joules;
            }
            catch (Exception error) when (NativeMissing(error)) { Unavailable(); return 0; }
        }

        /// <summary>The largest stall pressure Set accepts, in pascals.</summary>
        public const double MaximumStallPressurePa = 10000;
        /// <summary>The largest free speed Set accepts, in metres per second.</summary>
        public const double MaximumFreeSpeedMps = 100;

        // The native library is loaded by the colony owner. Until it has started one colony in
        // this process there is nothing to ask, and nothing here may trigger a library load.
        private static int state; // 0 not started, 1 ready, 2 unavailable (an older native library)

        internal static void NativeStarted()
        {
            if (Volatile.Read(ref state) != 0) return;
            var ready = false;
            try { ready = Native.Version() >= 1; }
            catch (Exception error) when (NativeMissing(error)) { }
            Interlocked.CompareExchange(ref state, ready ? 1 : 2, 0);
        }

        private static bool Ready() => Volatile.Read(ref state) == 1;
        private static bool Unavailable() { Volatile.Write(ref state, 2); return false; }
        private static bool NativeMissing(Exception error) =>
            error is DllNotFoundException || error is EntryPointNotFoundException || error is BadImageFormatException;

        private static class Native
        {
            private const string Library = "mixed_atmospheres_oni";
            [DllImport(Library, EntryPoint = "ma_oni_colony_face_force_version", CallingConvention = CallingConvention.Cdecl)]
            internal static extern uint Version();
            [DllImport(Library, EntryPoint = "ma_oni_colony_face_forces_active", CallingConvention = CallingConvention.Cdecl)]
            internal static extern uint Active();
            [DllImport(Library, EntryPoint = "ma_oni_colony_face_force_open", CallingConvention = CallingConvention.Cdecl)]
            internal static extern uint Open();
            [DllImport(Library, EntryPoint = "ma_oni_colony_face_force_set", CallingConvention = CallingConvention.Cdecl)]
            internal static extern uint Set(uint handle, long publicCell, int dx, int dy, double stallPressurePa, double freeSpeedMps);
            [DllImport(Library, EntryPoint = "ma_oni_colony_face_force_close", CallingConvention = CallingConvention.Cdecl)]
            internal static extern void Close(uint handle);
            [DllImport(Library, EntryPoint = "ma_oni_colony_face_force_take_work", CallingConvention = CallingConvention.Cdecl)]
            internal static extern double TakeWork(uint handle);
        }
    }
}
