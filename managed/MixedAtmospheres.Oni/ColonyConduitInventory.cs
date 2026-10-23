using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    // Exact sums of binary64 fluxes. Small components remain primary state;
    // an addition to a much larger inventory never erases its low part.
    internal readonly struct PipeAmount
    {
        private readonly double[]? atoms; // increasing magnitude, non-overlapping
        internal static PipeAmount Zero => default;
        internal PipeAmount(double value)
        {
            if (double.IsNaN(value) || double.IsInfinity(value)) throw new ArgumentOutOfRangeException(nameof(value));
            atoms = value == 0 ? null : new[] { value };
        }
        private PipeAmount(double[] value) { atoms = value.Length == 0 ? null : value; }
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
        internal PipeAmount Plus(PipeAmount other)=>RawPlus(other).Canonical();
        private PipeAmount RawPlus(PipeAmount other)
        {
            if (other.atoms == null) return this;
            if (atoms == null) return other;
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
            return new PipeAmount(current);
        }
        private PipeAmount Canonical()
        {
            if(atoms==null||atoms.Length<2)return this;
            var remaining=this;var descending=new List<double>(atoms.Length);
            while(remaining.Sign!=0)
            {
                var high=remaining.Value;descending.Add(high);
                remaining=remaining.RawPlus(new PipeAmount(-high));
                if(descending.Count>4096)throw new InvalidOperationException("Pipe expansion normalization failed.");
            }
            descending.Reverse();return new PipeAmount(descending.ToArray());
        }
        internal PipeAmount Minus(PipeAmount other) => Plus(other.Negated());
        internal PipeAmount Negated()
        {
            if (atoms == null) return this;
            var result = new double[atoms.Length];
            for (var i = 0; i < result.Length; ++i) result[i] = -atoms[i];
            return new PipeAmount(result);
        }
        // The rounded products define the shared flux. Subtraction and addition
        // of that same flux are exact; no endpoint is clipped or renormalized.
        internal PipeAmount Times(double factor)
        {
            if (double.IsNaN(factor) || double.IsInfinity(factor)) throw new ArgumentOutOfRangeException(nameof(factor));
            if (factor == 0 || atoms == null) return Zero;
            if (factor == 1) return this;
            var result = Zero;
            foreach (var atom in atoms) result = result.Plus(new PipeAmount(atom * factor));
            return result;
        }
        internal void Write(BinaryWriter writer)
        {
            writer.Write(atoms?.Length ?? 0);
            if (atoms != null) foreach (var atom in atoms) writer.Write(atom);
        }
        internal static PipeAmount Read(BinaryReader reader)
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
            if(nonoverlapping)return new PipeAmount(original);
            try{foreach(var atom in original)result=result.RawPlus(new PipeAmount(atom));}
            catch(InvalidOperationException error){throw new InvalidDataException("Invalid pipe expansion.",error);}
            if((result.atoms?.Length??0)!=count)throw new InvalidDataException("Invalid pipe expansion.");
            for(var i=0;i<count;++i)
                if(BitConverter.DoubleToInt64Bits(result.atoms![i])!=BitConverter.DoubleToInt64Bits(original[i]))
                    throw new InvalidDataException("Invalid pipe expansion ordering.");
            return new PipeAmount(original);
        }
    }

    internal readonly struct PipeMaterial
    {
        internal readonly int Id;
        internal readonly double HeatCapacity, EnergyOffset;
        internal PipeMaterial(int id, double heatCapacity, double energyOffset)
        {
            if (!(heatCapacity > 0) || double.IsInfinity(heatCapacity) || double.IsNaN(energyOffset) || double.IsInfinity(energyOffset))
                throw new ArgumentException("Invalid pipe material.");
            Id = id; HeatCapacity = heatCapacity; EnergyOffset = energyOffset;
        }
    }

    internal readonly struct PipePart
    {
        internal readonly int Id;
        internal readonly PipeAmount Amount;
        internal double Mass => Amount.Value;
        internal PipePart(int id, PipeAmount amount) { Id = id; Amount = amount; }
    }

    // Immutable constituent/passive inventory; the native packet is only its
    // compatibility projection. All masses and energy survive checkpointing.
    internal sealed class PipeInventory
    {
        internal static readonly PipeInventory Empty = new PipeInventory(Array.Empty<PipePart>(), PipeAmount.Zero, Array.Empty<PipePart>());
        private readonly PipePart[] parts, germs;
        internal ReadOnlySpan<PipePart> Parts => parts;
        internal ReadOnlySpan<PipePart> Germs => germs;
        internal PipeAmount Energy { get; }
        internal PipeAmount Total { get; }
        internal double Mass => Total.Value;
        internal int Count => parts.Length;
        internal int Dominant
        {
            get { var id = 0; var most = 0.0; foreach (var part in parts) if (part.Mass > most) { most = part.Mass; id = part.Id; } return id; }
        }
        private PipeInventory(PipePart[] masses, PipeAmount energy, PipePart[] diseases)
        {
            parts = masses; germs = diseases; Energy = energy;
            var total = PipeAmount.Zero; var previous = int.MinValue;
            foreach (var part in parts)
            {
                if (part.Id <= previous || part.Amount.Sign <= 0) throw new InvalidDataException("Invalid pipe constituent inventory.");
                total = total.Plus(part.Amount); previous = part.Id;
            }
            previous = int.MinValue;
            foreach (var germ in germs)
            {
                if (germ.Id <= previous || germ.Amount.Sign <= 0) throw new InvalidDataException("Invalid pipe passive inventory.");
                previous = germ.Id;
            }
            if (parts.Length == 0 && (energy.Sign != 0 || diseases.Length != 0)) throw new InvalidDataException("Massless pipe inventory carries energy or germs.");
            Total = total;
        }
        internal static PipeInventory Single(PipeMaterial material, double mass, double temperature, int disease = 0, double count = 0)
        {
            if (mass < 0 || double.IsNaN(mass) || double.IsInfinity(mass) || !(temperature > 0) || double.IsInfinity(temperature) || count < 0 || double.IsNaN(count) || double.IsInfinity(count))
                throw new ArgumentOutOfRangeException(nameof(mass));
            if (mass == 0) return Empty;
            var amount = new PipeAmount(mass);
            var energy = amount.Times(material.HeatCapacity * temperature).Plus(amount.Times(material.EnergyOffset));
            return new PipeInventory(new[] { new PipePart(material.Id, amount) }, energy,
                count > 0 ? new[] { new PipePart(disease, new PipeAmount(count)) } : Array.Empty<PipePart>());
        }
        internal double SpeciesMass(int id)
        {
            foreach (var part in parts) if (part.Id == id) return part.Mass;
            return 0;
        }
        internal bool ContainsFraction(int id,double minimum)
        {
            if(double.IsNaN(minimum)||minimum<0||minimum>1)throw new ArgumentOutOfRangeException(nameof(minimum));
            var mass=SpeciesMass(id);return mass>0&&mass>=minimum*Mass;
        }
        internal double Temperature(Func<int, PipeMaterial> material)
        {
            if (parts.Length == 0) return 0;
            var capacity = PipeAmount.Zero; var offsets = PipeAmount.Zero;
            foreach (var part in parts)
            {
                var entry = material(part.Id);
                capacity = capacity.Plus(part.Amount.Times(entry.HeatCapacity));
                offsets = offsets.Plus(part.Amount.Times(entry.EnergyOffset));
            }
            var temperature = Energy.Minus(offsets).Value / capacity.Value;
            if (!(temperature > 0) || double.IsInfinity(temperature)) throw new InvalidOperationException("Invalid pipe temperature.");
            return temperature;
        }
        internal PipeInventory WithHeat(double heat) => parts.Length == 0 && heat != 0
            ? throw new InvalidOperationException("Cannot heat an empty pipe.") : new PipeInventory(parts, Energy.Plus(new PipeAmount(heat)), germs);
        internal PipeInventory WithDiseaseDelta(int id, double delta)
        {
            var changes = new[] { new PipePart(id, new PipeAmount(delta)) };
            return new PipeInventory(parts, Energy, Join(germs, changes, false));
        }
        internal PipeInventory Joined(PipeInventory other)
        {
            if (other.Count == 0) return this;
            if (Count == 0) return other;
            return new PipeInventory(Join(parts, other.parts, false), Energy.Plus(other.Energy), Join(germs, other.germs, false));
        }
        internal (PipeInventory Remaining, PipeInventory Parcel) Split(double maximum, Func<int, PipeMaterial> material, int? only = null,bool excluding=false)
        {
            if (maximum < 0 || double.IsNaN(maximum) || double.IsInfinity(maximum)) throw new ArgumentOutOfRangeException(nameof(maximum));
            bool Selected(PipePart part)=>!only.HasValue||(excluding?part.Id!=only.Value:part.Id==only.Value);
            var selectedAmount = PipeAmount.Zero;var selectedCount=0;
            foreach(var part in parts)if(Selected(part))
            {
                selectedAmount=selectedAmount.Plus(part.Amount);++selectedCount;
            }
            if (maximum == 0 || selectedAmount.Sign == 0) return (this, Empty);
            if (selectedCount==parts.Length && Total.Minus(new PipeAmount(maximum)).Sign <= 0) return (Empty, this);
            var wanted = new PipeAmount(maximum);
            if (wanted.Minus(selectedAmount).Sign > 0) wanted = selectedAmount;
            var fraction = Math.Min(1, wanted.Value / selectedAmount.Value);
            var amounts = new PipeAmount[parts.Length];var actual = PipeAmount.Zero;var largest = -1;
            for (var i = 0; i < parts.Length; ++i)
            {
                var part = parts[i];
                if (Selected(part))
                {
                    var amount = part.Amount.Times(fraction);
                    if (amount.Minus(part.Amount).Sign > 0) amount = part.Amount;
                    amounts[i] = amount;actual = actual.Plus(amount);
                    if (largest < 0 || part.Mass > parts[largest].Mass) largest = i;
                }
            }
            // Product rounding is corrected in a constituent with room,
            // starting with the largest. Never put a bulk-sized ulp into a
            // trace merely because that trace is last in catalogue order.
            var correction = wanted.Minus(actual);
            void Correct(int i)
            {
                if (!Selected(parts[i])) return;
                var adjusted = amounts[i].Plus(correction);
                if (adjusted.Sign < 0) adjusted = PipeAmount.Zero;
                if (adjusted.Minus(parts[i].Amount).Sign > 0) adjusted = parts[i].Amount;
                correction = correction.Minus(adjusted.Minus(amounts[i]));amounts[i] = adjusted;
            }
            Correct(largest);
            for (var i = 0; correction.Sign != 0 && i < parts.Length; ++i) if (i != largest) Correct(i);
            if (correction.Sign != 0) throw new InvalidOperationException("Pipe shared flux cannot be represented within its constituents.");
            var moved = new List<PipePart>();
            for (var i = 0; i < parts.Length; ++i) if (amounts[i].Sign > 0) moved.Add(new PipePart(parts[i].Id,amounts[i]));
            var values = moved.ToArray();
            var movedMass = PipeAmount.Zero;
            foreach (var part in values) movedMass = movedMass.Plus(part.Amount);
            var passiveFraction = movedMass.Value / Mass;
            var passive = new List<PipePart>();
            foreach (var germ in germs)
            {
                var amount = germ.Amount.Times(passiveFraction);
                if (amount.Sign > 0) passive.Add(new PipePart(germ.Id, amount));
            }
            PipeAmount energy;
            if (!only.HasValue && fraction < 1) energy = Energy.Times(fraction);
            else
            {
                var temperature = Temperature(material); energy = PipeAmount.Zero;
                foreach (var part in values)
                {
                    var item = material(part.Id);
                    energy = energy.Plus(part.Amount.Times(item.HeatCapacity * temperature)).Plus(part.Amount.Times(item.EnergyOffset));
                }
            }
            var parcel = new PipeInventory(values, energy, passive.ToArray());
            var remainder = new PipeInventory(Join(parts, values, true), Energy.Minus(energy), Join(germs, parcel.germs, true));
            return (remainder, parcel);
        }
        internal static (PipeInventory Source, PipeInventory Target, PipeInventory Parcel) Transfer(
            PipeInventory source, PipeInventory target, double requested, double capacity, Func<int, PipeMaterial> material, int? only = null,bool excluding=false)
        {
            if (capacity < 0 || double.IsNaN(capacity) || double.IsInfinity(capacity)) throw new ArgumentOutOfRangeException(nameof(capacity));
            var room = new PipeAmount(capacity).Minus(target.Total);
            var accepted = room.Sign <= 0 ? 0 : Math.Min(requested, room.Value);
            while (accepted > 0 && new PipeAmount(accepted).Minus(room).Sign > 0)
                accepted = BitConverter.Int64BitsToDouble(BitConverter.DoubleToInt64Bits(accepted)-1);
            var split = source.Split(accepted, material, only,excluding);
            return (split.Remaining, target.Joined(split.Parcel), split.Parcel);
        }
        // Native storage/messages accept float mass/temperature and one integer
        // germ kind. Dispatch only quantities that representation can carry;
        // keep every undispatched low mass, joule and passive in this owner.
        internal (PipeInventory Remaining, PipeInventory Parcel) SplitNative(int species, float maximum,
            Func<int, PipeMaterial> material, int disease = -1)
        {
            if (float.IsNaN(maximum) || float.IsInfinity(maximum) || maximum < 0) throw new ArgumentOutOfRangeException(nameof(maximum));
            var available = PipeAmount.Zero;
            foreach (var part in parts) if (part.Id == species) available = part.Amount;
            if (available.Sign == 0 || maximum == 0) return (this, Empty);
            var amount = Math.Min(maximum, (float)available.Value);
            while (amount > 0 && new PipeAmount(amount).Minus(available).Sign > 0) amount = PreviousFloat(amount);
            var temperature = (float)Temperature(material);
            var wanted = PipeAmount.Zero;
            foreach (var germ in germs) if (germ.Id == disease) wanted = germ.Amount;
            for (var attempt = 0; attempt < 16 && amount > 0; ++attempt)
            {
                var count = disease < 0 ? 0 : (int)Math.Min(int.MaxValue, Math.Floor(wanted.Value * (amount / Mass)));
                while (count > 0 && new PipeAmount(count).Minus(wanted).Sign > 0) --count;
                var parcel = Single(material(species), amount, temperature, disease, count);
                var remainingMasses = Join(parts, parcel.parts, true);
                var remainingEnergy = Energy.Minus(parcel.Energy);
                var remainingGerms = Join(germs, parcel.germs, true);
                if (remainingMasses.Length == 0 && (remainingEnergy.Sign != 0 || remainingGerms.Length != 0))
                { amount = PreviousFloat(amount); continue; }
                var remaining = new PipeInventory(remainingMasses, remainingEnergy, remainingGerms);
                try { if (remaining.Count > 0) remaining.Temperature(material); }
                catch (InvalidOperationException) { amount = PreviousFloat(amount); continue; }
                return (remaining, parcel);
            }
            return (this, Empty);
        }
        private static float PreviousFloat(float value) => value <= 0 ? 0 : BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(value) - 1);
        private static PipePart[] Join(PipePart[] left, PipePart[] right, bool subtract)
        {
            var output = new List<PipePart>(left.Length + right.Length); int a = 0, b = 0;
            while (a < left.Length || b < right.Length)
            {
                var id = a < left.Length && (b == right.Length || left[a].Id < right[b].Id) ? left[a].Id : right[b].Id;
                var amount = a < left.Length && left[a].Id == id ? left[a++].Amount : PipeAmount.Zero;
                if (b < right.Length && right[b].Id == id) amount = subtract ? amount.Minus(right[b++].Amount) : amount.Plus(right[b++].Amount);
                if (amount.Sign < 0) throw new InvalidOperationException("Pipe transfer exceeds its inventory.");
                if (amount.Sign > 0) output.Add(new PipePart(id, amount));
            }
            return output.ToArray();
        }
        internal void Write(BinaryWriter writer)
        {
            writer.Write(parts.Length); foreach (var part in parts) { writer.Write(part.Id); part.Amount.Write(writer); }
            Energy.Write(writer);
            writer.Write(germs.Length); foreach (var germ in germs) { writer.Write(germ.Id); germ.Amount.Write(writer); }
        }
        internal static PipeInventory Read(BinaryReader reader)
        {
            var count = reader.ReadInt32();
            if (count < 0 || count > 65536) throw new InvalidDataException("Invalid pipe constituent count.");
            var parts = new PipePart[count];
            for (var i = 0; i < count; ++i) parts[i] = new PipePart(reader.ReadInt32(), PipeAmount.Read(reader));
            var energy = PipeAmount.Read(reader); count = reader.ReadInt32();
            if (count < 0 || count > 256) throw new InvalidDataException("Invalid pipe passive count.");
            var germs = new PipePart[count];
            for (var i = 0; i < count; ++i) germs[i] = new PipePart(reader.ReadInt32(), PipeAmount.Read(reader));
            return new PipeInventory(parts, energy, germs);
        }
    }
}
