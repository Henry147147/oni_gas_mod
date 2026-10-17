using System;
using System.Runtime.CompilerServices;

namespace MixedAtmospheres.Oni
{
    // Wire conversion must not execute floating-point arithmetic: Unity can
    // enable DAZ/FTZ, including while reading a positive native float parcel.
    internal static class NativeBinary32
    {
        private const ulong FractionMask=(1UL<<52)-1;
        private static int subnormalProbeBits=1;
        // Keep literal subnormal inputs behind a runtime call boundary: a JIT
        // compiling the caller with DAZ enabled can otherwise fold them to zero.
        [MethodImpl(MethodImplOptions.NoInlining)]
        internal static float FromBits(int bits)=>BitConverter.Int32BitsToSingle(bits);
        // Keep this deliberate subnormal probe out of the optimizing JIT's
        // constant folder, which can itself run while the caller has DAZ/FTZ
        // enabled. The wire conversion helpers still execute normally.
        [MethodImpl(MethodImplOptions.NoInlining | MethodImplOptions.NoOptimization)]
        internal static void RequireSubnormalRoundTrip()
        {
            // Read the probe bits at runtime. A JIT entered with DAZ enabled can
            // fold Int32BitsToSingle(1) to zero before our conversion runs.
            var bits=System.Threading.Volatile.Read(ref subnormalProbeBits);
            var scalar=ToDouble(BitConverter.Int32BitsToSingle(bits));
            if(BitConverter.DoubleToInt64Bits(scalar)!=0x36a0000000000000L ||
                BitConverter.SingleToInt32Bits(ToSingleExact(scalar))!=1)
                throw new InvalidOperationException("Native binary32 wire conversion lost a positive trace in the current runtime.");
        }
        internal static double ToDouble(float value)=>ToDouble(BitConverter.SingleToInt32Bits(value));
        internal static double ToDouble(int bits)
        {
            var raw=unchecked((uint)bits);var sign=(ulong)(raw>>31)<<63;
            var exponent=(raw>>23)&255;var fraction=raw&0x7fffff;
            if(exponent==255) return BitConverter.Int64BitsToDouble(unchecked((long)(sign|(2047UL<<52)|((ulong)fraction<<29))));
            if(exponent!=0) return BitConverter.Int64BitsToDouble(unchecked((long)(sign|((ulong)(exponent+896)<<52)|((ulong)fraction<<29))));
            if(fraction==0) return BitConverter.Int64BitsToDouble(unchecked((long)sign));
            var remaining=fraction;var highest=0;while((remaining>>=1)!=0) ++highest;
            var significand=(ulong)fraction<<(52-highest);
            return BitConverter.Int64BitsToDouble(unchecked((long)(sign|((ulong)(highest+874)<<52)|(significand&FractionMask))));
        }
        internal static float ToSingleExact(double value)
        {
            var raw=unchecked((ulong)BitConverter.DoubleToInt64Bits(value));var sign=(uint)(raw>>63)<<31;
            var exponent=(int)((raw>>52)&2047);var fraction=raw&FractionMask;
            if(exponent==0 && fraction==0) return FromBits(unchecked((int)sign));
            var power=exponent-1023;
            if(exponent==2047 || power< -149 || power>127) throw new ArgumentException("Value is not a finite exact binary32 scalar.");
            if(power>= -126) {
                if((fraction&((1UL<<29)-1))!=0) throw new ArgumentException("Value is not an exact binary32 scalar.");
                return FromBits(unchecked((int)(sign|((uint)(power+127)<<23)|(uint)(fraction>>29))));
            }
            var shift= -power-97;var significand=(1UL<<52)|fraction;
            if((significand&((1UL<<shift)-1))!=0) throw new ArgumentException("Value is not an exact binary32 subnormal.");
            return FromBits(unchecked((int)(sign|(uint)(significand>>shift))));
        }
    }
}
