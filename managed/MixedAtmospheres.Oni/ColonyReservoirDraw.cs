using System;
using System.Collections.Generic;

namespace MixedAtmospheres.Oni
{
    // What a reservoir's outlet takes from each stored constituent when the
    // pipe accepts `accepted` kg (user decision 2026-11-23: a liquid or gas
    // reservoir gives out its whole stored mixture, not one element at a time).
    // Every constituent gives the same share of its mass. The takes are
    // binary32, as the game's stored items are; each is rounded down, so
    // together they never exceed `accepted`, and a constituent never gives
    // more than it holds. When everything fits, everything leaves.
    internal static class ColonyReservoirDraw
    {
        internal static float[] Takes(IReadOnlyList<float> masses,float accepted)
        {
            var takes=new float[masses.Count];double total=0;
            foreach(var mass in masses)
            {
                if(!(mass>=0)||float.IsInfinity(mass))throw new ArgumentOutOfRangeException(nameof(masses));
                total+=mass;
            }
            if(!(accepted>0)||!(total>0))return takes;
            if(float.IsInfinity(accepted))throw new ArgumentOutOfRangeException(nameof(accepted));
            var share=Math.Min(1.0,accepted/total);double given=0;
            for(var i=0;i<takes.Length;++i)
            {
                var wanted=share>=1?masses[i]:Math.Min(masses[i],masses[i]*share);
                wanted=Math.Min(wanted,accepted-given);
                if(!(wanted>0))continue;
                var take=(float)wanted;
                if(take>wanted)take=BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(take)-1);
                if(!(take>0))continue;
                takes[i]=take;given+=take;
            }
            return takes;
        }
    }
}
