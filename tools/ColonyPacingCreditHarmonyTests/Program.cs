using System.Reflection;
using System.Runtime.CompilerServices;
using HarmonyLib;
using MixedAtmospheres.Oni;

namespace UnityEngine { public static class Debug {public static int Messages,Errors,LossMessages;public static void Log(string message){++Messages;if(message.Contains("pacing loss session"))++LossMessages;}public static void LogError(string message){++Errors;}} }
public sealed class SpeedControlScreen
{public static SpeedControlScreen Instance=new();public bool IsPaused;public int Mode=2;public int GetSpeed()=>Mode;}
public sealed class SaveLoader
{[MethodImpl(MethodImplOptions.NoInlining)]public void Save(string path,bool autosave,bool screenshot){if(Guards.ThrowSave)throw Guards.Failure;}}
public sealed class Game
{
    public float Actual;public int Calls,PartialEffects;public Action? Inside;
    [MethodImpl(MethodImplOptions.NoInlining)]public void SimEveryTick(float dt)
    {++Calls;Actual+=Math.Min(dt,.2f);++PartialEffects;var action=Inside;Inside=null;action?.Invoke();if(Guards.ThrowOriginal)throw Guards.Failure;}
}
namespace MixedAtmospheres.Oni
{
    internal static class ColonyOwner {internal static bool Active=true;internal static StatusValue Status=new(){Session=1,State=1,CompletedFrames=0};internal struct StatusValue{public ulong Session,CompletedFrames;public uint State;}}
    internal static class ColonyLoadRefusal {internal static bool Refused;}
    internal static class ColonyCallbackRequests {internal static bool Pending;}
    internal static class ColonyOwnerPausedResave {internal static bool Holding;}
    internal static class ColonyContinuationProbe {internal static bool Holding;}
}
static class Guards
{
    internal static bool FirstZero,FirstVeto,LaterVeto,Busy,ThrowOriginal,ThrowPostfix,ThrowSave;
    internal static float? LaterValue,PostfixValue;
    internal static float Allowance=.2f;
    internal static readonly Exception Failure=new InvalidOperationException("sentinel");
    internal static void Clear(){FirstZero=FirstVeto=LaterVeto=Busy=ThrowOriginal=ThrowPostfix=ThrowSave=false;LaterValue=PostfixValue=null;Allowance=.2f;}
}
[HarmonyPatch(typeof(Game),nameof(Game.SimEveryTick))]static class FirstGuard
{
    [HarmonyPriority(Priority.First)]static bool Prefix(ref float __0)
    {if(Guards.FirstZero||ColonyCallbackRequests.Pending||ColonyOwnerPausedResave.Holding)__0=0;return !Guards.FirstVeto&&!ColonyLoadRefusal.Refused&&!ColonyContinuationProbe.Holding;}
}
[HarmonyPatch(typeof(Game),nameof(Game.SimEveryTick))]static class PacingGuard
{
    static bool Prefix(ref float __0)
    {
        var before=__0;var run=!Guards.Busy||Math.Min(__0,.2f)<Guards.Allowance;
        if(!run&&Guards.Allowance>0){__0=Guards.Allowance;run=true;}
        if(ColonyPacingLossLog.Enabled)ColonyPacingLossLog.Pacing(before,run?__0:0,run,Guards.Busy);
        ColonyPacingRetainedLog.Pacing(before,run?__0:0,run,Guards.Busy);return run;
    }
}
[HarmonyPatch(typeof(Game),nameof(Game.SimEveryTick))]static class LaterGuard
{
    [HarmonyPriority(Priority.Low)]static bool Prefix(ref float __0){if(Guards.LaterValue.HasValue)__0=Guards.LaterValue.Value;return !Guards.LaterVeto;}
    static void Postfix(ref float __0){if(Guards.PostfixValue.HasValue)__0=Guards.PostfixValue.Value;if(Guards.ThrowPostfix)throw Guards.Failure;}
}
static class Program
{
    static int checks;
    static ColonyPacingRetained Counter=>(ColonyPacingRetained)typeof(ColonyPacingRetainedLog).GetField("counter",BindingFlags.Static|BindingFlags.NonPublic)!.GetValue(null)!;
    static void Check(bool v){++checks;if(!v)throw new Exception("Harmony retained check "+checks);}
    static void Near(double a,double b){if(Math.Abs(a-b)>=1e-7)Console.WriteLine($"Mismatch check {checks+1}: actual {a:R}, expected {b:R}");Check(Math.Abs(a-b)<1e-7);}
    static Game Fresh()
    {
        Guards.Clear();ColonyPacingRetainedLog.Reset();ColonyOwner.Active=true;ColonyOwner.Status.Session++;ColonyOwner.Status.State=1;
        SpeedControlScreen.Instance.IsPaused=false;SpeedControlScreen.Instance.Mode=2;ColonyLoadRefusal.Refused=ColonyCallbackRequests.Pending=ColonyOwnerPausedResave.Holding=ColonyContinuationProbe.Holding=false;
        var g=new Game();g.SimEveryTick(.35f);Near(Counter.Owed,.35f-.2f);return g;
    }
    static void Balance(){var s=Counter.Snapshot;Near(s.Offered+s.UnexpectedAdded,s.Admitted+Counter.Owed+s.Dropped);}
    static void Main()
    {
        Environment.SetEnvironmentVariable("MA_COLONY_PACING_RETAIN","1");
        new Harmony("ma.private.retention.controls").PatchAll(typeof(Program).Assembly);
        var g=Fresh();var debt=Counter.Owed;g.SimEveryTick(.03f);Check(Counter.Owed<debt);Balance();
        g=Fresh();debt=Counter.Owed;Guards.LaterVeto=true;g.SimEveryTick(.03f);Near(Counter.Owed,debt);Check(g.Calls==1);Balance();
        g=Fresh();debt=Counter.Owed;Guards.LaterValue=.01f;g.SimEveryTick(.03f);Near(Counter.Owed,debt);Near(g.Actual,.21f);Balance();
        g=Fresh();debt=Counter.Owed;Guards.LaterValue=.04f;g.SimEveryTick(.03f);Near(Counter.Owed,debt-.01f);Balance();
        g=Fresh();Guards.LaterValue=.1f;g.SimEveryTick(.03f);Near(Counter.Owed,0);Check(Counter.Snapshot.Errors==1);Balance();
        g=Fresh();Guards.PostfixValue=0;g.SimEveryTick(.03f);Near(Counter.Owed,0);Near(Counter.Snapshot.Admitted,g.Actual);Balance();
        g=Fresh();Guards.ThrowPostfix=true;try{g.SimEveryTick(.03f);Check(false);}catch(Exception e){Check(ReferenceEquals(e,Guards.Failure));}
        Near(Counter.Owed,0);Check(Counter.Snapshot.Exceptions==1&&Counter.Depth==0);Balance();
        g=Fresh();Guards.ThrowOriginal=true;try{g.SimEveryTick(.03f);Check(false);}catch(Exception e){Check(ReferenceEquals(e,Guards.Failure));}
        Near(Counter.Owed,0);Check(g.PartialEffects==2&&Counter.Snapshot.Exceptions==1&&Counter.Depth==0);Balance();
        g=Fresh();Guards.FirstZero=true;g.SimEveryTick(.03f);Near(Counter.Owed,0);Balance();
        g=Fresh();ColonyContinuationProbe.Holding=true;g.SimEveryTick(.03f);ColonyPacingRetainedLog.AfterUpdate();Near(Counter.Owed,0);Check(g.Calls==1);Balance();
        g=Fresh();ColonyCallbackRequests.Pending=true;g.SimEveryTick(.03f);Near(Counter.Owed,0);Near(g.Actual,.2f);Balance();
        g=Fresh();ColonyOwnerPausedResave.Holding=true;g.SimEveryTick(.03f);Near(Counter.Owed,0);Near(g.Actual,.2f);Balance();
        g=Fresh();g.Inside=()=>g.SimEveryTick(0);g.SimEveryTick(.03f);Check(Counter.Owed>0&&Counter.Snapshot.Errors==0);Balance();
        g=Fresh();g.Inside=()=>g.SimEveryTick(.03f);g.SimEveryTick(.03f);Check(Counter.Owed>0&&Counter.Snapshot.Errors==0);Balance();
        g=Fresh();g.Inside=()=>SpeedControlScreen.Instance.IsPaused=true;g.SimEveryTick(.03f);Near(Counter.Owed,0);Balance();
        g=Fresh();g.Inside=()=>SpeedControlScreen.Instance.Mode=1;g.SimEveryTick(.03f);Near(Counter.Owed,0);Balance();
        g=Fresh();SpeedControlScreen.Instance.IsPaused=true;ColonyPacingRetainedLog.AfterUpdate();Near(Counter.Owed,0);
        g=Fresh();Guards.Busy=true;Guards.Allowance=.005f;g.SimEveryTick(.03f);Near(g.Actual,.205f);Check(Counter.Owed>.15);Balance();
        g=Fresh();Guards.Busy=true;Guards.Allowance=0;g.SimEveryTick(.03f);Near(g.Actual,.2f);Check(g.Calls==1);Balance();
        g=Fresh();new SaveLoader().Save("private",false,false);Near(Counter.Owed,0);Balance();
        g=Fresh();Guards.ThrowSave=true;try{new SaveLoader().Save("private",false,false);Check(false);}catch(Exception e){Check(ReferenceEquals(e,Guards.Failure));}Near(Counter.Owed,0);Balance();
        g=Fresh();g.Inside=()=>new SaveLoader().Save("private",false,false);g.SimEveryTick(.03f);Near(Counter.Owed,0);Balance();
        g=Fresh();ColonyOwner.Status.Session++;ColonyPacingRetainedLog.AfterUpdate();Near(Counter.Owed,0);
        Console.WriteLine($"Private Harmony retained controls: {checks} checks passed.");
    }
}
