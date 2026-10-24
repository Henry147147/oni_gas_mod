using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    // ONI treats every root-level *.dll as a managed assembly. Keep native
    // libraries in a platform subdirectory and preload by absolute path. The
    // module stays loaded for the process lifetime, including SafeHandle cleanup.
    internal static class NativeLibraryBootstrap
    {
        private static IntPtr module;
        private static IntPtr coupledReferenceModule;
        private static IntPtr observerModule;
        internal static bool ObserverLoaded => observerModule != IntPtr.Zero;
        // The package carries the native backend for this platform.
        internal static bool ObserverAvailable(string modDirectory) =>
            IntPtr.Size == 8 && (RuntimeInformation.IsOSPlatform(OSPlatform.Linux) ? File.Exists(Path.Combine(modDirectory, "native", "linux-x64", "libmixed_atmospheres_oni.so")) :
            RuntimeInformation.IsOSPlatform(OSPlatform.Windows) && File.Exists(Path.Combine(modDirectory, "native", "win-x64", "mixed_atmospheres_oni.dll")));
        internal static void LoadObserver(string modDirectory)
        {
            if (observerModule != IntPtr.Zero) return;
            if (IntPtr.Size != 8) throw new PlatformNotSupportedException("A 64-bit native observer is required.");
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                var file = Path.GetFullPath(Path.Combine(modDirectory, "native", "win-x64", "mixed_atmospheres_oni.dll"));
                if (!File.Exists(file)) throw new FileNotFoundException("Native observer is missing", file);
                observerModule = LoadLibraryEx(file, IntPtr.Zero, 0x00000100 | 0x00001000);
                if (observerModule == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot load native observer");
            }
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            {
                var file = Path.GetFullPath(Path.Combine(modDirectory, "native", "linux-x64", "libmixed_atmospheres_oni.so"));
                if (!File.Exists(file)) throw new FileNotFoundException("Native observer is missing", file);
                observerModule = Dlopen(file, 2 | 0x100);
                if (observerModule == IntPtr.Zero) throw new DllNotFoundException("Cannot load native observer: " + Marshal.PtrToStringAnsi(Dlerror()));
            }
            else throw new PlatformNotSupportedException("Native observation targets Windows and Linux x64.");
        }
        internal static void LoadCoupledReference(string modDirectory)
        {
            if (coupledReferenceModule != IntPtr.Zero) return;
            if (IntPtr.Size != 8) throw new PlatformNotSupportedException("A 64-bit host is required.");
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                var path = Path.GetFullPath(Path.Combine(modDirectory, "native", "win-x64", "mixed_atmospheres_coupled.dll"));
                if (!File.Exists(path)) throw new FileNotFoundException("Explicit coupled reference is missing", path);
                coupledReferenceModule = LoadLibraryEx(path, IntPtr.Zero, 0x00000100 | 0x00001000);
                if (coupledReferenceModule == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot load " + path);
            }
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            {
                var path = Path.GetFullPath(Path.Combine(modDirectory, "native", "linux-x64", "libmixed_atmospheres_coupled.so"));
                if (!File.Exists(path)) throw new FileNotFoundException("Explicit coupled reference is missing", path);
                coupledReferenceModule = Dlopen(path, 2 | 0x100);
                if (coupledReferenceModule == IntPtr.Zero)
                    throw new DllNotFoundException("Cannot load " + path + ": " + Marshal.PtrToStringAnsi(Dlerror()));
            }
            else throw new PlatformNotSupportedException("This mod targets Windows and Linux x64.");
        }
        internal static void Load(string modDirectory)
        {
            if (module != IntPtr.Zero) return;
            if (IntPtr.Size != 8) throw new PlatformNotSupportedException("A 64-bit host is required.");
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                var path = Path.GetFullPath(Path.Combine(modDirectory, "native", "win-x64", "mixed_atmospheres.dll"));
                if (!File.Exists(path)) throw new FileNotFoundException("Windows native solver is missing", path);
                // Search dependencies in this library's folder and the normal
                // system/application directories, without changing global PATH.
                module = LoadLibraryEx(path, IntPtr.Zero, 0x00000100 | 0x00001000);
                if (module == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot load " + path);
            }
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            {
                var path = Path.GetFullPath(Path.Combine(modDirectory, "native", "linux-x64", "libmixed_atmospheres.so"));
                if (!File.Exists(path)) throw new FileNotFoundException("Linux native solver is missing", path);
                module = Dlopen(path, 2 | 0x100); // RTLD_NOW | RTLD_GLOBAL; CMake supplies a matching SONAME.
                if (module == IntPtr.Zero)
                    throw new DllNotFoundException("Cannot load " + path + ": " + Marshal.PtrToStringAnsi(Dlerror()));
            }
            else throw new PlatformNotSupportedException("This mod targets Windows and Linux x64.");
        }

        [DllImport("kernel32.dll", EntryPoint = "LoadLibraryExW", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryEx(string path, IntPtr reserved, uint flags);
        [DllImport("libdl.so.2", EntryPoint = "dlopen")]
        private static extern IntPtr Dlopen(string path, int flags);
        [DllImport("libdl.so.2", EntryPoint = "dlerror")]
        private static extern IntPtr Dlerror();
    }
}
