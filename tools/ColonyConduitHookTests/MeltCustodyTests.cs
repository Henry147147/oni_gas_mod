using System.Reflection;

internal static class MeltCustodyTests
{
    internal static void Run(Assembly assembly)
    {
        var type=assembly.GetType("MixedAtmospheres.Oni.OniNativeColony+Receipt")!;
        var count=assembly.GetType("MixedAtmospheres.Oni.ColonyMeltCustody")!.GetMethod("Count",BindingFlags.Static|BindingFlags.NonPublic)!;
        const int lead=111;var arena=(10,1,1,8,6);
        (ulong Sequence,ulong Frame,uint Cell,uint Kind,int Hash,float Mass) R(ulong sequence,ulong frame,uint cell,uint kind,int hash,float mass)=>(sequence,frame,cell,kind,hash,mass);
        Array Receipts(params (ulong Sequence,ulong Frame,uint Cell,uint Kind,int Hash,float Mass)[] items)
        {
            var array=Array.CreateInstance(type,items.Length);
            for(var k=0;k<items.Length;++k)
            {
                var r=Activator.CreateInstance(type)!;var item=items[k];
                foreach(var field in new (string Name,object Value)[]{("Sequence",item.Sequence),("CompletedFrame",item.Frame),("PublicCell",item.Cell),("Kind",item.Kind),("Callback",item.Hash),("MassKg",item.Mass)})
                    type.GetField(field.Name)!.SetValue(r,field.Value);
                array.SetValue(r,k);
            }
            return array;
        }
        (double Total,double Pending) Probe(double present,Array receipts,uint? available=null,ulong before=10,ulong after=10,ulong publishedBefore=9,ulong publishedAfter=9)
            =>((double,double))count.Invoke(null,new object[]{present,before,after,publishedBefore,publishedAfter,arena,lead,receipts,available??(uint)receipts.Length})!;
        void Check(bool ok,string why){if(!ok)throw new Exception(why);}
        void Refuses(System.Action action,string why)
        {
            try{action();}catch(TargetInvocationException e)when(e.InnerException is InvalidOperationException){return;}
            throw new Exception(why);
        }
        // Public synthetic custody: 50kg fluid +10kg tile +15kg delivered debris +25kg pending.
        // These are independent chosen inputs, not licensed runtime or memory captures.
        var good=Receipts(R(1,10,22,5,lead,25));
        Check(Probe(50+10+15,good)==(100,25),"The native-to-game custody gap omitted its undelivered material.");
        Check(Probe(100,Receipts())==(100,0),"A fully delivered source changed its total.");
        var excluded=Receipts(R(1,11,22,5,lead,99),R(2,10,22,5,222,99),R(3,10,99,5,lead,99),
            R(4,10,22,1,lead,99),R(5,10,19,5,lead,99),R(6,10,72,5,lead,99),R(7,10,2,5,lead,99));
        Check(Probe(100,excluded)==(100,0),"Future, other-species, wrong-kind or out-of-arena material was credited.");
        var tail=Receipts(R(1,10,22,5,lead,25),R(2,10,22,5,lead,75));
        Check(Probe(75,tail,1)==(100,25),"Unused receipt-array capacity was credited.");
        Refuses(()=>Probe(75,good,2),"A truncated receipt census passed.");
        Refuses(()=>Probe(75,good,after:11),"A changing native epoch passed.");
        Refuses(()=>Probe(75,good,publishedAfter:10),"A changing managed epoch passed.");
        Refuses(()=>Probe(75,good,before:8,after:8),"An older native epoch was combined with newer managed material.");
        Refuses(()=>Probe(75,Receipts(R(1,10,22,5,lead,12.5f),R(1,10,22,5,lead,12.5f))),"A duplicate receipt was credited twice.");
        foreach(var bad in new[]{float.NaN,float.PositiveInfinity,-1f,0f})
            Refuses(()=>Probe(75,Receipts(R(1,10,22,5,lead,bad))),"An invalid matching byproduct passed.");
        Refuses(()=>Probe(75,Receipts(R(1,0,22,5,lead,25))),"An unstamped byproduct passed.");
        Refuses(()=>Probe(double.NaN,good),"An invalid present inventory passed.");
        Console.WriteLine("PASS melt custody: complete synthetic100kg, future/wrong-source exclusion, receipt bounds, stable epochs and no duplicate credit");
    }
}
