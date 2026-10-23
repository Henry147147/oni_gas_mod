using System;
using System.Collections.Generic;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    internal readonly struct PipeBodyGerms
    {
        internal readonly int Id,Count;
        internal PipeBodyGerms(int id,int count)
        {
            if(count<0||id<0||id>byte.MaxValue||id==byte.MaxValue&&count!=0)throw new ArgumentOutOfRangeException();
            Id=id;Count=count;
        }
    }
    internal readonly struct PipeSurfaceFlux
    {
        internal readonly int Id,Count;
        internal PipeSurfaceFlux(int id,int count){Id=id;Count=count;}
    }
    // Pure preparation/accounting; the native body's observed single identity
    // remains authoritative. No hidden full-composition body is manufactured.
    internal sealed class PipeSurfaceGermPlan
    {
        internal readonly PipeInventory Fluid;
        internal readonly PipeSurfaceFlux OutOfBody;
        internal readonly PipeSurfaceFlux[] IntoBody;
        internal readonly PipePart[] FluidTransfer;
        private PipeSurfaceGermPlan(PipeInventory fluid,PipeSurfaceFlux outgoing,PipeSurfaceFlux[] incoming,PipePart[] transfer)
        {Fluid=fluid;OutOfBody=outgoing;IntoBody=incoming;FluidTransfer=transfer;}
        internal static PipeSurfaceGermPlan Prepare(PipeInventory fluid,PipeBodyGerms body,Func<int,bool> supports)
        {
            if(fluid.Count==0)return new PipeSurfaceGermPlan(fluid,new PipeSurfaceFlux(body.Id,0),Array.Empty<PipeSurfaceFlux>(),Array.Empty<PipePart>());
            var after=fluid;var flux=new SortedDictionary<int,PipeAmount>();var incoming=new List<PipeSurfaceFlux>();
            var back=body.Count>0?(int)((float)body.Count*.05f):0;
            if(back>0){after=after.WithDiseaseDelta(body.Id,back);Add(flux,body.Id,new PipeAmount(back));}
            // Reserve room against the total count, even when competition may
            // later replace an identity. Never overflow native integer storage.
            var room=int.MaxValue-(body.Count-back);
            foreach(var germ in fluid.Germs)
            {
                if(!supports(germ.Id))continue;
                // Preserve the native integer -> float -> scaled integer
                // trajectory. Fractions and quantities beyond the body's
                // integer representation remain in the owned fluid.
                var native=(int)Math.Min(int.MaxValue,Math.Floor(germ.Amount.Value));
                var count=Math.Min(room,(int)((float)native*.05f));if(count<=0)continue;
                after=after.WithDiseaseDelta(germ.Id,-count);room-=count;
                Add(flux,germ.Id,new PipeAmount(-count));incoming.Add(new PipeSurfaceFlux(germ.Id,count));
            }
            return new PipeSurfaceGermPlan(after,new PipeSurfaceFlux(body.Id,back),incoming.ToArray(),
                flux.Where(p=>p.Value.Sign!=0).Select(p=>new PipePart(p.Key,p.Value)).ToArray());
        }
        internal PipePart[] NativeCompetition(PipeBodyGerms before,PipeBodyGerms after)
        {
            var change=ObservedChange(before,after).ToDictionary(p=>p.Id,p=>p.Amount);
            // Body transfer is the opposite of fluid transfer. The remainder
            // is the measured native competition/identity conversion, not an
            // invented body inventory or unexplained balancing quantity.
            foreach(var transfer in FluidTransfer)Add(change,transfer.Id,transfer.Amount);
            return change.Where(p=>p.Value.Sign!=0).OrderBy(p=>p.Key).Select(p=>new PipePart(p.Key,p.Value)).ToArray();
        }
        internal static PipePart[] ObservedChange(PipeBodyGerms before,PipeBodyGerms after)
        {
            var change=new SortedDictionary<int,PipeAmount>();
            if(before.Count>0)Add(change,before.Id,new PipeAmount(-before.Count));
            if(after.Count>0)Add(change,after.Id,new PipeAmount(after.Count));
            return change.Where(p=>p.Value.Sign!=0).Select(p=>new PipePart(p.Key,p.Value)).ToArray();
        }
        private static void Add(IDictionary<int,PipeAmount> values,int id,PipeAmount amount)
        {values.TryGetValue(id,out var before);values[id]=before.Plus(amount);}
    }
}
