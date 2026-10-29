using System.Reflection;
using MixedAtmospheres.Oni;

static class ExtendedProof
{
    private static int checks;
    private static void Check(bool value, string why)
    {
        ++checks;
        if (!value) throw new Exception(why);
    }
    private static void Near(double a, double b, string why) => Check(Math.Abs(a - b) < 1e-7, why);
    private static void Balance(ColonyPacingRetained value)
    {
        var totals = value.Snapshot;
        Near(totals.Offered + totals.UnexpectedAdded, totals.Admitted + value.Owed + totals.Dropped, "Time accounting must close");
        Check(value.Owed >= 0 && value.Owed <= value.OwedLimit, "Debt exceeds configured bound");
        Check(totals.UnexpectedAdded == 0 && totals.Errors == 0, "Unexplained admitted time");
    }
    private static float Step(ColonyPacingRetained value, float raw, bool busy = false, float allowance = .2f,
        bool eligible = true, bool veto = false, bool zero = false)
    {
        var beforeOwed = value.Owed;
        var dt = raw;
        var token = value.Prepare(ref dt, eligible);
        if (eligible && raw > 0)
        {
            Check(dt <= ColonyPacingRetained.MaximumArgument, "Changed original argument cap");
            Check(dt <= (double)raw + Math.Min(beforeOwed, ColonyPacingRetained.MaximumExtra), "Repayment exceeds 1/60 s");
        }
        var before = dt;
        var run = !busy || dt < allowance;
        if (!run && allowance > 0) { dt = allowance; run = true; }
        value.Pacing(before, dt, run, busy);
        if (zero) dt = 0;
        value.Admission(dt, run && !veto);
        value.Commit(token, dt, run && !veto);
        value.Cancel(token);
        if (busy && run) Check(dt <= allowance, "Repayment crossed busy allowance");
        Balance(value);
        return dt;
    }
    private static void Main()
    {
        // Run the original .4-second suite unchanged, in the same process.
        typeof(Program).GetMethod("Main", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, null);
        Near(new ColonyPacingRetained().OwedLimit, .4, "Default bound changed");
        foreach (var invalid in new[] { 0.0, -0.0, -1, double.NaN, double.PositiveInfinity, double.NegativeInfinity, 2.0000000001 })
        {
            var refused = false;
            try { _ = new ColonyPacingRetained(invalid); }
            catch (ArgumentOutOfRangeException error) { refused = error.ParamName == "maximumOwed"; }
            Check(refused, "Invalid bound admitted");
        }
        foreach (var text in new string?[] { null, "", "NaN", "Infinity", "-1", "0", "2.1", "2,0" })
            Near(ColonyPacingRetained.ReadMaximumOwed(text), .4, "Invalid environment value changed default");
        foreach (var text in new[] { "2", "2.0", " 2.0 ", "2e0" })
            Near(ColonyPacingRetained.ReadMaximumOwed(text), 2, "Valid environment bound missed");

        var candidate = new ColonyPacingRetained(2);
        Step(candidate, .8f);
        Near(candidate.Owed, (double).8f - .2f, "Hitch time not retained");
        Near(candidate.Snapshot.Dropped, 0, "Hitch lost time below bound");
        var old = new ColonyPacingRetained();
        Step(old, .8f);
        Near(old.Owed, .4, "Original overload boundary changed");
        Check(old.Snapshot.Dropped > 0, "Original smaller bound no longer drops overflow");
        for (var k = 0; k < 100; ++k) Step(candidate, 1, true, 0);
        Near(candidate.Owed, 2, "Configured bound not reached");
        Check(candidate.Snapshot.Dropped > 98, "Overload was not booked");
        for (var k = 0; k < 200; ++k) Step(candidate, .01f);
        Near(candidate.Owed, 0, "Idle repayment did not drain debt");
        candidate.Reset(); Balance(candidate);

        // Intentional guard vetoes and zero-time calls do not borrow or replay.
        Step(candidate, .8f);
        var debt = candidate.Owed;
        Step(candidate, .03f, eligible: true, veto: true);
        Near(candidate.Owed, debt, "Guard veto consumed debt");
        Step(candidate, .03f, zero: true);
        Near(candidate.Owed, debt, "Intentional zero consumed debt");
        Step(candidate, 0, eligible: false);
        Near(candidate.Owed, debt, "Nested zero altered debt");
        candidate.Reset(); Balance(candidate);

        // Reset on pause/speed/session/save must invalidate outstanding tokens.
        foreach (var reason in new[] { "pause", "speed", "session", "save", "guard" })
        {
            Step(candidate, .8f);
            var arg = .03f; var token = candidate.Prepare(ref arg, true);
            candidate.Reset(); candidate.Commit(token, arg, true); candidate.Cancel(token);
            Balance(candidate); Near(candidate.Owed, 0, reason + " retained old debt");
            Check(candidate.Depth == 0, reason + " retained old scope");
        }
        Step(candidate, .8f);
        var outerArg = .03f; var outer = candidate.Prepare(ref outerArg, true);
        var innerArg = .03f; var inner = candidate.Prepare(ref innerArg, true);
        Near(innerArg, .03f, "Nested call borrowed the outer loan");
        candidate.Pacing(innerArg, innerArg, true, false); candidate.Admission(innerArg, true); candidate.Commit(inner, innerArg, true);
        candidate.Pacing(outerArg, outerArg, true, false); candidate.Admission(outerArg, true); candidate.Commit(outer, outerArg, true);
        Balance(candidate); Check(candidate.Depth == 0, "Nested scopes did not close");

        // Existing exact busy fence, all sub-tick boundaries, with larger debt.
        for (var sub = 0; sub < 12; ++sub)
        foreach (var partial in new[] { 0f, .005f, .016f })
        {
            candidate.Reset(); Step(candidate, 2.1f);
            var allowance = (12 - sub) / 60f - partial - 1e-4f;
            var dt = Step(candidate, .18f, true, allowance);
            Check(allowance <= 0 || partial + dt < (12 - sub) / 60f, "Crossed owner posting boundary");
        }
        candidate = new ColonyPacingRetained(2);
        for (var k = 0; k < 20000; ++k)
            Step(candidate, k % 97 == 0 ? .8f : .03f, k % 11 == 0, k % 22 == 0 ? 0 : .007f,
                eligible: k % 71 != 0, veto: k % 127 == 0, zero: k % 223 == 0);
        candidate.Reset(); Balance(candidate);

        // The larger configured state still allocates nothing after warmup.
        for (var k = 0; k < 100; ++k) Step(candidate, .03f);
        var bytes = GC.GetAllocatedBytesForCurrentThread();
        for (var k = 0; k < 100000; ++k)
        {
            var dt = .03f; var token = candidate.Prepare(ref dt, true);
            candidate.Pacing(dt, dt, true, false); candidate.Admission(dt, true); candidate.Commit(token, dt, true);
        }
        Check(GC.GetAllocatedBytesForCurrentThread() == bytes, "Larger bound added warmed allocations");
        Balance(candidate);
        Console.WriteLine($"Private 2-second retained pacing: {checks} additional checks passed; original .4 suite unchanged.");
    }
}
