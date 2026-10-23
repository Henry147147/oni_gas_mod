using System;

namespace MixedAtmospheres.Oni
{
    internal static class PipeContactWork
    {
        // Two binary32 factors multiply exactly in binary64 (at most48 bits).
        // 1000=1024-16-8 then converts kJ to J without rounding away a low
        // product component. Every term is far inside binary64's exponent range.
        private static PipeAmount Energy(float capacity,float temperature)
        {
            var product=(double)capacity*temperature;
            return new PipeAmount(product*1024).Minus(new PipeAmount(product*16)).Minus(new PipeAmount(product*8));
        }
        internal static PipeAmount IntoFluid(float bodyCapacityKilojoules,float bodyBefore,float bodyAfter)
        {
            if(!(bodyCapacityKilojoules>0)||float.IsInfinity(bodyCapacityKilojoules)||
                !(bodyBefore>0)||float.IsInfinity(bodyBefore)||!(bodyAfter>0)||float.IsInfinity(bodyAfter))
                throw new ArgumentOutOfRangeException("Invalid registered body contact state.");
            return Energy(bodyCapacityKilojoules,bodyBefore).Minus(Energy(bodyCapacityKilojoules,bodyAfter));
        }
    }
}
