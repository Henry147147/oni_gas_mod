using System;
using System.Collections.Generic;
using System.IO;

namespace MixedAtmospheres.Oni
{
    // The array-per-expansion PipeAmount as it was before inline atoms: the
    // oracle PipeAmountStorageTests compares the production struct against.
    // Exact sums of binary64 fluxes (legacy storage).

    internal readonly struct LegacyPipeAmount
    {
        private readonly double[]? atoms; // increasing magnitude, non-overlapping
        internal static LegacyPipeAmount Zero => default;
        internal LegacyPipeAmount(double value)
        {
            if (double.IsNaN(value) || double.IsInfinity(value)) throw new ArgumentOutOfRangeException(nameof(value));
            atoms = value == 0 ? null : new[] { value };
        }
        private LegacyPipeAmount(double[] value) { atoms = value.Length == 0 ? null : value; }
        internal int Sign => atoms == null ? 0 : Math.Sign(atoms[atoms.Length - 1]);
        internal double Value
        {
            get
            {
                if(atoms==null)return 0;
                var count=atoms.Length-1;var high=atoms[count];var low=0.0;
                while(count>0)
                {
                    var previous=high;var next=atoms[--count];high=previous+next;
                    low=next-(high-previous);if(low!=0)break;
                }
                // Resolve a half-ulp tie using the next exact lower part.
                // Adding low parts upwards can erase that decisive bit.
                if(count>0&&((low<0&&atoms[count-1]<0)||(low>0&&atoms[count-1]>0)))
                {
                    var doubled=low*2;var rounded=high+doubled;
                    if(rounded-high==doubled)high=rounded;
                }
                return high;
            }
        }
        internal LegacyPipeAmount Plus(LegacyPipeAmount other)=>RawPlus(other).Canonical();
        private LegacyPipeAmount RawPlus(LegacyPipeAmount other)
        {
            if (other.atoms == null) return this;
            if (atoms == null) return other;
            if (atoms.Length == 1 && other.atoms.Length == 1)
            {
                var a = atoms[0]; var b = other.atoms[0]; var total = a + b;
                if (double.IsInfinity(total)) throw new InvalidOperationException("Pipe account overflow.");
                var virtualB = total - a; var error = (a - (total - virtualB)) + (b - virtualB);
                return error == 0 ? new LegacyPipeAmount(total) : new LegacyPipeAmount(new[] { error, total });
            }
            var current = atoms;
            foreach (var value in other.atoms)
            {
                var result = new double[current.Length + 1]; var count = 0; var carry = value;
                foreach (var item in current)
                {
                    var total = carry + item;
                    if (double.IsInfinity(total)) throw new InvalidOperationException("Pipe account overflow.");
                    var virtualItem = total - carry;
                    var error = (carry - (total - virtualItem)) + (item - virtualItem);
                    if (error != 0) result[count++] = error;
                    carry = total;
                }
                if (carry != 0) result[count++] = carry;
                if (count != result.Length) Array.Resize(ref result, count);
                current = result;
            }
            return new LegacyPipeAmount(current);
        }
        private LegacyPipeAmount Canonical()
        {
            if(atoms==null||atoms.Length<2)return this;
            // TwoSum already produced a canonical expansion when its leading
            // atom is the correctly rounded total. Avoid rebuilding that pair.
            if(atoms.Length==2&&Value==atoms[1])return this;
            var remaining=this;var descending=new List<double>(atoms.Length);
            while(remaining.Sign!=0)
            {
                var high=remaining.Value;descending.Add(high);
                remaining=remaining.RawPlus(new LegacyPipeAmount(-high));
                if(descending.Count>4096)throw new InvalidOperationException("Pipe expansion normalization failed.");
            }
            descending.Reverse();return new LegacyPipeAmount(descending.ToArray());
        }
        internal LegacyPipeAmount Minus(LegacyPipeAmount other) => Plus(other.Negated());
        internal LegacyPipeAmount Negated()
        {
            if (atoms == null) return this;
            var result = new double[atoms.Length];
            for (var i = 0; i < result.Length; ++i) result[i] = -atoms[i];
            return new LegacyPipeAmount(result);
        }
        // The rounded products define the shared flux. Subtraction and addition
        // of that same flux are exact; no endpoint is clipped or renormalized.
        internal LegacyPipeAmount Times(double factor)
        {
            if (double.IsNaN(factor) || double.IsInfinity(factor)) throw new ArgumentOutOfRangeException(nameof(factor));
            if (factor == 0 || atoms == null) return Zero;
            if (factor == 1) return this;
            var result = Zero;
            // Each rounded product still defines exactly the same shared
            // flux. Sum them exactly first; normalize its representation once.
            foreach (var atom in atoms) result = result.RawPlus(new LegacyPipeAmount(atom * factor));
            return result.Canonical();
        }
        internal LegacyPipeAmount WeightedPopulation(double mass,double total)
        {
            if(mass<0||double.IsNaN(mass)||double.IsInfinity(mass)||!(total>0)||double.IsInfinity(total))
                throw new ArgumentOutOfRangeException("Invalid stored-population mixture weights.");
            if(atoms==null)return Zero;
            var result=Zero;
            foreach(var atom in atoms)result=result.RawPlus(new LegacyPipeAmount(StoragePopulationWeight.Product(atom,mass,total)));
            return result.Canonical();
        }
        internal void Write(BinaryWriter writer)
        {
            writer.Write(atoms?.Length ?? 0);
            if (atoms != null) foreach (var atom in atoms) writer.Write(atom);
        }
        internal static LegacyPipeAmount Read(BinaryReader reader)
        {
            var count = reader.ReadInt32();
            if (count < 0 || count > 4096) throw new InvalidDataException("Invalid pipe amount size.");
            var result = Zero;var original=new double[count];var nonoverlapping=true;
            for (var i = 0; i < count; ++i)
            {
                original[i]=reader.ReadDouble();
                if(double.IsNaN(original[i])||double.IsInfinity(original[i])||original[i]==0)throw new InvalidDataException("Invalid pipe expansion atom.");
                if(i>0)
                {
                    var exponent=(int)(((ulong)BitConverter.DoubleToInt64Bits(Math.Abs(original[i]))>>52)&2047);
                    var power=exponent-1076;
                    var half=power< -1074?0:power< -1022?BitConverter.Int64BitsToDouble(1L<<(power+1074)):
                        BitConverter.Int64BitsToDouble((long)(power+1023)<<52);
                    if(Math.Abs(original[i-1])>half)nonoverlapping=false;
                }
            }
            // Preserve valid old schema1 expansion bytes. New arithmetic uses
            // one canonical exact expansion, independent of reduction order.
            if(nonoverlapping)return new LegacyPipeAmount(original);
            try{foreach(var atom in original)result=result.RawPlus(new LegacyPipeAmount(atom));}
            catch(InvalidOperationException error){throw new InvalidDataException("Invalid pipe expansion.",error);}
            if((result.atoms?.Length??0)!=count)throw new InvalidDataException("Invalid pipe expansion.");
            for(var i=0;i<count;++i)
                if(BitConverter.DoubleToInt64Bits(result.atoms![i])!=BitConverter.DoubleToInt64Bits(original[i]))
                    throw new InvalidDataException("Invalid pipe expansion ordering.");
            return new LegacyPipeAmount(original);
        }
    }
}
