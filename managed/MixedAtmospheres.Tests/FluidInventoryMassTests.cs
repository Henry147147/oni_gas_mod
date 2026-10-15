using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using MixedAtmospheres.Core;

internal static class FluidInventoryMassTests
{
    internal static void Run(Action<string, Action> test, Action<bool> check, Action<Action> reject)
    {
        test("packed inventory retains input-order total mass while enumeration and copies remain ordinal", () =>
        {
            var source = new Dictionary<string, double>(StringComparer.Ordinal) { ["z"] = 1e16, ["a"] = 1, ["b"] = 1 };
            var inputSum = 0.0; foreach (var entry in source) inputSum += entry.Value;
            var inventory = new FluidInventory(source, 1e20);
            var ordinalSum = 0.0; foreach (var entry in inventory.MassesKg) ordinalSum += entry.Value;
            check(inputSum != ordinalSum && inventory.TotalMassKg == inputSum);
            check(inventory.MassesKg.Keys.SequenceEqual(new[] { "a", "b", "z" }));
            var copied = new FluidInventory(inventory.MassesKg, inventory.TotalEnergyJoules);
            check(copied.TotalMassKg == ordinalSum && copied.MassesKg.SequenceEqual(inventory.MassesKg));
            check(ReferenceEquals(copied.MassesKg, inventory.MassesKg)); // Sharing an already owned immutable map is safe.
        });

        test("packed inventory owns caller data retains minimum positive traces and filters only exact zero", () =>
        {
            var source = new Dictionary<string, double>(StringComparer.Ordinal)
            {
                ["zero"] = 0, ["negative_zero"] = -0.0, ["trace"] = double.Epsilon, ["bulk"] = 1, ["small"] = 1e-300
            };
            var inventory = new FluidInventory(source, 300);
            source.Clear(); source["trace"] = 0; source["new"] = 10;
            check(inventory.MassesKg.Count == 3 && inventory.MassesKg["trace"] == double.Epsilon && inventory.MassesKg["small"] == 1e-300);
            check(inventory.MassesKg.Keys.SequenceEqual(new[] { "bulk", "small", "trace" }) && inventory.MassesKg.Values.SequenceEqual(new[] { 1, 1e-300, double.Epsilon }));
            check(!inventory.MassesKg.ContainsKey("zero") && !inventory.MassesKg.ContainsKey("negative_zero") && !inventory.MassesKg.ContainsKey("new"));
            check(!inventory.MassesKg.TryGetValue("missing", out var absent) && absent == 0);
            reject(() => _ = inventory.MassesKg["missing"]); reject(() => inventory.MassesKg.ContainsKey(null!));
            check(inventory.MassesKg is not IDictionary<string, double> mutable || mutable.IsReadOnly);
            check(FluidInventory.Empty.MassesKg.Count == 0 && !FluidInventory.Empty.MassesKg.ContainsKey("bulk"));
            check(!((IEnumerable)FluidInventory.Empty.MassesKg).GetEnumerator().MoveNext());
        });

        test("packed mass construction validates identities and duplicates independently of source count and zero filtering", () =>
        {
            var entries = new[] { Pair("z", 3), Pair("a", double.Epsilon), Pair("zero", 0), Pair("c", 2), Pair("b", 1) };
            var inventory = new FluidInventory(new EnumerationMassSource(entries, 0), 300);
            check(inventory.MassesKg.Keys.SequenceEqual(new[] { "a", "b", "c", "z" }) && inventory.MassesKg["a"] == double.Epsilon);
            check(new FluidInventory(new EnumerationMassSource(new[] { Pair("oxygen", 0), Pair("oxygen", 2) }, 2), 300).MassesKg.Count == 1);
            reject(() => new FluidInventory(new EnumerationMassSource(new[] { Pair("oxygen", 1), Pair("oxygen", 2) }, 2), 300));
            reject(() => new FluidInventory(new Dictionary<string, double> { ["bad\nidentity"] = 0 }, 0));
            foreach (var value in new[] { -1.0, double.NaN, double.PositiveInfinity, double.NegativeInfinity })
                reject(() => new FluidInventory(new Dictionary<string, double> { ["oxygen"] = value }, 0));
        });

        test("packed ordinal lookups and enumeration remain immutable under concurrent readers", () =>
        {
            var source = new Dictionary<string, double>(StringComparer.Ordinal) { ["a"] = 1, ["A"] = 2, ["é"] = 3, ["Ω"] = 4, ["犬"] = 5 };
            for (var i = 0; i < 128; ++i) source.Add("material_" + i.ToString("D3", System.Globalization.CultureInfo.InvariantCulture), i + 0.25);
            var reference = new SortedDictionary<string, double>(source, StringComparer.Ordinal);
            var inventory = new FluidInventory(source, 1e9); source.Clear();
            Parallel.For(0, 100, iteration =>
            {
                check(inventory.MassesKg.SequenceEqual(reference));
                foreach (var expected in reference)
                    check(inventory.MassesKg.TryGetValue(expected.Key, out var actual) && actual == expected.Value);
                check(!inventory.MassesKg.TryGetValue("", out _) && !inventory.MassesKg.TryGetValue("zzzz", out _));
            });
        });
    }

    private static KeyValuePair<string, double> Pair(string key, double value) => new KeyValuePair<string, double>(key, value);

    // IReadOnlyDictionary is an enumeration boundary. Its advertised count is
    // intentionally inaccurate here to ensure allocation hints never drop entries.
    private sealed class EnumerationMassSource : IReadOnlyDictionary<string, double>
    {
        private readonly KeyValuePair<string, double>[] entries;
        public int Count { get; }
        internal EnumerationMassSource(KeyValuePair<string, double>[] entries, int count) { this.entries = entries; Count = count; }
        public IEnumerable<string> Keys => entries.Select(entry => entry.Key);
        public IEnumerable<double> Values => entries.Select(entry => entry.Value);
        public double this[string key] => entries.First(entry => entry.Key == key).Value;
        public bool ContainsKey(string key) => entries.Any(entry => entry.Key == key);
        public bool TryGetValue(string key, out double value)
        { foreach (var entry in entries) if (entry.Key == key) { value = entry.Value; return true; } value = 0; return false; }
        public IEnumerator<KeyValuePair<string, double>> GetEnumerator() => ((IEnumerable<KeyValuePair<string, double>>)entries).GetEnumerator();
        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }
}
