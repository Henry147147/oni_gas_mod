using System;
using System.IO;
using System.Linq;
using UnityEngine;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyOwnedStorage
    {
        private static long storageHeatTraceSequence;
        private static string ExactHeatTraceAmount(PipeAmount value)
        {
            using var bytes=new MemoryStream();using(var writer=new BinaryWriter(bytes))value.Write(writer);
            return Convert.ToBase64String(bytes.ToArray());
        }
        // Default-off observations only: no native message, publication,
        // physical inventory readback or replacement credit is introduced.
        private static void TraceHeat(string reason,Host host,Projection? view=null,float? targetTemperature=null,float? targetCapacity=null)
        {
            var path=Environment.GetEnvironmentVariable("MA_ONI_STORAGE_HEAT_TRACE");if(string.IsNullOrEmpty(path))return;
            var frames=ColonySaveBarrier.NativeFrameCounters();
            object? projection=null;
            if(view!=null)
            {
                var item=view.Item;
                projection=new{Element=(int)item.Element.idx,ElementHash=(int)item.ElementID,Mass=item.Mass,NativeHandle=view.Handle,
                    CapacityBits=BitConverter.SingleToInt32Bits(view.Capacity),BaselineBits=BitConverter.SingleToInt32Bits(view.Temperature),
                    PublishedBits=BitConverter.SingleToInt32Bits(item.Temperature),GameCvBits=BitConverter.SingleToInt32Bits(item.Element.specificHeatCapacity),
                    OwnedCv=ColonyConduits.Material(item.Element.idx).HeatCapacity,
                    TargetTemperatureBits=targetTemperature.HasValue?(int?)BitConverter.SingleToInt32Bits(targetTemperature.Value):null,
                    TargetCapacityBits=targetCapacity.HasValue?(int?)BitConverter.SingleToInt32Bits(targetCapacity.Value):null};
            }
            File.AppendAllText(path,ReportJson.SerializeLine(new{Kind=reason,Sequence=++storageHeatTraceSequence,UnityFrame=Time.frameCount,
                OwnerFrame=ColonyOwner.Status.CompletedFrames,NativeStarted=frames.Started,NativeEnded=frames.Ended,
                Prefab=host.Store.Address.Prefab,Revision=host.Store.Revision,ProjectedRevision=host.ProjectedRevision,
                Mass=host.Store.Inventory.Mass,Temperature=host.Store.Inventory.Count==0?0:host.Store.Inventory.Temperature(ColonyConduits.Material),
                Energy=host.Store.Inventory.Energy.Value,ExactEnergy=ExactHeatTraceAmount(host.Store.Inventory.Energy),
                Heat=host.Store.Heat.Value,ExactHeatUnits=host.Store.Heat.Units.ToString(System.Globalization.CultureInfo.InvariantCulture),Projection=projection})+"\n");
        }
        private static void TraceNativeHeatReceipt(Host host,NativeStorageHeatReceipt receipt)
        {
            var path=Environment.GetEnvironmentVariable("MA_ONI_STORAGE_HEAT_TRACE");if(string.IsNullOrEmpty(path))return;
            File.AppendAllText(path,ReportJson.SerializeLine(new{Kind="native-receipt",Sequence=++storageHeatTraceSequence,UnityFrame=Time.frameCount,
                OwnerFrame=ColonyOwner.Status.CompletedFrames,Prefab=host.Store.Address.Prefab,Revision=host.Store.Revision,
                Mass=host.Store.Inventory.Mass,ReceiptSequence=receipt.Sequence,NativeFrame=receipt.Frame,receipt.Handle,ReceiptKind=receipt.Kind,
                CapacityBits=BitConverter.SingleToInt32Bits(receipt.Capacity),BeforeBits=BitConverter.SingleToInt32Bits(receipt.Before),AfterBits=BitConverter.SingleToInt32Bits(receipt.After)})+"\n");
        }
        internal static void TraceHeatEndpoint(Storage storage,string endpoint,ulong session,ulong frame,double worldU,double worldK,double worldG,
            PipeAmount construction,PipeAmount total)
        {
            var path=Environment.GetEnvironmentVariable("MA_ONI_STORAGE_HEAT_TRACE");if(string.IsNullOrEmpty(path))return;
            if(!Hosts.TryGetValue(storage,out var host))throw new InvalidOperationException("A traced heat endpoint has no exact store.");
            TraceHeat("endpoint-"+endpoint,host);
            foreach(var view in host.Projections.Values)TraceHeat("endpoint-"+endpoint+"-projection",host,view);
            File.AppendAllText(path,ReportJson.SerializeLine(new{Kind="endpoint-energy",Endpoint=endpoint,Sequence=++storageHeatTraceSequence,
                Session=session,Frame=frame,WorldU=worldU,WorldK=worldK,WorldG=worldG,Construction=construction.Value,
                ExactConstruction=ExactHeatTraceAmount(construction),Store=host.Store.Inventory.Energy.Value,
                ExactStore=ExactHeatTraceAmount(host.Store.Inventory.Energy),Total=total.Value,ExactTotal=ExactHeatTraceAmount(total),
                Materials=host.Store.Inventory.Parts.ToArray().Select(part=>new{part.Id,Mass=part.Mass,ExactMass=ExactHeatTraceAmount(part.Amount),
                    OwnedCv=ColonyConduits.Material(part.Id).HeatCapacity,GameCv=ElementLoader.elements[part.Id].specificHeatCapacity,
                    Offset=ColonyConduits.Material(part.Id).EnergyOffset}).ToArray()})+"\n");
        }
    }
}
