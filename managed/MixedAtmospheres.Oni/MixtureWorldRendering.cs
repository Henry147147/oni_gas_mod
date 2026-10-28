using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Threading;
using HarmonyLib;
using MixedAtmospheres.Core;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    // Retains TerrainBG's shipped compiled gas shader, stock detail/flow inputs,
    // render queue and Gas/GasFront passes. Only their three existing property
    // textures are supplied from an explicit immutable mixture publication.
    internal static partial class MixtureWorldRendering
    {
        private static readonly PropertyTextures.Property[] GasProperties =
        { PropertyTextures.Property.GasColour, PropertyTextures.Property.GasPressure, PropertyTextures.Property.GasDanger };
        private static readonly FieldInfo LiveProperties = AccessTools.Field(typeof(PropertyTextures), "allTextureProperties");
        private static readonly FieldInfo PressureRange = AccessTools.Field(typeof(PropertyTextures), "PressureRange");
        private static readonly FieldInfo MinimumVisibility = AccessTools.Field(typeof(PropertyTextures), "MinPressureVisibility");
        private static readonly FieldInfo LerperCamera = AccessTools.Field(typeof(TextureLerper), "cameraGO");
        private static readonly bool compactTextures = MixturePresentationRuntime.CompactTexturesEnabled;
        internal delegate void VisibleRange(PropertyTextures owner, out int x0, out int y0, out int x1, out int y1);
        private static readonly VisibleRange? readVisibleRange = compactTextures
            ? TryBindVisibleRange(() => AccessTools.Method(typeof(PropertyTextures), "GetVisibleCellRange"))
            : null;
        internal static bool CompactRangeAvailable => readVisibleRange != null;

        // Optional cache admission must not poison the renderer's initializer.
        // Binding inspects metadata only; it never calls the game getter.
        internal static VisibleRange? TryBindVisibleRange(Func<MethodInfo?> lookup,
            Func<MethodInfo, VisibleRange>? bind = null)
        {
            try
            {
                var method = lookup();
                if (method == null || method.IsStatic || method.ReturnType != typeof(void) ||
                    method.DeclaringType == null || !method.DeclaringType.IsAssignableFrom(typeof(PropertyTextures))) return null;
                var parameters = method.GetParameters();
                if (parameters.Length != 4 || parameters.Any(p => !p.IsOut || p.ParameterType != typeof(int).MakeByRefType())) return null;
                return bind != null ? bind(method) : AccessTools.MethodDelegate<VisibleRange>(method);
            }
            catch (Exception error) when (error is ArgumentException || error is MissingMemberException ||
                error is NotSupportedException || error is InvalidOperationException || error is MemberAccessException ||
                error is TypeLoadException || error is TargetInvocationException)
            {
                return null;
            }
        }

        internal static bool TryPrepareOptionalTextures(VisibleRange? getter, PropertyTextures owner,
            RuntimeMixturePresentationFrame frame)
        {
            if (getter == null) return false;
            try
            {
                getter(owner, out var x0, out var y0, out var x1, out var y1);
                return frame.PrepareTextureScalars(x0, y0, x1, y1, out _);
            }
            catch (Exception error) when (error is NullReferenceException || error is InvalidOperationException ||
                error is ArgumentException || error is IndexOutOfRangeException)
            {
                // Camera/reset teardown can make the optional early lookup
                // unavailable. The original LateUpdate retains its own guards.
                // Sampling/model/coordinate failures remain outside this catch.
                return false;
            }
        }
        internal sealed class CompactTexturePlan
        {
            internal readonly RuntimeMixturePresentationFrame Frame;
            internal readonly OpacityParameters Parameters;
            internal readonly bool Admitted;
            internal CompactTexturePlan(RuntimeMixturePresentationFrame frame, OpacityParameters parameters, bool admitted)
            {
                Frame = frame;
                Parameters = parameters;
                Admitted = admitted;
            }
        }
        private static CompactTexturePlan? texturePlan;
        private static RuntimeMixturePresentationFrame? textureFrame, overlayFrame;
        private static OpacityParameters opacity = new OpacityParameters(15, 200, 0.1);
        private static ScheduleOverride? schedule;
        private static ValidationProbeSet? validationProbes;
        private static long validationEpoch;
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
        internal static OpacityParameters OpacitySettingsForValidation => opacity;
        internal static bool ValidationProbesActive => Volatile.Read(ref validationProbes) != null;
        internal static long ValidationProbeEpoch => Volatile.Read(ref validationProbes)?.Epoch ?? 0;

        static MixtureWorldRendering() { MixturePresentationRuntime.SessionEnding += ResetSessionRendering; }

        // The originals Install patches, for the game contract
        // (docs/game-update-resilience.md); keep in step with Install.
        internal static IEnumerable<MethodBase> ContractTargets() => new MethodBase?[]
        {
            AccessTools.Method(typeof(PropertyTextures), "LateUpdate"), AccessTools.Method(typeof(PropertyTextures), "OnReset"),
            AccessTools.Method(typeof(PropertyTextures), "OnShadersReloaded"), AccessTools.Method(typeof(PropertyTextures), nameof(PropertyTextures.DestroyInstance)),
            AccessTools.Method(typeof(PropertyTextures), "UpdateGasColour", new[] { typeof(TextureRegion), typeof(int), typeof(int), typeof(int), typeof(int) }),
            AccessTools.Method(typeof(PropertyTextures), "UpdatePressure", new[] { typeof(TextureRegion), typeof(int), typeof(int), typeof(int), typeof(int) }),
            AccessTools.Method(typeof(PropertyTextures), "UpdateDanger", new[] { typeof(TextureRegion), typeof(int), typeof(int), typeof(int), typeof(int) }),
            AccessTools.Method(typeof(SimDebugView), nameof(SimDebugView.UpdateData), new[] { typeof(Texture2D), typeof(byte[]), typeof(HashedString), typeof(byte) }),
            AccessTools.Method(typeof(SimDebugView), "GetOxygenMapColour", new[] { typeof(SimDebugView), typeof(int) }),
            AccessTools.Method(typeof(SimDebugView), "GetPressureMapColour", new[] { typeof(SimDebugView), typeof(int) }),
        }.Select(method => method ?? throw new NotSupportedException("A mixture render hook target is missing."));

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

        // Failed OnLoad teardown: Mod has already removed this owner's patches.
        internal static void ResetAfterFailedLoad() => installedOwner = null;

        private static MethodInfo Require(Type type, string name, Type result, params Type[] parameters)
        {
            var method = AccessTools.Method(type, name, parameters);
            if (method == null || method.ReturnType != result || method.GetMethodBody() == null)
                throw new NotSupportedException("Mixture renderer requires inspected managed method " + type.Name + "." + name);
            return method;
        }

        private static void BeginTextureBatch(PropertyTextures __instance, out RenderBatchState __state)
        {
            __state = new RenderBatchState(Volatile.Read(ref textureFrame),Volatile.Read(ref texturePlan));
            Volatile.Write(ref texturePlan,null);
            var acquired = MixturePresentationRuntime.AcquireFrame();
            if (acquired == null || !acquired.HasWorld)
            {
                textureFrame = null; RestoreScheduling(); return;
            }
            if (acquired.Width != Grid.WidthInCells || acquired.Height != Grid.HeightInCells)
                throw new InvalidOperationException("Mixture presentation geometry differs from the live grid.");
            EnsureScheduling(__instance);
            var admitted=false;
            if (compactTextures && Volatile.Read(ref validationProbes) == null)
                admitted = TryPrepareOptionalTextures(readVisibleRange, __instance, acquired);
            var range = Read<Vector2>(PressureRange, __instance);
            opacity = new OpacityParameters(range.x, range.y, Read<float>(MinimumVisibility, __instance));
            if(compactTextures)Volatile.Write(ref texturePlan,new CompactTexturePlan(acquired,opacity,ProofAdmit(acquired,admitted)));
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
                    Volatile.Write(ref texturePlan, __state.PreviousPlan);
                    __state.Lease?.Dispose();
                    if (__state.StartTimestamp != 0)
                    {
                        var finished = Stopwatch.GetTimestamp(); var elapsed = finished - __state.StartTimestamp;
                        DevelopmentTelemetry.Sampler.ObserveTextureBatch(LastTexturePublicationGeneration, __state.StartTimestamp, finished);
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
            if (__exception != null) { DisableValidationProbes(); LastError = __exception.ToString(); }
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

        private static void BeforeReset() { DisableValidationProbes(); RestoreScheduling(false); }
        private static void AfterReset(PropertyTextures __instance)
        { if (MixturePresentationRuntime.HasWorldProvider) EnsureScheduling(__instance); }
        private static void AfterShadersReloaded(PropertyTextures __instance)
        { if (schedule != null) BindDirectTextures(__instance); }
        private static void BeforeDestroy() { DisableValidationProbes(); RestoreScheduling(false); }

        private static void AfterGasColour(TextureRegion __0, int __1, int __2, int __3, int __4)
        {
            var plan = compactTextures ? Volatile.Read(ref texturePlan) : null;
            var frame = plan?.Frame ?? Volatile.Read(ref textureFrame);
            if (frame == null) return;
            var parameters = plan?.Parameters ?? opacity;
            var probes = Volatile.Read(ref validationProbes);
            long written = 0;
            if (probes != null)
            {
                for (var y = __2; y <= __4; ++y)
                    for (var x = __1; x <= __3; ++x)
                    {
                        var cell = x + y * frame.Width;
                        if (!Grid.IsActiveWorld(cell) || !frame.TryGetCell(cell, out var reading)) continue;
                        var sample = Sample(reading!, x, y, parameters);
                        __0.SetBytes(x, y, sample.R, sample.G, sample.B, sample.Alpha); ++written;
                        probes.Record(0, __0, cell, x, y, frame, reading!, parameters, sample.R, sample.G, sample.B, sample.Alpha);
                    }
                Interlocked.Add(ref colourCells, written); return;
            }
            if (plan?.Admitted == true)
            {
                for (var y = __2; y <= __4; ++y)
                    for (var x = __1; x <= __3; ++x)
                    {
                        var cell = x + y * frame.Width;
                        if (!Grid.IsActiveWorld(cell)) continue;
                        if (frame.TryGetTextureScalars(cell, x, y, out var scalar))
                        {
                            var sample = Sample(scalar, parameters);
                            __0.SetBytes(x, y, sample.R, sample.G, sample.B, sample.Alpha);
                            VerifyCompactWrite(0, __0, cell, x, y, frame, parameters, sample);
                            ++written;
                            continue;
                        }
                        if (!frame.TryGetCell(cell, out var reading)) continue;
                        var fallback = Sample(reading!, x, y, parameters);
                        __0.SetBytes(x, y, fallback.R, fallback.G, fallback.B, fallback.Alpha);
                        ++written;
                    }
                Interlocked.Add(ref colourCells, written);
                return;
            }
            ProofFullJob();
            // The normal path retains its original loop: no per-cell probe
            // check, provider access or diagnostic allocation when disabled.
            for (var y = __2; y <= __4; ++y)
                for (var x = __1; x <= __3; ++x)
                {
                    var cell = x + y * frame.Width;
                    if (!Grid.IsActiveWorld(cell) || !frame.TryGetCell(cell, out var reading)) continue;
                    var sample = Sample(reading!, x, y, parameters);
                    __0.SetBytes(x, y, sample.R, sample.G, sample.B, sample.Alpha); ++written;
                }
            Interlocked.Add(ref colourCells, written);
        }

        private static void AfterGasPressure(TextureRegion __0, int __1, int __2, int __3, int __4)
        {
            var plan = compactTextures ? Volatile.Read(ref texturePlan) : null;
            var frame = plan?.Frame ?? Volatile.Read(ref textureFrame);
            if (frame == null) return;
            var parameters = plan?.Parameters ?? opacity;
            var probes = Volatile.Read(ref validationProbes);
            long written = 0;
            if (probes != null)
            {
                for (var y = __2; y <= __4; ++y)
                    for (var x = __1; x <= __3; ++x)
                    {
                        var cell = x + y * frame.Width;
                        if (!Grid.IsActiveWorld(cell) || !frame.TryGetCell(cell, out var reading)) continue;
                        var value = WorldFluidOpacity(reading!, parameters);
                        __0.SetBytes(x, y, value); ++written;
                        probes.Record(1, __0, cell, x, y, frame, reading!, parameters, value, 0, 0, 0);
                    }
                Interlocked.Add(ref pressureCells, written); return;
            }
            if (plan?.Admitted == true)
            {
                for (var y = __2; y <= __4; ++y)
                    for (var x = __1; x <= __3; ++x)
                    {
                        var cell = x + y * frame.Width;
                        if (!Grid.IsActiveWorld(cell)) continue;
                        if (frame.TryGetTextureScalars(cell, x, y, out var scalar))
                        {
                            var sample = Sample(scalar, parameters);
                            __0.SetBytes(x, y, sample.Opacity);
                            VerifyCompactWrite(1, __0, cell, x, y, frame, parameters, sample);
                            ++written;
                            continue;
                        }
                        if (!frame.TryGetCell(cell, out var reading)) continue;
                        __0.SetBytes(x, y, WorldFluidOpacity(reading!, parameters));
                        ++written;
                    }
                Interlocked.Add(ref pressureCells, written);
                return;
            }
            ProofFullJob();
            for (var y = __2; y <= __4; ++y)
                for (var x = __1; x <= __3; ++x)
                {
                    var cell = x + y * frame.Width;
                    if (!Grid.IsActiveWorld(cell) || !frame.TryGetCell(cell, out var reading)) continue;
                    __0.SetBytes(x, y, WorldFluidOpacity(reading!, parameters)); ++written;
                }
            Interlocked.Add(ref pressureCells, written);
        }

        private static void AfterGasDanger(TextureRegion __0, int __1, int __2, int __3, int __4)
        {
            var plan = compactTextures ? Volatile.Read(ref texturePlan) : null;
            var frame = plan?.Frame ?? Volatile.Read(ref textureFrame);
            if (frame == null) return;
            var parameters = plan?.Parameters ?? opacity;
            var probes = Volatile.Read(ref validationProbes);
            long written = 0;
            if (probes != null)
            {
                for (var y = __2; y <= __4; ++y)
                    for (var x = __1; x <= __3; ++x)
                    {
                        var cell = x + y * frame.Width;
                        if (!Grid.IsActiveWorld(cell) || !frame.TryGetCell(cell, out var reading)) continue;
                        var value = HazardByte(reading!);
                        __0.SetBytes(x, y, value); ++written;
                        probes.Record(2, __0, cell, x, y, frame, reading!, parameters, value, 0, 0, 0);
                    }
                Interlocked.Add(ref dangerCells, written); return;
            }
            if (plan?.Admitted == true)
            {
                for (var y = __2; y <= __4; ++y)
                    for (var x = __1; x <= __3; ++x)
                    {
                        var cell = x + y * frame.Width;
                        if (!Grid.IsActiveWorld(cell)) continue;
                        if (frame.TryGetTextureScalars(cell, x, y, out var scalar))
                        {
                            var sample = Sample(scalar, parameters);
                            __0.SetBytes(x, y, sample.Hazard);
                            VerifyCompactWrite(2, __0, cell, x, y, frame, parameters, sample);
                            ++written;
                            continue;
                        }
                        if (!frame.TryGetCell(cell, out var reading)) continue;
                        __0.SetBytes(x, y, HazardByte(reading!));
                        ++written;
                    }
                Interlocked.Add(ref dangerCells, written);
                return;
            }
            ProofFullJob();
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
            var color = reading.WorldFluidColorAt(worldX, worldY);
            return new GasVisualSample(Byte(color.R), Byte(color.G), Byte(color.B), byte.MaxValue,
                WorldFluidOpacity(reading, parameters), HazardByte(reading), reading.TotalPressurePa);
        }

        internal static GasVisualSample Sample(MixtureTextureScalars reading, OpacityParameters parameters)
        {
            if (reading.GasMassKg <= 0 || reading.GasVolumeCubicMetres <= 0) return default;
            var color = reading.Color;
            return new GasVisualSample(Byte(color.R), Byte(color.G), Byte(color.B), byte.MaxValue,
                WorldFluidOpacity(reading, parameters), HazardByte(reading), reading.TotalPressurePa);
        }
        internal static byte WorldFluidOpacity(MixtureTextureScalars reading, OpacityParameters parameters)
        {
            var fraction = reading.GasMassKg <= 0 || reading.GasVolumeCubicMetres <= 0 ? 0 :
                Math.Max(Clamp01((reading.GasDensityKgPerCubicMetre * 101.3 - parameters.Minimum) /
                    (parameters.Maximum - parameters.Minimum)), parameters.MinimumVisibility);
            return Byte(reading.HasPhaseVolumeAppearance ?
                fraction * reading.GasVolumeCubicMetres / reading.VolumeCubicMetres +
                    reading.LiquidVolumeCubicMetres / reading.VolumeCubicMetres : fraction);
        }
        internal static byte HazardByte(MixtureTextureScalars reading)
            => reading.GasMassKg > 0 && !reading.IsBreathable ? byte.MaxValue : (byte)0;
        internal static GasVisualSample SampleForValidation(MixturePresentation reading, double worldX, double worldY)
            => Sample(reading, worldX, worldY, opacity);

        internal static byte DensityOpacity(MixturePresentation reading, OpacityParameters parameters)
            => Byte(GasOpacityFraction(reading, parameters));

        private static double GasOpacityFraction(MixturePresentation reading, OpacityParameters parameters)
        {
            if (reading.GasMassKg <= 0 || reading.GasVolumeCubicMetres <= 0) return 0;
            // Preserve familiar ONI density-based visibility. 101.3 is a legacy
            // visual normalization, NOT an equation of state or pressure query.
            var normalized = Clamp01((reading.GasDensityKgPerCubicMetre * 101.3 - parameters.Minimum) / (parameters.Maximum - parameters.Minimum));
            return Math.Max(normalized, parameters.MinimumVisibility);
        }

        internal static byte WorldFluidOpacity(MixturePresentation reading, OpacityParameters parameters)
        {
            if (!reading.HasPhaseVolumeAppearance) return DensityOpacity(reading, parameters);
            var physical = reading.ThermodynamicReading!;
            // Opaque liquid and the existing gas visibility are composited by
            // actual occupied phase volume. This is a visual approximation,
            // independent of physical pressure and gas breathing quantities.
            return Byte(GasOpacityFraction(reading, parameters) * physical.GasVolumeCubicMetres / physical.VolumeCubicMetres +
                physical.LiquidVolumeCubicMetres / physical.VolumeCubicMetres);
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
            DisableValidationProbes();
            Volatile.Write(ref texturePlan, null);
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
            internal readonly CompactTexturePlan? PreviousPlan;
            internal IDisposable? Lease;
            internal long StartTimestamp;
            internal RenderBatchState(RuntimeMixturePresentationFrame? previous, CompactTexturePlan? previousPlan = null)
            {
                Previous = previous;
                PreviousPlan = previousPlan;
            }
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

        // Explicit private validation only. Retains at most16 known cells and
        // three latest writes per cell. It never schedules work or changes a
        // texture. NativeArray reads below observe the successful CPU write;
        // neither those reads nor screenshots are GPU texture readback proof.
        internal static void EnableValidationProbes(RuntimeMixturePresentationFrame frame, IReadOnlyList<int> cells, bool explicitPrivateOptIn)
        {
            if (!explicitPrivateOptIn || !frame.IsFixture || !frame.HasWorld)
                throw new InvalidOperationException("Texture probes require an explicit private world fixture.");
            var owner = PropertyTextures.instance ?? throw new InvalidOperationException("Property textures are unavailable.");
            var buffers = new TextureBuffer[3];
            for (var i = 0; i < buffers.Length; ++i)
                buffers[i] = owner.textureBuffers[(int)GasProperties[i]] ?? throw new InvalidOperationException("A gas property buffer is unavailable.");
            var next = new ValidationProbeSet(Interlocked.Increment(ref validationEpoch), frame.Width, frame.Height, cells, buffers);
            Interlocked.Exchange(ref validationProbes, next)?.Stop();
        }

        internal static void DisableValidationProbes() => Interlocked.Exchange(ref validationProbes, null)?.Stop();

        internal static TextureProbeEvidence CaptureValidationProbe(RuntimeMixturePresentationFrame frame, int cell)
            => (Volatile.Read(ref validationProbes) ?? throw new InvalidOperationException("Private texture probes are inactive.")).Capture(frame, cell);

        internal sealed class TextureProbeChannelEvidence
        {
            public string Channel { get; }
            public bool Recorded { get; }
            public long ProbeEpoch { get; }
            public int Cell { get; }
            public int X { get; }
            public int Y { get; }
            public int RegionX { get; }
            public int RegionY { get; }
            public int TargetWidth { get; }
            public int TargetHeight { get; }
            public int PageWidth { get; }
            public int BytesPerPixel { get; }
            public int ByteOffset { get; }
            public int DestinationBytesLength { get; }
            public int DestinationBufferIdentity { get; }
            public int DestinationPageIdentity { get; }
            public int UnityFrame { get; }
            public long Revision { get; }
            public long PublicationGeneration { get; }
            public long SessionEpoch { get; }
            public double MinimumVisualDensityRange { get; }
            public double MaximumVisualDensityRange { get; }
            public double MinimumVisibility { get; }
            public double PhysicalPressurePa { get; }
            public double GasDensityKgPerCubicMetre { get; }
            public double TotalMassKg { get; }
            public double GasMassKg { get; }
            public double GasVolumeCubicMetres { get; }
            public double LiquidVolumeFraction { get; }
            public IReadOnlyList<byte> WriteArguments { get; }
            public IReadOnlyList<byte> ReadBackBytes { get; }
            public IReadOnlyList<byte> ExpectedBytes { get; }
            public bool ExactPinnedReading { get; }
            public bool ExactPinnedModel { get; }
            public bool ExpectedDestinationBuffer { get; }
            public bool CorrectAddress { get; }
            public bool Fresh { get; }
            public bool Passed { get; }
            public string Error { get; }
            internal TextureProbeChannelEvidence(int channel, long epoch, WriteObservation value,
                RuntimeMixturePresentationFrame expectedFrame, MixturePresentation? expectedReading, TextureBuffer buffer)
            {
                Channel = GasProperties[channel].ToString(); Recorded = value.Recorded; ProbeEpoch = epoch;
                Cell = value.Cell; X = value.X; Y = value.Y; RegionX = value.RegionX; RegionY = value.RegionY;
                TargetWidth = value.TargetWidth; TargetHeight = value.TargetHeight; PageWidth = value.PageWidth;
                BytesPerPixel = value.BytesPerPixel; ByteOffset = value.Offset; DestinationBytesLength = value.Length;
                DestinationBufferIdentity = ReferenceEquals(value.Buffer, null) ? 0 : RuntimeHelpers.GetHashCode(value.Buffer);
                DestinationPageIdentity = ReferenceEquals(value.Page, null) ? 0 : RuntimeHelpers.GetHashCode(value.Page);
                UnityFrame = value.Frame?.UnityFrame ?? -1; Revision = value.Frame?.Revision ?? -1;
                PublicationGeneration = value.Frame?.PublicationGeneration ?? -1; SessionEpoch = value.Frame?.SessionEpoch ?? -1;
                MinimumVisualDensityRange = value.Opacity.Minimum; MaximumVisualDensityRange = value.Opacity.Maximum;
                MinimumVisibility = value.Opacity.MinimumVisibility;
                PhysicalPressurePa = value.Reading?.TotalPressurePa ?? 0; GasDensityKgPerCubicMetre = value.Reading?.GasDensityKgPerCubicMetre ?? 0;
                TotalMassKg = value.Reading?.TotalMassKg ?? 0; GasMassKg = value.Reading?.GasMassKg ?? 0;
                GasVolumeCubicMetres = value.Reading?.GasVolumeCubicMetres ?? 0; LiquidVolumeFraction = value.Reading?.LiquidVolumeFraction ?? 0;
                ExactPinnedReading = expectedReading != null && ReferenceEquals(value.Reading, expectedReading);
                ExactPinnedModel = value.Frame != null && ReferenceEquals(value.Frame.Model, expectedFrame.Model);
                ExpectedDestinationBuffer = ReferenceEquals(value.Buffer, buffer);
                var count = channel == 0 ? 4 : 1;
                CorrectAddress = value.Recorded && value.X == value.Cell % expectedFrame.Width && value.Y == value.Cell / expectedFrame.Width &&
                    TryByteOffset(value.RegionX, value.RegionY, value.TargetWidth, value.TargetHeight, value.PageWidth,
                        value.BytesPerPixel, value.Length, value.X, value.Y, count, out var offset) && offset == value.Offset;
                var age = (long)expectedFrame.UnityFrame - UnityFrame;
                Fresh = value.Frame != null && age >= 0 && age <= 2 && Revision == expectedFrame.Revision &&
                    PublicationGeneration == expectedFrame.PublicationGeneration && SessionEpoch == expectedFrame.SessionEpoch;
                WriteArguments = Bytes(value.W0, value.W1, value.W2, value.W3, count);
                ReadBackBytes = Bytes(value.R0, value.R1, value.R2, value.R3, count);
                var expected = expectedReading == null || !value.Recorded ? default : Sample(expectedReading, value.X, value.Y, value.Opacity);
                ExpectedBytes = channel == 0 ? Bytes(expected.R, expected.G, expected.B, expected.Alpha, 4) :
                    Bytes(channel == 1 ? expected.Opacity : expected.Hazard, 0, 0, 0, 1);
                var bytesMatch = true;
                for (var i = 0; i < count; ++i) bytesMatch &= WriteArguments[i] == ReadBackBytes[i] && ReadBackBytes[i] == ExpectedBytes[i];
                Error = value.Error == 0 ? "" : "Actual texture CPU bytes could not be safely observed; code=" + value.Error;
                Passed = Recorded && Error.Length == 0 && ExactPinnedReading && ExactPinnedModel && ExpectedDestinationBuffer && CorrectAddress && Fresh && bytesMatch;
            }
            private static IReadOnlyList<byte> Bytes(byte a, byte b, byte c, byte d, int count)
                => Array.AsReadOnly(count == 1 ? new[] { a } : new[] { a, b, c, d });
        }

        internal sealed class TextureProbeEvidence
        {
            public string Scope => "Actual renderer write arguments, destination indexing and immediate CPU-buffer readback; not GPU readback or fluid ownership.";
            public long ProbeEpoch { get; }
            public int Cell { get; }
            public int CurrentUnityFrame { get; }
            public long Revision { get; }
            public long PublicationGeneration { get; }
            public bool SameCompletedBatch { get; }
            public bool ActiveProbe { get; }
            public bool Passed { get; }
            public IReadOnlyList<TextureProbeChannelEvidence> Channels { get; }
            internal TextureProbeEvidence(long epoch, int cell, RuntimeMixturePresentationFrame frame, bool active,
                WriteObservation[] observations, TextureBuffer[] buffers)
            {
                ProbeEpoch = epoch; Cell = cell; CurrentUnityFrame = frame.UnityFrame; Revision = frame.Revision;
                PublicationGeneration = frame.PublicationGeneration; ActiveProbe = active;
                frame.TryGetCell(cell, out var reading);
                var channels = new TextureProbeChannelEvidence[3];
                var pass = active;
                for (var i = 0; i < channels.Length; ++i) { channels[i] = new TextureProbeChannelEvidence(i, epoch, observations[i], frame, reading, buffers[i]); pass &= channels[i].Passed; }
                SameCompletedBatch = observations[0].Frame != null && ReferenceEquals(observations[0].Frame, observations[1].Frame) &&
                    ReferenceEquals(observations[0].Frame, observations[2].Frame);
                Passed = pass && SameCompletedBatch; Channels = Array.AsReadOnly(channels);
            }
        }

        internal struct WriteObservation
        {
            internal bool Recorded;
            internal int Cell, X, Y, RegionX, RegionY, TargetWidth, TargetHeight, PageWidth, BytesPerPixel, Offset, Length, Error;
            internal TextureBuffer? Buffer;
            internal TexturePage? Page;
            internal RuntimeMixturePresentationFrame? Frame;
            internal MixturePresentation? Reading;
            internal OpacityParameters Opacity;
            internal byte W0, W1, W2, W3, R0, R1, R2, R3;
        }

        private static bool TryByteOffset(int regionX, int regionY, int width, int height, int pageWidth, int bytesPerPixel,
            int bytesLength, int x, int y, int count, out int offset)
        {
            offset = -1; var dx = (long)x - regionX; var dy = (long)y - regionY;
            if (width <= 0 || height <= 0 || pageWidth < width || dx < 0 || dx >= width || dy < 0 || dy >= height || bytesPerPixel < count) return false;
            var pixel = dx + dy * pageWidth;
            if (bytesLength < count || pixel > (bytesLength - count) / (long)bytesPerPixel) return false;
            var index = pixel * bytesPerPixel;
            offset = (int)index; return true;
        }

        internal sealed class ValidationProbeSet
        {
            private sealed class CellProbe
            {
                internal readonly object Sync = new object();
                internal readonly WriteObservation[] Channels = new WriteObservation[3];
            }
            private readonly Dictionary<int, CellProbe> cells = new Dictionary<int, CellProbe>();
            private readonly TextureBuffer[] buffers;
            private readonly long epoch;
            private readonly int width, height;
            private int active = 1;
            internal long Epoch => epoch;
            internal ValidationProbeSet(long epoch, int width, int height, IReadOnlyList<int> knownCells, TextureBuffer[] buffers)
            {
                if (epoch <= 0 || width <= 0 || height <= 0 || (long)width * height > 16_777_216 || knownCells == null || knownCells.Count == 0 || knownCells.Count > 16 || buffers == null || buffers.Length != 3)
                    throw new ArgumentException("Bounded texture-probe declaration is invalid.");
                this.epoch = epoch; this.width = width; this.height = height; this.buffers = (TextureBuffer[])buffers.Clone();
                foreach (var buffer in this.buffers) if (ReferenceEquals(buffer, null)) throw new ArgumentException("A texture-probe destination is missing.");
                foreach (var cell in knownCells)
                {
                    if (cell < 0 || cell >= (long)width * height || cells.ContainsKey(cell)) throw new ArgumentException("Texture probe cell is duplicate or outside the declared grid.");
                    cells.Add(cell, new CellProbe());
                }
            }
            internal void Stop() => Volatile.Write(ref active, 0);
            internal void Record(int channel, TextureRegion region, int cell, int x, int y, RuntimeMixturePresentationFrame frame,
                MixturePresentation reading, OpacityParameters opacity, byte a, byte b, byte c, byte d)
            {
                if (Volatile.Read(ref active) == 0 || !cells.ContainsKey(cell)) return;
                var value = new WriteObservation { Recorded = true, Cell = cell, X = x, Y = y, RegionX = region.x, RegionY = region.y,
                    TargetWidth = region.targetWidth, TargetHeight = region.targetHeight, PageWidth = region.pageWidth, BytesPerPixel = region.bytesPerPixel,
                    Buffer = region.buffer, Page = region.page, Frame = frame, Reading = reading, Opacity = opacity, W0 = a, W1 = b, W2 = c, W3 = d };
                try
                {
                    value.Length = region.bytes.Length;
                    if (!TryByteOffset(value.RegionX, value.RegionY, value.TargetWidth, value.TargetHeight, value.PageWidth, value.BytesPerPixel,
                        value.Length, x, y, channel == 0 ? 4 : 1, out value.Offset)) value.Error = 1;
                    else
                    {
                        value.R0 = region.bytes[value.Offset];
                        if (channel == 0) { value.R1 = region.bytes[value.Offset + 1]; value.R2 = region.bytes[value.Offset + 2]; value.R3 = region.bytes[value.Offset + 3]; }
                    }
                }
                catch (Exception) { value.Error = 2; }
                RecordBytes(channel, value);
            }
            // Factored managed bookkeeping; host tests supply controlled byte
            // observations and never execute Unity NativeArray allocation/API.
            internal void RecordBytes(int channel, WriteObservation value)
            {
                if (channel < 0 || channel >= 3 || Volatile.Read(ref active) == 0 || value.Frame == null ||
                    value.Frame.Width != width || value.Frame.Height != height || !cells.TryGetValue(value.Cell, out var cell)) return;
                lock (cell.Sync) { if (Volatile.Read(ref active) != 0) cell.Channels[channel] = value; }
            }
            internal TextureProbeEvidence Capture(RuntimeMixturePresentationFrame frame, int knownCell)
            {
                if (frame.Width != width || frame.Height != height) throw new ArgumentException("Texture probe geometry differs from the observed frame.");
                if (!cells.TryGetValue(knownCell, out var cell)) throw new ArgumentOutOfRangeException(nameof(knownCell));
                WriteObservation[] copy;
                lock (cell.Sync) copy = (WriteObservation[])cell.Channels.Clone();
                return new TextureProbeEvidence(epoch, knownCell, frame, Volatile.Read(ref active) != 0, copy, buffers);
            }
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
