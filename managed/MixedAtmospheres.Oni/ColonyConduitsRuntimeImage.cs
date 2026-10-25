using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    // MACPIP01 schema 3 runtime extension (docs/mixed-pipes.md#saved-runtime):
    // everything the pipe modules hold beyond schema 2's cells, accounts and
    // pending jobs. Components are named by KPrefabID instance id, never by a
    // live object or native handle. Every list and map is written in a
    // canonical order; decode re-encodes and must reproduce the bytes.
    internal sealed class PipeRuntimeImage
    {
        internal sealed class BufferRecord{internal ulong Id;internal byte Phase;internal int Cell;internal PipeInventory Inventory=PipeInventory.Empty;}
        internal sealed class PumpRecord{internal int Prefab;internal byte Phase;internal int Cell;internal ulong[] Buffers=Array.Empty<ulong>();internal (ulong Id,PipeAmount Mass)[] Pending=Array.Empty<(ulong,PipeAmount)>();}
        internal sealed class VentRecord
        {
            internal int Prefab;internal byte Phase;internal int Cell;internal ulong[] Buffers=Array.Empty<ulong>();internal ulong[] Pending=Array.Empty<ulong>();
            internal SortedDictionary<int,PipeAmount> Competition=new SortedDictionary<int,PipeAmount>(),External=new SortedDictionary<int,PipeAmount>();
            internal byte BodyDisease;internal int BodyCount;internal int Exchanges;
        }
        internal sealed class SpillRecord{internal ulong Key;internal int Cell;internal ulong NextFrame;internal ulong[] Ids=Array.Empty<ulong>();}
        internal sealed class CultureRecord{internal byte Phase;internal int Cell;internal (int Id,GermPopulationClock Clock)[] Cultures=Array.Empty<(int,GermPopulationClock)>();}

        internal PipeInventory FromWorld=PipeInventory.Empty,ToWorld=PipeInventory.Empty;
        internal SortedDictionary<ulong,PipeAmount> PassiveExchange=new SortedDictionary<ulong,PipeAmount>();
        internal List<BufferRecord> Buffers=new List<BufferRecord>();
        internal List<PumpRecord> Pumps=new List<PumpRecord>();internal long PumpNotifications,PumpQueued;
        internal List<VentRecord> Vents=new List<VentRecord>();
        internal List<SpillRecord> Spills=new List<SpillRecord>();internal long SpillAttempts;
        internal PipeInventory ToStorage=PipeInventory.Empty,FromStorage=PipeInventory.Empty,Dumped=PipeInventory.Empty;
        internal SortedDictionary<int,PipeAmount> BoundaryMass=new SortedDictionary<int,PipeAmount>(),BoundaryGerms=new SortedDictionary<int,PipeAmount>();
        internal PipeAmount BoundaryEnergy;
        internal SortedDictionary<int,PipeInventory> DumpedByCell=new SortedDictionary<int,PipeInventory>();
        internal long Consumed,Dispensed,Dumps,DamageEvents;
        internal long Contacts,Excluded,Mismatches,Unsettled;internal PipeAmount ContactHeat,LaggedHeat;
        internal List<CultureRecord> Cultures=new List<CultureRecord>();
        // Null is the legacy schema-3 image, whose native allocation/cache
        // state was not saved. An empty list explicitly records no pipes.
        internal List<PipeNativeCulture>? NativeCultures;
        internal SortedDictionary<int,PipeAmount> Births=new SortedDictionary<int,PipeAmount>(),Deaths=new SortedDictionary<int,PipeAmount>();
        internal long BiologyTicks,BiologyCells,BiologyIdentities,BiologyExpected,BiologySkipped,PhaseEvents;

        private const int MaximumRecords=1<<24;
        internal void Write(BinaryWriter w)
        {
            FromWorld.Write(w);ToWorld.Write(w);WriteMap(w,PassiveExchange,(x,k)=>x.Write(k));
            w.Write(Buffers.Count);foreach(var b in Buffers.OrderBy(b=>b.Id)){w.Write(b.Id);w.Write(b.Phase);w.Write(b.Cell);b.Inventory.Write(w);}
            w.Write(Pumps.Count);
            foreach(var p in Pumps.OrderBy(p=>p.Prefab))
            {
                w.Write(p.Prefab);w.Write(p.Phase);w.Write(p.Cell);WriteIds(w,p.Buffers);
                w.Write(p.Pending.Length);foreach(var item in p.Pending.OrderBy(i=>i.Id)){w.Write(item.Id);item.Mass.Write(w);}
            }
            w.Write(PumpNotifications);w.Write(PumpQueued);
            w.Write(Vents.Count);
            foreach(var v in Vents.OrderBy(v=>v.Prefab))
            {
                w.Write(v.Prefab);w.Write(v.Phase);w.Write(v.Cell);WriteIds(w,v.Buffers);WriteIds(w,v.Pending);
                WriteMap(w,v.Competition,(x,k)=>x.Write(k));WriteMap(w,v.External,(x,k)=>x.Write(k));w.Write(v.BodyDisease);w.Write(v.BodyCount);w.Write(v.Exchanges);
            }
            w.Write(Spills.Count);foreach(var s in Spills.OrderBy(s=>s.Key)){w.Write(s.Key);w.Write(s.Cell);w.Write(s.NextFrame);WriteIds(w,s.Ids);}
            w.Write(SpillAttempts);
            ToStorage.Write(w);FromStorage.Write(w);Dumped.Write(w);WriteMap(w,BoundaryMass,(x,k)=>x.Write(k));WriteMap(w,BoundaryGerms,(x,k)=>x.Write(k));BoundaryEnergy.Write(w);
            w.Write(DumpedByCell.Count);foreach(var item in DumpedByCell){w.Write(item.Key);item.Value.Write(w);}
            w.Write(Consumed);w.Write(Dispensed);w.Write(Dumps);w.Write(DamageEvents);
            w.Write(Contacts);w.Write(Excluded);w.Write(Mismatches);w.Write(Unsettled);ContactHeat.Write(w);LaggedHeat.Write(w);
            w.Write(Cultures.Count);
            foreach(var c in Cultures.OrderBy(c=>c.Phase).ThenBy(c=>c.Cell))
            {w.Write(c.Phase);w.Write(c.Cell);w.Write(c.Cultures.Length);foreach(var item in c.Cultures.OrderBy(i=>i.Id)){w.Write(item.Id);item.Clock.Write(w);}}
            WriteMap(w,Births,(x,k)=>x.Write(k));WriteMap(w,Deaths,(x,k)=>x.Write(k));
            w.Write(BiologyTicks);w.Write(BiologyCells);w.Write(BiologyIdentities);w.Write(BiologyExpected);w.Write(BiologySkipped);w.Write(PhaseEvents);
            if(NativeCultures!=null)
            {w.Write(NativeCultures.Count);foreach(var culture in NativeCultures.OrderBy(c=>c.Phase).ThenBy(c=>c.Cell))culture.Write(w);}
        }
        internal static PipeRuntimeImage Read(BinaryReader r,PipeGeometry geometry,int schema=3)
        {
            var image=new PipeRuntimeImage();
            image.FromWorld=PipeInventory.Read(r);image.ToWorld=PipeInventory.Read(r);image.PassiveExchange=ReadMap(r,x=>x.ReadUInt64());
            for(int i=0,n=Count(r);i<n;++i)
                image.Buffers.Add(new BufferRecord{Id=r.ReadUInt64(),Phase=Phase(r),Cell=Cell(r,geometry),Inventory=PipeInventory.Read(r)});
            for(int i=0,n=Count(r);i<n;++i)
            {
                var pump=new PumpRecord{Prefab=r.ReadInt32(),Phase=Phase(r),Cell=Cell(r,geometry),Buffers=ReadIds(r)};
                var pending=new (ulong,PipeAmount)[Count(r)];for(var k=0;k<pending.Length;++k)pending[k]=(r.ReadUInt64(),PipeAmount.Read(r));
                pump.Pending=pending;image.Pumps.Add(pump);
            }
            image.PumpNotifications=r.ReadInt64();image.PumpQueued=r.ReadInt64();
            for(int i=0,n=Count(r);i<n;++i)
                image.Vents.Add(new VentRecord{Prefab=r.ReadInt32(),Phase=Phase(r),Cell=Cell(r,geometry),Buffers=ReadIds(r),Pending=ReadIds(r),
                    Competition=ReadMap(r,x=>x.ReadInt32()),External=ReadMap(r,x=>x.ReadInt32()),BodyDisease=r.ReadByte(),BodyCount=r.ReadInt32(),Exchanges=r.ReadInt32()});
            for(int i=0,n=Count(r);i<n;++i)image.Spills.Add(new SpillRecord{Key=r.ReadUInt64(),Cell=Cell(r,geometry),NextFrame=r.ReadUInt64(),Ids=ReadIds(r)});
            image.SpillAttempts=r.ReadInt64();
            image.ToStorage=PipeInventory.Read(r);image.FromStorage=PipeInventory.Read(r);image.Dumped=PipeInventory.Read(r);
            image.BoundaryMass=ReadMap(r,x=>x.ReadInt32());image.BoundaryGerms=ReadMap(r,x=>x.ReadInt32());image.BoundaryEnergy=PipeAmount.Read(r);
            for(int i=0,n=Count(r);i<n;++i){var cell=r.ReadInt32();if(image.DumpedByCell.ContainsKey(cell))throw new InvalidDataException("Duplicate dump record.");image.DumpedByCell.Add(cell,PipeInventory.Read(r));}
            image.Consumed=r.ReadInt64();image.Dispensed=r.ReadInt64();image.Dumps=r.ReadInt64();image.DamageEvents=r.ReadInt64();
            image.Contacts=r.ReadInt64();image.Excluded=r.ReadInt64();image.Mismatches=r.ReadInt64();image.Unsettled=r.ReadInt64();
            image.ContactHeat=PipeAmount.Read(r);image.LaggedHeat=PipeAmount.Read(r);
            for(int i=0,n=Count(r);i<n;++i)
            {
                var record=new CultureRecord{Phase=Phase(r),Cell=Cell(r,geometry)};var cultures=new (int,GermPopulationClock)[Count(r)];
                for(var k=0;k<cultures.Length;++k)cultures[k]=(r.ReadInt32(),GermPopulationClock.Read(r));
                record.Cultures=cultures;image.Cultures.Add(record);
            }
            image.Births=ReadMap(r,x=>x.ReadInt32());image.Deaths=ReadMap(r,x=>x.ReadInt32());
            image.BiologyTicks=r.ReadInt64();image.BiologyCells=r.ReadInt64();image.BiologyIdentities=r.ReadInt64();image.BiologyExpected=r.ReadInt64();
            image.BiologySkipped=r.ReadInt64();image.PhaseEvents=r.ReadInt64();
            if(schema>=4)
            {
                var count=Count(r);
                if((ulong)count>2UL*geometry.Width*geometry.Height||(long)count*49>r.BaseStream.Length-r.BaseStream.Position)
                    throw new InvalidDataException("Invalid saved native pipe culture count.");
                image.NativeCultures=new List<PipeNativeCulture>(count);
                for(var i=0;i<count;++i)image.NativeCultures.Add(PipeNativeCulture.Read(r,geometry));
            }
            image.Validate();
            return image;
        }
        // Identity rules: unique buffers; every id a pump/vent/spill names is
        // a buffer or a pending job (checked against the boundary by the caller).
        internal void Validate()
        {
            Unique(Buffers.Select(b=>b.Id),"buffer");Unique(Pumps.Select(p=>(ulong)(uint)p.Prefab),"pump");Unique(Vents.Select(v=>(ulong)(uint)v.Prefab),"vent");
            Unique(Spills.Select(s=>s.Key),"spill");Unique(Cultures.Select(c=>((ulong)c.Phase<<32)|(uint)c.Cell),"culture cell");
            foreach(var b in Buffers)if(b.Id==0||b.Inventory.Count==0)throw new InvalidDataException("Invalid saved pipe buffer.");
            foreach(var p in Pumps){Unique(p.Buffers,"pump buffer");Unique(p.Pending.Select(i=>i.Id),"pump reservation");}
            foreach(var v in Vents){Unique(v.Buffers,"vent buffer");Unique(v.Pending,"vent reservation");}
            foreach(var s in Spills)Unique(s.Ids,"spill id");
            foreach(var c in Cultures)Unique(c.Cultures.Select(i=>(ulong)(uint)i.Id),"culture identity");
            if(NativeCultures!=null)Unique(NativeCultures.Select(c=>((ulong)c.Phase<<32)|(uint)c.Cell),"native culture cell");
        }
        private static void Unique(IEnumerable<ulong> ids,string what)
        {var seen=new HashSet<ulong>();foreach(var id in ids)if(!seen.Add(id))throw new InvalidDataException("Duplicate saved "+what+".");}
        private static int Count(BinaryReader r)
        {
            var n=r.ReadInt32();
            if(n<0||n>MaximumRecords||n>r.BaseStream.Length-r.BaseStream.Position)throw new InvalidDataException("Invalid saved pipe record count.");
            return n;
        }
        private static byte Phase(BinaryReader r){var phase=r.ReadByte();if(phase!=1&&phase!=2)throw new InvalidDataException("Invalid saved pipe phase.");return phase;}
        private static int Cell(BinaryReader r,PipeGeometry geometry){var cell=r.ReadInt32();if(!geometry.Contains(cell))throw new InvalidDataException("Saved pipe record leaves its world.");return cell;}
        private static void WriteIds(BinaryWriter w,IEnumerable<ulong> ids){var sorted=ids.OrderBy(i=>i).ToArray();w.Write(sorted.Length);foreach(var id in sorted)w.Write(id);}
        private static ulong[] ReadIds(BinaryReader r)
        {
            var ids=new ulong[Count(r)];for(var i=0;i<ids.Length;++i){ids[i]=r.ReadUInt64();if(ids[i]==0||i>0&&ids[i]<=ids[i-1])throw new InvalidDataException("Noncanonical saved pipe identities.");}
            return ids;
        }
        private static void WriteMap<K>(BinaryWriter w,SortedDictionary<K,PipeAmount> map,Action<BinaryWriter,K> key) where K:notnull
        {var items=map.Where(i=>i.Value.Sign!=0).ToArray();w.Write(items.Length);foreach(var item in items){key(w,item.Key);item.Value.Write(w);}}
        private static SortedDictionary<K,PipeAmount> ReadMap<K>(BinaryReader r,Func<BinaryReader,K> key) where K:notnull
        {
            var map=new SortedDictionary<K,PipeAmount>();
            for(int i=0,n=Count(r);i<n;++i)
            {
                var k=key(r);var value=PipeAmount.Read(r);
                if(value.Sign==0||map.ContainsKey(k)||map.Count>0&&Comparer<K>.Default.Compare(map.Keys.Last(),k)>0)throw new InvalidDataException("Noncanonical saved pipe account.");
                map.Add(k,value);
            }
            return map;
        }
    }
}
