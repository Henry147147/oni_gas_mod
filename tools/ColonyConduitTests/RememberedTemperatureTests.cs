using System;
using System.Collections.Generic;
using MixedAtmospheres.Oni;

// PipeInventory remembers the temperature computed with the registered
// catalogue. A remembered value must equal a fresh computation, must never be
// reused after the catalogue changes, and a failure must recur.
static class RememberedTemperatureTests
{
    static void Check(bool okay,string reason){if(!okay)throw new Exception(reason);}
    internal static void Run()
    {
        var table=new Dictionary<int,PipeMaterial>{[1]=new PipeMaterial(1,1000,0),[2]=new PipeMaterial(2,2000,2e6),[3]=new PipeMaterial(3,700.5,-1e5)};
        PipeMaterial Catalogue(int id)=>table[id];
        Func<int,PipeMaterial> registered=Catalogue;Func<int,PipeMaterial> fresh=id=>table[id];
        var random=new Random(5);var inventories=new List<PipeInventory>();var compared=0;
        try
        {
            PipeInventory.RememberTemperatures(registered);
            for(var i=0;i<300;++i)
            {
                var a=PipeInventory.Single(table[1+random.Next(3)],random.NextDouble()*10,200+random.NextDouble()*200);
                var b=PipeInventory.Single(table[1+random.Next(3)],random.NextDouble(),200+random.NextDouble()*200);
                var joined=a.Joined(b).WithHeat(random.NextDouble()*1000-500);
                var split=joined.Split(joined.Mass*random.NextDouble(),registered);
                inventories.Add(joined);inventories.Add(split.Remaining);inventories.Add(split.Parcel);
            }
            void Compare(string stage)
            {
                foreach(var inventory in inventories)
                {
                    double? first=null,second=null,expected=null;
                    try{first=inventory.Temperature(registered);}catch(InvalidOperationException){}
                    try{second=inventory.Temperature(registered);}catch(InvalidOperationException){}
                    try{expected=inventory.Temperature(fresh);}catch(InvalidOperationException){}
                    Check(first.HasValue==expected.HasValue&&second.HasValue==expected.HasValue,stage+": remembered temperature changed a failure");
                    if(expected.HasValue)Check(BitConverter.DoubleToInt64Bits(first!.Value)==BitConverter.DoubleToInt64Bits(expected.Value)&&
                        BitConverter.DoubleToInt64Bits(second!.Value)==BitConverter.DoubleToInt64Bits(expected.Value),stage+": remembered temperature differs");
                    ++compared;
                }
            }
            Compare("first");
            // The same delegate over a changed catalogue: renewed, never reused.
            table[2]=new PipeMaterial(2,2500,1e6);PipeInventory.RememberTemperatures(registered);Compare("renewed");
            // A cold mixture below zero kelvin fails every time.
            var cold=PipeInventory.Single(table[1],1,1).WithHeat(-5000);
            for(var i=0;i<2;++i){try{cold.Temperature(registered);throw new Exception("invalid temperature remembered");}catch(InvalidOperationException){}}
        }
        finally{PipeInventory.RememberTemperatures(null);}
        Console.WriteLine($"PASS remembered pipe temperatures: {compared} inventories equal to fresh computation, renewed with the catalogue; failures recur");
    }
}
