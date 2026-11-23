// The public face-force class (docs/face-forces.md) against the real native
// backend, bound through reflection exactly as another mod binds it. No game,
// and so no colony owner (the backend starts one only with the game's
// simulation attached): this checks the surface, the refusals and that every
// native export is reached. Handles and forces on a running owner are covered
// by the native tests (oni_colony, mac_game_face_forces) and the game scenario.
using System.Reflection;
using System.Runtime.InteropServices;
using MixedAtmospheres.Oni.Interop;

internal static class Program
{
    private const string Library = "mixed_atmospheres_oni";
    private static void Check(bool ok, string what) { if (!ok) throw new Exception(what); }
    private static bool Throws<T>(Action call) where T : Exception { try { call(); } catch (T) { return true; } return false; }

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length != 1) throw new ArgumentException("native backend path required");
            var path = Path.GetFullPath(args[0]);
            NativeLibrary.SetDllImportResolver(typeof(Program).Assembly, (name, _, _) => name == Library ? NativeLibrary.Load(path) : IntPtr.Zero);

            // The documented surface, found the way another mod finds it.
            var assembly = AppDomain.CurrentDomain.GetAssemblies().First(a => a.GetName().Name == "MixedAtmospheres.Oni");
            var type = assembly.GetType("MixedAtmospheres.Oni.Interop.FaceForces", false) ?? throw new Exception("the class is not found by its documented name");
            const BindingFlags flags = BindingFlags.Public | BindingFlags.Static;
            Check(type.IsPublic && type.IsAbstract && type.IsSealed, "FaceForces is not a public static class");
            Check((int)type.GetProperty("ApiVersion", flags)!.GetValue(null)! == 1, "ApiVersion is not 1");
            var active = (Func<bool>)Delegate.CreateDelegate(typeof(Func<bool>), type.GetProperty("Active", flags)!.GetGetMethod()!);
            var open = (Func<int>)Delegate.CreateDelegate(typeof(Func<int>), type.GetMethod("Open", flags, null, Type.EmptyTypes, null)!);
            var set = (Func<int, int, int, int, double, double, bool>)Delegate.CreateDelegate(typeof(Func<int, int, int, int, double, double, bool>),
                type.GetMethod("Set", flags, null, new[] { typeof(int), typeof(int), typeof(int), typeof(int), typeof(double), typeof(double) }, null)!);
            var close = (Action<int>)Delegate.CreateDelegate(typeof(Action<int>), type.GetMethod("Close", flags, null, new[] { typeof(int) }, null)!);
            var takeWork = (Func<int, double>)Delegate.CreateDelegate(typeof(Func<int, double>), type.GetMethod("TakeWork", flags, null, new[] { typeof(int) }, null)!);
            var names = type.GetMembers(flags | BindingFlags.DeclaredOnly).Select(m => m.Name).Where(n => !n.StartsWith("get_")).OrderBy(n => n, StringComparer.Ordinal).ToArray();
            Check(names.SequenceEqual(new[] { "Active", "ApiVersion", "Close", "MaximumFreeSpeedMps", "MaximumStallPressurePa", "Open", "Set", "TakeWork" }),
                "the public members are " + string.Join(", ", names));

            // Before any colony of this process: inactive, and nothing loads or throws.
            Check(!active() && open() == 0 && !set(1, 0, 1, 0, 60, 4) && takeWork(1) == 0, "the interface is not inert before a colony");
            close(1);
            // Arguments outside the contract are the caller's bug, with or without a colony.
            foreach (var (dx, dy, pa, mps) in new[] { (1, 1, 60.0, 4.0), (0, 0, 60.0, 4.0), (-2, 0, 60.0, 4.0), (0, int.MinValue, 60.0, 4.0), (1, 0, -1.0, 4.0),
                (1, 0, 10000.5, 4.0), (1, 0, 60.0, 100.5), (1, 0, double.NaN, 4.0), (1, 0, 60.0, double.NegativeInfinity) })
                Check(Throws<ArgumentException>(() => set(1, 0, dx, dy, pa, mps)), $"({dx}, {dy}, {pa}, {mps}) was accepted");

            // With the native backend loaded and no colony owner (only the game
            // can start one): every export is reached and answers as documented.
            FaceForces.NativeStarted();
            Check(!active() && open() == 0, "the backend offers face forces without a colony owner");
            foreach (var cell in new[] { 0, -7, 1234, int.MaxValue })
                Check(!set(5, cell, 1, 0, 60, 4) && !set(5, cell, 0, -1, 10000, 100) && !set(5, cell, -1, 0, 0, 0), "an unknown handle was set");
            Check(takeWork(5) == 0, "an unknown handle reported work");
            close(5);
            Check(Throws<ArgumentException>(() => set(5, 0, 1, 1, 60, 4)) && Throws<ArgumentOutOfRangeException>(() => set(5, 0, 1, 0, 60, 101)),
                "an argument outside the contract was accepted with the backend loaded");
            Console.WriteLine("PASS face force interop: the documented class binds by reflection with exactly its documented members; it is inert before a colony; " +
                "arguments outside the contract throw ArgumentException; all six native exports are reached and refuse handles without a colony owner.");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
