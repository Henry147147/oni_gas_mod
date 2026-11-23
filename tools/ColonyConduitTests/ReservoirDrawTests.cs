using System;
using System.Linq;
using MixedAtmospheres.Oni;

// A reservoir's outlet takes the same share of every stored constituent
// (ColonyReservoirDraw): proportions, the pipe's room, conservation.
internal static class ReservoirDrawTests
{
    private static void Check(bool ok,string why){if(!ok)throw new Exception(why);}
    internal static void Run()
    {
        // 75 % water and 25 % polluted water, a 10 kg pipe: 7.5 and 2.5 kg.
        var takes=ColonyReservoirDraw.Takes(new[]{750f,250f},10f);
        Check(Math.Abs(takes[0]-7.5f)<=1e-6f&&Math.Abs(takes[1]-2.5f)<=1e-6f,"a 75/25 reservoir did not give 7.5 and 2.5 kg");
        Check((double)takes[0]+takes[1]<=10,"the takes exceed the pipe's room");
        // Successive draws keep the proportions and empty the reservoir completely.
        var held=new[]{6f,3f,1f};double given=0;var draws=0;
        while(held.Any(m=>m>0))
        {
            var before=held.Sum(m=>(double)m);var draw=ColonyReservoirDraw.Takes(held,1f);var sum=draw.Sum(m=>(double)m);
            Check(sum>0&&sum<=1,"a draw gave nothing or more than the pipe accepts");
            for(var i=0;i<held.Length;++i)
            {
                Check(draw[i]>=0&&draw[i]<=held[i],"a constituent gave more than it held");
                if(before>1.5)Check(Math.Abs(draw[i]/sum-held[i]/before)<=1e-6,"a draw's proportions differ from the reservoir's");
                held[i]-=draw[i];
            }
            given+=sum;Check(++draws<=64,"the reservoir never emptied");
        }
        Check(Math.Abs(given-10)<=1e-5,"successive draws did not give out what was stored");
        // Everything fits: everything leaves, bit for bit.
        var all=ColonyReservoirDraw.Takes(new[]{.3f,.2f,1e-12f},1f);
        Check(all[0]==.3f&&all[1]==.2f&&all[2]==1e-12f,"a draw that fits did not take every constituent whole");
        // A gas pipe (1 kg) from three gases of very different amounts; no room; nothing stored.
        var gases=ColonyReservoirDraw.Takes(new[]{900f,99.9f,.1f},1f);
        Check(gases.Sum(m=>(double)m)<=1&&gases.Sum(m=>(double)m)>.999999&&Math.Abs(gases[2]-1e-4f)<=1e-9f,"a trace gas did not leave in proportion");
        Check(ColonyReservoirDraw.Takes(new[]{5f,5f},0f).All(m=>m==0)&&ColonyReservoirDraw.Takes(new[]{0f,0f},1f).All(m=>m==0)&&ColonyReservoirDraw.Takes(Array.Empty<float>(),1f).Length==0,
            "a full pipe or an empty reservoir gave something");
        // Awkward binary32 values never exceed the room.
        var random=new Random(7);
        for(var n=0;n<20000;++n)
        {
            var masses=Enumerable.Range(0,1+random.Next(6)).Select(_=>(float)(random.NextDouble()*Math.Pow(10,random.Next(-6,4)))).ToArray();
            var room=(float)(random.NextDouble()*Math.Pow(10,random.Next(-3,2)));
            var draw=ColonyReservoirDraw.Takes(masses,room);double sum=0;
            for(var i=0;i<draw.Length;++i){Check(draw[i]>=0&&draw[i]<=masses[i],"a random draw took more than was held");sum+=draw[i];}
            Check(sum<=room,"a random draw exceeded the room");
            var total=masses.Sum(m=>(double)m);
            Check(sum>=Math.Min(total,room)*(1-1e-6)-1e-30,"a random draw left room unused");
        }
        try{ColonyReservoirDraw.Takes(new[]{float.NaN},1f);throw new Exception("a NaN mass was accepted");}catch(ArgumentOutOfRangeException){}
        Console.WriteLine("PASS reservoir draw: equal shares of every constituent, within the pipe's room, emptying completely");
    }
}
