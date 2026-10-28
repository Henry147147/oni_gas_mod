using System.Diagnostics;
using System.Runtime.CompilerServices;
using MixedAtmospheres.Core;
using MixedAtmospheres.Oni;
static partial class Proof
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    static WeakReference TemporaryModel(ColonyCompactReadings.Cache cache, MixturePresentationModel original)
    {
        var temporary = original.WithVisualOptions(new MixtureVisualOptions { PatternContrast = .125 });
        cache.Key(temporary, 918);
        return new WeakReference(temporary);
    }
    static unsafe int SlotBytes() => sizeof(ColonyCompactReadings.Cache.Slot);
    static (ColonyReadingBuffer Buffer, ColonyPresentation.Table Table) BufferOf(List<(ColonyPresentation.Table Table, OniNativeColony.Reading Cell)> cases, int count)
    {
        var readings = new OniNativeColony.Reading[count];
        var parts = new List<OniNativeColony.Constituent>();
        var first = cases[0].Table;
        for (int i = 0; i < count; ++i)
        {
            var item = cases[i % cases.Count];
            var c = item.Cell;
            c.PublicCell = (uint)i;
            c.FirstConstituent = (uint)parts.Count;
            for (int j = 0; j < c.Constituents; ++j)
                parts.Add(item.Table.Parts[item.Cell.FirstConstituent + j]);
            readings[i] = c;
        }
        var array = parts.ToArray();
        var buffer = new ColonyReadingBuffer { Readings = readings, Parts = array, Count = count, PartCount = array.Length, Frame = 7, Lease = 918, Sorted = true, Pooled = true, CompactCache = new ColonyCompactReadings.Cache(count, 918) };
        return (buffer, new ColonyPresentation.Table(first.Catalog, first.Ids, first.Phases, first.MolarMasses, readings, array));
    }
    static void Lifetime(MixturePresentationModel[] models, List<(ColonyPresentation.Table Table, OniNativeColony.Reading Cell)> cases)
    {
        ColonyReadingPool.Reset();
        var pooled = ColonyReadingPool.Acquire(4, 4);
        Check((pooled.CompactCache != null) == ColonyCompactReadings.Enabled, "cache allocation follows immutable environment opt-out");
        var firstCache = pooled.CompactCache;
        ColonyReadingPool.Release(pooled);
        pooled.ReleasedAt = Stopwatch.GetTimestamp() - 3 * Stopwatch.Frequency;
        var reused = ColonyReadingPool.Acquire(10000, 10000);
        Check(ReferenceEquals(pooled, reused), "pool buffer reused after grace");
        Check(ReferenceEquals(firstCache, reused.CompactCache), "cache fixed across reading-array growth");
        Check(ColonyReadingPool.CompactSlots == (ColonyCompactReadings.Enabled ? 4093 : 0), "live pool slots reflect enabled mode");
        ColonyReadingPool.Reset();
        Check(ColonyReadingPool.CompactSlots == 0, "pool reset releases live cache accounting");
        var pair = BufferOf(cases, 100);
        var b = pair.Buffer;
        int rejected = 0;
        var readings = new ColonyLazyReadings(b, pair.Table, 7, (_, _) => ++rejected);
        var old = readings.Describe(models[0]);
        var compact = (ICompactTextureSource)old;
        for (int i = 0; i < 100; ++i)
        {
            Check(compact.TrySampleTexture(i, i % 10, i / 10, out var c), "compact cache available");
            var full = old[i];
            var rgb = full.WorldFluidColorAt(i % 10, i / 10);
            Exact(rgb.R, c.Color.R, "cached color");
            Check(compact.TrySampleTexture(i, i % 10, i / 10, out var hit), "compact cache hit");
            Exact(c.Color.R, hit.Color.R, "hit exact");
            Check(compact.TrySampleTexture(i, -.125, 100.75, out var moved), "coordinate change");
            Exact(full.WorldFluidColorAt(-.125, 100.75).R, moved.Color.R, "coordinate key");
        }
        b.CompactCache = new ColonyCompactReadings.Cache(1, b.Lease);
        for (int pass = 0; pass < 3; ++pass)
            for (int i = 0; i < 100; ++i)
            {
                Check(compact.TrySampleTexture(i, .5, .75, out var c), "collision eviction sample");
                Exact(old[i].WorldFluidColorAt(.5, .75).R, c.Color.R, "collision eviction exact");
            }
        var parameters = new MixtureWorldRendering.OpacityParameters(15, 200, .1);
        for (int i = 0; i < 100; ++i)
        {
            Check(compact.TrySampleTexture(i, .5, .75, out var c), "actual renderer scalar input");
            var expected = MixtureWorldRendering.Sample(old[i], .5, .75, parameters);
            var actual = MixtureWorldRendering.Sample(c, parameters);
            Check(expected.R == actual.R && expected.G == actual.G && expected.B == actual.B && expected.Alpha == actual.Alpha && expected.Opacity == actual.Opacity && expected.Hazard == actual.Hazard, "actual renderer exact bytes");
            Exact(expected.PhysicalPressurePa, actual.PhysicalPressurePa, "actual renderer diagnostic pressure");
        }
        var alternate = (ICompactTextureSource)readings.Describe(models[2]);
        for (int i = 0; i < 100; ++i)
        {
            Check(alternate.TrySampleTexture(i, .25, .75, out var c), "new model");
            Exact(models[2].Describe(ColonyPresentation.Create(pair.Table, b.Readings[i], 7)).WorldFluidColorAt(.25, .75).R, c.Color.R, "new options color");
            Check(compact.TrySampleTexture(i, .25, .75, out var restored), "old pinned model");
            Exact(old[i].WorldFluidColorAt(.25, .75).R, restored.Color.R, "old model restored");
        }
        Parallel.For(0, 8, worker => { var m = models[worker % models.Length]; var source = (ICompactTextureSource)readings.Describe(m); for (int i = 0; i < 3000; ++i) { var index = i % 100; if (!source.TrySampleTexture(index, .375, .625, out var got)) throw new Exception("parallel missing"); var expected = m.Describe(ColonyPresentation.Create(pair.Table, b.Readings[index], 7)).WorldFluidColorAt(.375, .625); if (BitConverter.DoubleToInt64Bits(got.Color.R) != BitConverter.DoubleToInt64Bits(expected.R)) throw new Exception("parallel cache model tear"); } });
        // A held per-cell writer causes a lock-free raw fallback. Reuse never resets
        // the slot gate while an old lease may still own it.
        b.CompactCache!.Slots[0].Gate = 1;
        Check(compact.TrySampleTexture(0, 0, 0, out _), "busy cache raw fallback");
        b.CompactCache.Slots[0].Gate = 0;
        var staleBefore = ColonyReadingPool.StaleReads;
        ++b.Lease;
        Check(!compact.TrySampleTexture(0, 0, 0, out _), "retired buffer unavailable");
        Check(ColonyReadingPool.StaleReads == staleBefore, "shortcut leaves rejection accounting to full fallback");
        Check(old.TryGetValue(99, out var retained) && retained.Revision == 7, "old full cache retains immutable already-built reading");
        b.CompactCache!.Activate(b.Lease);
        var replacement = new ColonyLazyReadings(b, pair.Table, 7, (_, _) => ++rejected);
        var current = (ICompactTextureSource)replacement.Describe(models[0]);
        Check(current.TrySampleTexture(0, 0, 0, out var afterReuse), "same-frame lifecycle replacement");
        Exact(models[0].Describe(ColonyPresentation.Create(pair.Table, b.Readings[0], 7)).WorldFluidColorAt(0, 0).R, afterReuse.Color.R, "lease key invalidation");
        var duplicate = BufferOf(cases, 1);
        var invalid = duplicate.Buffer.Readings[0];
        duplicate.Table.Ids[1] = new string(duplicate.Table.Ids[0].ToCharArray());
        var duplicateParts = new[] { new OniNativeColony.Constituent { Species = 0, MassKg = 1, PartialVolumeCubicMetres = .5 }, new OniNativeColony.Constituent { Species = 1, MassKg = 1, PartialVolumeCubicMetres = .5 } };
        invalid.Constituents = 2;
        invalid.TemperatureKelvin = 300;
        invalid.GasPressurePa = invalid.MechanicalPressurePa = 100000;
        invalid.OccupiedVolumeCubicMetres = 1;
        invalid.LiquidVolumeCubicMetres = 0;
        invalid.FirstConstituent = 0;
        var badTable = new ColonyPresentation.Table(duplicate.Table.Catalog, duplicate.Table.Ids, duplicate.Table.Phases, duplicate.Table.MolarMasses, new[] { invalid }, duplicateParts);
        var badBuffer = new ColonyReadingBuffer { Readings = new[] { invalid }, Parts = duplicateParts, Count = 1, PartCount = 2, Frame = 7, Lease = 77, Sorted = true, CompactCache = new ColonyCompactReadings.Cache(1, 77) };
        int badRejects = 0;
        string? badDetail = null;
        var badReadings = new ColonyLazyReadings(badBuffer, badTable, 7, (_, detail) => { ++badRejects; badDetail = detail; });
        var badFull = badReadings.Describe(models[0]);
        var badCompact = (ICompactTextureSource)badFull;
        for (int i = 0; i < 3; ++i)
        {
            Check(!badCompact.TrySampleTexture(0, 0, 0, out _), "invalid compact falls back");
            Check(!badFull.TryGetValue(0, out _), "full fallback preserves rejection");
        }
        Check(badRejects == 1 && badDetail!.Contains("Duplicate constituent identity"), "duplicate rejection count/detail exact");
        duplicate.Table.Ids[1] = "nitrogen";
        // Existing runtime immutable-frame and nested worker scopes remain the only
        // authority for publication/model/session pinning.
        MixturePresentationRuntime.ResetProviders();
        MixturePresentationRuntime.Configure(models[0]);
        var session = MixturePresentationRuntime.NativeDomainSession;
        Check(MixturePresentationRuntime.PublishNativeDomainFrame(10, 10, 7, replacement, session), "runtime first publication");
        var frame = MixturePresentationRuntime.AcquireFrameFor(100)!;
        using (var lease = MixturePresentationRuntime.EnterWorkerBatch(frame))
        {
            Check(ReferenceEquals(MixturePresentationRuntime.AcquireFrame(), frame), "worker frame pinned");
            Check(frame.TryGetTextureScalars(0, 0, 0, out var original), "runtime compact route");
            MixturePresentationRuntime.Configure(models[2]);
            Check(ReferenceEquals(MixturePresentationRuntime.AcquireFrameFor(100), frame), "same Unity frame model pin");
            var newFrame = MixturePresentationRuntime.AcquireFrameFor(101)!;
            Check(!ReferenceEquals(newFrame.Model, frame.Model), "options model next frame");
            Check(newFrame.TryGetTextureScalars(0, 0, 0, out _), "new options route");
            Check(frame.TryGetTextureScalars(0, 0, 0, out var stillOld), "old pinned source remains coherent");
            Exact(original.Color.R, stillOld.Color.R, "pinned options exact");
            using (var inner = MixturePresentationRuntime.EnterWorkerBatch(newFrame))
                Check(ReferenceEquals(MixturePresentationRuntime.AcquireFrame(), newFrame), "nested frame pin");
            Check(ReferenceEquals(MixturePresentationRuntime.AcquireFrame(), frame), "nested frame restore");
            MixturePresentationRuntime.ResetProviders();
            bool denied = false;
            try
            {
                using var forbidden = MixturePresentationRuntime.EnterWorkerBatch(frame);
            }
            catch (InvalidOperationException) { denied = true; }
            Check(denied, "retired session cannot repin");
        }
        Check(!MixturePresentationRuntime.HasProvider, "old lease cannot restore retired provider");
        // Cache does not retain a provider, source array or table; only one current
        // model key is held per buffer, and explicit teardown drops that model.
        var cache = b.CompactCache;
        cache.RetireModel();
        Check(cache.Slots.Length <= 4093, "bounded cache slots");
        cache.Activate(918);
        var oldModel = TemporaryModel(cache, models[0]);
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        Check(oldModel.IsAlive, "cache keeps current model while active");
        cache.RetireModel();
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        Check(!oldModel.IsAlive, "retirement releases old model; numeric slots do not pin it");
        Check(cache.Key(models[0], 918) == null, "old lease cannot reinstall retired model");
        cache.Activate(b.Lease);
        foreach (var field in typeof(ColonyCompactReadings.Cache.Slot).GetFields(System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance))
            Check(!field.FieldType.IsClass, "numeric cache slots retain no source/model/provider refs");
        Check(current.TrySampleTexture(0, 0, 0, out _), "cache allocation warmup");
        var cacheBefore = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 20000; ++i)
            if (!current.TrySampleTexture(0, 0, 0, out _))
                throw new Exception("cache warm missing");
        Check(GC.GetAllocatedBytesForCurrentThread() - cacheBefore == 0, "warmed cached sampling allocation zero");
        Console.WriteLine($"PASS compact cache/runtime lifecycle, model/coordinates, nested worker/session/lease, concurrent-model controls; slot_bytes {SlotBytes()} worst48buffer_cache_bytes {48L * 4093 * SlotBytes()}");
    }
    static void CacheBenchmark(MixturePresentationModel model, List<(ColonyPresentation.Table Table, OniNativeColony.Reading Cell)> cases)
    {
        var pair = BufferOf(cases, 48000);
        var b = pair.Buffer;
        foreach (var mode in new[] { "full", "compact", "full", "compact" })
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            var start = GC.GetAllocatedBytesForCurrentThread();
            var watch = Stopwatch.StartNew();
            for (int batch = 0; batch < 60; ++batch)
            {
                ++b.Lease;
                b.CompactCache!.Activate(b.Lease);
                var readings = new ColonyLazyReadings(b, pair.Table, 7, (_, _) => { });
                var full = readings.Describe(model);
                var compact = (ICompactTextureSource)full;
                for (int channel = 0; channel < 3; ++channel)
                    for (int c = 0; c < 800; ++c)
                    {
                        var cell = batch * 800 + c;
                        var x = cell % 256;
                        var y = cell / 256;
                        if (mode == "compact")
                        {
                            if (!compact.TrySampleTexture(cell, x, y, out var got))
                                throw new Exception("benchmark missing");
                            sink += channel == 0 ? got.Color.R : channel == 1 ? got.GasDensityKgPerCubicMetre : (int)got.Failures;
                        }
                        else
                        {
                            var got = full[cell];
                            sink += channel == 0 ? got.WorldFluidColorAt(x, y).R : channel == 1 ? got.GasDensityKgPerCubicMetre : (int)got.Failures;
                        }
                    }
            }
            watch.Stop();
            Console.WriteLine($"CACHEBENCH {mode} cells48000 three-separated-channels elapsed_ms {watch.Elapsed.TotalMilliseconds:F3} allocated_bytes {GC.GetAllocatedBytesForCurrentThread() - start}");
        }
    }
}
