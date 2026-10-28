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
    }
}
