using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using HarmonyLib;
using KSerialization;

namespace MixedAtmospheres.Oni
{
    // Colony saves (docs/colony-owner.md#saves): ONI's native section becomes a
    // MACOLONY envelope holding the original native bytes and the colony
    // checkpoint. A game without the mod hands the envelope to its native
    // loader, which refuses it.
    internal static unsafe class ColonyOwnerPersistence
    {
        private const int OniBuild = 744825, MaterialModel = 3, NativeLoad = -672538170;
        private static readonly byte[] PayloadMagic = { (byte)'M', (byte)'A', (byte)'C', (byte)'O', (byte)'L', (byte)'P', (byte)'0', (byte)'1' };
        [DllImport("SimDLL", EntryPoint = "SIM_BeginSave")] private static extern byte* BeginSave(int* size, int width, int height);
        [DllImport("SimDLL", EntryPoint = "SIM_EndSave")] private static extern void EndSave();
        private static (OniNativeColony.Config config, byte[] image)? pending;
        internal static int Saves { get; private set; }
        internal static int Loads { get; private set; }

        private static byte[] CatalogueHash(OniNativeColony.Material[] materials)
        {
            var bytes = new byte[materials.Length * 168];
            fixed (byte* target = bytes)
                for (var k = 0; k < materials.Length; ++k) Marshal.StructureToPtr(materials[k], (IntPtr)(target + k * 168), false);
            using var hash = SHA256.Create(); return hash.ComputeHash(bytes);
        }
        private static byte[] ConfigBytes(OniNativeColony.Config config)
        {
            var bytes = new byte[56];
            fixed (byte* target = bytes) Marshal.StructureToPtr(config, (IntPtr)target, false);
            return bytes;
        }

        internal static bool TryWriteSave(BinaryWriter writer, int offsetX, int offsetY)
        {
            if (!ColonyOwner.Active) return false;
            if (ColonyOwner.Status.State != 1) throw new InvalidOperationException("A faulted colony owner cannot be saved: " + ColonyOwner.Status.Error);
            int size; byte* data = BeginSave(&size, offsetX, offsetY); byte[] native;
            try { native = new ReadOnlySpan<byte>(data, size).ToArray(); } finally { EndSave(); }
            var image = ColonyOwner.Checkpoint();
            var config = ConfigBytes(ColonyOwner.Config);
            var payload = new byte[8 + 4 + config.Length + image.Length];
            PayloadMagic.CopyTo(payload, 0); BinaryPrimitives.WriteInt32LittleEndian(payload.AsSpan(8), config.Length);
            config.CopyTo(payload, 12); image.CopyTo(payload, 12 + config.Length);
            var envelope = ColonySaveEnvelope.Create(OniBuild, MaterialModel, ColonyOwner.Status.CompletedFrames,
                CatalogueHash(ColonyOwner.Materials), native, payload);
            envelope.WriteLengthPrefixed(writer); ++Saves;
            UnityEngine.Debug.Log($"[Mixed Atmospheres] Colony saved at owner frame {ColonyOwner.Status.CompletedFrames} ({envelope.EncodedLength} bytes).");
            return true;
        }

        internal static bool TryLoad(IReader reader, ref int result)
        {
            var data = reader.RawBytes(); var position = reader.Position;
            if (position < 0 || position > data.Length - 4) return false;
            var length = BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(position));
            if (length < 8 || length > data.Length - position - 4 || !ColonySaveEnvelope.HasMagic(data.AsSpan(position + 4, length))) return false;
            var materials = ColonyOwner.BuildCatalogue();
            var envelope = ColonySaveEnvelope.Parse(data.AsSpan(position + 4, length), OniBuild, MaterialModel, CatalogueHash(materials));
            var payload = envelope.CustomData;
            if (payload.Length < 12 || !payload.Slice(0, 8).SequenceEqual(PayloadMagic)) return false;
            var configLength = BinaryPrimitives.ReadInt32LittleEndian(payload.Slice(8));
            if (configLength != 56 || payload.Length <= 12 + configLength) throw new InvalidDataException("Malformed colony save payload.");
            OniNativeColony.Config config;
            fixed (byte* source = payload.Slice(12, configLength)) config = Marshal.PtrToStructure<OniNativeColony.Config>((IntPtr)source);
            var image = payload.Slice(12 + configLength).ToArray();
            if (reader.ReadInt32() != length) throw new InvalidDataException("Colony save section changed while loading.");
            _ = reader.ReadBytes(length);
            var native = envelope.NativeData.ToArray(); IntPtr loaded;
            fixed (byte* pointer = native) loaded = Sim.SIM_HandleMessage(NativeLoad, native.Length, pointer);
            if (loaded == IntPtr.Zero) throw new InvalidDataException("ONI's native loader rejected the colony's native section.");
            pending = (config, image); result = 0; ++Loads;
            return true;
        }

        // The owner resumes at the first completed frame after the managed
        // cluster exists (ColonyOwner.AfterNativeMessage).
        internal static (OniNativeColony.Config config, byte[] image)? TakePending()
        {
            var value = pending; pending = null; return value;
        }
        internal static void Reset() { pending = null; }
    }

    [HarmonyPatch(typeof(Sim), nameof(Sim.Save))]
    internal static class ColonyOwnerSavePatch
    {
        private static bool Prefix(BinaryWriter __0, int __1, int __2) => !ColonyOwnerPersistence.TryWriteSave(__0, __1, __2);
    }
    [HarmonyPatch]
    internal static class ColonyOwnerLoadPatch
    {
        private static IEnumerable<MethodBase> TargetMethods() => new[] { AccessTools.Method(typeof(Sim), nameof(Sim.LoadWorld)), AccessTools.Method(typeof(Sim), nameof(Sim.Load)) };
        private static bool Prefix(IReader __0, ref int __result) => !ColonyOwnerPersistence.TryLoad(__0, ref __result);
    }
}
