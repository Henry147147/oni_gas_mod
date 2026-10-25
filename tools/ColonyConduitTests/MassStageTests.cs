using System;
using MixedAtmospheres.Oni;

// Projected pipe masses split into ONI's movable and deferred stages whose
// sum is the mass exactly, the movable part never above the native budget.
static class MassStageTests
{
    static void Check(bool okay, string reason) { if (!okay) throw new Exception(reason); }
    static float Next(Random random)
    {
        // Masses and budgets over the range pipes hold, with random low bits.
        var exponent = random.Next(-40, 8);
        return (float)((1 + random.NextDouble()) * Math.Pow(2, exponent));
    }
    internal static void Run()
    {
        var random = new Random(25102026); var hard = 0;
        for (var n = 0; n < 2000000; ++n)
        {
            var mass = Next(random);
            var movable = random.Next(8) switch { 0 => 0f, 1 => mass, 2 => -Next(random), 3 => mass * 2, _ => Math.Min(mass, Next(random)) };
            Check(PipeMassStages.Split(mass, movable, out var budget, out var deferred), $"no exact split of {mass:R} with budget {movable:R}");
            Check((double)budget + deferred == mass && budget + deferred == mass && budget >= 0 && deferred >= 0 && budget <= Math.Max(0, movable),
                $"split {budget:R} + {deferred:R} of {mass:R} (budget {movable:R})");
            // The old two-step search failed where the rounded complement missed.
            var old = Math.Min(mass, Math.Max(0, movable)); var rest = (float)(mass - old);
            if ((double)old + rest != mass) ++hard;
        }
        Check(hard > 0, "the control never met a split whose rounded complement misses");
        Check(!PipeMassStages.Split(0, 1, out _, out _) && !PipeMassStages.Split(float.PositiveInfinity, 1, out _, out _), "an empty or infinite mass split");
        Console.WriteLine($"PASS pipe mass stages: 2,000,000 random masses split exactly within the native budget ({hard} whose plain complement was inexact)");
    }
}
