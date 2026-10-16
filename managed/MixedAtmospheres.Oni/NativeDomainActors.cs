using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using static MixedAtmospheres.Oni.OniNativeDomain;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private sealed class ActorBinding
        {
            internal uint Kind, Cell, Species;
            internal int Blocked = -1, Unblocked = -1;
            internal bool Virgin = true, Configured, Removed;
        }
        private static readonly Dictionary<int, ActorBinding> pendingActorRegistrations = new Dictionary<int, ActorBinding>();
        private static readonly Dictionary<(uint kind, int handle), ActorBinding> actorBindings = new Dictionary<(uint, int), ActorBinding>();
        private static long actorMessagesRouted, actorConsumerPublications, actorEmitterPublications, actorBlockedPublications, actorUnblockedPublications;
        private static ActorStatus actorStatus;
        private static ulong emissionStatusFrame;
        private static readonly Dictionary<int, Sim.EmittedMassInfo> completedEmissionStatus = new Dictionary<int, Sim.EmittedMassInfo>();
        private static uint GasSpecies(ushort element)
        {
            for (uint species = 0; species < Ids.Length; ++species)
                if (species != 3 && ElementLoader.FindElementByHash(SpeciesHash(species)).idx == element) return species;
            throw new NotSupportedException("The registered point actor has an unsupported gas material.");
        }
        private static bool ActorMessage(int message) => message == 2024405073 || message == 1575539738 || message == 894417742 ||
            message == -505471181 || message == 403589164 || message == -1524118282;
        internal static bool TryHandleActorMessage(int message, int length, byte* payload)
        {
            if (!active || !ActorMessage(message)) return false;
            try
            {
                var expected = message == 2024405073 || message == 1575539738 ? 12 : message == -505471181 ? 16 : message == 403589164 ? 32 : 8;
                if (length != expected || payload == null) throw new ArgumentException("Invalid registered actor message.");
                var data = new ReadOnlySpan<byte>(payload, length);
                int Int(int offset) => BinaryPrimitives.ReadInt32LittleEndian(new ReadOnlySpan<byte>(payload + offset, 4));
                float Float(int offset) => BitConverter.Int32BitsToSingle(Int(offset));
                if (message == 2024405073)
                {
                    if (!OwnsPublicCell(Int(0))) return false;
                    if (data[8] != 0 || data[9] != 0 || Int(4) == -1)
                        throw new NotSupportedException("Owned consumers require selective, single-cell registration with a callback.");
                    pendingActorRegistrations.Add(Int(4), new ActorBinding { Kind = 1, Cell = checked((uint)Int(0)),
                        Species = GasSpecies(BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(10))) });
                    // Native registration starts at rate zero. Its normal handle
                    // callback is retained; later rate setters are custom-owned.
                    return false;
                }
                if (message == -505471181)
                {
                    if (Int(4) != -1) pendingActorRegistrations.Add(Int(4), new ActorBinding { Kind = 2, Blocked = Int(8), Unblocked = Int(12) });
                    return false;
                }
                var kind = message == 1575539738 || message == 894417742 ? 1U : 2U;
                var key = (kind, Int(0)); actorBindings.TryGetValue(key, out var binding);
                var remove = message == 894417742 || message == -1524118282;
                if (remove)
                {
                    if (binding == null) return false;
                    if (binding.Configured && !failed) ConfigureActorAtCurrentFrame(new ActorCommand { Handle = key.Item2, Kind = kind, Flags = 1 });
                    binding.Removed = true; binding.Configured = false;
                    return false; // ONI removes its inactive registration normally.
                }
                var cell = Int(4); var owned = OwnsPublicCell(cell);
                if (!owned)
                {
                    if (binding != null && binding.Configured) throw new NotSupportedException("Moving a registered actor across owners requires a reservation.");
                    if (binding != null) binding.Virgin = false;
                    return false;
                }
                if (binding == null || binding.Removed || !binding.Virgin)
                    throw new NotSupportedException("Only an inactive native registration can enter the custom actor owner.");
                var command = new ActorCommand { Handle = key.Item2, Kind = kind, PublicCell = checked((uint)cell), Species = binding.Species };
                if (kind == 1) command.MassKg = Float(8);
                else
                {
                    command.MassKg = Float(12); command.Interval = Float(8); command.Temperature = Float(16); command.MaximumGasDensity = Float(20);
                    if (Int(24) != 0 || data[30] != 0) throw new NotSupportedException("Owned emitters require disease-free, single-cell output.");
                    if (command.MassKg > 0) command.Species = GasSpecies(BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(28)));
                }
                ConfigureActorAtCurrentFrame(command); binding.Configured = true; binding.Cell = command.PublicCell; binding.Species = command.Species;
                ++actorMessagesRouted;
                return true; // The native transfer remains at its initial zero rate.
            }
            catch (Exception caught) { Fail(caught); throw; }
        }
        private static void ConfigureActorAtCurrentFrame(ActorCommand command)
        {
            command.Size = 80; command.Version = 1;
            for (var attempt = 0; attempt < 8; ++attempt)
            {
                var source = Status.Empty; Check(Copy(ref source, new Cell[cells.Length], (uint)cells.Length));
                command.Session = source.Session; command.ExpectedFrame = source.CompletedFrames;
                var result = ActorStatus.Empty; var code = ConfigureActor(ref command, ref result);
                if (code == 3) continue;
                Check(code); actorStatus = result; return;
            }
            throw new InvalidOperationException("Native actor frame kept changing during reservation.");
        }
        internal static void CheckActorBatch(int message, int length, int count, byte* payload)
        {
            if (!ActorMessage(message) || count == 0) return;
            if (count < 0 || length < 8 || payload == null) throw new ArgumentException("Invalid registered actor batch.");
            _ = checked(length * count);
            // Batch admission must preflight the whole owner set. Until that
            // transaction exists, refuse every actor batch in an owned session.
            var rejection = new NotSupportedException("Registered actor batches require an atomic owner admission.");
            Fail(rejection); throw rejection;
        }
        private static void CaptureActorRegistrations(Sim.GameDataUpdate* source)
        {
            for (var i = 0; i < source->numComponentStateChangedMessages; ++i)
            {
                var registration = source->componentStateChangedMessages[i];
                if (!pendingActorRegistrations.TryGetValue(registration.callbackIdx, out var binding)) continue;
                Require(registration.simHandle != -1, "Native actor registration returned an invalid handle.");
                actorBindings.Add((binding.Kind, registration.simHandle), binding);
                pendingActorRegistrations.Remove(registration.callbackIdx);
            }
            var next = ActorStatus.Empty; Check(CopyActorStatus(ref next)); actorStatus = next;
        }
        private static void PrepareActorSnapshot(OwnedGameDataSnapshot copy)
        {
            if (emissionStatusFrame != latest.CompletedFrames) { completedEmissionStatus.Clear(); emissionStatusFrame = latest.CompletedFrames; }
            for (var i = 0; i < copy.Pointer->numRemovedMassEntries; ++i)
            {
                var sample = copy.Pointer->removedMassEntries[i];
                Require(sample.mass == 0 || !actorBindings.TryGetValue((1U, sample.simHandle), out var binding) || !binding.Configured,
                    "An owned consumer still transferred original native mass.");
            }
            foreach (var pair in actorBindings)
            {
                if (pair.Key.kind != 2 || !pair.Value.Configured || pair.Value.Removed) continue;
                var index = pair.Key.handle & 0x00ffffff;
                Require(index < copy.Pointer->numEmittedMassEntries && copy.Pointer->emittedMassEntries[index].mass == 0,
                    "An owned emitter still transferred original native mass or lost its native status slot.");
                if (completedEmissionStatus.TryGetValue(pair.Key.handle, out var held)) copy.Pointer->emittedMassEntries[index] = held;
            }
        }
        private static void PublishActorReceipt(OwnedGameDataSnapshot copy, Receipt receipt, ushort element, float mass)
        {
            var kind = receipt.Kind == 3 ? 1U : 2U;
            Require(actorBindings.TryGetValue((kind, receipt.Callback), out var binding), "Native actor receipt has no admitted full handle.");
            if (receipt.Kind == 3)
            {
                copy.AppendEvents<Sim.ConsumedMassInfo>(SnapshotEventChannel.RemovedMassEntries, new[] {
                    new Sim.ConsumedMassInfo { simHandle = receipt.Callback, removedElemIdx = element, mass = mass,
                        temperature = mass == 0 ? 0 : (float)receipt.Temperature, diseaseIdx = 255, diseaseCount = 0 } });
                ++actorConsumerPublications;
            }
            else if (receipt.Kind == 4)
            {
                actualEmitterMass += receipt.MassKg;
                if (receipt.CompletedFrame != latest.CompletedFrames) { ++actorEmitterPublications; return; }
                var index = receipt.Callback & 0x00ffffff;
                Require(index < copy.Pointer->numEmittedMassEntries, "Native registration has no allocated emission status slot.");
                // Each completed interval is a receipt. The game slot is a
                // read-only binary32 projection, accumulated within this query.
                ref var slot = ref copy.Pointer->emittedMassEntries[index];
                slot = new Sim.EmittedMassInfo { elemIdx = element, diseaseIdx = 255, diseaseCount = 0,
                    mass = slot.mass + mass, temperature = mass == 0 ? slot.temperature : (float)receipt.Temperature };
                completedEmissionStatus[receipt.Callback] = slot;
                ++actorEmitterPublications;
            }
            else if (receipt.Kind == 5 || receipt.Kind == 6)
            {
                var callback = receipt.Kind == 5 ? binding!.Blocked : binding!.Unblocked;
                if (callback != -1) copy.AppendEvents<Sim.CallbackInfo>(SnapshotEventChannel.CallbackInfo, new[] { new Sim.CallbackInfo { callbackIdx = callback } });
                if (receipt.Kind == 5) ++actorBlockedPublications; else ++actorUnblockedPublications;
            }
            else throw new InvalidOperationException("Unknown registered actor receipt.");
        }
        private static void ResetActorBindings()
        {
            pendingActorRegistrations.Clear(); actorBindings.Clear();
            completedEmissionStatus.Clear(); emissionStatusFrame = 0;
        }
    }
}
