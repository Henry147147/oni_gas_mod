using System;
using System.IO;
using System.Numerics;
using MixedAtmospheres.Oni;

static class ContactWorkTests
{
    static void Check(bool okay,string reason){if(!okay)throw new Exception(reason);}
    static BigInteger SingleUnits(float value)
    {
        var bits=(uint)BitConverter.SingleToInt32Bits(value);var exponent=(int)(bits>>23&255);var mantissa=bits&0x7fffff;
        return exponent==0?mantissa:new BigInteger(mantissa|0x800000)<<(exponent-1);
    }
    static BigInteger DoubleUnits(double value)
    {
        var bits=(ulong)BitConverter.DoubleToInt64Bits(value);var exponent=(int)(bits>>52&2047);var mantissa=bits&0xfffffffffffff;
        var exact=exponent==0?mantissa:new BigInteger(mantissa|0x10000000000000)<<(exponent-1);
        return (bits>>63)==0?exact:-exact;
    }
    static BigInteger Exact(PipeAmount amount)
    {
        using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true))amount.Write(writer);
        stream.Position=0;using var reader=new BinaryReader(stream);var count=reader.ReadInt32();var exact=BigInteger.Zero;
        for(var i=0;i<count;++i)exact+=DoubleUnits(reader.ReadDouble());return exact;
    }
    internal static void Run()
    {
        const float cutoff = 1e-6f;
        var smaller = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(cutoff)-1);
        var larger = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(cutoff)+1);
        var cooler = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(300f)-1);
        foreach (var sign in new[]{1f,-1f})
        {
            var before=sign>0 ? 300f : cooler;
            var after=sign>0 ? cooler : 300f;
            // Both endpoint temperatures are representable, so the earlier
            // nonzero-wire test declared a message ONI never submitted.
            var wire=(before-after)*.03125f;
            Check(wire!=0 && Math.Abs(wire)<cutoff,"The small conduit control must have nonzero wire energy");
            foreach (var work in new[]{0f,smaller,cutoff})
                Check(PipeContactWork.BodyMessageKilojoules(sign*work,before,after,.03125f)==0,
                    "A sub-threshold conduit contact left a phantom save receipt");
            Check(PipeContactWork.BodyMessageKilojoules(sign*larger,before,after,.03125f)==wire,
                "A genuine message below the cutoff after carrier rounding lost its exact wire energy");
            Check(PipeContactWork.BodyMessageKilojoules(sign*larger,before,before,.03125f)==0,
                "An unchanged carrier endpoint left pending body work");
        }
        Check(PipeContactWork.IntoFluid(1.5f,400,300).Value==150000,"Registered body heat units/sign changed");
        var random=new Random(912037);
        for(var i=0;i<2000;++i)
        {
            float Scalar()=>BitConverter.Int32BitsToSingle(random.Next(1,255)<<23|random.Next(0,1<<23));
            var capacity=Scalar();var before=Scalar();var after=Scalar();var between=Scalar();
            var work=PipeContactWork.IntoFluid(capacity,before,after);
            var expected=(SingleUnits(capacity)*(SingleUnits(before)-SingleUnits(after))*1000)<<776;
            Check(Exact(work)==expected,"Body work lost an exact binary32 product component");
            Check(work.Plus(PipeContactWork.IntoFluid(capacity,after,before)).Sign==0,"Opposite body work did not cancel");
            Check(PipeContactWork.IntoFluid(capacity,before,between).Plus(PipeContactWork.IntoFluid(capacity,between,after)).Minus(work).Sign==0,
                "Partitioned represented work differs from endpoint body energy");
        }
        foreach(var invalid in new[]{0f,-1,float.NaN,float.PositiveInfinity})
        {try{_=PipeContactWork.IntoFluid(invalid,300,301);throw new Exception("Invalid registered body capacity accepted");}catch(ArgumentOutOfRangeException){}}
        Console.WriteLine("PASS conduit body work: exact full-range binary32 capacity/temperature products against independent integer oracle, units/sign, cancellation and partition closure");
    }
}
