using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Threading;
using HarmonyLib;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Retains TerrainBG's shipped compiled gas shader, stock detail/flow inputs,
    // render queue and Gas/GasFront passes. Only their three existing property
    // textures are supplied from an explicit immutable mixture publication.
    internal static class MixtureWorldRendering
    {
        private static readonly PropertyTextures.Property[] GasProperties =
        { PropertyTextures.Property.GasColour, PropertyTextures.Property.GasPressure, PropertyTextures.Property.GasDanger };
        private static readonly FieldInfo LiveProperties = AccessTools.Field(typeof(PropertyTextures), "allTextureProperties");
        private static readonly FieldInfo PressureRange = AccessTools.Field(typeof(PropertyTextures), "PressureRange");
        private static readonly FieldInfo MinimumVisibility = AccessTools.Field(typeof(PropertyTextures), "MinPressureVisibility");
        private static readonly FieldInfo LerperCamera = AccessTools.Field(typeof(TextureLerper), "cameraGO");
        private static RuntimeMixturePresentationFrame? textureFrame, overlayFrame;
        private static OpacityParameters opacity = new OpacityParameters(15, 200, 0.1);
        private static ScheduleOverride? schedule;
        private static string? installedOwner;
        private static long colourCells, pressureCells, dangerCells, textureBatches, maximumBatchTicks;
        internal static string LastError { get; private set; } = "";
        internal static bool Installed => installedOwner != null;
        internal static bool SchedulingActive => schedule != null;
        internal static long ColourCellsWritten => Interlocked.Read(ref colourCells);
        internal static long PressureCellsWritten => Interlocked.Read(ref pressureCells);
        internal static long DangerCellsWritten => Interlocked.Read(ref dangerCells);
        internal static long TextureBatches => Interlocked.Read(ref textureBatches);
        internal static long TextureWrites => ColourCellsWritten + PressureCellsWritten + DangerCellsWritten;
        // Includes stock PropertyTextures.LateUpdate and its synchronous jobs.
        internal static double MaximumTextureBatchMs => Interlocked.Read(ref maximumBatchTicks) * 1000.0 / Stopwatch.Frequency;
        internal static long LastTexturePublicationGeneration { get; private set; }

        static MixtureWorldRendering() { MixturePresentationRuntime.SessionEnding += ResetSessionRendering; }

        internal static void Install(Harmony harmony)
        {
            if (harmony == null) throw new ArgumentNullException(nameof(harmony));
            if (installedOwner != null)
            {
                if (installedOwner != harmony.Id) throw new InvalidOperationException("Mixture render hooks already belong to another Harmony owner.");
                return;
            }
            // Validate every target before installing the first hook. The mod's
            // existing binary/layout gate remains the outer version boundary.
            var late = Require(typeof(PropertyTextures), "LateUpdate", typeof(void));
            var reset = AccessTools.Method(typeof(PropertyTextures), "OnReset") ?? throw new NotSupportedException("Missing property-texture reset hook.");
            var shaders = Require(typeof(PropertyTextures), "OnShadersReloaded", typeof(void));
            var destroy = Require(typeof(PropertyTextures), nameof(PropertyTextures.DestroyInstance), typeof(void));
            var colour = Require(typeof(PropertyTextures), "UpdateGasColour", typeof(void), typeof(TextureRegion), typeof(int), typeof(int), typeof(int), typeof(int));
            var pressure = Require(typeof(PropertyTextures), "UpdatePressure", typeof(void), typeof(TextureRegion), typeof(int), typeof(int), typeof(int), typeof(int));
            var danger = Require(typeof(PropertyTextures), "UpdateDanger", typeof(void), typeof(TextureRegion), typeof(int), typeof(int), typeof(int), typeof(int));
            var overlay = Require(typeof(SimDebugView), nameof(SimDebugView.UpdateData), typeof(void), typeof(Texture2D), typeof(byte[]), typeof(HashedString), typeof(byte));
            var oxygenMap = Require(typeof(SimDebugView), "GetOxygenMapColour", typeof(Color), typeof(SimDebugView), typeof(int));
            var pressureMap = Require(typeof(SimDebugView), "GetPressureMapColour", typeof(Color), typeof(SimDebugView), typeof(int));
            if (LiveProperties == null || PressureRange == null || MinimumVisibility == null || LerperCamera == null)
                throw new NotSupportedException("Property-texture scheduling or lerper layout changed.");
            var installed = new List<(MethodBase target, MethodInfo patch)>();
            try
            {
                Patch(late, nameof(BeginTextureBatch), null, nameof(EndTextureBatch));
                Patch(reset, nameof(BeforeReset), nameof(AfterReset), null);
                Patch(shaders, null, nameof(AfterShadersReloaded), null);
                Patch(destroy, nameof(BeforeDestroy), null, null);
                Patch(colour, null, nameof(AfterGasColour), null);
                Patch(pressure, null, nameof(AfterGasPressure), null);
                Patch(danger, null, nameof(AfterGasDanger), null);
                Patch(overlay, nameof(BeginOverlayBatch), null, nameof(EndOverlayBatch));
                Patch(oxygenMap, nameof(OxygenMap), null, null);
                Patch(pressureMap, nameof(PressureMap), null, null);
                installedOwner = harmony.Id;
            }
            catch
            {
                foreach (var entry in installed) harmony.Unpatch(entry.target, entry.patch);
                throw;
            }
            void Patch(MethodBase target, string? prefix, string? postfix, string? finalizer)
            {
                HarmonyMethod? Method(string? name)
                {
                    if (name == null) return null;
                    var method = AccessTools.Method(typeof(MixtureWorldRendering), name);
                    installed.Add((target, method)); return new HarmonyMethod(method);
                }
                harmony.Patch(target, prefix: Method(prefix), postfix: Method(postfix), finalizer: Method(finalizer));
            }
        }

        private static MethodInfo Require(Type type, string name, Type result, params Type[] parameters)
        {
            var method = AccessTools.Method(type, name, parameters);
            if (method == null || method.ReturnType != result || method.GetMethodBody() == null)
                throw new NotSupportedException("Mixture renderer requires inspected managed method " + type.Name + "." + name);
            return method;
        }

        private static void BeginTextureBatch(PropertyTextures __instance, out RenderBatchState __state)
        {
            __state = new RenderBatchState(Volatile.Read(ref textureFrame));
            var acquired = MixturePresentationRuntime.AcquireFrame();
            if (acquired == null || !acquired.HasWorld)
            {
                textureFrame = null; RestoreScheduling(); return;
            }
            if (acquired.Width != Grid.WidthInCells || acquired.Height != Grid.HeightInCells)
                throw new InvalidOperationException("Mixture presentation geometry differs from the live grid.");
            EnsureScheduling(__instance);
            var range = Read<Vector2>(PressureRange, __instance);
            opacity = new OpacityParameters(range.x, range.y, Read<float>(MinimumVisibility, __instance));
            __state.Lease = MixturePresentationRuntime.EnterWorkerBatch(acquired);
            __state.StartTimestamp = Stopwatch.GetTimestamp();
            Volatile.Write(ref textureFrame, acquired);
            LastTexturePublicationGeneration = acquired.PublicationGeneration;
            Interlocked.Increment(ref textureBatches);
        }

        private static Exception? EndTextureBatch(PropertyTextures __instance, RenderBatchState? __state, Exception? __exception)
        {
            try
            {
                // Existing lerper cameras are muted; bind the completed target
                // textures directly AFTER the stock loop attempts its bindings.
                if (__state?.Lease != null && schedule != null) BindDirectTextures(__instance);
            }
            catch (Exception error)
            {
                LastError = error.ToString();
                if (__exception == null) __exception = error;
            }
            finally
            {
                if (__state != null)
                {
                    Volatile.Write(ref textureFrame, __state.Previous);
                    __state.Lease?.Dispose();
                    if (__state.StartTimestamp != 0)
                    {
                        var elapsed = Stopwatch.GetTimestamp() - __state.StartTimestamp;
                        var seen = Interlocked.Read(ref maximumBatchTicks);
                        while (elapsed > seen)
                        {
                            var replaced = Interlocked.CompareExchange(ref maximumBatchTicks, elapsed, seen);
                            if (replaced == seen) break;
                            seen = replaced;
                        }
                    }
                }
            }
            if (__exception != null) LastError = __exception.ToString();
            return __exception;
        }

        private static void BeginOverlayBatch(out RenderBatchState __state)
        {
            __state = new RenderBatchState(Volatile.Read(ref overlayFrame));
            var acquired = MixturePresentationRuntime.AcquireFrame();
            if (acquired != null) __state.Lease = MixturePresentationRuntime.EnterWorkerBatch(acquired);
            Volatile.Write(ref overlayFrame, acquired);
        }
        private static Exception? EndOverlayBatch(RenderBatchState? __state, Exception? __exception)
        {
            if (__state != null) { Volatile.Write(ref overlayFrame, __state.Previous); __state.Lease?.Dispose(); }
            return __exception;
        }

        private static void BeforeReset() { RestoreScheduling(false); }
        private static void AfterReset(PropertyTextures __instance)
        { if (MixturePresentationRuntime.HasWorldProvider) EnsureScheduling(__instance); }
        private static void AfterShadersReloaded(PropertyTextures __instance)
        { if (schedule != null) BindDirectTextures(__instance); }
        private static void BeforeDestroy() { RestoreScheduling(false); }

        private static void AfterGasColour(TextureRegion __0, int __1, int __2, int __3, int __4)
        {
            var frame = Volatile.Read(ref textureFrame); if (frame == null) return;
            long written = 0;
            for (var y = __2; y <= __4; ++y)
                for (var x = __1; x <= __3; ++x)
                {
                    var cell = x + y * frame.Width;
                    if (!Grid.IsActiveWorld(cell) || !frame.TryGetCell(cell, out var reading)) continue;
                    var sample = Sample(reading!, x, y, opacity);
                    __0.SetBytes(x, y, sample.R, sample.G, sample.B, sample.Alpha); ++written;
                }
            Interlocked.Add(ref colourCells, written);
        }

        private static void AfterGasPressure(TextureRegion __0, int __1, int __2, int __3, int __4)
        {
            var frame = Volatile.Read(ref textureFrame); if (frame == null) return;
            long written = 0;
            for (var y = __2; y <= __4; ++y)
                for (var x = __1; x <= __3; ++x)
                {
                    var cell = x + y * frame.Width;
                    if (!Grid.IsActiveWorld(cell) || !frame.TryGetCell(cell, out var reading)) continue;
                    __0.SetBytes(x, y, DensityOpacity(reading!, opacity)); ++written;
                }
            Interlocked.Add(ref pressureCells, written);
        }

        private static void AfterGasDanger(TextureRegion __0, int __1, int __2, int __3, int __4)
        {
            var frame = Volatile.Read(ref textureFrame); if (frame == null) return;
            long written = 0;
            for (var y = __2; y <= __4; ++y)
                for (var x = __1; x <= __3; ++x)
                {
                    var cell = x + y * frame.Width;
                    if (!Grid.IsActiveWorld(cell) || !frame.TryGetCell(cell, out var reading)) continue;
                    __0.SetBytes(x, y, HazardByte(reading!)); ++written;
                }
            Interlocked.Add(ref dangerCells, written);
        }

        private static bool OxygenMap(SimDebugView __0, int __1, ref Color __result)
        {
            var frame = Volatile.Read(ref overlayFrame);
            if (frame == null || !frame.TryGetCell(__1, out var reading)) return true;
            if (reading!.IsBreathable) __result = __0.breathableGradient.Evaluate(1);
            else if ((reading.Failures & (BreathingFailure.Contaminant | BreathingFailure.CarbonDioxideFractionHigh | BreathingFailure.CarbonDioxidePartialPressureHigh)) != 0
                && __0.toxicColour != null && __0.toxicColour.Length != 0) __result = __0.toxicColour[__0.toxicColour.Length - 1];
            else __result = __0.unbreathableColour;
            return false;
        }

        private static bool PressureMap(SimDebugView __0, int __1, ref Color __result)
        {
            var frame = Volatile.Read(ref overlayFrame);
            if (frame == null || !frame.TryGetCell(__1, out var reading)) return true;
            // This actual pressure overlay uses DOUBLE Pa. The legacy byte
            // texture below controls only normal-world visual density opacity.
            var range = (double)__0.maxPressureExpected - __0.minPressureExpected;
            var value = reading!.TotalPressurePa <= 0 || range <= 0 ? 0 : Clamp01((reading.TotalPressurePa / 1000 - __0.minPressureExpected) / range) * 0.9;
            __result = new Color((float)value, (float)value, (float)value, 1); return false;
        }

        internal static GasVisualSample Sample(MixturePresentation reading, double worldX, double worldY, OpacityParameters parameters)
        {
            if (reading == null) throw new ArgumentNullException(nameof(reading));
            if (reading.GasMassKg <= 0 || reading.GasVolumeCubicMetres <= 0) return default;
            var color = reading.ColorAt(worldX, worldY);
            return new GasVisualSample(Byte(color.R), Byte(color.G), Byte(color.B), byte.MaxValue,
                DensityOpacity(reading, parameters), HazardByte(reading), reading.TotalPressurePa);
        }

        internal static byte DensityOpacity(MixturePresentation reading, OpacityParameters parameters)
        {
            if (reading.GasMassKg <= 0 || reading.GasVolumeCubicMetres <= 0) return 0;
            // Preserve familiar ONI density-based visibility. 101.3 is a legacy
            // visual normalization, NOT an equation of state or pressure query.
            var normalized = Clamp01((reading.GasDensityKgPerCubicMetre * 101.3 - parameters.Minimum) / (parameters.Maximum - parameters.Minimum));
            return Byte(Math.Max(normalized, parameters.MinimumVisibility));
        }
        internal static byte HazardByte(MixturePresentation reading) => reading.GasMassKg > 0 && !reading.IsBreathable ? byte.MaxValue : (byte)0;
        private static byte Byte(double value) => (byte)Math.Round(Clamp01(value) * 255, MidpointRounding.AwayFromZero);
        private static double Clamp01(double value) => Math.Max(0, Math.Min(1, value));

        private static void EnsureScheduling(PropertyTextures instance)
        {
            if (schedule != null && ReferenceEquals(schedule.Owner, instance)) return;
            RestoreScheduling();
            schedule = new ScheduleOverride(instance);
            BindDirectTextures(instance);
        }

        private static void RestoreScheduling(bool rebind = true)
        {
            var previous = schedule; schedule = null;
            previous?.Restore(rebind);
        }

        private static void BindDirectTextures(PropertyTextures instance)
        {
            foreach (var property in GasProperties)
            {
                var index = (int)property;
                if (instance.textureBuffers == null || index >= instance.textureBuffers.Length || instance.textureBuffers[index] == null)
                    throw new InvalidOperationException("Gas property texture buffers have not been initialized.");
                Shader.SetGlobalTexture("_" + property + "Tex", instance.textureBuffers[index].texture);
            }
        }

        private static T Read<T>(FieldInfo field, object owner)
            => field.GetValue(owner) is T value ? value : throw new NotSupportedException("Invalid property-texture field " + field.Name);

        private static FieldInfo Field(Type type, string name)
            => type.GetField(name) ?? throw new NotSupportedException("Missing property-texture scheduling field " + name);

        internal static void Shutdown() => MixturePresentationRuntime.Shutdown();

        private static void ResetSessionRendering()
        {
            LastError = "";
            try { RestoreScheduling(false); }
            catch (Exception error) { LastError = error.ToString(); throw; }
            finally
            {
                Volatile.Write(ref textureFrame, null); Volatile.Write(ref overlayFrame, null);
                Interlocked.Exchange(ref colourCells, 0); Interlocked.Exchange(ref pressureCells, 0);
                Interlocked.Exchange(ref dangerCells, 0); Interlocked.Exchange(ref textureBatches, 0);
                Interlocked.Exchange(ref maximumBatchTicks, 0); LastTexturePublicationGeneration = 0;
            }
        }

        private sealed class RenderBatchState
        {
            internal readonly RuntimeMixturePresentationFrame? Previous;
            internal IDisposable? Lease;
            internal long StartTimestamp;
            internal RenderBatchState(RuntimeMixturePresentationFrame? previous) { Previous = previous; }
        }

        internal readonly struct OpacityParameters
        {
            internal double Minimum { get; }
            internal double Maximum { get; }
            internal double MinimumVisibility { get; }
            internal OpacityParameters(double minimum, double maximum, double minimumVisibility)
            {
                if (double.IsNaN(minimum) || double.IsInfinity(minimum) || double.IsNaN(maximum) || double.IsInfinity(maximum) || maximum <= minimum ||
                    double.IsNaN(minimumVisibility) || double.IsInfinity(minimumVisibility) || minimumVisibility < 0 || minimumVisibility > 1)
                    throw new ArgumentException("Invalid visual density-opacity range.");
                Minimum = minimum; Maximum = maximum; MinimumVisibility = minimumVisibility;
            }
        }

        internal readonly struct GasVisualSample
        {
            internal readonly byte R, G, B, Alpha, Opacity, Hazard;
            internal readonly double PhysicalPressurePa;
            internal GasVisualSample(byte r, byte g, byte b, byte alpha, byte opacity, byte hazard, double pressure)
            { R = r; G = g; B = b; Alpha = alpha; Opacity = opacity; Hazard = hazard; PhysicalPressurePa = pressure; }
        }

        private sealed class ScheduleOverride
        {
            internal readonly PropertyTextures Owner;
            private readonly IList entries;
            private readonly List<(int index, bool everyFrame, bool blend)> originals = new List<(int, bool, bool)>();
            private readonly List<(GameObject camera, bool active)> cameras = new List<(GameObject, bool)>();
            private readonly TextureLerper[]? lerpers;
            internal ScheduleOverride(PropertyTextures owner)
            {
                Owner = owner; entries = Read<IList>(LiveProperties, owner);
                lerpers = owner.lerpers;
                try
                {
                    for (var index = 0; index < entries.Count; ++index)
                    {
                        var boxed = entries[index] ?? throw new NotSupportedException("Null property-texture scheduling record."); var type = boxed.GetType();
                        var property = Read<PropertyTextures.Property>(Field(type, "simProperty"), boxed);
                        if (Array.IndexOf(GasProperties, property) < 0) continue;
                        var every = Field(type, "updateEveryFrame"); var blend = Field(type, "blend");
                        originals.Add((index, Read<bool>(every, boxed), Read<bool>(blend, boxed)));
                        every.SetValue(boxed, true); blend.SetValue(boxed, false); entries[index] = boxed;
                    }
                    if (originals.Count < GasProperties.Length) throw new NotSupportedException("Missing gas texture scheduling records.");
                    foreach (var property in GasProperties)
                    {
                        var lerper = lerpers?[(int)property];
                        if (lerper == null) continue;
                        var camera = (GameObject?)LerperCamera.GetValue(lerper);
                        if (camera == null) throw new NotSupportedException("Gas lerper camera is not initialized.");
                        cameras.Add((camera, camera.activeSelf)); camera.SetActive(false);
                    }
                }
                catch { Restore(false); throw; }
            }
            internal void Restore(bool rebind)
            {
                foreach (var original in originals)
                {
                    if (original.index >= entries.Count) continue;
                    var boxed = entries[original.index] ?? throw new NotSupportedException("Null property-texture scheduling record."); var type = boxed.GetType();
                    Field(type, "updateEveryFrame").SetValue(boxed, original.everyFrame);
                    Field(type, "blend").SetValue(boxed, original.blend); entries[original.index] = boxed;
                }
                foreach (var camera in cameras) if (camera.camera != null) camera.camera.SetActive(camera.active);
                if (rebind && ReferenceEquals(Owner.lerpers, lerpers) && lerpers != null)
                    foreach (var property in GasProperties)
                    {
                        var lerper = lerpers[(int)property];
                        if (lerper != null) Shader.SetGlobalTexture("_" + property + "Tex", lerper.Update());
                    }
                originals.Clear(); cameras.Clear();
            }
        }
    }
}
