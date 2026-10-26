using System;

namespace MixedAtmospheres.Oni
{
    // Fixture-only observation of material still awaiting the game's normal receipt delivery.
    internal static class ColonyMeltCustody
    {
        internal static (double Total,double Pending) Count(double present,
            ulong nativeBefore,ulong nativeAfter,ulong publishedBefore,ulong publishedAfter,
            (int GridWidth,int Left,int Bottom,int Width,int Height) arena,int expectedOre,
            OniNativeColony.Receipt[] receipts,uint available)
        {
            if(nativeBefore!=nativeAfter||publishedBefore!=publishedAfter||nativeBefore<publishedBefore||available>receipts.Length)
                throw new InvalidOperationException("Melt custody source changed or its receipt census was truncated.");
            if(double.IsNaN(present)||double.IsInfinity(present)||present<0||arena.GridWidth<=0||arena.Width<=0||arena.Height<=0)
                throw new InvalidOperationException("Invalid melt custody census.");
            double pending=0;ulong previous=0;
            for(var k=0U;k<available;++k)
            {
                var r=receipts[k];
                if(r.Sequence<=previous)throw new InvalidOperationException("Melt custody receipts repeat or are out of order.");
                previous=r.Sequence;
                if(r.Kind!=OniNativeColony.Byproduct||r.Callback!=expectedOre||r.CompletedFrame>nativeBefore)continue;
                var x=(long)r.PublicCell%arena.GridWidth;var y=(long)r.PublicCell/arena.GridWidth;
                if(x<arena.Left||x>=(long)arena.Left+arena.Width||y<arena.Bottom||y>=(long)arena.Bottom+arena.Height)continue;
                if(r.CompletedFrame==0||float.IsNaN(r.MassKg)||float.IsInfinity(r.MassKg)||!(r.MassKg>0))
                    throw new InvalidOperationException("Invalid pending melt byproduct.");
                pending+=r.MassKg;
            }
            var total=present+pending;
            if(double.IsNaN(total)||double.IsInfinity(total))throw new InvalidOperationException("Melt custody overflowed.");
            return(total,pending);
        }
    }
}
