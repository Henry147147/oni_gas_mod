using System.Collections.Generic;
using System.Globalization;

namespace MixedAtmospheres.Oni
{
    internal static class ColonyPipeInspection
    {
        internal static MixturePipeReading? Read(ConduitFlow flow,int cell,string label)
        {
            if(!ColonyConduits.TryRead(flow,cell,out var inventory))return null;
            var hover=new List<string>();var detail=new List<string>();
            if(inventory.Count==0)return new MixturePipeReading(label,new[]{"Empty"},new[]{"Empty"});
            var temperature=inventory.Temperature(ColonyConduits.Material);
            var state=GameUtil.GetFormattedTemperature((float)temperature)+" · "+Number(inventory.Mass)+" kg";
            hover.Add(state);detail.Add(state+" · "+temperature.ToString("R",CultureInfo.InvariantCulture)+" K");
            var traces=false;
            foreach(var part in inventory.Parts)
            {
                var element=ElementLoader.FindElementByHash(ColonyConduits.ElementHash(part.Id));
                var trace=!inventory.ContainsFraction(part.Id,ColonySensors.TraceFraction);
                var row=element.name+": "+Number(part.Mass)+" kg ("+Number(100*part.Mass/inventory.Mass)+"%)";
                detail.Add(row+(trace?" · trace":""));if(!trace)hover.Add(row);else traces=true;
            }
            if(traces)hover.Add("Traces · select for complete contents");
            foreach(var germ in inventory.Germs)
            {
                var name=germ.Id<byte.MaxValue?GameUtil.GetFormattedDiseaseName((byte)germ.Id,false):"Other germs";
                var row=name+": "+Number(germ.Mass)+" germs";hover.Add(row);detail.Add(row);
            }
            return new MixturePipeReading(label,hover,detail);
        }
        private static string Number(double value)=>value.ToString("G6",CultureInfo.InvariantCulture);
    }
}
