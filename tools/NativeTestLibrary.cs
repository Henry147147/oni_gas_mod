using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using MixedAtmospheres.Core;

// Test-host-only resolver: a stale library beside a previously built test
// executable must not override the freshly built library supplied by build.py.
internal static class NativeTestLibrary
{
    internal static void PinFromEnvironment()
    {
        var requested = Environment.GetEnvironmentVariable("MA_TEST_NATIVE_LIBRARY");
        if (string.IsNullOrWhiteSpace(requested)) return;
        var path = Path.GetFullPath(requested);
        if (!File.Exists(path)) throw new FileNotFoundException("The explicitly selected native test library is missing.", path);
        var module = NativeLibrary.Load(path);
        try
        {
            NativeLibrary.SetDllImportResolver(typeof(FluidWorld).Assembly,
                (name, _, _) => name == "mixed_atmospheres" ? module : IntPtr.Zero);
        }
        catch { NativeLibrary.Free(module); throw; }
        // Keep loaded through SafeHandle cleanup at process exit.
        Console.WriteLine("Native test library SHA256: " + Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant());
    }
}
