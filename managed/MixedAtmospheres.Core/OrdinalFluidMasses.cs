using System;
using System.Collections;
using System.Collections.Generic;

namespace MixedAtmospheres.Core
{
    /// <summary>Owned canonical mass entries. No caller or public collection view can mutate the backing array.</summary>
    internal sealed class OrdinalFluidMasses : IReadOnlyDictionary<string, double>
    {
        private static readonly OrdinalFluidMasses Empty = new OrdinalFluidMasses(Array.Empty<KeyValuePair<string, double>>());
        private static readonly Comparison<KeyValuePair<string, double>> CompareEntries =
            (left, right) => StringComparer.Ordinal.Compare(left.Key, right.Key);
        private readonly KeyValuePair<string, double>[] entries;
        private OrdinalFluidMasses(KeyValuePair<string, double>[] ownedEntries) { entries = ownedEntries; }

        internal ReadOnlySpan<KeyValuePair<string, double>> Entries => entries;
        public int Count => entries.Length;
        public IEnumerable<string> Keys { get { foreach (var entry in entries) yield return entry.Key; } }
        public IEnumerable<double> Values { get { foreach (var entry in entries) yield return entry.Value; } }
        public double this[string key] => TryGetValue(key, out var value) ? value : throw new KeyNotFoundException("Unknown fluid material: " + key);

        internal static OrdinalFluidMasses Create(IReadOnlyDictionary<string, double> massesKg, out double totalMass)
        {
            totalMass = 0;
            // A previously owned immutable map can be shared safely. Re-sum in
            // enumeration order: a constructor fed MassesKg previously summed
            // that ordinal order, which can differ from the original caller's.
            if (massesKg is OrdinalFluidMasses owned)
            {
                foreach (var entry in owned.entries) totalMass += entry.Value;
                return owned;
            }
            var packed = Array.Empty<KeyValuePair<string, double>>(); var count = 0;
            foreach (var entry in massesKg)
            {
                FluidChecks.Identity(entry.Key, nameof(massesKg)); FluidChecks.Nonnegative(entry.Value, nameof(massesKg));
                // Preserve the original constructor's input-order summation,
                // including positive traces. Sorting happens only afterwards.
                totalMass += entry.Value;
                if (entry.Value == 0) continue;
                if (count == packed.Length)
                {
                    // The advertised count is only a bounded allocation hint;
                    // iteration still determines the actual owned contents.
                    var capacity = count == 0 ? Math.Max(1, Math.Min(massesKg.Count, 65536)) : checked(count * 2);
                    Array.Resize(ref packed, capacity);
                }
                packed[count++] = entry;
            }
            if (count == 0) return Empty;
            if (packed.Length != count) Array.Resize(ref packed, count);
            Array.Sort(packed, CompareEntries);
            for (var i = 1; i < packed.Length; ++i)
                if (StringComparer.Ordinal.Equals(packed[i - 1].Key, packed[i].Key))
                    throw new ArgumentException("Duplicate fluid material: " + packed[i].Key, nameof(massesKg));
            return new OrdinalFluidMasses(packed);
        }

        public bool ContainsKey(string key) => TryGetValue(key, out _);
        public bool TryGetValue(string key, out double value)
        {
            if (key == null) throw new ArgumentNullException(nameof(key));
            var low = 0; var high = entries.Length - 1;
            while (low <= high)
            {
                var middle = low + (high - low) / 2;
                var comparison = StringComparer.Ordinal.Compare(entries[middle].Key, key);
                if (comparison == 0) { value = entries[middle].Value; return true; }
                if (comparison < 0) low = middle + 1; else high = middle - 1;
            }
            value = 0; return false;
        }

        public Enumerator GetEnumerator() => new Enumerator(entries);
        IEnumerator<KeyValuePair<string, double>> IEnumerable<KeyValuePair<string, double>>.GetEnumerator() => GetEnumerator();
        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

        internal struct Enumerator : IEnumerator<KeyValuePair<string, double>>
        {
            private readonly KeyValuePair<string, double>[] entries;
            private int position;
            internal Enumerator(KeyValuePair<string, double>[] entries) { this.entries = entries; position = -1; }
            public KeyValuePair<string, double> Current => position >= 0 && position < entries.Length ? entries[position] :
                throw new InvalidOperationException("The mass enumerator is not on an entry.");
            object IEnumerator.Current => Current;
            public bool MoveNext() { if (position < entries.Length) ++position; return position < entries.Length; }
            public void Reset() { position = -1; }
            public void Dispose() { }
        }
    }
}
