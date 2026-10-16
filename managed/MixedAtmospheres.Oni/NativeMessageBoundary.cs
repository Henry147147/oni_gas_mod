using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
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

        private static IEnumerable<CodeInstruction> ReadInstructions(MethodBase method)
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
        internal static IntPtr Handle(int message, int length, byte* payload)
        {
            if (NativeContractCapture.IsActive) return NativeContractCapture.Accept(message, length, (IntPtr)payload);
            var world = ProbeScope.World;
            if (world == null)
            {
                if (NativeSealedDomainValidation.Active) NativeSealedDomainValidation.BeforeNativeMutation(message, length, payload);
                var native = Sim.SIM_HandleMessage(message, length, payload);
                NativeBackendObservation.AfterNativeMessage(message);
                if (NativeSealedDomainValidation.IsMode) native = NativeSealedDomainValidation.AfterNativeMessage(message, native);
                return ColonyRuntimeValidation.AfterNativeMessage(message, native);
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
            if (ProbeScope.World == null)
            {
                if (NativeSealedDomainValidation.Active)
                {
                    if (length < 0 || count < 0 || (count > 0 && payload == null)) throw new ArgumentException("Invalid native batch.");
                    _ = checked(length * count);
                    for (var index = 0; index < count; ++index)
                        NativeSealedDomainValidation.BeforeNativeMutation(message, length, payload + checked(index * length));
                }
                return Sim.SIM_HandleMessages(message, length, count, payload);
            }
            // A partial batch followed by a rejection would break atomicity.
            // No batch is accepted until its whole transaction is implemented.
            throw new NotSupportedException("Batch native messages are not implemented in the probe.");
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
