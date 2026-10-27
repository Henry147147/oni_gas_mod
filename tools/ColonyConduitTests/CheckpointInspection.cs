using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using MixedAtmospheres.Oni;

// Private saved-state diagnosis. Decode a copy through the production codec;
// never load the game or change the source checkpoint. Separate fingerprints
// localize a complete-image mismatch without exposing a whole private capture.
internal static class CheckpointInspection
{
    private static string Fingerprint(Action<BinaryWriter> write)
    {
        using var stream=new MemoryStream();
        using(var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true))write(writer);
        return Convert.ToHexStringLower(SHA256.HashData(stream.ToArray()));
    }
    private static string Amount(PipeAmount value)
    {
        using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream))value.Write(writer);
        return Convert.ToBase64String(stream.ToArray());
    }
    internal static void Print(string path)
    {
        var bytes=File.ReadAllBytes(path);
        using var reader=new BinaryReader(new MemoryStream(bytes));
        _=reader.ReadBytes(8);var schema=reader.ReadInt32();
        var geometry=PipeGeometry.Read(reader);var catalogue=reader.ReadBytes(32);
        var image=ColonyConduitCheckpoint.Decode(bytes,geometry,catalogue);
        var sections=new SortedDictionary<string,string>();
        sections["cells"]=Fingerprint(w=>{foreach(var cell in image.Cells){w.Write(cell.Phase);w.Write(cell.Cell);cell.Inventory.Write(w);}});
        sections["accounts"]=Fingerprint(w=>{
            image.Initial.Write(w);image.Supplied.Write(w);image.Removed.Write(w);image.ExternalHeat.Write(w);
            foreach(var germ in image.DiseaseExchange){w.Write(germ.Id);germ.Amount.Write(w);}
        });
        if(image.Boundary!=null)sections["boundary"]=Fingerprint(image.Boundary.Write);
        var runtime=image.Runtime;var stores=runtime?.OwnedStorage;var schedule=runtime?.NativeSchedule;
        if(runtime!=null)
        {
            // Only this decoded copy is edited; preserve all pre-schema5 state
            // while fingerprinting storage and scheduling independently.
            runtime.OwnedStorage=null;runtime.NativeSchedule=null;
            try{sections["runtime_without_storage_schedule"]=Fingerprint(w=>runtime.Write(w,geometry));}
            finally{runtime.OwnedStorage=stores;runtime.NativeSchedule=schedule;}
        }
        if(stores!=null)sections["storage"]=Fingerprint(stores.Write);
        if(schedule!=null)sections["native_schedule"]=Fingerprint(w=>schedule.Write(w,geometry));
        var details=stores?.Stores.Select(store=>new{
            store.Address.Prefab,store.Address.Ordinal,store.Phase,store.Revision,
            Hash=Fingerprint(store.Write),Mass=store.Inventory.Mass,Energy=store.Inventory.Energy.Value,ExactEnergy=Amount(store.Inventory.Energy),
            Heat=store.Heat.Value,ExactHeatUnits=store.Heat.Units.ToString(System.Globalization.CultureInfo.InvariantCulture),
            Parts=store.Inventory.Parts.ToArray().Select(part=>new{part.Id,part.Mass,ExactMass=Amount(part.Amount)}),
            Germs=store.Inventory.Germs.ToArray().Select(germ=>new{germ.Id,Count=germ.Mass,ExactCount=Amount(germ.Amount)}),
            Clocks=store.Clocks.ToDictionary(pair=>pair.Key,pair=>Fingerprint(pair.Value.Write)),
            Contexts=store.CultureContexts.ToDictionary(pair=>pair.Key,pair=>Fingerprint(pair.Value.Write))
        });
        var runtimeInfo=runtime==null?null:new{
            runtime.Contacts,ContactHeat=runtime.ContactHeat.Value,LaggedHeat=runtime.LaggedHeat.Value,
            runtime.Excluded,runtime.Mismatches,runtime.Unsettled,runtime.BiologyTicks,runtime.BiologyCells,runtime.BiologyIdentities,
            runtime.BiologyExpected,runtime.BiologySkipped,runtime.PhaseEvents,
            NativeFlows=schedule?.Flows.Select(flow=>new{flow.Phase,ElapsedBits=BitConverter.SingleToInt32Bits(flow.Elapsed),Cells=flow.Cells.Count,Updaters=flow.Updaters.Count})
        };
        var completed=stores?.Retired.Select(history=>new{history.Phase,Hash=Fingerprint(history.Write),AuditClosed=history.Audit(),
            ExactHeatUnits=history.Heat.Units.ToString(System.Globalization.CultureInfo.InvariantCulture),
            Initial=Fingerprint(history.Initial.Write),Admitted=Fingerprint(history.Admitted.Write),Withdrawn=Fingerprint(history.Withdrawn.Write),
            Births=history.Births.ToDictionary(pair=>pair.Key,pair=>pair.Value.Units.ToString(System.Globalization.CultureInfo.InvariantCulture)),
            Deaths=history.Deaths.ToDictionary(pair=>pair.Key,pair=>pair.Value.Units.ToString(System.Globalization.CultureInfo.InvariantCulture))});
        Console.WriteLine(JsonSerializer.Serialize(new{Schema=schema,Sha256=Convert.ToHexStringLower(SHA256.HashData(bytes)),Sections=sections,Stores=details,Completed=completed,Runtime=runtimeInfo},new JsonSerializerOptions{WriteIndented=true}));
    }
}
