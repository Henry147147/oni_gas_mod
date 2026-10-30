namespace MixedAtmospheres.Oni;

internal static class OniNativeColony
{
    internal struct Status
    {
        internal ulong Session,CompletedFrames;
        internal static Status Empty=>default;
    }
    internal static Status Current;
    internal static int Result,Reads;
    internal static int CopyStatus(ref Status value){++Reads;value=Current;return Result;}
    internal static void Check(int result){if(result!=0)throw new InvalidOperationException("Native status refused");}
}

internal static partial class ColonyScenarios
{
    private static ulong storedHeatSourceSession,storedHeatSourceFrame;
    private static bool storedHeatFenceArmed;
    // The old UI stage could be30 while the physical window was only21.
    internal static int UiFrame;
    private static void CheckClock(bool value){if(!value)throw new Exception("Incorrect physical heat window");}
    private static void RefuseClock(Action action)
    {try{action();}catch(InvalidOperationException){return;}throw new Exception("Invalid physical clock accepted");}
    internal static void RunClockControls()
    {
        storedHeatSourceSession=1;storedHeatSourceFrame=187;OniNativeColony.Result=0;OniNativeColony.Reads=0;
        foreach(var ui in new[]{0,29,30,31,1000})foreach(var elapsed in new ulong[]{0,21,22,23})
        {
            UiFrame=ui;OniNativeColony.Current=new(){Session=1,CompletedFrames=187+elapsed};
            CheckClock(StoredHeatFinalDue()==(elapsed>=22));
            CheckClock(StoredHeatWindowValid(1,187+elapsed)==(elapsed==22));
        }
        CheckClock(OniNativeColony.Reads==20);
        CheckClock(!StoredHeatWindowValid(2,209)&&!StoredHeatWindowValid(1,186));
        storedHeatFenceArmed=true;
        OniNativeColony.Current=new(){Session=1,CompletedFrames=208};CheckClock(StoredHeatMayTick(.2f));
        OniNativeColony.Current.CompletedFrames++;CheckClock(!StoredHeatMayTick(.2f));
        CheckClock(StoredHeatMayTick(0)&&!StoredHeatMayTick(.2f));
        storedHeatFenceArmed=false;CheckClock(StoredHeatMayTick(.2f));
        OniNativeColony.Current=new(){Session=2,CompletedFrames=209};RefuseClock(()=>StoredHeatFinalDue());
        OniNativeColony.Current=new(){Session=1,CompletedFrames=186};RefuseClock(()=>StoredHeatFinalDue());
        OniNativeColony.Current=new(){Session=1,CompletedFrames=209};OniNativeColony.Result=6;RefuseClock(()=>StoredHeatFinalDue());OniNativeColony.Result=0;
        Console.WriteLine("PASS physical heat clock: UI-stage independence,21/22/23frame admission, exact endpoint window, session/backwards/native-read refusal");
    }
}
