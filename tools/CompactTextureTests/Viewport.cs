using System.Diagnostics;
using MixedAtmospheres.Core;
using MixedAtmospheres.Oni;
static partial class Proof
{
    static void ViewportBenchmark(MixturePresentationModel model, List<(ColonyPresentation.Table Table, OniNativeColony.Reading Cell)> cases)
    {
        var pair = BufferOf(cases, 32768);
        var b = pair.Buffer;
        for (int i = 0; i < b.Count; ++i)
        {
            b.Readings[i].PublicCell = (uint)(i * 3);
        }
        foreach (var shape in new[] { (W: 80, H: 30, Name: "800"), (W: 160, H: 75, Name: "4k"), (W: 256, H: 188, Name: "16k"), (W: 256, H: 384, Name: "fullnative32768") })
        {
            var keys = Enumerable.Range(0, b.Count).Where(i => b.Readings[i].PublicCell % 256 < shape.W && b.Readings[i].PublicCell / 256 < shape.H).ToArray();
            foreach (var mode in new[] { "full", "compact", "full", "compact" })
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
                GC.Collect();
                var counts = ColonyCompactReadings.Counts;
                var start = GC.GetAllocatedBytesForCurrentThread();
                var watch = Stopwatch.StartNew();
                int fitted = 0;
                for (int physical = 0; physical < 3; ++physical)
                {
                    ++b.Lease;
                    b.CompactCache!.Activate(b.Lease);
                    var readings = new ColonyLazyReadings(b, pair.Table, 7, (_, _) => { });
                    var full = readings.Describe(model);
                    var compact = (ICompactTextureSource)full;
                    for (int unity = 0; unity < 3; ++unity)
                    {
                        var use = mode == "compact" && compact.PrepareTextureScalars(256, 0, 0, shape.W - 1, shape.H - 1, out _);
                        if (use)
                            ++fitted;
                        for (int channel = 0; channel < 3; ++channel)
                            foreach (var index in keys)
                            {
                                var cell = (int)b.Readings[index].PublicCell;
                                var x = cell % 256;
                                var y = cell / 256;
                                if (use)
                                {
                                    if (!compact.TrySampleTexture(cell, x, y, out var got))
                                        throw new Exception("benchmark missing compact");
                                    sink += channel == 0 ? got.Color.R : channel == 1 ? got.GasDensityKgPerCubicMetre : (int)got.Failures;
                                }
                                else
                                {
                                    var got = full[cell];
                                    sink += channel == 0 ? got.WorldFluidColorAt(x, y).R : channel == 1 ? got.GasDensityKgPerCubicMetre : (int)got.Failures;
                                }
                            }
                    }
                }
                watch.Stop();
                var after = ColonyCompactReadings.Counts;
                Console.WriteLine($"VIEWBENCH {shape.Name} {mode} visible {keys.Length} physical3 unity3 channels3 fit_batches {fitted}/9 elapsed_ms {watch.Elapsed.TotalMilliseconds:F3} allocated_bytes {GC.GetAllocatedBytesForCurrentThread() - start} hits {after.Hits - counts.Hits} misses {after.Misses - counts.Misses} raw {after.Raw - counts.Raw}");
            }
        }
    }
    static void BatchControls(MixturePresentationModel model, List<(ColonyPresentation.Table Table, OniNativeColony.Reading Cell)> cases)
    {
        var pair = BufferOf(cases, 32768);
        var b = pair.Buffer;
        for (int i = 0; i < b.Count; ++i)
            b.Readings[i].PublicCell = (uint)(i * 3);
        var lazy = new ColonyLazyReadings(b, pair.Table, 7, (_, _) => { });
        var source = (ICompactTextureSource)lazy.Describe(model);
        Check(source.PrepareTextureScalars(256, 0, 0, 79, 29, out var visible) && visible == 800, "realistic800batch fits unique slots");
        Check(!source.PrepareTextureScalars(256, 0, 0, 159, 74, out visible) && visible == 4000, "4k collision batch chooses whole full fallback");
        Check(!source.PrepareTextureScalars(256, 0, 0, 255, 383, out visible) && visible == 32768, "full native batch chooses whole full fallback");
        Check(source.PrepareTextureScalars(256, 0, 0, 79, 29, out visible) && visible == 800, "zoom in returns to compact");
        b.Sorted = false;
        ++b.Lease;
        b.CompactCache!.Activate(b.Lease);
        var unsorted = (ICompactTextureSource)new ColonyLazyReadings(b, pair.Table, 7, (_, _) => { }).Describe(model);
        Check(!unsorted.PrepareTextureScalars(256, 0, 0, 79, 29, out _), "unsorted source uses full fallback");
    }
}
