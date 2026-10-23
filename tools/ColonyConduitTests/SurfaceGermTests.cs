using System;
using System.IO;
using System.Linq;
using MixedAtmospheres.Oni;

static class SurfaceGermTests
{
    static readonly PipeMaterial Material=new(1,1000,0);
    static void Check(bool okay,string why){if(!okay)throw new Exception(why);}
    static PipeAmount Germ(PipeInventory inventory,int id)=>inventory.Germs.ToArray().Where(p=>p.Id==id).Select(p=>p.Amount).Aggregate(PipeAmount.Zero,(a,b)=>a.Plus(b));
    static PipeAmount Amount(PipePart[] values,int id)=>values.Where(p=>p.Id==id).Select(p=>p.Amount).Aggregate(PipeAmount.Zero,(a,b)=>a.Plus(b));
    static byte[] Image(PipeInventory inventory){using var stream=new MemoryStream();using var writer=new BinaryWriter(stream);inventory.Write(writer);return stream.ToArray();}
    static void Exact(PipeAmount left,PipeAmount right,string why)=>Check(left.Minus(right).Sign==0,why);
    internal static void Run()
    {
        var fluid=PipeInventory.Single(Material,1,300,0,1000).WithDiseaseDelta(1,2000).WithDiseaseDelta(2,1e-240);
        var body=new PipeBodyGerms(0,100);
        var plan=PipeSurfaceGermPlan.Prepare(fluid,body,_=>true);
        Check(plan.OutOfBody.Id==0&&plan.OutOfBody.Count==5,"Native float surface transfer changed");
        Check(plan.IntoBody.Length==2&&plan.IntoBody[0].Id==0&&plan.IntoBody[0].Count==50&&plan.IntoBody[1].Count==100,"Surface transfer did not sample every supported fluid identity in stable order");
        Exact(Germ(plan.Fluid,0),new PipeAmount(955),"Same-ID return was resampled during emission");
        Exact(Germ(plan.Fluid,1),new PipeAmount(1900),"Minority surface transfer missing");
        Exact(Germ(plan.Fluid,2),new PipeAmount(1e-240),"Fractional surface population disappeared");
        Exact(plan.Fluid.Total,fluid.Total,"Surface transfer changed mass");Exact(plan.Fluid.Energy,fluid.Energy,"Surface transfer changed heat");
        foreach(var observed in new[]{new PipeBodyGerms(0,145),new PipeBodyGerms(1,93),new PipeBodyGerms(255,0)})
        {
            var competition=plan.NativeCompetition(body,observed);var actual=PipeSurfaceGermPlan.ObservedChange(body,observed);
            foreach(var id in new[]{0,1,2})
            {
                Exact(Germ(plan.Fluid,id).Minus(Germ(fluid,id)),Amount(plan.FluidTransfer,id),"Fluid transfer account does not match actual contents");
                Exact(Amount(actual,id).Plus(Amount(plan.FluidTransfer,id)),Amount(competition,id),"Observed identity competition is conflated with transfer");
            }
        }
        var huge=PipeInventory.Single(Material,1,300,0,1e30).WithDiseaseDelta(1,1e30).WithDiseaseDelta(2,1e-240);
        var capped=PipeSurfaceGermPlan.Prepare(huge,new PipeBodyGerms(1,int.MaxValue),_=>true);
        Check(capped.IntoBody.Sum(p=>(long)p.Count)+(long)int.MaxValue-capped.OutOfBody.Count<=int.MaxValue,"Native surface count overflow");
        foreach(var id in new[]{0,1,2})Exact(Germ(capped.Fluid,id),Germ(huge,id).Plus(Amount(capped.FluidTransfer,id)),"Huge population rounded away retained germ debt");
        var unsupported=PipeSurfaceGermPlan.Prepare(fluid,new PipeBodyGerms(255,0),id=>id==0);
        Exact(Germ(unsupported.Fluid,1),Germ(fluid,1),"Unsupported native germ identity left owned inventory");
        var empty=PipeSurfaceGermPlan.Prepare(PipeInventory.Empty,body,_=>true);
        Check(empty.Fluid.Count==0&&empty.OutOfBody.Count==0&&empty.IntoBody.Length==0,"Empty emission manufactured massless germs");
        Check(Image(plan.Fluid).SequenceEqual(Image(PipeSurfaceGermPlan.Prepare(fluid,body,_=>true).Fluid)),"Surface preparation changed replay bytes");
        foreach(var population in new[]{1,19,20,1000,16777219,int.MaxValue})
        {
            var sample=PipeInventory.Single(Material,1,300,0,population);
            var native=PipeSurfaceGermPlan.Prepare(sample,new PipeBodyGerms(255,0),_=>true);
            Check(native.IntoBody.Sum(p=>p.Count)==(int)((float)population*.05f),"Single-ID native integer/float transfer trajectory changed");
        }
        foreach(var invalid in new[]{(-1,0),(256,1),(255,1),(0,-1)})
        {try{_ = new PipeBodyGerms(invalid.Item1,invalid.Item2);throw new Exception("Malformed native body accepted");}catch(ArgumentOutOfRangeException){}}
        Console.WriteLine("PASS surface germ preparation retains all fractional/overflow identities and separates measured native competition from transfer (runtime inactive)");
    }
}
