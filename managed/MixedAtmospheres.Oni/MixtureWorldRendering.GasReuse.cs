using System;
using System.Threading;
using HarmonyLib;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Gas texture reuse (docs/native-fluid-rendering.md#gas-texture-reuse).
    // The renderer schedules the three gas textures every frame so they never
    // blend between publications. Their content depends only on the
    // presentation (its session epoch and generation, which every change of
    // world, fixture or model advances), the visible cells, the active world
    // and the opacity settings, and a publication lasts several Unity frames
    // (four at speed 3, twelve at speed 1, all of a pause). Between uploads ONI's render
    // textures keep the last image, so a frame whose inputs match the last
    // completed upload, inside its cell range, skips the three updates: their
    // cell callbacks return at once and the unlocked page goes back to the pool
    // without a texture upload or blit. Any doubt (validation probes, the
    // compact proof, a time-lapse capture, a lost render texture, half a
    // second since the last upload) uploads as before.
    internal static partial class MixtureWorldRendering
    {
        private const float GasReuseLimitSeconds = .5f;
        private static readonly VisibleRange? gasVisibleRange =
            TryBindVisibleRange(() => AccessTools.Method(typeof(PropertyTextures), "GetVisibleCellRange"));
        private static volatile bool reusingGasTextures;
        private static readonly TextureBuffer?[] reusedBuffers = new TextureBuffer?[3];
        private static GasUpload lastGasUpload;
        private static long gasUploads, gasReuses;
        internal static long GasTextureUploads => Interlocked.Read(ref gasUploads);
        internal static long GasTextureReuses => Interlocked.Read(ref gasReuses);

        private struct GasUpload
        {
            internal bool Valid;
            internal long Epoch, Generation;
            internal PropertyTextures? Owner;
            internal int X0, Y0, X1, Y1, World;
            internal OpacityParameters Opacity;
            internal float At;
        }

        // Decides, before the stock loop, whether this frame reuses the gas
        // textures. `upload` is this frame's key when it uploads instead.
        private static bool DecideGasReuse(PropertyTextures owner, RuntimeMixturePresentationFrame frame, OpacityParameters parameters, out GasUpload upload)
        {
            upload = default;
            if (gasVisibleRange == null || Volatile.Read(ref validationProbes) != null || compactProof) return false;
            int x0, y0, x1, y1;
            try { gasVisibleRange(owner, out x0, out y0, out x1, out y1); }
            catch (Exception error) when (error is NullReferenceException || error is InvalidOperationException ||
                error is ArgumentException || error is IndexOutOfRangeException) { return false; }
            var world = ClusterManager.Instance != null ? ClusterManager.Instance.activeWorldId : -1;
            upload = new GasUpload { Valid = true, Epoch = frame.SessionEpoch, Generation = frame.PublicationGeneration, Owner = owner, X0 = x0, Y0 = y0, X1 = x1, Y1 = y1, World = world, Opacity = parameters, At = Time.unscaledTime };
            var last = lastGasUpload;
            if (!last.Valid || last.Epoch != frame.SessionEpoch || last.Generation != frame.PublicationGeneration ||
                !ReferenceEquals(last.Owner, owner) || last.World != world ||
                last.Opacity.Minimum != parameters.Minimum || last.Opacity.Maximum != parameters.Maximum ||
                last.Opacity.MinimumVisibility != parameters.MinimumVisibility) return false;
            if (x0 < last.X0 || y0 < last.Y0 || x1 > last.X1 || y1 > last.Y1) return false;
            if (Time.unscaledTime - last.At > GasReuseLimitSeconds || GameUtil.IsCapturingTimeLapse()) return false;
            if (owner.textureBuffers == null) return false;
            for (var k = 0; k < GasProperties.Length; ++k)
            {
                var index = (int)GasProperties[k];
                var buffer = index < owner.textureBuffers.Length ? owner.textureBuffers[index] : null;
                if (buffer == null || buffer.texture == null || !buffer.texture.IsCreated()) return false;
                reusedBuffers[k] = buffer;
            }
            return true;
        }

        private static void BeginGasReuse(bool reuse)
        {
            if (reuse) Interlocked.Increment(ref gasReuses);
            reusingGasTextures = reuse;
        }

        // Records a completed upload; any failure forgets the last one.
        private static void EndGasReuse(bool reused, GasUpload upload, bool failed)
        {
            reusingGasTextures = false;
            Array.Clear(reusedBuffers, 0, reusedBuffers.Length);
            if (failed) { lastGasUpload = default; return; }
            if (reused || !upload.Valid) return;
            // UpdateProperty does nothing while the game loads.
            if (Game.Instance == null || Game.Instance.IsLoading()) { lastGasUpload = default; return; }
            lastGasUpload = upload;
            Interlocked.Increment(ref gasUploads);
        }

        private static void ForgetGasUpload() => lastGasUpload = default;

        // Prefix of the three stock gas callbacks (job threads): skip the
        // stock cell loop while the textures are reused.
        private static bool BeforeGasCallback() => !reusingGasTextures;

        // Prefix of TextureBuffer.Unlock (main thread): a reused gas texture
        // returns its unwritten page without uploading or blitting it.
        private static bool BeforeUnlock(TextureBuffer __instance, TextureRegion region)
        {
            if (!reusingGasTextures || __instance == null) return true;
            var reused = reusedBuffers;
            if (!ReferenceEquals(__instance, reused[0]) && !ReferenceEquals(__instance, reused[1]) && !ReferenceEquals(__instance, reused[2])) return true;
            __instance.pool.Release(region.page);
            return false;
        }
    }
}
