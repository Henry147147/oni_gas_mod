using System.Reflection;
using MixedAtmospheres.Oni;

static class ExtraProof
{
    private static int checks;
    private static void Check(bool ok,string message)
    { ++checks;if(!ok)throw new Exception(message); }
    private static void Balance(ColonyPacingRetained value)
    {
        var t=value.Snapshot;
        Check(Math.Abs(t.Offered+t.UnexpectedAdded-t.Admitted-value.Owed-t.Dropped)<1e-7,"Time balance failed");
        Check(value.Owed>=0&&value.Owed<=value.OwedLimit,"Debt bound failed");
        Check(t.Errors==0&&t.UnexpectedAdded==0,"Unexplained admission");
    }
    private static void Step(ColonyPacingRetained value,float raw,bool busy=false,float allowance=.2f,bool veto=false,bool zero=false)
    {
        var owed=value.Owed;var dt=raw;var token=value.Prepare(ref dt,true);
        Check(dt<=.2f,"Original argument ceiling changed");
        Check(dt<=raw+Math.Min(owed,value.ExtraLimit),"Configured loan exceeded");
        var before=dt;var run=!busy||dt<allowance;
        if(!run&&allowance>0){dt=allowance;run=true;}
        value.Pacing(before,dt,run,busy);
        if(zero)dt=0;
        value.Admission(dt,run&&!veto);value.Commit(token,dt,run&&!veto);value.Cancel(token);
        if(busy&&run)Check(dt<=allowance,"Busy fence crossed");
        Balance(value);
    }
    private static void Main()
    {
        typeof(ExtendedProof).GetMethod("Main",BindingFlags.NonPublic|BindingFlags.Static)!.Invoke(null,null);
        Check(new ColonyPacingRetained().ExtraLimit==1f/60f,"Default repayment changed");
        foreach(var invalid in new[]{0f,-0f,-1f,float.NaN,float.PositiveInfinity,float.NegativeInfinity,.034f})
        {
            var refused=false;
            try{_=new ColonyPacingRetained(2,invalid);}catch(ArgumentOutOfRangeException e){refused=e.ParamName=="maximumExtra";}
            Check(refused,"Invalid repayment admitted");
        }
        foreach(var text in new string?[]{null,"","NaN","Infinity","-1","0","0.034","0,033"})
            Check(ColonyPacingRetained.ReadMaximumExtra(text)==1f/60f,"Invalid repayment changed default");
        Check(ColonyPacingRetained.ReadMaximumExtra("0.03333333333333333")==1f/30f,"Configured repayment missed");
        var c=new ColonyPacingRetained(2,1f/30f);Step(c,.8f);var owed=c.Owed;
        var dt=.03f;var token=c.Prepare(ref dt,true);
        Check(dt>.03f+1f/60f,"Larger configured repayment unused");
        Check((double)dt-.03f<=1f/30f,"Repayment rounded above ceiling");
        c.Pacing(dt,dt,true,false);c.Admission(dt,true);c.Commit(token,dt,true);Balance(c);
        Check(c.Owed<owed,"Loan was not repaid");
        Check(c.Snapshot.RepaymentOffers==1&&c.Snapshot.UnclippedLoans==1&&c.Snapshot.ClippedLoans==0,"Unclipped telemetry wrong");
        Check(c.Snapshot.ProposedExtra==c.Snapshot.Repaid,"Admitted proposal telemetry differs");
        var repaid=c.Snapshot.Repaid;var proposed=c.Snapshot.ProposedExtra;
        Step(c,.03f,true,0);
        Check(c.Snapshot.ClippedLoans==1&&c.Snapshot.ProposedExtra>proposed,"Held proposal telemetry missing");
        Check(c.Snapshot.Repaid==repaid,"Held proposal counted as repayment");
        for(var sub=0;sub<12;++sub)foreach(var partial in new[]{0f,.005f,.016f})
        {
            c=new ColonyPacingRetained(2,1f/30f);Step(c,2.1f);
            var allowance=(12-sub)/60f-partial-1e-4f;
            Step(c,.18f,true,allowance);
            Check(allowance<=0||partial+allowance<(12-sub)/60f,"Original epsilon changed");
        }
        c=new ColonyPacingRetained(2,1f/30f);
        for(var k=0;k<20000;++k)Step(c,k%97==0?.8f:.03f,k%11==0,k%22==0?0:.007f,k%127==0,k%223==0);
        c.Reset();Balance(c);Check(c.Owed==0,"Reset retained debt");
        for(var k=0;k<100;++k)Step(c,.03f);
        var bytes=GC.GetAllocatedBytesForCurrentThread();
        for(var k=0;k<100000;++k){dt=.03f;token=c.Prepare(ref dt,true);c.Pacing(dt,dt,true,false);c.Admission(dt,true);c.Commit(token,dt,true);}
        Check(GC.GetAllocatedBytesForCurrentThread()==bytes,"Configured repayment added allocations");Balance(c);
        Console.WriteLine($"Private 1/30-second repayment: {checks} additional controls passed; original/default suites unchanged.");
    }
}
