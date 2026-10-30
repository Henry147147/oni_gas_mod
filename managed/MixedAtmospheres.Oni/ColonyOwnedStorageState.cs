using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    internal readonly struct StoredFluidAddress : IEquatable<StoredFluidAddress>, IComparable<StoredFluidAddress>
    {
        internal readonly int Prefab,Ordinal;
        internal StoredFluidAddress(int prefab,int ordinal)
        {
            if(prefab<0||ordinal<0||ordinal>255)throw new InvalidDataException("Invalid owned storage component identity.");
            Prefab=prefab;Ordinal=ordinal;
        }
        public bool Equals(StoredFluidAddress other)=>Prefab==other.Prefab&&Ordinal==other.Ordinal;
        public override bool Equals(object? other)=>other is StoredFluidAddress value&&Equals(value);
        public override int GetHashCode()=>HashCode.Combine(Prefab,Ordinal);
        public int CompareTo(StoredFluidAddress other){var order=Prefab.CompareTo(other.Prefab);return order!=0?order:Ordinal.CompareTo(other.Ordinal);}
        internal void Write(BinaryWriter writer){writer.Write(Prefab);writer.Write(Ordinal);}
        internal static StoredFluidAddress Read(BinaryReader reader)=>new StoredFluidAddress(reader.ReadInt32(),reader.ReadInt32());
    }

    // Native storage items are views. Every actual constituent, joule and
    // disease identity stays here, including quantities no float item carries.
    internal sealed class OwnedFluidStore
    {
        internal readonly StoredFluidAddress Address;
        internal readonly byte Phase;
        internal PipeInventory Inventory {get;private set;}
        internal PipeInventory Initial {get;private set;}
        internal StorageJournalInventory Admitted {get;private set;}=StorageJournalInventory.Empty;
        internal StorageJournalInventory Withdrawn {get;private set;}=StorageJournalInventory.Empty;
        internal StorageJournalAmount Heat {get;private set;}
        internal ulong Revision {get;private set;}
        internal readonly SortedDictionary<int,GermPopulationClock> Clocks=new SortedDictionary<int,GermPopulationClock>();
        internal readonly SortedDictionary<int,StoredGermContext> CultureContexts=new SortedDictionary<int,StoredGermContext>();
        internal readonly SortedDictionary<int,StorageJournalAmount> Births=new SortedDictionary<int,StorageJournalAmount>(),Deaths=new SortedDictionary<int,StorageJournalAmount>();
        // The first live scheduling control can bind untouched empty hosts.
        // A drained store is not equivalent: native projections and their
        // continuation history may still exist even when held mass is zero.
        internal bool PristineEmpty=>Inventory.Count==0&&Initial.Count==0&&Admitted.Masses.Count==0&&Admitted.Germs.Count==0&&Admitted.Energy.Sign==0&&
            Withdrawn.Masses.Count==0&&Withdrawn.Germs.Count==0&&Withdrawn.Energy.Sign==0&&Heat.Sign==0&&Clocks.Count==0&&CultureContexts.Count==0&&Births.Count==0&&Deaths.Count==0;
        internal OwnedFluidStore(StoredFluidAddress address,byte phase,PipeInventory initial)
        {
            if(phase!=1&&phase!=2)throw new InvalidDataException("Invalid owned storage phase.");
            Address=address;Phase=phase;Inventory=Initial=initial??throw new ArgumentNullException(nameof(initial));
        }
        internal sealed class Move
        {
            internal readonly OwnedFluidStore Owner;
            internal readonly ulong Revision;
            internal readonly PipeInventory Before,After,Parcel,Outside;
            internal readonly StorageJournalInventory Journal;
            internal readonly ulong NextRevision;
            internal readonly int[] Retired;
            internal readonly bool Intake;
            internal Move(OwnedFluidStore owner,PipeInventory after,PipeInventory parcel,PipeInventory outside,bool intake)
            {
                Owner=owner;Revision=owner.Revision;Before=owner.Inventory;After=after;Parcel=parcel;Outside=outside;Intake=intake;
                Journal=intake?owner.Admitted.Joined(parcel):owner.Withdrawn.Joined(parcel);NextRevision=checked(Revision+1);
                Retired=owner.Clocks.Keys.Concat(owner.CultureContexts.Keys).Distinct().Where(id=>Germs(after,id).Sign==0).ToArray();
            }
        }
        internal Move PrepareAdmission(PipeInventory incoming,double capacity,Func<int,PipeMaterial> material)
        {
            var transfer=PipeInventory.Transfer(incoming,Inventory,double.MaxValue,capacity,material);
            return new Move(this,transfer.Target,transfer.Parcel,transfer.Source,true);
        }
        // Reservoirs leave only=null: every phase constituent travels in the
        // same proportion. A recipe/filter names its requested reagent.
        internal Move PrepareWithdrawal(double maximum,Func<int,PipeMaterial> material,int? only=null,bool excluding=false)
        {
            var split=Inventory.Split(maximum,material,only,excluding);
            return new Move(this,split.Remaining,split.Parcel,PipeInventory.Empty,false);
        }
        internal static bool NativeProjectionReady(float initialCapacity,bool thermalEnabled,int handle)
            =>initialCapacity==0||!thermalEnabled||handle!=-1;
        // A native projection's first registration can contain contact work.
        // It must be credited while the source fluid is still held; preparing
        // or committing its withdrawal before that boundary would orphan heat.
        internal Move? PrepareWithdrawalTo(bool thermalReady,PipeInventory destination,double maximum,double capacity,Func<int,PipeMaterial> material)
            =>thermalReady?PrepareWithdrawalTo(destination,maximum,capacity,material):null;
        internal Move PrepareWithdrawalTo(PipeInventory destination,double maximum,double capacity,Func<int,PipeMaterial> material)
        {
            var transfer=PipeInventory.Transfer(Inventory,destination,maximum,capacity,material);
            return new Move(this,transfer.Source,transfer.Parcel,transfer.Target,false);
        }
        internal void Commit(Move move)
        {
            if(!ReferenceEquals(move.Owner,this)||move.Revision!=Revision||!ReferenceEquals(move.Before,Inventory))
                throw new InvalidOperationException("Owned storage changed after its transfer was prepared.");
            // Prepare every allocation before changing either authoritative
            // field; callers commit the paired pipe state on the same thread.
            Inventory=move.After;if(move.Intake)Admitted=move.Journal;else Withdrawn=move.Journal;Revision=move.NextRevision;
            foreach(var id in move.Retired){Clocks.Remove(id);CultureContexts.Remove(id);}
        }
        internal sealed class HeatChange
        {
            internal readonly OwnedFluidStore Owner;
            internal readonly PipeInventory Before,After;
            internal readonly StorageJournalAmount Heat;
            internal readonly ulong Revision,NextRevision;
            internal HeatChange(OwnedFluidStore owner,PipeAmount work,Func<int,PipeMaterial> material)
            {
                Owner=owner;Before=owner.Inventory;Revision=owner.Revision;
                After=Before.WithHeat(work);Heat=owner.Heat.Plus(work);NextRevision=checked(Revision+1);
                if(After.Count>0)After.Temperature(material);
            }
        }
        internal HeatChange PrepareHeat(PipeAmount work,Func<int,PipeMaterial> material)=>new HeatChange(this,work,material);
        internal void Commit(HeatChange change)
        {
            if(!ReferenceEquals(change.Owner,this)||change.Revision!=Revision||!ReferenceEquals(change.Before,Inventory))
                throw new InvalidOperationException("Owned storage changed after its heat credit was prepared.");
            Inventory=change.After;Heat=change.Heat;Revision=change.NextRevision;
        }
        internal void AddHeat(PipeAmount representedWork,Func<int,PipeMaterial> material)=>Commit(PrepareHeat(representedWork,material));
        internal void CommitCulture(int id,GermPopulationStep step,StoredGermContext? context=null)
        {
            if(id<0||id>=255||step.Population.Sign<0||Germs(Inventory,id).Plus(step.Change).Minus(step.Population).Sign!=0)
                throw new InvalidOperationException("Owned storage biology step does not match its actual population.");
            var next=Inventory.WithDiseaseDelta(id,step.Change);var journal=step.Change.Sign>0?Births:Deaths;
            journal.TryGetValue(id,out var before);var total=before.Plus(step.Change.Sign>0?step.Change:step.Change.Negated());
            var revision=checked(Revision+1);
            Inventory=next;
            if(step.Population.Sign>0){Clocks[id]=step.Clock;if(context.HasValue)CultureContexts[id]=context.Value;}
            else{Clocks.Remove(id);CultureContexts.Remove(id);}
            if(step.Change.Sign!=0)journal[id]=total;Revision=revision;
        }
        internal bool Audit()
        {
            foreach(var id in Initial.Parts.ToArray().Select(p=>p.Id).Concat(Admitted.Masses.Keys).Concat(Withdrawn.Masses.Keys).Concat(Inventory.Parts.ToArray().Select(p=>p.Id)).Distinct())
            {
                Admitted.Masses.TryGetValue(id,out var admitted);Withdrawn.Masses.TryGetValue(id,out var withdrawn);
                if(StorageJournalAmount.From(Mass(Initial,id)).Plus(admitted).Minus(withdrawn).Minus(StorageJournalAmount.From(Mass(Inventory,id))).Sign!=0)return false;
            }
            if(StorageJournalAmount.From(Initial.Energy).Plus(Admitted.Energy).Plus(Heat).Minus(Withdrawn.Energy).Minus(StorageJournalAmount.From(Inventory.Energy)).Sign!=0)return false;
            foreach(var id in Initial.Germs.ToArray().Select(p=>p.Id).Concat(Admitted.Germs.Keys).Concat(Withdrawn.Germs.Keys).Concat(Inventory.Germs.ToArray().Select(p=>p.Id)).Concat(Births.Keys).Concat(Deaths.Keys).Distinct())
            {
                Admitted.Germs.TryGetValue(id,out var admitted);Withdrawn.Germs.TryGetValue(id,out var withdrawn);Births.TryGetValue(id,out var born);Deaths.TryGetValue(id,out var died);
                if(StorageJournalAmount.From(Germs(Initial,id)).Plus(admitted).Plus(born).Minus(died).Minus(withdrawn).Minus(StorageJournalAmount.From(Germs(Inventory,id))).Sign!=0)return false;
            }
            return true;
        }
        private static PipeAmount Mass(PipeInventory inventory,int id)
        {foreach(var part in inventory.Parts)if(part.Id==id)return part.Amount;return PipeAmount.Zero;}
        private static PipeAmount Germs(PipeInventory inventory,int id)
        {foreach(var part in inventory.Germs)if(part.Id==id)return part.Amount;return PipeAmount.Zero;}
        internal void Write(BinaryWriter writer)
        {
            Address.Write(writer);writer.Write(Phase);writer.Write(Revision);
            Inventory.Write(writer);Initial.Write(writer);Admitted.Write(writer);Withdrawn.Write(writer);Heat.Write(writer);
            writer.Write(Clocks.Count);foreach(var clock in Clocks){writer.Write(clock.Key);clock.Value.Write(writer);}
            writer.Write(CultureContexts.Count);foreach(var context in CultureContexts){writer.Write(context.Key);context.Value.Write(writer);}
            WriteJournal(writer,Births);WriteJournal(writer,Deaths);
        }
        internal static OwnedFluidStore Read(BinaryReader reader)
        {
            var address=StoredFluidAddress.Read(reader);var phase=reader.ReadByte();var revision=reader.ReadUInt64();
            var inventory=PipeInventory.Read(reader);var initial=PipeInventory.Read(reader);var admitted=StorageJournalInventory.Read(reader);var withdrawn=StorageJournalInventory.Read(reader);var heat=StorageJournalAmount.Read(reader);
            var store=new OwnedFluidStore(address,phase,initial){Inventory=inventory,Admitted=admitted,Withdrawn=withdrawn,Heat=heat,Revision=revision};
            var previous=-1;
            for(int k=0,n=Count(reader);k<n;++k)
            {var id=reader.ReadInt32();if(id<=previous||id>=255||Germs(inventory,id).Sign==0)throw new InvalidDataException("Invalid owned storage germ clock.");store.Clocks.Add(id,GermPopulationClock.Read(reader));previous=id;}
            previous=-1;
            for(int k=0,n=Count(reader);k<n;++k)
            {var id=reader.ReadInt32();if(id<=previous||id>=255||!store.Clocks.ContainsKey(id))throw new InvalidDataException("Invalid owned storage culture context.");store.CultureContexts.Add(id,StoredGermContext.Read(reader));previous=id;}
            ReadJournal(reader,store.Births);ReadJournal(reader,store.Deaths);
            if(!store.Audit())throw new InvalidDataException("Owned storage checkpoint fails its physical inventory audit.");
            return store;
        }
        private static int Count(BinaryReader reader)
        {var count=reader.ReadInt32();if(count<0||count>255)throw new InvalidDataException("Invalid owned storage disease count.");return count;}
        private static void WriteJournal(BinaryWriter writer,SortedDictionary<int,StorageJournalAmount> journal)
        {writer.Write(journal.Count);foreach(var entry in journal){writer.Write(entry.Key);entry.Value.Write(writer);}}
        private static void ReadJournal(BinaryReader reader,SortedDictionary<int,StorageJournalAmount> journal)
        {
            var previous=-1;
            for(int k=0,n=Count(reader);k<n;++k)
            {var id=reader.ReadInt32();var value=StorageJournalAmount.Read(reader);if(id<=previous||id>=255||value.Sign<=0)throw new InvalidDataException("Invalid owned storage biology journal.");journal.Add(id,value);previous=id;}
        }
    }

    // Schema5's component-storage section. A null section means the older
    // native-item runtime; an empty section means exact storage is active and
    // currently owns no components. These modes must remain distinguishable.
    internal sealed class OwnedFluidStorageImage
    {
        internal const int MaximumStores=1_000_000;
        internal readonly OwnedFluidStore[] Stores;
        internal readonly RetiredFluidStorage[] Retired;
        internal OwnedFluidStorageImage(IEnumerable<OwnedFluidStore> stores,IEnumerable<RetiredFluidStorage>? retired=null)
        {
            Stores=stores.OrderBy(s=>s.Address).ToArray();
            Retired=retired?.OrderBy(s=>s.Phase).ToArray()??Array.Empty<RetiredFluidStorage>();
            if(Retired.Length>2)throw new InvalidDataException("Too many completed storage phases.");
            for(var i=0;i<Retired.Length;++i)if(!Retired[i].Audit()||i>0&&Retired[i-1].Phase==Retired[i].Phase)
                throw new InvalidDataException("Invalid or duplicate completed storage phase.");
            if(Stores.Length>MaximumStores)throw new InvalidDataException("Too many owned storage components.");
            for(var i=0;i<Stores.Length;++i)
                if(i>0&&Stores[i-1].Address.Equals(Stores[i].Address)||!Stores[i].Audit())
                    throw new InvalidDataException("Invalid or duplicate owned storage component.");
        }
        internal void Write(BinaryWriter writer)
        {
            writer.Write(Stores.Length);foreach(var store in Stores)store.Write(writer);
            if(Retired.Length!=0){writer.Write(Retired.Length);foreach(var history in Retired)history.Write(writer);}
        }
        internal static OwnedFluidStorageImage Read(BinaryReader reader,bool retired=false)
        {
            var count=reader.ReadInt32();
            if(count<0||count>MaximumStores||count>(reader.BaseStream.Length-reader.BaseStream.Position)/10)
                throw new InvalidDataException("Invalid owned storage component count.");
            var stores=new OwnedFluidStore[count];
            for(var i=0;i<count;++i)
            {
                stores[i]=OwnedFluidStore.Read(reader);
                if(i>0&&stores[i-1].Address.CompareTo(stores[i].Address)>=0)
                    throw new InvalidDataException("Noncanonical owned storage component order.");
            }
            var archives=Array.Empty<RetiredFluidStorage>();
            if(retired)
            {
                var phases=reader.ReadInt32();if(phases<1||phases>2)throw new InvalidDataException("Invalid completed storage phase count.");
                archives=new RetiredFluidStorage[phases];
                for(var i=0;i<phases;++i)
                {archives[i]=RetiredFluidStorage.Read(reader);if(i>0&&archives[i-1].Phase>=archives[i].Phase)throw new InvalidDataException("Noncanonical completed storage phase order.");}
            }
            return new OwnedFluidStorageImage(stores,archives);
        }
    }
}
