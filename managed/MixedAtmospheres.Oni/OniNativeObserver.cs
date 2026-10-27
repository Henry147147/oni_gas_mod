using System;
using System.Runtime.InteropServices;

namespace MixedAtmospheres.Oni
{
    // Shared by the optional Unity observer and the isolated process harness.
    // The native module must already be preloaded from its absolute mod path.
    // Observation never grants simulation ownership or changes native messages.
    internal sealed class OniNativeObserver : IDisposable
    {
        private const string Library = "mixed_atmospheres_oni";
        private const int HookCount = 29;
        private bool disposed;
        private readonly string gameDirectory;
        internal static readonly string[] HookNames = {
            "initialize", "shutdown", "message", "messages", "begin-save", "end-save", "main", "process-frame",
            "next-frame", "update-data", "pressure", "liquid", "temperature", "state-transition", "load-transition",
            "sublimation", "post-process", "step", "begin-frame", "end-frame", "consumer", "emitter", "building-heat",
            "gas-interaction", "gas-interaction-pair", "gas-displacement", "liquid-displacement", "emitter-dispatch", "chunk-heat"
        };

        [StructLayout(LayoutKind.Sequential)]
        private struct Config
        {
            internal uint Size, Version, Flags, Reserved;
            internal IntPtr GameDirectory;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct HookStatistics
        {
            public ulong Entries;
            public ulong Exits;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct Status
        {
            public uint Size, Version, OniBuild, Attached, AuthoritativeOwner, RecoveryRequired;
            public ulong InstalledMask, ActiveCalls, PositiveTimestepReturns, TraceReserved, TraceDropped;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = HookCount)]
            public HookStatistics[] Hooks;
            internal static Status Empty => new Status { Size = 528, Version = 4, Hooks = new HookStatistics[HookCount] };
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct TraceEvent
        {
            public ulong Sequence, Thread;
            public uint Hook, Entering, ValueBits, Reserved;
        }
        internal sealed class Reading
        {
            public bool ForwardingOnly { get; }
            public bool AuthoritativeFluidOwner => false;
            public uint OniBuild { get; }
            public bool Attached { get; }
            public bool RecoveryRequired { get; }
            public ulong InstalledMask { get; }
            public ulong ActiveCalls { get; }
            public ulong PositiveTimestepReturns { get; }
            public ulong TraceEventsReserved { get; }
            public ulong TraceEventsDropped { get; }
            public HookReading[] Hooks { get; }
            internal Reading(Status status)
            {
                if (status.AuthoritativeOwner != 0) throw new InvalidOperationException("A forwarding observer reported fluid authority.");
                ForwardingOnly = DomainActive() == 0;
                OniBuild = status.OniBuild; Attached = status.Attached != 0; InstalledMask = status.InstalledMask;
                RecoveryRequired = status.RecoveryRequired != 0;
                ActiveCalls = status.ActiveCalls; PositiveTimestepReturns = status.PositiveTimestepReturns;
                TraceEventsReserved = status.TraceReserved; TraceEventsDropped = status.TraceDropped;
                Hooks = new HookReading[HookCount];
                for (var i = 0; i < Hooks.Length; ++i) Hooks[i] = new HookReading(HookNames[i], status.Hooks[i]);
            }
        }
        internal sealed class HookReading
        {
            public string Name { get; }
            public ulong Entries { get; }
            public ulong Exits { get; }
            internal HookReading(string name, HookStatistics counts) { Name = name; Entries = counts.Entries; Exits = counts.Exits; }
        }

        internal OniNativeObserver(string directory)
        {
            if (IntPtr.Size != 8 || Marshal.SizeOf<Config>() != 24 || Marshal.SizeOf<Status>() != 528 || Marshal.SizeOf<TraceEvent>() != 32)
                throw new PlatformNotSupportedException("Native observer ABI layouts differ.");
            if (NativeVersion() != 4) throw new NotSupportedException("Unsupported native observer ABI; rebuild the managed and native backend together.");
            gameDirectory = directory ?? throw new ArgumentNullException(nameof(directory));
            WithConfig((ref Config config) => {
                var status = Status.Empty;
                Check(Probe(ref config, ref status));
                Check(Attach(ref config, ref status));
            });
        }
        // Null when the native library accepts the game's SimDLL; otherwise
        // its reason. Hashes files only: nothing is attached or changed.
        internal static string? ProbeGame(string directory)
        {
            if (NativeVersion() != 4) return "the mod's native library is from a different build of the mod";
            var path = Marshal.StringToCoTaskMemUTF8(directory);
            try
            {
                var config = new Config { Size = 24, Version = 4, Flags = 1, GameDirectory = path };
                var status = Status.Empty;
                var result = Probe(ref config, ref status);
                return result == 0 ? null : result == 2 ? Marshal.PtrToStringAnsi(LastError()) : throw new InvalidOperationException("Native observer " + result + ": " + Marshal.PtrToStringAnsi(LastError()));
            }
            finally { Marshal.FreeCoTaskMem(path); }
        }
        private delegate void ConfigAction(ref Config config);
        private void WithConfig(ConfigAction action)
        {
            var path = Marshal.StringToCoTaskMemUTF8(gameDirectory);
            try
            {
                var config = new Config { Size = 24, Version = 4, Flags = 1, GameDirectory = path };
                action(ref config);
            }
            finally { Marshal.FreeCoTaskMem(path); }
        }
        internal Reading Capture() => CaptureStatus();
        internal static Reading CaptureStatus()
        {
            var status = Status.Empty;
            Check(GetStatus(ref status));
            return new Reading(status);
        }
        internal TraceEvent[] CaptureTrace()
        {
            var records = new TraceEvent[4096];
            uint count = 0;
            Check(CopyTrace(records, (uint)records.Length, ref count));
            Array.Resize(ref records, checked((int)count));
            return records;
        }
        internal void VerifyTraceBufferRejection()
        {
            var records = new[] { new TraceEvent { Sequence = ulong.MaxValue } };
            uint count = uint.MaxValue;
            if (CopyTrace(records, 1, ref count) != 5 || count != uint.MaxValue || records[0].Sequence != ulong.MaxValue)
                throw new InvalidOperationException("Undersized native trace capture changed its outputs.");
        }
        internal void VerifyLiveDetachRejected()
        {
            if (Detach() != 3) throw new InvalidOperationException("Observer did not refuse live native detachment.");
            WithConfig((ref Config config) => {
                var output = Status.Empty;
                if (Attach(ref config, ref output) != 3) throw new InvalidOperationException("Duplicate observer attachment was accepted.");
            });
        }
        public void Dispose()
        {
            if (disposed) return;
            Check(Detach());
            disposed = true;
        }
        private static void Check(int result)
        {
            if (result != 0) throw new InvalidOperationException("Native observer " + result + ": " + Marshal.PtrToStringAnsi(LastError()));
        }
        [DllImport(Library, EntryPoint = "ma_oni_abi_version", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint NativeVersion();
        [DllImport(Library, EntryPoint = "ma_oni_domain_active", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint DomainActive();
        [DllImport(Library, EntryPoint = "ma_oni_probe", CallingConvention = CallingConvention.Cdecl)]
        private static extern int Probe(ref Config config, ref Status output);
        [DllImport(Library, EntryPoint = "ma_oni_attach_observer", CallingConvention = CallingConvention.Cdecl)]
        private static extern int Attach(ref Config config, ref Status output);
        [DllImport(Library, EntryPoint = "ma_oni_observer_status", CallingConvention = CallingConvention.Cdecl)]
        private static extern int GetStatus(ref Status output);
        [DllImport(Library, EntryPoint = "ma_oni_copy_trace", CallingConvention = CallingConvention.Cdecl)]
        private static extern int CopyTrace([In, Out] TraceEvent[] output, uint capacity, ref uint count);
        [DllImport(Library, EntryPoint = "ma_oni_detach_observer", CallingConvention = CallingConvention.Cdecl)]
        private static extern int Detach();
        [DllImport(Library, EntryPoint = "ma_oni_last_error", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr LastError();
    }
}
