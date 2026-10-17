using System;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        private static bool gpuGasVerified, gpuGasBindingsVerified;
        private static GpuPixels gpuGasColour = new GpuPixels(), gpuGasOpacity = new GpuPixels(), gpuGasDanger = new GpuPixels();
        private static int[][] gpuExpectedColours = Array.Empty<int[]>();
        private static int[] gpuExpectedOpacity = Array.Empty<int>(), gpuExpectedDanger = Array.Empty<int>();
        private static long gpuPublicationGeneration;
        private static bool gpuOrientationVerified, gpuPageRowsReflected;
        private static int[][] gpuExpectedOutline = Array.Empty<int[]>();
        private static GpuPixels gpuIdentityOutline = new GpuPixels(), gpuReflectedOutline = new GpuPixels();
        private static void CalibrateGpuPageCoordinates()
        {
            var expected = new System.Collections.Generic.List<int[]>();
            for (var yy = y - 1; yy <= y + Height; ++yy)
                for (var xx = x - 1; xx <= x + Width; ++xx)
                {
                    var cell = Grid.XYToCell(xx, yy); var material = Grid.Element[cell];
                    var wall = Grid.LiquidImpermeable[cell]; var solid = material.IsSolid;
                    var red = wall ? solid ? 250 : 230 : solid ? 255 : 0;
                    var green = !wall && !solid && material.IsLiquid ? 255 : 0;
                    var blue = !wall && !solid && (material.IsGas || material.id == SimHashes.Vacuum) ? 255 : 0;
                    var mass = Grid.Mass[cell];
                    var alpha = (int)(byte)(255 * Math.Min(1f, Math.Max(mass > 0 ? 1f / 255 : 0, mass / 2000)));
                    expected.Add(new[] { red, green, blue, alpha });
                }
            gpuExpectedOutline = expected.ToArray();
            var phase = PropertyTextures.instance.textureBuffers[(int)PropertyTextures.Property.SolidLiquidGasMass].texture;
            gpuIdentityOutline = ReadTexturePixels(phase, x-1, y-1, Width+2, Height+2, false);
            gpuReflectedOutline = ReadTexturePixels(phase, x-1, y-1, Width+2, Height+2, true);
            Require(gpuIdentityOutline.Available && gpuReflectedOutline.Available, "Native GPU coordinate controls are unavailable.");
            gpuPageRowsReflected = GpuPageReadbackPlan.SelectReflection(gpuExpectedOutline, gpuIdentityOutline.Rgba, gpuReflectedOutline.Rgba);
            gpuOrientationVerified = true;
        }
        private static void VerifyNativeGpuGasTextures()
        {
            CalibrateGpuPageCoordinates();
            var frame = MixturePresentationRuntime.AcquireFrame() ?? throw new InvalidOperationException("No completed GPU source.");
            gpuPublicationGeneration = frame.PublicationGeneration;
            var colour = Shader.GetGlobalTexture("_GasColourTex"); var opacity = Shader.GetGlobalTexture("_GasPressureTex");
            var danger = Shader.GetGlobalTexture("_GasDangerTex"); var buffers = PropertyTextures.instance.textureBuffers;
            gpuGasBindingsVerified = ReferenceEquals(colour, buffers[(int)PropertyTextures.Property.GasColour].texture) &&
                ReferenceEquals(opacity, buffers[(int)PropertyTextures.Property.GasPressure].texture) &&
                ReferenceEquals(danger, buffers[(int)PropertyTextures.Property.GasDanger].texture);
            gpuGasColour = TexturePixels(colour, true); gpuGasOpacity = TexturePixels(opacity, true); gpuGasDanger = TexturePixels(danger, true);
            var samples = Enumerable.Range(0, cells.Length).Select(index => {
                var global = Grid.XYToCell(x + index % Width, y + index / Width);
                Require(frame.TryGetCell(global, out var reading) && reading!.Revision == (long)latest.CompletedFrames,
                    "GPU source reading is stale.");
                return MixtureWorldRendering.SampleForValidation(reading!, global % frame.Width, global / frame.Width);
            }).ToArray();
            gpuExpectedColours = samples.Select(item => new[] { (int)item.R, item.G, item.B, item.Alpha }).ToArray();
            gpuExpectedOpacity = samples.Select(item => (int)item.Opacity).ToArray();
            gpuExpectedDanger = samples.Select(item => (int)item.Hazard).ToArray();
            gpuGasVerified = gpuGasBindingsVerified && gpuGasColour.Available && gpuGasOpacity.Available && gpuGasDanger.Available &&
                gpuGasColour.Rgba.Length == cells.Length && gpuGasOpacity.Rgba.Length == cells.Length && gpuGasDanger.Rgba.Length == cells.Length &&
                frame.PublicationGeneration == MixtureWorldRendering.LastTexturePublicationGeneration &&
                Enumerable.Range(0, cells.Length).All(index => gpuGasColour.Rgba[index].SequenceEqual(gpuExpectedColours[index]) &&
                    gpuGasOpacity.Rgba[index][3] == gpuExpectedOpacity[index] && gpuGasDanger.Rgba[index][3] == gpuExpectedDanger[index]);
            Require(gpuGasVerified, "Actual shader-bound gas GPU texels disagree with the completed colour, density-opacity or hazard reading.");
        }
        private static object GpuGasReport() => new { Required = IsMode, Verified = gpuGasVerified, BindingsVerified = gpuGasBindingsVerified,
            Source = "shader-bound-GPU-readback", GraphicsDevice = SystemInfo.graphicsDeviceType.ToString(),
            UploadPath = "native-texture-page-blit", NativeUploadsUnchanged = true, Session = latest.Session, CompletedFrame = latest.CompletedFrames,
            Orientation = new { Verified = gpuOrientationVerified, Source = "native-phase-and-mass-sealed-outline", RowsReflected = gpuPageRowsReflected,
                X = x-1, Y = y-1, Width = Width+2, Height = Height+2,
                Expected = gpuExpectedOutline, Identity = gpuIdentityOutline, Reflected = gpuReflectedOutline },
            PublicationGeneration = gpuPublicationGeneration, TexturePublicationGeneration = MixtureWorldRendering.LastTexturePublicationGeneration,
            Colour = gpuGasColour, Opacity = gpuGasOpacity, Danger = gpuGasDanger,
            ExpectedColours = gpuExpectedColours, ExpectedOpacity = gpuExpectedOpacity, ExpectedDanger = gpuExpectedDanger };
    }
}
