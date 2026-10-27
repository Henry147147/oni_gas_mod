using System.Reflection;

internal static class ClusterTemplateTests
{
    internal static void Run(Assembly assembly)
    {
        const BindingFlags flags=BindingFlags.Static|BindingFlags.NonPublic;
        static void Check(bool ok,string message){if(!ok)throw new Exception(message);}
        var worlds=assembly.GetType("MixedAtmospheres.Oni.ColonyClusterWorlds")!;
        var patch=assembly.GetType("MixedAtmospheres.Oni.ColonyRocketTemplatePatch")!;
        var prefix=patch.GetMethod("Prefix",flags)!;
        var reset=worlds.GetMethod("Reset",flags)!;
        bool Pending()
        {
            var report=worlds.GetMethod("Report",flags)!.Invoke(null,null)!;
            return (bool)report.GetType().GetProperty("PendingLayout")!.GetValue(report)!;
        }
        var target=typeof(WorldContainer).GetMethod("PlaceInteriorTemplate",BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic)!;
        Check(target.GetParameters().Length==2&&target.GetParameters()[1].ParameterType==typeof(System.Action),
            "Rocket template completion no longer uses the inspected action parameter.");
        reset.Invoke(null,null);
        try
        {
            System.Action untouched=()=>{};object?[] inactive={untouched};prefix.Invoke(null,inactive);
            Check(ReferenceEquals(inactive[0],untouched),"Inactive/base-game templates acquired an experimental callback.");
            var regionType=assembly.GetType("MixedAtmospheres.Oni.ColonyWorldRegion")!;
            var regions=Array.CreateInstance(regionType,1);
            regions.SetValue(Activator.CreateInstance(regionType,BindingFlags.Instance|BindingFlags.NonPublic,null,new object[]{0,0,0,4,4,false},null),0);
            var layoutType=assembly.GetType("MixedAtmospheres.Oni.ColonyWorldLayout")!;
            var layout=Activator.CreateInstance(layoutType,BindingFlags.Instance|BindingFlags.NonPublic,null,new object[]{8,8,regions},null)!;
            worlds.GetMethod("Initialize",flags)!.Invoke(null,new[]{layout});
            var calls=0;System.Action completed=()=>{Check(Pending(),"Game callback ran before the ready-world signal.");++calls;};
            object?[] active={completed};prefix.Invoke(null,active);
            Check(calls==0&&!Pending(),"A rocket world was declared ready before its template completed.");
            ((System.Action)active[0]!)();
            Check(calls==1&&Pending(),"Template completion lost its callback or ready-world signal.");
        }
        finally{reset.Invoke(null,null);}
        Console.WriteLine("PASS rocket template completion: original callback retained, readiness deferred, ordinary templates untouched");
    }
}
