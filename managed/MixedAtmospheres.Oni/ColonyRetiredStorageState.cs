using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    // Completed history has no native host and no future inventory/culture
    // evolution. Aggregate by phase so repeated construction does not retain
    // Unity objects or grow a list of obsolete prefab identities.
    internal sealed class RetiredFluidStorage
    {
        internal readonly byte Phase;
        internal readonly StorageJournalInventory Initial,Admitted,Withdrawn;
        internal readonly StorageJournalAmount Heat;
        internal readonly IReadOnlyDictionary<int,StorageJournalAmount> Births,Deaths;
        private RetiredFluidStorage(byte phase,StorageJournalInventory initial,StorageJournalInventory admitted,
            StorageJournalInventory withdrawn,StorageJournalAmount heat,IReadOnlyDictionary<int,StorageJournalAmount> births,IReadOnlyDictionary<int,StorageJournalAmount> deaths)
        {
            Phase=phase;Initial=initial;Admitted=admitted;Withdrawn=withdrawn;Heat=heat;
            Births=new ReadOnlyDictionary<int,StorageJournalAmount>(Copy(births));Deaths=new ReadOnlyDictionary<int,StorageJournalAmount>(Copy(deaths));
            if((phase!=1&&phase!=2)||!Meaningful||!Audit())throw new InvalidDataException("Invalid completed storage history.");
        }
        private static SortedDictionary<int,StorageJournalAmount> Copy(IReadOnlyDictionary<int,StorageJournalAmount> values)
        {
            var result=new SortedDictionary<int,StorageJournalAmount>();
            foreach(var pair in values)
            {if(pair.Key<0||pair.Key>=255||pair.Value.Sign<=0)throw new InvalidDataException("Invalid completed storage culture journal.");result.Add(pair.Key,pair.Value);}
            return result;
        }
        private bool Meaningful=>new[]{Initial,Admitted,Withdrawn}.Any(j=>j.Masses.Count!=0||j.Germs.Count!=0||j.Energy.Sign!=0)||Heat.Sign!=0||Births.Count!=0||Deaths.Count!=0;
        internal static RetiredFluidStorage From(OwnedFluidStore store)
        {
            if(store.Inventory.Count!=0||store.Clocks.Count!=0||store.CultureContexts.Count!=0||store.PristineEmpty||!store.Audit())
                throw new InvalidOperationException("Only a completed nonpristine empty store can retire its history.");
            return new RetiredFluidStorage(store.Phase,StorageJournalInventory.Empty.Joined(store.Initial),store.Admitted,store.Withdrawn,store.Heat,store.Births,store.Deaths);
        }
        private static SortedDictionary<int,StorageJournalAmount> Joined(IReadOnlyDictionary<int,StorageJournalAmount> left,IReadOnlyDictionary<int,StorageJournalAmount> right)
        {
            var result=Copy(left);
            foreach(var pair in right){result.TryGetValue(pair.Key,out var previous);result[pair.Key]=previous.Plus(pair.Value);}
            return result;
        }
        internal RetiredFluidStorage Joined(RetiredFluidStorage other)
        {
            if(Phase!=other.Phase)throw new InvalidOperationException("Completed storage histories have different phases.");
            return new RetiredFluidStorage(Phase,Initial.Joined(other.Initial),Admitted.Joined(other.Admitted),Withdrawn.Joined(other.Withdrawn),Heat.Plus(other.Heat),
                Joined(Births,other.Births),Joined(Deaths,other.Deaths));
        }
        private static StorageJournalAmount At(IReadOnlyDictionary<int,StorageJournalAmount> values,int id)=>values.TryGetValue(id,out var amount)?amount:default;
        internal bool Audit()
        {
            foreach(var id in Initial.Masses.Keys.Concat(Admitted.Masses.Keys).Concat(Withdrawn.Masses.Keys).Distinct())
                if(At(Initial.Masses,id).Plus(At(Admitted.Masses,id)).Minus(At(Withdrawn.Masses,id)).Sign!=0)return false;
            if(Initial.Energy.Plus(Admitted.Energy).Plus(Heat).Minus(Withdrawn.Energy).Sign!=0)return false;
            foreach(var id in Initial.Germs.Keys.Concat(Admitted.Germs.Keys).Concat(Withdrawn.Germs.Keys).Concat(Births.Keys).Concat(Deaths.Keys).Distinct())
                if(At(Initial.Germs,id).Plus(At(Admitted.Germs,id)).Plus(At(Births,id)).Minus(At(Deaths,id)).Minus(At(Withdrawn.Germs,id)).Sign!=0)return false;
            return true;
        }
        internal void Write(BinaryWriter writer)
        {
            writer.Write(Phase);Initial.Write(writer);Admitted.Write(writer);Withdrawn.Write(writer);Heat.Write(writer);
            WriteMap(writer,Births);WriteMap(writer,Deaths);
        }
        private static void WriteMap(BinaryWriter writer,IReadOnlyDictionary<int,StorageJournalAmount> values)
        {writer.Write(values.Count);foreach(var pair in values){writer.Write(pair.Key);pair.Value.Write(writer);}}
        private static SortedDictionary<int,StorageJournalAmount> ReadMap(BinaryReader reader)
        {
            var count=reader.ReadInt32();if(count<0||count>255||count>(reader.BaseStream.Length-reader.BaseStream.Position)/9)throw new InvalidDataException("Invalid completed culture journal count.");
            var result=new SortedDictionary<int,StorageJournalAmount>();var previous=-1;
            for(var i=0;i<count;++i)
            {var id=reader.ReadInt32();var amount=StorageJournalAmount.Read(reader);if(id<=previous||id>=255||amount.Sign<=0)throw new InvalidDataException("Invalid completed culture journal.");result.Add(id,amount);previous=id;}
            return result;
        }
        internal static RetiredFluidStorage Read(BinaryReader reader)=>new RetiredFluidStorage(reader.ReadByte(),StorageJournalInventory.Read(reader),StorageJournalInventory.Read(reader),
            StorageJournalInventory.Read(reader),StorageJournalAmount.Read(reader),ReadMap(reader),ReadMap(reader));
    }
}
