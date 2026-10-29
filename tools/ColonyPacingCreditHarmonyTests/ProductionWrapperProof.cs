using System;
using System.Reflection;
using HarmonyLib;
using MixedAtmospheres.Oni;

// The original 87 Harmony assertions remain in Program.cs. These checks cover
// the production defaults, opt-out, logging policy and configured repayment.
static class ProductionWrapperProof
{
    private static int checks;
    private static void Check(bool value,string why){++checks;if(!value)throw new Exception(why);}
    private static ColonyPacingRetained Counter=>(ColonyPacingRetained)typeof(ColonyPacingRetainedLog).GetField("counter",BindingFlags.NonPublic|BindingFlags.Static)!.GetValue(null)!;
    private static void Clean()
    {
        ColonyPacingRetainedLog.Reset();Guards.Clear();ColonyOwner.Active=true;ColonyOwner.Status.State=1;++ColonyOwner.Status.Session;
        SpeedControlScreen.Instance.IsPaused=false;SpeedControlScreen.Instance.Mode=2;
        ColonyLoadRefusal.Refused=ColonyCallbackRequests.Pending=ColonyOwnerPausedResave.Holding=ColonyContinuationProbe.Holding=false;
    }
    private static int Main()
    {
        try
        {
            Check(Counter.OwedLimit==2,"Production wrapper did not select2seconds; pure defaults must remain unchanged");
            Check(Counter.ExtraLimit==1f/30f,"Production wrapper did not select1f30; pure defaults must remain unchanged");
            Check(ColonyPacingLossLog.Enabled==(Environment.GetEnvironmentVariable("MA_COLONY_PACING_LOSS_LOG")=="1"&&!ColonyPacingRetainedLog.Enabled),"Legacy loss diagnostics ran alongside credit or ignored their explicit flag");
            if(!ColonyPacingRetainedLog.Enabled&&Environment.GetEnvironmentVariable("MA_TEST_CREDIT_REQUIRE_ENABLED")!="1")
            {
                new Harmony("ma.credit.optout.controls").PatchAll(typeof(Program).Assembly);Clean();var game=new Game();
                game.SimEveryTick(.8f);Check(game.Calls==1&&game.Actual==.2f&&Counter.Owed==0,"Opt-out retained capped time");
                game.SimEveryTick(.03f);Check(game.Calls==2&&game.Actual==.2f+.03f&&Counter.Owed==0,"Opt-out repaid time");
                Guards.Busy=true;Guards.Allowance=.005f;game.SimEveryTick(.03f);Check(game.Calls==3&&game.Actual==.2f+.03f+.005f,"Opt-out changed original busy allowance");
                Guards.LaterVeto=true;game.SimEveryTick(.03f);Check(game.Calls==3&&Counter.Snapshot.Calls==0&&Counter.Depth==0,"Opt-out installed retention hooks");
                ColonyPacingLossLog.Reset();Check((UnityEngine.Debug.LossMessages>0)==ColonyPacingLossLog.Enabled,"Legacy observer Prepare/emission did not follow flag policy");
                Console.WriteLine($"PASS pacing credit opt-out: {checks} assertions; original cap, busy fence and veto preserved.");return 0;
            }
            typeof(Program).GetMethod("Main",BindingFlags.NonPublic|BindingFlags.Static)!.Invoke(null,null);
            Clean();var candidate=new Game();candidate.SimEveryTick(.8f);
            Check(Math.Abs(Counter.Owed-((double).8f-.2f))<1e-7,"Configured hitch bound not applied");
            for(var k=0;k<1000;++k)candidate.SimEveryTick(.01f);
            var spacing=(double)BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(.01f)+1)-.01f;
            Check(Counter.Owed>=0&&Counter.Owed<spacing,"Repayable Harmony credit remained");
            var totals=Counter.Snapshot;Check(Math.Abs(totals.Offered+totals.UnexpectedAdded-totals.Admitted-Counter.Owed-totals.Dropped)<1e-7,"Configured Harmony accounting failed");
            Check(Counter.Depth==0&&totals.Errors==0&&totals.UnexpectedAdded==0,"Configured Harmony credit failed");
            Clean();UnityEngine.Debug.Messages=UnityEngine.Debug.Errors=0;candidate=new Game();candidate.SimEveryTick(.8f);ColonyPacingRetainedLog.Reset();
            Check((UnityEngine.Debug.Messages>0)==(Environment.GetEnvironmentVariable("MA_COLONY_PACING_CREDIT_LOG")=="1"),"Full pacing telemetry was not opt-in");
            Check(UnityEngine.Debug.Errors==0,"Ordinary retention emitted an error");
            Clean();UnityEngine.Debug.Errors=0;candidate=new Game();candidate.SimEveryTick(.35f);Guards.LaterValue=.1f;candidate.SimEveryTick(.03f);ColonyPacingRetainedLog.Reset();
            Check(UnityEngine.Debug.Errors>0,"Nonzero accounting faults were hidden with diagnostics disabled");
            Check(UnityEngine.Debug.LossMessages==0,"Legacy loss observer emitted strings alongside credit");
            Console.WriteLine($"PASS production pacing credit: original81Harmony plus{checks} configured/production assertions (historic6configured checks retained); Mono allocation API unsupported, no allocation claim.");return 0;
        }
        catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
}
