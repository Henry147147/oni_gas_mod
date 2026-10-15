using System.Buffers.Binary;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using HarmonyLib;
using KSerialization;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;
using MixedAtmospheres.Oni;

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length != 1) throw new ArgumentException("Pass the locally licensed ONI root.");
        var managed = Path.Combine(Path.GetFullPath(args[0]), "OxygenNotIncluded_Data", "Managed");
        AssemblyLoadContext.Default.Resolving += (_, name) =>
        { var path = Path.Combine(managed, name.Name + ".dll"); return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null; };
        return Run();
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static int Run()
    {
        var tests = new (string, System.Action)[]
        {
            ("only the complete empty serialized marker is unassigned", MarkerDefault),
            ("assigned marker retains canonical full allocation and native identities", MarkerExact),
            ("partial malformed noncanonical and unsupported marker records reject", MarkerMalformed),
            ("marker matching rejects generation allocation and cross-colony reuse", MarkerHandleReuse),
            ("native serialization template contains only seven private primitive fields", SerializationContract),
            ("native directory and type lookup roundtrip the inert marker", SerializeDefault),
            ("native serializer retains explicit private fixture identities and Int64 generation", SerializeAssigned),
            ("invalid serialized marker data remains invalid without repair", SerializeInvalid),
            ("local native marker mapping preserves an unrelated global load directory", SerializeLocalMapping),
            ("two default-marker owners prove geometry without claiming a manifest", GeometryDefault),
            ("all supported orientations match independent and pinned native offset transforms", Orientations),
            ("distinct adjacent anchors with intersecting full footprints reject", Overlap),
            ("offset validation rejects row wrap bounds and integer overflow", OffsetBounds),
            ("footprint validates every native and Core world cell", WorldCells),
            ("building placement cache omissions duplicates and stale coordinates reject", BuildingCache),
            ("occupancy caches and layers are validated without requiring an unused cache", OccupyCache),
            ("every native building-layer occupant must be the exact owner and nonsolid", Occupants),
            ("both utility caches connections and physical gas pipes must agree", Ports),
            ("required role duplicates and spawn failures reject while enabled is diagnostic", Components),
            ("definition lifecycle native packets capacity and construction contract reject changes", IdentityContract),
            ("global saved IDs resolve to exact owners without creating or repairing a tracker", TrackerIdentity),
            ("observation and validated-pair collections are owned immutable copies", Immutable),
            ("assigned manifest validates exact allocations and retains empty unbound rows", ManifestExact),
            ("default markers never satisfy assigned-manifest validation", ManifestRequiresMarkers),
            ("manifest saved IDs tokens and full handles must be unique", ManifestDuplicates),
            ("assigned marker native colony token generation and allocation mismatches reject", ManifestMismatch),
            ("equivalent restored allocations cannot replace the exact completed source", AllocationIdentity),
            ("world metadata and every nonempty unbound allocation remain outside eligibility", DomainCoverage),
            ("native capture guards its installation thread before any Unity access", CaptureThread),
            ("licensed native capture reads caches and components without repair or fluid calls", CaptureContract),
            ("prefab installation targets only completed GasReservoir and never assigns marker fields", PrefabContract)
        };
        var passed = 0;
        foreach (var (name, test) in tests)
        {
            try { test(); ++passed; Console.WriteLine("PASS " + name); }
            catch (Exception e) { Console.WriteLine("FAIL " + name + "\n" + e); }
        }
        Console.WriteLine($"{passed}/{tests.Length} marker/footprint checks passed; no Unity game, native save, load or ownership activation executed.");
        return passed == tests.Length ? 0 : 1;
    }
    private static void Check(bool condition, string message = "Assertion failed.") { if (!condition) throw new InvalidOperationException(message); }
    private static void Reject(System.Action action)
    {
        try { action(); } catch (Exception e) when (e is ArgumentException || e is InvalidOperationException || e is NotSupportedException) { return; }
        throw new InvalidOperationException("Expected bounded validation rejection.");
    }
    private static OwnedReservoirMarkerRecord Parse(int version = 0, string? native = "", string? owner = "", string? core = "", string? id = "", long generation = 0, string? token = "")
        => OwnedReservoirMarkerRecord.Parse(version, native, owner, core, id, generation, token);
    private static OwnedReservoirMarkerRecord Assigned(Fixture f, FluidContainerHandle handle, Guid owner)
        => Parse(1, f.NativeColony.ToString("N"), owner.ToString("N"), handle.ColonyId.ToString("N"), handle.Id, handle.Generation, handle.AllocationToken.ToString("N"));
    private static void MarkerDefault()
    {
        Check(Parse().State == OwnedReservoirMarkerState.Unassigned);
        Check(Parse(native: null).State == OwnedReservoirMarkerState.Invalid);
        Check(Parse(id: "partial").State == OwnedReservoirMarkerState.Invalid);
        Check(Parse(generation: 1).State == OwnedReservoirMarkerState.Invalid);
    }
    private static void MarkerExact()
    {
        var f = new Fixture(); var record = Assigned(f, f.A, f.TokenA);
        Check(record.State == OwnedReservoirMarkerState.Assigned && record.Error == "" && record.Matches(f.A));
        Check(record.NativeColonyId == f.NativeColony && record.PersistentOwnerToken == f.TokenA && record.Generation == f.A.Generation);
    }
    private static void MarkerMalformed()
    {
        var f = new Fixture(); var n = f.NativeColony.ToString("N"); var t = f.TokenA.ToString("N"); var c = f.A.ColonyId.ToString("N"); var a = f.A.AllocationToken.ToString("N");
        foreach (var record in new[] {
            Parse(2,n,t,c,f.A.Id,1,a), Parse(-1,n,t,c,f.A.Id,1,a), Parse(1,n.ToUpperInvariant(),t,c,f.A.Id,1,a),
            Parse(1,f.NativeColony.ToString("D"),t,c,f.A.Id,1,a), Parse(1,Guid.Empty.ToString("N"),t,c,f.A.Id,1,a),
            Parse(1,n,null,c,f.A.Id,1,a), Parse(1,n,t,c,"",1,a), Parse(1,n,t,c,"x\n",1,a),
            Parse(1,n,t,c,"\ud800",1,a), Parse(1,n,t,c,new string('x',257),1,a), Parse(1,n,t,c,f.A.Id,0,a) })
            Check(record.State == OwnedReservoirMarkerState.Invalid && record.Error.Length > 0);
    }
    private static void MarkerHandleReuse()
    {
        var f = new Fixture(); var record = Assigned(f, f.A, f.TokenA); Check(!record.Matches(f.B));
        var foreign = new Fixture(); Check(!record.Matches(foreign.A));
        Check(!Parse(1,f.NativeColony.ToString("N"),f.TokenA.ToString("N"),f.A.ColonyId.ToString("N"),f.A.Id,f.A.Generation+1,f.A.AllocationToken.ToString("N")).Matches(f.A));
        Check(!Parse(1,f.NativeColony.ToString("N"),f.TokenA.ToString("N"),f.A.ColonyId.ToString("N"),f.A.Id,f.A.Generation,Guid.NewGuid().ToString("N")).Matches(f.A));
    }
    private static void SerializationContract()
    {
        Manager.Initialize(); Manager.Clear(); var template = Manager.GetSerializationTemplate(typeof(OwnedReservoirOwnerMarker));
        Check(template.serializableFields.Count == 7 && template.serializableProperties.Count == 0);
        Check(template.serializableFields.All(row => row.field.IsPrivate && new[] { typeof(string), typeof(int), typeof(long) }.Contains(row.field.FieldType)));
        Check(template.onSerializing == null && template.onSerialized == null && template.customSerialize == null);
        Check(!typeof(OwnedReservoirOwnerMarker).IsDefined(typeof(SkipSaveFileSerialization), false));
        Check(typeof(OwnedReservoirOwnerMarker).GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly).Select(m=>m.Name).SequenceEqual(new[]{"ReadRecord"}));
    }
    private static OwnedReservoirOwnerMarker Marker(params (string field, object value)[] fields)
    {
        // Only this private test supplies fields. No production setter exists.
        var marker = (OwnedReservoirOwnerMarker)RuntimeHelpers.GetUninitializedObject(typeof(OwnedReservoirOwnerMarker));
        foreach (var field in typeof(OwnedReservoirOwnerMarker).GetFields(BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            field.SetValue(marker, field.FieldType == typeof(string) ? "" : field.FieldType == typeof(long) ? (object)0L : (object)0);
        foreach (var (name,value) in fields) typeof(OwnedReservoirOwnerMarker).GetField(name,BindingFlags.Instance|BindingFlags.NonPublic)!.SetValue(marker,value);
        return marker;
    }
    private static OwnedReservoirOwnerMarker Roundtrip(OwnedReservoirOwnerMarker marker)
    {
        Manager.Initialize(); Manager.Clear();
        using var data = new MemoryStream(); using (var writer = new BinaryWriter(data,System.Text.Encoding.UTF8,true)) Serializer.SerializeTypeless(marker,writer);
        using var directory = new MemoryStream(); using (var writer = new BinaryWriter(directory,System.Text.Encoding.UTF8,true)) Manager.SerializeDirectory(writer);
        Manager.Clear(); Manager.Initialize(); var directoryReader = new FastReader(directory.ToArray()); Manager.DeserializeDirectory(directoryReader);
        Check(directoryReader.Position == directory.Length && Manager.GetType(typeof(OwnedReservoirOwnerMarker).ToString()) == typeof(OwnedReservoirOwnerMarker));
        var copy = Marker(); var reader = new FastReader(data.ToArray()); Check(Deserializer.DeserializeTypeless(copy,reader)); Check(reader.Position == data.Length);
        return copy;
    }
    private static void SerializeDefault() => Check(Roundtrip(Marker()).ReadRecord().State == OwnedReservoirMarkerState.Unassigned);
    private static void SerializeAssigned()
    {
        var f = new Fixture(); const long generation = 1L << 40;
        var marker = Marker(("schemaVersion",1),("nativeColonyId",f.NativeColony.ToString("N")),("persistentOwnerToken",f.TokenA.ToString("N")),
            ("coreColonyId",f.A.ColonyId.ToString("N")),("containerId",f.A.Id),("generation",generation),("allocationToken",f.A.AllocationToken.ToString("N")));
        var record = Roundtrip(marker).ReadRecord();
        Check(record.State == OwnedReservoirMarkerState.Assigned && record.Generation == generation && record.AllocationToken == f.A.AllocationToken && record.PersistentOwnerToken == f.TokenA);
    }
    private static void SerializeInvalid() => Check(Roundtrip(Marker(("schemaVersion",91),("containerId","broken"))).ReadRecord().State == OwnedReservoirMarkerState.Invalid);
    [SerializationConfig(MemberSerialization.OptIn)]
    private sealed class SerializationSentinel { [Serialize] public string Value = "unrelated-native-load-map"; }
    private static void SerializeLocalMapping()
    {
        Manager.Initialize(); Manager.Clear(); var sentinel=new SerializationSentinel();
        using var sentinelData=new MemoryStream(); using(var w=new BinaryWriter(sentinelData,System.Text.Encoding.UTF8,true))Serializer.SerializeTypeless(sentinel,w);
        using var directory=new MemoryStream(); using(var w=new BinaryWriter(directory,System.Text.Encoding.UTF8,true))Manager.SerializeDirectory(w);
        Manager.Clear();Manager.Initialize();Manager.DeserializeDirectory(new FastReader(directory.ToArray()));
        var retained=Manager.GetDeserializationMapping(typeof(SerializationSentinel));
        // The live proof can use this local mapping without clearing/replacing
        // the game's global directory or assuming its current save schema.
        var source=Marker();var target=Marker();var template=Manager.GetSerializationTemplate(typeof(OwnedReservoirOwnerMarker));
        using var templateData=new MemoryStream();using(var w=new BinaryWriter(templateData,System.Text.Encoding.UTF8,true))template.SerializeTemplate(w);
        var inputTemplate=new DeserializationTemplate(typeof(OwnedReservoirOwnerMarker).ToString(),new FastReader(templateData.ToArray()));
        var local=new DeserializationMapping(inputTemplate,template);
        using var data=new MemoryStream();using(var w=new BinaryWriter(data,System.Text.Encoding.UTF8,true))Serializer.SerializeTypeless(source,w);
        var reader=new FastReader(data.ToArray());Check(local.Deserialize(target,reader)&&reader.Position==data.Length&&target.ReadRecord().State==OwnedReservoirMarkerState.Unassigned);
        Check(ReferenceEquals(retained,Manager.GetDeserializationMapping(typeof(SerializationSentinel))));
        var copy=new SerializationSentinel{Value="changed"};Check(Deserializer.DeserializeTypeless(copy,new FastReader(sentinelData.ToArray()))&&copy.Value==sentinel.Value);
    }
    private static OwnedReservoirValidatedFootprints Validate(OwnedReservoirFootprintObservation a, OwnedReservoirFootprintObservation b)
        => OwnedReservoirFootprintValidation.ValidatePair(new[]{a,b});
    private static void GeometryDefault()
    {
        var f = new Fixture(); var before=f.State.Save(); var proof = Validate(f.Observe(false),f.Observe(true));
        Check(!proof.ManifestValidated && proof.Owners.Count == 2 && proof.Owners[0].Marker.State == OwnedReservoirMarkerState.Unassigned);
        Check(proof.Owners[0].Identity.SavedInstanceId == 0 && f.State.Save().SequenceEqual(before));
    }
    private static void Orientations()
    {
        var f=new Fixture();
        foreach(var orientation in new[]{0,1,2,3,5,6})
        {
            var a=f.Observe(false,orientation:orientation); Validate(a,f.Observe(true));
            foreach(var offset in OwnedReservoirFootprintValidation.PinnedOffsets())
            {
                var native=Rotatable.GetRotatedCellOffset(new CellOffset(offset.X,offset.Y),(Orientation)orientation);
                var pure=OwnedReservoirFootprintValidation.Rotate(offset,orientation);
                Check(native.x==pure.X && native.y==pure.Y);
            }
        }
        Reject(()=>OwnedReservoirFootprintValidation.Rotate(new OwnedReservoirOffset(1,2),4));
    }
    private static void Overlap()
    {
        var f=new Fixture(); var a=f.Observe(false); var b=f.Observe(true,anchor:f.AnchorA+1);
        Check(a.Identity.AnchorCell!=b.Identity.AnchorCell); Reject(()=>Validate(a,b));
    }
    private static void OffsetBounds()
    {
        var f=new Fixture(); var s=f.State.CaptureSnapshot();
        foreach(var offset in new[]{new OwnedReservoirOffset(-1,0),new OwnedReservoirOffset(int.MinValue,0),new OwnedReservoirOffset(int.MaxValue,int.MaxValue)})
            Reject(()=>OwnedReservoirFootprintValidation.CellAt(s,0,offset));
        Reject(()=>OwnedReservoirFootprintValidation.CellAt(s,s.Width-1,new OwnedReservoirOffset(1,0)));
        Reject(()=>OwnedReservoirFootprintValidation.CellAt(s,s.Width*s.Height-1,new OwnedReservoirOffset(0,1)));
        Reject(()=>OwnedReservoirFootprintValidation.CellAt(s,-1,new OwnedReservoirOffset()));
        Reject(()=>f.Observe(false,anchor:0));
    }
    private static void WorldCells()
    {
        var f=new Fixture(); var a=f.Observe(false); var cells=a.Cells.ToArray(); var c=cells[7];
        cells[7]=new(c.Cell,99,c.CoreWorldId,false,c.BuildingOccupant); Reject(()=>Validate(Copy(a,cells:cells),f.Observe(true)));
        cells[7]=new(c.Cell,c.NativeWorldId,99,false,c.BuildingOccupant); Reject(()=>Validate(Copy(a,cells:cells),f.Observe(true)));
        var other=new Fixture(); Reject(()=>Validate(a,other.Observe(true)));
    }
    private static void BuildingCache()
    {
        var f=new Fixture(); var a=f.Observe(false); var cells=a.BuildingCells!.ToArray();
        cells[0]=cells[1]; Reject(()=>Validate(Copy(a,building:cells),f.Observe(true)));
        cells=a.BuildingCells!.ToArray(); cells[0]++; Reject(()=>Validate(Copy(a,building:cells),f.Observe(true)));
        Reject(()=>Validate(Copy(a,clearBuilding:true),f.Observe(true)));
    }
    private static void OccupyCache()
    {
        var f=new Fixture(); var a=f.Observe(false); Validate(a,f.Observe(true));
        Validate(Copy(a,layers:new[]{(int)ObjectLayer.Building},occupied:a.BuildingCells),f.Observe(true));
        Reject(()=>Validate(Copy(a,layers:new[]{(int)ObjectLayer.Building}),f.Observe(true)));
        Reject(()=>Validate(Copy(a,layers:new[]{(int)ObjectLayer.Building},occupied:a.BuildingCells,applies:false),f.Observe(true)));
        Reject(()=>Validate(Copy(a,layers:new[]{(int)ObjectLayer.GasConduit},occupied:a.BuildingCells),f.Observe(true)));
        Reject(()=>Validate(Copy(a,layers:new[]{(int)ObjectLayer.Building,(int)ObjectLayer.Building},occupied:a.BuildingCells),f.Observe(true)));
        var offsets=a.OccupyRotatedOffsets.ToArray(); offsets[0]=offsets[1]; Reject(()=>Validate(Copy(a,rotated:offsets),f.Observe(true)));
    }
    private static void Occupants()
    {
        var f=new Fixture(); var a=f.Observe(false); var c=a.Cells[14];
        foreach(var occupant in new[]{OwnedReservoirOccupant.Absent,OwnedReservoirOccupant.Other})
        { var cells=a.Cells.ToArray(); cells[14]=new(c.Cell,7,7,false,occupant); Reject(()=>Validate(Copy(a,cells:cells),f.Observe(true))); }
        var solid=a.Cells.ToArray(); solid[14]=new(c.Cell,7,7,true,OwnedReservoirOccupant.SameOwner); Reject(()=>Validate(Copy(a,cells:solid),f.Observe(true)));
    }
    private static void Ports()
    {
        var f=new Fixture(); var a=f.Observe(false); var p=a.Ports[0];
        foreach(var changed in new[]{ new OwnedReservoirFootprintPort(true,p.Cell,p.CachedCell+1,p.ConduitType,p.ConnectionOccupant,false),
            new(true,p.Cell+1,p.CachedCell,p.ConduitType,p.ConnectionOccupant,false),new(true,p.Cell,p.CachedCell,(int)ConduitType.Liquid,p.ConnectionOccupant,false),
            new(true,p.Cell,p.CachedCell,p.ConduitType,OwnedReservoirOccupant.Other,false),new(true,p.Cell,p.CachedCell,p.ConduitType,p.ConnectionOccupant,true) })
            Reject(()=>Validate(Copy(a,ports:new[]{changed,a.Ports[1]}),f.Observe(true)));
        Reject(()=>Validate(Copy(a,ports:new[]{a.Ports[0],a.Ports[0]}),f.Observe(true)));
    }
    private static void Components()
    {
        var f=new Fixture(); var a=f.Observe(false); var roles=a.Components.ToArray();
        foreach(var changed in new[]{new OwnedReservoirFootprintComponent("building",2,true,true),new("building",1,false,true)})
        { roles=a.Components.Where(r=>r.Role!="building").Append(changed).ToArray(); Reject(()=>Validate(Copy(a,components:roles),f.Observe(true))); }
        roles=a.Components.Select(r=>new OwnedReservoirFootprintComponent(r.Role,1,true,false)).ToArray(); Validate(Copy(a,components:roles),f.Observe(true));
        Reject(()=>Validate(Copy(a,components:a.Components.Skip(1).ToArray()),f.Observe(true)));
    }
    private static void IdentityContract()
    {
        var f=new Fixture(); var a=f.Observe(false); var id=a.Identity;
        foreach(var changed in new[]{Identity(id,savedId:-1),Identity(id,prefab:"Other"),Identity(id,width:6),Identity(id,layer:(int)ObjectLayer.GasConduit),
            Identity(id,mass:double.NaN),Identity(id,mass:400.125000001),Identity(id,capacity:999),Identity(id,active:false),Identity(id,pending:true),
            Identity(id,conflicted:true),Identity(id,end:true),Identity(id,empty:false),Identity(id,inputType:(int)ConduitType.Liquid)})
            Reject(()=>Validate(Copy(a,identity:changed),f.Observe(true)));
    }
    private static void Immutable()
    {
        var f=new Fixture(); var a=f.Observe(false); var cells=a.Cells.ToArray(); var buildings=a.BuildingCells!.ToArray();
        var copy=Copy(a,cells:cells,building:buildings); cells[0]=cells[1]; buildings[0]=buildings[1];
        var proof=Validate(copy,f.Observe(true)); Check(copy.Cells[0].Cell!=copy.Cells[1].Cell);
        Reject(()=>((IList<OwnedReservoirFootprintCell>)copy.Cells)[0]=copy.Cells[1]);
        Reject(()=>((IList<OwnedReservoirFootprintObservation>)proof.Owners)[0]=proof.Owners[1]);
    }
    private static void TrackerIdentity()
    {
        var f = new Fixture(); var a = f.Observe(false);
        Reject(() => Validate(Copy(a, identity: Identity(a.Identity, trackerMatches: false)), f.Observe(true)));
        var singleton = typeof(KPrefabIDTracker).GetField("Instance", BindingFlags.Static | BindingFlags.NonPublic)!;
        var previous = singleton.GetValue(null);
        var lookupMethod = typeof(OwnedReservoirFootprintCapture).GetMethod("RequireTrackedOwner", BindingFlags.Static | BindingFlags.NonPublic)!;
        var lookup = lookupMethod.CreateDelegate<Func<KPrefabID, int, KPrefabIDTracker>>();
        var owner = (KPrefabID)RuntimeHelpers.GetUninitializedObject(typeof(KPrefabID)); owner.InstanceID = 42;
        var other = (KPrefabID)RuntimeHelpers.GetUninitializedObject(typeof(KPrefabID)); other.InstanceID = 42;
        var tracker = new KPrefabIDTracker();
        var map = (Dictionary<int, KPrefabID>)typeof(KPrefabIDTracker).GetField("prefabIdMap", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(tracker)!;
        try
        {
            singleton.SetValue(null, null);
            Reject(() => lookup(owner, 42)); Check(singleton.GetValue(null) == null);
            singleton.SetValue(null, tracker);
            Reject(() => lookup(owner, 42)); Check(map.Count == 0);
            map.Add(42, owner); Check(ReferenceEquals(lookup(owner, 42), tracker));
            map[42] = other;
            Check(!owner.conflicted && !other.conflicted && owner.InstanceID == other.InstanceID);
            Reject(() => lookup(owner, 42)); Check(ReferenceEquals(map[42], other));
            map[42] = owner; owner.InstanceID = 43;
            Reject(() => lookup(owner, 42)); Reject(() => lookup(owner, -1));
            Check(ReferenceEquals(map[42], owner) && !map.ContainsKey(43));
        }
        finally { singleton.SetValue(null, previous); }
        var lookupCalls = PatchProcessor.GetOriginalInstructions(lookupMethod).Where(i => i.operand is MethodBase)
            .Select(i => (MethodBase)i.operand).ToArray();
        Check(lookupCalls.Any(m => m.DeclaringType == typeof(KPrefabIDTracker) && m.Name == "GetInstance"));
        Check(!lookupCalls.Any(m => m.DeclaringType == typeof(KPrefabIDTracker) && m.Name is "Get" or "Register" or "Unregister" or "DestroyInstance"));
        var capture = typeof(OwnedReservoirFootprintCapture).GetMethod("Capture", BindingFlags.Static | BindingFlags.NonPublic)!;
        Check(PatchProcessor.GetOriginalInstructions(capture).Count(i => i.operand is MethodBase m && m.Name == "RequireTrackedOwner") == 2);
    }
    private static void ManifestExact()
    {
        var f=new Fixture(); var before=f.State.Save();
        var proof=OwnedReservoirFootprintValidation.ValidatePair(new[]{f.Observe(false,assigned:true),f.Observe(true,assigned:true)},f.Owners,f.NativeColony);
        Check(proof.ManifestValidated && proof.Owners[0].Source.Containers.Count==3 && before.SequenceEqual(f.State.Save()));
        Check(proof.Owners[0].Allocation!.Inventory.MassesKg["oxygen"]==1 && proof.Owners[0].Allocation!.Inventory.RadiationQuantity==1e-200);
        Check(OwnedReservoirFootprintValidation.ValidatePair(proof.Owners,f.Owners.Reverse().ToArray(),f.NativeColony).ManifestValidated);
    }
    private static void ManifestRequiresMarkers()
    {
        var f=new Fixture(); Reject(()=>OwnedReservoirFootprintValidation.ValidatePair(new[]{f.Observe(false),f.Observe(true)},f.Owners,f.NativeColony));
        Reject(()=>OwnedReservoirFootprintValidation.ValidatePair(new[]{f.Observe(false,assigned:true),f.Observe(true,assigned:true)},f.Owners));
    }
    private static void ManifestDuplicates()
    {
        var f=new Fixture(); var o=f.Owners; var pair=new[]{f.Observe(false,assigned:true),f.Observe(true,assigned:true)};
        foreach(var changed in new[]{f.Owner(f.B,0,f.AnchorB,f.TokenB),f.Owner(f.B,1,f.AnchorB,f.TokenA),f.Owner(f.A,1,f.AnchorB,f.TokenB)})
            Reject(()=>OwnedReservoirFootprintValidation.ValidatePair(pair,new[]{o[0],changed},f.NativeColony));
    }
    private static void ManifestMismatch()
    {
        var f=new Fixture(); var a=f.Observe(false,assigned:true); var h=f.A;
        foreach(var marker in new[]{Assigned(f,f.B,f.TokenA),Assigned(f,f.A,f.TokenB),
            Parse(1,Guid.NewGuid().ToString("N"),f.TokenA.ToString("N"),h.ColonyId.ToString("N"),h.Id,h.Generation,h.AllocationToken.ToString("N")),
            Parse(1,f.NativeColony.ToString("N"),f.TokenA.ToString("N"),h.ColonyId.ToString("N"),h.Id,h.Generation+1,h.AllocationToken.ToString("N"))})
            Reject(()=>OwnedReservoirFootprintValidation.ValidatePair(new[]{Copy(a,marker:marker),f.Observe(true,assigned:true)},f.Owners,f.NativeColony));
    }
    private static void AllocationIdentity()
    {
        var f=new Fixture(); var a=f.Observe(false,assigned:true); var restored=ColonyFluidState.Restore(f.State.Save(),f.Catalog).CaptureSnapshot().SampleContainer(f.A);
        Reject(()=>OwnedReservoirFootprintValidation.ValidatePair(new[]{Copy(a,allocation:restored),f.Observe(true,assigned:true)},f.Owners,f.NativeColony));
    }
    private static void DomainCoverage()
    {
        foreach(var world in new[]{false,true})
        {
            var f=new Fixture(); using(var tx=f.State.BeginTransaction())
            { tx.Inject(0,new FluidInventory(new Dictionary<string,double>(),0,diseaseCounts:new Dictionary<int,double>{{99,1e-200}}),"metadata-fixture"); if(!world)tx.TransferAllCellToContainer(0,f.Empty); tx.Commit(); }
            Reject(()=>OwnedReservoirFootprintValidation.ValidatePair(new[]{f.Observe(false,assigned:true),f.Observe(true,assigned:true)},f.Owners,f.NativeColony));
        }
    }
    private static void CaptureThread()
    {
        var f=new Fixture(); Reject(()=>OwnedReservoirFootprintCapture.Capture(null!,f.State.CaptureSnapshot()));
        var type=typeof(OwnedReservoirOwnerMarker); type.GetField("installationThread",BindingFlags.Static|BindingFlags.NonPublic)!.SetValue(null,Environment.CurrentManagedThreadId);
        type.GetField("<Installed>k__BackingField",BindingFlags.Static|BindingFlags.NonPublic)!.SetValue(null,true);
        try { Task.Run(()=>Reject(()=>OwnedReservoirFootprintCapture.Capture(null!,f.State.CaptureSnapshot()))).GetAwaiter().GetResult(); }
        finally { type.GetField("<Installed>k__BackingField",BindingFlags.Static|BindingFlags.NonPublic)!.SetValue(null,false); }
    }
    private static void CaptureContract()
    {
        var capture=typeof(OwnedReservoirFootprintCapture).GetMethod("Capture",BindingFlags.Static|BindingFlags.NonPublic)!;
        var calls=PatchProcessor.GetOriginalInstructions(capture).Where(i=>i.operand is MethodBase).Select(i=>((MethodBase)i.operand).Name).ToArray();
        foreach(var required in new[]{"RequireNativeThread","GetValue","GetOccupiedGridCells","SampleContainer","get_ApplyToCells","get_Orientation"})Check(calls.Contains(required),required);
        foreach(var forbidden in new[]{"get_PlacementCells","RefreshCells","get_OccupiedCellsOffsets","UpdateOccupiedArea","SetPosition","ReplaceElement","Store","Save","LoadWorld"})Check(!calls.Contains(forbidden),forbidden);
        foreach(var (type,name) in new[]{(typeof(Building),"placementCells"),(typeof(ConduitConsumer),"utilityCell"),(typeof(ConduitDispenser),"utilityCell")})
            Check(type.GetField(name,BindingFlags.Instance|BindingFlags.NonPublic)!=null,name);
    }
    private static void PrefabContract()
    {
        var target=AccessTools.Method(typeof(GasReservoirConfig),nameof(GasReservoirConfig.DoPostConfigureComplete),new[]{typeof(UnityEngine.GameObject)});
        Check(target!=null && target.ReturnType==typeof(void));
        var hook=typeof(OwnedReservoirOwnerMarker).GetMethod("AddToCompletePrefab",BindingFlags.Static|BindingFlags.NonPublic)!;
        var il=PatchProcessor.GetOriginalInstructions(hook); Check(il.Any(i=>i.operand is MethodInfo m && m.Name=="AddComponent"));
        Check(!il.Any(i=>i.operand is FieldInfo f && !f.IsStatic && f.DeclaringType==typeof(OwnedReservoirOwnerMarker) && i.opcode==System.Reflection.Emit.OpCodes.Stfld));
    }
    private static OwnedReservoirFootprintObservation Copy(OwnedReservoirFootprintObservation a, OwnedReservoirFootprintIdentity? identity=null,
        OwnedReservoirMarkerRecord? marker=null,IReadOnlyList<OwnedReservoirFootprintCell>? cells=null,IReadOnlyList<int>? building=null,bool clearBuilding=false,
        IReadOnlyList<OwnedReservoirOffset>? rotated=null,IReadOnlyList<int>? occupied=null,IReadOnlyList<int>? layers=null,bool? applies=null,
        IReadOnlyList<OwnedReservoirFootprintPort>? ports=null,IReadOnlyList<OwnedReservoirFootprintComponent>? components=null,FluidContainerSnapshot? allocation=null)
        => new(a.Source,allocation??a.Allocation,identity??a.Identity,marker??a.Marker,a.DefinitionOffsets,clearBuilding?null:building??a.BuildingCells,
            a.OccupyUnrotatedOffsets,rotated??a.OccupyRotatedOffsets,occupied??a.OccupyCachedCells,layers??a.OccupyLayers,applies??a.OccupyAppliesToCells,cells??a.Cells,ports??a.Ports,components??a.Components);
    private static OwnedReservoirFootprintIdentity Identity(OwnedReservoirFootprintIdentity a,int? savedId=null,string? prefab=null,int? width=null,int? layer=null,
        double? mass=null,double? capacity=null,bool? active=null,bool? pending=null,bool? conflicted=null,bool? end=null,bool? empty=null,int? inputType=null,
        bool? trackerMatches=null)
        =>new(a.NativeColonyId,savedId??a.SavedInstanceId,prefab??a.PrefabId,a.DefinitionPrefabId,a.AnchorCell,a.WorldId,a.Orientation,width??a.DefinitionWidth,a.DefinitionHeight,
            layer??a.ObjectLayer,a.ConstructionElementId,mass??a.ConstructionMassKg,capacity??a.CapacityKg,active??a.Active,pending??a.PendingDestruction,conflicted??a.Conflicted,
            end??a.EndOfLife,empty??a.NativeItemsEmpty,a.InputOffset,a.OutputOffset,inputType??a.DefinitionInputConduitType,a.DefinitionOutputConduitType,
            trackerMatches??a.GlobalTrackerMatches);

    private sealed class Fixture
    {
        internal readonly Guid NativeColony=Guid.ParseExact("123456789abcdef0123456789abcdef0","N"),TokenA=Guid.NewGuid(),TokenB=Guid.NewGuid();
        internal readonly FluidMaterialCatalog Catalog=new("marker-footprint-fixture-v1",new[]{new FluidMaterial("oxygen",NativeMatterPhase.Gas,.032,658)});
        internal readonly ColonyFluidState State;
        internal readonly FluidContainerHandle A,B,Empty;
        internal int AnchorA=>8+8*40;
        internal int AnchorB=>24+8*40;
        internal OwnedReservoirCheckpointOwner[] Owners=>new[]{Owner(A,0,AnchorA,TokenA),Owner(B,1,AnchorB,TokenB)};
        internal Fixture()
        {
            const int width=42,height=26,count=width*height; var raw=new byte[29+36*count]; System.Text.Encoding.ASCII.GetBytes("SIMSAVE\0").CopyTo(raw,0);
            void Int(int offset,int value)=>BinaryPrimitives.WriteInt32LittleEndian(raw.AsSpan(offset),value);
            Int(8,15);Int(12,width);Int(16,height);for(var i=0;i<count;++i){Int(29+16*i,1);Int(29+24*count+12*i,1);}
            var framed=new byte[raw.Length+4];BinaryPrimitives.WriteInt32LittleEndian(framed,raw.Length);raw.CopyTo(framed,4);
            State=ColonyFluidState.Import(NativeColonyImport.StageNewColony(40,24,new[]{new NativeWorldPlacement(7,40,24,0,0)},new[]{new NativeWorldSource(7,framed)},
                new Dictionary<int,NativeMatterPhase>{{1,NativeMatterPhase.Vacuum}},new Dictionary<int,NativeFluidBinding>(),1),Catalog);
            using var tx=State.BeginTransaction();A=tx.CreateContainer("marker:a",new FluidContainerDefinition(10,1000,FluidContainerPhases.Gas));
            B=tx.CreateContainer("marker:b",new FluidContainerDefinition(20,1000,FluidContainerPhases.Gas));Empty=tx.CreateContainer("empty:unbound",new FluidContainerDefinition(1,1,FluidContainerPhases.Gas));
            tx.Inject(0,Catalog.CreateInventory(new Dictionary<string,double>{{"oxygen",1}},293.15,radiationQuantity:1e-200),"explicit-fixture");tx.TransferAllCellToContainer(0,A);tx.Commit();
        }
        internal OwnedReservoirCheckpointOwner Owner(FluidContainerHandle handle,int id,int anchor,Guid token)
            =>new(id,"GasReservoir",token,handle,7,anchor,0,4,400.125,State.CaptureSnapshot().SampleContainer(handle).Definition.VolumeCubicMetres);
        internal OwnedReservoirFootprintObservation Observe(bool second,int orientation=0,int? anchor=null,bool assigned=false)
        {
            var source=State.CaptureSnapshot();var handle=second?B:A;var location=anchor??(second?AnchorB:AnchorA);var ownerToken=second?TokenB:TokenA;
            OwnedReservoirOffset Transform(int x,int y)=>orientation switch{0=>new(x,y),1=>new(y,-x),2=>new(-x,-y),3=>new(-y,x),5=>new(-x,y),6=>new(x,-y),_=>throw new InvalidOperationException()};
            int Cell(OwnedReservoirOffset o)
            { var x=location%40+o.X;var y=location/40+o.Y;if(x<0||x>=40||y<0||y>=24)throw new InvalidOperationException();return x+40*y; }
            var shape=new List<OwnedReservoirOffset>();var rotated=new List<OwnedReservoirOffset>();var cells=new List<OwnedReservoirFootprintCell>();
            for(var y=0;y<3;++y)for(var x=-2;x<=2;++x){shape.Add(new(x,y));rotated.Add(Transform(x,y));cells.Add(new(Cell(Transform(x,y)),7,7,false,OwnedReservoirOccupant.SameOwner));}
            var roles=new List<OwnedReservoirFootprintComponent>();foreach(var role in new[]{"storage","prefab","construction","building","occupy-area","reservoir","smart","consumer","dispenser","marker"})roles.Add(new(role,1,true,true));
            if(orientation!=0)roles.Add(new("orientation",1,true,true));
            var identity=new OwnedReservoirFootprintIdentity(NativeColony,second?1:0,"GasReservoir","GasReservoir",location,7,orientation,5,3,(int)ObjectLayer.Building,4,400.125,1000,true,false,false,false,true,new(1,2),new(0,0),globalTrackerMatches:true);
            var input=Cell(Transform(1,2));var output=Cell(Transform(0,0));
            return new(source,source.SampleContainer(handle),identity,assigned?Assigned(this,handle,ownerToken):Parse(),shape,cells.Select(c=>c.Cell).ToArray(),shape,rotated,null,Array.Empty<int>(),true,cells,
                new[]{new OwnedReservoirFootprintPort(true,input,input,(int)ConduitType.Gas,OwnedReservoirOccupant.SameOwner,false),new(false,output,output,(int)ConduitType.Gas,OwnedReservoirOccupant.SameOwner,false)},roles);
        }
    }
}
