using System;
using System.IO;
using MixedAtmospheres.Core;
using MixedAtmospheres.Oni;

static class ScalarProductTests
{
    static void Check(bool okay,string reason){if(!okay)throw new Exception(reason);}
    static byte[] Image(PipeAmount amount)
    {using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream))amount.Write(writer);return stream.ToArray();}
    static double[] Atoms(PipeAmount amount)
    {using var stream=new MemoryStream(Image(amount));using var reader=new BinaryReader(stream);var atoms=new double[reader.ReadInt32()];for(var i=0;i<atoms.Length;++i)atoms[i]=reader.ReadDouble();return atoms;}
    internal static void Run()
    {
        var random=new Random(81742);var checkedProducts=0;
        for(var sample=0;sample<1000;++sample)
        {
            var amount=PipeAmount.Zero;
            for(var i=0;i<12;++i)amount=amount.Plus(new PipeAmount(Math.ScaleB(random.NextDouble()-.5,random.Next(-900,901))));
            foreach(var factor in new[]{0.0,-0.0,1.0,-1.0,.0001f,-.0001f,.6,.4,Math.ScaleB(.75,random.Next(-100,101))})
            {
                var expected=default(CoupledAccountQuantity);
                foreach(var atom in Atoms(amount))expected=expected.Add(CoupledAccountQuantity.FromDouble(atom*factor));
                var actual=amount.Times(factor);var exact=default(CoupledAccountQuantity);
                foreach(var atom in Atoms(actual))exact=exact.Add(CoupledAccountQuantity.FromDouble(atom));
                Check(expected.Binary64Units==exact.Binary64Units,"Scalar multiplication changed a defined rounded atom product");
                Check(expected.TryGetRoundedValue(out var rounded)&&actual.Value==rounded,"Product representation changed its correctly rounded readout");++checkedProducts;
            }
        }
        foreach(var factor in new[]{double.NaN,double.PositiveInfinity,double.NegativeInfinity})
        {try{_=new PipeAmount(1).Times(factor);throw new Exception("Nonfinite scalar factor accepted");}catch(ArgumentOutOfRangeException){}}
        try{_=new PipeAmount(double.MaxValue).Times(2);throw new Exception("Product overflow was silently clipped");}catch(ArgumentOutOfRangeException){}
        Check(new PipeAmount(double.Epsilon).Times(.5).Sign==0&&new PipeAmount(-double.Epsilon).Times(.5).Sign==0,"Rounded subnormal flux changed");
        using(var stream=new MemoryStream())
        {
            using(var writer=new BinaryWriter(stream,System.Text.Encoding.UTF8,true)){writer.Write(2);writer.Write(Math.ScaleB(1,-53));writer.Write(Math.BitIncrement(1));}
            var original=stream.ToArray();stream.Position=0;using var reader=new BinaryReader(stream);var legacy=PipeAmount.Read(reader);
            Check(Image(legacy.Times(1)).AsSpan().SequenceEqual(original),"Unit multiplication rewrote a valid legacy representation");
        }
        Console.WriteLine($"PASS scalar product flux: {checkedProducts} exact rounded-product sums against independent integer oracle; legacy unit path, signs, subnormal and overflow contracts");
    }
}
