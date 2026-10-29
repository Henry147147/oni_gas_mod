using MixedAtmospheres.Oni;
static class Program
{
    static int checks;
    static void Check(bool v){++checks;if(!v)throw new Exception("Retained pacing check "+checks);}
    static void Near(double a,double b)=>Check(Math.Abs(a-b)<1e-7);
    static void Balance(ColonyPacingRetained c){var s=c.Snapshot;Near(s.Offered+s.UnexpectedAdded,s.Admitted+c.Owed+s.Dropped);Check(c.Owed>=0&&c.Owed<=ColonyPacingRetained.MaximumOwed);}
    static void Step(ColonyPacingRetained c,float raw,bool busy=false,float allowance=.2f,bool eligible=true,bool veto=false,bool zero=false)
    {
        var dt=raw;var t=c.Prepare(ref dt,eligible);Check(!eligible||dt<=.2f);
        var before=dt;var run=!busy||dt<allowance;
        if(!run){if(allowance<=0)run=false;else{dt=allowance;run=true;}}
        c.Pacing(before,dt,run,busy);
        Check(!busy||!run||dt<=allowance);
        if(zero)dt=0;
        c.Admission(dt,run&&!veto);c.Commit(t,dt,run&&!veto);c.Cancel(t);Balance(c);
    }
    static void Main()
    {
        var c=new ColonyPacingRetained();
        Step(c,.35f);Near(c.Owed,.35f-.2f);Near(c.Snapshot.Dropped,0);
        var debt=c.Owed;Step(c,.03f);Check(c.Owed<debt);Check(c.Snapshot.Repaid<=1f/60f);
        debt=c.Owed;Step(c,.03f,true,0);Near(c.Owed,debt+.03f);
        debt=c.Owed;Step(c,.03f,true,.01f);Near(c.Owed,debt+.03f-.01f);
        // A later veto/zero must roll back tentative repayment.
        debt=c.Owed;Step(c,.03f,false,.2f,true,true);Near(c.Owed,debt);
        Step(c,.03f,false,.2f,true,false,true);Near(c.Owed,debt);
        // Explicit overload has bounded lag and accounts every dropped second.
        for(var k=0;k<100;++k)Step(c,1,true,0);
        Near(c.Owed,.4);Check(c.Snapshot.Dropped>99);
        c.Reset();Balance(c);Near(c.Owed,0);Check(c.Snapshot.ResetSeconds>=.4);
        // Known intentional holds do not collect debt or repay any old loan.
        Step(c,.03f,true,0,false);Near(c.Owed,0);Check(c.Snapshot.IgnoredSeconds>0);
        // Busy allowance always wins, including a call that could repay debt.
        Step(c,.3f);debt=c.Owed;Step(c,.04f,true,.005f);Near(c.Owed,debt+.04f-.005f);
        c.Reset();Balance(c);
        // A completed call's finalizer is inert; an exception resets debt.
        Step(c,.3f);var arg=.04f;var token=c.Prepare(ref arg,true);c.Exception(token);c.Exception(token);Balance(c);Near(c.Owed,0);Check(c.Snapshot.Exceptions==1);
        // Session/pause/speed resets make stale tokens inert.
        arg=.02f;token=c.Prepare(ref arg,true);c.Reset();c.Cancel(token);Check(c.Depth==0);
        // Zero-time lifecycle and nested positive calls cannot borrow twice.
        c=new ColonyPacingRetained();Step(c,.3f);var outerArg=.03f;var outer=c.Prepare(ref outerArg,true);
        var innerArg=.03f;var inner=c.Prepare(ref innerArg,true);Near(innerArg,.03f);c.Pacing(innerArg,innerArg,true,false);c.Admission(innerArg,true);c.Commit(inner,innerArg,true);
        c.Pacing(outerArg,outerArg,true,false);c.Admission(outerArg,true);c.Commit(outer,outerArg,true);Balance(c);
        Check(c.Depth==0);Check(c.Snapshot.Errors==0);
        // Every busy sub-tick boundary retains the exact existing epsilon:
        // even a full repayment candidate cannot reach the next owner post.
        for(var sub=0;sub<12;++sub)foreach(var partial in new[]{0f,.005f,.016f})
        {
            c=new ColonyPacingRetained();Step(c,.5f);
            var allowance=(12-sub)/60f-partial-1e-4f;
            var dt=.18f;var t=c.Prepare(ref dt,true);var before=dt;
            var run=dt<allowance;
            if(!run&&allowance>0){dt=allowance;run=true;}
            c.Pacing(before,dt,run,true);c.Admission(dt,run);c.Commit(t,dt,run);Balance(c);
            Check(!run||partial+dt<(12-sub)/60f);
        }
        // A nested zero-time publication neither repays nor discards debt
        // reserved by its outer positive call.
        c=new ColonyPacingRetained();Step(c,.3f);outerArg=.03f;outer=c.Prepare(ref outerArg,true);
        innerArg=0;inner=c.Prepare(ref innerArg,false);c.Pacing(0,0,true,false);c.Admission(0,true);c.Commit(inner,0,true);
        c.Pacing(outerArg,outerArg,true,false);c.Admission(outerArg,true);c.Commit(outer,outerArg,true);Balance(c);Check(c.Snapshot.Errors==0);
        // Matched hitch/overload/idle drain sequence, then explicit reset.
        c=new ColonyPacingRetained();for(var k=0;k<4000;++k)Step(c,k%97==0?.8f:.03f,k%11==0,k%22==0?0:.007f);
        for(var k=0;k<100;++k)Step(c,.01f);c.Reset();Balance(c);Check(c.Snapshot.Errors==0);
        // Fixed-size state has no warmed allocation.
        c=new ColonyPacingRetained();for(var k=0;k<100;++k){arg=.03f;token=c.Prepare(ref arg,true);c.Pacing(arg,arg,true,false);c.Admission(arg,true);c.Commit(token,arg,true);}
        var bytes=GC.GetAllocatedBytesForCurrentThread();for(var k=0;k<100000;++k){arg=.03f;token=c.Prepare(ref arg,true);c.Pacing(arg,arg,true,false);c.Admission(arg,true);c.Commit(token,arg,true);}
        Check(GC.GetAllocatedBytesForCurrentThread()==bytes);Balance(c);
        Console.WriteLine($"Private retained pacing controls: {checks} checks passed.");
    }
}
