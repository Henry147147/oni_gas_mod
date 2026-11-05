using System;
using System.Collections.Generic;
using System.IO;
using MixedAtmospheres.Oni;

// PipeAmount keeps up to two atoms inline. Every operation must return the
// same atoms, readout and failure as the array-per-expansion implementation
// (LegacyPipeAmount), including long expansions worked on the heap.
static class PipeAmountStorageTests
{
    static void Check(bool okay,string reason){if(!okay)throw new Exception(reason);}
    static byte[] Image(PipeAmount amount){using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream))amount.Write(writer);return stream.ToArray();}
    static byte[] Image(LegacyPipeAmount amount){using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream))amount.Write(writer);return stream.ToArray();}
    static PipeAmount ReadNew(byte[] image){using var stream=new MemoryStream(image);using var reader=new BinaryReader(stream);return PipeAmount.Read(reader);}
    static LegacyPipeAmount ReadOld(byte[] image){using var stream=new MemoryStream(image);using var reader=new BinaryReader(stream);return LegacyPipeAmount.Read(reader);}
    static byte[] Atoms(params double[] atoms)
    {using var stream=new MemoryStream();using(var writer=new BinaryWriter(stream)){writer.Write(atoms.Length);foreach(var atom in atoms)writer.Write(atom);}return stream.ToArray();}
    static long compared,longest,longResults,failures;
    // Same atoms, readout bits and sign.
    static void Same(PipeAmount actual,LegacyPipeAmount expected,string operation)
    {
        var a=Image(actual);var b=Image(expected);
        Check(a.AsSpan().SequenceEqual(b),$"{operation} changed atoms: {BitConverter.ToString(a)} != {BitConverter.ToString(b)}");
        Check(BitConverter.DoubleToInt64Bits(actual.Value)==BitConverter.DoubleToInt64Bits(expected.Value),$"{operation} changed its readout");
        Check(actual.Sign==expected.Sign,$"{operation} changed its sign");
        var count=BitConverter.ToInt32(a,0);longest=Math.Max(longest,count);if(count>16)++longResults;++compared;
    }
    // Same result, or the same exception type and message.
    static (PipeAmount,LegacyPipeAmount)? Both(Func<PipeAmount> actual,Func<LegacyPipeAmount> expected,string operation)
    {
        PipeAmount a=default;LegacyPipeAmount b=default;Exception? left=null,right=null;
        try{a=actual();}catch(Exception error){left=error;}
        try{b=expected();}catch(Exception error){right=error;}
        if(left!=null||right!=null)
        {
            Check(left!=null&&right!=null&&left.GetType()==right.GetType()&&left.Message==right.Message,
                $"{operation} failure differs: {left?.GetType().Name}:{left?.Message} / {right?.GetType().Name}:{right?.Message}");
            ++failures;return null;
        }
        Same(a,b,operation);return(a,b);
    }
    static double Scalar(Random random)
    {
        switch(random.Next(12))
        {
            case 0:return 0;
            case 1:return random.Next(2)==0?double.Epsilon:-double.Epsilon;
            case 2:return Math.ScaleB(random.NextDouble()-.5,random.Next(-1074,-1000));
            case 3:return Math.ScaleB(random.NextDouble()-.5,random.Next(1000,1024));
            case 4:return (float)(random.NextDouble()*10-2);
            case 5:return Math.ScaleB(1,random.Next(-1074,1024))*(random.Next(2)==0?1:-1);
            case 6:return Math.BitIncrement(Math.ScaleB(1,random.Next(-60,60)));
            case 7:return random.Next(2)==0?double.MaxValue:-double.MaxValue;
            default:return Math.ScaleB(random.NextDouble()-.5,random.Next(-200,201));
        }
    }
    static double Factor(Random random)
    {
        switch(random.Next(10))
        {
            case 0:return 0;case 1:return -0.0;case 2:return 1;case 3:return -1;
            case 4:return .0001f;case 5:return Math.ScaleB(.75,random.Next(-100,101));
            case 6:return Math.ScaleB(random.NextDouble(),random.Next(-1100,1030));
            default:return random.NextDouble()*2-.5;
        }
    }
    internal static void Run()
    {
        var random=new Random(20261105);
        var pool=new List<(PipeAmount New,LegacyPipeAmount Old)>{(PipeAmount.Zero,LegacyPipeAmount.Zero)};
        void Add((PipeAmount,LegacyPipeAmount)? pair){if(pair is var(a,b)){if(pool.Count<512)pool.Add((a,b));else pool[random.Next(pool.Count)]=(a,b);}}
        // Long expansions: binades far apart need one atom each.
        for(var sample=0;sample<200;++sample)
        {
            var a=PipeAmount.Zero;var b=LegacyPipeAmount.Zero;
            for(var term=random.Next(2,60);term>0;--term)
            {
                var value=Math.ScaleB(random.NextDouble()+.5,random.Next(-1074,1000))*(random.Next(2)==0?1:-1);
                var pair=Both(()=>a.Plus(new PipeAmount(value)),()=>b.Plus(new LegacyPipeAmount(value)),"long sum");
                if(pair is var(x,y)){a=x;b=y;}
            }
            Add((a,b));
        }
        // Half-ulp ties around powers of two: the readout's tie rule and the
        // normalization's peel at binade edges.
        for(var sample=0;sample<400;++sample)
        {
            var a=PipeAmount.Zero;var b=LegacyPipeAmount.Zero;var scale=random.Next(-1000,1000);
            foreach(var unit in new[]{0,-53,-54,-106,-107,-52,-1,-159})
            {
                if(random.Next(3)==0)continue;
                var value=Math.ScaleB(random.Next(2)==0?1:-1,Math.Max(-1074,Math.Min(1023,scale+unit)))*(random.Next(4)==0?1.5:1);
                var pair=Both(()=>a.Plus(new PipeAmount(value)),()=>b.Plus(new LegacyPipeAmount(value)),"tie sum");
                if(pair is var(x,y)){a=x;b=y;}
            }
            Add((a,b));
        }
        // Saved images: valid non-canonical expansions, overlapping and invalid ones.
        var images=new List<byte[]>{Atoms(),Atoms(Math.ScaleB(1,-53),Math.BitIncrement(1)),Atoms(Math.ScaleB(1,-54),1,1e20),
            Atoms(1,2),Atoms(3,1),Atoms(1e-300,1e-100,1,1e100,1e300),Atoms(Math.ScaleB(1,-53),Math.ScaleB(1,-1),1),Atoms(0.0),Atoms(1,double.NaN)};
        for(var sample=0;sample<400;++sample)
        {
            var atoms=new double[random.Next(1,8)];var exponent=random.Next(-1074,-200);
            for(var i=0;i<atoms.Length;++i){exponent+=random.Next(1,130);atoms[i]=Math.ScaleB(random.NextDouble()+.5,Math.Min(exponent,1022))*(random.Next(2)==0?1:-1);}
            images.Add(Atoms(atoms));
        }
        foreach(var image in images)
        {
            PipeAmount a=default;LegacyPipeAmount b=default;Exception? left=null,right=null;
            try{a=ReadNew(image);}catch(Exception error){left=error;}
            try{b=ReadOld(image);}catch(Exception error){right=error;}
            if(left!=null||right!=null){Check(left?.GetType()==right?.GetType()&&left?.Message==right?.Message,"saved expansion failure differs");++failures;continue;}
            Same(a,b,"read");Check(Image(a).AsSpan().SequenceEqual(image),"read rewrote a saved expansion");Add((a,b));
        }
        for(var step=0;step<1000000;++step)
        {
            var (a,b)=pool[random.Next(pool.Count)];var (c,d)=pool[random.Next(pool.Count)];
            switch(random.Next(9))
            {
                case 0:{var v=Scalar(random);Add(Both(()=>new PipeAmount(v),()=>new LegacyPipeAmount(v),"scalar"));break;}
                case 1:Add(Both(()=>a.Plus(c),()=>b.Plus(d),"plus"));break;
                case 2:Add(Both(()=>a.Minus(c),()=>b.Minus(d),"minus"));break;
                case 3:Add(Both(()=>a.Negated(),()=>b.Negated(),"negated"));break;
                case 4:{var f=Factor(random);Add(Both(()=>a.Times(f),()=>b.Times(f),"times"));break;}
                case 5:
                {
                    var total=Math.ScaleB(random.NextDouble()+.01,random.Next(-30,30));var mass=random.Next(3)==0?total:total*random.NextDouble();
                    Add(Both(()=>a.WeightedPopulation(mass,total),()=>b.WeightedPopulation(mass,total),"weighted"));break;
                }
                case 6:{var v=Scalar(random);Add(Both(()=>a.Plus(new PipeAmount(v)),()=>b.Plus(new LegacyPipeAmount(v)),"plus scalar"));break;}
                case 7:{var v=Scalar(random);Add(Both(()=>a.Minus(new PipeAmount(v)),()=>b.Minus(new LegacyPipeAmount(v)),"minus scalar"));break;}
                default:Add(Both(()=>ReadNew(Image(a)),()=>ReadOld(Image(b)),"round trip"));break;
            }
        }
        Check(longest>16&&longResults>0&&failures>0,"storage fuzz did not reach long expansions and failures");
        Console.WriteLine($"PASS pipe amount inline storage: {compared} results identical to the array-per-expansion oracle (atoms, readout, sign); {failures} identical failures; longest expansion {longest} atoms ({longResults} longer than 16)");
    }
}
