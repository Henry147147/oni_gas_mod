using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Numerics;

namespace MixedAtmospheres.Core
{
    /// <summary>Exact sum of finite binary64 boundary-account operands, in units of 2^-1074. This is bookkeeping, never physical state.</summary>
    public readonly struct CoupledAccountQuantity
    {
        public BigInteger Binary64Units { get; }
        private CoupledAccountQuantity(BigInteger units) { Binary64Units = units; }
        public static CoupledAccountQuantity FromDouble(double value)
        {
            FluidChecks.Finite(value, nameof(value));
            var bits = unchecked((ulong)BitConverter.DoubleToInt64Bits(value));
            var exponent = (int)((bits >> 52) & 2047); var significand = new BigInteger(bits & 0x000fffffffffffffUL);
            if (exponent != 0) significand += BigInteger.One << 52;
            var units = significand << (exponent == 0 ? 0 : exponent - 1);
            return new CoupledAccountQuantity((bits >> 63) == 0 ? units : -units);
        }
        public CoupledAccountQuantity Add(CoupledAccountQuantity value) => new CoupledAccountQuantity(Binary64Units + value.Binary64Units);
        /// <summary>Nearest binary64, ties to even. False means finite binary64 range is exceeded; the exact account remains available.</summary>
        public bool TryGetRoundedValue(out double value)
        {
            value = 0; if (Binary64Units.IsZero) return true;
            var absolute = BigInteger.Abs(Binary64Units); var bytes = absolute.ToByteArray(); var last = bytes.Length - 1;
            while (bytes[last] == 0) --last;
            var bitLength = last * 8; var topByte = bytes[last];
            while (topByte != 0) { ++bitLength; topByte >>= 1; }
            var shift = Math.Max(0, bitLength - 53); var rounded = absolute >> shift;
            if (shift != 0)
            {
                var remainder = absolute - (rounded << shift); var half = BigInteger.One << (shift - 1);
                if (remainder > half || (remainder == half && !rounded.IsEven)) ++rounded;
            }
            var mantissa = (ulong)rounded;
            if (mantissa == (1UL << 53)) { mantissa >>= 1; ++shift; }
            var exponent = mantissa >= (1UL << 52) ? (ulong)shift + 1 : 0;
            if (exponent >= 2047) return false;
            var result = exponent == 0 ? mantissa : (exponent << 52) | (mantissa - (1UL << 52));
            if (Binary64Units.Sign < 0) result |= 1UL << 63;
            value = BitConverter.Int64BitsToDouble(unchecked((long)result)); return true;
        }
    }

    /// <summary>Cumulative declared external boundary accounts, without retaining earlier frames or receipts.</summary>
    public sealed class CoupledBoundaryLedger
    {
        public static CoupledBoundaryLedger Empty { get; } = new CoupledBoundaryLedger(new Dictionary<string, CoupledAccountQuantity>(StringComparer.Ordinal),
            new Dictionary<int, CoupledAccountQuantity>(), default, default, default, default, default, default);
        public IReadOnlyDictionary<string, CoupledAccountQuantity> MassChangesKg { get; }
        public IReadOnlyDictionary<int, CoupledAccountQuantity> DiseaseChanges { get; }
        public CoupledAccountQuantity RadiationChange { get; }
        public CoupledAccountQuantity MaterialEnergyJoules { get; }
        public CoupledAccountQuantity ShaftWorkJoules { get; }
        public CoupledAccountQuantity HeatJoules { get; }
        public CoupledAccountQuantity MomentumX { get; }
        public CoupledAccountQuantity MomentumY { get; }
        private CoupledBoundaryLedger(Dictionary<string, CoupledAccountQuantity> masses, Dictionary<int, CoupledAccountQuantity> disease,
            CoupledAccountQuantity radiation, CoupledAccountQuantity material, CoupledAccountQuantity work,
            CoupledAccountQuantity heat, CoupledAccountQuantity px, CoupledAccountQuantity py)
        {
            MassChangesKg = new ReadOnlyDictionary<string, CoupledAccountQuantity>(masses); DiseaseChanges = new ReadOnlyDictionary<int, CoupledAccountQuantity>(disease);
            RadiationChange = radiation; MaterialEnergyJoules = material; ShaftWorkJoules = work; HeatJoules = heat; MomentumX = px; MomentumY = py;
        }
        internal CoupledBoundaryLedger Append(IReadOnlyDictionary<int, CoupledBoundaryReceipt> receipts)
        {
            if (receipts.Count == 0) return this;
            var masses = MassChangesKg.ToDictionary(entry => entry.Key, entry => entry.Value, StringComparer.Ordinal);
            var disease = DiseaseChanges.ToDictionary(entry => entry.Key, entry => entry.Value);
            var radiation = RadiationChange; var material = MaterialEnergyJoules; var work = ShaftWorkJoules;
            var heat = HeatJoules; var px = MomentumX; var py = MomentumY;
            foreach (var receipt in receipts.OrderBy(entry => entry.Key).Select(entry => entry.Value))
            {
                var budget = receipt.Budget;
                foreach (var entry in budget.MassChangesKg) Add(masses, entry.Key, entry.Value);
                foreach (var entry in budget.DiseaseChanges) Add(disease, entry.Key, entry.Value);
                if (disease.Count > FluidChecks.MaximumDiseaseKinds) throw new NotSupportedException("Cumulative boundary disease identity capacity exceeded.");
                radiation = radiation.Add(CoupledAccountQuantity.FromDouble(budget.RadiationChange));
                material = material.Add(CoupledAccountQuantity.FromDouble(budget.MaterialEnergyJoules));
                work = work.Add(CoupledAccountQuantity.FromDouble(budget.ShaftWorkJoules));
                heat = heat.Add(CoupledAccountQuantity.FromDouble(budget.HeatJoules));
                px = px.Add(CoupledAccountQuantity.FromDouble(budget.MomentumX)); py = py.Add(CoupledAccountQuantity.FromDouble(budget.MomentumY));
            }
            return new CoupledBoundaryLedger(masses, disease, radiation, material, work, heat, px, py);
        }
        private static void Add<TKey>(Dictionary<TKey, CoupledAccountQuantity> values, TKey key, double value) where TKey : notnull
        {
            values.TryGetValue(key, out var old); var next = old.Add(CoupledAccountQuantity.FromDouble(value));
            if (next.Binary64Units.IsZero) values.Remove(key); else values[key] = next;
        }
    }

    /// <summary>One world's immutable edit plan. Native indices are local to this world; frame submission names the exact source frame.</summary>
    public sealed class CoupledWorldMutation
    {
        public int WorldId { get; }
        public IReadOnlyList<CoupledCellEdit> Edits { get; }
        public CoupledMutationBudget Budget { get; }
        public CoupledWorldMutation(int worldId, IReadOnlyList<CoupledCellEdit> edits, CoupledMutationBudget budget)
        {
            if (edits == null) throw new ArgumentNullException(nameof(edits));
            if (edits.Count == 0 || edits.Count > 4096) throw new ArgumentException("Prepare 1..4096 cell edits per world.");
            var owned = edits.ToArray();
            if (owned.Length == 0 || owned.Length > 4096 || owned.Any(value => value == null)) throw new ArgumentException("Invalid cell edit collection.");
            WorldId = worldId; Edits = Array.AsReadOnly(owned); Budget = budget ?? throw new ArgumentNullException(nameof(budget));
        }
    }
    /// <summary>One completed frame's accepted native mutation account. Contains no native handle or other-owner reservation.</summary>
    public sealed class CoupledBoundaryReceipt
    {
        public int WorldId { get; }
        public ulong SourceRevision { get; }
        public ulong PreparedRevision => Diagnostics.Revision;
        public CoupledMutationBudget Budget { get; }
        public CoupledMutationDiagnostics Diagnostics { get; }
        internal CoupledBoundaryReceipt(int world, ulong revision, CoupledMutation mutation)
        { WorldId = world; SourceRevision = revision; Budget = mutation.Budget; Diagnostics = mutation.Diagnostics; }
    }
}
