using MixedAtmospheres.Oni;

ColonyOwnedStorage.RunControls();
ColonyScenarios.RunClockControls();

namespace UnityEngine
{
    internal enum FindObjectsInactive { Include }
    internal enum FindObjectsSortMode { None }
    internal class Object
    {internal static T[] FindObjectsByType<T>(FindObjectsInactive _,FindObjectsSortMode __)=>Array.Empty<T>();}
}
internal sealed class PrimaryElement
{
    internal byte DiseaseIdx=1;internal int DiseaseCount=10,ThermalWrites;
    internal int MassBits=unchecked((int)0x3f800000),TemperatureBits=unchecked((int)0x43960000);internal bool FailDiseaseWrite;
    internal void SetMassTemperature(){++ThermalWrites;throw new InvalidOperationException("Unleased thermal writer");}
    internal void ModifyDiseaseCount(int delta,string reason){ColonyOwnedStorage.Reject(this,nameof(ModifyDiseaseCount));if(FailDiseaseWrite)throw new InvalidOperationException("Injected disease setter failure");DiseaseCount+=delta;if(DiseaseCount==0)DiseaseIdx=255;}
    internal void AddDisease(byte id,int count,string reason){ColonyOwnedStorage.Reject(this,nameof(AddDisease));if(FailDiseaseWrite)throw new InvalidOperationException("Injected disease setter failure");DiseaseIdx=id;DiseaseCount+=count;}
}
internal sealed class Item
{internal PrimaryElement Primary=new();internal T GetComponent<T>()=>(T)(object)Primary;}
internal sealed class Storage
{internal List<Item> items=new();internal bool isSpawned=>true;internal bool IsEndOfLife()=>false;}

namespace MixedAtmospheres.Oni
{
    internal static class ColonyMachinePorts
    {
        internal readonly record struct Part(int Id);
        internal readonly record struct NativeItem(Part Part,byte Disease,int Count);
        internal static IEnumerable<NativeItem> NativeItems(ColonyOwnedStorage.Inventory inventory)=>inventory.Views;
    }
    internal static partial class ColonyOwnedStorage
    {
        internal sealed class Inventory
        {internal List<ColonyMachinePorts.NativeItem> Views=new();internal long Energy=599000;}
        private sealed class Store
        {
            internal Inventory Inventory=new();internal long Revision=1;
            internal void CommitCulture(int id,int population,object context)
            {Inventory.Views=new(){new(new(id),population>0?(byte)2:(byte)255,population)};++Revision;}
        }
        private sealed class Projection {internal PrimaryElement Item=new();}
        private sealed class Host
        {internal Store Store=new();internal long ProjectedRevision=1;internal Storage Storage=new();internal Dictionary<int,Projection> Projections=new();internal List<Projection> Retiring=new();}
        private sealed class PreparedCulture
        {internal Host Host=null!;internal int Id=7,Step=13;internal object Context=new();}
        private readonly record struct DiseaseHeader(PrimaryElement primaryElement,int Marker);
        internal sealed partial class BiologyScope
        {
            private readonly List<DiseaseHeader> headers;
            private readonly (int Row,DiseaseHeader Header)[] originals;
            private readonly PreparedCulture[] changes;
            private readonly (Host Host,Inventory Inventory)[] owners;
            private bool finished;
            private BiologyScope(Host host,bool missing=false,int population=13)
            {
                var primary=host.Projections[7].Item;
                headers=missing?new():new(){new(primary,99)};originals=new[]{(0,new DiseaseHeader(primary,17))};
                changes=new[]{new PreparedCulture{Host=host,Step=population}};owners=new[]{(host,host.Store.Inventory)};
            }
            internal static void Controls()=>ScopeControls();
            private static void ScopeControls()
            {
                foreach(var population in new[]{0,13,int.MaxValue})foreach(var dirtyThermal in new[]{false,true})
                {
                    var host=Prime();if(dirtyThermal)++host.Store.Revision;
                    var beforeRevision=host.Store.Revision;var primary=host.Projections[7].Item;var beforeEnergy=host.Store.Inventory.Energy;
                    var scope=new BiologyScope(host,population:population);scope.Finish(true);
                    Check(scope.headers[0].Marker==17&&StoredBiologyTicks==1,"native culture header was not restored exactly once");
                    Check(primary.DiseaseCount==population&&primary.DiseaseIdx==(population>0?2:255),"culture projection does not match physical update");
                    Check(primary.ThermalWrites==0&&primary.MassBits==0x3f800000&&primary.TemperatureBits==0x43960000&&host.Store.Inventory.Energy==beforeEnergy,"biology rewrote mass, temperature or energy");
                    Check(host.ProjectedRevision==1&&host.Store.Revision==beforeRevision+1,"culture update falsely acknowledged thermal publication");
                    scope.Finish(true);Check(StoredBiologyTicks==1&&Notifications==1,"culture finalizer repeated its work");
                    Refuse(()=>primary.ModifyDiseaseCount(1,"external"));Refuse(()=>primary.AddDisease(3,1,"external"));
                    Check(editing==0&&primary.DiseaseCount==population,"culture scope leaked or admitted an external native mutation");
                    PublishReady=false;Refuse(RequireQuiescent);Check(primary.ThermalWrites==0&&host.ProjectedRevision==1,"BUSY save changed thermal operands or admitted stale projection");
                    PublishReady=true;RequireQuiescent();Check(Publications==2&&host.ProjectedRevision==host.Store.Revision,"save bypassed complete thermal publisher");
                }
                var failed=Prime();var failedScope=new BiologyScope(failed);failedScope.Finish(false);
                Check(failed.Store.Revision==1&&failed.Projections[7].Item.DiseaseCount==10&&StoredBiologyTicks==0&&Notifications==0&&failedScope.headers[0].Marker==17,"failed biology committed or did not restore native header");
                var missing=Prime();Refuse(()=>new BiologyScope(missing,missing:true).Finish(true));Check(missing.Store.Revision==1&&StoredBiologyTicks==0,"missing native carrier committed biology");
                var untouched=Prime();RefreshCultures(untouched);Check(Notifications==0&&untouched.Projections[7].Item.DiseaseCount==10,"unchanged source republished cultures");
                var retired=Prime();retired.Retiring.Add(new());Refuse(RequireQuiescent);
                var foreign=Prime();foreign.Storage.items.Add(new());Refuse(RequireQuiescent);
                var throwing=Prime();throwing.Store.Revision=2;throwing.Store.Inventory.Views=new(){new(new(7),2,13)};
                throwing.Projections[7].Item.FailDiseaseWrite=true;Refuse(()=>RefreshCultures(throwing));Check(editing==0,"throwing culture setter leaked edit scope");
                throwing.Projections[7].Item.FailDiseaseWrite=false;Refuse(()=>throwing.Projections[7].Item.ModifyDiseaseCount(1,"external"));
                var tiny=Prime();tiny.Store.Revision=2;tiny.Store.Inventory.Views.Add(new(new(8),2,1));RefreshCultures(tiny);
                Check(tiny.Projections.Count==1&&tiny.Projections[7].Item.ThermalWrites==0&&tiny.ProjectedRevision==1,"unrepresented trace culture created a thermal carrier or acknowledged pending heat");
            }
        }
        private static readonly Dictionary<Storage,Host> Hosts=new();
        private static bool Enabled=true,PublishReady;
        private static int StoredBiologyTicks,Notifications,Publications,Assertions,editing;
        private static Host Prime()
        {Hosts.Clear();StoredBiologyTicks=Notifications=Publications=0;PublishReady=false;var host=new Host();host.Store.Inventory.Views.Add(new(new(7),1,10));host.Projections.Add(7,new());Hosts.Add(host.Storage,host);return host;}
        private static void Refresh(Host host)=>host.Projections[7].Item.SetMassTemperature();
        private static void PublishThermalProjection(Host host){++Publications;if(PublishReady)host.ProjectedRevision=host.Store.Revision;}
        private static void NotifyStorage(Host host)=>++Notifications;
        private static void CreditNativeStorageHeat(){}
        private static int? ViewOf(PrimaryElement item)=>1;
        private static int ReservoirPhase(Storage storage)=>2;
        private static void Check(bool value,string error){++Assertions;if(!value)throw new Exception(error);}
        private static void Refuse(Action action){try{action();}catch(InvalidOperationException){++Assertions;return;}catch(NotSupportedException){++Assertions;return;}throw new Exception("Unsafe storage projection accepted");}
        internal static void RunControls(){BiologyScope.Controls();Console.WriteLine("PASS "+Assertions+" storage projection controls: real biology completion and save admission, exact culture/header work, pending thermal publication, BUSY refusal, retirement and foreign-carrier guards");}
    }
}
