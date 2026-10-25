namespace MixedAtmospheres.Oni
{
    // The fixture's seed is stage10, partial arm34 and check38. Select only
    // the formerly uncontrolled closed preparation phase; these stages stay
    // fixed. The supported native callbacks are observed five frames apart.
    internal static class ColonyValvePreparation
    {
        internal static bool TryPartialCallback(ulong seedFrame,ulong previous,ulong last,out ulong next)
        {
            next=0;
            if(previous==0||last<=previous||last>seedFrame||last-previous!=5||seedFrame>ulong.MaxValue-29)return false;
            var arm=seedFrame+24;var end=seedFrame+28;var period=last-previous;
            next=arm+period-(arm-last)%period;
            // Prepare with a frame of publication margin before the original
            // endpoint, without extending its four-frame physical window.
            return next>arm&&next<end;
        }
    }
}
