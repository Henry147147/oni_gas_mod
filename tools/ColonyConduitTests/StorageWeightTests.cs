using System;
using System.Numerics;
using MixedAtmospheres.Core;
using MixedAtmospheres.Oni;

internal static class StorageWeightTests
{
    private static void Check(bool condition,string message){if(!condition)throw new Exception(message);}
    private static BigInteger Units(double value)=>CoupledAccountQuantity.FromDouble(value).Binary64Units;
    // An independent oracle binary-searches the ordered finite IEEE encodings
    // using exact 2^-1074 integer units; it does not derive exponent/mantissa
    // normalization or scale the production rational's quotient.
    private static bool Oracle(double value,double mass,double total,out double expected)
    {
        var numerator=BigInteger.Abs(Units(value))*Units(mass);var denominator=Units(total);expected=0;
        var sign=unchecked((ulong)BitConverter.DoubleToInt64Bits(value))&(1UL<<63);
        const ulong maximum=0x7fefffffffffffff;
        var maximumUnits=Units(double.MaxValue);
        if(numerator>=(maximumUnits+(BigInteger.One<<2044))*denominator)return false;
        ulong lower=0,upper=maximum;
        while(lower<upper)
        {
            var middle=lower+(upper-lower+1)/2;
            if(Units(BitConverter.Int64BitsToDouble((long)middle))*denominator<=numerator)lower=middle;else upper=middle-1;
        }
        var left=Units(BitConverter.Int64BitsToDouble((long)lower))*denominator;
        if(lower<maximum&&left!=numerator)
        {
            var right=Units(BitConverter.Int64BitsToDouble((long)(lower+1)))*denominator;
            var compare=(numerator-left).CompareTo(right-numerator);
            if(compare>0||compare==0&&(lower&1)!=0)++lower;
        }
        expected=BitConverter.Int64BitsToDouble(unchecked((long)(lower|sign)));return true;
    }
    internal static void Run()
    {
        var cases=new (double Value,double Mass,double Total)[]{
            (1e300,double.Epsilon,5000),(-1e300,double.Epsilon,5000),(double.Epsilon,1e300,1e300),
            (1e-240,1e300,double.Epsilon),(1,1,3),(1,1,10),(double.MaxValue,1,2),
            (double.Epsilon,.5,1),(3*double.Epsilon,.5,1),(-double.Epsilon,.5,1),(-0.0,1,2),
            (1,0,double.Epsilon),(double.MaxValue,2,1)};
        var random=new Random(29);
        double Finite()=>BitConverter.Int64BitsToDouble(random.NextInt64(1,0x7fefffffffffffff));
        foreach(var sample in cases)
            Compare(sample.Value,sample.Mass,sample.Total);
        for(var i=0;i<192;++i)Compare((i&1)==0?Finite():-Finite(),Finite(),Finite());
        void Compare(double value,double mass,double total)
        {
            var finite=Oracle(value,mass,total,out var expected);
            if(!finite)
            {try{StoragePopulationWeight.Product(value,mass,total);}catch(InvalidOperationException){return;}throw new Exception("Overflowing biological weighted flux was silently clipped.");}
            var actual=StoragePopulationWeight.Product(value,mass,total);
            Check(BitConverter.DoubleToInt64Bits(actual)==BitConverter.DoubleToInt64Bits(expected),$"Weighted biological flux differs from exact rational IEEE oracle: {value:R}*{mass:R}/{total:R}, {actual:R}!={expected:R}");
        }
        var trace=new PipeAmount(1e300).WeightedPopulation(double.Epsilon,5000);
        Check(trace.Sign>0&&trace.Value>1e-28,"A physically meaningful trace-species biological effect was zeroed by an underflowed mass ratio.");
        var original=new PipeAmount(1).Plus(new PipeAmount(1e-240));
        Check(original.WeightedPopulation(7,7).Minus(original).Sign==0,"Unit population weighting erased an expansion's low physical part.");
        Console.WriteLine("PASS storage population weighting: exact-rational IEEE oracle over full exponent range, tiny mass times huge population, ties, signed zero and explicit overflow");
    }
}
