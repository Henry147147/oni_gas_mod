using System;
using System.Numerics;

namespace MixedAtmospheres.Oni
{
    // One rounded binary64 product of a population operand and a mass ratio.
    // Forming the ratio first can underflow even when the final population
    // flux is ordinary (for example 1e300 * minimum-subnormal kg / 5000kg).
    // Integer arithmetic decides rounding without an overflowing intermediate.
    internal static class StoragePopulationWeight
    {
        private static (BigInteger Significand,int Exponent) Parts(double value)
        {
            var bits=unchecked((ulong)BitConverter.DoubleToInt64Bits(Math.Abs(value)));
            var exponent=(int)((bits>>52)&2047);var significand=new BigInteger(bits&0xfffffffffffffUL);
            if(exponent!=0)significand+=BigInteger.One<<52;
            return(significand,exponent==0?-1074:exponent-1075);
        }
        private static int Bits(BigInteger value)
        {
            var bytes=value.ToByteArray();var last=bytes.Length-1;while(bytes[last]==0)--last;
            var count=last*8;for(var high=bytes[last];high!=0;high>>=1)++count;return count;
        }
        internal static double Product(double population,double mass,double total)
        {
            if(double.IsNaN(population)||double.IsInfinity(population)||mass<0||double.IsNaN(mass)||double.IsInfinity(mass)||
                !(total>0)||double.IsInfinity(total))throw new ArgumentOutOfRangeException("Invalid stored-population weighting operands.");
            if(population==0||mass==0)return BitConverter.Int64BitsToDouble(BitConverter.DoubleToInt64Bits(population)&long.MinValue);
            if(mass==total)return population;
            var a=Parts(population);var b=Parts(mass);var c=Parts(total);
            var numerator=a.Significand*b.Significand;var denominator=c.Significand;
            var exponent=a.Exponent+b.Exponent-c.Exponent;var relative=Bits(numerator)-Bits(denominator);
            if(relative>=0?numerator<(denominator<<relative):(numerator<<-relative)<denominator)--relative;
            var highest=relative+exponent;
            if(highest>1023)throw new InvalidOperationException("Stored-population weighted flux exceeds finite binary64 range.");
            var quantum=highest>=-1022?highest-52:-1074;
            var shift=exponent-quantum;
            if(shift>=0)numerator<<=shift;else denominator<<=-shift;
            var rounded=BigInteger.DivRem(numerator,denominator,out var remainder);
            var comparison=(remainder<<1).CompareTo(denominator);
            if(comparison>0||comparison==0&&!rounded.IsEven)++rounded;
            if(rounded.IsZero)return population<0?BitConverter.Int64BitsToDouble(long.MinValue):0;
            if(rounded>=BigInteger.One<<53){rounded>>=1;++quantum;}
            ulong bits;
            if(rounded<BigInteger.One<<52)bits=(ulong)rounded;
            else
            {
                var encoded=quantum+1075;
                if(encoded>=2047)throw new InvalidOperationException("Stored-population weighted flux exceeds finite binary64 range.");
                bits=((ulong)encoded<<52)|((ulong)rounded&0xfffffffffffffUL);
            }
            if(population<0)bits|=1UL<<63;
            return BitConverter.Int64BitsToDouble(unchecked((long)bits));
        }
    }
}
