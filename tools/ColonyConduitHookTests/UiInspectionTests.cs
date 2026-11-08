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
        Console.WriteLine("PASS management visibility: actual Research/Skills/Starmap/ClusterMap screens hide the panel, ordinary colony panels retain it; installed active-screen contract matches");
    }
}
