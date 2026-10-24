using System;
using System.Collections.Generic;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    // World germ evolution (docs/colony-owner.md#germs).
    internal static partial class ColonyScenarios
    {
        private static byte DiseaseIndex(Klei.AI.Disease disease) => (byte)Db.Get().Diseases.GetIndex(disease.id);
        // Every disease identity in the selected cells of the latest publication.
        private static Dictionary<ulong, double> ArenaGerms(IEnumerable<(int x, int y)> cells, out int holding)
        {
            var selected = cells.Select(c => (uint)Cell(c.x, c.y)).Distinct().OrderBy(c => c).ToArray();
            WorldPassiveSnapshot? snapshot = null;
            for (var attempt = 0; attempt < 8 && snapshot == null; ++attempt)
            {
                var result = ColonyWorldPassives.Read(OniNativeColony.ReadPassives, 0, 0, selected, out snapshot);
                if (result != ColonyWorldPassives.Busy) OniNativeColony.Check(result);
            }
            if (snapshot == null) throw new InvalidOperationException("The passive publication kept advancing.");
            var totals = new Dictionary<ulong, double>();
            holding = snapshot.Rows.Where(r => r.Identity <= 256).Select(r => r.PublicCell).Distinct().Count();
            foreach (var row in snapshot.Rows)
                if (row.Identity <= 256) totals[row.Identity] = (totals.TryGetValue(row.Identity, out var t) ? t : 0) + row.Quantity;
            return totals;
        }
        // Explicit native comparison (run with MA_COLONY_GERM_ORACLE=1: the
        // colony keeps the legacy mode, so the game's native code evolves the
        // germs of sealed single cells on the shadow, and the owner predicts
        // every such step from the same inputs).
        private static readonly (int x, int y)[] oracleCells = { (1, 1), (3, 1), (5, 1), (1, 3), (3, 3), (5, 3) };
        private static readonly List<(int x, int y, SimHashes element, float mass, float temperature, Klei.AI.Disease disease, int germs)> oraclePockets =
            new List<(int, int, SimHashes, float, float, Klei.AI.Disease, int)>();
        // Six sealed cultures from the game's own rules: one that grows inside
        // its density window and growth temperatures, one above its density
        // window, one at a killing temperature, one below its minimum count
        // (absolute death to extinction), and two in other media.
        private static void PlanOraclePockets()
        {
            oraclePockets.Clear();
            var diseases = new[] { Db.Get().Diseases.SlimeGerms, Db.Get().Diseases.FoodGerms, Db.Get().Diseases.PollenGerms, Db.Get().Diseases.ZombieSpores };
            var media = new[] { (SimHashes.ContaminatedOxygen, 1.2f), (SimHashes.DirtyWater, 900f), (SimHashes.Water, 900f), (SimHashes.Oxygen, 1.2f), (SimHashes.CarbonDioxide, 1.5f) };
            Klei.AI.DiseaseGrowthRules.ElemGrowthInfo Rule(Klei.AI.Disease d, SimHashes e) => d.elemGrowthInfo[ElementLoader.FindElementByHash(e).idx];
            float Growing(Klei.AI.Disease d) => (d.temperatureRange.minGrowth + d.temperatureRange.maxGrowth) / 2;
            int Within(Klei.AI.DiseaseGrowthRules.ElemGrowthInfo g, float mass) => (int)Math.Max(100, Math.Min(Math.Max(g.minCountPerKG * mass * 4, 1000), Math.Min(g.maxCountPerKG * mass / 4, 1e6)));
            foreach (var d in diseases) foreach (var (e, m) in media)
            {
                var g = Rule(d, e);
                if (oraclePockets.Count == 0 && g.populationHalfLife < 0 && !float.IsInfinity(g.populationHalfLife))
                    oraclePockets.Add((1, 1, e, m, Growing(d), d, Within(g, m)));
            }
            var slime = Db.Get().Diseases.SlimeGerms; var food = Db.Get().Diseases.FoodGerms;
            var over = Rule(slime, SimHashes.ContaminatedOxygen);
            oraclePockets.Add((3, 1, SimHashes.ContaminatedOxygen, 1.2f, Growing(slime), slime,
                (int)Math.Min(2e9, Math.Max(2e5, over.maxCountPerKG * 1.2f * 3))));
            oraclePockets.Add((5, 1, SimHashes.Oxygen, 1.2f, slime.temperatureRange.maxViable + 15, slime, 100000));
            oraclePockets.Add((1, 3, SimHashes.Oxygen, 1.2f, Growing(slime), slime, 12));
            oraclePockets.Add((3, 3, SimHashes.Water, 900f, Growing(food), food, 20000));
            oraclePockets.Add((5, 3, SimHashes.DirtyWater, 900f, 290f, food, 50000));
            while (oraclePockets.Count > oracleCells.Length) oraclePockets.RemoveAt(oraclePockets.Count - 1);
            for (var i = 0; i < oraclePockets.Count; ++i)
            {
                var p = oraclePockets[i]; oraclePockets[i] = (oracleCells[i].x, oracleCells[i].y, p.element, p.mass, p.temperature, p.disease, p.germs);
            }
        }
        private static void GermOracle(int frame)
        {
            if (Once(0, "sealed single-cell pockets"))
            {
                PlanOraclePockets();
                Arena((x, y) => oraclePockets.FirstOrDefault(p => p.x == x && p.y == y) is var p && p.mass > 0 ?
                    (p.element, p.mass, p.temperature) : (SimHashes.Unobtanium, 20000f, 300f));
            }
            if (Once(8, "seed one culture per pocket"))
            {
                Record("legacy mode under the oracle control", ColonyWorldGerms.TryRead(out var s, out _) && s.Mode == 0, $"mode {s.Mode}");
                foreach (var p in oraclePockets) SimMessages.ModifyDiseaseOnCell(Cell(p.x, p.y), DiseaseIndex(p.disease), p.germs);
            }
            if (Once(160, "compare"))
            {
                ColonyWorldGerms.TryRead(out var s, out _);
                var pockets = string.Join("; ", oraclePockets.Select(p => $"{p.disease.Id} in {p.element} {p.temperature:F0} K: {p.germs} -> {Grid.DiseaseCount[Cell(p.x, p.y)]}"));
                var detail = $"{s.OracleComparisons} comparisons, {s.OracleMismatches} mismatches, {s.OracleExtinctions} extinctions, born {s.OracleBorn:G6}, died {s.OracleDied:G6}; {s.OracleLast}";
                Record("owner predicts every native world germ step", s.OracleComparisons >= 400 && s.OracleMismatches == 0, detail);
                Record("growth, decay and extinction exercised", s.OracleBorn > 0 && s.OracleDied > 0 && s.OracleExtinctions > 0, detail + "; " + pockets);
                Finish();
            }
        }

        // Owner mode: two identities share every cell of a polluted-oxygen
        // room, each inside its density window, and each total follows its own
        // rule frame by frame; slimelung seeded in one cell of an oxygen room
        // beyond a wall spreads and follows its rule; the germ accounts close.
        private static int InWindow(Klei.AI.Disease d, SimHashes e, double mass)
        {
            var g = d.elemGrowthInfo[ElementLoader.FindElementByHash(e).idx];
            double low = g.minCountPerKG * mass, high = float.IsInfinity(g.maxCountPerKG) ? double.PositiveInfinity : g.maxCountPerKG * mass;
            var count = low > 0 && !double.IsInfinity(high) ? Math.Sqrt(low * high) : low > 0 ? 10 * low : high / 10;
            return (int)Math.Max(Math.Min(Math.Max(count, 10 * low), Math.Min(high / 10, 1e6)), Math.Min(1000, high / 2));
        }
        // The game's per-frame factor inside the density window at a temperature.
        private static double FrameFactor(Klei.AI.Disease d, SimHashes e, float temperature)
        {
            var g = d.elemGrowthInfo[ElementLoader.FindElementByHash(e).idx];
            var t = Klei.AI.Disease.HalfLifeToGrowthRate(Klei.AI.Disease.CalculateRangeHalfLife(temperature, ref d.temperatureRange, ref d.temperatureHalfLives), .2f);
            return 1 + (t - 1) + (Klei.AI.Disease.HalfLifeToGrowthRate(g.populationHalfLife, .2f) - 1);
        }
        private static void WorldGerms(int frame)
        {
            var left = Enumerable.Range(0, 4).SelectMany(x => Enumerable.Range(0, Height).Select(y => (x, y))).ToArray();
            var right = Enumerable.Range(5, 3).SelectMany(x => Enumerable.Range(0, Height).Select(y => (x, y))).ToArray();
            var slime = Db.Get().Diseases.SlimeGerms; var food = Db.Get().Diseases.FoodGerms;
            ulong Id(Klei.AI.Disease d) => DiseaseIndex(d) + 1UL;
            if (Once(0, "polluted and clean rooms"))
                Arena((x, y) => x < 4 ? (SimHashes.ContaminatedOxygen, 1f, 300f) : x == 4 ? (SimHashes.Unobtanium, 20000f, 300f) : (SimHashes.Oxygen, 1f, 300f));
            if (Once(8, "seed"))
            {
                ColonyWorldGerms.TryRead(out var s, out _);
                Record("owner evolves world germs", s.Mode == 1, $"mode {s.Mode}, supported {ColonyWorldGerms.Supported} {ColonyWorldGerms.ConfigureFailure}");
                recorded["routed"] = ColonyWorldGerms.RoutedEdits;
                foreach (var (x, y) in left)
                {
                    SimMessages.ModifyDiseaseOnCell(Cell(x, y), DiseaseIndex(slime), InWindow(slime, SimHashes.ContaminatedOxygen, 1));
                    SimMessages.ModifyDiseaseOnCell(Cell(x, y), DiseaseIndex(food), InWindow(food, SimHashes.ContaminatedOxygen, 1));
                }
                SimMessages.ModifyDiseaseOnCell(Cell(6, 2), DiseaseIndex(slime), 100000);
            }
            if (Once(11, "seeded"))
            {
                var l = ArenaGerms(left, out _); var r = ArenaGerms(right, out _);
                recorded["l-slime"] = l.TryGetValue(Id(slime), out var a) ? a : 0; recorded["l-food"] = l.TryGetValue(Id(food), out var b) ? b : 0;
                recorded["r-slime"] = r.TryGetValue(Id(slime), out var c) ? c : 0;
                ColonyWorldGerms.TryRead(out var s, out _); recorded["frames"] = s.Frames;
                Record("the game's germ edits reached the owner", ColonyWorldGerms.RoutedEdits - recorded["routed"] == 2 * left.Length + 1 && recorded["l-slime"] > 0 &&
                    recorded["l-food"] > 0 && recorded["r-slime"] > 1e4, $"routed {ColonyWorldGerms.RoutedEdits - recorded["routed"]}, left slimelung {recorded["l-slime"]:G6}, " +
                    $"food poisoning {recorded["l-food"]:G6}, right slimelung {recorded["r-slime"]:G6}");
            }
            if (Once(110, "check"))
            {
                var l = ArenaGerms(left, out var leftCells); var r = ArenaGerms(right, out var rightCells);
                ColonyWorldGerms.TryRead(out var s, out var accounts);
                var frames = s.Frames - recorded["frames"];
                double Held(Dictionary<ulong, double> t, Klei.AI.Disease d) => t.TryGetValue(Id(d), out var v) ? v : 0;
                double Predicted(Klei.AI.Disease d, double before) => before * Math.Pow(FrameFactor(d, SimHashes.ContaminatedOxygen, 300), frames);
                bool Close(double before, double predicted, double actual) => Math.Abs(actual - predicted) <= .1 * Math.Abs(predicted - before) + 1e-5 * before;
                var ps = Predicted(slime, recorded["l-slime"]); var pf = Predicted(food, recorded["l-food"]);
                Record("each identity in the shared cells follows its own rule", Close(recorded["l-slime"], ps, Held(l, slime)) && Close(recorded["l-food"], pf, Held(l, food)),
                    $"{frames} frames; slimelung {recorded["l-slime"]:G8} -> {Held(l, slime):G8} (predicted {ps:G8}), food poisoning {recorded["l-food"]:G8} -> {Held(l, food):G8} (predicted {pf:G8})");
                var rule = FrameFactor(slime, SimHashes.Oxygen, 300) - 1;
                Record("slimelung seeded in clean oxygen spreads and follows its rule", rightCells > 3 && (rule < 0 ? Held(r, slime) < recorded["r-slime"] : Held(r, slime) > recorded["r-slime"]),
                    $"{recorded["r-slime"]:G6} -> {Held(r, slime):G6} in {rightCells} cells (rule {rule:G3} per frame inside its window)");
                Record("germ accounts close", s.AuditError < 1e-9 && s.Frames > 100,
                    $"audit {s.AuditError:G3}, {s.Frames} frames, {s.Cultures} cultures in {s.GermCells} cells ({s.ExpectedCultures} expected-population), born {s.Born:G6}, died {s.Died:G6}, culled {s.Culled:G6}, moved {s.Moved:G6}");
                Finish();
            }
        }
    }
}
