using MixedAtmospheres.Oni;

static class Program
{
    static int checks;
    static void Check(bool value) { ++checks; if(!value)throw new Exception("Pacing loss check "+checks); }
    static void Near(double actual,double expected) => Check(Math.Abs(actual-expected)<1e-7);
    static void Main()
    {
        var counter=new ColonyPacingLoss();
        var normal=counter.Begin(.03f,true,151,7);counter.Pacing(.03f,.03f,true,false);counter.Complete(normal,.03f,true);counter.Cancel(normal);
        var clip=counter.Begin(.05f,true,152,7);counter.Pacing(.05f,.01f,true,true);counter.Complete(clip,.01f,true);
        var hold=counter.Begin(.04f,true,153,7);counter.Pacing(.04f,0,false,true);counter.Complete(hold,.04f,false);
        var earlierZero=counter.Begin(.06f,true,154,7);counter.Pacing(0,0,true,true);counter.Complete(earlierZero,0,true);
        var earlierVeto=counter.Begin(.07f,true,155,7);counter.Complete(earlierVeto,.07f,false);
        var laterVeto=counter.Begin(.08f,true,156,7);counter.Pacing(.08f,.02f,true,true);counter.Complete(laterVeto,.02f,false);
        var value=counter.Drain();Check(value.Calls==6&&value.OriginalRuns==3&&value.OriginalSkipped==3&&value.PacingCalls==5&&value.BusyCalls==4);
        Check(value.ClippedCalls==2&&value.FullyHeldCalls==1&&value.EarlierZeroCalls==1&&value.OtherVetoCalls==2);
        Near(value.OfferedSeconds,.33);Near(value.AdmittedSeconds,.04);Near(value.LostSeconds,.29);Near(value.PacingLostSeconds,.14);Near(value.OtherLostSeconds,.15);
        Check(value.FirstFrame==151&&value.LastFrame==156&&counter.Depth==0&&value.AttributionErrors==0);
        Check(counter.Snapshot.Calls==0);
        var capped=counter.Begin(1,true,200,7);counter.Pacing(1,1,true,false);counter.Complete(capped,1,true);value=counter.Drain();Near(value.AdmittedSeconds,.2f);Near(value.RawOfferedSeconds,1);Near(value.OriginalCapLostSeconds,1-.2f);Near(value.PacingLostSeconds,0);
        var outer=counter.Begin(.03f,true,201,7);var nested=counter.Begin(0,true,201,7);counter.Pacing(0,0,true,false);counter.Complete(nested,0,true);
        counter.Pacing(.03f,.02f,true,true);counter.Complete(outer,.02f,true);value=counter.Drain();Check(value.Calls==2);Near(value.PacingLostSeconds,.01);Check(counter.Depth==0);
        var fault=counter.Begin(.1f,true,202,7);counter.Pacing(.1f,.01f,true,true);counter.Exception(fault);counter.Exception(fault);
        value=counter.Drain();Check(value.Exceptions==1&&value.Calls==0&&value.OfferedSeconds==0&&counter.Depth==0);
        var stale=counter.Begin(.1f,true,203,7);counter.Reset();var fresh=counter.Begin(.02f,true,1,8);
        counter.Complete(stale,.1f,true);counter.Pacing(.02f,.02f,true,false);counter.Complete(fresh,.02f,true);value=counter.Drain();Check(value.Calls==1&&value.FirstFrame==1&&counter.Depth==0);
        var tokens=new ColonyPacingLossToken[ColonyPacingLoss.Capacity+1];for(var k=0;k<tokens.Length;++k)tokens[k]=counter.Begin(.01f,true,210,8);
        for(var k=tokens.Length-1;k>=0;--k){counter.Pacing(.01f,.01f,true,false);counter.Complete(tokens[k],.01f,true);}
        value=counter.Drain();Check(value.Overflow==1&&value.Calls==ColonyPacingLoss.Capacity&&counter.Depth==0);
        var inactive=counter.Begin(.1f,false,0,0);counter.Pacing(.1f,0,false,true);counter.Complete(inactive,.1f,false);Check(counter.Drain().Calls==0);
        var invalid=counter.Begin(float.NaN,true,211,8);counter.Complete(invalid,float.NaN,true);value=counter.Drain();Check(value.InvalidDt==1&&value.OfferedSeconds==0&&value.AdmittedSeconds==0);
        var gained=counter.Begin(.01f,true,212,8);counter.Pacing(.1f,.02f,true,true);counter.Complete(gained,.02f,true);value=counter.Drain();Check(value.AttributionErrors==1);Near(value.GainedSeconds,.01);
        var cancelled=counter.Begin(.1f,true,213,8);counter.Cancel(cancelled);Check(counter.Depth==0&&counter.Drain().Calls==0);
        // The warmed per-call path uses the fixed array and value tokens only.
        for(var k=0;k<100;++k){var t=counter.Begin(.03f,true,220,8);counter.Pacing(.03f,.02f,true,true);counter.Complete(t,.02f,true);}counter.Drain();
        var before=GC.GetAllocatedBytesForCurrentThread();for(var k=0;k<100000;++k){var t=counter.Begin(.03f,true,220,8);counter.Pacing(.03f,.02f,true,true);counter.Complete(t,.02f,true);}
        Check(GC.GetAllocatedBytesForCurrentThread()==before);value=counter.Drain();Check(value.Calls==100000);
        Console.WriteLine($"Pacing loss controls: {checks} checks passed.");
        GateControls();
    }

    // The serial hold before overlapped pacing, as an oracle for Admit(false, ...).
    static bool SerialHold(int subTick, float simDt, ref float dt, out bool held)
    {
        var allowed = (12 - subTick) / 60f - simDt - 1e-4f;
        if ((dt < .2f ? dt : .2f) < allowed) { held = false; return true; }
        held = true;
        if (allowed <= 0) return false;
        dt = allowed; return true;
    }
    // Game.SimEveryTick: returns the positive and zero-time UnsafeSim200ms calls it makes.
    static (int Positive, int Zero) SimEveryTick(ref int subTick, ref float simDt, float dt)
    {
        dt = dt < .2f ? dt : .2f; simDt += dt; var positive = 0;
        if (simDt >= 1f / 60f)
        {
            do { subTick = (subTick + 1) % 12; if (subTick == 0) ++positive; simDt -= 1f / 60f; } while (simDt >= 1f / 60f);
            return (positive, 0);
        }
        return (0, 1);
    }
    static void GateControls()
    {
        var start = checks;
        var random = new Random(20261105);
        // Serial pacing keeps its exact decisions.
        for (var k = 0; k < 200000; ++k)
        {
            var subTick = random.Next(12); var simDt = (float)(random.NextDouble() / 60); var dt = (float)(random.NextDouble() * (k % 3 == 0 ? .5 : .06));
            var a = dt; var b = dt;
            var run = ColonyPacingGate.Admit(false, subTick, simDt, ref a, out var held);
            var expected = SerialHold(subTick, simDt, ref b, out var expectedHeld);
            Check(run == expected && held == expectedHeld && BitConverter.SingleToInt32Bits(a) == BitConverter.SingleToInt32Bits(b));
        }
        // Overlapped pacing against SimEveryTick: while a posted frame awaits its
        // handoff the game posts neither a tick nor a zero-time frame, and every
        // call posts at most one tick; once handed, the next tick follows.
        foreach (var frameSeconds in new[] { 1 / 144f, 1 / 60f, .05f, .12f, .25f })
        {
            int subTick = 0, posts = 0, zeros = 0, handed = 0, pendingCalls = 0; float simDt = 0; var pending = false;
            for (var call = 0; call < 20000; ++call)
            {
                var dt = frameSeconds * (float)(.5 + random.NextDouble());
                if (pending && --pendingCalls <= 0) { pending = false; ++handed; }
                if (pending)
                {
                    var run = ColonyPacingGate.Admit(true, subTick, simDt, ref dt, out _);
                    if (!run) continue;
                    var (positive, zero) = SimEveryTick(ref subTick, ref simDt, dt);
                    Check(positive == 0 && zero == 0);
                }
                else
                {
                    var (positive, zero) = SimEveryTick(ref subTick, ref simDt, dt);
                    Check(positive <= 1);
                    posts += positive; zeros += zero;
                    if (positive != 0) { pending = true; pendingCalls = random.Next(1, 6); }
                }
            }
            Check(posts > 100 && posts - handed <= 1);
            if (frameSeconds < 1 / 60f) Check(zeros > 0);
        }
        var frames = new ColonyPostedFrames();
        Check(frames.Posted == 0 && frames.Shown == 0);
        frames.Post(5); Check(frames.Posted == 5 && frames.Shown == 0);
        frames.Post(6); Check(frames.Posted == 6 && frames.Shown == 5);
        var refused = false; try { frames.Post(6); } catch (InvalidOperationException) { refused = true; } Check(refused && frames.Posted == 6 && frames.Shown == 5);
        frames.Reset(); Check(frames.Posted == 0 && frames.Shown == 0); frames.Post(1); Check(frames.Shown == 0);
        Console.WriteLine($"Pacing gate controls: {checks - start} checks passed.");
    }
}
