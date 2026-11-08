using System.Reflection;
using HarmonyLib;

internal static class UiInspectionTests
{
    internal static void Run(Assembly assembly)
    {
        void Check(bool condition, string why) { if (!condition) throw new Exception(why); }
        const BindingFlags flags = BindingFlags.NonPublic | BindingFlags.Static;
        var display = assembly.GetType("MixedAtmospheres.Oni.MixtureStatusDisplay")!;
        var classify = display.GetMethod("IsFullscreenManagementScreen", flags)!;
        bool Fullscreen(Type? type) => (bool)classify.Invoke(null, new object?[] { type })!;
        foreach (var type in new[] { typeof(ResearchScreen), typeof(SkillsScreen), typeof(StarmapScreen), typeof(ClusterMapScreen) })
            Check(Fullscreen(type), type.Name + " remains obscured by the fluid status panel.");
        foreach (var type in new Type?[] { null, typeof(ScheduleScreen), typeof(VitalsTableScreen), typeof(ReportScreen) })
            Check(!Fullscreen(type), "An ordinary colony panel incorrectly hides the fluid status.");
        Check(AccessTools.Field(typeof(ManagementMenu), "activeScreen").FieldType == typeof(ManagementMenu.ScreenData) &&
            typeof(KScreen).IsAssignableFrom(typeof(ManagementMenu.ScreenData).GetField("screen")!.FieldType),
            "The supported management menu's actual active-screen contract changed.");
        var active = PatchProcessor.GetOriginalInstructions(display.GetMethod("FullscreenManagementActive", flags));
        Check(!active.Any(i => i.operand is MethodInfo method && method.Name == "IsFullscreenUIActive"),
            "The panel still relies on the game's uninitialized fullscreen toggle list.");
        NativeContentsShares(assembly);
        Console.WriteLine("PASS management visibility: actual Research/Skills/Starmap/ClusterMap screens hide the panel, ordinary colony panels retain it; installed active-screen contract matches");
    }

    private static void NativeContentsShares(Assembly assembly)
    {
        const BindingFlags flags=BindingFlags.NonPublic|BindingFlags.Static;
        var inspection=assembly.GetType("MixedAtmospheres.Oni.MixtureInspection")!;
        var exact=inspection.GetField("ExactReadings",flags)!;
        var figures=inspection.GetField("SignificantFigures",flags)!;
        var format=assembly.GetType("MixedAtmospheres.Oni.ColonyPipeInspection")!.GetMethod("StatusMassPercent",flags)!;
        string Share(double mass,double total)=>(string)format.Invoke(null,new object[]{mass,total})!;
        void Check(bool condition,string why){if(!condition)throw new Exception(why);}
        var savedExact=exact.GetValue(null);var savedFigures=figures.GetValue(null);
        try
        {
            foreach(var isExact in new[]{true,false})
                for(var precision=1;precision<=4;++precision)
                {
                    exact.SetValue(null,isExact);figures.SetValue(null,precision);
                    // The fixed mixed-pipe-readers scene: 3 kg water, 1 kg
                    // polluted water, and .003 kg salt. Do not change its
                    // inputs or expected native status-line share contract.
                    Check(Share(3,4.003)=="74.9%"&&Share(1,4.003)=="25%",
                        "Native Contents shares changed with exact/precision preferences.");
                    Check(Share(.999999,1)=="<100%"&&Share(1,1)=="100%",
                        "Native Contents labels an impure packet as pure.");
                    Check(Share(1e-50,1)=="1E-48%"&&Share(double.Epsilon,100)=="below numeric range",
                        "Native Contents rounds a positive share to zero.");
                }
        }
        finally{exact.SetValue(null,savedExact);figures.SetValue(null,savedFigures);}
        Console.WriteLine("PASS native Contents status shares: fixed water/polluted-water/salt recipe retains 74.9/25 percent in every display mode; positive traces and impure packets remain explicit");
    }
}
