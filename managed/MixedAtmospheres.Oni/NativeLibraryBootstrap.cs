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
