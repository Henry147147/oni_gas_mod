using System;
using System.Collections.Generic;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    internal static partial class ColonyPipePorts
    {
        internal static ulong[] AdmitRetirement(ColonyRetirementInventory source,int cell,System.Action commit)
        {
            Require();if(actorBatch!=null)throw new InvalidOperationException("Rocket retirement cannot cross an open actor transaction.");
            var ids=new ulong[2];var cursor=nextLocal;
            var from=FromWorld;var retainedHeat=HeatRemainder;
            var exchange=new SortedDictionary<ulong,PipeAmount>(passiveExchange);
            var k=kinetic;var g=potential;var jx=momentumX;var jy=momentumY;
            foreach(var phase in source.Phases)
            {
                from=from.Joined(PipeInventory.Conserved(phase.Inventory.Parts.ToArray(),phase.Inventory.Energy,Array.Empty<PipePart>()));
                retainedHeat=retainedHeat.Plus(phase.Remainder);
                foreach(var germ in phase.Inventory.Germs)Add(exchange,checked((ulong)germ.Id+1),germ.Amount);
                k=k.WithAdded(phase.Kinetic);g=g.WithAdded(phase.Potential);jx=jx.WithAdded(phase.MomentumX);jy=jy.WithAdded(phase.MomentumY);
            }
            try
            {
                for(var i=0;i<2;++i)if(source.Phases[i].Inventory.Count>0)
                {ids[i]=cursor;cursor=checked(cursor+1);Buffers.Add(ids[i],new Buffer((byte)(i+1),cell,source.Phases[i].Inventory));}
                commit(); // native commit allocates nothing and invokes no callbacks
            }
            catch
            {foreach(var id in ids)if(id!=0)Buffers.Remove(id);throw;}
            FromWorld=from;HeatRemainder=retainedHeat;passiveExchange=exchange;nextLocal=cursor;
            kinetic=k;potential=g;momentumX=jx;momentumY=jy;return ids;
        }
    }
}
