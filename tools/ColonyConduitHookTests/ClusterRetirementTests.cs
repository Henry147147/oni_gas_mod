using System.Reflection;

internal static class ClusterRetirementTests
{
    internal static void Run(Assembly assembly)
    {
        const BindingFlags all=BindingFlags.Static|BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
        static void Check(bool ok,string reason){if(!ok)throw new Exception(reason);}
        var destroy=typeof(ClusterManager).GetMethod("DestoryRocketInteriorWorld",all)!;
        Check(destroy.GetParameters().Select(p=>p.ParameterType).SequenceEqual(new[]{typeof(int),typeof(ClustercraftExteriorDoor)}),"Rocket destruction signature changed.");
        var delete=typeof(ClusterManager).GetMethod("DeleteWorldObjects",all)!;
        Check(delete.GetParameters().Single().ParameterType==typeof(WorldContainer),"Rocket native cleanup lost its world parameter.");
        var resources=typeof(WorldContainer).GetMethod("TransferLiquidsSolidsAndGases",all)!;
        Check(resources.GetParameters().Length==2&&resources.GetParameters()[1].ParameterType==typeof(HashSet<int>),"Landed recovery signature changed.");
        var debris=typeof(WorldContainer).GetMethod("TransferLiquidsSolidsAndGasesToDebris",all)!;
        Check(debris.GetParameters().Select(p=>p.ParameterType).SequenceEqual(new[]{typeof(List<Storage>).MakeByRefType(),typeof(HashSet<int>),typeof(SimHashes)}),
            "Airborne recovery signature changed.");
        var retirement=assembly.GetType("MixedAtmospheres.Oni.ColonyClusterRetirement")!;
        retirement.GetMethod("Reset",all)!.Invoke(null,null);
        Check(!(bool)retirement.GetProperty("Pending",all)!.GetValue(null)!,"Inactive retirement reports a pending world.");
        Check((bool)retirement.GetMethod("Begin",all)!.Invoke(null,new object[]{1234})!,"Inactive/base-game destruction acquired a retirement transaction.");
        retirement.GetMethod("PumpDeletes",all)!.Invoke(null,null); // inactive: no native or Unity access
        var driver=assembly.GetType("MixedAtmospheres.Oni.ColonyPacingDriver")!;
        Check(driver.GetMethod("LateUpdate",all)?.ReturnType==typeof(void),"Deferred cleanup lost its Unity LateUpdate boundary.");
        var barrier=assembly.GetType("MixedAtmospheres.Oni.ColonySaveBarrier")!;
        bool Eligible()=>(bool)barrier.GetProperty("CanRunLifecycle",all)!.GetValue(null)!;
        Check(Eligible(),"Idle lifecycle boundary is unavailable.");
        barrier.GetMethod("EnterStep",all)!.Invoke(null,null);
        try{Check(!Eligible(),"A lifecycle command became eligible inside an unfinished native publication.");}
        finally{barrier.GetMethod("ExitStep",all)!.Invoke(null,null);}
        var unregister=assembly.GetType("MixedAtmospheres.Oni.ColonyRocketUnregisterPatch")!.GetMethod("Prefix",all)!;
        Check(unregister.ReturnType==typeof(bool)&&unregister.GetParameters().Single().ParameterType==typeof(WorldContainer),"The allocation fence lost its live world.");
        Console.WriteLine("PASS rocket retirement bindings: live destruction, both recovery destinations, native grid cleanup, unregister fence; inactive path untouched");
    }
}
