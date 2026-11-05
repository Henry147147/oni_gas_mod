using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace MixedAtmospheres.Oni
{
    // A projected pipe mass split into ONI's movable (initial) and deferred
    // (added) stages. The two parts sum to the mass exactly, in binary32 and in
    // any wider evaluation: the part of at least half the mass has an exact
    // complement (Sterbenz), so when the movable budget is the smaller part
    // the deferred rest is rounded first and the budget is its exact
    // complement, never above the native budget.
    internal static class PipeMassStages
    {
        internal static bool Split(float mass, float movable, out float budget, out float deferred)
        {
            budget = deferred = 0;
            if (!(mass > 0) || float.IsInfinity(mass) || float.IsNaN(movable)) return false;
            var limit = Math.Min(mass, Math.Max(0, movable)); budget = limit;
            if (budget >= mass / 2) deferred = (float)(mass - budget);
            else
            {
                deferred = (float)(mass - budget); budget = (float)(mass - deferred);
                while (budget > limit) { deferred = BitConverter.Int32BitsToSingle(BitConverter.SingleToInt32Bits(deferred) + 1); budget = (float)(mass - deferred); }
            }
            return budget >= 0 && deferred >= 0 && budget <= limit && (double)budget + deferred == mass;
        }
    }

    // Exact sums of binary64 fluxes. Small components remain primary state;
    // an addition to a much larger inventory never erases its low part.
    // The atoms (increasing magnitude, non-overlapping, never zero) of an
    // expansion of up to two are held inline: the common sums allocate
    // nothing. Every operation runs the same arithmetic on the same atoms in
    // the same order as when each expansion was its own array; intermediate
    // expansions are worked in one scratch buffer, so a long result is
    // allocated once.
    internal readonly struct PipeAmount
    {
        private readonly double low, high; // count 1: low; count 2: low, high
        private readonly double[]? many;   // count 3 or more
        private readonly int count;
        internal static PipeAmount Zero => default;
        // Scratch expansions up to this many atoms live on the stack.
        private const int StackAtoms = 128;
        internal PipeAmount(double value)
        {
            if (double.IsNaN(value) || double.IsInfinity(value)) throw new ArgumentOutOfRangeException(nameof(value));
            low = value == 0 ? 0 : value; high = 0; many = null; count = value == 0 ? 0 : 1;
        }
        private PipeAmount(double single, bool _) { low = single; high = 0; many = null; count = 1; }
        private PipeAmount(double low, double high) { this.low = low; this.high = high; many = null; count = 2; }
        private PipeAmount(double[] atoms) { low = high = 0; many = atoms; count = atoms.Length; }
        // A private copy of atoms (validated by the caller).
        private static PipeAmount From(ReadOnlySpan<double> atoms) => atoms.Length switch
        {
            0 => default,
            1 => new PipeAmount(atoms[0], true),
            2 => new PipeAmount(atoms[0], atoms[1]),
            _ => new PipeAmount(atoms.ToArray()),
        };
        private double At(int index) => count > 2 ? many![index] : index == 0 ? low : high;
        private double Top => count > 2 ? many![count - 1] : count == 2 ? high : low;
        private int CopyTo(Span<double> destination)
        {
            if (count > 2) many!.AsSpan().CopyTo(destination);
            else { if (count > 0) destination[0] = low; if (count > 1) destination[1] = high; }
            return count;
        }
        internal int Sign => count == 0 ? 0 : Math.Sign(Top);
        internal double Value
        {
            get
            {
                if (count == 0) return 0;
                if (count == 1) return low;
                if (count == 2) return high + low;
                return ValueOf(many!);
            }
        }
        private static double ValueOf(ReadOnlySpan<double> atoms)
        {
            if (atoms.Length == 0) return 0;
            var count = atoms.Length - 1; var high = atoms[count]; var low = 0.0;
            while (count > 0)
            {
                var previous = high; var next = atoms[--count]; high = previous + next;
                low = next - (high - previous); if (low != 0) break;
            }
            // Resolve a half-ulp tie using the next exact lower part.
            // Adding low parts upwards can erase that decisive bit.
            if (count > 0 && ((low < 0 && atoms[count - 1] < 0) || (low > 0 && atoms[count - 1] > 0)))
            {
                var doubled = low * 2; var rounded = high + doubled;
                if (rounded - high == doubled) high = rounded;
            }
            return high;
        }
        internal PipeAmount Plus(PipeAmount other) => Sum(other, false);
        internal PipeAmount Minus(PipeAmount other) => Sum(other, true);
        // The canonical exact sum (difference when subtracting; negation is
        // exact): this expansion grown by each atom of the other, then
        // normalized, as RawPlus followed by Canonical.
        private PipeAmount Sum(PipeAmount other, bool negate)
        {
            if (other.count == 0) return Canonical();
            if (count == 0) return (negate ? other.Negated() : other).Canonical();
            if (count == 1 && other.count == 1) return RawPlus(other, negate).Canonical();
            var capacity = count + other.count;
            Span<double> atoms = capacity <= StackAtoms ? stackalloc double[capacity] : new double[capacity];
            var length = CopyTo(atoms);
            for (var i = 0; i < other.count; ++i)
            {
                var value = other.At(i);
                length = Grow(atoms, length, negate ? -value : value);
            }
            return Canonical(atoms, length);
        }
        // This expansion grown by each atom of the other, each by one exact
        // two-sum pass (no normalization).
        private PipeAmount RawPlus(PipeAmount other, bool negate)
        {
            if (other.count == 0) return this;
            if (count == 0) return negate ? other.Negated() : other;
            if (count == 1 && other.count == 1)
            {
                var a = low; var b = negate ? -other.low : other.low; var total = a + b;
                if (double.IsInfinity(total)) throw new InvalidOperationException("Pipe account overflow.");
                var virtualB = total - a; var error = (a - (total - virtualB)) + (b - virtualB);
                return error == 0 ? new PipeAmount(total) : new PipeAmount(error, total);
            }
            var capacity = count + other.count;
            Span<double> atoms = capacity <= StackAtoms ? stackalloc double[capacity] : new double[capacity];
            var length = CopyTo(atoms);
            for (var i = 0; i < other.count; ++i)
            {
                var value = other.At(i);
                length = Grow(atoms, length, negate ? -value : value);
            }
            return From(atoms.Slice(0, length));
        }
        // atoms[0, length) plus one nonzero finite value, exactly as RawPlus
        // of a one-atom amount (room for length + 1).
        private static int Add(Span<double> atoms, int length, double value)
        {
            if (length == 0) { atoms[0] = value; return 1; }
            if (length == 1)
            {
                var a = atoms[0]; var b = value; var total = a + b;
                if (double.IsInfinity(total)) throw new InvalidOperationException("Pipe account overflow.");
                var virtualB = total - a; var error = (a - (total - virtualB)) + (b - virtualB);
                if (error != 0) { atoms[0] = error; atoms[1] = total; return 2; }
                if (total != 0) { atoms[0] = total; return 1; }
                return 0;
            }
            return Grow(atoms, length, value);
        }
        // Grows atoms[0, length) by value in place (room for length + 1).
        // Each error is written at or below the item just read.
        private static int Grow(Span<double> atoms, int length, double value)
        {
            var count = 0; var carry = value;
            for (var i = 0; i < length; ++i)
            {
                var item = atoms[i]; var total = carry + item;
                if (double.IsInfinity(total)) throw new InvalidOperationException("Pipe account overflow.");
                var virtualItem = total - carry;
                var error = (carry - (total - virtualItem)) + (item - virtualItem);
                if (error != 0) atoms[count++] = error;
                carry = total;
            }
            if (carry != 0) atoms[count++] = carry;
            return count;
        }
        private PipeAmount Canonical()
        {
            if (count < 2) return this;
            // TwoSum already produced a canonical expansion when its leading
            // atom is the correctly rounded total. Avoid rebuilding that pair.
            if (count == 2 && Value == high) return this;
            Span<double> atoms = count <= StackAtoms ? stackalloc double[count] : new double[count];
            return Peel(atoms, CopyTo(atoms));
        }
        // Canonical of the expansion atoms[0, length) (the buffer is consumed).
        private static PipeAmount Canonical(Span<double> atoms, int length)
        {
            if (length < 2) return From(atoms.Slice(0, length));
            if (length == 2 && ValueOf(atoms.Slice(0, 2)) == atoms[1]) return new PipeAmount(atoms[0], atoms[1]);
            return Peel(atoms, length);
        }
        // Peel the correctly rounded value off the remainder until none is
        // left; the peeled values, smallest first, are the expansion. Each
        // peeled value is the correct rounding of the remainder's exact value
        // (ValueOf), so the result depends on that value alone: when the top
        // atom is itself the rounded value, the atoms below it are the exact
        // remainder and the subtraction pass is skipped.
        private static PipeAmount Peel(Span<double> source, int length)
        {
            var room = length + 8;
            Span<double> remaining = room <= StackAtoms ? stackalloc double[room] : new double[room];
            Span<double> descending = room <= StackAtoms ? stackalloc double[room] : new double[room];
            source.Slice(0, length).CopyTo(remaining); var produced = 0;
            while (length != 0 && Math.Sign(remaining[length - 1]) != 0)
            {
                var top = ValueOf(remaining.Slice(0, length));
                if (produced == descending.Length) descending = Larger(descending, produced);
                descending[produced++] = top;
                var subtracted = new PipeAmount(-top);
                if (BitConverter.DoubleToInt64Bits(top) == BitConverter.DoubleToInt64Bits(remaining[length - 1])) --length;
                else if (subtracted.count != 0)
                {
                    if (length == remaining.Length) remaining = Larger(remaining, length);
                    length = Add(remaining, length, subtracted.low);
                }
                if (produced > 4096) throw new InvalidOperationException("Pipe expansion normalization failed.");
            }
            if (produced == 1) return new PipeAmount(descending[0], true);
            if (produced == 2) return new PipeAmount(descending[1], descending[0]);
            var result = new double[produced];
            for (var i = 0; i < produced; ++i) result[i] = descending[produced - 1 - i];
            return new PipeAmount(result);
        }
        private static Span<double> Larger(Span<double> atoms, int length)
        {
            var larger = new double[atoms.Length * 2 + 2];
            atoms.Slice(0, length).CopyTo(larger);
            return larger;
        }
        internal PipeAmount Negated()
        {
            if (count == 0) return this;
            if (count == 1) return new PipeAmount(-low, true);
            if (count == 2) return new PipeAmount(-low, -high);
            var result = new double[count];
            for (var i = 0; i < result.Length; ++i) result[i] = -many![i];
            return new PipeAmount(result);
        }
        // The rounded products define the shared flux. Subtraction and addition
        // of that same flux are exact; no endpoint is clipped or renormalized.
        internal PipeAmount Times(double factor)
        {
            if (double.IsNaN(factor) || double.IsInfinity(factor)) throw new ArgumentOutOfRangeException(nameof(factor));
            if (factor == 0 || count == 0) return Zero;
            if (factor == 1) return this;
            if (count == 1) return new PipeAmount(low * factor);
            // Each rounded product still defines exactly the same shared
            // flux. Sum them exactly first; normalize its representation once.
            Span<double> atoms = count < StackAtoms ? stackalloc double[count + 1] : new double[count + 1];
            var length = 0;
            for (var i = 0; i < count; ++i)
            {
                var product = new PipeAmount(At(i) * factor);
                if (product.count != 0) length = Add(atoms, length, product.low);
            }
            return Canonical(atoms, length);
        }
        internal PipeAmount WeightedPopulation(double mass,double total)
        {
            if(mass<0||double.IsNaN(mass)||double.IsInfinity(mass)||!(total>0)||double.IsInfinity(total))
                throw new ArgumentOutOfRangeException("Invalid stored-population mixture weights.");
            if(count==0)return Zero;
            Span<double> atoms = count < StackAtoms ? stackalloc double[count + 1] : new double[count + 1];
            var length = 0;
            for(var i=0;i<count;++i)
            {
                var product=new PipeAmount(StoragePopulationWeight.Product(At(i),mass,total));
                if(product.count!=0)length=Add(atoms,length,product.low);
            }
            return Canonical(atoms, length);
        }
        internal void Write(BinaryWriter writer)
        {
            writer.Write(count);
            for (var i = 0; i < count; ++i) writer.Write(At(i));
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
            if(nonoverlapping)return Saved(original);
            try{foreach(var atom in original)result=result.RawPlus(new PipeAmount(atom),false);}
            catch(InvalidOperationException error){throw new InvalidDataException("Invalid pipe expansion.",error);}
            if(result.count!=count)throw new InvalidDataException("Invalid pipe expansion.");
            for(var i=0;i<count;++i)
                if(BitConverter.DoubleToInt64Bits(result.At(i))!=BitConverter.DoubleToInt64Bits(original[i]))
                    throw new InvalidDataException("Invalid pipe expansion ordering.");
            return Saved(original);
        }
        private static PipeAmount Saved(double[] atoms) => atoms.Length > 2 ? new PipeAmount(atoms) : From(atoms);
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
        // ONI's SIM_MAX_TEMPERATURE: its native pipes refuse hotter contents.
        internal const double MaximumTemperature = 10000;
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
        // Boundary parcels already carry native committed U and all passives.
        // Never reconstruct them from a float temperature compatibility view.
        internal static PipeInventory Conserved(PipePart[] masses,PipeAmount energy,PipePart[] diseases)
            =>new PipeInventory((PipePart[])masses.Clone(),energy,(PipePart[])diseases.Clone());
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
        // The one material catalogue whose temperatures are remembered, and its
        // generation: a remembered value is used only for the same catalogue
        // delegate and generation (ColonyConduits renews it whenever its
        // catalogue changes). The inventory is immutable, so the value is the
        // one Temperature would compute again; a failure is never remembered.
        private static Func<int, PipeMaterial>? rememberedMaterial;
        private static int rememberedGeneration;
        internal static void RememberTemperatures(Func<int, PipeMaterial>? material)
        { rememberedMaterial = material; System.Threading.Interlocked.Increment(ref rememberedGeneration); }
        private double temperatureValue;
        private int temperatureGeneration; // 0: none
        internal double Temperature(Func<int, PipeMaterial> material)
        {
            if (parts.Length == 0) return 0;
            if (!ReferenceEquals(material, rememberedMaterial)) return ComputeTemperature(material);
            var generation = System.Threading.Volatile.Read(ref rememberedGeneration);
            if (System.Threading.Volatile.Read(ref temperatureGeneration) == generation) return temperatureValue;
            var temperature = ComputeTemperature(material);
            temperatureValue = temperature; System.Threading.Volatile.Write(ref temperatureGeneration, generation);
            return temperature;
        }
        private double ComputeTemperature(Func<int, PipeMaterial> material)
        {
            var capacity = PipeAmount.Zero; var offsets = PipeAmount.Zero;
            foreach (var part in parts)
            {
                var entry = material(part.Id);
                capacity = capacity.Plus(part.Amount.Times(entry.HeatCapacity));
                offsets = offsets.Plus(part.Amount.Times(entry.EnergyOffset));
            }
            var temperature = Energy.Minus(offsets).Value / capacity.Value;
            if (!(temperature > 0) || double.IsInfinity(temperature))
                throw new InvalidOperationException($"Invalid pipe temperature {temperature:R} K: energy {Energy.Value:R} J, offsets {offsets.Value:R} J, " +
                    $"capacity {capacity.Value:R} J/K, parts [{string.Join(", ", parts.Select(p => $"{p.Id}:{p.Amount.Value:R}"))}].");
            return temperature;
        }
        internal PipeInventory WithHeat(double heat)=>WithHeat(new PipeAmount(heat));
        internal PipeInventory WithHeat(PipeAmount heat) => parts.Length == 0 && heat.Sign != 0
            ? throw new InvalidOperationException("Cannot heat an empty pipe.") : new PipeInventory(parts, Energy.Plus(heat), germs);
        internal PipeInventory WithDiseaseDelta(int id, double delta)=>WithDiseaseDelta(id,new PipeAmount(delta));
        internal PipeInventory WithDiseaseDelta(int id, PipeAmount delta)
        {
            var changes = new[] { new PipePart(id, delta) };
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
            // The smaller side's energy is its share at the inventory's
            // temperature and the larger side keeps the exact complement, so
            // rounding never lands in a trace: a parcel taking all but 1e-39 kg
            // must not leave that trace the bulk's rounding error as heat.
            var kept = Join(parts, values, true);
            var keptMass = PipeAmount.Zero;
            foreach (var part in kept) keptMass = keptMass.Plus(part.Amount);
            PipeAmount energy;
            if (keptMass.Minus(movedMass).Sign < 0) energy = Energy.Minus(AtTemperature(kept, material));
            else if (!only.HasValue && fraction < 1) energy = Energy.Times(fraction);
            else energy = AtTemperature(values, material);
            var parcel = new PipeInventory(values, energy, passive.ToArray());
            var remainder = new PipeInventory(kept, Energy.Minus(energy), Join(germs, parcel.germs, true));
            return (remainder, parcel);
        }
        private PipeAmount AtTemperature(PipePart[] amounts, Func<int, PipeMaterial> material)
        {
            var temperature = Temperature(material); var energy = PipeAmount.Zero;
            foreach (var part in amounts)
            {
                var item = material(part.Id);
                energy = energy.Plus(part.Amount.Times(item.HeatCapacity * temperature)).Plus(part.Amount.Times(item.EnergyOffset));
            }
            return energy;
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
                // The retained rest (with the dispatched float temperature's
                // rounding) must still be a temperature ONI's pipes can show.
                try { if (remaining.Count > 0 && !(remaining.Temperature(material) <= MaximumTemperature)) { amount = PreviousFloat(amount); continue; } }
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
