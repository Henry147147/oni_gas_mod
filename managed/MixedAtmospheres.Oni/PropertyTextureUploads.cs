using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Threading;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Unchanged full-map property textures are not uploaded again
    // (docs/native-fluid-rendering.md#full-map-texture-uploads).
    //
    // Every Unity frame PropertyTextures.UpdateProperty copies five textures
    // the native simulation publishes for the whole cluster (flow, liquid,
    // liquid data, material data, sunlight exposure: 21 bytes a cell, about
    // 5.4 MB on a 636x404 Spaced Out! cluster) into Texture2Ds and uploads
    // them. Their source buffers change only inside Game.StepTheSim (ONI's
    // data request, which every publication of this mod goes through); at
    // speed 3 and 100 fps that is one frame in six or seven. A frame whose
    // texture, source pointer and size match the last upload made since the
    // latest StepTheSim skips the copy and the upload; an upload is forced at
    // least every half second. MA_TEXTURE_UPLOAD_REUSE=0 turns it off.
    // Under the colony owner the source is a plane of its published snapshot,
    // whose content identity (ColonyOwner.TryGetPublishedTexture) changes only
    // with its bytes: an upload of the same identity is reused across data
    // requests too, so a paused game (a data request every frame) uploads
    // only when a plane actually changed.
    [HarmonyPatch(typeof(PropertyTextures), "UpdateProperty")]
    internal static class PropertyTextureUploads
    {
        internal static readonly bool Enabled = Environment.GetEnvironmentVariable("MA_TEXTURE_UPLOAD_REUSE") != "0";
        private const float RefreshSeconds = .5f;
        private const int ExpectedLoads = 5, ExpectedApplies = 1;
        private struct Upload { internal Texture2D? Texture; internal IntPtr Data; internal int Size; internal long Generation, Content; internal float At; }
        private static readonly Upload[] uploads = new Upload[8];
        private static long generation = 1, uploaded, reused;
        private static Texture2D? skippedApply;
        internal static long Uploaded => Interlocked.Read(ref uploaded);
        internal static long Reused => Interlocked.Read(ref reused);
        internal static bool Installed { get; private set; }

        private static readonly MethodInfo LoadRaw = AccessTools.Method(typeof(Texture2D), nameof(Texture2D.LoadRawTextureData), new[] { typeof(IntPtr), typeof(int) });
        private static readonly MethodInfo ApplyTexture = AccessTools.Method(typeof(Texture2D), nameof(Texture2D.Apply), Type.EmptyTypes);

        private static bool Prepare() => Enabled;

        // The five copies and the one upload after them, or nothing at all
        // if the method no longer has exactly that shape.
        private static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
        {
            var list = instructions.ToList();
            var loads = list.Count(i => i.Calls(LoadRaw)); var applies = list.Count(i => i.Calls(ApplyTexture));
            if (!Enabled || loads != ExpectedLoads || applies != ExpectedApplies)
            {
                if (Enabled) Debug.LogWarning("[Mixed Atmospheres] PropertyTextures.UpdateProperty changed shape (" + loads + " copies, " + applies + " uploads); full-map texture uploads stay as the game makes them.");
                return list;
            }
            foreach (var instruction in list)
            {
                if (instruction.Calls(LoadRaw)) { instruction.opcode = OpCodes.Call; instruction.operand = AccessTools.Method(typeof(PropertyTextureUploads), nameof(Load)); }
                else if (instruction.Calls(ApplyTexture)) { instruction.opcode = OpCodes.Call; instruction.operand = AccessTools.Method(typeof(PropertyTextureUploads), nameof(Apply)); }
            }
            Installed = true;
            return list;
        }

        // Any data request may have rewritten the source buffers.
        [HarmonyPatch(typeof(Game), "StepTheSim")]
        internal static class NewGameData
        {
            private static void Postfix() => ++generation;
        }

        internal static void Load(Texture2D texture, IntPtr data, int size)
        {
            skippedApply = null;
            var slot = Slot(texture);
            ref var last = ref uploads[slot];
            // Content 0: a source without an identity (no colony owner).
            var content = ColonyOwner.TryGetPublishedTexture(data, size, out var identity) ? identity : 0;
            var same = content != 0 ? last.Content == content : last.Content == 0 && last.Data == data && last.Generation == generation;
            if (ReferenceEquals(last.Texture, texture) && last.Size == size && same &&
                Time.unscaledTime - last.At < RefreshSeconds && texture != null)
            {
                skippedApply = texture; ++reused; return;
            }
            texture!.LoadRawTextureData(data, size);
            last = new Upload { Texture = texture, Data = data, Size = size, Generation = generation, Content = content, At = Time.unscaledTime };
            ++uploaded;
        }

        internal static void Apply(Texture2D texture)
        {
            if (skippedApply != null && ReferenceEquals(skippedApply, texture)) { skippedApply = null; return; }
            skippedApply = null;
            texture.Apply();
        }

        // The texture's slot, or the oldest one for a texture not seen yet.
        private static int Slot(Texture2D texture)
        {
            var oldest = 0;
            for (var k = 0; k < uploads.Length; ++k)
            {
                if (ReferenceEquals(uploads[k].Texture, texture)) return k;
                if (uploads[k].Texture == null) return k;
                if (uploads[k].At < uploads[oldest].At) oldest = k;
            }
            return oldest;
        }

        internal static void Reset() { Array.Clear(uploads, 0, uploads.Length); skippedApply = null; ++generation; }
    }
}
