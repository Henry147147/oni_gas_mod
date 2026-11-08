using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using HarmonyLib;

namespace MixedAtmospheres.Oni
{
    internal static class ColonyPipeInspection
    {
        internal static MixturePipeReading? Read(ConduitFlow flow,int cell,string label)
        {
            if(!ColonyConduits.TryRead(flow,cell,out var inventory))return null;
            return Describe(inventory,label,flow,cell);
        }
        internal static MixturePipeReading Describe(PipeInventory inventory,string label,ConduitFlow? flow=null,int cell=-1)
        {
            var hover=new List<string>();var detail=new List<string>();
            if(inventory.Count==0)return new MixturePipeReading(label,new[]{"Empty"},new[]{"Empty"},flow,cell);
            var temperature=inventory.Temperature(ColonyConduits.Material);
            var state=MixtureInspection.PacketTemperature(temperature)+" · "+MixtureInspection.PacketMass(inventory.Mass,inventory.Total.Sign>0);
            hover.Add(state);detail.Add(state+" · "+temperature.ToString("R",CultureInfo.InvariantCulture)+" K");
            var traces=false;
            var shares=new List<string>();
            foreach(var part in inventory.Parts)
            {
                var element=ElementLoader.FindElementByHash(ColonyConduits.ElementHash(part.Id));
                var trace=!inventory.ContainsFraction(part.Id,ColonySensors.TraceFraction);
                var share=MixtureInspection.MassPercent(part.Mass,inventory.Mass);
                var row=element.name+": "+MixtureInspection.PacketMass(part.Mass,part.Amount.Sign>0)+" ("+share+")";
                detail.Add(row+(trace?" · trace":""));
                if(!trace)shares.Add(element.name+" "+share);else traces=true;
            }
            foreach(var line in MixtureInspection.CompactLines(shares))hover.Add(line);
            hover.Add(traces?"Mass shares · traces present · select for all contents":"Mass shares · select for constituent masses");
            var germRows=new List<string>();
            foreach(var germ in inventory.Germs)
            {
                var name=germ.Id<byte.MaxValue?GameUtil.GetFormattedDiseaseName((byte)germ.Id,false):"Other germs";
                var row=name+": "+Number(germ.Mass)+" germs";germRows.Add(row);detail.Add(row);
            }
            foreach(var line in MixtureInspection.CompactLines(germRows))hover.Add(line);
            return new MixturePipeReading(label,hover,detail,flow,cell);
        }
        private static string Number(double value)=>MixtureInspection.Number(value);
        // Native Contents status text has always used one decimal for shares,
        // including exact development scenes. Keep that short status contract;
        // hover and selected composition retain their independent precision.
        internal static string StatusMassPercent(double mass,double total)
        {
            var percent=100*(mass/total);
            if(mass>0&&percent==0)return "below numeric range";
            var text=percent.ToString("0.#",CultureInfo.InvariantCulture);
            if(percent>0&&text=="0")text=percent.ToString("G6",CultureInfo.InvariantCulture);
            return (mass<total&&text=="100"?"<100":text)+"%";
        }
        // The pipe status line ("Contents: ...") on hover and in details. An
        // owned cell lists its non-trace species with mass shares instead of
        // the dominant element's projection.
        internal static string? ContentsLine(ConduitFlow flow,int cell)
        {
            if(!ColonyConduits.TryRead(flow,cell,out var inventory))return null;
            if(inventory.Count==0)return STRINGS.BUILDING.STATUSITEMS.PIPECONTENTS.EMPTY;
            var names=new List<string>();var traces=false;
            foreach(var part in inventory.Parts)
            {
                if(!inventory.ContainsFraction(part.Id,ColonySensors.TraceFraction)){traces=true;continue;}
                var element=ElementLoader.FindElementByHash(ColonyConduits.ElementHash(part.Id));
                names.Add(inventory.Count==1?element.name:element.name+" "+StatusMassPercent(part.Mass,inventory.Mass));
            }
            if(traces)names.Add("traces");
            var text=string.Format(STRINGS.BUILDING.STATUSITEMS.PIPECONTENTS.CONTENTS,MixtureInspection.PacketMass(inventory.Mass,inventory.Total.Sign>0),
                string.Join(", ",names),MixtureInspection.PacketTemperature(inventory.Temperature(ColonyConduits.Material)));
            if(OverlayScreen.Instance!=null&&OverlayScreen.Instance.mode==OverlayModes.Disease.ID)
            {
                var total=0.0;var dominant=-1;var largest=0.0;
                foreach(var germ in inventory.Germs){total+=germ.Mass;if(germ.Mass>largest&&germ.Id<byte.MaxValue){largest=germ.Mass;dominant=germ.Id;}}
                if(dominant>=0)text+=string.Format(STRINGS.BUILDING.STATUSITEMS.PIPECONTENTS.CONTENTS_WITH_DISEASE,GameUtil.GetFormattedDisease((byte)dominant,(int)Math.Min(int.MaxValue,Math.Floor(total)),true));
            }
            return text;
        }
    }
    [HarmonyPatch]
    internal static class ColonyMixedPipeStatusPatch
    {
        private static MethodBase? Target()
        {
            var method=AccessTools.Method(typeof(Database.BuildingStatusItems).GetNestedType("<>c",BindingFlags.NonPublic|BindingFlags.Public),"<CreateStatusItems>b__337_35");
            return method!=null&&method.ReturnType==typeof(string)&&method.GetParameters().Length==2?method:null;
        }
        private static bool Prepare()=>Target()!=null;
        private static MethodBase TargetMethod()=>Target()!;
        private static bool Prefix(string __0,object __1,ref string __result)
        {
            if(!ColonyConduits.Active||!(__1 is Conduit conduit))return true;
            try
            {
                var line=ColonyPipeInspection.ContentsLine(conduit.GetFlowManager(),Grid.PosToCell(conduit));
                // The native callback rewrites its string argument in place.
                // Resolve from the incoming template before that replacement
                // removes the token, rather than examining it in a postfix.
                if(line!=null){__result=__0.Replace("{Contents}",line);return false;}
            }
            catch(InvalidOperationException){} // keep the native line for an unrepresentable trace
            return true;
        }
    }
}
