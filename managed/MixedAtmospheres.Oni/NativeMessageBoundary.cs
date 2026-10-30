using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Native extern methods have no IL body to prefix. Rewrite their managed
    // call sites instead. Outside the explicit probe scope these wrappers call
    // the original native entry points with exactly the original arguments.
    // This intentionally does NOT claim coverage of saves, conduit services,
    // other assemblies, or third-party mods.
    [HarmonyPatch]
    internal static class NativeMessagePatch
    {
        private static readonly MethodInfo Single = AccessTools.Method(typeof(Sim), nameof(Sim.SIM_HandleMessage));
        private static readonly MethodInfo Batch = AccessTools.Method(typeof(Sim), nameof(Sim.SIM_HandleMessages));
        private static readonly MethodInfo SingleBridge = AccessTools.Method(typeof(NativeMessageBoundary), nameof(NativeMessageBoundary.Handle));
        private static readonly MethodInfo BatchBridge = AccessTools.Method(typeof(NativeMessageBoundary), nameof(NativeMessageBoundary.HandleBatch));
        internal static int TargetCount { get; private set; }

        private static IEnumerable<MethodBase> TargetMethods()
        {
            TargetCount = 0;
            foreach (var type in new[] { typeof(Sim), typeof(SimMessages), typeof(DebugHandler), typeof(SubworldZoneRenderData) })
                foreach (var method in type.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance | BindingFlags.DeclaredOnly))
                {
                    if (method.GetMethodBody() == null) continue;
                    if (!ReadInstructions(method).Any(instruction => instruction.Calls(Single) || instruction.Calls(Batch))) continue;
                    ++TargetCount;
                    yield return method;
                }
        }

        internal static IEnumerable<CodeInstruction> ReadInstructions(MethodBase method)
        {
            // The shipped Harmony API types its optional ILGenerator through
            // Unity's mscorlib. Netstandard's facade cannot bind that signature
            // at compile time; reflection avoids importing host framework DLLs.
            var reader = typeof(PatchProcessor).GetMethods(BindingFlags.Public | BindingFlags.Static)
                .Single(candidate => candidate.Name == "GetOriginalInstructions" && candidate.GetParameters().Length == 2
                    && candidate.GetParameters()[0].ParameterType == typeof(MethodBase)
                    && !candidate.GetParameters()[1].ParameterType.IsByRef);
            return (IEnumerable<CodeInstruction>)(reader.Invoke(null, new object?[] { method, null })
                ?? throw new InvalidOperationException("Harmony could not read native call sites."));
        }

        private static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
        {
            foreach (var instruction in instructions)
            {
                if (instruction.Calls(Single)) instruction.operand = SingleBridge;
                else if (instruction.Calls(Batch)) instruction.operand = BatchBridge;
                yield return instruction;
            }
        }

        internal static void RequirePatched(MethodInfo method)
        {
            var patch = AccessTools.Method(typeof(NativeMessagePatch), nameof(Transpiler));
            if (Harmony.GetPatchInfo(method)?.Transpilers.Any(item => item.PatchMethod == patch) != true)
                throw new InvalidOperationException("Native call interception not installed: " + method);
        }
    }

    internal static unsafe class NativeMessageBoundary
    {
        internal static void ResetStoragePublication() { }
        // Preparing GameData posts the queued native frame. Finish its actual
        // thermal work before managed publication and conduit callbacks can
        // withdraw exact storage. NewGameFrame only queues the frame: waiting
        // there would prevent PrepareGameData from ever starting it.
        private static IntPtr ForwardNative(int message,int length,byte* payload)
        {
            if(!ColonyFrameLog.Enabled)return ForwardNativeUntimed(message,length,payload);
            var begun=ColonyFrameLog.Now;var collections=GC.CollectionCount(0);
            try{return ForwardNativeUntimed(message,length,payload);}
            finally{if(message==DevelopmentTelemetrySampler.PrepareGameDataMessage&&GC.CollectionCount(0)!=collections)ColonyFrameLog.Add(ColonyFrameLog.Part.NativeWaitGc,begun);
                ColonyFrameLog.Add(message==DevelopmentTelemetrySampler.PrepareGameDataMessage?ColonyFrameLog.Part.NativeWait:
                message==DevelopmentTelemetrySampler.NewGameFrameMessage?ColonyFrameLog.Part.NativeSubmit:ColonyFrameLog.Part.NativeOther,begun);}
        }
        private static IntPtr ForwardNativeUntimed(int message,int length,byte* payload)
        {
            var result=Sim.SIM_HandleMessage(message,length,payload);
            FinishNativePublication(message);
            return result;
        }
        private static IntPtr ForwardNativeBatch(int message,int length,int count,byte* payload)
        {
            var result=Sim.SIM_HandleMessages(message,length,count,payload);
            FinishNativePublication(message);
            return result;
        }
        private static void FinishNativePublication(int message)
        {
            // Prepare returns a view into the just-published GameData buffer;
            // native Step writes the other buffer until the next Prepare.
            // Waiting changes neither that pointer nor callback dispatch order.
            // Include batches and frames queued by another managed thread:
            // the backend owns the actual posted prefix and complete-Step fence.
            if(message==DevelopmentTelemetrySampler.PrepareGameDataMessage && ColonyOwner.Active)
                _=ColonyNativeFrames.Complete();
        }
        // An ordinary game, and the colony-owner scenes that check it (no
        // development domain, floor arena or telemetry): only the colony owner
        // routes messages. Scenes add only their report counters.
        private static bool Ordinary => !NativeSealedDomainValidation.IsMode && !DevelopmentTelemetry.Active && FloorArenas.Id.Length == 0 &&
            (ColonyRuntimeValidation.Settings == null || ColonyRuntimeValidation.Settings.Mode.StartsWith("colony-owner", StringComparison.Ordinal));
        internal static IntPtr Handle(int message, int length, byte* payload)
        {
            if (NativeContractCapture.IsActive) return NativeContractCapture.Accept(message, length, (IntPtr)payload);
            ColonyLoadedTiles.Rewrite(message,length,payload);
            var world = ProbeScope.World;
            if (world == null && Ordinary)
            {
                var scene = ColonyRuntimeValidation.Settings != null;
                if (scene) ColonyOwner.Count(message, 1);
                if (ColonyOwner.TryHandle(message, length, 1, payload)) return IntPtr.Zero;
                var timed = scene && ColonyOwner.Active && (message == -775326397 || message == 1078620451);
                var started = timed ? System.Diagnostics.Stopwatch.GetTimestamp() : 0;
                var result = ForwardNative(message, length, payload);
                if (timed) ColonyOwner.RecordBlocked(message, System.Diagnostics.Stopwatch.GetTimestamp() - started);
                NativeBackendObservation.AfterNativeMessage(message);
                result = ColonyOwner.AfterNativeMessage(message, result);
                return scene ? ColonyRuntimeValidation.AfterNativeMessage(message, result) : result;
            }
            if (world == null)
            {
                NativeSealedDomainValidation.BeforeRestoredNativeFrame(message,length,payload);
                if (NativeSealedDomainValidation.TryHandleCellMassEdits(message, length, 1, payload)) return IntPtr.Zero;
                if (NativeSealedDomainValidation.TryHandleHeat(message, length, 1, payload)) return IntPtr.Zero;
                if (NativeSealedDomainValidation.TryHandleTransfers(message, length, 1, payload)) return IntPtr.Zero;
                if (NativeSealedDomainValidation.TryHandleActorMessage(message, length, payload)) return IntPtr.Zero;
                ColonyOwner.Count(message, 1);
                if (message == DevelopmentTelemetrySampler.NewGameFrameMessage && length >= 4) FloorArenas.OnFrame(*(float*)payload);
                if (ColonyOwner.TryHandle(message, length, 1, payload)) return IntPtr.Zero;
                if (NativeSealedDomainValidation.Active) NativeSealedDomainValidation.BeforeNativeMutation(message, length, payload);
                // Development telemetry only: wall time of the game thread in
                // the native frame submit/wait and the managed publication.
                var timed = DevelopmentTelemetry.Active && (message == DevelopmentTelemetrySampler.NewGameFrameMessage ||
                    message == DevelopmentTelemetrySampler.PrepareGameDataMessage);
                var started = timed ? System.Diagnostics.Stopwatch.GetTimestamp() : 0;
                var colonyTimed = ColonyOwner.Active && (message == -775326397 || message == 1078620451);
                var colonyStarted = colonyTimed ? System.Diagnostics.Stopwatch.GetTimestamp() : 0;
                var native = ForwardNative(message, length, payload);
                if (colonyTimed) ColonyOwner.RecordBlocked(message, System.Diagnostics.Stopwatch.GetTimestamp() - colonyStarted);
                var returned = timed ? System.Diagnostics.Stopwatch.GetTimestamp() : 0;
                var processed = timed && message == DevelopmentTelemetrySampler.PrepareGameDataMessage && native != IntPtr.Zero
                    ? ((Sim.GameDataUpdate*)native)->numFramesProcessed : 0;
                NativeBackendObservation.AfterNativeMessage(message);
                if (NativeSealedDomainValidation.IsMode) native = NativeSealedDomainValidation.AfterNativeMessage(message, native);
                else native = ColonyOwner.AfterNativeMessage(message, native);
                var published = ColonyRuntimeValidation.AfterNativeMessage(message, native);
                if (timed) DevelopmentTelemetry.Sampler.RecordNativeMessage(message, started, returned, System.Diagnostics.Stopwatch.GetTimestamp(), processed);
                return published;
            }
            if (length < 0 || (length != 0 && payload == null)) throw new ArgumentException("Invalid scoped message payload.");
            switch ((SimMessageHashes)message)
            {
                case SimMessageHashes.SimFrameManager_NewGameFrame:
                    if (length == 0 || length % sizeof(Sim.NewGameFrame) != 0)
                        throw new ArgumentException("Invalid active-region frame payload.");
                    var frames = (Sim.NewGameFrame*)payload;
                    for (var i = 1; i < length / sizeof(Sim.NewGameFrame); ++i)
                        if (frames[i].elapsedSeconds != frames[0].elapsedSeconds)
                            throw new ArgumentException("Active regions disagree on timestep.");
                    // A global timestep is advanced once, regardless of the
                    // number of asteroid/active-region records in this message.
                    world.Step(frames[0].elapsedSeconds);
                    ++ProbeScope.InterceptedFrames;
                    return IntPtr.Zero;
                case SimMessageHashes.PrepareGameData:
                    if (length != 0) throw new ArgumentException("Unexpected snapshot request payload.");
                    ++ProbeScope.InterceptedSnapshots;
                    // Only the probe invokes this in scope. No Game.StepTheSim
                    // consumes this value; full array publication is not ready.
                    return IntPtr.Zero;
                default:
                    // A direct native call from SimMessages.SetDebugProperties
                    // proves coverage beyond the Sim.HandleMessage wrapper.
                    if (message == -1683118492 && length == sizeof(Sim.DebugProperties))
                    {
                        ++ProbeScope.InterceptedDirectMessages;
                        return IntPtr.Zero;
                    }
                    throw new NotSupportedException("Scoped probe refuses unsupported native message " + message);
            }
        }

        internal static IntPtr HandleBatch(int message, int length, int count, byte* payload)
        {
            if (NativeContractCapture.IsActive) throw new NotSupportedException("Batch calls are forbidden during capture-only export.");
            if (ProbeScope.World == null && Ordinary)
            {
                if (ColonyRuntimeValidation.Settings != null) ColonyOwner.Count(message, count);
                return ColonyOwner.TryHandle(message, length, count, payload) ? IntPtr.Zero : ForwardNativeBatch(message, length, count, payload);
            }
            if (ProbeScope.World == null)
            {
                if (NativeSealedDomainValidation.TryHandleHeat(message, length, count, payload)) return IntPtr.Zero;
                if (NativeSealedDomainValidation.TryHandleCellMassEdits(message, length, count, payload)) return IntPtr.Zero;
                if (NativeSealedDomainValidation.TryHandleTransfers(message, length, count, payload)) return IntPtr.Zero;
                if (NativeSealedDomainValidation.TryHandleActorBatch(message, length, count, payload)) return IntPtr.Zero;
                ColonyOwner.Count(message, count);
                if (ColonyOwner.TryHandle(message, length, count, payload)) return IntPtr.Zero;
                if (NativeSealedDomainValidation.Active)
                {
                    if (length < 0 || count < 0 || (count > 0 && payload == null)) throw new ArgumentException("Invalid native batch.");
                    _ = checked(length * count);
                    for (var index = 0; index < count; ++index)
                        NativeSealedDomainValidation.BeforeNativeMutation(message, length, payload + checked(index * length));
                }
                return ForwardNativeBatch(message, length, count, payload);
            }
            // A partial batch followed by a rejection would break atomicity.
            // No batch is accepted until its whole transaction is implemented.
            throw new NotSupportedException("Batch native messages are not implemented in the probe.");
        }
    }

    internal static class ColonyNativeFrames
    {
        [StructLayout(LayoutKind.Sequential)]
        internal struct Progress
        {
            internal uint Size,Version;
            internal ulong Session,Queued,Posted,Processed,PostedSteps,CompletedSteps;
            internal static Progress Empty=>new Progress{Size=56,Version=1};
        }
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_native_frames_progress",CallingConvention=CallingConvention.Cdecl)]
        private static extern int CaptureNative(ref Progress output);
        [DllImport("mixed_atmospheres_oni",EntryPoint="ma_oni_native_frames_complete",CallingConvention=CallingConvention.Cdecl)]
        private static extern int CompleteNative(ref Progress output,uint timeoutMilliseconds);
        private static Progress Validate(Progress value,bool completed)
        {
            if(value.Size!=56 || value.Version!=1 || value.Session==0 || value.Posted>value.Queued || value.Processed>value.Posted ||
                (completed && (value.Processed!=value.Posted || value.CompletedSteps<value.PostedSteps)))
                throw new InvalidOperationException("Native frame progress is invalid or incomplete.");
            return value;
        }
        internal static Progress Capture()
        {var value=Progress.Empty;OniNativeColony.Check(CaptureNative(ref value));return Validate(value,false);}
        internal static Progress Complete(int timeoutMilliseconds=10000)
        {
            if(timeoutMilliseconds<1 || timeoutMilliseconds>60000)throw new ArgumentOutOfRangeException(nameof(timeoutMilliseconds));
            var value=Progress.Empty;OniNativeColony.Check(CompleteNative(ref value,(uint)timeoutMilliseconds));return Validate(value,true);
        }
    }

    [HarmonyPatch(typeof(Sim), nameof(Sim.Save))]
    internal static class SaveProbePatch
    {
        internal static bool Prefix(BinaryWriter __0)
        {
            var world = ProbeScope.World;
            if (world == null) return true;
            var bytes = world.Save();
            __0.Write(bytes.Length);
            __0.Write(bytes);
            ++ProbeScope.InterceptedSaves;
            return false;
        }
    }
}
