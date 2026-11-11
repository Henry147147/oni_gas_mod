using MixedAtmospheres.Core;
using MixedAtmospheres.Oni;
static partial class Proof
{
    // More visible readings than the former 4,093-slot cap: the compact path
    // is admitted and every sample, hit or collision miss, gives the rich
    // renderer's bytes. Through the pool's shared table the second and third
    // texture passes hit; through a small private table nearly all collide.
    static void AboveOldCap(MixturePresentationModel[] models, List<(ColonyPresentation.Table Table, OniNativeColony.Reading Cell)> cases)
    {
        var options = new MixtureWorldRendering.OpacityParameters(15, 200, .1);
        foreach (var shared in ColonyCompactReadings.Enabled ? new[] { true, false } : new[] { false })
        {
            var pair = BufferOf(cases, 12000);
            var b = pair.Buffer;
            for (int i = 0; i < b.Count; ++i) b.Readings[i].PublicCell = (uint)(i * 2);   // every other cell of a 200-wide grid
            ColonyReadingPool.Reset();
            if (shared)
            {
                var pooled = ColonyReadingPool.Acquire(b.Count, b.PartCount);
                b.CompactCache = pooled.CompactCache; b.Lease = pooled.Lease;
                Check(b.CompactCache != null && b.CompactCache.Slots.Length == ColonyReadingPool.SharedCompactSlots, "pooled shared table");
            }
            else { b.CompactCache = new ColonyCompactReadings.Cache(4093, b.Lease); }
            var readings = new ColonyLazyReadings(b, pair.Table, 7, (_, _) => { });
            foreach (var model in models)
            {
                var full = readings.Describe(model); var compact = (ICompactTextureSource)full;
                // Columns 10..189 of rows 5..114: 9,900 visible readings.
                Check(compact.PrepareTextureScalars(200, 10, 5, 189, 114, out var visible) && visible == 90 * 110, "above the old cap admits the compact path " + visible);
                var before = ColonyCompactReadings.Counts;
                for (int channel = 0; channel < 3; ++channel)
                    for (int y = 5; y <= 114; ++y)
                        for (int x = 10; x <= 189; x += 2)
                        {
                            var cell = x + y * 200;
                            Check(compact.TrySampleTexture(cell, x, y, out var scalar), "above-cap compact sample");
                            var rich = MixtureWorldRendering.Sample(full[cell], x, y, options);
                            var actual = MixtureWorldRendering.Sample(scalar, options);
                            Check(rich.R == actual.R && rich.G == actual.G && rich.B == actual.B && rich.Alpha == actual.Alpha &&
                                rich.Opacity == actual.Opacity && rich.Hazard == actual.Hazard, "above-cap renderer bytes");
                            Exact(rich.PhysicalPressurePa, actual.PhysicalPressurePa, "above-cap pressure");
                        }
                var after = ColonyCompactReadings.Counts;
                var hits = after.Hits - before.Hits; var misses = after.Misses - before.Misses;
                Check(hits + misses == 3L * visible, "every above-cap sample went through the cache");
                if (shared) Check(hits >= 2L * visible && misses <= visible, "shared table keeps the whole view: later passes hit");
                else Check(misses > 2L * visible, "small table: collisions are misses, values unchanged");
            }
        }
        ColonyReadingPool.Reset();
        Console.WriteLine("PASS above the former 4,093-slot cap: compact path admitted, shared-table hits and small-table collisions give rich renderer bytes");
    }
}
