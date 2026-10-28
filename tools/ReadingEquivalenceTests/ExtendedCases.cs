using System.Collections;
using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using MixedAtmospheres.Core;
using MixedAtmospheres.Core.Interop;

// Hash every public property as binary data, including exact double bits and
// complete exception messages. The portable digest excludes only colour objects:
// Math.Pow/Cos may differ across the platform C runtimes. The optional full
// capture/reference comparison retains every unquantized colour bit locally.
// The expected portable digest is recorded from the original
// SortedDictionary implementation at f781290, not generated from the optimized
// reading. MA_READING_EQUIVALENCE_DUMP optionally writes the captured bytes for
// a direct comparison against a clean reference build; no game data is used.
// MA_READING_EQUIVALENCE_REFERENCE compares that original capture byte for byte
// on the same OS/runtime. MA_READING_EQUIVALENCE_LOCAL_HASH additionally checks
// the random-cell colour digest against a supplied same-platform reference.
internal static class ExtendedCases
{
    private const string Expected = "65E5BABD0BD7D914A4314301D44CA6E34F122080E92FD261D131C51A2C45FEA0";
    private static readonly Dictionary<Type, PropertyInfo[]> Properties = new();

    // Optional diagnostic, not a timing/acceptance bound. Input rows and source
    // strings are prepared before measuring only the reading construction.
    internal static int Allocations()
    {
        const int repeats = 100000;
        double checksum = 0;
        for (var count = 1; count <= 5; ++count)
        {
            var rows = Enumerable.Range(0, count).Select(i => new MixtureThermodynamicConstituent(
                "gas:" + i, NativeMatterPhase.Gas, 1, 1, 1.0 / count)).ToArray();
            MixtureThermodynamicReading Create() => new(rows, count, 1, 0, 300,
                count * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300,
                count * FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300,
                1, 1, 0, 0, "allocation-profile", "allocation-fingerprint");
            for (var i = 0; i < 1000; ++i) checksum += Create().GasMoles;
            var before = GC.GetAllocatedBytesForCurrentThread();
            for (var i = 0; i < repeats; ++i) checksum += Create().GasMoles;
            var allocated = GC.GetAllocatedBytesForCurrentThread() - before;
            Console.WriteLine($"reading allocations constituents {count} bytes_each {allocated / (double)repeats:0.0} repeats {repeats}");
        }
        GC.KeepAlive(checksum);
        return 0;
    }

    internal static int Run()
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        using var portableHash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        using var bytes = new MemoryStream();
        using var writer = new BinaryWriter(bytes, Encoding.UTF8, true);
        using var portableBytes = new MemoryStream();
        using var portableWriter = new BinaryWriter(portableBytes, Encoding.UTF8, true);
        using var dump = Environment.GetEnvironmentVariable("MA_READING_EQUIVALENCE_DUMP") is string path ? File.Create(path) : null;
        using var reference = Environment.GetEnvironmentVariable("MA_READING_EQUIVALENCE_REFERENCE") is string referencePath ? File.OpenRead(referencePath) : null;
        var referenceBytes = Array.Empty<byte>();
        var cases = 0;
        void Record(string name, Func<object> create)
        {
            bytes.SetLength(0);
            portableBytes.SetLength(0);
            writer.Write(name); portableWriter.Write(name);
            try { var value = create(); Write(writer, value, false); Write(portableWriter, value, true); }
            catch (ArgumentException error)
            {
                writer.Write(error.GetType().FullName!); writer.Write(error.Message);
                portableWriter.Write(error.GetType().FullName!); portableWriter.Write(error.Message);
            }
            writer.Flush(); portableWriter.Flush();
            var span = bytes.GetBuffer().AsSpan(0, checked((int)bytes.Length));
            hash.AppendData(span); dump?.Write(span); ++cases;
            portableHash.AppendData(portableBytes.GetBuffer().AsSpan(0, checked((int)portableBytes.Length)));
            if (reference != null)
            {
                if (referenceBytes.Length < span.Length) Array.Resize(ref referenceBytes, span.Length);
                var expected = referenceBytes.AsSpan(0, span.Length); reference.ReadExactly(expected);
                if (!span.SequenceEqual(expected)) throw new InvalidOperationException("Same-platform reference differs at " + name);
            }
        }

        foreach (var count in new[] { 0, 1, 2, 4, 16, 17, 23, 24, 25, 128, 65536, 65537 })
        foreach (var input in new[] { "array", "list", "iterator" })
        foreach (var gas in new[] { false, true })
        {
            var rows = Enumerable.Range(0, count).Select(i => new MixtureThermodynamicConstituent(
                "edge:" + (count - i).ToString("D6", CultureInfo.InvariantCulture),
                gas ? NativeMatterPhase.Gas : NativeMatterPhase.Liquid, 1, 1, 1)).ToArray();
            IEnumerable<MixtureThermodynamicConstituent> source = input switch
            {
                "list" => rows.ToList(),
                "iterator" => rows.Select(row => row),
                _ => rows
            };
            Record($"count:{count}:{input}:{gas}", () => Reading(source, count, gas));
            if (count > 0)
            {
                var duplicate = rows.Concat(new[] { rows[count / 2] });
                Record($"duplicate:{count}:{input}:{gas}", () => Reading(input == "iterator" ? duplicate : duplicate.ToArray(), count + 1, gas));
            }
        }

        // Nonzero momentum/kinetic energy, mixed phases, complete presentation
        // properties and unquantized colours at both positive and negative positions.
        var catalog = new FluidMaterialCatalog("edge-profile", new[] {
            new FluidMaterial("oxygen", NativeMatterPhase.Gas, 1, 700, 0, 0, 1e6),
            new FluidMaterial("carbon_dioxide", NativeMatterPhase.Gas, 1, 700, 0, 0, 1e6),
            new FluidMaterial("water", NativeMatterPhase.Liquid, 1, 4184, 1, 0, 0, 1e6)
        });
        var model = new MixturePresentationModel(catalog, new[] {
            new MixtureSpeciesStyle("oxygen", "Oxygen", new MixtureSrgbColor(.3, .6, .9)),
            new MixtureSpeciesStyle("carbon_dioxide", "Carbon dioxide", new MixtureSrgbColor(.5, .5, .5)),
            new MixtureSpeciesStyle("water", "Water", new MixtureSrgbColor(.9, .3, .6))
        }, new AtmosphereSettings(), "oxygen", "carbon_dioxide");
        for (var i = 0; i < 128; ++i)
        {
            var rows = new[] {
                new MixtureThermodynamicConstituent("water", NativeMatterPhase.Liquid, .5, 1, .5),
                new MixtureThermodynamicConstituent("oxygen", NativeMatterPhase.Gas, 1, 1, .5)
            };
            var pressure = FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * (200 + i) * 2;
            var reading = new MixtureThermodynamicReading(rows, 1.5, -1000 + i, 100,
                200 + i, pressure, pressure, 1, .5, .5, 0, catalog.Version, catalog.Fingerprint,
                i, "edge:cell:" + i, 3, 4);
            Record("mixed-reading:" + i, () => reading);
            var presentation = model.Describe(reading);
            Record("mixed-presentation:" + i, () => presentation);
            Record("mixed-colour:" + i, () => presentation.WorldFluidColorAt(i - 64.5, i * .125));
        }
        foreach (var identity in new[] { "", " ", "line\nbreak", new string('x', 257), "\ud800" })
            Record("invalid-identity:" + identity, () => new MixtureThermodynamicConstituent(identity, NativeMatterPhase.Liquid, 1, 1, 1));
        Record("default-constituent", () => Reading(new[] { default(MixtureThermodynamicConstituent) }, 1, false));

        var actual = Convert.ToHexString(hash.GetHashAndReset());
        var portable = Convert.ToHexString(portableHash.GetHashAndReset());
        Console.WriteLine($"Extended reading portable {portable} local-colour {actual} cases {cases}");
        if (portable != Expected) { Console.Error.WriteLine("Extended reading equivalence failed."); return 1; }
        if (reference != null && reference.ReadByte() != -1) throw new InvalidOperationException("Same-platform reference has trailing bytes.");
        Console.WriteLine("PASS array/list/iterator readings, size limits, duplicates and exact non-colour presentation properties match original bytes");
        if (reference != null) Console.WriteLine("PASS same-platform reference matches all bytes including unquantized colours");
        return 0;
    }

    private static MixtureThermodynamicReading Reading(IEnumerable<MixtureThermodynamicConstituent> rows, int count, bool gas)
    {
        var pressure = gas && count != 0 ? FluidMaterialCatalog.GasConstantJoulesPerMolKelvin * 300 : 0;
        return new MixtureThermodynamicReading(rows, count, count == 0 ? 0 : -1000, 0,
            count == 0 ? 0 : 300, pressure, pressure, Math.Max(1, count), gas ? count : 0,
            gas ? 0 : count, count == 0 ? 1 : 0, "edge-profile", "edge-fingerprint");
    }

    private static void Write(BinaryWriter writer, object? value, bool portable)
    {
        if (value == null) { writer.Write((byte)0); return; }
        var type = value.GetType(); writer.Write(type.FullName!);
        if (portable && value is MixtureSrgbColor) { writer.Write("platform-runtime-colour"); return; }
        switch (value)
        {
            case string text: writer.Write(text); return;
            case double number: writer.Write(BitConverter.DoubleToInt64Bits(number)); return;
            case bool flag: writer.Write(flag); return;
            case long integer: writer.Write(integer); return;
            case Enum enumeration: writer.Write(Convert.ToInt64(enumeration, CultureInfo.InvariantCulture)); return;
            case IEnumerable sequence:
                foreach (var entry in sequence) { writer.Write((byte)1); Write(writer, entry, portable); }
                writer.Write((byte)0); return;
        }
        if (!Properties.TryGetValue(type, out var properties))
            Properties[type] = properties = type.GetProperties(BindingFlags.Public | BindingFlags.Instance).OrderBy(p => p.Name, StringComparer.Ordinal).ToArray();
        foreach (var property in properties) { writer.Write(property.Name); Write(writer, property.GetValue(value), portable); }
    }
}
