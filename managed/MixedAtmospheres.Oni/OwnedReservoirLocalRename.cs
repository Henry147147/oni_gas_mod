using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace MixedAtmospheres.Oni
{
    internal enum OwnedReservoirRenameBackend : byte { WindowsNtfsRenameInfoEx = 1, LinuxRenameAt2 = 2 }

    internal sealed class OwnedReservoirRenameException : IOException
    {
        internal bool Attempted { get; }
        internal int? NativeError { get; }
        internal OwnedReservoirRenameException(string message, bool attempted, int? nativeError, Exception? inner = null)
            : base(message, inner) { Attempted = attempted; NativeError = nativeError; }
    }

    // Narrow local filesystem backend only. No game/Core calls, retries,
    // overwrite-copy fallback, metadata restoration or native-save permit.
    internal static class OwnedReservoirLocalRename
    {
        internal const int FileRenameInfoEx = 22;
        internal const uint ReplaceIfExists = 1;
        internal const uint PosixSemantics = 2;
        private const uint DeleteAccess = 0x00010000;
        private const uint ShareReadWriteDelete = 7;
        private const uint OpenExisting = 3;
        private const uint FileAttributeNormal = 0x80;
        private const int AtCurrentWorkingDirectory = -100;
        private static readonly Encoding StrictUtf8 = new UTF8Encoding(false, true);

        [StructLayout(LayoutKind.Sequential)]
        private struct RenameInfoLayout
        {
            internal uint Flags;
            internal IntPtr RootDirectory;
            internal uint FileNameLength;
            internal ushort FileName;
        }

        internal static int WindowsFileNameOffset => Marshal.OffsetOf<RenameInfoLayout>(nameof(RenameInfoLayout.FileName)).ToInt32();
        internal static int WindowsRootDirectoryOffset => Marshal.OffsetOf<RenameInfoLayout>(nameof(RenameInfoLayout.RootDirectory)).ToInt32();
        internal static int WindowsFileNameLengthOffset => Marshal.OffsetOf<RenameInfoLayout>(nameof(RenameInfoLayout.FileNameLength)).ToInt32();
        internal static int WindowsHeaderSize => Marshal.SizeOf<RenameInfoLayout>();

        internal static OwnedReservoirRenameBackend Select(string absoluteParent)
        {
            if (IntPtr.Size != 8 || !BitConverter.IsLittleEndian || RuntimeInformation.ProcessArchitecture != Architecture.X64)
                throw new PlatformNotSupportedException("The bounded rename backend supports only the verified little-endian x64 layout.");
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                RequireWindowsAbi();
                var drive = new DriveInfo(Path.GetPathRoot(absoluteParent)!);
                if (drive.DriveType != DriveType.Fixed || !string.Equals(drive.DriveFormat, "NTFS", StringComparison.OrdinalIgnoreCase))
                    throw new PlatformNotSupportedException("The first Windows rename backend requires a direct local fixed NTFS volume.");
                return OwnedReservoirRenameBackend.WindowsNtfsRenameInfoEx;
            }
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            {
                DriveInfo? selected = null;
                foreach (var drive in DriveInfo.GetDrives())
                {
                    var root = drive.RootDirectory.FullName.TrimEnd(Path.DirectorySeparatorChar);
                    if ((absoluteParent == root || absoluteParent.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.Ordinal)) &&
                        (selected == null || drive.RootDirectory.FullName.Length > selected.RootDirectory.FullName.Length)) selected = drive;
                }
                var format = selected?.DriveFormat ?? "";
                // These names share Linux's ext-family filesystem identifier;
                // this is the scoped WSL/Linux test family, not all mounts.
                if (format != "ext2/ext3" && format != "ext2" && format != "ext3" && format != "ext4")
                    throw new PlatformNotSupportedException("The first Linux rename backend requires a direct local ext-family filesystem.");
                return OwnedReservoirRenameBackend.LinuxRenameAt2;
            }
            throw new PlatformNotSupportedException("No verified local rename backend exists for this platform.");
        }

        internal static void Rename(OwnedReservoirRenameBackend backend, string stage, string target, bool replacing
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
            , bool invalidNativeRequest = false
#endif
            )
        {
            if (!Path.IsPathFullyQualified(stage) || !Path.IsPathFullyQualified(target) ||
                !string.Equals(Path.GetDirectoryName(stage), Path.GetDirectoryName(target), StringComparison.Ordinal))
                throw new OwnedReservoirRenameException("Native rename requires exact same-directory absolute paths.", false, null);
            if (backend == OwnedReservoirRenameBackend.WindowsNtfsRenameInfoEx)
            {
                var informationClass = FileRenameInfoEx;
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                if (invalidNativeRequest) informationClass = int.MaxValue;
#endif
                RenameWindows(stage, target, replacing, informationClass); return;
            }
            if (backend == OwnedReservoirRenameBackend.LinuxRenameAt2)
            {
                var flags = replacing ? 0U : 1U; // RENAME_NOREPLACE for creation.
#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
                if (invalidNativeRequest) flags = uint.MaxValue;
#endif
                RenameLinux(stage, target, flags); return;
            }
            throw new OwnedReservoirRenameException("Unknown local rename backend.", false, null);
        }

        private static void RequireWindowsAbi()
        {
            if (!BitConverter.IsLittleEndian || IntPtr.Size != 8 || WindowsRootDirectoryOffset != 8 || WindowsFileNameLengthOffset != 16 ||
                WindowsFileNameOffset != 20 || WindowsHeaderSize != 24 || FileRenameInfoEx != 22)
                throw new PlatformNotSupportedException("The native FILE_RENAME_INFO layout differs from the inspected x64 SDK contract.");
        }

        private static byte[] WindowsBuffer(string target, bool replacing)
        {
            RequireWindowsAbi();
            // Strict UTF8 validation rejects unpaired UTF16 surrogates before
            // Encoding.Unicode could substitute a different target filename.
            _ = StrictUtf8.GetByteCount(target);
            var name = Encoding.Unicode.GetBytes(target);
            var buffer = new byte[Math.Max(WindowsHeaderSize, checked(WindowsFileNameOffset + name.Length + 2))];
            var flags = replacing ? ReplaceIfExists | PosixSemantics : 0U;
            Array.Copy(BitConverter.GetBytes(flags), 0, buffer, 0, 4);
            Array.Copy(BitConverter.GetBytes(checked((uint)name.Length)), 0, buffer, WindowsFileNameLengthOffset, 4);
            Array.Copy(name, 0, buffer, WindowsFileNameOffset, name.Length);
            // RootDirectory, padding and the explicit WCHAR terminator are
            // zero. FileNameLength excludes that terminator.
            return buffer;
        }

        private static void RenameWindows(string stage, string target, bool replacing, int informationClass)
        {
            var bytes = WindowsBuffer(target, replacing);
            IntPtr buffer = IntPtr.Zero;
            var attempted = false;
            try
            {
                buffer = Marshal.AllocHGlobal(bytes.Length); Marshal.Copy(bytes, 0, buffer, bytes.Length);
                using var source = CreateFileW(stage, DeleteAccess, ShareReadWriteDelete, IntPtr.Zero, OpenExisting, FileAttributeNormal, IntPtr.Zero);
                if (source.IsInvalid)
                {
                    var error = Marshal.GetLastWin32Error();
                    throw new OwnedReservoirRenameException("Opening the verified stage for DELETE-only rename failed.", false, error, new Win32Exception(error));
                }
                attempted = true;
                var renamed = SetFileInformationByHandle(source, informationClass, buffer, checked((uint)bytes.Length));
                var nativeError = renamed ? 0 : Marshal.GetLastWin32Error();
                if (!renamed) throw new OwnedReservoirRenameException("FileRenameInfoEx failed; the outcome requires read-only reconciliation.", true, nativeError, new Win32Exception(nativeError));
            }
            catch (EntryPointNotFoundException error) { throw new OwnedReservoirRenameException("Windows rename API is unavailable; no rename entry point ran.", false, null, error); }
            catch (DllNotFoundException error) { throw new OwnedReservoirRenameException("Windows rename library is unavailable; no rename entry point ran.", false, null, error); }
            catch (OwnedReservoirRenameException) { throw; }
            catch (Exception error) { throw new OwnedReservoirRenameException("Windows rename backend failed.", attempted, null, error); }
            finally { if (buffer != IntPtr.Zero) Marshal.FreeHGlobal(buffer); }
        }

        private static void RenameLinux(string stage, string target, uint flags)
        {
            var from = StrictUtf8.GetBytes(stage + "\0"); var to = StrictUtf8.GetBytes(target + "\0");
            try
            {
                var result = RenameAt2(AtCurrentWorkingDirectory, from, AtCurrentWorkingDirectory, to, flags);
                var error = result == 0 ? 0 : Marshal.GetLastWin32Error();
                if (result != 0) throw new OwnedReservoirRenameException("renameat2 failed; no fallback or automatic retry is permitted.", true, error, new Win32Exception(error));
            }
            catch (EntryPointNotFoundException error) { throw new OwnedReservoirRenameException("libc renameat2 is unavailable; no rename entry point ran.", false, null, error); }
            catch (DllNotFoundException error) { throw new OwnedReservoirRenameException("libc is unavailable; no rename entry point ran.", false, null, error); }
            catch (OwnedReservoirRenameException) { throw; }
            catch (Exception error) { throw new OwnedReservoirRenameException("Linux rename backend failed.", true, null, error); }
        }

        // statx has a fixed Linux UAPI buffer. Check the type without opening
        // a FIFO/device that could block before complete-file validation.
        // Namespace exclusion still belongs to the caller: this is not a
        // substitute for openat-relative race-free traversal.
        [StructLayout(LayoutKind.Explicit, Size = 256)]
        private struct LinuxStatx
        {
            [FieldOffset(0)] internal uint Mask;
            [FieldOffset(28)] internal ushort Mode;
        }
        internal static void RequireRegularFileType(string path)
        {
            if (!RuntimeInformation.IsOSPlatform(OSPlatform.Linux)) return;
            if (IntPtr.Size != 8 || Marshal.SizeOf<LinuxStatx>() != 256 || Marshal.OffsetOf<LinuxStatx>(nameof(LinuxStatx.Mode)).ToInt32() != 28)
                throw new PlatformNotSupportedException("The checked Linux statx layout is unavailable.");
            var name = StrictUtf8.GetBytes(path + "\0");
            // AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT, STATX_TYPE.
            var result = Statx(AtCurrentWorkingDirectory, name, 0x100 | 0x800, 1, out var status);
            var error = result == 0 ? 0 : Marshal.GetLastWin32Error();
            if (result != 0) throw new IOException("statx could not validate the existing file type.", new Win32Exception(error));
            if ((status.Mask & 1) == 0 || (status.Mode & 0xf000) != 0x8000)
                throw new IOException("Only Linux regular default-stream files are supported.");
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint desiredAccess, uint shareMode, IntPtr securityAttributes,
            uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);
        [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetFileInformationByHandle(SafeFileHandle handle, int informationClass, IntPtr information, uint size);
        [DllImport("libc", EntryPoint = "renameat2", ExactSpelling = true, SetLastError = true)]
        private static extern int RenameAt2(int oldDirectory, [In] byte[] oldPath, int newDirectory, [In] byte[] newPath, uint flags);
        [DllImport("libc", EntryPoint = "statx", ExactSpelling = true, SetLastError = true)]
        private static extern int Statx(int directory, [In] byte[] path, int flags, uint mask, out LinuxStatx status);

#if MIXED_ATMOSPHERES_RECOVERABLE_FILE_TESTS
        internal static byte[] WindowsBufferForTest(string target, bool replacing) => WindowsBuffer(target, replacing);
#endif
    }
}
