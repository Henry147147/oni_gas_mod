using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static unsafe partial class NativeSealedDomainValidation
    {
        // Bounded, read-only diagnostics for the explicit development scene.
        // GPU reads happen only after completion, on the publication thread.
        private static object? RenderingInputs()
        {
            if (currentSnapshot == null) return null;
            var snapshot = currentSnapshot.Pointer;
            var view = MixturePresentationRuntime.AcquireFrame();
            var rows = Enumerable.Range(0, cells.Length).Select(index =>
            {
                var global = Grid.XYToCell(x + index % Width, y + index / Width);
                return new
                {
                    Cell = global, Solid = IsDomainSolid(index), PublishedProperties = snapshot->properties[global],
                    LiquidTexture = Pixel(snapshot->propertyTextureLiquid, global),
                    LiquidDataTexture = Pixel(snapshot->propertyTextureLiquidData, global),
                    MaterialDataTexture = Pixel(snapshot->propertyTextureMaterialData, global),
                    Reading = publishedReadings != null && publishedReadings.TryGetValue(global, out var physical) ? physical : null,
                    ExpectedGas = ExpectedGas(view, global)
                };
            }).ToArray();
            return new
            {
                NativeFlowFenceVerified = nativeFlowFenceVerified,
                NativeGuardMask = NativeFlowMask,
                NativeSnapshotCopyVerified = nativeSnapshotVerified,
                LiquidBindingUsesOwnedSnapshot = PropertyTextures.externalLiquidTex == snapshot->propertyTextureLiquid,
                LiquidDataBindingUsesOwnedSnapshot = PropertyTextures.externalLiquidDataTex == snapshot->propertyTextureLiquidData,
                RenderedRevision = renderedRevision,
                PublicationGeneration = view?.PublicationGeneration,
                TexturePublicationGeneration = MixtureWorldRendering.LastTexturePublicationGeneration,
                WorldAppearance = WorldAppearanceInputs(view),
                Rows = rows, Textures = RenderingTextures()
            };
        }

        private static object? WorldAppearanceInputs(RuntimeMixturePresentationFrame? view)
        {
            if (view == null) return null;
            var parameters = MixtureWorldRendering.OpacitySettingsForValidation;
            var options = view.Model.VisualOptions;
            return new {
                Model = "actual-phase-volume-linear-rgb-v1", GridWidth = view.Width, GridHeight = view.Height,
                options.PatternEnabled, options.PatternContrast,
                Opacity = new { parameters.Minimum, parameters.Maximum, parameters.MinimumVisibility },
                Styles = view.Model.Styles.Values.OrderBy(style => style.MaterialId).Select(style => new {
                    style.MaterialId, Srgb = new[] { style.Color.R, style.Color.G, style.Color.B }
                }).ToArray(),
                LiquidInterfaceGeometry = false
            };
        }

        private static int[] Pixel(IntPtr address, int cell)
        {
            if (address == IntPtr.Zero) return Array.Empty<int>();
            var start = (byte*)address + checked(4 * cell);
            return new[] { (int)start[0], start[1], start[2], start[3] };
        }

        private static object RenderingTextures()
        {
            if (!completed) return new { Available = false, Reason = "Readback is restricted to the completed development scene." };
            if (Thread.CurrentThread.ManagedThreadId != domainManagedThread)
                return new { Available = false, Reason = "Not on the game publication thread." };
            var owner = PropertyTextures.instance;
            var index = (int)PropertyTextures.Property.SolidLiquidGasMass;
            if (owner?.textureBuffers == null || index >= owner.textureBuffers.Length || owner.textureBuffers[index] == null)
                return new { Available = false, Reason = "Phase texture buffer unavailable." };
            return new
            {
                Available = true, X = x, Y = y, Width, Height,
                PhaseBuffer = TexturePixels(owner.textureBuffers[index].texture, true),
                PhaseShader = TexturePixels(Shader.GetGlobalTexture("_SolidLiquidGasMassTex"), true),
                LiquidShader = TexturePixels(Shader.GetGlobalTexture("_LiquidTex")),
                GasColourShader = TexturePixels(Shader.GetGlobalTexture("_GasColourTex"), true),
                GasPressureShader = TexturePixels(Shader.GetGlobalTexture("_GasPressureTex"), true),
                GasDangerShader = TexturePixels(Shader.GetGlobalTexture("_GasDangerTex"), true)
            };
        }

        private static object? ExpectedGas(RuntimeMixturePresentationFrame? view, int cell)
        {
            if (view == null || !view.TryGetCell(cell, out var reading) || reading == null) return null;
            var sample = MixtureWorldRendering.SampleForValidation(reading, cell % view.Width, cell / view.Width);
            return new { Colour = new[] { (int)sample.R, sample.G, sample.B, sample.Alpha }, Pressure = sample.Opacity, Danger = sample.Hazard };
        }

        // A completed visible scene may keep running under manual inspection;
        // one file per hovered cell and frame is capped for each process.
        internal const int MaximumNativeHoverFiles = 4096;
        private static int nativeHoverFiles;
        private static void ObserveNativeHover(int cell, MixturePresentation reading, IReadOnlyList<string> contents, bool selected)
        {
            if (!active || !completed || publishedReadings == null || !publishedReadings.TryGetValue(cell, out var physical)) return;
            var filename = "native-hover-S" + latest.Session.ToString(CultureInfo.InvariantCulture) + "-C" + cell.ToString(CultureInfo.InvariantCulture) +
                "-F" + reading.Revision.ToString(CultureInfo.InvariantCulture) + (selected ? "-selected.json" : "-hover.json");
            var path = Path.Combine(ColonyRuntimeValidation.OutputDirectory, filename);
            if (nativeHoverFiles >= MaximumNativeHoverFiles || File.Exists(path)) return;
            ++nativeHoverFiles;
            var view = MixturePresentationRuntime.AcquireFrame();
            File.WriteAllText(path, ReportJson.Serialize(new
            {
                Kind = "actual-native-development-cell-hover", Platform = Application.platform.ToString(),
                NativeSession = latest.Session, NativeFrame = latest.CompletedFrames, Cell = cell, Selected = selected,
                reading.Revision, reading.SourceId,
                ReadingUsesExactPhysicalObject = ReferenceEquals(reading.ThermodynamicReading, physical),
                ReadingMatchesCompletedFrame = reading.Revision == (long)latest.CompletedFrames,
                PublicationGeneration = view?.PublicationGeneration,
                TexturePublicationGeneration = MixtureWorldRendering.LastTexturePublicationGeneration,
                Header = MixtureInspection.FormatHeader(reading).ToUpperInvariant(), State = MixtureInspection.FormatState(reading),
                Contents = contents.ToArray(), Warnings = reading.Warnings, ExpandedText = MixtureInspection.FormatFullComposition(reading),
                Physical = physical, WholeColonyFluidAuthority = false, FirstPlayableColonyPassed = false
            }));
        }

        private sealed class GpuPixels
        {
            public bool Available;
            public string Reason = "", Type = "", Kind = "GPU ReadPixels", Format = "";
            public int TextureWidth, TextureHeight;
            public int ReadX, ReadY, ReadWidth, ReadHeight;
            public bool RowsReflected;
            public int[][] Rgba = Array.Empty<int[]>();
        }
        private static GpuPixels TexturePixels(Texture? texture, bool pageTexture = false)
            => ReadTexturePixels(texture, x, y, Width, Height, pageTexture && gpuPageRowsReflected);
        private static GpuPixels ReadTexturePixels(Texture? texture, int left, int bottom, int width, int height, bool reflected)
        {
            if (texture == null) return new GpuPixels { Reason = "No bound texture." };
            var previous = RenderTexture.active;
            Texture2D? readback = null;
            try
            {
                if (!(texture is RenderTexture render))
                    return new GpuPixels { Reason = "Bound texture is not a RenderTexture.", Type = texture.GetType().FullName ?? "" };
                RenderTexture.active = render;
                var sourceY = GpuPageReadbackPlan.ReadY(bottom, height, texture.height, reflected);
                readback = new Texture2D(width, height, TextureFormat.RGBA32, false, true);
                readback.ReadPixels(new Rect(left, sourceY, width, height), 0, 0, false);
                var raw = readback.GetPixels32();
                var pixels = Enumerable.Range(0, raw.Length).Select(index => {
                    var value = raw[GpuPageReadbackPlan.SourceIndex(index, width, height, reflected)];
                    return new[] { (int)value.r, value.g, value.b, value.a };
                }).ToArray();
                return new GpuPixels { Available = true, Format = render.format.ToString(), Rgba = pixels,
                    TextureWidth = texture.width, TextureHeight = texture.height, ReadX = left, ReadY = sourceY,
                    ReadWidth = width, ReadHeight = height, RowsReflected = reflected };
            }
            catch (UnityException error)
            { return new GpuPixels { Reason = error.Message }; }
            finally
            {
                RenderTexture.active = previous;
                if (readback != null) UnityEngine.Object.Destroy(readback);
            }
        }
    }
}
