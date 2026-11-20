using System;
using System.Collections.Generic;
using System.Linq;
using MixedAtmospheres.Oni;
using MixedAtmospheres.Oni.Tests.Review1;

// The pipe move rule (managed/MixedAtmospheres.Oni/ColonyConduitMoves.cs,
// docs/mixed-pipes.md#throughput) against ONI's own float decision.
// PipeMoves.Accepted and PipeMoves.Move are what ColonyConduits.Limit, Move
// and the staged Remove call for two owned cells. "Legacy" replays the
// decision of a513af5; Review1Moves is the rule of 9a2568c, verbatim. Each
// review counterexample is shown failing on the rule it was found on.
internal static class PipeMoveTests
{
    private static readonly PipeMaterial Oxygen = new(1, 1005, 0), Methane = new(2, 2191, 0), Co2 = new(3, 846, 0), Water = new(4, 4179, 0);
    private static PipeMaterial Material(int id) => id switch { 1 => Oxygen, 2 => Methane, 3 => Co2, 4 => Water, _ => throw new ArgumentException() };
    private static void Check(bool ok, string why) { if (!ok) throw new Exception(why); }
    private const float Gas = 1f, Liquid = 10f;

    // ONI's network move (ConduitFlow.UpdateConduit): the movable part of the
    // packet the source shows (all of it, or less when part arrived this
    // tick), capped by the free space the target shows, in binary32.
    private static float OniNetworkRequest(PipeInventory source, PipeInventory target, float capacity, float movable = 1)
    {
        var shown = PipeMoves.Shown(source, capacity); var room = capacity - PipeMoves.Shown(target, capacity);
        return Math.Min(movable >= 1 ? shown : shown * movable, Math.Max(0f, room));
    }
    private static (PipeInventory Source, PipeInventory Target, PipeInventory Parcel, float Accepted) Step(PipeInventory source, PipeInventory target, float request, float capacity)
    {
        var accepted = PipeMoves.Accepted(request, source, target, capacity);
        var move = PipeMoves.Move(source, target, accepted, capacity, Material);
        return (move.Source, move.Target, move.Parcel, accepted);
    }
    // The filter of 1.10.2 and this revision (ColonyConduits.TransferSelected).
    private static (PipeInventory Input, PipeInventory Output) Filter(PipeInventory input, PipeInventory output, float capacity, int species, bool excluding)
    {
        var moved = PipeInventory.Transfer(input, output, capacity, capacity, Material, species, excluding);
        return (moved.Source, moved.Target);
    }

    private static class Legacy
    {
        private static bool MovesWhole(PipeInventory source, PipeInventory target, float amount, float capacity)
        {
            var projected = (float)source.Mass;
            return projected > 0 && amount >= projected && (float)target.Total.Plus(source.Total).Value <= capacity;
        }
        internal static float Limit(PipeInventory source, PipeInventory target, float request, float capacity)
        {
            if (MovesWhole(source, target, request, capacity)) return request;
            var available = source.Total; var room = new PipeAmount(capacity).Minus(target.Total);
            if (available.Sign <= 0 || room.Sign <= 0) return 0;
            request = Math.Min(request, (float)Math.Min(available.Value, room.Value));
            while (request > 0 && (new PipeAmount(request).Minus(available).Sign > 0 || new PipeAmount(request).Minus(room).Sign > 0))
                request = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(request) - 1);
            return request;
        }
        internal static (PipeInventory Source, PipeInventory Target) Move(PipeInventory source, PipeInventory target, float amount, float capacity)
        {
            if (MovesWhole(source, target, amount, capacity)) return (PipeInventory.Empty, target.Joined(source));
            var transfer = PipeInventory.Transfer(source, target, amount, capacity, Material);
            if (transfer.Parcel.Mass != amount) throw new InvalidOperationException("Native pipe move exceeded owned inventory/capacity.");
            return (transfer.Source, transfer.Target);
        }
        internal static PipeInventory Returned(PipeInventory source, float amount) => source.Split(amount, Material).Parcel;
    }

    private static double Germs(PipeInventory inventory, int id) { foreach (var g in inventory.Germs) if (g.Id == id) return g.Mass; return 0; }
    private static void Conserved(PipeInventory before, params PipeInventory[] after)
    {
        foreach (var id in new[] { 1, 2, 3, 4 })
        {
            var left = before.Parts.ToArray().Where(p => p.Id == id).Select(p => p.Amount).Aggregate(PipeAmount.Zero, (a, b) => a.Plus(b));
            var right = after.SelectMany(s => s.Parts.ToArray()).Where(p => p.Id == id).Select(p => p.Amount).Aggregate(PipeAmount.Zero, (a, b) => a.Plus(b));
            Check(left.Minus(right).Sign == 0, "a move changed a constituent's mass");
        }
        Check(before.Energy.Minus(after.Aggregate(PipeAmount.Zero, (e, s) => e.Plus(s.Energy))).Sign == 0, "a move changed the energy");
        var ids = before.Germs.ToArray().Select(g => g.Id).Concat(after.SelectMany(s => s.Germs.ToArray()).Select(g => g.Id)).Distinct();
        foreach (var id in ids)
        {
            var left = before.Germs.ToArray().Where(g => g.Id == id).Select(g => g.Amount).Aggregate(PipeAmount.Zero, (a, b) => a.Plus(b));
            var right = after.SelectMany(s => s.Germs.ToArray()).Where(g => g.Id == id).Select(g => g.Amount).Aggregate(PipeAmount.Zero, (a, b) => a.Plus(b));
            Check(left.Minus(right).Sign == 0, "a move changed a germ count");
        }
    }
    private static PipeInventory Receipts(PipeMaterial material, float each, int count, double temperature = 300, int disease = 0, double germs = 0)
    {
        var sum = PipeInventory.Empty;
        for (var k = 0; k < count; ++k) sum = sum.Joined(PipeInventory.Single(material, each, temperature, disease, k == 0 ? germs : 0));
        return sum;
    }

    internal static void Run()
    {
        FirstReview(); SecondReview();
        Property(Gas, 20260120); Property(Liquid, 20260121);
        Lines(Gas, 77); Lines(Liquid, 78);
        Worst(Gas); Worst(Liquid);
        Liveness(Gas, 91); Liveness(Liquid, 92);
    }

    private static void FirstReview()
    {
        // 1: the outlet shows 0.75 kg beside 0.25000003 kg; ONI moves the whole packet.
        var outlet = PipeInventory.Single(Oxygen, 0.7500000298023224, 300); var next = PipeInventory.Single(Oxygen, 0.2500000335276127, 300);
        var request = OniNetworkRequest(outlet, next, Gas);
        var limited = Legacy.Limit(outlet, next, request, Gas); var old = Legacy.Move(outlet, next, limited, Gas);
        Check(request == 0.75f && limited == 0.74999994f && Math.Abs(old.Source.Mass - 8.94e-8) < 1e-9, "a513af5 no longer keeps 89.4 ug");
        var move = Step(outlet, next, request, Gas);
        Check(move.Accepted == request && move.Source.Count == 0 && PipeMoves.Shown(move.Target, Gas) == 1f && PipeMoves.WithinBound(move.Target, Gas), "first review 1");
        Conserved(outlet.Joined(next), move.Source, move.Target);
        // 2: no flow into a target that shows no room; requests capped at the shown packet.
        var full = PipeInventory.Single(Oxygen, 1, 300); var trace = PipeInventory.Single(Oxygen, Math.ScaleB(1, -26), 300);
        Check(Legacy.Limit(trace, full, (float)trace.Mass, Gas) > 0 && PipeMoves.Accepted((float)trace.Mass, trace, full, Gas) == 0, "first review 2: full target");
        var quarter = PipeInventory.Single(Oxygen, .25, 300);
        Check(Legacy.Limit(quarter, PipeInventory.Empty, 1, Gas) == 1 && PipeMoves.Accepted(1, quarter, PipeInventory.Empty, Gas) == .25f, "first review 2: oversized request");
        // 4: staged RemoveElement returns what moved, all 100 germs.
        var five = Receipts(Oxygen, .1f, 5, 300, 3, 100);
        var returned = Step(five, PipeInventory.Empty, .5f, Gas);
        Check(Math.Floor(Germs(Legacy.Returned(five, .5f), 3)) == 99 && Germs(returned.Parcel, 3) == 100 && returned.Source.Count == 0, "first review 4");
        // 5: exactly half a step above the capacity shows the capacity and is within the bound.
        var halfStep = PipeInventory.Single(Oxygen, 1 + Math.ScaleB(1, -24), 300);
        Check(PipeMoves.Shown(halfStep, Gas) == 1f && PipeMoves.WithinBound(halfStep, Gas), "first review 5");
        Console.WriteLine("PASS pipe moves, first review: 0.75 kg beside 0.25000003 kg moves whole (a513af5 kept 89.4 ug); no flow into a full cell; requests capped at the shown packet; the staged return keeps 100 germs (a513af5: 99)");
    }

    private static void SecondReview()
    {
        // 1: mixed-pipe-valves' partial window: an outlet built to 0.1 kg below
        // a 10 kg pipe (0.01 below 1 kg for gas) and a valve offering more.
        foreach (var (capacity, gap, material) in new[] { (Liquid, .1, Water), (Gas, .01, Oxygen) })
        {
            var mass = new PipeAmount(capacity).Minus(new PipeAmount(gap));
            var outlet = PipeInventory.Conserved(new[] { new PipePart(material.Id, mass) }, mass.Times(material.HeatCapacity * 300), Array.Empty<PipePart>());
            var source = PipeInventory.Single(material, capacity * .7, 300).Joined(PipeInventory.Single(Methane, capacity * .2, 300));
            var request = Math.Min(capacity, PipeMoves.Shown(source, capacity));
            bool Gate(PipeInventory output, PipeInventory input) => output.Total.Minus(new PipeAmount(capacity)).Sign <= 0 && Math.Abs(output.Mass - capacity) < 1e-7 && Math.Abs(source.Mass - input.Mass - gap) < 1e-7;
            var review = Review1Moves.Move(source, outlet, Review1Moves.Accepted(request, source, outlet, capacity, Material), capacity, Material);
            // ONI's rounded room is above the exact gap for liquid (0.10000038f), below it for gas (0.00999999f).
            Check(Gate(review.Target, review.Source) == (capacity == Gas), $"9a2568c's valve result changed: output {review.Target.Mass:R}");
            var move = Step(source, outlet, request, capacity);
            Check(Gate(move.Target, move.Source), $"the valve gate fails: output {move.Target.Mass:R}, taken {source.Mass - move.Source.Mass:R}");
        }
        // 3: an exact reservoir with 0.5 + 2^-60 kg of room and a packet of that
        // mass. 9a2568c took it whole and the store refused the low atom (an
        // owner fault); the inlet of 1.10.2, kept here, takes 0.5 kg.
        {
            var held = PipeInventory.Single(Water, 0.5 - Math.ScaleB(1, -53), 300).Joined(PipeInventory.Single(Water, Math.ScaleB(1, -53) - Math.ScaleB(1, -60), 300));
            var packet = PipeInventory.Single(Water, .5, 300).Joined(PipeInventory.Single(Water, Math.ScaleB(1, -60), 300));
            var room = new PipeAmount(1).Minus(held.Total);
            Check(room.Minus(packet.Total).Sign == 0, "fixture: exact room equals the packet");
            var tank = new OwnedFluidStore(new StoredFluidAddress(90, 0), 2, held);
            Check(tank.PrepareAdmission(packet, 1, Material).Outside.Count != 0, "9a2568c's whole intake no longer faults");
            var limit = (float)Math.Min(1f, room.Value); while (new PipeAmount(limit).Minus(room).Sign > 0) limit = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(limit) - 1);
            var taken = packet.Split(Math.Min(limit, packet.Mass), Material);
            Check(tank.PrepareAdmission(taken.Parcel, 1, Material).Outside.Count == 0 && PipeMoves.Shown(taken.Remaining, Liquid) > 0, "the kept intake faults or hides its remainder");
        }
        // 4: liveness. The repaired 1.000000063 kg oxygen packet behind a 2^-26 kg
        // CO2 trace in an oxygen filter's inlet, the CO2 outlet backed up; and
        // seven 0.136f portions plus 0.048f beside a 2^-25 kg residue.
        {
            var repaired = PipeInventory.Single(Oxygen, 1.000000063329935, 300);
            var trace = PipeInventory.Single(Co2, Math.ScaleB(1, -26), 300);
            var request = OniNetworkRequest(repaired, trace, Gas);
            Check(request == 1f && Review1Moves.Accepted(request, repaired, trace, Gas, Material) == 0, "9a2568c no longer waits");
            var portions = Receipts(Oxygen, .136f, 7).Joined(PipeInventory.Single(Oxygen, .048f, 300));
            var residue = PipeInventory.Single(Oxygen, Math.ScaleB(1, -25), 300);
            Check(Review1Moves.Accepted(OniNetworkRequest(portions, residue, Gas), portions, residue, Gas, Material) == 0, "9a2568c no longer waits on valve portions");
            Check(Step(portions, residue, OniNetworkRequest(portions, residue, Gas), Gas).Accepted > 0, "valve portions wait");
            // The line: packets keep arriving at its head; the filter passes oxygen to a clear outlet and its CO2 outlet never drains.
            var line = new[] { repaired, PipeInventory.Empty, trace }; var oxygenOut = PipeInventory.Empty; var co2Out = PipeInventory.Single(Co2, 1, 300);
            var arrived = PipeInventory.Empty; var ticks = 0;
            for (; ticks < 40; ++ticks)
            {
                for (var k = line.Length - 2; k >= 0; --k)
                {
                    var move = Step(line[k], line[k + 1], OniNetworkRequest(line[k], line[k + 1], Gas), Gas); line[k] = move.Source; line[k + 1] = move.Target;
                }
                (line[^1], co2Out) = Filter(line[^1], co2Out, Gas, Co2.Id, false);
                (line[^1], oxygenOut) = Filter(line[^1], oxygenOut, Gas, Co2.Id, true);
                arrived = arrived.Joined(oxygenOut); oxygenOut = PipeInventory.Empty;
                if (ticks < 10) line[0] = line[0].Joined(PipeInventory.Single(Oxygen, .3f, 300));
                if (ticks >= 10 && line.Take(2).All(c => c.Count == 0) && line[^1].SpeciesMass(Oxygen.Id) == 0) break;
            }
            Check(ticks < 40 && Math.Abs(arrived.Mass - (1.000000063329935 + 10 * (double).3f)) < 1e-12, $"the line behind the filter did not drain ({ticks} ticks, {arrived.Mass:R} kg)");
        }
        // 5: a filter with 0.5 kg oxygen and a 2^-160 kg selected CO2 part:
        // 9a2568c refused the part that shows 0 and left it alone in a cell
        // that shows empty; the filter of 1.10.2 moves it to the filtered outlet.
        {
            var input = PipeInventory.Single(Oxygen, .5, 300).Joined(PipeInventory.Single(Co2, Math.ScaleB(1, -160), 300));
            Check(Review1Moves.Accepted(float.MaxValue, input, PipeInventory.Empty, Gas, Material, Co2.Id) == 0, "9a2568c no longer strands the trace");
            var (rest, filtered) = Filter(input, PipeInventory.Empty, Gas, Co2.Id, false);
            var (left, passed) = Filter(rest, PipeInventory.Empty, Gas, Co2.Id, true);
            Check(left.Count == 0 && filtered.SpeciesMass(Co2.Id) > 0 && passed.SpeciesMass(Oxygen.Id) == .5, "the filter strands a trace");
        }
        // 6: a vent taking the repaired packet from a cell that shows 1f tells
        // ONI 1f (9a2568c reported the parcel's own float, 1.0000001f).
        {
            var repaired = PipeInventory.Single(Oxygen, 1.000000063329935, 300);
            Check((float)repaired.Mass == 1.0000001192092896f && PipeMoves.Shown(repaired, Gas) == 1f, "vent return");
        }
        // 7: a restored cell beyond the bound is refused (ColonyConduits.Restore uses WithinBound).
        Check(!PipeMoves.WithinBound(PipeInventory.Single(Oxygen, 2, 300), Gas) && PipeMoves.WithinBound(PipeInventory.Single(Oxygen, 1.000000063329935, 300), Gas), "restore bound");
        // 8: the shown packet is the binary32 of the binary64 value ONI receives.
        Check(PipeMoves.Shown(PipeInventory.Single(Oxygen, .5, 300).Joined(PipeInventory.Single(Oxygen, Math.ScaleB(1, -25), 300)).Joined(PipeInventory.Single(Oxygen, Math.ScaleB(1, -60), 300)), Gas) == .5f, "shown rounding");
        // 2 (kept): a machine inlet leaves 7.45 ug of five 0.1f receipts when it asks 0.5f.
        // Under half a step it narrows no room ONI shows, and the next whole packet takes it along.
        {
            var five = Receipts(Oxygen, .1f, 5); var inlet = five.Split(.5f, Material).Remaining;
            Check(Math.Abs(inlet.Mass - 7.45e-9) < 1e-10 && PipeMoves.Room(inlet, Gas) == 1f, "inlet residue narrows the room");
            var packet = Receipts(Oxygen, .2f, 5);
            var move = Step(packet, inlet, OniNetworkRequest(packet, inlet, Gas), Gas);
            Check(move.Source.Count == 0 && PipeMoves.WithinBound(move.Target, Gas), "a whole packet did not take an inlet residue along");
        }
        Console.WriteLine("PASS pipe moves, second review: the valve gate's partial window keeps exact capacity (9a2568c passed 10.00000038 kg); the kept reservoir intake admits 0.5 kg " +
            "where 9a2568c faulted; no wait at the bound (9a2568c stopped the line), the filtered line drains; the filter routes a 2^-160 kg part; vents and restore use the shown mass and the bound");
    }

    private static PipeInventory Random(Random random, float capacity, bool allowEmpty = true)
    {
        if (allowEmpty && random.Next(6) == 0) return PipeInventory.Empty;
        if (random.Next(10) == 0) return PipeInventory.Single(Oxygen, capacity + PipeMoves.ExcessBound(capacity) * random.NextDouble() * .999, 300, 3, random.Next(2) * 7);
        var inventory = PipeInventory.Empty; var receipts = 1 + random.Next(7); var scale = capacity * (float)random.NextDouble();
        for (var k = 0; k < receipts; ++k)
        {
            var mass = (float)(scale / receipts * (0.5 + random.NextDouble()));
            if (!(mass > 0)) continue;
            inventory = inventory.Joined(PipeInventory.Single(random.Next(3) == 0 ? Methane : Oxygen, mass, 280 + 40 * random.NextDouble(), 3, random.Next(4) == 0 ? random.Next(1000) : 0));
        }
        if (random.Next(5) == 0) inventory = inventory.Joined(PipeInventory.Single(Co2, Math.ScaleB(random.NextDouble(), -24 - random.Next(30)), 300));
        while (!PipeMoves.WithinBound(inventory, capacity)) inventory = inventory.Split(inventory.Mass * .75, Material).Parcel;
        return inventory;
    }
    // (a) a cell that shows empty is empty, (b) nothing enters a target that
    // shows no room, (c) never more than the request or the shown packet,
    // (d) exact mass, energy and germs, (e) the target stays within the bound
    // and a move that is not whole never takes it over its capacity.
    private static void Checked(PipeInventory source, PipeInventory target, float request, float capacity, ref int whole, ref int partial, ref int none, ref int over)
    {
        var shown = PipeMoves.Shown(source, capacity);
        var move = Step(source, target, request, capacity);
        Check(move.Accepted <= request && move.Accepted <= shown, "(c) more than requested or shown");
        if (PipeMoves.Room(target, capacity) == 0) Check(move.Accepted == 0, "(b) flow into a target that shows no room");
        Conserved(source.Joined(target), move.Source, move.Target);
        Check(move.Parcel.Count == 0 || move.Target.Total.Minus(target.Total).Minus(move.Parcel.Total).Sign == 0, "parcel differs from what arrived");
        Check(PipeMoves.Shown(move.Source, capacity) > 0 || move.Source.Count == 0, "(a) a cell that shows empty holds a residue");
        Check(PipeMoves.WithinBound(move.Target, capacity), "(e) beyond the bound");
        if (move.Accepted > 0 && move.Source.Count == 0 && move.Parcel.Total.Minus(source.Total).Sign == 0) { ++whole; if (PipeMoves.Excess(move.Target, capacity).Sign > 0) ++over; }
        else if (move.Accepted > 0) { ++partial; Check(PipeMoves.Excess(move.Target, capacity).Sign <= 0 || PipeMoves.Excess(target, capacity).Sign > 0, "(e) a partial move took a cell over its capacity"); }
        else ++none;
    }
    private static void Property(float capacity, int seed)
    {
        var random = new Random(seed); int whole = 0, partial = 0, none = 0, over = 0;
        for (var trial = 0; trial < 200000; ++trial)
        {
            var source = Random(random, capacity, false); var target = Random(random, capacity);
            Checked(source, target, OniNetworkRequest(source, target, capacity), capacity, ref whole, ref partial, ref none, ref over);
            Checked(source, target, OniNetworkRequest(source, target, capacity, (float)random.NextDouble()), capacity, ref whole, ref partial, ref none, ref over);
            var staged = random.Next(3) switch { 0 => (float)source.Mass, 1 => capacity, _ => (float)(capacity * random.NextDouble()) };
            Checked(source, target, staged, capacity, ref whole, ref partial, ref none, ref over);
        }
        Console.WriteLine($"PASS pipe moves, {capacity} kg pipes: 600000 moves from network and staged requests ({whole} whole, {over} of them over capacity; {partial} partial; {none} without flow): " +
            "a cell that shows empty is empty, no flow without shown room, never more than shown, exact accounts, the bound holds, partial moves keep exact capacity");
    }
    // Successive moves along lines fed by two outlets that, like pumps, fill
    // only an empty outlet cell; random cell order (packets merge into cells
    // that have not moved yet); part of a packet sometimes still unmovable;
    // an inlet at the end taking random float amounts.
    private static void Lines(float capacity, int seed)
    {
        var random = new Random(seed); double worst = 0; int moves = 0, over = 0, wholeIntoOver = 0;
        for (var line = 0; line < 300; ++line)
        {
            var cells = Enumerable.Range(0, 10).Select(_ => PipeInventory.Empty).ToArray();
            var before = PipeInventory.Empty; var sunk = PipeInventory.Empty;
            for (var tick = 0; tick < 200; ++tick)
            {
                foreach (var outlet in new[] { 0, 3 })
                    if (cells[outlet].Count == 0)
                    {
                        var packet = Receipts(random.Next(2) == 0 ? Oxygen : Methane, (float)(capacity * (.06 + .1 * random.NextDouble())), 1 + random.Next(5), 300, 3, random.Next(2) * 50);
                        cells[outlet] = packet; before = before.Joined(packet);
                    }
                for (var pass = 0; pass < 3; ++pass)
                    foreach (var k in Enumerable.Range(0, cells.Length - 1).OrderBy(_ => random.Next()))
                    {
                        var wasOver = PipeMoves.Excess(cells[k], capacity).Sign > 0;
                        var move = Step(cells[k], cells[k + 1], OniNetworkRequest(cells[k], cells[k + 1], capacity, random.Next(4) == 0 ? (float)random.NextDouble() : 1), capacity);
                        Check(PipeMoves.Shown(move.Source, capacity) > 0 || move.Source.Count == 0, "(a) residue in a line");
                        if (wasOver && move.Source.Count == 0) ++wholeIntoOver;
                        cells[k] = move.Source; cells[k + 1] = move.Target; ++moves;
                        var excess = PipeMoves.Excess(cells[k + 1], capacity).Value; worst = Math.Max(worst, excess); if (excess > 0) ++over;
                        Check(PipeMoves.WithinBound(cells[k + 1], capacity), "(e) beyond the bound in a line");
                    }
                // The inlet asks a float: the whole shown packet or less.
                var take = random.Next(2) == 0 ? PipeMoves.Shown(cells[^1], capacity) : (float)(capacity * random.NextDouble());
                var taken = cells[^1].Split(Math.Min(take, cells[^1].Mass), Material); sunk = sunk.Joined(taken.Parcel); cells[^1] = taken.Remaining;
            }
            Conserved(before, cells.Append(sunk).ToArray());
        }
        Console.WriteLine($"PASS pipe moves, {capacity} kg lines: {moves} successive moves, {over} left a cell over its capacity (worst {worst:R} kg, bound {PipeMoves.ExcessBound(capacity):R}), " +
            $"{wholeIntoOver} over-capacity packets moved on whole; no cell shows empty while holding mass, accounts exact");
    }
    // The largest excess one whole move between cells within capacity makes.
    private static void Worst(float capacity)
    {
        double worst = 0; var sweeps = 0;
        double Under(float x) { var up = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(x) + 1); return x + (up - (double)x) * (.5 - 1e-6); }
        for (var a = capacity * 1e-6f; a < capacity; a = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(a) + 997))
        {
            var b = capacity - a;
            var target = PipeInventory.Single(Oxygen, Under(a), 300); var source = PipeInventory.Single(Methane, Under(b), 300);
            if (PipeMoves.Shown(target, capacity) != a || PipeMoves.Shown(source, capacity) != b) continue;
            var move = Step(source, target, OniNetworkRequest(source, target, capacity), capacity); ++sweeps;
            Check(move.Accepted == b && move.Source.Count == 0, "a full-room packet did not move whole");
            worst = Math.Max(worst, PipeMoves.Excess(move.Target, capacity).Value);
        }
        Check(worst <= PipeMoves.ExcessBound(capacity) && worst > PipeMoves.StepBelow(capacity), $"worst single-move excess {worst:R}");
        // An over-capacity packet that would pass the bound on a trace moves partially, leaving a shown remainder.
        var step = PipeMoves.StepBelow(capacity);
        var heavy = PipeInventory.Single(Methane, capacity + PipeMoves.ExcessBound(capacity) * .99, 300); var trace = PipeInventory.Single(Oxygen, step * .45, 300);
        var partial = Step(heavy, trace, OniNetworkRequest(heavy, trace, capacity), capacity);
        Check(partial.Accepted > 0 && PipeMoves.Shown(partial.Source, capacity) > 0 && PipeMoves.Excess(partial.Target, capacity).Sign <= 0, "an over-capacity packet beside a trace");
        Console.WriteLine($"PASS pipe moves, {capacity} kg worst case: {sweeps} adversarial whole moves reach {worst:R} kg ({worst / step:F3} steps) within {PipeMoves.ExcessBound(capacity):R}; " +
            "an over-capacity packet beside a trace moves all but a shown remainder");
    }
    // Lines that end in filters with a blocked outlet: the rest drains in a bounded number of ticks.
    private static void Liveness(float capacity, int seed)
    {
        var random = new Random(seed); var worstTicks = 0;
        for (var run = 0; run < 400; ++run)
        {
            var cells = Enumerable.Range(0, 6).Select(_ => Random(random, capacity)).ToArray();
            cells[^1] = cells[^1].Joined(PipeInventory.Single(Co2, Math.ScaleB(random.NextDouble(), -20 - random.Next(10)), 300));
            var blocked = PipeInventory.Single(Co2, capacity, 300); var drained = PipeInventory.Empty; var ticks = 0;
            for (; ticks < 200; ++ticks)
            {
                for (var k = cells.Length - 2; k >= 0; --k)
                {
                    var move = Step(cells[k], cells[k + 1], OniNetworkRequest(cells[k], cells[k + 1], capacity), capacity); cells[k] = move.Source; cells[k + 1] = move.Target;
                }
                (cells[^1], blocked) = Filter(cells[^1], blocked, capacity, Co2.Id, false);
                PipeInventory passed; (cells[^1], passed) = Filter(cells[^1], PipeInventory.Empty, capacity, Co2.Id, true); drained = drained.Joined(passed);
                if (cells.All(c => c.Parts.ToArray().All(p => p.Id == Co2.Id))) break;
            }
            Check(ticks < 200, "a filtered line never drained: " + string.Join(" | ", cells.Select(c => string.Join(",", c.Parts.ToArray().Select(p => $"{p.Id}:{p.Amount.Value:R}")))));
            worstTicks = Math.Max(worstTicks, ticks);
        }
        Console.WriteLine($"PASS pipe moves, {capacity} kg liveness: 400 lines ending in a filter whose CO2 outlet never drains pass everything else within {worstTicks + 1} ticks");
    }
}
