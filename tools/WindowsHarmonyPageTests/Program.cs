using System;
using System.Reflection;
using HarmonyLib;

internal static class Program
{
    private static void Check(bool passed, string message)
    {
        if (!passed) throw new InvalidOperationException(message);
    }

    private static void CheckPages(bool guarded)
    {
        var type = typeof(Harmony).Assembly.GetType("MonoMod.Core.Platforms.Systems.WindowsSystem+PageAllocator", true);
        var allocator = Activator.CreateInstance(type, true);
        var pointer = typeof(IntPtr);
        var output = pointer.MakeByRefType();
        var at = type.GetMethod("TryAllocatePage", new[] { pointer, pointer, typeof(bool), output });
        var any = type.GetMethod("TryAllocatePage", new[] { pointer, typeof(bool), output });
        var free = type.GetMethod("TryFreePage");
        var size = new IntPtr(65536);

        void Release(IntPtr address)
        {
            Check(address != IntPtr.Zero && (bool)free.Invoke(allocator, new object[] { address, null }),
                "Could not release the test's data page.");
        }

        object[] request = { IntPtr.Zero, size, false, IntPtr.Zero };
        bool success = (bool)at.Invoke(allocator, request);
        var actual = (IntPtr)request[3];
        if (success) Release(actual);
        Check(guarded ? !success && actual == IntPtr.Zero : success && actual != IntPtr.Zero,
            "Unexpected result for a positioned request at address zero.");

        object[] ordinary = { size, false, IntPtr.Zero };
        Check((bool)any.Invoke(allocator, ordinary), "Unpositioned data allocation failed.");
        var address = (IntPtr)ordinary[2];
        Release(address);

        request[0] = address;
        Check((bool)at.Invoke(allocator, request), "A valid positioned request failed.");
        actual = (IntPtr)request[3];
        Release(actual);
        Check(actual == address, "A positioned allocation returned a different page.");
    }

    private static int Main()
    {
        var file = Environment.GetEnvironmentVariable("MA_TEST_WINDOWS_ADAPTER");
        if (string.IsNullOrEmpty(file)) throw new ArgumentException("Set MA_TEST_WINDOWS_ADAPTER to the packaged adapter DLL.");
        CheckPages(false);
        var adapter = Assembly.LoadFrom(file);
        var type = adapter.GetType("MixedAtmospheres.Oni.WindowsHarmonyPageAllocation", true);
        var owner = new Harmony("MixedAtmospheres.WindowsHarmonyPageTests");
        try
        {
            type.GetMethod("Install", BindingFlags.Static | BindingFlags.NonPublic).Invoke(null, new object[] { owner });
            CheckPages(true);
        }
        finally
        {
            owner.UnpatchAll(owner.Id);
        }
        CheckPages(false);
        Console.WriteLine("Windows page allocation passed before installation, with the packaged guard, and after removal.");
        return 0;
    }
}
