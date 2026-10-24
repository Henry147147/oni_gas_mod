using System;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    // Pipe-to-pipe buildings that change what passes through
    // (docs/mixed-pipes.md#machine-ports): the conduction panel bridge
    // exchanges heat with its body, the morb rover maker takes one germ kind.
    // Both move the whole owned mixture; the native decision code supplies
    // the rate, the body heat and the germs taken.
    internal static class ColonyPipeSpecialEdges
    {
        [ThreadStatic] private static HandleVector<int>.Handle capturing;
        [ThreadStatic] private static bool capture;
        [ThreadStatic] private static float capturedKilojoules;
        [ThreadStatic] private static bool captured;
        private static readonly MethodInfo Exchange=AccessTools.Method(typeof(ContactConductivePipeBridge.Instance),"ExchangeStorageTemperatureWithBuilding");
        private static readonly FieldInfo Timer=AccessTools.Field(typeof(ContactConductivePipeBridge),"noLiquidTimer"),
            MorbInput=AccessTools.Field(typeof(MorbRoverMaker.Instance),"inputCell"),MorbOutput=AccessTools.Field(typeof(MorbRoverMaker.Instance),"outputCell");
        private static readonly MethodInfo CanPump=AccessTools.PropertyGetter(typeof(MorbRoverMaker.Instance),"CanPumpGerms"),
            Remaining=AccessTools.PropertyGetter(typeof(MorbRoverMaker.Instance),"MorbDevelopment_RemainingGerms"),AddGerms=AccessTools.Method(typeof(MorbRoverMaker.Instance),"AddGerms");
        internal static long Panels { get; private set; }
        internal static PipeAmount PanelHeat { get; private set; }
        internal static long MorbGerms { get; private set; }
        internal static void Reset(){Panels=MorbGerms=0;PanelHeat=PipeAmount.Zero;captured=capture=false;}

        internal static bool Conductive(ContactConductivePipeBridge.Instance bridge,float dt)
        {
            var flow=Conduit.GetFlowManager(bridge.type);
            if(!ColonyMachinePorts.Enabled||flow==null||!ColonyConduits.Owns(flow,bridge.inputCell)||!ColonyConduits.Owns(flow,bridge.outputCell))return false;
            if(!flow.HasConduit(bridge.inputCell)||!flow.HasConduit(bridge.outputCell))return true;
            var source=ColonyConduits.ReadSynced(flow,bridge.inputCell);if(!((float)source.Mass>0))return true;
            var amount=Math.Min((float)source.Mass,bridge.def.pumpKGRate*dt);
            var accepted=ColonyConduits.Acceptable(flow,bridge.outputCell,amount,ColonyConduits.ElementHash(source.Dominant));
            if(!(accepted>0))return true;
            var timer=Timer.GetValue(bridge.sm);AccessTools.Method(timer.GetType(),"Set").Invoke(timer,new object[]{1.5f,bridge,false});
            var parcel=ColonyConduits.TransferAll(flow,bridge.inputCell,bridge.outputCell,accepted);
            if(parcel.Count==0)return true;
            // The native exchange uses the parcel's projection; the mixture
            // receives exactly the energy the body is given, negated.
            capturing=bridge.structureHandle;capture=true;captured=false;capturedKilojoules=0;
            try{Exchange.Invoke(bridge,new object[]{ColonyConduits.Compatibility(parcel),(float)parcel.Mass,dt});}
            finally{capture=false;}
            if(captured){var joules=PipeContactWork.Kilojoules(capturedKilojoules).Negated();ColonyConduits.AddHeat(flow,bridge.outputCell,joules);PanelHeat=PanelHeat.Plus(joules);}
            ++Panels;return true;
        }
        internal static void ObserveBodyEnergy(HandleVector<int>.Handle handle,float kilojoules)
        {
            if(!capture||!capturing.Equals(handle))return;
            if(captured)throw new InvalidOperationException("A conduction panel produced body energy twice for one parcel.");
            captured=true;capturedKilojoules=kilojoules;
        }

        internal static bool Morb(MorbRoverMaker.Instance maker,float dt)
        {
            if(!ColonyMachinePorts.Enabled)return false;
            var flow=Conduit.GetFlowManager(maker.def.GERM_INTAKE_CONDUIT_TYPE);var input=(int)MorbInput.GetValue(maker);var output=(int)MorbOutput.GetValue(maker);
            if(flow==null||!ColonyConduits.Owns(flow,input)||!ColonyConduits.Owns(flow,output))return false;
            if(!(bool)CanPump.Invoke(maker,null)||!flow.HasConduit(input)||!flow.HasConduit(output))return true;
            var source=ColonyConduits.ReadSynced(flow,input);if(!((float)source.Mass>0))return true;
            var amount=Math.Min((float)source.Mass,maker.def.GetConduitMaxPackageMass()*dt);
            var accepted=ColonyConduits.Acceptable(flow,output,amount,ColonyConduits.ElementHash(source.Dominant));
            if(!(accepted>0))return true;
            var parcel=ColonyConduits.TransferAll(flow,input,output,accepted);
            var available=0.0;foreach(var germ in parcel.Germs)if(germ.Id==maker.def.GERM_TYPE)available=Math.Floor(germ.Mass);
            var taken=(long)Math.Min(Math.Min((long)Remaining.Invoke(maker,null),maker.def.MAX_GERMS_TAKEN_PER_PACKAGE),available);
            if(taken<=0)return true;
            // Germs the building absorbs leave the pipe as a booked biological
            // exchange, like germs that die in it.
            ColonyConduits.AdjustGerms(flow,output,maker.def.GERM_TYPE,-taken,true);
            MorbGerms+=taken;AddGerms.Invoke(maker,new object[]{taken,true});return true;
        }
    }
    [HarmonyPatch(typeof(ContactConductivePipeBridge.Instance),"Flow")]
    internal static class ColonyMixedConductivePanelPatch
    {private static bool Prefix(ContactConductivePipeBridge.Instance __instance,float __0)=>!ColonyPipeSpecialEdges.Conductive(__instance,__0);}
    [HarmonyPatch(typeof(MorbRoverMaker.Instance),"Flow")]
    internal static class ColonyMixedMorbMakerPatch
    {private static bool Prefix(MorbRoverMaker.Instance __instance,float __0)=>!ColonyPipeSpecialEdges.Morb(__instance,__0);}
    [HarmonyPatch(typeof(StructureTemperatureComponents),nameof(StructureTemperatureComponents.ProduceEnergy))]
    internal static class ColonyMixedPanelEnergyPatch
    {private static void Prefix(HandleVector<int>.Handle __0,float __1)=>ColonyPipeSpecialEdges.ObserveBodyEnergy(__0,__1);}
}
